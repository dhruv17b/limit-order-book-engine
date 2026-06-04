#pragma once
#include <map>
#include <list>
#include <vector>
#include <functional>
#include <algorithm>
#include <iostream>
#include "ob/types.hpp"

class OrderBook {
public:
    std::vector<Trade> add_limit_order(Order order) {
        std::vector<Trade> trades;

        // FOK: must fully fill immediately, or do nothing.
        if (order.type == OrderType::FOK &&
            available_quantity(order) < order.quantity) {
            return trades;
        }

        if (order.side == Side::Buy) {
            while (order.quantity > 0 && !asks_.empty()) {
                auto best = asks_.begin();
                if (best->first > order.price) break;

                auto& level = best->second;
                while (order.quantity > 0 && !level.empty()) {
                    Order& resting = level.front();
                    uint64_t fill = std::min(order.quantity, resting.quantity);
                    trades.push_back(Trade{resting.id, order.id, resting.price, fill});
                    order.quantity   -= fill;
                    resting.quantity -= fill;
                    if (resting.quantity == 0) {
                        index_remove(resting.id);
                        level.pop_front();
                    }
                }
                if (level.empty())
                    asks_.erase(best);
            }
            if (order.quantity > 0 && order.type == OrderType::Limit) {
                auto& level = bids_[order.price];
                level.push_back(order);
                index_put(order.id, Locator{Side::Buy, order.price, std::prev(level.end())});
            }
        }
        else {  // Side::Sell
            while (order.quantity > 0 && !bids_.empty()) {
                auto best = bids_.begin();
                if (best->first < order.price) break;

                auto& level = best->second;
                while (order.quantity > 0 && !level.empty()) {
                    Order& resting = level.front();
                    uint64_t fill = std::min(order.quantity, resting.quantity);
                    trades.push_back(Trade{resting.id, order.id, resting.price, fill});
                    order.quantity   -= fill;
                    resting.quantity -= fill;
                    if (resting.quantity == 0) {
                        index_remove(resting.id);
                        level.pop_front();
                    }
                }
                if (level.empty())
                    bids_.erase(best);
            }
            if (order.quantity > 0 && order.type == OrderType::Limit) {
                auto& level = asks_[order.price];
                level.push_back(order);
                index_put(order.id, Locator{Side::Sell, order.price, std::prev(level.end())});
            }
        }

        return trades;
    }

    std::vector<Trade> add_market_order(Order order) {
        std::vector<Trade> trades;

        if (order.side == Side::Buy) {
            while (order.quantity > 0 && !asks_.empty()) {
                auto best = asks_.begin();
                auto& level = best->second;
                while (order.quantity > 0 && !level.empty()) {
                    Order& resting = level.front();
                    uint64_t fill = std::min(order.quantity, resting.quantity);
                    trades.push_back(Trade{resting.id, order.id, resting.price, fill});
                    order.quantity   -= fill;
                    resting.quantity -= fill;
                    if (resting.quantity == 0) {
                        index_remove(resting.id);
                        level.pop_front();
                    }
                }
                if (level.empty())
                    asks_.erase(best);
            }
        }
        else {  // Side::Sell
            while (order.quantity > 0 && !bids_.empty()) {
                auto best = bids_.begin();
                auto& level = best->second;
                while (order.quantity > 0 && !level.empty()) {
                    Order& resting = level.front();
                    uint64_t fill = std::min(order.quantity, resting.quantity);
                    trades.push_back(Trade{resting.id, order.id, resting.price, fill});
                    order.quantity   -= fill;
                    resting.quantity -= fill;
                    if (resting.quantity == 0) {
                        index_remove(resting.id);
                        level.pop_front();
                    }
                }
                if (level.empty())
                    bids_.erase(best);
            }
        }

        return trades;
    }

    bool cancel_order(uint64_t order_id) {
        if (order_id >= index_.size() || !index_[order_id].present)
            return false;

        Locator loc = index_[order_id];

        if (loc.side == Side::Buy) {
            auto& level = bids_[loc.price];
            level.erase(loc.it);
            if (level.empty())
                bids_.erase(loc.price);
        } else {
            auto& level = asks_[loc.price];
            level.erase(loc.it);
            if (level.empty())
                asks_.erase(loc.price);
        }

        index_[order_id].present = false;
        return true;
    }

