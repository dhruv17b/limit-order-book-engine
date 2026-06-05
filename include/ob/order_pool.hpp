#pragma once
#include <vector>
#include <cstdint>
#include "ob/types.hpp"

// A pool of Order slots linked by index (intrusive list).
// Allocation/free reuse slots via a free list — no per-order heap allocation.
class OrderPool {
public:
    static constexpr uint32_t NIL = UINT32_MAX;

    struct Node {
        Order    order;
        uint32_t next = NIL;   // index of next node in its list (or NIL)
        uint32_t  prev = NIL;   // index of prev node in its list (or NIL)
    };

    // Grab a free slot, store the order in it, return its index.
    uint32_t alloc(const Order& o) {
        uint32_t idx;
        if (free_head_ != NIL) {
            idx = free_head_;              // reuse a freed slot
            free_head_ = nodes_[idx].next;
        } else {
            idx = (uint32_t)nodes_.size(); // grow if no free slot
            nodes_.push_back(Node{});
        }
        nodes_[idx].order = o;
        nodes_[idx].next  = NIL;
        return idx;
    }

    // Return a slot to the free list for reuse.
    void free(uint32_t idx) {
        nodes_[idx].next = free_head_;
        free_head_ = idx;
    }

    // Access a node by index.
    Node&       operator[](uint32_t idx)       { return nodes_[idx]; }
    const Node& operator[](uint32_t idx) const { return nodes_[idx]; }

    void reserve(size_t n) { nodes_.reserve(n); }

private:
    std::vector<Node> nodes_;
    uint32_t free_head_ = NIL;   // first reusable slot, or NIL
};