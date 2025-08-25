#include "cluster/consensus_manager.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <random>

namespace slonana {
namespace cluster {

ConsensusManager::ConsensusManager(const std::string& node_id, const ValidatorConfig& config)
    : node_id_(node_id), config_(config), current_state_(NodeState::FOLLOWER), 
      current_term_(0), voted_for_(""), running_(false), leader_id_(""),
      election_timeout_ms_(150 + (std::rand() % 150)), // 150-300ms random
      heartbeat_interval_ms_(50) {
    
    // Initialize consensus state
    reset_election_timer();
    
    std::cout << "Consensus manager initialized for node: " << node_id_ << std::endl;
}

ConsensusManager::~ConsensusManager() {
    stop();
}

bool ConsensusManager::start() {
    if (running_.load()) return false;
    
    running_.store(true);
    
    // Start consensus threads
    election_thread_ = std::thread(&ConsensusManager::election_loop, this);
    heartbeat_thread_ = std::thread(&ConsensusManager::heartbeat_loop, this);
    consensus_thread_ = std::thread(&ConsensusManager::consensus_loop, this);
    
    std::cout << "Consensus manager started for node: " << node_id_ << std::endl;
    return true;
}

void ConsensusManager::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    
    // Stop all threads
    if (election_thread_.joinable()) election_thread_.join();
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
    if (consensus_thread_.joinable()) consensus_thread_.join();
    
    std::cout << "Consensus manager stopped for node: " << node_id_ << std::endl;
}

void ConsensusManager::add_peer(const std::string& peer_id, std::shared_ptr<IClusterCommunication> comm) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    peers_[peer_id] = comm;
    peer_states_[peer_id] = PeerState{0, 0, true};
    
    std::cout << "Added peer to consensus: " << peer_id << std::endl;
}

void ConsensusManager::remove_peer(const std::string& peer_id) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    peers_.erase(peer_id);
    peer_states_.erase(peer_id);
    
    std::cout << "Removed peer from consensus: " << peer_id << std::endl;
}

bool ConsensusManager::propose_value(const std::vector<uint8_t>& value) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    // Only leader can propose values
    if (current_state_ != NodeState::LEADER) {
        return false;
    }
    
    // Create log entry
    uint64_t index = log_.size() + 1;
    LogEntry entry{current_term_, index, value, false};
    
    // Add to our log
    log_.push_back(entry);
    
    // Replicate to followers
    replicate_to_followers(entry);
    
    std::cout << "Proposed value at index " << index << " term " << current_term_ << std::endl;
    return true;
}

void ConsensusManager::handle_vote_request(const VoteRequest& request) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    bool vote_granted = false;
    
    // Check if we can vote for this candidate
    if (request.term > current_term_) {
        current_term_ = request.term;
        voted_for_ = "";
        current_state_ = NodeState::FOLLOWER;
    }
    
    if (request.term == current_term_ && 
        (voted_for_.empty() || voted_for_ == request.candidate_id) &&
        is_log_up_to_date(request.last_log_index, request.last_log_term)) {
        
        voted_for_ = request.candidate_id;
        vote_granted = true;
        reset_election_timer();
    }
    
    // Send vote response
    VoteResponse response{current_term_, vote_granted};
    auto peer_comm = get_peer_communication(request.candidate_id);
    if (peer_comm) {
        peer_comm->send_vote_response(response);
    }
    
    std::cout << "Handled vote request from " << request.candidate_id 
              << ", granted: " << vote_granted << std::endl;
}

void ConsensusManager::handle_vote_response(const VoteResponse& response) {
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
        
        // Check if we have majority
        size_t majority = (peers_.size() + 1) / 2 + 1;
        if (votes_received_ >= majority) {
            become_leader();
        }
    }
}

