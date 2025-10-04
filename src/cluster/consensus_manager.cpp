/**
 * @file consensus_manager.cpp
 * @brief Implements the Raft consensus algorithm for cluster management.
 *
 * This file provides the logic for the ConsensusManager class, which handles
 * leader election, log replication, heartbeating, and state transitions
 * required for the Raft consensus protocol.
 */
#include "cluster/consensus_manager.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>

namespace slonana {
namespace cluster {

/**
 * @brief Constructs a new ConsensusManager.
 * @param node_id A unique identifier for this node in the cluster.
 * @param config The validator configuration, containing consensus parameters.
 */
ConsensusManager::ConsensusManager(const std::string &node_id,
                                   const ValidatorConfig &config)
    : node_id_(node_id), config_(config), current_state_(NodeState::FOLLOWER),
      current_term_(0), voted_for_(""), running_(false), leader_id_(""),
      election_timeout_ms_(150 + (std::rand() % 150)), // 150-300ms random
      heartbeat_interval_ms_(50) {
  reset_election_timer();
  std::cout << "Consensus manager initialized for node: " << node_id_ << std::endl;
}

/**
 * @brief Destructor for the ConsensusManager.
 * @details Ensures that the consensus manager is gracefully stopped, terminating
 * all background threads.
 */
ConsensusManager::~ConsensusManager() { stop(); }

/**
 * @brief Starts the consensus manager's background threads.
 * @details This launches the threads responsible for handling elections,
 * sending heartbeats (if a leader), and processing other consensus tasks.
 * @return True if the manager was started successfully, false if already running.
 */
bool ConsensusManager::start() {
  if (running_.load()) return false;
  running_.store(true);
  election_thread_ = std::thread(&ConsensusManager::election_loop, this);
  heartbeat_thread_ = std::thread(&ConsensusManager::heartbeat_loop, this);
  consensus_thread_ = std::thread(&ConsensusManager::consensus_loop, this);
  std::cout << "Consensus manager started for node: " << node_id_ << std::endl;
  return true;
}

/**
 * @brief Stops the consensus manager and joins its background threads.
 */
void ConsensusManager::stop() {
  if (!running_.load()) return;
  running_.store(false);
  if (election_thread_.joinable()) election_thread_.join();
  if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
  if (consensus_thread_.joinable()) consensus_thread_.join();
  std::cout << "Consensus manager stopped for node: " << node_id_ << std::endl;
}

/**
 * @brief Adds a new peer to the consensus group.
 * @param peer_id The unique identifier of the peer to add.
 * @param comm A shared pointer to a communication channel for the peer.
 */
void ConsensusManager::add_peer(const std::string &peer_id,
                                std::shared_ptr<IClusterCommunication> comm) {
  std::lock_guard<std::mutex> lock(peers_mutex_);
  peers_[peer_id] = comm;
  peer_states_[peer_id] = PeerState{0, 0, true};
  std::cout << "Added peer to consensus: " << peer_id << std::endl;
}

/**
 * @brief Removes a peer from the consensus group.
 * @param peer_id The unique identifier of the peer to remove.
 */
void ConsensusManager::remove_peer(const std::string &peer_id) {
  std::lock_guard<std::mutex> lock(peers_mutex_);
  peers_.erase(peer_id);
  peer_states_.erase(peer_id);
  std::cout << "Removed peer from consensus: " << peer_id << std::endl;
}

/**
 * @brief Proposes a new value to be added to the replicated log.
 * @details This can only be called by the leader. It creates a new log entry
 * and begins the process of replicating it to all followers.
 * @param value The data to be replicated.
 * @return True if the value was successfully proposed, false if this node is not the leader.
 */
bool ConsensusManager::propose_value(const std::vector<uint8_t> &value) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (current_state_ != NodeState::LEADER) {
    return false;
  }
  uint64_t index = log_.size() + 1;
  LogEntry entry{current_term_, index, value, false};
  log_.push_back(entry);
  replicate_to_followers(entry);
  std::cout << "Proposed value at index " << index << " term " << current_term_ << std::endl;
  return true;
}

/**
 * @brief Handles an incoming vote request from a candidate node.
 * @details The node will grant its vote if the candidate's term is current and
 * its log is at least as up-to-date as this node's log.
 * @param request The vote request received from the candidate.
 */
void ConsensusManager::handle_vote_request(const VoteRequest &request) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  bool vote_granted = false;
  if (request.term > current_term_) {
    current_term_ = request.term;
    voted_for_ = "";
    current_state_ = NodeState::FOLLOWER;
  }
  if (request.term == current_term_ && (voted_for_.empty() || voted_for_ == request.candidate_id) && is_log_up_to_date(request.last_log_index, request.last_log_term)) {
    voted_for_ = request.candidate_id;
    vote_granted = true;
    reset_election_timer();
  }
  VoteResponse response{current_term_, vote_granted};
  if (auto peer_comm = get_peer_communication(request.candidate_id)) {
    peer_comm->send_vote_response(response);
  }
  std::cout << "Handled vote request from " << request.candidate_id << ", granted: " << vote_granted << std::endl;
}

