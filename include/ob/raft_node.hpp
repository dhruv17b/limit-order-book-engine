#pragma once
#include <cstdint>
#include <vector>
#include "ob/types.hpp"   // reuse your Command type for log entries
#include <cstdlib>

enum class RaftRole { Follower, Candidate, Leader };

// One entry in the replicated log: a command plus the term it was created in.
struct LogEntry {
    uint64_t term;
    Command  command;
};

enum class MsgType {
    RequestVote,
    RequestVoteReply,
    AppendEntries,
    AppendEntriesReply
};

struct Message {
    MsgType  type;
    int      from;
    int      to;
    uint64_t term;          // sender's currentTerm — every message carries it

    // --- RequestVote fields ---
    uint64_t last_log_index = 0;   // candidate's log info (for the safety check)
    uint64_t last_log_term  = 0;

    // --- RequestVoteReply ---
    bool vote_granted = false;

    // --- AppendEntries fields ---
    uint64_t prev_log_index = 0;
    uint64_t prev_log_term  = 0;
    std::vector<LogEntry> entries;   // empty = heartbeat
    uint64_t leader_commit = 0;

    // --- AppendEntriesReply ---
    bool success = false;
};

class RaftNode {
public:
    // A client proposes a command to the leader. Only the leader accepts it;
    // it appends to its own log (replication happens via AppendEntries on ticks).
    // Returns true if accepted (we are the leader).
    bool propose(const Command& cmd) {
        if (role_ != RaftRole::Leader) return false;
        log_.push_back(LogEntry{current_term_, cmd});
        return true;
    }
    size_t log_size() const { return log_.size(); }
    uint64_t commit_index() const { return commit_index_; }
    bool is_leader() const { return role_ == RaftRole::Leader; }
    
    RaftNode(int id, int cluster_size)
        : id_(id), cluster_size_(cluster_size) {
            reset_election_timer();
        }

    int id() const { return id_; }
    RaftRole role() const { return role_; }
    uint64_t current_term() const { return current_term_; }
    // Advance this node's clock by one unit. Returns any messages to send.
    std::vector<Message> tick() {
        std::vector<Message> out;

        // Leaders don't time out; they send heartbeats instead.
        if (role_ == RaftRole::Leader) {
            heartbeat_elapsed_++;
            if (heartbeat_elapsed_ >= HEARTBEAT_INTERVAL) {
                heartbeat_elapsed_ = 0;
                out = make_heartbeats();    // send AppendEntries to all followers
            }
            return out;
        }

        // Followers and candidates: count toward the election timeout.
        election_elapsed_++;
        if (election_elapsed_ >= election_timeout_) {
            out = start_election();         // timed out — become a candidate
        }
        return out;
    }
    // React to an incoming message. Returns any messages to send in response.
    std::vector<Message> handle_message(const Message& msg) {
        std::vector<Message> out;

        // --- Universal term rule (runs for every message) ---
        if (msg.term > current_term_) {
            current_term_ = msg.term;   // we're behind: catch up
            role_ = RaftRole::Follower;
            voted_for_ = -1;            // new term, haven't voted yet
        }

        switch (msg.type) {

        case MsgType::RequestVote: {
            bool grant = false;
            // Only consider granting if the candidate's term isn't stale.
            if (msg.term >= current_term_) {
                bool already_voted = (voted_for_ != -1 && voted_for_ != msg.from);
                // Candidate's log must be at least as up-to-date as ours (safety).
                uint64_t my_last_term  = log_.empty() ? 0 : log_.back().term;
                uint64_t my_last_index = log_.size();
                bool log_ok = (msg.last_log_term > my_last_term) ||
                              (msg.last_log_term == my_last_term &&
                               msg.last_log_index >= my_last_index);
                if (!already_voted && log_ok) {
                    grant = true;
                    voted_for_ = msg.from;
                    reset_election_timer();   // granting a vote resets our timer
                }
            }
            // Reply with our vote decision.
            Message reply;
            reply.type = MsgType::RequestVoteReply;
            reply.from = id_;
            reply.to   = msg.from;
            reply.term = current_term_;
            reply.vote_granted = grant;
            out.push_back(reply);
            break;
        }

        case MsgType::RequestVoteReply: {
            // Only a candidate cares, and only for the current term.
            if (role_ == RaftRole::Candidate && msg.term == current_term_) {
                if (msg.vote_granted) {
                    votes_received_++;
                    // Majority? Become leader.
                    if (votes_received_ > cluster_size_ / 2) {
                        role_ = RaftRole::Leader;
                        heartbeat_elapsed_ = HEARTBEAT_INTERVAL;  // send heartbeats soon
                    }
                }
            }
            break;
        }

        case MsgType::AppendEntries: {
            bool success = false;
            // A valid leader for our term (or newer): accept, stay/become follower.
            if (msg.term >= current_term_) {
                role_ = RaftRole::Follower;
                reset_election_timer();   // heard from leader: don't start an election
                success = true;
                // (Log handling goes here in the replication phase — heartbeat for now.)
            }
            Message reply;
            reply.type = MsgType::AppendEntriesReply;
            reply.from = id_;
            reply.to   = msg.from;
            reply.term = current_term_;
            reply.success = success;
            out.push_back(reply);
            break;
        }

        case MsgType::AppendEntriesReply: {
            // Leader bookkeeping for log replication — handled in the next phase.
            break;
        }
        }

        return out;
    }

private:
    std::vector<Message> start_election() {
        current_term_++;
        role_ = RaftRole::Candidate;
        voted_for_ = id_;
        votes_received_ = 1;
        reset_election_timer();
        std::vector<Message> out;
        for (int peer = 0; peer < cluster_size_; ++peer) {
            if (peer == id_) continue;
            Message m;
            m.type = MsgType::RequestVote;
            m.from = id_;
            m.to   = peer;
            m.term = current_term_;
            m.last_log_index = log_.size();
            m.last_log_term  = log_.empty() ? 0 : log_.back().term;
            out.push_back(m);
        }
        return out;
    }

    std::vector<Message> make_heartbeats() {
        std::vector<Message> out;
        for (int peer = 0; peer < cluster_size_; ++peer) {
            if (peer == id_) continue;
            Message m;
            m.type = MsgType::AppendEntries;
            m.from = id_;
            m.to   = peer;
            m.term = current_term_;
            out.push_back(m);
        }
        return out;
    }

    void reset_election_timer() {
        election_elapsed_ = 0;
        election_timeout_ = 10 + (rand() % 11);
    }

    int id_;
    int cluster_size_;

    // --- persistent state (would survive a crash) ---
    uint64_t current_term_ = 0;
    int      voted_for_    = -1;   // -1 means "voted for nobody this term"
    std::vector<LogEntry> log_;
    int votes_received_ = 0;
    int heartbeat_elapsed_ = 0;
    static constexpr int HEARTBEAT_INTERVAL = 3;

    // --- volatile state ---
    uint64_t commit_index_ = 0;
    uint64_t last_applied_ = 0;

    // --- role & timing ---
    RaftRole role_ = RaftRole::Follower;
    int election_timeout_ = 0;      // randomized deadline (in "ticks")
    int election_elapsed_ = 0;      // how long since we last heard from a leader
};