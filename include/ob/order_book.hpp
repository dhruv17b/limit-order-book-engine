#pragma once
#include <map>
#include <vector>
#include <functional>
#include <algorithm>
#include <iostream>
#include "ob/types.hpp"
#include "ob/order_pool.hpp"

class OrderBook {
public:
OrderBook() {
        index_.reserve(4'000'000);   // avoid repeated resize+copy as ids grow
        pool_.reserve(4'000'000);    // pre-allocate pool slots up front too
    }
    std::vector<Trade> add_limit_order(Order order) {
        std::vector<Trade> trades;

        if (order.type == OrderType::FOK &&
            available_quantity(order) < order.quantity) {
            return trades;
        }

        if (order.side == Side::Buy)
            match(order, asks_, trades, /*buy=*/true);
        else
            match(order, bids_, trades, /*buy=*/false);

        if (order.quantity > 0 && order.type == OrderType::Limit) {
            if (order.side == Side::Buy) rest(order, bids_, Side::Buy);
            else                          rest(order, asks_, Side::Sell);
        }
        return trades;
    }

    std::vector<Trade> add_market_order(Order order) {
        std::vector<Trade> trades;
        if (order.side == Side::Buy)
            match(order, asks_, trades, /*buy=*/true, /*market=*/true);
        else
            match(order, bids_, trades, /*buy=*/false, /*market=*/true);
        return trades;
    }

