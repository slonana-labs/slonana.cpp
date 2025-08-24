#include "cluster/failover_manager.h"
#include "common/types.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <random>
#include <sstream>
#include <thread>

namespace slonana {
namespace cluster {

FailoverManager::FailoverManager(const std::string& node_id, const ValidatorConfig& config)
    : node_id_(node_id), config_(config), current_state_(FailoverState::NORMAL),
      running_(false), current_leader_("") {
    
    // Initialize default failover configuration
    failover_config_.health_check_interval_ms = 5000;
    failover_config_.failure_detection_timeout_ms = 15000;
    failover_config_.max_consecutive_failures = 3;
    failover_config_.cpu_threshold = 90.0;
    failover_config_.memory_threshold = 90.0;
    failover_config_.network_latency_threshold_ms = 1000.0;
    failover_config_.enable_automatic_failover = true;
    failover_config_.enable_load_based_failover = false;
    failover_config_.failover_cooldown_ms = 30000;
    
    // Initialize statistics
    stats_.total_failovers = 0;
    stats_.successful_failovers = 0;
    stats_.failed_failovers = 0;
    stats_.avg_failover_time_ms = 0;
    stats_.active_nodes = 0;
    stats_.failed_nodes = 0;
    stats_.current_leader = "";
    stats_.current_state = FailoverState::NORMAL;
    
    last_failover_time_ = std::chrono::steady_clock::now() - 
        std::chrono::milliseconds(failover_config_.failover_cooldown_ms);
    
    std::cout << "Failover manager initialized for node: " << node_id_ << std::endl;
}

FailoverManager::~FailoverManager() {
    stop();
}

bool FailoverManager::start() {
    if (running_.load()) return false;
    
    running_.store(true);
    
    // Start failover management threads
    monitoring_thread_ = std::thread(&FailoverManager::monitoring_loop, this);
    failover_thread_ = std::thread(&FailoverManager::failover_loop, this);
    recovery_thread_ = std::thread(&FailoverManager::recovery_loop, this);
    
    std::cout << "Failover manager started for node: " << node_id_ << std::endl;
    return true;
}

void FailoverManager::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    
    // Stop all threads
    if (monitoring_thread_.joinable()) monitoring_thread_.join();
    if (failover_thread_.joinable()) failover_thread_.join();
    if (recovery_thread_.joinable()) recovery_thread_.join();
    
    std::cout << "Failover manager stopped for node: " << node_id_ << std::endl;
}

void FailoverManager::register_node(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    NodeHealth health;
    health.node_id = node_id;
    health.is_responsive = true;
    health.last_heartbeat = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    health.cpu_usage = 0.0;
    health.memory_usage = 0.0;
    health.disk_usage = 0.0;
    health.network_latency_ms = 0.0;
    health.error_count = 0;
    health.is_leader = false;
    health.is_available = true;
    
    node_health_[node_id] = health;
    failure_counts_[node_id] = 0;
    
    std::cout << "Registered node for failover monitoring: " << node_id << std::endl;
}

void FailoverManager::unregister_node(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    auto it = node_health_.find(node_id);
    if (it != node_health_.end()) {
        node_health_.erase(it);
        failure_counts_.erase(node_id);
        std::cout << "Unregistered node from failover monitoring: " << node_id << std::endl;
    }
}

void FailoverManager::update_node_health(const std::string& node_id, const NodeHealth& health) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    auto it = node_health_.find(node_id);
    if (it != node_health_.end()) {
        // Update existing health data
        it->second = health;
        it->second.last_heartbeat = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        // Reset failure count if node is healthy
        if (health.is_responsive && health.is_available) {
            failure_counts_[node_id] = 0;
        }
        
        // Trigger health change callback
        if (health_change_callback_) {
            health_change_callback_(node_id, health);
        }

        // Remove code calling update_node_health_internal
        // Already updated node health above
    }
}

