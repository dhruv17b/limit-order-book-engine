#include <gtest/gtest.h>
#include "ob/order_book.hpp"
#include<random>
#include "ob/order_pool.hpp"
#include "ob/simple_order_book.hpp"
#include "ob/protocol.hpp"
#include "ob/message_bus.hpp"

// Scenario: rest a sell of 10 @ 100, then a sell of 5 @ 101.
// Send a buy of 12 @ 101. Expect:
//   trade 10 @ 100 (maker = first sell)
//   trade  2 @ 101 (maker = second sell)
// Buy is fully filled; 3 remain resting at 101 on the ask side.

// Drives a cluster of RaftNodes through one "tick" of simulated time:
// tick every node, then deliver all resulting messages (which may produce more).
inline void simulate_step(std::vector<RaftNode>& nodes, MessageBus& bus) {
    // 1. Tick every node; collect the messages they emit.
    for (auto& node : nodes) {
        if (bus.is_crashed(node.id())) continue;   // crashed nodes don't tick
        for (const Message& m : node.tick())
            bus.send(m);
    }
    // 2. Deliver every queued message, feeding replies back into the bus,
    //    until the queue drains (messages can cascade: vote -> reply -> ...).
    Message m;
    while (bus.pop(m)) {
        if (bus.is_crashed(m.to)) continue;
        for (const Message& reply : nodes[m.to].handle_message(m))
            bus.send(reply);
    }
}

TEST(Matching, CrossesAsksByPriceTimePriority) {
    OrderBook book;

    book.add_limit_order(Order{1, Side::Sell, OrderType::Limit, 100, 10, 1});
    book.add_limit_order(Order{2, Side::Sell, OrderType::Limit, 101, 5, 2});

    std::vector<Trade> trades =
        book.add_limit_order(Order{3, Side::Buy, OrderType::Limit, 101, 12, 3});

    ASSERT_EQ(trades.size(), 2u);

    EXPECT_EQ(trades[0].maker_order_id, 1u);
    EXPECT_EQ(trades[0].taker_order_id, 3u);
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(trades[0].quantity, 10u);

    EXPECT_EQ(trades[1].maker_order_id, 2u);
    EXPECT_EQ(trades[1].taker_order_id, 3u);
    EXPECT_EQ(trades[1].price, 101);
    EXPECT_EQ(trades[1].quantity, 2u);
}
TEST(Matching, PartialFillRestRemainder){
    OrderBook book;

    book.add_limit_order(Order{1, Side::Sell, OrderType::Limit, 100, 8, 1});

    std::vector<Trade> trades =
        book.add_limit_order(Order{2, Side::Buy, OrderType::Limit, 100, 20, 2});
    
    ASSERT_EQ(trades.size(), 1u);
    
    EXPECT_EQ(trades[0].maker_order_id, 1u);
    EXPECT_EQ(trades[0].taker_order_id, 2u);
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(trades[0].quantity, 8u);
}
TEST(Matching, NoCross){
    OrderBook book;

    book.add_limit_order(Order{1, Side::Sell, OrderType::Limit, 100, 5, 1});

    std::vector<Trade> trades =
        book.add_limit_order(Order{2, Side::Buy, OrderType::Limit, 99, 5, 2});

    EXPECT_EQ(trades.size(), 0u);
}
TEST(Matching,MarketOrder){
    OrderBook book;

    book.add_limit_order(Order{1, Side::Sell, OrderType::Limit, 100, 10, 1});

    std::vector<Trade> trades =
        book.add_market_order(Order{2, Side::Buy, OrderType::Market, 0, 6, 2});

    ASSERT_EQ(trades.size(), 1u);

    EXPECT_EQ(trades[0].maker_order_id, 1u);
    EXPECT_EQ(trades[0].taker_order_id, 2u);
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(trades[0].quantity, 6u);
}
TEST(Matching, CancelRemovesRestingOrder){
    OrderBook book;

    book.add_limit_order(Order{1, Side::Sell, OrderType::Limit, 100, 10, 1});
    EXPECT_TRUE(book.cancel_order(1)); // found and removed
    EXPECT_FALSE(book.cancel_order(999)); // no such order

    std::vector<Trade> trades =
        book.add_limit_order(Order{2, Side::Buy, OrderType::Limit, 100, 10, 2});

    EXPECT_EQ(trades.size(), 0u);
}
TEST(Matching, TimePriorityWithingPriceLevel){
    OrderBook book;
     book.add_limit_order(Order{1, Side::Sell, OrderType::Limit, 100, 5, 1});
     book.add_limit_order(Order{2, Side::Sell, OrderType::Limit, 100, 5, 2});

     std::vector<Trade> trades =
         book.add_limit_order(Order{3, Side::Buy, OrderType::Limit, 100, 8, 3});

         ASSERT_EQ(trades.size(), 2u);
         EXPECT_EQ(trades[0].maker_order_id, 1u);
         EXPECT_EQ(trades[0].quantity, 5u);
         EXPECT_EQ(trades[1].maker_order_id, 2u);
         EXPECT_EQ(trades[1].quantity, 3u);
}
TEST(Matching, MarketOrderEmptyBook){
    OrderBook book;

    std::vector<Trade> trades =
        book.add_market_order(Order{1, Side::Buy, OrderType::Market, 0, 10, 1});

    EXPECT_EQ(trades.size(), 0u);
}
TEST(Matching, IOC){
    OrderBook book;
    
    book.add_limit_order(Order{1, Side::Sell, OrderType::Limit, 100, 5, 1});
    std::vector<Trade> ioc_trades =
     book.add_limit_order(Order{2, Side::Buy, OrderType::IOC, 100, 8, 2});
    ASSERT_EQ(ioc_trades.size(), 1u);
    EXPECT_EQ(ioc_trades[0].maker_order_id, 1u);
    EXPECT_EQ(ioc_trades[0].taker_order_id, 2u);
    EXPECT_EQ(ioc_trades[0].price, 100);
    EXPECT_EQ(ioc_trades[0].quantity, 5u);

    std::vector<Trade> follow_up = 
    book.add_limit_order(Order{3, Side::Sell, OrderType::Limit, 100, 3, 3});

    EXPECT_EQ(follow_up.size(), 0u);
}

