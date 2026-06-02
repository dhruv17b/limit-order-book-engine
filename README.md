# Limit Order Book Matching Engine

A from-scratch limit order book and matching engine in C++, built to understand
how exchanges match orders and to practice low-latency systems design.

## Status (Week 1)
- Limit orders with price-time priority matching (both sides)
- Market orders (fill at any price, no resting remainder)
- Cancel by order id, backed by an order-id index for O(1) lookup
- Partial fills, no-cross, and time-priority cases covered by unit tests

## Design notes
- Prices are stored as integer ticks, never floating point, to avoid rounding errors.
- A trade executes at the resting (maker) order's price; price improvement goes to the taker.
- The book uses two sorted maps (bids high-to-low, asks low-to-high), each price
  level holding a FIFO list of orders so the oldest at a price fills first.
- An order-id index maps each resting order to its exact list position, so cancels
  don't scan the book. (This naive structure is correct-first; Week 4 replaces it for speed.)

## Build and test
```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```