void ConsensusManager::handle_append_entries(const AppendEntriesRequest& request) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    bool success = false;
    
    // Check term
    if (request.term >= current_term_) {
        current_term_ = request.term;
        current_state_ = NodeState::FOLLOWER;
        leader_id_ = request.leader_id;
        reset_election_timer();
        
        // Check log consistency
        if (request.prev_log_index == 0 || 
            (request.prev_log_index <= log_.size() && 
             log_[request.prev_log_index - 1].term == request.prev_log_term)) {
            
            success = true;
            
            // Append new entries
            if (!request.entries.empty()) {
                // Remove conflicting entries
                if (request.prev_log_index < log_.size()) {
                    log_.erase(log_.begin() + request.prev_log_index, log_.end());
                }
                
                // Append new entries
                log_.insert(log_.end(), request.entries.begin(), request.entries.end());
            }
            
            // Update commit index
            if (request.leader_commit > commit_index_) {
                commit_index_ = std::min(request.leader_commit, static_cast<uint64_t>(log_.size()));
                apply_committed_entries();
            }
        }
    }
    
    // Send response
    AppendEntriesResponse response{current_term_, success};
    auto peer_comm = get_peer_communication(request.leader_id);
    if (peer_comm) {
        peer_comm->send_append_entries_response(response);
    }
}

void ConsensusManager::handle_append_entries_response(const std::string& peer_id, 
                                                     const AppendEntriesResponse& response) {
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
    
    auto it = peer_states_.find(peer_id);
    if (it != peer_states_.end()) {
        if (response.success) {
            // Update peer state
            it->second.next_index = log_.size() + 1;
            it->second.match_index = log_.size();
            
            // Check if we can commit more entries
            update_commit_index();
        } else {
            // Decrement next_index and retry
            if (it->second.next_index > 1) {
                it->second.next_index--;
            }
        }
    }
}

NodeState ConsensusManager::get_state() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_state_;
}

std::string ConsensusManager::get_leader_id() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return leader_id_;
}

uint64_t ConsensusManager::get_current_term() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_term_;
}

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

void ConsensusManager::election_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        std::lock_guard<std::mutex> lock(state_mutex_);
        
        if (current_state_ != NodeState::LEADER && is_election_timeout()) {
            start_election();
        }
    }
}

void ConsensusManager::heartbeat_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(heartbeat_interval_ms_));
        
        std::lock_guard<std::mutex> lock(state_mutex_);
        
        if (current_state_ == NodeState::LEADER) {
            send_heartbeats();
        }
    }
}

void ConsensusManager::consensus_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Process any pending consensus operations
        process_pending_operations();
    }
}

void ConsensusManager::start_election() {
    current_term_++;
    current_state_ = NodeState::CANDIDATE;
    voted_for_ = node_id_;
    votes_received_ = 1; // Vote for ourselves
    reset_election_timer();
    
    std::cout << "Starting election for term " << current_term_ << std::endl;
    
    // Send vote requests to all peers
    uint64_t last_log_index = log_.size();
    uint64_t last_log_term = log_.empty() ? 0 : log_.back().term;
    
    VoteRequest request{current_term_, node_id_, last_log_index, last_log_term};
    
    for (const auto& [peer_id, comm] : peers_) {
        comm->send_vote_request(request);
    }
}

void ConsensusManager::become_leader() {
    current_state_ = NodeState::LEADER;
    leader_id_ = node_id_;
    
    // Initialize peer states
    for (auto& [peer_id, state] : peer_states_) {
        state.next_index = log_.size() + 1;
        state.match_index = 0;
    }
    
    std::cout << "Became leader for term " << current_term_ << std::endl;
    
    // Send initial heartbeats
    send_heartbeats();
}

void ConsensusManager::send_heartbeats() {
    uint64_t prev_log_index = log_.size();
    uint64_t prev_log_term = log_.empty() ? 0 : log_.back().term;
    
    AppendEntriesRequest request{
        current_term_, node_id_, prev_log_index, prev_log_term,
        {}, commit_index_
    };
    
    for (const auto& [peer_id, comm] : peers_) {
        comm->send_append_entries(request);
    }
}

void ConsensusManager::replicate_to_followers(const LogEntry& entry) {
    for (const auto& [peer_id, comm] : peers_) {
        auto it = peer_states_.find(peer_id);
        if (it != peer_states_.end()) {
            uint64_t prev_log_index = it->second.next_index - 1;
            uint64_t prev_log_term = (prev_log_index == 0) ? 0 : log_[prev_log_index - 1].term;
            
            AppendEntriesRequest request{
                current_term_, node_id_, prev_log_index, prev_log_term,
                {entry}, commit_index_
            };
            
            comm->send_append_entries(request);
        }
    }
}