std::vector<NodeHealth> FailoverManager::get_all_node_health() const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    std::vector<NodeHealth> health_list;
    for (const auto& pair : node_health_) {
        health_list.push_back(pair.second);
    }
    return health_list;
}

NodeHealth FailoverManager::get_node_health(const std::string& node_id) const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    auto it = node_health_.find(node_id);
    if (it != node_health_.end()) {
        return it->second;
    }
    
    // Return empty health if node not found
    NodeHealth empty_health;
    empty_health.node_id = node_id;
    empty_health.is_responsive = false;
    empty_health.is_available = false;
    return empty_health;
}

void FailoverManager::set_current_leader(const std::string& leader_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    current_leader_ = leader_id;
    
    // Update leader status in node health
    {
        std::lock_guard<std::mutex> nodes_lock(nodes_mutex_);
        for (auto& pair : node_health_) {
            pair.second.is_leader = (pair.first == leader_id);
        }
    }
    
    std::cout << "Current leader set to: " << leader_id << std::endl;
}

std::string FailoverManager::get_current_leader() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_leader_;
}

bool FailoverManager::is_leader_healthy() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (current_leader_.empty()) {
        return false;
    }
    
    std::lock_guard<std::mutex> nodes_lock(nodes_mutex_);
    auto it = node_health_.find(current_leader_);
    if (it != node_health_.end()) {
        const NodeHealth& health = it->second;
        return health.is_responsive && health.is_available && 
               health.cpu_usage < failover_config_.cpu_threshold &&
               health.memory_usage < failover_config_.memory_threshold &&
               health.network_latency_ms < failover_config_.network_latency_threshold_ms;
    }
    
    return false;
}

bool FailoverManager::trigger_manual_failover(const std::string& node_id, const std::string& reason) {
    if (is_failover_cooldown_active()) {
        std::cout << "Failover cooldown active, manual failover rejected" << std::endl;
        return false;
    }
    
    std::cout << "Manual failover triggered for node: " << node_id << " Reason: " << reason << std::endl;
    return execute_failover(node_id, FailoverTrigger::MANUAL_FAILOVER);
}

bool FailoverManager::force_leader_election() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (current_state_ != FailoverState::NORMAL) {
        std::cout << "Cannot force leader election, failover in progress" << std::endl;
        return false;
    }
    
    current_state_ = FailoverState::ELECTING_REPLACEMENT;
    current_leader_ = "";
    
    // Find best candidate for leadership
    std::string best_candidate = select_replacement_node("");
    if (!best_candidate.empty()) {
        std::cout << "Forcing leader election, selected candidate: " << best_candidate << std::endl;
        
        if (action_handler_) {
            bool success = action_handler_->promote_node_to_leader(best_candidate);
            if (success) {
                set_current_leader(best_candidate);
                current_state_ = FailoverState::NORMAL;
                return true;
            }
        }
    }
    
    current_state_ = FailoverState::NORMAL;
    return false;
}

void FailoverManager::enable_automatic_failover(bool enable) {
    failover_config_.enable_automatic_failover = enable;
    std::cout << "Automatic failover " << (enable ? "enabled" : "disabled") << std::endl;
}

bool FailoverManager::is_automatic_failover_enabled() const {
    return failover_config_.enable_automatic_failover;
}

bool FailoverManager::attempt_node_recovery(const std::string& node_id) {
    if (!action_handler_) {
        return false;
    }
    
    std::cout << "Attempting recovery for node: " << node_id << std::endl;
    
    bool success = action_handler_->restore_node_to_cluster(node_id);
    if (success) {
        reset_failure_count(node_id);
        
        // Update node health to mark as available
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        auto it = node_health_.find(node_id);
        if (it != node_health_.end()) {
            it->second.is_available = true;
            it->second.is_responsive = true;
            it->second.error_count = 0;
        }
        
        std::cout << "Node recovery successful: " << node_id << std::endl;
    } else {
        std::cout << "Node recovery failed: " << node_id << std::endl;
    }
    
    return success;
}

