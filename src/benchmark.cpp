#include <chrono>
#include <random>
#include <vector>
#include <iostream>
#include "ob/order_book.hpp"
#include <algorithm>

int main() {
    const int N = 1'000'000;   // number of commands to run

    // --- Phase 1: generate commands up front (NOT timed) ---
    std::mt19937 rng(42);
    std::uniform_int_distribution<int>      side_d(0, 1);
    std::uniform_int_distribution<int64_t>  price_d(990, 1010); // tight band around mid
    std::uniform_int_distribution<uint64_t> qty_d(1, 20);
    std::uniform_int_distribution<int>      action_d(0, 9);     // 0-2 new, 3-9 cancel-ish

    std::vector<Command> commands;
    commands.reserve(N);
    std::vector<uint64_t> live_ids;
    uint64_t next_id = 1;

    for (int i = 0; i < N; ++i) {
        // ~70% cancels when possible (realistic cancel-heavy flow), else new orders.
        if (action_d(rng) >= 3 && !live_ids.empty()) {
            uint64_t id = live_ids[rng() % live_ids.size()];
            commands.push_back(Command{CommandType::Cancel, {}, id});
        } else {
            uint64_t id = next_id++;
            Side side = side_d(rng) ? Side::Buy : Side::Sell;
            commands.push_back(Command{CommandType::New,
                Order{id, side, OrderType::Limit, price_d(rng), qty_d(rng), id}, 0});
            live_ids.push_back(id);
        }
    }

    // --- Phase 2: feed them through the engine (TIMED) ---
    OrderBook book;
    auto start = std::chrono::steady_clock::now();

    uint64_t total_events = 0;
    for (const auto& cmd : commands)
        total_events += book.apply(cmd).size();

    auto end = std::chrono::steady_clock::now();

    // --- Report ---
    double seconds = std::chrono::duration<double>(end - start).count();
    double per_sec = N / seconds;

    std::cout << "Ran " << N << " commands in " << seconds << " s\n";
    std::cout << "Throughput: " << (per_sec / 1e6) << " million orders/sec\n";
    std::cout << "(produced " << total_events << " events)\n";
    // --- Latency measurement: time each command individually ---
    std::vector<double> latencies_ns;
    latencies_ns.reserve(N);

    OrderBook book2;
    for (const auto& cmd : commands) {
        auto t0 = std::chrono::steady_clock::now();
        book2.apply(cmd);
        auto t1 = std::chrono::steady_clock::now();
        latencies_ns.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
    }

    std::sort(latencies_ns.begin(), latencies_ns.end());
    auto pct = [&](double p) {
        return latencies_ns[(size_t)(p / 100.0 * (latencies_ns.size() - 1))];
    };

    std::cout << "\nLatency per command:\n";
    std::cout << "  p50:   " << pct(50)   << " ns\n";
    std::cout << "  p99:   " << pct(99)   << " ns\n";
    std::cout << "  p99.9: " << pct(99.9) << " ns\n";
    std::cout << "  max:   " << latencies_ns.back() << " ns\n";

    return 0;
}