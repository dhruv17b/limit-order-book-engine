#pragma once
#include <cstdint>

enum class Side { Buy, Sell };
enum class OrderType { Limit, Market };

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