TEST(Matching, FOK){
    OrderBook book;
    
    book.add_limit_order(Order{1, Side::Sell, OrderType::Limit, 100, 5, 1});
    std::vector<Trade> killed =
     book.add_limit_order(Order{2, Side::Buy, OrderType::FOK, 100, 8, 2});
    EXPECT_EQ(killed.size(), 0u);

    std::vector<Trade> proof = 
    book.add_limit_order(Order{3, Side::Buy, OrderType::Limit, 100, 5, 3});

    ASSERT_EQ(proof.size(), 1u);
    EXPECT_EQ(proof[0].quantity, 5u);

    OrderBook book2;
    book2.add_limit_order(Order{10, Side::Sell, OrderType::Limit, 100, 10, 10});
    std::vector<Trade> filled =
     book2.add_limit_order(Order{11, Side::Buy, OrderType::FOK, 100, 8, 11});
    ASSERT_EQ(filled.size(), 1u);
    EXPECT_EQ(filled[0].quantity, 8u);
}
TEST(Matching, ModifyChangesRestingOrder){
    OrderBook book;

    book.add_limit_order(Order{1, Side::Buy, OrderType::Limit, 100, 5, 1});

    std::vector<Trade> mod_trades = book.modify_order(1, 105, 7);
    EXPECT_EQ(mod_trades.size(), 0u);

    std::vector<Trade> trades = 
    book.add_limit_order(Order{2, Side::Sell, OrderType::Limit, 105, 7, 2});
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].maker_order_id, 1u);
    EXPECT_EQ(trades[0].price, 105);
    EXPECT_EQ(trades[0].quantity, 7u);
}

