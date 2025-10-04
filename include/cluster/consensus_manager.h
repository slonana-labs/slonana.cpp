/**
 * @file consensus_manager.h
 * @brief Defines the core components for the Raft-based consensus mechanism.
 *
 * This file contains the primary classes and data structures for managing cluster
 * consensus, including state management, log replication, and leader election,
 * based on the Raft algorithm.
 */
#pragma once

#include "common/types.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace slonana {

// Forward declarations
namespace common {
struct ValidatorConfig;
}

namespace cluster {

using namespace slonana::common;

/**
 * @brief Represents the state of a node in the Raft consensus algorithm.
 */
enum class NodeState {
    /// @brief The node is following the leader.
    FOLLOWER,
    /// @brief The node is actively seeking to become the new leader.
    CANDIDATE,
    /// @brief The node is the current leader of the cluster.
    LEADER
};

/**
 * @brief Represents an entry in the replicated log.
 */
struct LogEntry {
    /// @brief The term in which this entry was created.
    uint64_t term;
    /// @brief The index of this entry in the log.
    uint64_t index;
    /// @brief The command or data for the state machine.
    std::vector<uint8_t> data;
    /// @brief A flag indicating if this entry has been committed.
    bool committed;
};

/**
 * @brief A request sent by a candidate to solicit votes during an election.
 */
struct VoteRequest {
    /// @brief The candidate's current term.
    uint64_t term;
    /// @brief The ID of the candidate requesting the vote.
    std::string candidate_id;
    /// @brief The index of the candidate's last log entry.
    uint64_t last_log_index;
    /// @brief The term of the candidate's last log entry.
    uint64_t last_log_term;
};

/**
 * @brief A response to a VoteRequest.
 */
struct VoteResponse {
    /// @brief The term of the node responding to the vote request.
    uint64_t term;
    /// @brief True if the vote was granted, false otherwise.
    bool vote_granted;
};

/**
 * @brief A request sent by the leader to replicate log entries or as a heartbeat.
 */
struct AppendEntriesRequest {
    /// @brief The leader's current term.
    uint64_t term;
    /// @brief The ID of the leader.
    std::string leader_id;
    /// @brief The index of the log entry immediately preceding the new ones.
    uint64_t prev_log_index;
    /// @brief The term of the `prev_log_index` entry.
    uint64_t prev_log_term;
    /// @brief The log entries to store (empty for heartbeats).
    std::vector<LogEntry> entries;
    /// @brief The leader's commit index.
    uint64_t leader_commit;
};

/**
 * @brief A response to an AppendEntriesRequest.
 */
struct AppendEntriesResponse {
    /// @brief The term of the responding node, for the leader to update itself if needed.
    uint64_t term;
    /// @brief True if the follower contained an entry matching `prev_log_index` and `prev_log_term`.
    bool success;
};

/**
 * @brief Represents the state of a peer as tracked by the leader.
 */
struct PeerState {
    /// @brief The index of the next log entry to send to this peer.
    uint64_t next_index;
    /// @brief The index of the highest log entry known to be replicated on this peer.
    uint64_t match_index;
    /// @brief A flag indicating if the peer is currently active and reachable.
    bool active;
};

/**
 * @brief A collection of statistics about the current state of the consensus manager.
 */
struct ConsensusStats {
    uint64_t current_term;
    NodeState current_state;
    std::string leader_id;
    uint64_t log_size;
    uint64_t commit_index;
    size_t peer_count;
};

/**
 * @brief An interface for abstracting the communication layer between cluster nodes.
 */
class IClusterCommunication {
public:
  virtual ~IClusterCommunication() = default;
  virtual bool send_vote_request(const VoteRequest &request) = 0;
  virtual bool send_vote_response(const VoteResponse &response) = 0;
  virtual bool send_append_entries(const AppendEntriesRequest &request) = 0;
  virtual bool send_append_entries_response(const AppendEntriesResponse &response) = 0;
};

/**
 * @brief Manages the Raft consensus algorithm for the cluster.
 * @details This class is responsible for leader election, log replication, and
 * applying committed entries to the state machine. It orchestrates the node's
 * behavior as a follower, candidate, or leader.
 */
class ConsensusManager {
private:
  std::string node_id_;
  ValidatorConfig config_;
  mutable std::mutex state_mutex_;
  NodeState current_state_;
  uint64_t current_term_;
  std::string voted_for_;
  std::string leader_id_;
  std::vector<LogEntry> log_;
  uint64_t commit_index_ = 0;
  uint64_t last_applied_ = 0;
  std::unordered_map<std::string, PeerState> peer_states_;
  std::unordered_map<std::string, std::shared_ptr<IClusterCommunication>> peers_;
  mutable std::mutex peers_mutex_;
  std::atomic<bool> running_;
  std::thread election_thread_;
  std::thread heartbeat_thread_;
  std::thread consensus_thread_;
  std::chrono::steady_clock::time_point last_heartbeat_time_;
  int election_timeout_ms_;
  int heartbeat_interval_ms_;
  size_t votes_received_ = 0;
  std::function<void(const std::vector<uint8_t> &)> state_machine_callback_;

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

  void election_loop();
  void heartbeat_loop();
  void consensus_loop();
  void start_election();
  void become_leader();
  void send_heartbeats();
  void replicate_to_followers(const LogEntry &entry);
  void update_commit_index();
  void apply_committed_entries();
  bool is_log_up_to_date(uint64_t last_log_index, uint64_t last_log_term) const;
  bool is_election_timeout() const;
  void reset_election_timer();
  std::shared_ptr<IClusterCommunication> get_peer_communication(const std::string &peer_id);
  void process_pending_operations();

public:
  ConsensusManager(const std::string &node_id, const ValidatorConfig &config);
  ~ConsensusManager();

  bool start();
  void stop();
  bool is_running() const { return running_.load(); }

  void add_peer(const std::string &peer_id, std::shared_ptr<IClusterCommunication> comm);
  void remove_peer(const std::string &peer_id);

  bool propose_value(const std::vector<uint8_t> &value);
  void handle_vote_request(const VoteRequest &request);
  void handle_vote_response(const VoteResponse &response);
  void handle_append_entries(const AppendEntriesRequest &request);
  void handle_append_entries_response(const std::string &peer_id, const AppendEntriesResponse &response);

  NodeState get_state() const;
  std::string get_leader_id() const;
  uint64_t get_current_term() const;
  ConsensusStats get_stats() const;

  void set_state_machine_callback(std::function<void(const std::vector<uint8_t> &)> callback);
};

/**
 * @brief A collection of utility functions for the consensus module.
 */
namespace consensus_utils {
std::string node_state_to_string(NodeState state);
NodeState string_to_node_state(const std::string &state);
std::vector<uint8_t> serialize_vote_request(const VoteRequest &request);
VoteRequest deserialize_vote_request(const std::vector<uint8_t> &data);
std::vector<uint8_t> serialize_append_entries(const AppendEntriesRequest &request);
AppendEntriesRequest deserialize_append_entries(const std::vector<uint8_t> &data);
} // namespace consensus_utils

} // namespace cluster
} // namespace slonana