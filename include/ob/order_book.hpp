#pragma once
#include <map>
#include <list>
#include <vector>
#include <functional>
#include "ob/types.hpp"
#include <algorithm>

class OrderBook {
public:
    // Match the incoming order against the book; return generated trades.
    // Any unfilled remainder rests on the book.
    std::vector<Trade> add_limit_order(Order order) {
        std::vector<Trade> trades;

        // TODO — this is what you write.
        //
             if (order.side == Side::Buy) {
    while (order.quantity > 0 && !asks_.empty()) {
        auto best = asks_.begin();              // lowest ask (comparator sorts ascending)
        if (best->first > order.price) break;   // no cross — stop matching

        auto& level = best->second;             // the FIFO list at this price
        while (order.quantity > 0 && !level.empty()) {
            Order& resting = level.front();      // oldest order first = time priority
            uint64_t fill = std::min(order.quantity, resting.quantity);

            trades.push_back(Trade{resting.id, order.id, resting.price, fill});

            order.quantity   -= fill;
            resting.quantity -= fill;

            if (resting.quantity == 0)
                level.pop_front();               // fully filled, remove it
        }

        if (level.empty())
            asks_.erase(best);                   // clean up the empty price level
    }

    if (order.quantity > 0)
        bids_[order.price].push_back(order);     // remainder rests on the book
}
else {  // Side::Sell
    while (order.quantity > 0 && !bids_.empty()) {
        auto best = bids_.begin();              // highest bid (comparator sorts descending)
        if (best->first < order.price) break;   // no cross — buyer won't pay enough

        auto& level = best->second;
        while (order.quantity > 0 && !level.empty()) {
            Order& resting = level.front();      // oldest first = time priority
            uint64_t fill = std::min(order.quantity, resting.quantity);

            trades.push_back(Trade{resting.id, order.id, resting.price, fill});

            order.quantity   -= fill;
            resting.quantity -= fill;

            if (resting.quantity == 0)
                level.pop_front();
        }

        if (level.empty())
            bids_.erase(best);
    }

    if (order.quantity > 0)
        asks_[order.price].push_back(order);     // remainder rests on the ask side
}
return trades;

        
    }
    std::vector<Trade> add_market_order(Order order){
        std::vector<Trade> trades;

        if(order.side== Side::Buy){
            while(order.quantity > 0 && !asks_.empty()){
                auto best = asks_.begin();
                auto& level = best->second;
                while(order.quantity > 0 && !level.empty()){
                    Order& resting = level.front();
                    uint64_t fill = std::min(order.quantity, resting.quantity);

                    trades.push_back(Trade{resting.id, order.id, resting.price, fill});

                    order.quantity -= fill;
                    resting.quantity -= fill;

                    if(resting.quantity == 0)
                        level.pop_front();
                }
                if(level.empty())
                    asks_.erase(best);
            }
        }
        else {
            while(order.quantity > 0 && !bids_.empty()){
                auto best = bids_.begin();
                auto& level = best->second;
                while(order.quantity > 0 && !level.empty()){
                    Order& resting = level.front();
                    uint64_t fill = std::min(order.quantity, resting.quantity);

                    trades.push_back(Trade{resting.id, order.id, resting.price, fill});

                    order.quantity -= fill;
                    resting.quantity -= fill;

                    if(resting.quantity == 0)
                        level.pop_front();
                }
                if(level.empty())
                    bids_.erase(best);
            }
        }
        return trades;
    }

private:
    // Bids: highest price first.  Asks: lowest price first.
    std::map<int64_t, std::list<Order>, std::greater<int64_t>> bids_;
    std::map<int64_t, std::list<Order>, std::less<int64_t>>    asks_;
};