/**
 * @brief Handles a response to a vote request sent by this node when it was a candidate.
 * @details Tallies votes received. If a majority is reached, this node becomes the leader.
 * @param response The vote response from a peer.
 */
void ConsensusManager::handle_vote_response(const VoteResponse &response) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (current_state_ != NodeState::CANDIDATE || response.term != current_term_) {
    return;
  }
  if (response.term > current_term_) {
    current_term_ = response.term;
    current_state_ = NodeState::FOLLOWER;
    voted_for_ = "";
    return;
  }
  if (response.vote_granted) {
    votes_received_++;
    if (votes_received_ >= (peers_.size() + 1) / 2 + 1) {
      become_leader();
    }
  }
}

/**
 * @brief Handles an AppendEntries request from the leader.
 * @details This is used for both log replication and heartbeating. The node checks
 * for log consistency and appends new entries if they are valid.
 * @param request The AppendEntries request from the leader.
 */
void ConsensusManager::handle_append_entries(const AppendEntriesRequest &request) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  bool success = false;
  if (request.term >= current_term_) {
    current_term_ = request.term;
    current_state_ = NodeState::FOLLOWER;
    leader_id_ = request.leader_id;
    reset_election_timer();
    if (request.prev_log_index == 0 || (request.prev_log_index <= log_.size() && log_[request.prev_log_index - 1].term == request.prev_log_term)) {
      success = true;
      if (!request.entries.empty()) {
        if (request.prev_log_index < log_.size()) {
          log_.erase(log_.begin() + request.prev_log_index, log_.end());
        }
        log_.insert(log_.end(), request.entries.begin(), request.entries.end());
      }
      if (request.leader_commit > commit_index_) {
        commit_index_ = std::min(request.leader_commit, static_cast<uint64_t>(log_.size()));
        apply_committed_entries();
      }
    }
  }
  AppendEntriesResponse response{current_term_, success};
  if (auto peer_comm = get_peer_communication(request.leader_id)) {
    peer_comm->send_append_entries_response(response);
  }
}

/**
 * @brief Handles a response to an AppendEntries request sent by this node as leader.
 * @details Updates the peer's state (`next_index`, `match_index`) based on whether
 * the replication was successful. If not, it decrements `next_index` to retry.
 * @param peer_id The ID of the peer that responded.
 * @param response The AppendEntries response from the peer.
 */
