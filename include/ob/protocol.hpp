#pragma once
#include <cstdint>
#include <cstring>
#include <array>
#include "ob/types.hpp"

// Every command is exactly 32 bytes on the wire.
// NOTE: This assumes sender and receiver share the same byte order (endianness).
// Both run on the same x86 machine here, so we copy raw bytes without conversion.
// A production protocol would convert to network byte order (big-endian).
constexpr size_t WIRE_SIZE = 32;

using WireBytes = std::array<uint8_t, WIRE_SIZE>;

// Turn a Command into 32 bytes.
inline WireBytes serialize(const Command& cmd) {
    WireBytes buf{};   // zero-initialized, so unused fields are 0

    buf[0] = static_cast<uint8_t>(cmd.type);              // command type
    buf[1] = static_cast<uint8_t>(cmd.order.side);        // side
    buf[2] = static_cast<uint8_t>(cmd.order.type);        // order type
    // bytes 3..7 left as 0 (padding)

    // For New, the id is in cmd.order.id; for Cancel/Modify it's cmd.target_id.
    uint64_t id = (cmd.type == CommandType::New) ? cmd.order.id : cmd.target_id;

    std::memcpy(&buf[8],  &id,                  8);       // id
    std::memcpy(&buf[16], &cmd.order.price,     8);       // price
    std::memcpy(&buf[24], &cmd.order.quantity,  8);       // quantity

    return buf;
}

// Turn 32 bytes back into a Command.
inline Command deserialize(const WireBytes& buf) {
    Command cmd{};

    cmd.type       = static_cast<CommandType>(buf[0]);
    Side      side = static_cast<Side>(buf[1]);
    OrderType otype= static_cast<OrderType>(buf[2]);

    uint64_t id, quantity;
    int64_t  price;
    std::memcpy(&id,       &buf[8],  8);
    std::memcpy(&price,    &buf[16], 8);
    std::memcpy(&quantity, &buf[24], 8);

    if (cmd.type == CommandType::New) {
        cmd.order = Order{id, side, otype, price, quantity, id};
    } else {
        // Cancel / Modify: id is the target; price/quantity carry new values (Modify).
        cmd.target_id = id;
        cmd.order = Order{0, side, otype, price, quantity, 0};
    }

    return cmd;
}