void ConsensusManager::update_commit_index() {
    if (current_state_ != NodeState::LEADER) return;
    
    // Find highest index replicated on majority of servers
    for (uint64_t index = commit_index_ + 1; index <= log_.size(); index++) {
        if (log_[index - 1].term == current_term_) {
            size_t replicated_count = 1; // Count ourselves
            
            for (const auto& [peer_id, state] : peer_states_) {
                if (state.match_index >= index) {
                    replicated_count++;
                }
            }
            
            size_t majority = (peers_.size() + 1) / 2 + 1;
            if (replicated_count >= majority) {
                commit_index_ = index;
            } else {
                break;
            }
        }
    }
    
    apply_committed_entries();
}

void ConsensusManager::apply_committed_entries() {
    while (last_applied_ < commit_index_) {
        last_applied_++;
        const LogEntry& entry = log_[last_applied_ - 1];
        
        // Apply the entry to state machine
        if (state_machine_callback_) {
            state_machine_callback_(entry.data);
        }
        
        std::cout << "Applied log entry " << last_applied_ << std::endl;
    }
}

bool ConsensusManager::is_log_up_to_date(uint64_t last_log_index, uint64_t last_log_term) const {
    if (log_.empty()) {
        return true;
    }
    
    uint64_t our_last_term = log_.back().term;
    uint64_t our_last_index = log_.size();
    
    return (last_log_term > our_last_term) || 
           (last_log_term == our_last_term && last_log_index >= our_last_index);
}

bool ConsensusManager::is_election_timeout() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_time_).count();
    return elapsed >= election_timeout_ms_;
}

void ConsensusManager::reset_election_timer() {
    last_heartbeat_time_ = std::chrono::steady_clock::now();
}

std::shared_ptr<IClusterCommunication> ConsensusManager::get_peer_communication(const std::string& peer_id) {
    auto it = peers_.find(peer_id);
    return (it != peers_.end()) ? it->second : nullptr;
}

void ConsensusManager::process_pending_operations() {
    // Comprehensive pending operation processing with timeout handling and retry logic
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    auto current_time = std::chrono::steady_clock::now();
    
    // Process timeouts for pending operations
    auto it = pending_operations_.begin();
    while (it != pending_operations_.end()) {
        auto& operation = *it;
        
        // Check for operation timeout
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time - operation.timestamp);
        
        if (elapsed > std::chrono::milliseconds(operation.timeout_ms)) {
            std::cout << "Consensus: Operation " << operation.id << " timed out after " 
                      << elapsed.count() << "ms" << std::endl;
            
            // Handle timeout based on operation type
            if (operation.retry_count < operation.max_retries) {
                // Retry the operation
                operation.retry_count++;
                operation.timestamp = current_time;
                operation.timeout_ms *= 2; // Exponential backoff
                
                std::cout << "Consensus: Retrying operation " << operation.id 
                          << " (attempt " << operation.retry_count + 1 << "/" 
                          << operation.max_retries + 1 << ")" << std::endl;
                ++it;
            } else {
                // Max retries exceeded, mark as failed
                std::cout << "Consensus: Operation " << operation.id 
                          << " failed after " << operation.max_retries + 1 << " attempts" << std::endl;
                it = pending_operations_.erase(it);
            }
        } else {
            ++it;
        }
    }
    
    // Process any ready operations that have received enough confirmations
    for (auto& operation : pending_operations_) {
        if (operation.confirmations >= required_confirmations_) {
            std::cout << "Consensus: Operation " << operation.id 
                      << " confirmed with " << operation.confirmations << " votes" << std::endl;
            
            // Apply the operation to state machine
            if (state_machine_callback_) {
                state_machine_callback_(operation.data);
            }
        }
    }
    
    // Remove confirmed operations
    pending_operations_.erase(
        std::remove_if(pending_operations_.begin(), pending_operations_.end(),
            [this](const auto& op) { return op.confirmations >= required_confirmations_; }),
        pending_operations_.end());
}

void ConsensusManager::set_state_machine_callback(std::function<void(const std::vector<uint8_t>&)> callback) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_machine_callback_ = callback;
}

}} // namespace slonana::cluster