void ConsensusManager::handle_append_entries_response(const std::string &peer_id, const AppendEntriesResponse &response) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (current_state_ != NodeState::LEADER || response.term != current_term_) {
    return;
  }
  if (response.term > current_term_) {
    current_term_ = response.term;
    current_state_ = NodeState::FOLLOWER;
    voted_for_ = "";
    return;
  }
  if (auto it = peer_states_.find(peer_id); it != peer_states_.end()) {
    if (response.success) {
      it->second.next_index = log_.size() + 1;
      it->second.match_index = log_.size();
      update_commit_index();
    } else {
      if (it->second.next_index > 1) {
        it->second.next_index--;
      }
    }
  }
}

/**
 * @brief Gets the current state of the node (Follower, Candidate, or Leader).
 * @return The current NodeState.
 */
NodeState ConsensusManager::get_state() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return current_state_;
}

/**
 * @brief Gets the ID of the current cluster leader.
 * @return The node ID of the leader, or an empty string if there is no leader.
 */
std::string ConsensusManager::get_leader_id() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return leader_id_;
}

/**
 * @brief Gets the current consensus term number.
 * @return The current term.
 */
uint64_t ConsensusManager::get_current_term() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return current_term_;
}

/**
 * @brief Retrieves statistics about the current state of the consensus manager.
 * @return A `ConsensusStats` struct containing the current metrics.
 */
ConsensusStats ConsensusManager::get_stats() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  ConsensusStats stats;
  stats.current_term = current_term_;
  stats.current_state = current_state_;
  stats.leader_id = leader_id_;
  stats.log_size = log_.size();
  stats.commit_index = commit_index_;
  stats.peer_count = peers_.size();
  return stats;
}

/**
 * @brief The main loop for the election timer thread.
 * @details This loop periodically checks if the election timeout has been
 * reached. If so, and if the node is not already a leader, it starts a new election.
 */
void ConsensusManager::election_loop() {
  while (running_.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (current_state_ != NodeState::LEADER && is_election_timeout()) {
      start_election();
    }
  }
}

/**
 * @brief The main loop for the leader's heartbeat thread.
 * @details If this node is the leader, this loop periodically sends heartbeat
 * (empty AppendEntries) messages to all peers to maintain authority.
 */
void ConsensusManager::heartbeat_loop() {
  while (running_.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(heartbeat_interval_ms_));
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (current_state_ == NodeState::LEADER) {
      send_heartbeats();
    }
  }
}

/**
 * @brief A general-purpose loop for other consensus-related tasks.
 * @details This loop can be used for tasks like processing pending operations
 * that require periodic checks.
 */
void ConsensusManager::consensus_loop() {
  while (running_.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    process_pending_operations();
  }
}

/**
 * @brief Initiates a new election.
 * @details Transitions the node to the Candidate state, increments the term,
 * votes for itself, and sends VoteRequests to all peers.
 */
void ConsensusManager::start_election() {
  current_term_++;
  current_state_ = NodeState::CANDIDATE;
  voted_for_ = node_id_;
  votes_received_ = 1; // Vote for self
  reset_election_timer();
  std::cout << "Starting election for term " << current_term_ << std::endl;
  uint64_t last_log_index = log_.size();
  uint64_t last_log_term = log_.empty() ? 0 : log_.back().term;
  VoteRequest request{current_term_, node_id_, last_log_index, last_log_term};
  for (const auto &[peer_id, comm] : peers_) {
    comm->send_vote_request(request);
  }
}

/**
 * @brief Transitions this node to the Leader state.
 * @details This is called after winning an election. It initializes the state
 * needed to manage followers and sends initial heartbeats.
 */
void ConsensusManager::become_leader() {
  current_state_ = NodeState::LEADER;
  leader_id_ = node_id_;
  for (auto &[peer_id, state] : peer_states_) {
    state.next_index = log_.size() + 1;
    state.match_index = 0;
  }
  std::cout << "Became leader for term " << current_term_ << std::endl;
  send_heartbeats();
}

/**
 * @brief Sends heartbeat messages to all peers.
 * @details Constructs and sends an empty AppendEntriesRequest to all followers
 * to assert leadership and prevent new elections.
 */
