# Limit Order Book Matching Engine

A from-scratch, low-latency limit order book and matching engine in C++, with a
binary network protocol and a live market-data feed. Built to understand how
electronic exchanges work — from the matching core out to the network layer.

## What it does
- Matches orders by **price-time priority** (best price first, then oldest first).
- Supports limit, market, IOC (immediate-or-cancel), and FOK (fill-or-kill) orders,
  plus cancel and modify.
- Exposes a **command-in / event-out** interface, making the core a deterministic
  state machine.
- Runs as a **TCP server** clients connect to over a compact binary protocol.
- Broadcasts a **live market-data feed** (top-of-book) to subscribers in real time.

## Performance
Measured on a single thread, Release build (10M mixed commands):
- ~8 million commands/sec throughput.
- Latency per command: p50 ~150 ns, p99.9 ~2 µs after optimization.
- Optimized from a naive baseline via profiling — see the design notes for the
  before/after and the reasoning.

## Verified three ways
- **Example tests** for specific matching scenarios.
- **Property tests**: invariants (no crossed book, consistent index) hold across
  thousands of random command sequences.
- **Differential tests**: the optimized engine produces identical output to a simple
  reference implementation across 20k random operations — proving optimizations
  preserved behavior.
- **Fuzz tests**: 100k random/malformed messages, hardened with input validation.

## Architecture
[command client] --orders--> [TCP server: select() multiplexing] --> [matching engine]
                                       |                                      |
[subscriber] <----- live top-of-book feed <----------------------------------+

- Single-threaded `select()`-based server multiplexes many connections at once.
- Connections declare a role: order submitter or market-data subscriber.
- 32-byte fixed binary wire protocol (see design notes for the format).

## Build and run
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure        # run the test suite
Run the live demo (three terminals):
./build/server         # terminal 1: the exchange
./build/subscriber     # terminal 2: watch the book update live
./build/client         # terminal 3: send orders

## Design decisions and limitations
See [DESIGN.md](DESIGN.md) for the engineering decisions, optimization story,
and honest limitations.

## Status
Built over 2 weeks as a learning project: correct engine → full order types →
benchmarked → optimized → verified → networked → live feed → hardened.