void FailoverManager::reset_failure_count(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    failure_counts_[node_id] = 0;
}

bool FailoverManager::validate_node_recovery(const std::string& node_id) {
    if (!action_handler_) {
        return false;
    }
    
    NodeHealth health = action_handler_->get_node_health(node_id);
    return health.is_responsive && health.is_available && 
           health.cpu_usage < failover_config_.cpu_threshold &&
           health.memory_usage < failover_config_.memory_threshold;
}

FailoverStats FailoverManager::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    FailoverStats current_stats = stats_;
    
    // Update current state and leader
    {
        std::lock_guard<std::mutex> state_lock(state_mutex_);
        current_stats.current_state = current_state_;
        current_stats.current_leader = current_leader_;
    }
    
    // Update active and failed node counts
    {
        std::lock_guard<std::mutex> nodes_lock(nodes_mutex_);
        current_stats.active_nodes = 0;
        current_stats.failed_nodes = 0;
        
        for (const auto& pair : node_health_) {
            if (pair.second.is_responsive && pair.second.is_available) {
                current_stats.active_nodes++;
            } else {
                current_stats.failed_nodes++;
            }
        }
    }
    
    return current_stats;
}

FailoverState FailoverManager::get_current_state() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_state_;
}

std::vector<FailoverEvent> FailoverManager::get_failover_history() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return failover_history_;
}

std::vector<std::string> FailoverManager::get_failed_nodes() const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    std::vector<std::string> failed_nodes;
    for (const auto& pair : node_health_) {
        if (!pair.second.is_responsive || !pair.second.is_available) {
            failed_nodes.push_back(pair.first);
        }
    }
    return failed_nodes;
}

std::vector<std::string> FailoverManager::get_healthy_nodes() const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    std::vector<std::string> healthy_nodes;
    for (const auto& pair : node_health_) {
        if (pair.second.is_responsive && pair.second.is_available &&
            pair.second.cpu_usage < failover_config_.cpu_threshold &&
            pair.second.memory_usage < failover_config_.memory_threshold) {
            healthy_nodes.push_back(pair.first);
        }
    }
    return healthy_nodes;
}

void FailoverManager::set_failover_callback(std::function<void(const FailoverEvent&)> callback) {
    failover_callback_ = callback;
}

void FailoverManager::set_health_change_callback(std::function<void(const std::string&, NodeHealth)> callback) {
    health_change_callback_ = callback;
}

void FailoverManager::simulate_node_failure(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    auto it = node_health_.find(node_id);
    if (it != node_health_.end()) {
        it->second.is_responsive = false;
        it->second.is_available = false;
        it->second.cpu_usage = 100.0;
        it->second.error_count = 1000;
        
        failure_counts_[node_id] = failover_config_.max_consecutive_failures;
        
        std::cout << "Simulated failure for node: " << node_id << std::endl;
    }
}

void FailoverManager::simulate_network_partition(const std::vector<std::string>& isolated_nodes) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    for (const std::string& node_id : isolated_nodes) {
        auto it = node_health_.find(node_id);
        if (it != node_health_.end()) {
            it->second.is_responsive = false;
            it->second.network_latency_ms = 10000.0; // Very high latency
            failure_counts_[node_id] = failover_config_.max_consecutive_failures / 2;
        }
    }
    
    std::cout << "Simulated network partition for " << isolated_nodes.size() << " nodes" << std::endl;
}

bool FailoverManager::run_failover_test() {
    std::cout << "Running failover test..." << std::endl;
    
    // Save current state
    auto original_leader = get_current_leader();
    auto original_state = get_current_state();
    
    // Trigger test failover
    bool test_result = force_leader_election();
    
    // Validate new state
    if (test_result) {
        auto new_leader = get_current_leader();
        bool leader_changed = (new_leader != original_leader && !new_leader.empty());
        
        std::cout << "Failover test " << (leader_changed ? "PASSED" : "FAILED") << std::endl;
        std::cout << "Original leader: " << original_leader << ", New leader: " << new_leader << std::endl;
        
        return leader_changed;
    }
    
    std::cout << "Failover test FAILED - could not trigger failover" << std::endl;
    return false;
}