   bool cancel_order(uint64_t order_id) {
        if (order_id >= index_.size() || !index_[order_id].present)
            return false;

        Locator loc = index_[order_id];

        if (loc.side == Side::Buy) {
            auto map_it = bids_.find(loc.price);
            if (map_it != bids_.end()) {
                unlink(map_it->second, loc.node);
                pool_.free(loc.node);
                if (map_it->second.head == OrderPool::NIL)
                    bids_.erase(map_it);
            }
        } else {
            auto map_it = asks_.find(loc.price);
            if (map_it != asks_.end()) {
                unlink(map_it->second, loc.node);
                pool_.free(loc.node);
                if (map_it->second.head == OrderPool::NIL)
                    asks_.erase(map_it);
            }
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

    // Top of book: best bid and best ask prices (0 if that side is empty).
struct TopOfBook {
    int64_t best_bid;
    uint64_t bid_qty;
    int64_t best_ask;
    uint64_t ask_qty;
};

TopOfBook top_of_book() const {
    TopOfBook t{0, 0, 0, 0};
    if (!bids_.empty()) {
        auto it = bids_.begin();
        t.best_bid = it->first;
        // sum quantity at best bid level
        for (uint32_t n = it->second.head; n != OrderPool::NIL; n = pool_[n].next)
            t.bid_qty += pool_[n].order.quantity;
    }
    if (!asks_.empty()) {
        auto it = asks_.begin();
        t.best_ask = it->first;
        for (uint32_t n = it->second.head; n != OrderPool::NIL; n = pool_[n].next)
            t.ask_qty += pool_[n].order.quantity;
    }
    return t;
}

    bool check_invariants() const {
        if (!bids_.empty() && !asks_.empty())
            if (bids_.begin()->first >= asks_.begin()->first) return false;

        size_t resting_count = 0;
        for (const auto& [price, level] : bids_)
            for (uint32_t n = level.head; n != OrderPool::NIL; n = pool_[n].next) {
                uint64_t id = pool_[n].order.id;
                if (id >= index_.size() || !index_[id].present) return false;
                if (index_[id].side != Side::Buy) return false;
                ++resting_count;
            }
        for (const auto& [price, level] : asks_)
            for (uint32_t n = level.head; n != OrderPool::NIL; n = pool_[n].next) {
                uint64_t id = pool_[n].order.id;
                if (id >= index_.size() || !index_[id].present) return false;
                if (index_[id].side != Side::Sell) return false;
                ++resting_count;
            }

        size_t present_count = 0;
        for (const auto& loc : index_) if (loc.present) ++present_count;
        return present_count == resting_count;
    }

    void print_sizes() const {
        size_t resting = 0;
        for (const auto& [p, l] : bids_)
            for (uint32_t n = l.head; n != OrderPool::NIL; n = pool_[n].next) ++resting;
        for (const auto& [p, l] : asks_)
            for (uint32_t n = l.head; n != OrderPool::NIL; n = pool_[n].next) ++resting;
        std::cout << "resting orders: " << resting
                  << ", index_ size: " << index_.size() << "\n";
    }

private:
    struct Level {
        uint32_t head = OrderPool::NIL;  // oldest order, fills first
        uint32_t tail = OrderPool::NIL;  // newest order, appended here
    };

    struct Locator {
        Side     side;
        int64_t  price;
        uint32_t node;          // pool index of the resting order
        bool     present = false;
    };

    using BidMap = std::map<int64_t, Level, std::greater<int64_t>>;
    using AskMap = std::map<int64_t, Level, std::less<int64_t>>;

    // Append a node to the end of a level's intrusive list (preserves time priority).
    void link_back(Level& level, uint32_t node) {
        pool_[node].prev = level.tail;
        pool_[node].next = OrderPool::NIL;
        if (level.tail != OrderPool::NIL) pool_[level.tail].next = node;
        else                              level.head = node;   // was empty
        level.tail = node;
    }

    // Remove a node from anywhere in a level's list in O(1) using prev/next.
    void unlink(Level& level, uint32_t node) {
        uint32_t p = pool_[node].prev, n = pool_[node].next;
        if (p != OrderPool::NIL) pool_[p].next = n; else level.head = n;
        if (n != OrderPool::NIL) pool_[n].prev = p; else level.tail = p;
    }

    void index_put(uint64_t id, const Locator& loc) {
        if (id >= index_.size()) index_.resize(id + 1);
        index_[id] = loc;
        index_[id].present = true;
    }
    void index_remove(uint64_t id) {
        if (id < index_.size()) index_[id].present = false;
    }

    template <class Book>
    void rest(const Order& order, Book& book, Side side) {
        Level& level = book[order.price];
        uint32_t node = pool_.alloc(order);
        link_back(level, node);
        index_put(order.id, Locator{side, order.price, node});
    }

    // Match `order` against the opposite book. buy=true means we cross when
    // best price <= order.price; market skips the price check.
    template <class Book>
    void match(Order& order, Book& book, std::vector<Trade>& trades,
               bool buy, bool market = false) {
        while (order.quantity > 0 && !book.empty()) {
            auto best = book.begin();
            int64_t price = best->first;
            if (!market) {
                if (buy  && price > order.price) break;
                if (!buy && price < order.price) break;
            }
            Level& level = best->second;
            while (order.quantity > 0 && level.head != OrderPool::NIL) {
                uint32_t node = level.head;
                Order& resting = pool_[node].order;
                uint64_t fill = std::min(order.quantity, resting.quantity);
                trades.push_back(Trade{resting.id, order.id, resting.price, fill});
                order.quantity   -= fill;
                resting.quantity -= fill;
                if (resting.quantity == 0) {
                    index_remove(resting.id);
                    unlink(level, node);
                    pool_.free(node);
                }
            }
            if (level.head == OrderPool::NIL) book.erase(best);
        }
    }

    uint64_t available_quantity(const Order& order) {
        uint64_t total = 0;
        auto scan = [&](auto& book, bool buy) {
            for (auto& [price, level] : book) {
                if (buy  && price > order.price) break;
                if (!buy && price < order.price) break;
                for (uint32_t n = level.head; n != OrderPool::NIL; n = pool_[n].next) {
                    total += pool_[n].order.quantity;
                    if (total >= order.quantity) return;
                }
            }
        };
        if (order.side == Side::Buy) scan(asks_, true);
        else                          scan(bids_, false);
        return total;
    }

    OrderPool pool_;
    std::vector<Locator> index_;
    BidMap bids_;
    AskMap asks_;
};