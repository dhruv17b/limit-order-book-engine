#include <gtest/gtest.h>
#include "ob/order_book.hpp"
#include<random>
#include "ob/order_pool.hpp"

// Scenario: rest a sell of 10 @ 100, then a sell of 5 @ 101.
// Send a buy of 12 @ 101. Expect:
//   trade 10 @ 100 (maker = first sell)
//   trade  2 @ 101 (maker = second sell)
// Buy is fully filled; 3 remain resting at 101 on the ask side.
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