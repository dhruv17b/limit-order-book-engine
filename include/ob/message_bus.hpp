#pragma once
#include <vector>
#include <deque>
#include <set>
#include "ob/raft_node.hpp"

// Carries messages between RaftNodes in one process.
// We control delivery, so we can simulate delays, drops, and crashes.
class MessageBus {
public:
    // A node "sends" by handing the message to the bus.
    void send(const Message& msg) {
        // Don't deliver to or from a crashed node.
        if (crashed_.count(msg.to) || crashed_.count(msg.from)) return;
        queue_.push_back(msg);
    }

    // Pull the next message waiting for delivery (or report empty).
    bool pop(Message& out) {
        if (queue_.empty()) return false;
        out = queue_.front();
        queue_.pop_front();
        return true;
    }

    bool empty() const { return queue_.empty(); }

    // Simulate failures:
    void crash(int node_id)   { crashed_.insert(node_id); }
    void recover(int node_id) { crashed_.erase(node_id); }
    bool is_crashed(int id) const { return crashed_.count(id) > 0; }

private:
    std::deque<Message> queue_;   // messages awaiting delivery
    std::set<int>       crashed_; // nodes currently "down"
};