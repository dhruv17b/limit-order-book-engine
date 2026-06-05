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

// Sanity-check a decoded command. Returns true if it's plausible enough to process.
// Rejects garbage from malformed/hostile input before it reaches the engine.
inline bool is_valid(const Command& cmd) {
    // Command type must be one of the known values.
    if (cmd.type != CommandType::New &&
        cmd.type != CommandType::Cancel &&
        cmd.type != CommandType::Modify)
        return false;

    // For New, the order fields must be sane.
    if (cmd.type == CommandType::New) {
        // Side must be Buy or Sell.
        if (cmd.order.side != Side::Buy && cmd.order.side != Side::Sell)
            return false;
        // Order type must be one of the known values.
        if (cmd.order.type != OrderType::Limit &&
            cmd.order.type != OrderType::Market &&
            cmd.order.type != OrderType::IOC &&
            cmd.order.type != OrderType::FOK)
            return false;
        // Quantity must be positive and not absurd.
        if (cmd.order.quantity == 0 || cmd.order.quantity > 1'000'000'000ull)
            return false;
        // Price must be positive and within a sane range.
        if (cmd.order.price <= 0 || cmd.order.price > 1'000'000'000ll)
            return false;
        // Order id must be within a reasonable range (protects the flat index).
        if (cmd.order.id == 0 || cmd.order.id > 100'000'000ull)
            return false;
    } else {
        // Cancel / Modify: target id must be reasonable.
        if (cmd.target_id == 0 || cmd.target_id > 100'000'000ull)
            return false;
        // For Modify, also check the new price/quantity.
        if (cmd.type == CommandType::Modify) {
            if (cmd.order.quantity == 0 || cmd.order.quantity > 1'000'000'000ull)
                return false;
            if (cmd.order.price <= 0 || cmd.order.price > 1'000'000'000ll)
                return false;
        }
    }
    return true;
}