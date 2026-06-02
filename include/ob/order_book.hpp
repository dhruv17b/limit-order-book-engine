#pragma once
#include <map>
#include <list>
#include <vector>
#include <functional>
#include "ob/types.hpp"
#include <algorithm>
#include <unordered_map>

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
                index_.erase(resting.id);              // remove from index
                level.pop_front();               // fully filled, remove it
        }

        if (level.empty())
            asks_.erase(best);                   // clean up the empty price level
    }

    if (order.quantity > 0){
        auto& level = bids_[order.price];
        level.push_back(order);
        index_[order.id] = Locator{Side::Buy, order.price, std::prev(level.end())};
    }
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
                index_.erase(resting.id);              // remove from index
                level.pop_front();
        }

        if (level.empty())
            bids_.erase(best);
    }

    if (order.quantity > 0){
        auto& level = asks_[order.price];
        level.push_back(order);
        index_[order.id] = Locator{Side::Sell, order.price, std::prev(level.end())};
    }
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
                        index_.erase(resting.id);              // remove from index
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
                        index_.erase(resting.id);
                        level.pop_front();
                }
                if(level.empty())
                    bids_.erase(best);
            }
        }
        return trades;
    }
    bool cancel_order(uint64_t order_id){
        auto found = index_.find(order_id);
        if(found == index_.end())
            return false; // order not found
        Locator loc = found->second;

        if(loc.side == Side::Buy){
            auto& level = bids_[loc.price];
            level.erase(loc.it);
            if(level.empty())
                bids_.erase(loc.price);
        }
        else {
            auto& level = asks_[loc.price];
            level.erase(loc.it);
            if(level.empty())
                asks_.erase(loc.price);
        }
        index_.erase(order_id);
        return true;
    }

private:
    // Bids: highest price first.  Asks: lowest price first.
    struct Locator {
        Side side;
        int64_t price;
        std::list<Order>::iterator it;
    };
    std::unordered_map<uint64_t, Locator> index_; // order ID -> location in the book
    std::map<int64_t, std::list<Order>, std::greater<int64_t>> bids_;
    std::map<int64_t, std::list<Order>, std::less<int64_t>>    asks_;
};