// Private methods implementation

void FailoverManager::monitoring_loop() {
    while (running_.load()) {
        validate_cluster_state();
        
        // Check health of all nodes
        std::vector<std::string> nodes_to_check;
        {
            std::lock_guard<std::mutex> lock(nodes_mutex_);
            for (const auto& pair : node_health_) {
                nodes_to_check.push_back(pair.first);
            }
        }
        
        for (const auto& node_id : nodes_to_check) {
            check_node_health(node_id);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(failover_config_.health_check_interval_ms));
    }
}

void FailoverManager::failover_loop() {
    while (running_.load()) {
        if (!failover_config_.enable_automatic_failover) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }
        
        // Check for failover conditions
        std::vector<std::string> nodes_to_check;
        {
            std::lock_guard<std::mutex> lock(nodes_mutex_);
            for (const auto& pair : node_health_) {
                nodes_to_check.push_back(pair.first);
            }
        }
        
        for (const auto& node_id : nodes_to_check) {
            FailoverTrigger trigger;
            if (should_trigger_failover(node_id, trigger)) {
                std::cout << "Triggering automatic failover for node: " << node_id << std::endl;
                execute_failover(node_id, trigger);
                break; // Only handle one failover at a time
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(failover_config_.health_check_interval_ms));
    }
}

