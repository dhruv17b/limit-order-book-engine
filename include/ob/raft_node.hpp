#pragma once
#include <cstdint>
#include <vector>
#include "ob/types.hpp"   // reuse your Command type for log entries
#include <cstdlib>
#include "ob/order_book.hpp"

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
    OrderBook::TopOfBook engine_top() const { return engine_.top_of_book(); }
    uint64_t last_applied() const { return last_applied_; }

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
                out = make_append_entries();    // send AppendEntries to all followers
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
                        become_leader();
                    }
                }
            }
            break;
        }

        case MsgType::AppendEntries: {
            bool success = false;

            // Reject outright if the leader's term is stale.
            if (msg.term >= current_term_) {
                role_ = RaftRole::Follower;
                reset_election_timer();        // heard from a valid leader

                // Consistency check: do we have a matching entry at prev_log_index?
                bool log_ok;
                if (msg.prev_log_index == 0) {
                    log_ok = true;             // leader is sending from the very start
                } else if (msg.prev_log_index <= log_.size()) {
                    // entry exists; does its term match?
                    log_ok = (log_[msg.prev_log_index - 1].term == msg.prev_log_term);
                } else {
                    log_ok = false;            // we don't even have that entry yet
                }

                if (log_ok) {
                    success = true;

                    // Append the new entries, overwriting any conflicting tail.
                    uint64_t idx = msg.prev_log_index;   // last index we agree on
                    for (const LogEntry& e : msg.entries) {
                        idx++;
                        if (idx <= log_.size()) {
                            // Existing entry here — if it conflicts, truncate from here.
                            if (log_[idx - 1].term != e.term) {
                                log_.resize(idx - 1);     // drop conflicting tail
                                log_.push_back(e);
                            }
                            // else: identical entry already present, skip.
                        } else {
                            log_.push_back(e);            // new entry, append
                        }
                    }

                    // Advance commit index toward the leader's (apply happens later).
                    if (msg.leader_commit > commit_index_) {
                        uint64_t last_new = msg.prev_log_index + msg.entries.size();
                        commit_index_ = std::min(msg.leader_commit, last_new);
                    }
                }
            }

            Message reply;
            reply.type = MsgType::AppendEntriesReply;
            reply.from = id_;
            reply.to   = msg.from;
            reply.term = current_term_;
            reply.success = success;
            // Tell the leader how much of our log is now good (for nextIndex/matchIndex).
            reply.last_log_index = log_.size();
            out.push_back(reply);
            break;
        }

        case MsgType::AppendEntriesReply: {
            // Only the leader processes these, and only for the current term.
            if (role_ == RaftRole::Leader && msg.term == current_term_) {
                if (msg.success) {
                    // Follower accepted: record how much it now has.
                    match_index_[msg.from] = msg.last_log_index;
                    next_index_[msg.from]  = msg.last_log_index + 1;

                    // Can we commit anything new? An entry is committed once a
                    // majority of nodes have it (and it's from our current term).
                    for (uint64_t n = commit_index_ + 1; n <= log_.size(); ++n) {
                        if (log_[n - 1].term != current_term_) continue;  // safety rule
                        int count = 1;                       // count ourselves
                        for (int peer = 0; peer < cluster_size_; ++peer) {
                            if (peer == id_) continue;
                            if (match_index_[peer] >= n) count++;
                        }
                        if (count > cluster_size_ / 2)
                            commit_index_ = n;               // majority has it -> commit
                    }
                } else {
                    // Follower rejected: our prevLog didn't match. Back up and retry.
                    if (next_index_[msg.from] > 1)
                        next_index_[msg.from]--;
                }
            }
            break;
        }
        }
        apply_committed();
        return out;
        }

private:
    OrderBook engine_;   // this replica's state machine
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
           // reconcile engine with any newly-committed entries
        return out;
    }

    // Build AppendEntries for every follower, carrying whatever entries each
    // one still needs (based on next_index_). Empty entries act as a heartbeat.
    std::vector<Message> make_append_entries() {
        std::vector<Message> out;
        for (int peer = 0; peer < cluster_size_; ++peer) {
            if (peer == id_) continue;

            uint64_t ni = next_index_[peer];          // next index to send this peer
            uint64_t prev_index = ni - 1;             // entry just before it
            uint64_t prev_term = 0;
            if (prev_index > 0 && prev_index <= log_.size())
                prev_term = log_[prev_index - 1].term; // log_ is 0-based; index is 1-based

            Message m;
            m.type = MsgType::AppendEntries;
            m.from = id_;
            m.to   = peer;
            m.term = current_term_;
            m.prev_log_index = prev_index;
            m.prev_log_term  = prev_term;
            m.leader_commit  = commit_index_;

            // Attach all entries from ni to the end of our log.
            for (uint64_t i = ni; i <= log_.size(); ++i)
                m.entries.push_back(log_[i - 1]);     // 1-based index -> 0-based vector

            out.push_back(m);
        }
        return out;
    }

    void become_leader() {
        role_ = RaftRole::Leader;
        heartbeat_elapsed_ = HEARTBEAT_INTERVAL;   // trigger heartbeats soon
        // Optimistically assume every follower is caught up to our log end.
        next_index_.assign(cluster_size_, log_.size() + 1);
        match_index_.assign(cluster_size_, 0);
    }

    void reset_election_timer() {
        election_elapsed_ = 0;
        election_timeout_ = 10 + (rand() % 11);
    }

    // Feed any newly-committed log entries into the engine, in order.
    void apply_committed() {
        while (last_applied_ < commit_index_) {
            last_applied_++;
            const Command& cmd = log_[last_applied_ - 1].command;  // 1-based -> 0-based
            engine_.apply(cmd);
        }
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
    std::vector<uint64_t> next_index_;   // per-follower: next log index to send
    std::vector<uint64_t> match_index_;  // per-follower: highest confirmed index

    // --- volatile state ---
    uint64_t commit_index_ = 0;
    uint64_t last_applied_ = 0;

    // --- role & timing ---
    RaftRole role_ = RaftRole::Follower;
    int election_timeout_ = 0;      // randomized deadline (in "ticks")
    int election_elapsed_ = 0;      // how long since we last heard from a leader
};