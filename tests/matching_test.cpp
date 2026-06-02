#include <gtest/gtest.h>
#include "ob/order_book.hpp"

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