void FailoverManager::recovery_loop() {
    while (running_.load()) {
        // Attempt recovery of failed nodes
        std::vector<std::string> failed_nodes = get_failed_nodes();
        
        for (const auto& node_id : failed_nodes) {
            // Don't attempt recovery too frequently
            static std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_recovery_attempts;
            auto now = std::chrono::steady_clock::now();
            
            if (last_recovery_attempts.find(node_id) == last_recovery_attempts.end() ||
                now - last_recovery_attempts[node_id] > std::chrono::minutes(5)) {
                
                std::cout << "Attempting automatic recovery for failed node: " << node_id << std::endl;
                bool recovery_success = attempt_node_recovery(node_id);
                last_recovery_attempts[node_id] = now;
                
                if (recovery_success) {
                    std::cout << "Automatic recovery successful for node: " << node_id << std::endl;
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::minutes(2));
    }
}

bool FailoverManager::check_node_health(const std::string& node_id) {
    if (!action_handler_) {
        return false;
    }
    
    NodeHealth current_health = action_handler_->get_node_health(node_id);
    update_node_health(node_id, current_health);
    
    return current_health.is_responsive && current_health.is_available;
}

bool FailoverManager::is_node_failed(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    auto health_it = node_health_.find(node_id);
    auto failure_it = failure_counts_.find(node_id);
    
    if (health_it == node_health_.end() || failure_it == failure_counts_.end()) {
        return false;
    }
    
    const NodeHealth& health = health_it->second;
    int failure_count = failure_it->second;
    
    // Check multiple failure conditions
    bool unresponsive = !health.is_responsive;
    bool high_resource_usage = (health.cpu_usage > failover_config_.cpu_threshold ||
                               health.memory_usage > failover_config_.memory_threshold);
    bool high_latency = (health.network_latency_ms > failover_config_.network_latency_threshold_ms);
    bool consecutive_failures = (failure_count >= failover_config_.max_consecutive_failures);
    
    return unresponsive || consecutive_failures || (high_resource_usage && high_latency);
}

bool FailoverManager::should_trigger_failover(const std::string& node_id, FailoverTrigger& trigger) {
    if (is_failover_cooldown_active()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    auto it = node_health_.find(node_id);
    if (it == node_health_.end()) {
        return false;
    }
    
    const NodeHealth& health = it->second;
    
    // Check various trigger conditions
    if (!health.is_responsive) {
        trigger = FailoverTrigger::NODE_UNRESPONSIVE;
        return true;
    }
    
    if (health.network_latency_ms > failover_config_.network_latency_threshold_ms) {
        trigger = FailoverTrigger::NETWORK_PARTITION;
        return true;
    }
    
    if (health.cpu_usage > failover_config_.cpu_threshold ||
        health.memory_usage > failover_config_.memory_threshold) {
        
        if (failover_config_.enable_load_based_failover) {
            trigger = FailoverTrigger::LOAD_THRESHOLD_EXCEEDED;
            return true;
        }
    }
    
    auto failure_it = failure_counts_.find(node_id);
    if (failure_it != failure_counts_.end() && 
        failure_it->second >= failover_config_.max_consecutive_failures) {
        trigger = FailoverTrigger::HEALTH_CHECK_FAILED;
        return true;
    }
    
    return false;
}

bool FailoverManager::execute_failover(const std::string& failed_node, FailoverTrigger trigger) {
    auto start_time = std::chrono::steady_clock::now();
    
    // Create failover event
    FailoverEvent event;
    event.event_id = failover_utils::generate_event_id();
    event.trigger = trigger;
    event.failed_node_id = failed_node;
    event.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        start_time.time_since_epoch()).count();
    event.state = FailoverState::DETECTING_FAILURE;
    event.reason = failover_utils::failover_trigger_to_string(trigger);
    
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_state_ = FailoverState::DETECTING_FAILURE;
    }
    
    std::cout << "Executing failover for node: " << failed_node 
              << " Trigger: " << event.reason << std::endl;
    
    // Step 1: Isolate failed node
    if (action_handler_) {
        action_handler_->isolate_failed_node(failed_node);
    }
    
    // Step 2: Select replacement node
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_state_ = FailoverState::ELECTING_REPLACEMENT;
    }
    
    std::string replacement_node = select_replacement_node(failed_node);
    if (replacement_node.empty()) {
        std::cout << "No suitable replacement node found for failover" << std::endl;
        
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_state_ = FailoverState::NORMAL;
        
        // Update statistics
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.failed_failovers++;
        
        return false;
    }
    
    event.replacement_node_id = replacement_node;
    
    // Step 3: Switch traffic and promote new leader
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_state_ = FailoverState::SWITCHING_TRAFFIC;
    }
    
    bool traffic_switch_success = true;
    bool promotion_success = true;
    
    if (action_handler_) {
        if (failed_node == current_leader_) {
            // Demote old leader and promote new one
            action_handler_->demote_node_from_leader(failed_node);
            promotion_success = action_handler_->promote_node_to_leader(replacement_node);
            
            if (promotion_success) {
                set_current_leader(replacement_node);
            }
        }
        
        traffic_switch_success = action_handler_->redirect_traffic(failed_node, replacement_node);
    }
    
    // Step 4: Complete failover
    auto end_time = std::chrono::steady_clock::now();
    bool overall_success = traffic_switch_success && promotion_success;
    
    if (overall_success) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_state_ = FailoverState::FAILED_OVER;
        last_failover_time_ = start_time;
        
        std::cout << "Failover completed successfully. New leader: " << replacement_node << std::endl;
    } else {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_state_ = FailoverState::NORMAL;
        
        std::cout << "Failover failed for node: " << failed_node << std::endl;
    }
    
    // Update event and statistics
    event.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    event.state = current_state_;
    
    record_failover_event(event);
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_failovers++;
        if (overall_success) {
            stats_.successful_failovers++;
        } else {
            stats_.failed_failovers++;
        }
        
        // Update average failover time
        stats_.avg_failover_time_ms = 
            (stats_.avg_failover_time_ms * (stats_.total_failovers - 1) + event.duration_ms) /
            stats_.total_failovers;
    }
    
    // Notify callback
    if (failover_callback_) {
        failover_callback_(event);
    }
    
    // Return to normal state after a brief period
    std::this_thread::sleep_for(std::chrono::seconds(5));
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (current_state_ == FailoverState::FAILED_OVER) {
            current_state_ = FailoverState::NORMAL;
        }
    }
    
    return overall_success;
}