    std::vector<Trade> modify_order(uint64_t order_id, int64_t new_price, uint64_t new_quantity) {
        if (order_id >= index_.size() || !index_[order_id].present)
            return {};

        Side side = index_[order_id].side;
        cancel_order(order_id);
        Order replacement{order_id, side, OrderType::Limit, new_price, new_quantity, 0};
        return add_limit_order(replacement);
    }

    std::vector<Event> apply(const Command& cmd) {
        std::vector<Event> events;
        events.reserve(4);

        switch (cmd.type) {
            case CommandType::New: {
                std::vector<Trade> trades =
                    (cmd.order.type == OrderType::Market)
                        ? add_market_order(cmd.order)
                        : add_limit_order(cmd.order);
                events.push_back(Event{EventType::Accepted, {}, cmd.order.id});
                for (const Trade& t : trades)
                    events.push_back(Event{EventType::Trade, t, cmd.order.id});
                break;
            }
            case CommandType::Cancel: {
                bool ok = cancel_order(cmd.target_id);
                events.push_back(Event{ok ? EventType::Canceled : EventType::Rejected,
                                       {}, cmd.target_id});
                break;
            }
            case CommandType::Modify: {
                std::vector<Trade> trades =
                    modify_order(cmd.target_id, cmd.order.price, cmd.order.quantity);
                events.push_back(Event{EventType::Accepted, {}, cmd.target_id});
                for (const Trade& t : trades)
                    events.push_back(Event{EventType::Trade, t, cmd.target_id});
                break;
            }
        }
        return events;
    }

    bool check_invariants() const {
        // 1. Book must not cross.
        if (!bids_.empty() && !asks_.empty()) {
            if (bids_.begin()->first >= asks_.begin()->first) return false;
        }

        // 2. Index consistency: every resting order has a present, correct entry.
        size_t resting_count = 0;
        for (const auto& [price, level] : bids_) {
            for (const auto& o : level) {
                if (o.id >= index_.size() || !index_[o.id].present) return false;
                if (index_[o.id].side != Side::Buy) return false;
                ++resting_count;
            }
        }
        for (const auto& [price, level] : asks_) {
            for (const auto& o : level) {
                if (o.id >= index_.size() || !index_[o.id].present) return false;
                if (index_[o.id].side != Side::Sell) return false;
                ++resting_count;
            }
        }

        // Count present slots; must equal resting orders (no stale entries).
        size_t present_count = 0;
        for (const auto& loc : index_)
            if (loc.present) ++present_count;
        if (present_count != resting_count) return false;

        return true;
    }

    void print_sizes() const {
        size_t resting = 0;
        for (const auto& [p, level] : bids_) resting += level.size();
        for (const auto& [p, level] : asks_) resting += level.size();
        std::cout << "resting orders: " << resting
                  << ", index_ size: " << index_.size() << "\n";
    }

private:
    struct Locator {
        Side side;
        int64_t price;
        std::list<Order>::iterator it;
        bool present = false;
    };

    void index_put(uint64_t id, const Locator& loc) {
        if (id >= index_.size())
            index_.resize(id + 1);
        index_[id] = loc;
        index_[id].present = true;
    }

    void index_remove(uint64_t id) {
        if (id < index_.size())
            index_[id].present = false;
    }

    uint64_t available_quantity(const Order& order) {
        uint64_t total = 0;
        if (order.side == Side::Buy) {
            for (auto& [price, level] : asks_) {
                if (price > order.price) break;
                for (auto& resting : level) {
                    total += resting.quantity;
                    if (total >= order.quantity) return total;
                }
            }
        } else {
            for (auto& [price, level] : bids_) {
                if (price < order.price) break;
                for (auto& resting : level) {
                    total += resting.quantity;
                    if (total >= order.quantity) return total;
                }
            }
        }
        return total;
    }

    std::vector<Locator> index_;
    std::map<int64_t, std::list<Order>, std::greater<int64_t>> bids_;
    std::map<int64_t, std::list<Order>, std::less<int64_t>>    asks_;
};