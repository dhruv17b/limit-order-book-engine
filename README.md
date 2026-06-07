# Limit Order Book Matching Engine

A from-scratch, low-latency limit order book and matching engine in C++, extended
into a networked exchange with a live market-data feed and Raft-based replication.
Built to understand how electronic exchanges work — from the matching core, out to
the network layer, up to fault-tolerant multi-node consensus.

## What it does
- Matches orders by **price-time priority** (best price first, then oldest first).
- Supports **limit, market, IOC, and FOK** orders, plus **cancel** and **modify**.
- Exposes a **command-in / event-out** interface, making the core a deterministic
  state machine.
- Runs as a **TCP server** clients connect to over a compact binary protocol.
- Broadcasts a **live market-data feed** (top-of-book) to subscribers in real time.
- Replicates across a cluster via **Raft consensus** — leader election, failover,
  and log replication — so replicas survive node failure and converge to identical state.

## Performance
Single thread, Release build, measured on a laptop under WSL (a noisy environment,
~±20% run-to-run; figures are best-of-several plugged-in runs):
- ~8 million commands/sec throughput at 10M-command scale.
- After optimization, latency tail flattened to p99.9 ~2 µs (from ~24 µs), worst-case
  max ~1–3 ms (from ~137 ms).
- Over the network with a synchronous request-reply client: ~6,000 orders/sec, bounded
  by round-trip latency (not the engine) — see design notes.

## Verified five ways
- **Example tests** for specific matching scenarios.
- **Property tests**: invariants (no crossed book, consistent index) hold across
  thousands of random command sequences.
- **Differential tests**: the optimized engine produces identical output to a simple
  reference implementation across 20k random operations.
- **Fuzz tests**: 100k random/malformed messages, hardened with input validation.
- **Raft tests**: leader election, re-election after leader crash, log replication
  with majority commit, and replicas converging to identical order books.

20 passing tests total.

## Architecture
[command client] --orders--> [TCP server: select() multiplexing] --> [matching engine]
|                                      |
[subscriber] <----- live top-of-book feed <----------------------------------+
[Raft cluster] : leader replicates a committed command log to followers;
each replica applies it to its own engine -> identical state.
- Single-threaded `select()`-based server multiplexes many connections at once.
- Connections declare a role: order submitter or market-data subscriber.
- 32-byte fixed binary wire protocol (see design notes).
- Raft (simulated in a single process with a controllable message bus) provides
  consensus on the ordered command log.

## Build and test
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure

## Run the live demo (three terminals)
./build/server         # terminal 1: the exchange
./build/subscriber     # terminal 2: watch the book update live
./build/client         # terminal 3: send orders

## Design decisions and limitations
See [DESIGN.md](DESIGN.md) for the engineering decisions, the optimization story,
the Raft implementation, and honest limitations.

## Status
Built over an extended project: correct engine → full order types → benchmarked →
optimized → verified → networked → live feed → hardened → Raft replication.