std::string FailoverManager::select_replacement_node(const std::string& failed_node) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    std::vector<std::pair<std::string, double>> candidates;
    
    for (const auto& pair : node_health_) {
        const std::string& node_id = pair.first;
        const NodeHealth& health = pair.second;
        
        // Skip the failed node and non-responsive nodes
        if (node_id == failed_node || !health.is_responsive || !health.is_available) {
            continue;
        }
        
        // Calculate node score based on health metrics
        double score = failover_utils::calculate_node_score(health);
        candidates.emplace_back(node_id, score);
    }
    
    if (candidates.empty()) {
        return "";
    }
    
    // Sort by score (higher is better)
    std::sort(candidates.begin(), candidates.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::cout << "Selected replacement node: " << candidates[0].first 
              << " with score: " << candidates[0].second << std::endl;
    
    return candidates[0].first;
}

bool FailoverManager::is_failover_cooldown_active() {
    auto now = std::chrono::steady_clock::now();
    auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_failover_time_).count();
    
    return time_since_last < failover_config_.failover_cooldown_ms;
}

void FailoverManager::record_failover_event(const FailoverEvent& event) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    failover_history_.push_back(event);
    
    // Keep only recent events (last 100)
    if (failover_history_.size() > 100) {
        failover_history_.erase(failover_history_.begin());
    }
}

void FailoverManager::cleanup_old_events() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    // Remove events older than 24 hours
    failover_history_.erase(
        std::remove_if(failover_history_.begin(), failover_history_.end(),
            [now](const FailoverEvent& event) {
                return (now - event.timestamp) > (24 * 60 * 60 * 1000);
            }),
        failover_history_.end());
}

bool FailoverManager::validate_cluster_state() {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    size_t healthy_nodes = 0;
    size_t total_nodes = node_health_.size();
    
    for (const auto& pair : node_health_) {
        if (pair.second.is_responsive && pair.second.is_available) {
            healthy_nodes++;
        }
    }
    
    // Check if we have sufficient healthy nodes for cluster operation
    double health_ratio = total_nodes > 0 ? 
        static_cast<double>(healthy_nodes) / total_nodes : 0.0;
    
    if (health_ratio < 0.5) {
        std::cout << "WARNING: Cluster health critical - only " << healthy_nodes 
                  << " of " << total_nodes << " nodes are healthy" << std::endl;
        return false;
    }
    
    return true;
}

// Utility functions implementation

