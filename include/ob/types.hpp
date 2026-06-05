#pragma once
#include <cstdint>

enum class Side { Buy, Sell };
enum class OrderType { Limit, Market, IOC, FOK };

struct Order {
    uint64_t  id;
    Side      side;
    OrderType type;
    int64_t   price;     // integer ticks — never a double
    uint64_t  quantity;  // remaining quantity
    uint64_t  timestamp; // arrival order; a counter is fine
};

struct Trade {
    uint64_t maker_order_id;
    uint64_t taker_order_id;
    int64_t  price;      // the maker's price
    uint64_t quantity;
};

enum class CommandType { New, Cancel, Modify };

struct Command {
    CommandType type;
    Order       order;      // New: the order. Modify: id + new price/qty in here.
    uint64_t    target_id;  // Cancel / Modify: which existing order.
};

enum class EventType { Accepted, Rejected, Trade, Canceled };

struct Event {
    EventType type;
    Trade     trade;     // meaningful only when type == Trade
    uint64_t  order_id;  // the order this event is about
    
};
inline bool operator==(const Event& a, const Event& b) {
    return a.type == b.type
        && a.order_id == b.order_id
        && a.trade.maker_order_id == b.trade.maker_order_id
        && a.trade.taker_order_id == b.trade.taker_order_id
        && a.trade.price == b.trade.price
        && a.trade.quantity == b.trade.quantity;
}