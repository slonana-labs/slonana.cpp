#include "cluster/multi_master_coordinator.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

namespace slonana {
namespace cluster {

MultiMasterCoordinator::MultiMasterCoordinator(
    const std::string &coordinator_id, const ValidatorConfig &config)
    : coordinator_id_(coordinator_id), config_(config), running_(false) {

  // Initialize default configuration
  coordination_config_.max_masters_per_region = 5;
  coordination_config_.max_masters_per_shard = 3;
  coordination_config_.min_masters_for_consensus = 3;
  coordination_config_.master_election_timeout_ms = 5000;
  coordination_config_.heartbeat_interval_ms = 1000;
  coordination_config_.sync_interval_ms = 10000;
  coordination_config_.failover_timeout_ms = 3000;
  coordination_config_.enable_automatic_failover = true;
  coordination_config_.enable_cross_region_sync = true;
  coordination_config_.enable_load_balancing = true;
  coordination_config_.global_coordination_strategy = "hybrid";

  // Initialize global consensus state
  global_consensus_state_.consensus_term = 1;
  global_consensus_state_.state_version = 1;
  global_consensus_state_.last_update = std::chrono::system_clock::now();

  // Initialize statistics
  std::memset(&current_stats_, 0, sizeof(current_stats_));

  std::cout << "Multi-master coordinator initialized: " << coordinator_id_
            << std::endl;
}

MultiMasterCoordinator::~MultiMasterCoordinator() { stop(); }

bool MultiMasterCoordinator::start() {
  if (running_.load())
    return false;

  running_.store(true);

  // Start background threads
  global_consensus_thread_ =
      std::thread(&MultiMasterCoordinator::global_consensus_loop, this);
  coordination_thread_ =
      std::thread(&MultiMasterCoordinator::coordination_loop, this);
  sync_coordinator_thread_ =
      std::thread(&MultiMasterCoordinator::sync_coordinator_loop, this);
  performance_monitor_thread_ =
      std::thread(&MultiMasterCoordinator::performance_monitor_loop, this);

  std::cout << "Multi-master coordinator started: " << coordinator_id_
            << std::endl;
  return true;
}

void MultiMasterCoordinator::stop() {
  if (!running_.load())
    return;

  running_.store(false);

  // Stop all threads
  if (global_consensus_thread_.joinable())
    global_consensus_thread_.join();
  if (coordination_thread_.joinable())
    coordination_thread_.join();
  if (sync_coordinator_thread_.joinable())
    sync_coordinator_thread_.join();
  if (performance_monitor_thread_.joinable())
    performance_monitor_thread_.join();

  std::cout << "Multi-master coordinator stopped: " << coordinator_id_
            << std::endl;
}

bool MultiMasterCoordinator::initiate_global_consensus() {
  std::cout << "Initiating global consensus process" << std::endl;

  std::lock_guard<std::mutex> lock(global_state_mutex_);

  // Start new consensus term
  global_consensus_state_.consensus_term++;
  global_consensus_state_.state_version++;
  global_consensus_state_.last_update = std::chrono::system_clock::now();

  // Elect global leader
  if (!elect_global_leader()) {
    std::cerr << "Failed to elect global leader" << std::endl;
    return false;
  }

  // Update master assignments
  if (!update_master_assignments()) {
    std::cerr << "Failed to update master assignments" << std::endl;
    return false;
  }

  // Reconcile regional leaders
  if (!reconcile_regional_leaders()) {
    std::cerr << "Failed to reconcile regional leaders" << std::endl;
    return false;
  }

  // Balance shard assignments
  if (!balance_shard_assignments()) {
    std::cerr << "Failed to balance shard assignments" << std::endl;
    return false;
  }

  notify_coordination_event(
      "global_consensus_completed",
      "term=" + std::to_string(global_consensus_state_.consensus_term));

  return true;
}

bool MultiMasterCoordinator::update_global_state(
    const GlobalConsensusState &state) {
  std::lock_guard<std::mutex> lock(global_state_mutex_);

  if (state.state_version > global_consensus_state_.state_version) {
    global_consensus_state_ = state;

    notify_coordination_event("global_state_updated",
                              "version=" + std::to_string(state.state_version));

    std::cout << "Updated global consensus state to version: "
              << state.state_version << std::endl;
    return true;
  }

  return false;
}

GlobalConsensusState
MultiMasterCoordinator::get_global_consensus_state() const {
  std::lock_guard<std::mutex> lock(global_state_mutex_);
  return global_consensus_state_;
}

bool MultiMasterCoordinator::validate_global_state_consistency() {
  std::lock_guard<std::mutex> lock(global_state_mutex_);

  // Validate that we have a global leader
  if (global_consensus_state_.global_leader.empty()) {
    std::cout << "Global state inconsistency: No global leader" << std::endl;
    return false;
  }

  // Validate master assignments
  if (global_consensus_state_.master_assignments.empty()) {
    std::cout << "Global state inconsistency: No master assignments"
              << std::endl;
    return false;
  }

  // Validate minimum masters for consensus
  if (global_consensus_state_.master_assignments.size() <
      coordination_config_.min_masters_for_consensus) {
    std::cout
        << "Global state inconsistency: Insufficient masters for consensus"
        << std::endl;
    return false;
  }

  return true;
}

bool MultiMasterCoordinator::coordinate_master_election(
    MasterRole role, const std::string &region) {
  std::cout << "Coordinating master election for role: "
            << master_role_to_string(role);
  if (!region.empty()) {
    std::cout << " in region: " << region;
  }
  std::cout << std::endl;

  // Create coordination event
  MasterCoordinationEvent event;
  event.event_id = generate_event_id();
  event.event_type = "election";
  event.source_master = coordinator_id_;
  event.parameters["role"] = master_role_to_string(role);
  event.parameters["region"] = region;
  event.timestamp = std::chrono::system_clock::now();
  event.is_processed = false;

  // Find best candidate
  std::string candidate = select_best_master_candidate(role, region);
  if (candidate.empty()) {
    std::cerr << "No suitable candidate found for master election" << std::endl;
    return false;
  }

  event.target_master = candidate;
  event.parameters["candidate"] = candidate;

  // Process election
  if (multi_master_manager_) {
    if (!multi_master_manager_->initiate_master_election(role)) {
      std::cerr << "Failed to initiate master election" << std::endl;
      return false;
    }
  }

  // Broadcast event
  broadcast_coordination_event(event);

  // Update global state
  std::lock_guard<std::mutex> lock(global_state_mutex_);
  global_consensus_state_.master_assignments[candidate] = role;
  if (!region.empty()) {
    global_consensus_state_.region_leaders[region] = candidate;
  }
  global_consensus_state_.state_version++;

  notify_coordination_event("master_elected", "master=" + candidate + ",role=" +
                                                  master_role_to_string(role));

  return true;
}

bool MultiMasterCoordinator::coordinate_master_promotion(
    const std::string &node_id, MasterRole role) {
  std::cout << "Coordinating master promotion: " << node_id
            << " to role: " << master_role_to_string(role) << std::endl;

  if (!validate_master_capacity(node_id, role)) {
    std::cerr << "Node does not have capacity for master role" << std::endl;
    return false;
  }

  // Create coordination event
  MasterCoordinationEvent event;
  event.event_id = generate_event_id();
  event.event_type = "promotion";
  event.source_master = coordinator_id_;
  event.target_master = node_id;
  event.parameters["role"] = master_role_to_string(role);
  event.timestamp = std::chrono::system_clock::now();
  event.is_processed = false;

  // Execute promotion
  if (multi_master_manager_) {
    // For remote nodes, we would send the promotion command
    // For local node, promote directly
    if (node_id == coordinator_id_) {
      if (!multi_master_manager_->promote_to_master(role)) {
        std::cerr << "Failed to promote local node to master" << std::endl;
        return false;
      }
    }
  }

  // Update load balancer if needed
  if (load_balancer_ && role == MasterRole::RPC_MASTER) {
    BackendServer server;
    server.server_id = node_id;
    server.address = config_.node_address;
    server.port = config_.node_port;
    server.region = config_.node_region;
    server.weight = 100;
    server.max_connections = 1000;
    server.is_active = true;

    load_balancer_->register_backend_server(server);
  }

  // Broadcast event
  broadcast_coordination_event(event);

  // Update global state
  std::lock_guard<std::mutex> lock(global_state_mutex_);
  global_consensus_state_.master_assignments[node_id] = role;
  global_consensus_state_.state_version++;

  notify_coordination_event("master_promoted", "master=" + node_id + ",role=" +
                                                   master_role_to_string(role));

  return true;
}

bool MultiMasterCoordinator::coordinate_master_demotion(
    const std::string &master_id) {
  std::cout << "Coordinating master demotion: " << master_id << std::endl;

  // Create coordination event
  MasterCoordinationEvent event;
  event.event_id = generate_event_id();
  event.event_type = "demotion";
  event.source_master = coordinator_id_;
  event.target_master = master_id;
  event.timestamp = std::chrono::system_clock::now();
  event.is_processed = false;

  // Execute demotion
  if (multi_master_manager_) {
    if (master_id == coordinator_id_) {
      if (!multi_master_manager_->demote_from_master()) {
        std::cerr << "Failed to demote local master" << std::endl;
        return false;
      }
    }
  }

  // Update load balancer
  if (load_balancer_) {
    load_balancer_->unregister_backend_server(master_id);
  }

  // Broadcast event
  broadcast_coordination_event(event);

  // Update global state
  std::lock_guard<std::mutex> lock(global_state_mutex_);
  global_consensus_state_.master_assignments.erase(master_id);

  // Remove from regional leaders if present
  for (auto it = global_consensus_state_.region_leaders.begin();
       it != global_consensus_state_.region_leaders.end();) {
    if (it->second == master_id) {
      it = global_consensus_state_.region_leaders.erase(it);
    } else {
      ++it;
    }
  }

  global_consensus_state_.state_version++;

  notify_coordination_event("master_demoted", "master=" + master_id);

  return true;
}

bool MultiMasterCoordinator::coordinate_failover(
    const std::string &failed_master) {
  std::cout << "Coordinating failover for failed master: " << failed_master
            << std::endl;

  std::lock_guard<std::mutex> lock(global_state_mutex_);

  // Get failed master's role
  auto role_it = global_consensus_state_.master_assignments.find(failed_master);
  if (role_it == global_consensus_state_.master_assignments.end()) {
    std::cerr << "Failed master not found in assignments" << std::endl;
    return false;
  }

  MasterRole failed_role = role_it->second;

  // Find replacement candidate
  std::string replacement = select_best_master_candidate(failed_role);
  if (replacement.empty()) {
    std::cerr << "No replacement candidate found for failover" << std::endl;
    return false;
  }

  // Create coordination event
  MasterCoordinationEvent event;
  event.event_id = generate_event_id();
  event.event_type = "failover";
  event.source_master = failed_master;
  event.target_master = replacement;
  event.parameters["failed_role"] = master_role_to_string(failed_role);
  event.timestamp = std::chrono::system_clock::now();
  event.is_processed = false;

  // Execute failover
  global_consensus_state_.master_assignments.erase(failed_master);
  global_consensus_state_.master_assignments[replacement] = failed_role;

  // Update regional leaders
  for (auto &region_pair : global_consensus_state_.region_leaders) {
    if (region_pair.second == failed_master) {
      region_pair.second = replacement;
    }
  }

  global_consensus_state_.state_version++;

  // Broadcast event
  broadcast_coordination_event(event);

  // Update statistics
  std::lock_guard<std::mutex> stats_lock(stats_mutex_);
  current_stats_.successful_failovers++;

  notify_coordination_event("master_failover_completed",
                            "failed=" + failed_master +
                                ",replacement=" + replacement);

  return true;
}

bool MultiMasterCoordinator::initiate_cross_master_sync(
    const CrossMasterSyncRequest &request) {
  std::lock_guard<std::mutex> lock(sync_mutex_);

  pending_sync_requests_.push_back(request);
  sync_timeouts_[request.request_id] =
      std::chrono::system_clock::now() + std::chrono::seconds(30);

  std::cout << "Initiated cross-master sync: " << request.request_id
            << " type: " << request.sync_type
            << " from: " << request.source_master << std::endl;

  notify_coordination_event("sync_initiated", "request=" + request.request_id);

  return true;
}

bool MultiMasterCoordinator::handle_sync_request(
    const CrossMasterSyncRequest &request) {
  std::cout << "Handling sync request: " << request.request_id
            << " type: " << request.sync_type << std::endl;

  // Simulate sync handling based on type
  bool success = true;

  if (request.sync_type == "ledger") {
    // Synchronize ledger data
    std::cout << "Synchronizing ledger data from slot " << request.start_slot
              << " to " << request.end_slot << std::endl;
  } else if (request.sync_type == "state") {
    // Synchronize validator state
    std::cout << "Synchronizing validator state" << std::endl;
  } else if (request.sync_type == "config") {
    // Synchronize configuration
    std::cout << "Synchronizing configuration" << std::endl;
  } else if (request.sync_type == "full") {
    // Full synchronization
    std::cout << "Performing full synchronization" << std::endl;
  }

  // Update statistics
  if (success) {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    current_stats_.sync_operations_completed++;
  }

  notify_coordination_event("sync_handled",
                            "request=" + request.request_id +
                                ",success=" + (success ? "true" : "false"));

  return success;
}

std::vector<CrossMasterSyncRequest>
MultiMasterCoordinator::get_pending_sync_requests() const {
  std::lock_guard<std::mutex> lock(sync_mutex_);
  return pending_sync_requests_;
}

bool MultiMasterCoordinator::complete_sync_request(
    const std::string &request_id, bool success) {
  std::lock_guard<std::mutex> lock(sync_mutex_);

  // Remove from pending requests
  pending_sync_requests_.erase(
      std::remove_if(pending_sync_requests_.begin(),
                     pending_sync_requests_.end(),
                     [&request_id](const CrossMasterSyncRequest &req) {
                       return req.request_id == request_id;
                     }),
      pending_sync_requests_.end());

  sync_timeouts_.erase(request_id);

  std::cout << "Completed sync request: " << request_id
            << " success: " << (success ? "true" : "false") << std::endl;

  notify_coordination_event("sync_completed",
                            "request=" + request_id +
                                ",success=" + (success ? "true" : "false"));

  return true;
}

bool MultiMasterCoordinator::coordinate_load_rebalancing() {
  std::cout << "Coordinating load rebalancing across masters" << std::endl;

  if (!load_balancer_) {
    std::cerr << "Load balancer not available for rebalancing" << std::endl;
    return false;
  }

  // Get current server loads
  auto server_loads = load_balancer_->get_server_loads();
  if (server_loads.empty()) {
    return true; // No servers to rebalance
  }

  // Calculate average load
  uint64_t total_load = 0;
  for (const auto &load_pair : server_loads) {
    total_load += load_pair.second;
  }

  double average_load = static_cast<double>(total_load) / server_loads.size();

  // Find overloaded and underloaded servers
  std::vector<std::string> overloaded_servers;
  std::vector<std::string> underloaded_servers;

  for (const auto &load_pair : server_loads) {
    if (load_pair.second > average_load * 1.5) {
      overloaded_servers.push_back(load_pair.first);
    } else if (load_pair.second < average_load * 0.5) {
      underloaded_servers.push_back(load_pair.first);
    }
  }

  // Redistribute traffic
  for (size_t i = 0;
       i < overloaded_servers.size() && i < underloaded_servers.size(); ++i) {
    redistribute_traffic(overloaded_servers[i], underloaded_servers[i]);
  }

  notify_coordination_event(
      "load_rebalancing_completed",
      "overloaded=" + std::to_string(overloaded_servers.size()) +
          ",underloaded=" + std::to_string(underloaded_servers.size()));

  return true;
}

bool MultiMasterCoordinator::redistribute_traffic(
    const std::string &source_master, const std::string &target_master) {
  std::cout << "Redistributing traffic from " << source_master << " to "
            << target_master << std::endl;

  if (!load_balancer_) {
    return false;
  }

  // Create new load balancing rule to redirect some traffic
  LoadBalancingRule redistribution_rule;
  redistribution_rule.rule_name =
      "redistribution_" + source_master + "_" + target_master;
  redistribution_rule.service_pattern = ".*";
  redistribution_rule.strategy = LoadBalancingStrategy::WEIGHTED_ROUND_ROBIN;
  redistribution_rule.backend_servers = {source_master, target_master};
  redistribution_rule.server_weights[source_master] = 30; // Reduce weight
  redistribution_rule.server_weights[target_master] = 70; // Increase weight
  redistribution_rule.health_check_interval_ms = 3000;
  redistribution_rule.max_retries = 2;
  redistribution_rule.enable_session_affinity = false;

  load_balancer_->add_load_balancing_rule(redistribution_rule);

  notify_coordination_event("traffic_redistributed",
                            "source=" + source_master +
                                ",target=" + target_master);

  return true;
}

bool MultiMasterCoordinator::update_traffic_weights(
    const std::unordered_map<std::string, uint32_t> &weights) {
  std::cout << "Updating traffic weights for " << weights.size() << " masters"
            << std::endl;

  if (!load_balancer_) {
    return false;
  }

  // Update weights in existing rules
  auto rules = load_balancer_->get_load_balancing_rules();
  for (auto &rule : rules) {
    if (rule.strategy == LoadBalancingStrategy::WEIGHTED_ROUND_ROBIN) {
      for (const auto &weight_pair : weights) {
        rule.server_weights[weight_pair.first] = weight_pair.second;
      }
      load_balancer_->update_load_balancing_rule(rule);
    }
  }

  return true;
}

bool MultiMasterCoordinator::update_master_performance(
    const MasterPerformanceMetrics &metrics) {
  std::lock_guard<std::mutex> lock(performance_mutex_);

  performance_metrics_[metrics.master_id] = metrics;

  return true;
}

std::vector<MasterPerformanceMetrics>
MultiMasterCoordinator::get_master_performance_metrics() const {
  std::lock_guard<std::mutex> lock(performance_mutex_);

  std::vector<MasterPerformanceMetrics> metrics;
  for (const auto &pair : performance_metrics_) {
    metrics.push_back(pair.second);
  }

  return metrics;
}

std::string MultiMasterCoordinator::identify_performance_bottleneck() const {
  std::lock_guard<std::mutex> lock(performance_mutex_);

  if (performance_metrics_.empty()) {
    return "";
  }

  std::string bottleneck_master;
  double worst_score = 1.0;

  for (const auto &pair : performance_metrics_) {
    const auto &metrics = pair.second;

    // Calculate performance score (lower is worse)
    double cpu_score = 1.0 - metrics.cpu_utilization;
    double memory_score = 1.0 - metrics.memory_utilization;
    double response_score =
        std::max(0.0, 1.0 - (metrics.average_response_time.count() / 1000.0));
    double error_score = 1.0 - metrics.error_rate;

    double composite_score =
        (cpu_score + memory_score + response_score + error_score) / 4.0;

    if (composite_score < worst_score) {
      worst_score = composite_score;
      bottleneck_master = pair.first;
    }
  }

  return bottleneck_master;
}

bool MultiMasterCoordinator::optimize_master_allocation() {
  std::cout << "Optimizing master allocation based on performance metrics"
            << std::endl;

  auto performance_metrics = get_master_performance_metrics();
  if (performance_metrics.empty()) {
    return true;
  }

  // Identify bottleneck
  std::string bottleneck = identify_performance_bottleneck();
  if (!bottleneck.empty()) {
    std::cout << "Performance bottleneck identified: " << bottleneck
              << std::endl;

    // Consider redistributing load or promoting additional masters
    coordinate_load_rebalancing();
  }

  // Check if we need more masters in any region
  std::lock_guard<std::mutex> lock(global_state_mutex_);

  std::unordered_map<std::string, uint32_t> masters_per_region;

  // Get active masters and count by region
  if (multi_master_manager_) {
    auto active_masters = multi_master_manager_->get_active_masters();
    for (const auto &master : active_masters) {
      masters_per_region[master.region]++;
    }
  }

  // Promote additional masters if needed
  for (const auto &region_pair : masters_per_region) {
    if (region_pair.second < coordination_config_.max_masters_per_region) {
      coordinate_master_election(MasterRole::RPC_MASTER, region_pair.first);
    }
  }

  notify_coordination_event("master_allocation_optimized");

  return true;
}

bool MultiMasterCoordinator::setup_regional_coordination(
    const std::string &region) {
  std::cout << "Setting up regional coordination for region: " << region
            << std::endl;

  // Elect regional leader
  coordinate_master_election(MasterRole::GLOBAL_MASTER, region);

  // Setup cross-region links if topology manager is available
  if (topology_manager_) {
    CrossRegionLink link;
    link.source_region = "default"; // Current region
    link.target_region = region;
    link.bandwidth_mbps = 1000;
    link.latency_ms = 50;
    link.reliability_score = 0.99;

    topology_manager_->establish_cross_region_link(link);
  }

  notify_coordination_event("regional_coordination_setup", "region=" + region);

  return true;
}

bool MultiMasterCoordinator::coordinate_cross_region_operations(
    const std::string &source_region, const std::string &target_region) {
  std::cout << "Coordinating cross-region operations: " << source_region
            << " -> " << target_region << std::endl;

  // Create cross-region sync request
  CrossMasterSyncRequest sync_request;
  sync_request.request_id = generate_sync_request_id();
  sync_request.source_master =
      global_consensus_state_.region_leaders[source_region];
  sync_request.sync_type = "state";
  sync_request.start_slot = 0;
  sync_request.end_slot = UINT64_MAX;
  sync_request.is_urgent = false;

  initiate_cross_master_sync(sync_request);

  return true;
}

std::vector<std::string> MultiMasterCoordinator::get_regional_leaders() const {
  std::lock_guard<std::mutex> lock(global_state_mutex_);

  std::vector<std::string> leaders;
  for (const auto &pair : global_consensus_state_.region_leaders) {
    leaders.push_back(pair.second);
  }

  return leaders;
}

bool MultiMasterCoordinator::process_coordination_event(
    const MasterCoordinationEvent &event) {
  std::cout << "Processing coordination event: " << event.event_type << " ("
            << event.event_id << ")" << std::endl;

  if (event.event_type == "election") {
    // Handle master election event
    auto role_it = event.parameters.find("role");
    if (role_it != event.parameters.end()) {
      MasterRole role = string_to_master_role(role_it->second);
      return coordinate_master_election(role, event.parameters.at("region"));
    }
  } else if (event.event_type == "promotion") {
    // Handle master promotion event
    auto role_it = event.parameters.find("role");
    if (role_it != event.parameters.end()) {
      MasterRole role = string_to_master_role(role_it->second);
      return coordinate_master_promotion(event.target_master, role);
    }
  } else if (event.event_type == "demotion") {
    // Handle master demotion event
    return coordinate_master_demotion(event.target_master);
  } else if (event.event_type == "failover") {
    // Handle failover event
    return coordinate_failover(event.source_master);
  }

  return false;
}

std::vector<MasterCoordinationEvent>
MultiMasterCoordinator::get_pending_events() const {
  std::lock_guard<std::mutex> lock(events_mutex_);

  std::vector<MasterCoordinationEvent> pending;
  for (const auto &event : pending_events_) {
    if (!event.is_processed) {
      pending.push_back(event);
    }
  }

  return pending;
}

bool MultiMasterCoordinator::broadcast_coordination_event(
    const MasterCoordinationEvent &event) {
  std::lock_guard<std::mutex> lock(events_mutex_);

  pending_events_.push_back(event);

  std::cout << "Broadcasted coordination event: " << event.event_type << " ("
            << event.event_id << ")" << std::endl;

  return true;
}

bool MultiMasterCoordinator::update_configuration(
    const MultiMasterConfiguration &config) {
  coordination_config_ = config;

  std::cout << "Updated multi-master configuration" << std::endl;
  notify_coordination_event("configuration_updated");

  return true;
}

MultiMasterConfiguration MultiMasterCoordinator::get_configuration() const {
  return coordination_config_;
}

bool MultiMasterCoordinator::validate_configuration() const {
  if (coordination_config_.min_masters_for_consensus < 1) {
    return false;
  }

  if (coordination_config_.max_masters_per_region <
      coordination_config_.min_masters_for_consensus) {
    return false;
  }

  return true;
}

bool MultiMasterCoordinator::perform_health_check() {
  bool all_healthy = true;

  // Check global consensus state validity
  if (!validate_global_state_consistency()) {
    all_healthy = false;
  }

  // Check component health
  if (multi_master_manager_ && !multi_master_manager_->is_running()) {
    all_healthy = false;
  }

  if (topology_manager_ && !topology_manager_->is_running()) {
    all_healthy = false;
  }

  if (load_balancer_ && !load_balancer_->is_running()) {
    all_healthy = false;
  }

  return all_healthy;
}

bool MultiMasterCoordinator::is_coordination_healthy() const {
  std::lock_guard<std::mutex> lock(global_state_mutex_);

  // Check if we have recent global consensus
  auto now = std::chrono::system_clock::now();
  auto time_since_consensus = std::chrono::duration_cast<std::chrono::minutes>(
      now - global_consensus_state_.last_update);

  if (time_since_consensus.count() > 10) { // 10 minutes
    return false;
  }

  // Check if we have sufficient masters
  if (global_consensus_state_.master_assignments.size() <
      coordination_config_.min_masters_for_consensus) {
    return false;
  }

  return true;
}

std::unordered_map<std::string, std::string>
MultiMasterCoordinator::get_coordination_status() const {
  std::unordered_map<std::string, std::string> status;

  std::lock_guard<std::mutex> global_lock(global_state_mutex_);
  std::lock_guard<std::mutex> stats_lock(stats_mutex_);

  status["coordinator_id"] = coordinator_id_;
  status["global_leader"] = global_consensus_state_.global_leader;
  status["consensus_term"] =
      std::to_string(global_consensus_state_.consensus_term);
  status["state_version"] =
      std::to_string(global_consensus_state_.state_version);
  status["total_masters"] =
      std::to_string(global_consensus_state_.master_assignments.size());
  status["healthy"] = is_coordination_healthy() ? "true" : "false";

  return status;
}

MultiMasterCoordinator::CoordinatorStats
MultiMasterCoordinator::get_statistics() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  return current_stats_;
}

// Private methods
void MultiMasterCoordinator::global_consensus_loop() {
  while (running_.load()) {
    if (coordination_config_.global_coordination_strategy == "centralized" ||
        coordination_config_.global_coordination_strategy == "hybrid") {

      if (!validate_global_state_consistency()) {
        initiate_global_consensus();
      }
    }

    update_coordination_statistics();

    std::this_thread::sleep_for(std::chrono::seconds(30));
  }
}

void MultiMasterCoordinator::coordination_loop() {
  while (running_.load()) {
    // Process pending coordination events
    auto pending_events = get_pending_events();
    for (auto &event : pending_events) {
      process_coordination_event(event);

      std::lock_guard<std::mutex> lock(events_mutex_);
      processed_events_[event.event_id] = true;
    }

    cleanup_expired_events();

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
}

void MultiMasterCoordinator::sync_coordinator_loop() {
  while (running_.load()) {
    auto pending_requests = get_pending_sync_requests();

    for (const auto &request : pending_requests) {
      if (handle_sync_request(request)) {
        complete_sync_request(request.request_id, true);
      } else {
        complete_sync_request(request.request_id, false);
      }
    }

    cleanup_expired_sync_requests();

    std::this_thread::sleep_for(
        std::chrono::milliseconds(coordination_config_.sync_interval_ms));
  }
}

void MultiMasterCoordinator::performance_monitor_loop() {
  while (running_.load()) {
    // Analyze performance metrics and optimize if needed
    if (!identify_performance_bottleneck().empty()) {
      optimize_master_allocation();
    }

    // Coordinate load rebalancing if enabled
    if (coordination_config_.enable_load_balancing) {
      coordinate_load_rebalancing();
    }

    std::this_thread::sleep_for(std::chrono::seconds(60)); // Run every minute
  }
}

bool MultiMasterCoordinator::elect_global_leader() {
  if (!multi_master_manager_) {
    return false;
  }

  auto masters = multi_master_manager_->get_active_masters();
  if (masters.empty()) {
    return false;
  }

  // Simple election - select master with highest fitness
  std::string best_candidate;
  double best_fitness = 0.0;

  for (const auto &master : masters) {
    double fitness =
        calculate_master_fitness(master.node_id, MasterRole::GLOBAL_MASTER);
    if (fitness > best_fitness) {
      best_fitness = fitness;
      best_candidate = master.node_id;
    }
  }

  if (!best_candidate.empty()) {
    global_consensus_state_.global_leader = best_candidate;
    std::cout << "Elected global leader: " << best_candidate << std::endl;
    return true;
  }

  return false;
}

bool MultiMasterCoordinator::update_master_assignments() {
  if (!multi_master_manager_) {
    return false;
  }

  auto masters = multi_master_manager_->get_active_masters();

  global_consensus_state_.master_assignments.clear();

  for (const auto &master : masters) {
    global_consensus_state_.master_assignments[master.node_id] = master.role;
  }

  return true;
}

bool MultiMasterCoordinator::reconcile_regional_leaders() {
  // Ensure each region has a leader
  std::unordered_set<std::string> regions;

  if (multi_master_manager_) {
    auto masters = multi_master_manager_->get_active_masters();
    for (const auto &master : masters) {
      if (!master.region.empty()) {
        regions.insert(master.region);
      }
    }
  }

  for (const auto &region : regions) {
    if (global_consensus_state_.region_leaders.find(region) ==
        global_consensus_state_.region_leaders.end()) {
      // Need to elect a regional leader
      std::string regional_leader =
          select_best_master_candidate(MasterRole::GLOBAL_MASTER, region);
      if (!regional_leader.empty()) {
        global_consensus_state_.region_leaders[region] = regional_leader;
      }
    }
  }

  return true;
}

bool MultiMasterCoordinator::balance_shard_assignments() {
  // Balance shard assignments across available masters
  auto shard_masters =
      multi_master_manager_->get_masters_by_role(MasterRole::SHARD_MASTER);

  global_consensus_state_.shard_masters.clear();

  for (size_t i = 0; i < shard_masters.size(); ++i) {
    global_consensus_state_.shard_masters[i] = shard_masters[i].node_id;
  }

  return true;
}

std::string MultiMasterCoordinator::select_best_master_candidate(
    MasterRole role, const std::string &region) const {
  if (!multi_master_manager_) {
    return "";
  }

  auto masters = multi_master_manager_->get_active_masters();

  std::string best_candidate;
  double best_fitness = 0.0;

  for (const auto &master : masters) {
    // Filter by region if specified
    if (!region.empty() && master.region != region) {
      continue;
    }

    // Check capacity
    if (!validate_master_capacity(master.node_id, role)) {
      continue;
    }

    double fitness = calculate_master_fitness(master.node_id, role);
    if (fitness > best_fitness) {
      best_fitness = fitness;
      best_candidate = master.node_id;
    }
  }

  return best_candidate;
}

bool MultiMasterCoordinator::validate_master_capacity(
    const std::string &master_id, MasterRole role) const {
  // Check if master has capacity for additional role
  std::lock_guard<std::mutex> lock(performance_mutex_);

  auto metrics_it = performance_metrics_.find(master_id);
  if (metrics_it != performance_metrics_.end()) {
    const auto &metrics = metrics_it->second;

    // Simple capacity check based on resource utilization
    if (metrics.cpu_utilization > 0.8 || metrics.memory_utilization > 0.8) {
      return false;
    }
  }

  return true;
}

double
MultiMasterCoordinator::calculate_master_fitness(const std::string &master_id,
                                                 MasterRole role) const {
  std::lock_guard<std::mutex> lock(performance_mutex_);

  auto metrics_it = performance_metrics_.find(master_id);
  if (metrics_it == performance_metrics_.end()) {
    return 0.5; // Default fitness
  }

  const auto &metrics = metrics_it->second;

  // Calculate fitness based on performance metrics
  double cpu_fitness = 1.0 - metrics.cpu_utilization;
  double memory_fitness = 1.0 - metrics.memory_utilization;
  double response_fitness =
      std::max(0.0, 1.0 - (metrics.average_response_time.count() / 1000.0));
  double error_fitness = 1.0 - metrics.error_rate;

  return (cpu_fitness + memory_fitness + response_fitness + error_fitness) /
         4.0;
}

bool MultiMasterCoordinator::synchronize_masters(
    const std::vector<std::string> &masters) {
  // Create sync requests for all masters
  for (const auto &master : masters) {
    CrossMasterSyncRequest request;
    request.request_id = generate_sync_request_id();
    request.source_master = coordinator_id_;
    request.sync_type = "state";
    request.start_slot = 0;
    request.end_slot = UINT64_MAX;
    request.is_urgent = false;

    initiate_cross_master_sync(request);
  }

  return true;
}

bool MultiMasterCoordinator::validate_sync_consistency(
    const std::vector<std::string> &masters) {
  // Validate that all masters have consistent state
  // In a real implementation, this would check state hashes, slot numbers, etc.
  return true;
}

bool MultiMasterCoordinator::resolve_sync_conflicts(
    const std::vector<std::string> &masters) {
  // Resolve any conflicts in master states
  // In a real implementation, this would use conflict resolution algorithms
  return true;
}

void MultiMasterCoordinator::update_coordination_statistics() {
  std::lock_guard<std::mutex> stats_lock(stats_mutex_);
  std::lock_guard<std::mutex> global_lock(global_state_mutex_);

  current_stats_.total_masters =
      global_consensus_state_.master_assignments.size();
  current_stats_.active_masters = 0;
  current_stats_.masters_by_region =
      global_consensus_state_.region_leaders.size();
  current_stats_.last_global_consensus = global_consensus_state_.last_update;

  // Count active masters
  if (multi_master_manager_) {
    auto active_masters = multi_master_manager_->get_active_masters();
    current_stats_.active_masters = active_masters.size();
  }

  // Calculate coordination efficiency (simplified)
  if (current_stats_.total_coordination_events > 0) {
    current_stats_.coordination_efficiency =
        static_cast<double>(current_stats_.successful_failovers) /
        current_stats_.total_coordination_events;
  }

  current_stats_.total_coordination_events++;
}

void MultiMasterCoordinator::cleanup_expired_events() {
  std::lock_guard<std::mutex> lock(events_mutex_);

  auto now = std::chrono::system_clock::now();

  pending_events_.erase(
      std::remove_if(pending_events_.begin(), pending_events_.end(),
                     [&now](const MasterCoordinationEvent &event) {
                       auto age =
                           std::chrono::duration_cast<std::chrono::hours>(
                               now - event.timestamp);
                       return age.count() >
                              1; // Remove events older than 1 hour
                     }),
      pending_events_.end());
}

void MultiMasterCoordinator::cleanup_expired_sync_requests() {
  std::lock_guard<std::mutex> lock(sync_mutex_);

  auto now = std::chrono::system_clock::now();

  for (auto it = sync_timeouts_.begin(); it != sync_timeouts_.end();) {
    if (now > it->second) {
      // Timeout expired, remove request
      pending_sync_requests_.erase(
          std::remove_if(pending_sync_requests_.begin(),
                         pending_sync_requests_.end(),
                         [&it](const CrossMasterSyncRequest &req) {
                           return req.request_id == it->first;
                         }),
          pending_sync_requests_.end());

      it = sync_timeouts_.erase(it);
    } else {
      ++it;
    }
  }
}

std::string MultiMasterCoordinator::generate_event_id() const {
  static std::atomic<uint64_t> counter{0};
  return "coord_event_" + std::to_string(counter.fetch_add(1)) + "_" +
         std::to_string(std::time(nullptr));
}

std::string MultiMasterCoordinator::generate_sync_request_id() const {
  static std::atomic<uint64_t> counter{0};
  return "sync_req_" + std::to_string(counter.fetch_add(1)) + "_" +
         std::to_string(std::time(nullptr));
}

void MultiMasterCoordinator::notify_coordination_event(
    const std::string &event_type, const std::string &details) {
  if (event_callback_) {
    event_callback_(event_type, details);
  }
}

// Utility functions
const char *
master_coordination_event_type_to_string(const std::string &event_type) {
  return event_type.c_str();
}

std::string create_coordination_event_details(
    const std::unordered_map<std::string, std::string> &parameters) {
  std::ostringstream oss;
  for (const auto &pair : parameters) {
    oss << pair.first << "=" << pair.second << ",";
  }
  std::string result = oss.str();
  if (!result.empty()) {
    result.pop_back(); // Remove trailing comma
  }
  return result;
}

} // namespace cluster
} // namespace slonana