namespace failover_utils {

std::string failover_trigger_to_string(FailoverTrigger trigger) {
    switch (trigger) {
        case FailoverTrigger::NODE_UNRESPONSIVE:
            return "NODE_UNRESPONSIVE";
        case FailoverTrigger::NETWORK_PARTITION:
            return "NETWORK_PARTITION";
        case FailoverTrigger::HEALTH_CHECK_FAILED:
            return "HEALTH_CHECK_FAILED";
        case FailoverTrigger::MANUAL_FAILOVER:
            return "MANUAL_FAILOVER";
        case FailoverTrigger::LOAD_THRESHOLD_EXCEEDED:
            return "LOAD_THRESHOLD_EXCEEDED";
        default:
            return "UNKNOWN";
    }
}

FailoverTrigger string_to_failover_trigger(const std::string& trigger) {
    if (trigger == "NODE_UNRESPONSIVE") return FailoverTrigger::NODE_UNRESPONSIVE;
    if (trigger == "NETWORK_PARTITION") return FailoverTrigger::NETWORK_PARTITION;
    if (trigger == "HEALTH_CHECK_FAILED") return FailoverTrigger::HEALTH_CHECK_FAILED;
    if (trigger == "MANUAL_FAILOVER") return FailoverTrigger::MANUAL_FAILOVER;
    if (trigger == "LOAD_THRESHOLD_EXCEEDED") return FailoverTrigger::LOAD_THRESHOLD_EXCEEDED;
    return FailoverTrigger::NODE_UNRESPONSIVE; // Default
}

std::string failover_state_to_string(FailoverState state) {
    switch (state) {
        case FailoverState::NORMAL:
            return "NORMAL";
        case FailoverState::DETECTING_FAILURE:
            return "DETECTING_FAILURE";
        case FailoverState::ELECTING_REPLACEMENT:
            return "ELECTING_REPLACEMENT";
        case FailoverState::SWITCHING_TRAFFIC:
            return "SWITCHING_TRAFFIC";
        case FailoverState::RECOVERY_IN_PROGRESS:
            return "RECOVERY_IN_PROGRESS";
        case FailoverState::FAILED_OVER:
            return "FAILED_OVER";
        default:
            return "UNKNOWN";
    }
}

FailoverState string_to_failover_state(const std::string& state) {
    if (state == "NORMAL") return FailoverState::NORMAL;
    if (state == "DETECTING_FAILURE") return FailoverState::DETECTING_FAILURE;
    if (state == "ELECTING_REPLACEMENT") return FailoverState::ELECTING_REPLACEMENT;
    if (state == "SWITCHING_TRAFFIC") return FailoverState::SWITCHING_TRAFFIC;
    if (state == "RECOVERY_IN_PROGRESS") return FailoverState::RECOVERY_IN_PROGRESS;
    if (state == "FAILED_OVER") return FailoverState::FAILED_OVER;
    return FailoverState::NORMAL; // Default
}

std::string generate_event_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;
    
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    uint64_t random_part = dis(gen);
    
    std::stringstream ss;
    ss << "event_" << std::hex << timestamp << "_" << std::hex << random_part;
    return ss.str();
}

bool is_node_health_critical(const NodeHealth& health, const FailoverConfig& config) {
    return !health.is_responsive || 
           !health.is_available ||
           health.cpu_usage > config.cpu_threshold ||
           health.memory_usage > config.memory_threshold ||
           health.network_latency_ms > config.network_latency_threshold_ms;
}

double calculate_node_score(const NodeHealth& health) {
    if (!health.is_responsive || !health.is_available) {
        return 0.0;
    }
    
    // Calculate score based on resource utilization (lower is better)
    double cpu_score = std::max(0.0, 100.0 - health.cpu_usage);
    double memory_score = std::max(0.0, 100.0 - health.memory_usage);
    double disk_score = std::max(0.0, 100.0 - health.disk_usage);
    double latency_score = std::max(0.0, 100.0 - (health.network_latency_ms / 10.0));
    double error_score = std::max(0.0, 100.0 - std::min(100.0, static_cast<double>(health.error_count)));
    
    // Weighted average score
    double score = (cpu_score * 0.3 + memory_score * 0.3 + disk_score * 0.2 + 
                   latency_score * 0.1 + error_score * 0.1);
    
    return score;
}

