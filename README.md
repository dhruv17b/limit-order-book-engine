# Limit Order Book Matching Engine

A from-scratch limit order book and matching engine in C++, built to understand
how exchanges match orders and to practice low-latency systems design.

## Status (Till now)
- Limit orders with price-time priority matching (both sides)
- Market orders (fill at any price, no resting remainder)
- IOC (immediate-or-cancel): fills what it can, drops the rest
- FOK (fill-or-kill): fully fills or does nothing, via a pre-trade liquidity check
- Cancel by order id, backed by an order-id index for fast lookup
- Modify (cancel + re-add; loses time priority, as a real venue would)
- Command-in / event-out interface (`apply`): the engine as a deterministic state machine
- Replay driver: reconstructs book state by replaying a command file
- Property tests: thousands of random command sequences verify invariants hold

## Design notes
- Prices are stored as integer ticks, never floating point, to avoid rounding errors.
- A trade executes at the resting (maker) order's price; price improvement goes to the taker.
- The book uses two sorted maps (bids high-to-low, asks low-to-high), each price
  level holding a FIFO list of orders so the oldest at a price fills first.
- An order-id index maps each resting order to its exact list position. The invariant:
  an order's index entry exists exactly while it rests, and is removed the instant it
  leaves the book — whether by fill or cancel. (Property tests enforce this.)
- The engine is deterministic: same commands in the same order produce the same events
  and the same final book. This is what makes replay-based recovery possible.
- This structure is correct-first, not fast; Week 4 replaces it for performance.

## Build and test
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

## Replay a command file
./build/replay commands.txt

## Performance baseline (naive std::map/std::list)
- ~9.3 million commands/sec throughput (1M mixed new+cancel commands, Release build, single thread)
- Measured before any optimization; The next phase targets improving this.
- Latency per command (same run): p50 120 ns, p99 721 ns, p99.9 2483 ns, max ~2.1 ms
- The large p50→tail spread points to heap allocation stalls — the next phase target.

## Profiled bottlenecks(Current phase)
- cancel_order is the hottest function (~28% self time) under cancel-heavy flow.
- Dominated by std::unordered_map (the order-id index) lookups/erases.
- Per-command std::vector<Event> allocation is a second major cost.
- These are the next phase optimization targets.