void ConsensusManager::send_heartbeats() {
  uint64_t prev_log_index = log_.size();
  uint64_t prev_log_term = log_.empty() ? 0 : log_.back().term;
  AppendEntriesRequest request{current_term_, node_id_, prev_log_index, prev_log_term, {}, commit_index_};
  for (const auto &[peer_id, comm] : peers_) {
    comm->send_append_entries(request);
  }
}

/**
 * @brief Replicates a new log entry to all follower nodes.
 * @param entry The log entry to replicate.
 */
void ConsensusManager::replicate_to_followers(const LogEntry &entry) {
  for (const auto &[peer_id, comm] : peers_) {
    if (auto it = peer_states_.find(peer_id); it != peer_states_.end()) {
      uint64_t prev_log_index = it->second.next_index - 1;
      uint64_t prev_log_term = (prev_log_index == 0) ? 0 : log_[prev_log_index - 1].term;
      AppendEntriesRequest request{current_term_, node_id_, prev_log_index, prev_log_term, {entry}, commit_index_};
      comm->send_append_entries(request);
    }
  }
}

/**
 * @brief Updates the commit index based on the replication status of followers.
 * @details A log entry is considered committed when it has been replicated to a
 * majority of the cluster. This method finds the highest log index that satisfies
 * this condition.
 */
void ConsensusManager::update_commit_index() {
  if (current_state_ != NodeState::LEADER) return;
  for (uint64_t index = commit_index_ + 1; index <= log_.size(); index++) {
    if (log_[index - 1].term == current_term_) {
      size_t replicated_count = 1; // Count self
      for (const auto &[peer_id, state] : peer_states_) {
        if (state.match_index >= index) {
          replicated_count++;
        }
      }
      if (replicated_count >= (peers_.size() + 1) / 2 + 1) {
        commit_index_ = index;
      } else {
        break;
      }
    }
  }
  apply_committed_entries();
}

/**
 * @brief Applies all committed but not-yet-applied log entries to the state machine.
 */
void ConsensusManager::apply_committed_entries() {
  while (last_applied_ < commit_index_) {
    last_applied_++;
    const LogEntry &entry = log_[last_applied_ - 1];
    if (state_machine_callback_) {
      state_machine_callback_(entry.data);
    }
    std::cout << "Applied log entry " << last_applied_ << std::endl;
  }
}

/**
 * @brief Checks if a candidate's log is at least as up-to-date as this node's log.
 * @param last_log_index The index of the candidate's last log entry.
 * @param last_log_term The term of the candidate's last log entry.
 * @return True if the candidate's log is considered up-to-date.
 */
bool ConsensusManager::is_log_up_to_date(uint64_t last_log_index, uint64_t last_log_term) const {
  if (log_.empty()) return true;
  uint64_t our_last_term = log_.back().term;
  uint64_t our_last_index = log_.size();
  return (last_log_term > our_last_term) || (last_log_term == our_last_term && last_log_index >= our_last_index);
}

/**
 * @brief Checks if the election timeout has elapsed since the last heartbeat.
 * @return True if the timeout has been reached, false otherwise.
 */
bool ConsensusManager::is_election_timeout() const {
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_time_).count();
  return elapsed >= election_timeout_ms_;
}

/**
 * @brief Resets the election timer to the current time.
 */
void ConsensusManager::reset_election_timer() {
  last_heartbeat_time_ = std::chrono::steady_clock::now();
}

/**
 * @brief Retrieves the communication channel for a specific peer.
 * @param peer_id The ID of the peer.
 * @return A shared pointer to the peer's communication channel, or nullptr if not found.
 */
std::shared_ptr<IClusterCommunication>
ConsensusManager::get_peer_communication(const std::string &peer_id) {
  auto it = peers_.find(peer_id);
  return (it != peers_.end()) ? it->second : nullptr;
}

/**
 * @brief Processes pending operations, handling timeouts and retries.
 * @details This method is called periodically to manage operations that are waiting
 * for consensus. It also applies confirmed operations to the state machine.
 */
