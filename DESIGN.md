# Design Notes

## Overview
A limit order book matching engine, built from scratch in C++, extended into a
networked exchange with a live market-data feed. This document records the key
engineering decisions, the optimization work, and the system's known limitations.

## Core data model
- **Prices are integer ticks, never floating point.** Floating-point rounding would
  misprice trades and break exact comparison; integers are exact and fast.
- **Trades execute at the resting (maker) order's price**, so a crossing order
  receives price improvement — standard exchange behavior.
- **Matching is price-time priority**: best price first, and within a price level the
  oldest order fills first (FIFO).

## The matching core
The engine has a single entry point: `apply(Command)` returns a list of `Event`s.
Every operation — new order, cancel, modify — enters as a Command and leaves as
Events (Accepted, Trade, Canceled, Rejected). `apply` only routes each command to the
matching logic and packages the result; it contains no matching logic itself.

This boundary keeps the core a self-contained state machine — nothing about sockets,
storage, or testing leaks into the matching code — and it's the single seam everything
else reuses: the network server decodes a wire message into a Command and calls
`apply`; the replay driver reads commands from a file and calls `apply`; the tests
feed commands and check events. One door into the engine keeps the whole system simple.

## Determinism (and why it matters)
The engine is deterministic: the same commands in the same order always produce the
same events and the same final book. This single property enables:
- **Replay-based recovery** — replaying a command log rebuilds exact state.
- **Differential testing** — two implementations can be compared for identical output.
- **(Future) replication** — replicas processing the same command stream stay in sync.

## Performance optimization
The guiding discipline: profile first, measure every change, keep only what the data
justifies. All benchmarks were run in Release build, single-threaded, on a laptop
under WSL — a noisy environment (roughly ±20% run-to-run, and ~30% swing between
battery and plugged-in power), so figures are the best of several plugged-in runs,
and only large changes are distinguishable above the noise.

**Baseline** (naive `std::map` + `std::list` + `std::unordered_map` index):
~9.4M commands/sec at 1M commands (p50 120 ns, p99 721 ns, p99.9 ~2.5 µs). But it
**degraded badly at scale** — at 10M commands, throughput collapsed to ~2.2M/sec as
the index grew to ~180k live entries. Profiling (`perf`) showed the bottleneck was
the cancel path dominated by the `unordered_map` index plus per-command allocation —
**not** the matching loop, which was the surprising and instructive finding.

Two changes, each measured:

1. **Flat array index** (replacing `unordered_map`): order ids are sequential, so an
   id indexes directly into a vector — no hashing, no per-operation allocation, no
   rehash stalls. Result: ~2.2M → ~8M commands/sec at scale (~3.5×). Tradeoff: the
   index uses memory proportional to the id range, only sensible because ids are dense.

2. **Object pool for orders + reserved index**: orders live in a pre-allocated slot
   array linked by index (intrusive list) instead of heap-allocated `std::list` nodes,
   and the index vector is reserved up front. Result: throughput unchanged (~8M/sec,
   within noise), but the **latency tail flattened sharply** — p99.9 ~24 µs → ~2 µs,
   worst-case max ~137 ms → ~1–3 ms. The gain is in predictability, not average speed:
   it removed rare allocation/resize stalls that caused occasional multi-millisecond
   freezes. Kept on that evidence, since predictable tail latency matters in trading.

## Networking
- **32-byte fixed-size binary wire protocol.** Fixed size makes framing trivial (read
  exactly 32 bytes = one complete message), and binary is compact and parse-free versus
  text/JSON. Assumes same-endianness between client and server — a documented
  limitation; a cross-platform protocol would use network byte order.
- **TCP server with raw POSIX sockets** (`socket`/`bind`/`listen`/`accept`), chosen
  over a library to understand the layer directly.
- **`select()`-based I/O multiplexing**: one thread serves many connections by waking on
  whichever socket is ready, instead of blocking on one. This is what lets the server
  accept and process orders while pushing the feed to subscribers — all single-threaded.
- **Role-based connections**: a first byte declares submitter (0) or subscriber (1).
- **Live market-data feed**: after each order, the server broadcasts top-of-book (best
  bid/ask and sizes) to all subscribers, who display the book updating in real time.

## Verification strategy
Correctness is checked in four complementary layers, each catching what the others miss:
- **Example tests** pin down specific scenarios (a crossing buy, a partial fill, time
  priority within a level), documenting intended behavior precisely.
- **Property tests** run thousands of random command sequences and assert invariants
  after every one: the book never crosses, and the order index stays consistent with
  what's resting. These catch interaction bugs no hand-written case would construct.
- **Differential tests** run the same random commands through both the optimized engine
  and a simple reference engine (the original naive version, kept for this purpose),
  asserting identical output across 20k operations — proving the optimizations changed
  how the engine works without changing what it does.
- **Fuzz tests** feed 100k random/malformed messages through decode-and-apply and
  confirm it never crashes, backed by input validation at the protocol boundary.

Two real bugs were caught this way: a heap-use-after-free (a fill removed an order from
the book but not the index) found via AddressSanitizer during replay, and an
index-resize crash on a malformed order id found by fuzzing — fixed by validating
commands before they reach the engine.

## Known limitations
- The `select()` server still uses a blocking framing read loop, so a client sending a
  partial message can stall it; fully robust handling needs non-blocking sockets with
  per-client buffers.
- The market-data feed is top-of-book only, not full depth.
- A subscriber joining mid-stream receives no initial snapshot — only updates from when
  it connected.
- Single-node only: no journaling/persistence or replication yet.
- The wire protocol assumes same-endianness between client and server.

## Possible future work
- Full-depth market data with an initial snapshot for new subscribers.
- Journaling + snapshots for crash recovery (the determinism already supports this).
- Raft-based replication for fault tolerance.
- Non-blocking sockets for robustness against slow or partial clients.