std::vector<uint8_t> serialize_failover_event(const FailoverEvent& event) {
    std::vector<uint8_t> serialized;
    
    // Serialize event ID
    uint32_t id_len = event.event_id.size();
    for (int i = 0; i < 4; i++) {
        serialized.push_back((id_len >> (i * 8)) & 0xFF);
    }
    serialized.insert(serialized.end(), event.event_id.begin(), event.event_id.end());
    
    // Serialize trigger (1 byte)
    serialized.push_back(static_cast<uint8_t>(event.trigger));
    
    // Serialize node IDs
    uint32_t failed_len = event.failed_node_id.size();
    for (int i = 0; i < 4; i++) {
        serialized.push_back((failed_len >> (i * 8)) & 0xFF);
    }
    serialized.insert(serialized.end(), event.failed_node_id.begin(), event.failed_node_id.end());
    
    uint32_t replacement_len = event.replacement_node_id.size();
    for (int i = 0; i < 4; i++) {
        serialized.push_back((replacement_len >> (i * 8)) & 0xFF);
    }
    serialized.insert(serialized.end(), event.replacement_node_id.begin(), event.replacement_node_id.end());
    
    // Serialize timestamp (8 bytes)
    for (int i = 0; i < 8; i++) {
        serialized.push_back((event.timestamp >> (i * 8)) & 0xFF);
    }
    
    // Serialize state (1 byte)
    serialized.push_back(static_cast<uint8_t>(event.state));
    
    // Serialize reason
    uint32_t reason_len = event.reason.size();
    for (int i = 0; i < 4; i++) {
        serialized.push_back((reason_len >> (i * 8)) & 0xFF);
    }
    serialized.insert(serialized.end(), event.reason.begin(), event.reason.end());
    
    // Serialize duration (8 bytes)
    for (int i = 0; i < 8; i++) {
        serialized.push_back((event.duration_ms >> (i * 8)) & 0xFF);
    }
    
    return serialized;
}

FailoverEvent deserialize_failover_event(const std::vector<uint8_t>& data) {
    FailoverEvent event;
    size_t offset = 0;
    
    if (data.size() < 4) return event;
    
    // Deserialize event ID
    uint32_t id_len = 0;
    for (int i = 0; i < 4; i++) {
        id_len |= (static_cast<uint32_t>(data[offset + i]) << (i * 8));
    }
    offset += 4;
    
    if (offset + id_len > data.size()) return event;
    event.event_id.assign(data.begin() + offset, data.begin() + offset + id_len);
    offset += id_len;
    
    // Deserialize trigger
    if (offset >= data.size()) return event;
    event.trigger = static_cast<FailoverTrigger>(data[offset]);
    offset++;
    
    // Deserialize failed node ID
    if (offset + 4 > data.size()) return event;
    uint32_t failed_len = 0;
    for (int i = 0; i < 4; i++) {
        failed_len |= (static_cast<uint32_t>(data[offset + i]) << (i * 8));
    }
    offset += 4;
    
    if (offset + failed_len > data.size()) return event;
    event.failed_node_id.assign(data.begin() + offset, data.begin() + offset + failed_len);
    offset += failed_len;
    
    // Deserialize replacement node ID
    if (offset + 4 > data.size()) return event;
    uint32_t replacement_len = 0;
    for (int i = 0; i < 4; i++) {
        replacement_len |= (static_cast<uint32_t>(data[offset + i]) << (i * 8));
    }
    offset += 4;
    
    if (offset + replacement_len > data.size()) return event;
    event.replacement_node_id.assign(data.begin() + offset, data.begin() + offset + replacement_len);
    offset += replacement_len;
    
    // Deserialize timestamp
    if (offset + 8 > data.size()) return event;
    event.timestamp = 0;
    for (int i = 0; i < 8; i++) {
        event.timestamp |= (static_cast<uint64_t>(data[offset + i]) << (i * 8));
    }
    offset += 8;
    
    // Deserialize state
    if (offset >= data.size()) return event;
    event.state = static_cast<FailoverState>(data[offset]);
    offset++;
    
    // Deserialize reason
    if (offset + 4 > data.size()) return event;
    uint32_t reason_len = 0;
    for (int i = 0; i < 4; i++) {
        reason_len |= (static_cast<uint32_t>(data[offset + i]) << (i * 8));
    }
    offset += 4;
    
    if (offset + reason_len > data.size()) return event;
    event.reason.assign(data.begin() + offset, data.begin() + offset + reason_len);
    offset += reason_len;
    
    // Deserialize duration
    if (offset + 8 <= data.size()) {
        event.duration_ms = 0;
        for (int i = 0; i < 8; i++) {
            event.duration_ms |= (static_cast<uint64_t>(data[offset + i]) << (i * 8));
        }
    }
    
    return event;
}

} // namespace failover_utils

}} // namespace slonana::cluster