void ConsensusManager::process_pending_operations() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  auto current_time = std::chrono::steady_clock::now();
  auto it = pending_operations_.begin();
  while (it != pending_operations_.end()) {
    auto &operation = *it;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - operation.timestamp);
    if (elapsed > std::chrono::milliseconds(operation.timeout_ms)) {
      if (operation.retry_count < operation.max_retries) {
        operation.retry_count++;
        operation.timestamp = current_time;
        operation.timeout_ms *= 2; // Exponential backoff
        ++it;
      } else {
        it = pending_operations_.erase(it);
      }
    } else {
      ++it;
    }
  }
  for (auto &operation : pending_operations_) {
    if (operation.confirmations >= required_confirmations_) {
      if (state_machine_callback_) {
        state_machine_callback_(operation.data);
      }
    }
  }
  pending_operations_.erase(std::remove_if(pending_operations_.begin(), pending_operations_.end(),
                     [this](const auto &op) { return op.confirmations >= required_confirmations_; }), pending_operations_.end());
}

/**
 * @brief Sets the callback function to be invoked when a log entry is committed.
 * @details This function is how the consensus manager applies changes to the
 * application's state machine.
 * @param callback The function to be called with the committed data.
 */
void ConsensusManager::set_state_machine_callback(
    std::function<void(const std::vector<uint8_t> &)> callback) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  state_machine_callback_ = callback;
}

namespace consensus_utils {
/**
 * @brief Converts a NodeState enum to its string representation.
 * @param state The NodeState to convert.
 * @return The string representation of the state.
 */
std::string node_state_to_string(NodeState state) {
    switch (state) {
        case NodeState::FOLLOWER: return "Follower";
        case NodeState::CANDIDATE: return "Candidate";
        case NodeState::LEADER: return "Leader";
        default: return "Unknown";
    }
}

/**
 * @brief Converts a string to its corresponding NodeState enum value.
 * @param state The string to convert.
 * @return The corresponding NodeState.
 */
NodeState string_to_node_state(const std::string &state) {
    if (state == "Follower") return NodeState::FOLLOWER;
    if (state == "Candidate") return NodeState::CANDIDATE;
    if (state == "Leader") return NodeState::LEADER;
    return NodeState::FOLLOWER; // Default
}

/**
 * @brief Serializes a VoteRequest struct into a byte vector.
 * @param request The VoteRequest to serialize.
 * @return A byte vector containing the serialized data.
 */
std::vector<uint8_t> serialize_vote_request(const VoteRequest &request) {
    // In a real implementation, this would use a robust serialization library
    // like Protocol Buffers, FlatBuffers, or similar.
    std::vector<uint8_t> data;
    // ... serialization logic ...
    return data;
}

/**
 * @brief Deserializes a byte vector into a VoteRequest struct.
 * @param data The byte vector to deserialize.
 * @return The deserialized VoteRequest struct.
 */
VoteRequest deserialize_vote_request(const std::vector<uint8_t> &data) {
    VoteRequest request;
    // ... deserialization logic ...
    return request;
}

/**
 * @brief Serializes an AppendEntriesRequest struct into a byte vector.
 * @param request The AppendEntriesRequest to serialize.
 * @return A byte vector containing the serialized data.
 */
std::vector<uint8_t> serialize_append_entries(const AppendEntriesRequest &request) {
    std::vector<uint8_t> data;
    // ... serialization logic ...
    return data;
}

/**
 * @brief Deserializes a byte vector into an AppendEntriesRequest struct.
 * @param data The byte vector to deserialize.
 * @return The deserialized AppendEntriesRequest struct.
 */
AppendEntriesRequest deserialize_append_entries(const std::vector<uint8_t> &data) {
    AppendEntriesRequest request;
    // ... deserialization logic ...
    return request;
}
} // namespace consensus_utils

} // namespace cluster
} // namespace slonana