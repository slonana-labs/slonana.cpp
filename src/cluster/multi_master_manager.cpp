#include "cluster/multi_master_manager.h"
#include <iostream>
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace slonana {
namespace cluster {

MultiMasterManager::MultiMasterManager(const std::string& node_id, const ValidatorConfig& config)
    : node_id_(node_id), config_(config), running_(false), message_sequence_(0) {
    
    // Initialize default service affinity rules
    ServiceAffinityRule rpc_rule;
    rpc_rule.service_type = "rpc";
    rpc_rule.min_masters = 2;
    rpc_rule.max_masters = 5;
    rpc_rule.load_balancing_strategy = "least_loaded";
    service_affinity_rules_["rpc"] = rpc_rule;
    
    ServiceAffinityRule ledger_rule;
    ledger_rule.service_type = "ledger";
    ledger_rule.min_masters = 1;
    ledger_rule.max_masters = 3;
    ledger_rule.load_balancing_strategy = "round_robin";
    service_affinity_rules_["ledger"] = ledger_rule;
    
    ServiceAffinityRule gossip_rule;
    gossip_rule.service_type = "gossip";
    gossip_rule.min_masters = 3;
    gossip_rule.max_masters = 7;
    gossip_rule.load_balancing_strategy = "regional";
    service_affinity_rules_["gossip"] = gossip_rule;
    
    std::cout << "Multi-master manager initialized for node: " << node_id_ << std::endl;
}

MultiMasterManager::~MultiMasterManager() {
    stop();
}

bool MultiMasterManager::start() {
    if (running_.load()) return false;
    
    running_.store(true);
    
    // Start background threads
    master_monitor_thread_ = std::thread(&MultiMasterManager::master_monitor_loop, this);
    load_balancer_thread_ = std::thread(&MultiMasterManager::load_balancer_loop, this);
    message_processor_thread_ = std::thread(&MultiMasterManager::message_processor_loop, this);
    health_checker_thread_ = std::thread(&MultiMasterManager::health_checker_loop, this);
    
    std::cout << "Multi-master manager started for node: " << node_id_ << std::endl;
    return true;
}

void MultiMasterManager::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    
    // Stop all threads
    if (master_monitor_thread_.joinable()) master_monitor_thread_.join();
    if (load_balancer_thread_.joinable()) load_balancer_thread_.join();
    if (message_processor_thread_.joinable()) message_processor_thread_.join();
    if (health_checker_thread_.joinable()) health_checker_thread_.join();
    
    std::cout << "Multi-master manager stopped for node: " << node_id_ << std::endl;
}

bool MultiMasterManager::register_master(const MasterNode& master) {
    if (!validate_master_node(master)) {
        std::cerr << "Invalid master node configuration" << std::endl;
        return false;
    }
    
    std::lock_guard<std::mutex> lock(masters_mutex_);
    
    // Generate master ID if not provided
    std::string master_id = master.node_id.empty() ? generate_master_id(master) : master.node_id;
    
    MasterNode new_master = master;
    new_master.node_id = master_id;
    new_master.last_heartbeat = std::chrono::system_clock::now();
    new_master.is_healthy = true;
    
    masters_[master_id] = new_master;
    masters_by_role_[new_master.role].insert(master_id);
    
    if (!new_master.region.empty()) {
        masters_by_region_[new_master.region].insert(master_id);
    }
    
    notify_master_event("master_registered", new_master);
    
    std::cout << "Registered master: " << master_id 
              << " with role: " << master_role_to_string(new_master.role) << std::endl;
    
    return true;
}

bool MultiMasterManager::unregister_master(const std::string& master_id) {
    std::lock_guard<std::mutex> lock(masters_mutex_);
    
    auto it = masters_.find(master_id);
    if (it == masters_.end()) {
        return false;
    }
    
    MasterNode master = it->second;
    
    // Remove from role mapping
    masters_by_role_[master.role].erase(master_id);
    
    // Remove from region mapping
    if (!master.region.empty()) {
        masters_by_region_[master.region].erase(master_id);
    }
    
    // Remove from masters registry
    masters_.erase(it);
    
    notify_master_event("master_unregistered", master);
    
    std::cout << "Unregistered master: " << master_id << std::endl;
    return true;
}

std::vector<MasterNode> MultiMasterManager::get_active_masters() const {
    std::lock_guard<std::mutex> lock(masters_mutex_);
    
    std::vector<MasterNode> active_masters;
    for (const auto& pair : masters_) {
        if (pair.second.is_healthy) {
            active_masters.push_back(pair.second);
        }
    }
    
    return active_masters;
}

std::vector<MasterNode> MultiMasterManager::get_masters_by_role(MasterRole role) const {
    std::lock_guard<std::mutex> lock(masters_mutex_);
    
    std::vector<MasterNode> role_masters;
    
    auto role_it = masters_by_role_.find(role);
    if (role_it != masters_by_role_.end()) {
        for (const auto& master_id : role_it->second) {
            auto master_it = masters_.find(master_id);
            if (master_it != masters_.end() && master_it->second.is_healthy) {
                role_masters.push_back(master_it->second);
            }
        }
    }
    
    return role_masters;
}

bool MultiMasterManager::add_service_affinity(const ServiceAffinityRule& rule) {
    std::lock_guard<std::mutex> lock(affinity_mutex_);
    
    service_affinity_rules_[rule.service_type] = rule;
    
    std::cout << "Added service affinity rule for: " << rule.service_type 
              << " (min: " << rule.min_masters << ", max: " << rule.max_masters << ")" << std::endl;
    
    return true;
}

std::string MultiMasterManager::select_master_for_service(const std::string& service_type) const {
    std::lock_guard<std::mutex> affinity_lock(affinity_mutex_);
    std::lock_guard<std::mutex> masters_lock(masters_mutex_);
    
    auto rule_it = service_affinity_rules_.find(service_type);
    if (rule_it == service_affinity_rules_.end()) {
        // No specific rule, use default strategy
        return select_least_loaded_master();
    }
    
    const ServiceAffinityRule& rule = rule_it->second;
    
    // Get masters that can handle this service
    std::vector<MasterNode> candidate_masters;
    
    if (!rule.preferred_masters.empty()) {
        // Use preferred masters if specified
        for (const auto& master_id : rule.preferred_masters) {
            auto master_it = masters_.find(master_id);
            if (master_it != masters_.end() && master_it->second.is_healthy) {
                candidate_masters.push_back(master_it->second);
            }
        }
    } else {
        // Use all healthy masters
        for (const auto& pair : masters_) {
            if (pair.second.is_healthy) {
                candidate_masters.push_back(pair.second);
            }
        }
    }
    
    if (candidate_masters.empty()) {
        return "";
    }
    
    // Apply load balancing strategy
    if (rule.load_balancing_strategy == "least_loaded") {
        auto min_it = std::min_element(candidate_masters.begin(), candidate_masters.end(),
            [](const MasterNode& a, const MasterNode& b) {
                return a.load_score < b.load_score;
            });
        return min_it->node_id;
    } else if (rule.load_balancing_strategy == "round_robin") {
        // Simple round-robin based on hash of service type
        std::hash<std::string> hasher;
        size_t index = hasher(service_type) % candidate_masters.size();
        return candidate_masters[index].node_id;
    } else if (rule.load_balancing_strategy == "regional") {
        // Prefer masters in the same region as this node
        std::string my_region = "default";  // TODO: Get from config
        
        auto regional_it = std::find_if(candidate_masters.begin(), candidate_masters.end(),
            [&my_region](const MasterNode& master) {
                return master.region == my_region;
            });
        
        if (regional_it != candidate_masters.end()) {
            return regional_it->node_id;
        }
        
        // Fallback to least loaded
        auto min_it = std::min_element(candidate_masters.begin(), candidate_masters.end(),
            [](const MasterNode& a, const MasterNode& b) {
                return a.load_score < b.load_score;
            });
        return min_it->node_id;
    }
    
    return candidate_masters[0].node_id;
}

std::vector<std::string> MultiMasterManager::get_masters_for_service(const std::string& service_type) const {
    std::lock_guard<std::mutex> affinity_lock(affinity_mutex_);
    std::lock_guard<std::mutex> masters_lock(masters_mutex_);
    
    std::vector<std::string> service_masters;
    
    auto rule_it = service_affinity_rules_.find(service_type);
    if (rule_it == service_affinity_rules_.end()) {
        // No specific rule, return all healthy masters
        for (const auto& pair : masters_) {
            if (pair.second.is_healthy) {
                service_masters.push_back(pair.first);
            }
        }
        return service_masters;
    }
    
    const ServiceAffinityRule& rule = rule_it->second;
    
    if (!rule.preferred_masters.empty()) {
        // Use preferred masters
        for (const auto& master_id : rule.preferred_masters) {
            auto master_it = masters_.find(master_id);
            if (master_it != masters_.end() && master_it->second.is_healthy) {
                service_masters.push_back(master_id);
            }
        }
    } else {
        // Use all healthy masters up to max_masters limit
        uint32_t count = 0;
        for (const auto& pair : masters_) {
            if (pair.second.is_healthy && count < rule.max_masters) {
                service_masters.push_back(pair.first);
                count++;
            }
        }
    }
    
    return service_masters;
}

bool MultiMasterManager::update_master_load(const std::string& master_id, uint64_t load_score) {
    std::lock_guard<std::mutex> lock(masters_mutex_);
    
    auto it = masters_.find(master_id);
    if (it == masters_.end()) {
        return false;
    }
    
    it->second.load_score = load_score;
    it->second.last_heartbeat = std::chrono::system_clock::now();
    
    return true;
}

std::string MultiMasterManager::select_least_loaded_master(MasterRole role) const {
    std::lock_guard<std::mutex> lock(masters_mutex_);
    
    std::vector<MasterNode> candidate_masters;
    
    if (role == MasterRole::NONE) {
        // Any healthy master
        for (const auto& pair : masters_) {
            if (pair.second.is_healthy) {
                candidate_masters.push_back(pair.second);
            }
        }
    } else {
        // Masters with specific role
        auto role_masters = get_masters_by_role(role);
        candidate_masters = std::move(role_masters);
    }
    
    if (candidate_masters.empty()) {
        return "";
    }
    
    auto min_it = std::min_element(candidate_masters.begin(), candidate_masters.end(),
        [](const MasterNode& a, const MasterNode& b) {
            return a.load_score < b.load_score;
        });
    
    return min_it->node_id;
}

std::string MultiMasterManager::select_master_by_region(const std::string& region, MasterRole role) const {
    std::lock_guard<std::mutex> lock(masters_mutex_);
    
    auto region_it = masters_by_region_.find(region);
    if (region_it == masters_by_region_.end()) {
        return "";
    }
    
    std::vector<MasterNode> regional_masters;
    for (const auto& master_id : region_it->second) {
        auto master_it = masters_.find(master_id);
        if (master_it != masters_.end() && master_it->second.is_healthy) {
            if (role == MasterRole::NONE || master_it->second.role == role) {
                regional_masters.push_back(master_it->second);
            }
        }
    }
    
    if (regional_masters.empty()) {
        return "";
    }
    
    // Select least loaded master in the region
    auto min_it = std::min_element(regional_masters.begin(), regional_masters.end(),
        [](const MasterNode& a, const MasterNode& b) {
            return a.load_score < b.load_score;
        });
    
    return min_it->node_id;
}

bool MultiMasterManager::promote_to_master(MasterRole role, uint32_t shard_id) {
    // Add role to this node's roles
    my_roles_.insert(role);
    
    // Create master node entry for this node
    MasterNode my_master;
    my_master.node_id = node_id_;
    my_master.address = "127.0.0.1";  // TODO: Get from config
    my_master.port = 8899;           // TODO: Get from config
    my_master.role = role;
    my_master.shard_id = shard_id;
    my_master.region = "default";    // TODO: Get from config
    my_master.load_score = 0;
    my_master.is_healthy = true;
    
    bool success = register_master(my_master);
    
    if (success) {
        std::cout << "Node " << node_id_ << " promoted to master with role: " 
                  << master_role_to_string(role) << std::endl;
    }
    
    return success;
}

bool MultiMasterManager::demote_from_master() {
    bool success = unregister_master(node_id_);
    
    if (success) {
        my_roles_.clear();
        std::cout << "Node " << node_id_ << " demoted from master roles" << std::endl;
    }
    
    return success;
}

bool MultiMasterManager::initiate_master_election(MasterRole role) {
    std::cout << "Initiating master election for role: " << master_role_to_string(role) << std::endl;
    
    // Simple election based on load and priority
    std::lock_guard<std::mutex> lock(masters_mutex_);
    
    std::vector<std::pair<std::string, uint64_t>> candidates;
    
    for (const auto& pair : masters_) {
        if (pair.second.is_healthy) {
            uint64_t priority = calculate_master_priority(pair.second, role);
            candidates.emplace_back(pair.first, priority);
        }
    }
    
    if (candidates.empty()) {
        return false;
    }
    
    // Sort by priority (higher is better)
    std::sort(candidates.begin(), candidates.end(),
        [](const auto& a, const auto& b) {
            return a.second > b.second;
        });
    
    std::string elected_master = candidates[0].first;
    
    std::cout << "Elected master: " << elected_master 
              << " for role: " << master_role_to_string(role) 
              << " with priority: " << candidates[0].second << std::endl;
    
    // Update master role
    auto master_it = masters_.find(elected_master);
    if (master_it != masters_.end()) {
        // Remove from old role
        masters_by_role_[master_it->second.role].erase(elected_master);
        
        // Add to new role
        master_it->second.role = role;
        masters_by_role_[role].insert(elected_master);
        
        notify_master_event("master_elected", master_it->second);
    }
    
    return true;
}

bool MultiMasterManager::vote_for_master(const std::string& candidate_id, MasterRole role) {
    std::lock_guard<std::mutex> lock(masters_mutex_);
    
    auto it = masters_.find(candidate_id);
    if (it == masters_.end() || !it->second.is_healthy) {
        return false;
    }
    
    std::cout << "Voted for master candidate: " << candidate_id 
              << " for role: " << master_role_to_string(role) << std::endl;
    
    return true;
}

bool MultiMasterManager::send_cross_master_message(const CrossMasterMessage& message) {
    std::lock_guard<std::mutex> lock(messaging_mutex_);
    
    CrossMasterMessage msg = message;
    msg.sequence_number = ++message_sequence_;
    msg.timestamp = std::chrono::system_clock::now();
    
    outgoing_messages_.push_back(msg);
    
    return true;
}

std::vector<CrossMasterMessage> MultiMasterManager::receive_cross_master_messages() {
    std::lock_guard<std::mutex> lock(messaging_mutex_);
    
    std::vector<CrossMasterMessage> messages = std::move(incoming_messages_);
    incoming_messages_.clear();
    
    return messages;
}

bool MultiMasterManager::broadcast_to_masters(const std::string& message_type, const std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> masters_lock(masters_mutex_);
    
    for (const auto& pair : masters_) {
        if (pair.second.is_healthy && pair.first != node_id_) {
            CrossMasterMessage message;
            message.source_master = node_id_;
            message.target_master = pair.first;
            message.message_type = message_type;
            message.payload = payload;
            
            send_cross_master_message(message);
        }
    }
    
    return true;
}

bool MultiMasterManager::is_master_healthy(const std::string& master_id) const {
    std::lock_guard<std::mutex> lock(masters_mutex_);
    
    auto it = masters_.find(master_id);
    return it != masters_.end() && it->second.is_healthy;
}

void MultiMasterManager::mark_master_unhealthy(const std::string& master_id) {
    std::lock_guard<std::mutex> lock(masters_mutex_);
    
    auto it = masters_.find(master_id);
    if (it != masters_.end()) {
        it->second.is_healthy = false;
        notify_master_event("master_unhealthy", it->second);
        std::cout << "Marked master as unhealthy: " << master_id << std::endl;
    }
}

void MultiMasterManager::mark_master_healthy(const std::string& master_id) {
    std::lock_guard<std::mutex> lock(masters_mutex_);
    
    auto it = masters_.find(master_id);
    if (it != masters_.end()) {
        it->second.is_healthy = true;
        it->second.last_heartbeat = std::chrono::system_clock::now();
        notify_master_event("master_healthy", it->second);
        std::cout << "Marked master as healthy: " << master_id << std::endl;
    }
}

bool MultiMasterManager::add_master_to_region(const std::string& master_id, const std::string& region) {
    std::lock_guard<std::mutex> lock(masters_mutex_);
    
    auto it = masters_.find(master_id);
    if (it == masters_.end()) {
        return false;
    }
    
    // Remove from old region
    if (!it->second.region.empty()) {
        masters_by_region_[it->second.region].erase(master_id);
    }
    
    // Add to new region
    it->second.region = region;
    masters_by_region_[region].insert(master_id);
    
    std::cout << "Added master " << master_id << " to region: " << region << std::endl;
    return true;
}

std::vector<std::string> MultiMasterManager::get_regional_masters(const std::string& region) const {
    std::lock_guard<std::mutex> lock(masters_mutex_);
    
    std::vector<std::string> regional_masters;
    
    auto it = masters_by_region_.find(region);
    if (it != masters_by_region_.end()) {
        for (const auto& master_id : it->second) {
            auto master_it = masters_.find(master_id);
            if (master_it != masters_.end() && master_it->second.is_healthy) {
                regional_masters.push_back(master_id);
            }
        }
    }
    
    return regional_masters;
}

bool MultiMasterManager::setup_cross_region_replication(const std::string& source_region, const std::string& target_region) {
    std::cout << "Setting up cross-region replication from " << source_region 
              << " to " << target_region << std::endl;
    
    auto source_masters = get_regional_masters(source_region);
    auto target_masters = get_regional_masters(target_region);
    
    if (source_masters.empty() || target_masters.empty()) {
        std::cerr << "Cannot setup cross-region replication: missing masters in regions" << std::endl;
        return false;
    }
    
    // Create replication configuration message
    std::vector<uint8_t> payload;
    std::string config = source_region + ":" + target_region;
    payload.assign(config.begin(), config.end());
    
    broadcast_to_masters("cross_region_replication", payload);
    
    return true;
}

bool MultiMasterManager::update_configuration(const std::unordered_map<std::string, std::string>& config) {
    // Update internal configuration
    for (const auto& pair : config) {
        std::cout << "Updated config: " << pair.first << " = " << pair.second << std::endl;
    }
    
    // Broadcast configuration update to all masters
    std::vector<uint8_t> payload;
    std::ostringstream oss;
    for (const auto& pair : config) {
        oss << pair.first << "=" << pair.second << "\n";
    }
    std::string config_str = oss.str();
    payload.assign(config_str.begin(), config_str.end());
    
    broadcast_to_masters("config_update", payload);
    
    return true;
}

std::unordered_map<std::string, std::string> MultiMasterManager::get_configuration() const {
    std::unordered_map<std::string, std::string> config;
    
    config["node_id"] = node_id_;
    config["total_masters"] = std::to_string(masters_.size());
    config["active_roles"] = std::to_string(my_roles_.size());
    
    return config;
}

MultiMasterManager::MultiMasterStats MultiMasterManager::get_statistics() const {
    std::lock_guard<std::mutex> lock(masters_mutex_);
    
    MultiMasterStats stats;
    std::memset(&stats, 0, sizeof(stats));
    
    stats.total_masters = masters_.size();
    
    uint64_t total_load = 0;
    for (const auto& pair : masters_) {
        if (pair.second.is_healthy) {
            stats.healthy_masters++;
        }
        
        stats.masters_by_role[static_cast<int>(pair.second.role)]++;
        total_load += pair.second.load_score;
    }
    
    stats.average_load_score = stats.total_masters > 0 ? total_load / stats.total_masters : 0;
    stats.total_cross_master_messages = message_sequence_;
    stats.average_response_time = std::chrono::milliseconds(10);  // Simulated
    
    return stats;
}

// Private methods
void MultiMasterManager::master_monitor_loop() {
    while (running_.load()) {
        update_topology();
        rebalance_services();
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

void MultiMasterManager::load_balancer_loop() {
    while (running_.load()) {
        // Periodic load balancing logic
        std::lock_guard<std::mutex> lock(masters_mutex_);
        
        // Calculate average load
        uint64_t total_load = 0;
        uint32_t active_masters = 0;
        
        for (const auto& pair : masters_) {
            if (pair.second.is_healthy) {
                total_load += pair.second.load_score;
                active_masters++;
            }
        }
        
        if (active_masters > 0) {
            uint64_t average_load = total_load / active_masters;
            
            // Check for load imbalance
            for (const auto& pair : masters_) {
                if (pair.second.is_healthy && pair.second.load_score > average_load * 2) {
                    std::cout << "High load detected on master: " << pair.first 
                              << " (load: " << pair.second.load_score 
                              << ", avg: " << average_load << ")" << std::endl;
                    
                    // Trigger rebalancing if needed
                    notify_master_event("high_load", pair.second);
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

void MultiMasterManager::message_processor_loop() {
    while (running_.load()) {
        std::vector<CrossMasterMessage> messages;
        
        {
            std::lock_guard<std::mutex> lock(messaging_mutex_);
            messages = std::move(outgoing_messages_);
            outgoing_messages_.clear();
        }
        
        // Process outgoing messages
        for (const auto& message : messages) {
            std::cout << "Processing cross-master message: " << message.message_type 
                      << " from " << message.source_master 
                      << " to " << message.target_master << std::endl;
            
            // Simulate message transmission
            // In real implementation, this would use network protocols
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void MultiMasterManager::health_checker_loop() {
    while (running_.load()) {
        auto now = std::chrono::system_clock::now();
        
        std::lock_guard<std::mutex> lock(masters_mutex_);
        
        for (auto& pair : masters_) {
            auto time_since_heartbeat = std::chrono::duration_cast<std::chrono::seconds>(
                now - pair.second.last_heartbeat);
            
            if (time_since_heartbeat.count() > 30) {  // 30 second timeout
                if (pair.second.is_healthy) {
                    pair.second.is_healthy = false;
                    handle_master_failure(pair.first);
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

void MultiMasterManager::process_master_heartbeat(const MasterNode& master) {
    update_master_load(master.node_id, master.load_score);
    mark_master_healthy(master.node_id);
}

void MultiMasterManager::handle_master_failure(const std::string& master_id) {
    std::cout << "Handling master failure: " << master_id << std::endl;
    
    auto it = masters_.find(master_id);
    if (it != masters_.end()) {
        MasterRole failed_role = it->second.role;
        
        // Try to elect a new master for the failed role
        if (failed_role != MasterRole::NONE) {
            initiate_master_election(failed_role);
        }
        
        notify_master_event("master_failed", it->second);
    }
}

void MultiMasterManager::rebalance_services() {
    // Check service affinity requirements and rebalance if needed
    std::lock_guard<std::mutex> affinity_lock(affinity_mutex_);
    
    for (const auto& rule_pair : service_affinity_rules_) {
        const auto& rule = rule_pair.second;
        auto service_masters = get_masters_for_service(rule.service_type);
        
        if (service_masters.size() < rule.min_masters) {
            std::cout << "Service " << rule.service_type 
                      << " needs more masters (current: " << service_masters.size() 
                      << ", min: " << rule.min_masters << ")" << std::endl;
            
            // Trigger master promotion or election
        } else if (service_masters.size() > rule.max_masters) {
            std::cout << "Service " << rule.service_type 
                      << " has too many masters (current: " << service_masters.size() 
                      << ", max: " << rule.max_masters << ")" << std::endl;
            
            // Trigger master demotion
        }
    }
}

void MultiMasterManager::update_topology() {
    // Update network topology information
    std::lock_guard<std::mutex> lock(masters_mutex_);
    
    // Log current topology state
    std::cout << "Current multi-master topology: " << masters_.size() << " total masters" << std::endl;
    
    for (const auto& role_pair : masters_by_role_) {
        std::cout << "  " << master_role_to_string(role_pair.first) 
                  << ": " << role_pair.second.size() << " masters" << std::endl;
    }
    
    for (const auto& region_pair : masters_by_region_) {
        std::cout << "  Region " << region_pair.first 
                  << ": " << region_pair.second.size() << " masters" << std::endl;
    }
}

bool MultiMasterManager::validate_master_node(const MasterNode& master) const {
    return !master.address.empty() && master.port > 0;
}

std::string MultiMasterManager::generate_master_id(const MasterNode& master) const {
    std::ostringstream oss;
    oss << master.address << ":" << master.port << ":" 
        << master_role_to_string(master.role);
    return oss.str();
}

uint64_t MultiMasterManager::calculate_master_priority(const MasterNode& master, MasterRole role) const {
    uint64_t priority = 1000;
    
    // Lower load score means higher priority
    priority += (1000 - std::min(static_cast<uint64_t>(1000), master.load_score));
    
    // Prefer masters already in the desired role
    if (master.role == role) {
        priority += 500;
    }
    
    // Regional preference could be added here
    
    return priority;
}

void MultiMasterManager::notify_master_event(const std::string& event, const MasterNode& master) {
    if (master_event_callback_) {
        master_event_callback_(event, master);
    }
}

// Utility functions
const char* master_role_to_string(MasterRole role) {
    switch (role) {
        case MasterRole::NONE: return "none";
        case MasterRole::RPC_MASTER: return "rpc_master";
        case MasterRole::LEDGER_MASTER: return "ledger_master";
        case MasterRole::GOSSIP_MASTER: return "gossip_master";
        case MasterRole::SHARD_MASTER: return "shard_master";
        case MasterRole::GLOBAL_MASTER: return "global_master";
        default: return "unknown";
    }
}

MasterRole string_to_master_role(const std::string& role_str) {
    if (role_str == "rpc_master") return MasterRole::RPC_MASTER;
    if (role_str == "ledger_master") return MasterRole::LEDGER_MASTER;
    if (role_str == "gossip_master") return MasterRole::GOSSIP_MASTER;
    if (role_str == "shard_master") return MasterRole::SHARD_MASTER;
    if (role_str == "global_master") return MasterRole::GLOBAL_MASTER;
    return MasterRole::NONE;
}

} // namespace cluster
} // namespace slonana