// The command/event interface dispatches and reports events correctly.
TEST(Commands, ApplyProducesEvents) {
    OrderBook book;

    auto e1 = book.apply(Command{CommandType::New,
                Order{1, Side::Sell, OrderType::Limit, 100, 10, 1}, 0});
    ASSERT_EQ(e1.size(), 1u);
    EXPECT_EQ(e1[0].type, EventType::Accepted);

    auto e2 = book.apply(Command{CommandType::New,
                Order{2, Side::Buy, OrderType::Limit, 100, 4, 2}, 0});
    ASSERT_EQ(e2.size(), 2u);                  // Accepted + one Trade
    EXPECT_EQ(e2[0].type, EventType::Accepted);
    EXPECT_EQ(e2[1].type, EventType::Trade);
    EXPECT_EQ(e2[1].trade.quantity, 4u);
}

// Fire many random command sequences; the book's invariants must ALWAYS hold.
TEST(Property, InvariantsHoldUnderRandomCommands) {
    std::mt19937 rng(12345);  // fixed seed → reproducible runs
    std::uniform_int_distribution<int>     side_d(0, 1);
    std::uniform_int_distribution<int64_t> price_d(95, 105);
    std::uniform_int_distribution<uint64_t> qty_d(1, 10);
    std::uniform_int_distribution<int>     action_d(0, 3); // 0-2 new, 3 cancel

    OrderBook book;
    uint64_t next_id = 1;
    std::vector<uint64_t> live_ids;  // ids we might try to cancel

    for (int step = 0; step < 5000; ++step) {
        if (action_d(rng) == 3 && !live_ids.empty()) {
            // Cancel a previously-seen id (may already be filled — that's fine).
            uint64_t id = live_ids[rng() % live_ids.size()];
            book.apply(Command{CommandType::Cancel, {}, id});
        } else {
            uint64_t id = next_id++;
            Side side = side_d(rng) ? Side::Buy : Side::Sell;
            book.apply(Command{CommandType::New,
                Order{id, side, OrderType::Limit, price_d(rng), qty_d(rng), id}, 0});
            live_ids.push_back(id);
        }

        // After EVERY command, the world must be consistent.
        ASSERT_TRUE(book.check_invariants()) << "Invariant broke at step " << step;
    }
}

// Differential test: the optimized engine must behave identically to the
// simple reference engine on every command of a random stream.
TEST(Differential, OptimizedMatchesReference) {
    std::mt19937 rng(98765);
    std::uniform_int_distribution<int>      side_d(0, 1);
    std::uniform_int_distribution<int64_t>  price_d(95, 105);
    std::uniform_int_distribution<uint64_t> qty_d(1, 10);
    std::uniform_int_distribution<int>      action_d(0, 4); // 0-2 new, 3 cancel, 4 modify

    OrderBook       fast;       // your optimized engine
    SimpleOrderBook reference;  // the naive, trusted engine

    uint64_t next_id = 1;
    std::vector<uint64_t> seen_ids;

    for (int step = 0; step < 20000; ++step) {
        Command cmd{};
        int action = action_d(rng);

        if (action == 3 && !seen_ids.empty()) {
            cmd = Command{CommandType::Cancel, {},
                          seen_ids[rng() % seen_ids.size()]};
        } else if (action == 4 && !seen_ids.empty()) {
            cmd = Command{CommandType::Modify,
                          Order{0, Side::Buy, OrderType::Limit,
                                price_d(rng), qty_d(rng), 0},
                          seen_ids[rng() % seen_ids.size()]};
        } else {
            uint64_t id = next_id++;
            Side side = side_d(rng) ? Side::Buy : Side::Sell;
            cmd = Command{CommandType::New,
                          Order{id, side, OrderType::Limit,
                                price_d(rng), qty_d(rng), id}, 0};
            seen_ids.push_back(id);
        }

        // Feed the SAME command to both engines.
        std::vector<Event> ev_fast = fast.apply(cmd);
        std::vector<Event> ev_ref  = reference.apply(cmd);

        // They must agree, event for event.
        ASSERT_EQ(ev_fast.size(), ev_ref.size())
            << "Event count differs at step " << step;
        for (size_t i = 0; i < ev_fast.size(); ++i) {
            ASSERT_TRUE(ev_fast[i] == ev_ref[i])
                << "Event " << i << " differs at step " << step;
        }
    }
}

