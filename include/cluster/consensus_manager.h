#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <functional>

namespace slonana {

// Forward declarations
namespace common {
    struct ValidatorConfig;
}

namespace cluster {

using namespace slonana::common;

enum class NodeState {
    FOLLOWER,
    CANDIDATE,
    LEADER
};

struct LogEntry {
    uint64_t term;
    uint64_t index;
    std::vector<uint8_t> data;
    bool committed;
};

struct VoteRequest {
    uint64_t term;
    std::string candidate_id;
    uint64_t last_log_index;
    uint64_t last_log_term;
};

struct VoteResponse {
    uint64_t term;
    bool vote_granted;
};

struct AppendEntriesRequest {
    uint64_t term;
    std::string leader_id;
    uint64_t prev_log_index;
    uint64_t prev_log_term;
    std::vector<LogEntry> entries;
    uint64_t leader_commit;
};

struct AppendEntriesResponse {
    uint64_t term;
    bool success;
};

struct PeerState {
    uint64_t next_index;
    uint64_t match_index;
    bool active;
};

struct ConsensusStats {
    uint64_t current_term;
    NodeState current_state;
    std::string leader_id;
    uint64_t log_size;
    uint64_t commit_index;
    size_t peer_count;
};

// Interface for cluster communication
class IClusterCommunication {
public:
    virtual ~IClusterCommunication() = default;
    virtual bool send_vote_request(const VoteRequest& request) = 0;
    virtual bool send_vote_response(const VoteResponse& response) = 0;
    virtual bool send_append_entries(const AppendEntriesRequest& request) = 0;
    virtual bool send_append_entries_response(const AppendEntriesResponse& response) = 0;
};

// Raft consensus algorithm implementation
class ConsensusManager {
private:
    std::string node_id_;
    ValidatorConfig config_;
    
    // Raft state
    mutable std::mutex state_mutex_;
    NodeState current_state_;
    uint64_t current_term_;
    std::string voted_for_;
    std::string leader_id_;
    
    // Log state
    std::vector<LogEntry> log_;
    uint64_t commit_index_ = 0;
    uint64_t last_applied_ = 0;
    
    // Leader state
    std::unordered_map<std::string, PeerState> peer_states_;
    
    // Peers and communication
    std::unordered_map<std::string, std::shared_ptr<IClusterCommunication>> peers_;
    mutable std::mutex peers_mutex_;
    
    // Threading
    std::atomic<bool> running_;
    std::thread election_thread_;
    std::thread heartbeat_thread_;
    std::thread consensus_thread_;
    
    // Timing
    std::chrono::steady_clock::time_point last_heartbeat_time_;
    int election_timeout_ms_;
    int heartbeat_interval_ms_;
    
    // Election state
    size_t votes_received_ = 0;
    
    // State machine callback
    std::function<void(const std::vector<uint8_t>&)> state_machine_callback_;
    
    // Pending operations management
    struct PendingOperation {
        std::string id;
        std::vector<uint8_t> data;
        std::chrono::steady_clock::time_point timestamp;
        int timeout_ms;
        int retry_count;
        int max_retries;
        int confirmations;
    };
    
    std::vector<PendingOperation> pending_operations_;
    int required_confirmations_ = 3; // Majority consensus
    
    // Private methods
    void election_loop();
    void heartbeat_loop();
    void consensus_loop();
    void start_election();
    void become_leader();
    void send_heartbeats();
    void replicate_to_followers(const LogEntry& entry);
    void update_commit_index();
    void apply_committed_entries();
    bool is_log_up_to_date(uint64_t last_log_index, uint64_t last_log_term) const;
    bool is_election_timeout() const;
    void reset_election_timer();
    std::shared_ptr<IClusterCommunication> get_peer_communication(const std::string& peer_id);
    void process_pending_operations();

public:
    ConsensusManager(const std::string& node_id, const ValidatorConfig& config);
    ~ConsensusManager();
    
    // Lifecycle management
    bool start();
    void stop();
    bool is_running() const { return running_.load(); }
    
    // Peer management
    void add_peer(const std::string& peer_id, std::shared_ptr<IClusterCommunication> comm);
    void remove_peer(const std::string& peer_id);
    
    // Consensus operations
    bool propose_value(const std::vector<uint8_t>& value);
    void handle_vote_request(const VoteRequest& request);
    void handle_vote_response(const VoteResponse& response);
    void handle_append_entries(const AppendEntriesRequest& request);
    void handle_append_entries_response(const std::string& peer_id, const AppendEntriesResponse& response);
    
    // State queries
    NodeState get_state() const;
    std::string get_leader_id() const;
    uint64_t get_current_term() const;
    ConsensusStats get_stats() const;
    
    // State machine integration
    void set_state_machine_callback(std::function<void(const std::vector<uint8_t>&)> callback);
};

// Utility functions
namespace consensus_utils {
    std::string node_state_to_string(NodeState state);
    NodeState string_to_node_state(const std::string& state);
    std::vector<uint8_t> serialize_vote_request(const VoteRequest& request);
    VoteRequest deserialize_vote_request(const std::vector<uint8_t>& data);
    std::vector<uint8_t> serialize_append_entries(const AppendEntriesRequest& request);
    AppendEntriesRequest deserialize_append_entries(const std::vector<uint8_t>& data);
}

}} // namespace slonana::cluster