// Serialize then deserialize must reproduce the original command exactly.
TEST(Protocol, RoundTripPreservesCommand) {
    // A New limit order.
    Command newcmd{CommandType::New,
        Order{42, Side::Buy, OrderType::Limit, 1234, 99, 42}, 0};
    Command back = deserialize(serialize(newcmd));
    EXPECT_EQ(back.type, CommandType::New);
    EXPECT_EQ(back.order.id, 42u);
    EXPECT_EQ(back.order.side, Side::Buy);
    EXPECT_EQ(back.order.type, OrderType::Limit);
    EXPECT_EQ(back.order.price, 1234);
    EXPECT_EQ(back.order.quantity, 99u);

    // A Cancel.
    Command cancel{CommandType::Cancel, {}, 7};
    Command cback = deserialize(serialize(cancel));
    EXPECT_EQ(cback.type, CommandType::Cancel);
    EXPECT_EQ(cback.target_id, 7u);

    // A Modify (target id + new price/qty).
    Command modify{CommandType::Modify,
        Order{0, Side::Sell, OrderType::Limit, 555, 12, 0}, 9};
    Command mback = deserialize(serialize(modify));
    EXPECT_EQ(mback.type, CommandType::Modify);
    EXPECT_EQ(mback.target_id, 9u);
    EXPECT_EQ(mback.order.price, 555);
    EXPECT_EQ(mback.order.quantity, 12u);
}

// Fuzz: feed random/malformed 32-byte messages through deserialize + apply.
// The engine must never crash, regardless of input.
TEST(Fuzz, RandomBytesDoNotCrash) {
    std::mt19937 rng(13579);
    std::uniform_int_distribution<int> byte_d(0, 255);

    OrderBook book;
    for (int iter = 0; iter < 100000; ++iter) {
        WireBytes buf;
        for (auto& b : buf) b = (uint8_t)byte_d(rng);

        Command cmd = deserialize(buf);
        if (is_valid(cmd))      // only apply plausible commands
            book.apply(cmd);
    }
    SUCCEED();
}

// A 3-node cluster should elect exactly one leader.
TEST(Raft, ElectsALeader) {
    const int N = 3;
    MessageBus bus;
    std::vector<RaftNode> nodes;
    for (int i = 0; i < N; ++i)
        nodes.emplace_back(i, N);

    // Run enough steps for an election timeout to fire and resolve.
    for (int step = 0; step < 50; ++step)
        simulate_step(nodes, bus);

    // Count leaders. There must be exactly one.
    int leaders = 0;
    for (auto& node : nodes)
        if (node.role() == RaftRole::Leader) leaders++;

    EXPECT_EQ(leaders, 1) << "expected exactly one leader";

    // All nodes should agree on the same term.
    uint64_t term = nodes[0].current_term();
    for (auto& node : nodes)
        EXPECT_EQ(node.current_term(), term) << "terms should converge";
}

// If the leader crashes, the cluster must elect a new one.
TEST(Raft, ReelectsAfterLeaderCrash) {
    const int N = 3;
    MessageBus bus;
    std::vector<RaftNode> nodes;
    for (int i = 0; i < N; ++i) nodes.emplace_back(i, N);

    // Elect an initial leader.
    for (int step = 0; step < 50; ++step) simulate_step(nodes, bus);

    int leader_id = -1;
    for (auto& node : nodes)
        if (node.role() == RaftRole::Leader) leader_id = node.id();
    ASSERT_NE(leader_id, -1) << "should have a leader first";
    uint64_t old_term = nodes[leader_id].current_term();

    // Crash the leader.
    bus.crash(leader_id);

    // Run more steps; a surviving follower should time out and win a new term.
    for (int step = 0; step < 100; ++step) simulate_step(nodes, bus);

    int new_leaders = 0, new_leader_id = -1;
    for (auto& node : nodes) {
        if (bus.is_crashed(node.id())) continue;
        if (node.role() == RaftRole::Leader) { new_leaders++; new_leader_id = node.id(); }
    }
    EXPECT_EQ(new_leaders, 1) << "a new leader should emerge among survivors";
    EXPECT_NE(new_leader_id, leader_id) << "new leader should differ from crashed one";
    EXPECT_GT(nodes[new_leader_id].current_term(), old_term)
        << "new leader should be in a later term";
}