#include "network/topology_manager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <sstream>

namespace slonana {
namespace network {

NetworkTopologyManager::NetworkTopologyManager(const std::string &node_id,
                                               const ValidatorConfig &config)
    : node_id_(node_id), config_(config), running_(false) {

  // Initialize default load balancing policies
  LoadBalancingPolicy default_policy;
  default_policy.policy_name = "default";
  default_policy.algorithm = "least_connections";
  default_policy.max_connections_per_node = 1000;
  default_policy.health_check_interval_ms = 5000;
  default_policy.enable_sticky_sessions = false;
  load_balancing_policies_["default"] = default_policy;

  LoadBalancingPolicy rpc_policy;
  rpc_policy.policy_name = "rpc";
  rpc_policy.algorithm = "round_robin";
  rpc_policy.max_connections_per_node = 500;
  rpc_policy.health_check_interval_ms = 3000;
  rpc_policy.enable_sticky_sessions = true;
  load_balancing_policies_["rpc"] = rpc_policy;

  // Initialize metrics
  std::memset(&current_metrics_, 0, sizeof(current_metrics_));

  std::cout << "Network topology manager initialized for node: " << node_id_
            << std::endl;
}

NetworkTopologyManager::~NetworkTopologyManager() { stop(); }

bool NetworkTopologyManager::start() {
  if (running_.load())
    return false;

  running_.store(true);

  // Start background threads
  topology_monitor_thread_ =
      std::thread(&NetworkTopologyManager::topology_monitor_loop, this);
  health_checker_thread_ =
      std::thread(&NetworkTopologyManager::health_checker_loop, this);
  metrics_collector_thread_ =
      std::thread(&NetworkTopologyManager::metrics_collector_loop, this);
  partition_manager_thread_ =
      std::thread(&NetworkTopologyManager::partition_manager_loop, this);

  std::cout << "Network topology manager started for node: " << node_id_
            << std::endl;
  return true;
}

void NetworkTopologyManager::stop() {
  if (!running_.load())
    return;

  running_.store(false);

  // Stop all threads
  if (topology_monitor_thread_.joinable())
    topology_monitor_thread_.join();
  if (health_checker_thread_.joinable())
    health_checker_thread_.join();
  if (metrics_collector_thread_.joinable())
    metrics_collector_thread_.join();
  if (partition_manager_thread_.joinable())
    partition_manager_thread_.join();

  std::cout << "Network topology manager stopped for node: " << node_id_
            << std::endl;
}

bool NetworkTopologyManager::register_node(const NetworkNode &node) {
  if (!validate_network_node(node)) {
    std::cerr << "Invalid network node configuration" << std::endl;
    return false;
  }

  std::lock_guard<std::mutex> lock(nodes_mutex_);

  NetworkNode new_node = node;
  new_node.last_seen = std::chrono::system_clock::now();
  new_node.is_active = true;

  nodes_[node.node_id] = new_node;

  // Update regional mapping
  if (!node.region.empty()) {
    nodes_by_region_[node.region].insert(node.node_id);
  }

  // Update capability mapping
  for (const auto &capability : node.capabilities) {
    nodes_by_capability_[capability].insert(node.node_id);
  }

  notify_network_event("node_registered", node.node_id,
                       "region=" + node.region);

  std::cout << "Registered network node: " << node.node_id
            << " in region: " << node.region << " with "
            << node.capabilities.size() << " capabilities" << std::endl;

  return true;
}

bool NetworkTopologyManager::unregister_node(const std::string &node_id) {
  std::lock_guard<std::mutex> lock(nodes_mutex_);

  auto it = nodes_.find(node_id);
  if (it == nodes_.end()) {
    return false;
  }

  NetworkNode node = it->second;

  // Remove from regional mapping
  if (!node.region.empty()) {
    nodes_by_region_[node.region].erase(node_id);
  }

  // Remove from capability mapping
  for (const auto &capability : node.capabilities) {
    nodes_by_capability_[capability].erase(node_id);
  }

  // Remove from nodes registry
  nodes_.erase(it);

  notify_network_event("node_unregistered", node_id, "region=" + node.region);

  std::cout << "Unregistered network node: " << node_id << std::endl;
  return true;
}

bool NetworkTopologyManager::update_node_status(const std::string &node_id,
                                                bool is_active) {
  std::lock_guard<std::mutex> lock(nodes_mutex_);

  auto it = nodes_.find(node_id);
  if (it == nodes_.end()) {
    return false;
  }

  it->second.is_active = is_active;
  it->second.last_seen = std::chrono::system_clock::now();

  notify_network_event(is_active ? "node_activated" : "node_deactivated",
                       node_id);

  return true;
}

std::vector<NetworkNode> NetworkTopologyManager::get_active_nodes() const {
  std::lock_guard<std::mutex> lock(nodes_mutex_);

  std::vector<NetworkNode> active_nodes;
  for (const auto &pair : nodes_) {
    if (pair.second.is_active) {
      active_nodes.push_back(pair.second);
    }
  }

  return active_nodes;
}

std::vector<NetworkNode>
NetworkTopologyManager::get_nodes_by_region(const std::string &region) const {
  std::lock_guard<std::mutex> lock(nodes_mutex_);

  std::vector<NetworkNode> regional_nodes;

  auto region_it = nodes_by_region_.find(region);
  if (region_it != nodes_by_region_.end()) {
    for (const auto &node_id : region_it->second) {
      auto node_it = nodes_.find(node_id);
      if (node_it != nodes_.end() && node_it->second.is_active) {
        regional_nodes.push_back(node_it->second);
      }
    }
  }

  return regional_nodes;
}

std::vector<NetworkNode> NetworkTopologyManager::get_nodes_by_capability(
    const std::string &capability) const {
  std::lock_guard<std::mutex> lock(nodes_mutex_);

  std::vector<NetworkNode> capable_nodes;

  auto cap_it = nodes_by_capability_.find(capability);
  if (cap_it != nodes_by_capability_.end()) {
    for (const auto &node_id : cap_it->second) {
      auto node_it = nodes_.find(node_id);
      if (node_it != nodes_.end() && node_it->second.is_active) {
        capable_nodes.push_back(node_it->second);
      }
    }
  }

  return capable_nodes;
}

bool NetworkTopologyManager::create_partition(
    const std::string &partition_id, const std::vector<std::string> &node_ids) {
  std::lock_guard<std::mutex> partitions_lock(partitions_mutex_);
  std::lock_guard<std::mutex> nodes_lock(nodes_mutex_);

  if (partitions_.find(partition_id) != partitions_.end()) {
    std::cerr << "Partition already exists: " << partition_id << std::endl;
    return false;
  }

  // Validate all nodes exist
  for (const auto &node_id : node_ids) {
    if (nodes_.find(node_id) == nodes_.end()) {
      std::cerr << "Node not found for partition: " << node_id << std::endl;
      return false;
    }
  }

  NetworkPartition partition;
  partition.partition_id = partition_id;
  partition.node_ids = node_ids;
  partition.partition_size = node_ids.size();
  partition.is_healthy = true;

  partitions_[partition_id] = partition;

  // Update node-to-partition mapping
  for (const auto &node_id : node_ids) {
    node_to_partition_[node_id] = partition_id;
  }

  notify_network_event("partition_created", partition_id,
                       "size=" + std::to_string(node_ids.size()));

  std::cout << "Created network partition: " << partition_id << " with "
            << node_ids.size() << " nodes" << std::endl;

  return true;
}

bool NetworkTopologyManager::delete_partition(const std::string &partition_id) {
  std::lock_guard<std::mutex> lock(partitions_mutex_);

  auto it = partitions_.find(partition_id);
  if (it == partitions_.end()) {
    return false;
  }

  NetworkPartition partition = it->second;

  // Remove node-to-partition mappings
  for (const auto &node_id : partition.node_ids) {
    node_to_partition_.erase(node_id);
  }

  partitions_.erase(it);

  notify_network_event("partition_deleted", partition_id);

  std::cout << "Deleted network partition: " << partition_id << std::endl;
  return true;
}

bool NetworkTopologyManager::assign_master_to_partition(
    const std::string &partition_id, const std::string &master_id) {
  std::lock_guard<std::mutex> lock(partitions_mutex_);

  auto it = partitions_.find(partition_id);
  if (it == partitions_.end()) {
    return false;
  }

  it->second.primary_master = master_id;

  notify_network_event("master_assigned", partition_id, "master=" + master_id);

  std::cout << "Assigned master " << master_id
            << " to partition: " << partition_id << std::endl;
  return true;
}

std::vector<NetworkPartition> NetworkTopologyManager::get_partitions() const {
  std::lock_guard<std::mutex> lock(partitions_mutex_);

  std::vector<NetworkPartition> partitions;
  for (const auto &pair : partitions_) {
    partitions.push_back(pair.second);
  }

  return partitions;
}

std::string NetworkTopologyManager::get_partition_for_node(
    const std::string &node_id) const {
  std::lock_guard<std::mutex> lock(partitions_mutex_);

  auto it = node_to_partition_.find(node_id);
  return it != node_to_partition_.end() ? it->second : "";
}

bool NetworkTopologyManager::establish_cross_region_link(
    const CrossRegionLink &link) {
  std::lock_guard<std::mutex> lock(links_mutex_);

  std::string link_key = link.source_region + "->" + link.target_region;

  CrossRegionLink new_link = link;
  new_link.is_active = true;
  new_link.reliability_score = calculate_link_reliability(link);

  cross_region_links_[link_key] = new_link;

  notify_network_event("cross_region_link_established", link_key,
                       "bandwidth=" + std::to_string(link.bandwidth_mbps));

  std::cout << "Established cross-region link: " << link.source_region << " -> "
            << link.target_region << " (bandwidth: " << link.bandwidth_mbps
            << " Mbps)" << std::endl;

  return true;
}

bool NetworkTopologyManager::remove_cross_region_link(
    const std::string &source_region, const std::string &target_region) {
  std::lock_guard<std::mutex> lock(links_mutex_);

  std::string link_key = source_region + "->" + target_region;

  auto it = cross_region_links_.find(link_key);
  if (it == cross_region_links_.end()) {
    return false;
  }

  cross_region_links_.erase(it);

  notify_network_event("cross_region_link_removed", link_key);

  std::cout << "Removed cross-region link: " << source_region << " -> "
            << target_region << std::endl;

  return true;
}

std::vector<CrossRegionLink>
NetworkTopologyManager::get_cross_region_links() const {
  std::lock_guard<std::mutex> lock(links_mutex_);

  std::vector<CrossRegionLink> links;
  for (const auto &pair : cross_region_links_) {
    if (pair.second.is_active) {
      links.push_back(pair.second);
    }
  }

  return links;
}

std::vector<std::string> NetworkTopologyManager::find_path_to_region(
    const std::string &target_region) const {
  std::lock_guard<std::mutex> nodes_lock(nodes_mutex_);
  std::lock_guard<std::mutex> links_lock(links_mutex_);

  // Simple pathfinding using available cross-region links
  std::vector<std::string> path;

  // Get current node's region
  std::string current_region = "default"; // TODO: Get from config

  if (current_region == target_region) {
    return {current_region};
  }

  // Direct link check
  std::string direct_link_key = current_region + "->" + target_region;
  auto direct_it = cross_region_links_.find(direct_link_key);
  if (direct_it != cross_region_links_.end() && direct_it->second.is_active) {
    return {current_region, target_region};
  }

  // Multi-hop pathfinding (simplified BFS)
  std::map<std::string, std::string> parent;
  std::vector<std::string> queue = {current_region};
  std::unordered_set<std::string> visited = {current_region};

  while (!queue.empty()) {
    std::string current = queue.front();
    queue.erase(queue.begin());

    for (const auto &link_pair : cross_region_links_) {
      const auto &link = link_pair.second;
      if (link.source_region == current && link.is_active) {
        if (link.target_region == target_region) {
          // Found path, reconstruct it
          path.push_back(target_region);
          std::string node = current;
          while (node != current_region) {
            path.insert(path.begin(), node);
            node = parent[node];
          }
          path.insert(path.begin(), current_region);
          return path;
        }

        if (visited.find(link.target_region) == visited.end()) {
          visited.insert(link.target_region);
          parent[link.target_region] = current;
          queue.push_back(link.target_region);
        }
      }
    }
  }

  return {}; // No path found
}

bool NetworkTopologyManager::add_load_balancing_policy(
    const LoadBalancingPolicy &policy) {
  std::lock_guard<std::mutex> lock(policies_mutex_);

  load_balancing_policies_[policy.policy_name] = policy;

  std::cout << "Added load balancing policy: " << policy.policy_name
            << " with algorithm: " << policy.algorithm << std::endl;

  return true;
}

bool NetworkTopologyManager::remove_load_balancing_policy(
    const std::string &policy_name) {
  std::lock_guard<std::mutex> lock(policies_mutex_);

  auto it = load_balancing_policies_.find(policy_name);
  if (it == load_balancing_policies_.end()) {
    return false;
  }

  load_balancing_policies_.erase(it);

  std::cout << "Removed load balancing policy: " << policy_name << std::endl;
  return true;
}

std::string NetworkTopologyManager::select_node_for_service(
    const std::string &service_type, const std::string &client_region) const {
  std::lock_guard<std::mutex> policies_lock(policies_mutex_);
  std::lock_guard<std::mutex> nodes_lock(nodes_mutex_);

  // Find appropriate policy
  auto policy_it = load_balancing_policies_.find(service_type);
  if (policy_it == load_balancing_policies_.end()) {
    policy_it = load_balancing_policies_.find("default");
  }

  if (policy_it == load_balancing_policies_.end()) {
    return "";
  }

  const LoadBalancingPolicy &policy = policy_it->second;

  // Get candidate nodes
  std::vector<NetworkNode> candidates;

  if (!client_region.empty()) {
    // Prefer nodes in client's region
    auto regional_nodes = get_nodes_by_region(client_region);
    candidates = std::move(regional_nodes);
  }

  if (candidates.empty()) {
    // Use all active nodes
    candidates = get_active_nodes();
  }

  if (candidates.empty()) {
    return "";
  }

  // Apply load balancing algorithm
  if (policy.algorithm == "round_robin") {
    // Simple round-robin based on hash
    std::hash<std::string> hasher;
    size_t index = hasher(service_type + client_region) % candidates.size();
    return candidates[index].node_id;
  } else if (policy.algorithm == "least_connections") {
    // Select node with lowest load
    auto min_it =
        std::min_element(candidates.begin(), candidates.end(),
                         [this](const NetworkNode &a, const NetworkNode &b) {
                           return calculate_node_load(a.node_id) <
                                  calculate_node_load(b.node_id);
                         });
    return min_it->node_id;
  } else if (policy.algorithm == "weighted") {
    // Weighted selection based on policy weights
    std::vector<std::pair<std::string, uint32_t>> weighted_candidates;
    for (const auto &node : candidates) {
      auto weight_it = policy.weights.find(node.node_id);
      uint32_t weight =
          weight_it != policy.weights.end() ? weight_it->second : 1;
      weighted_candidates.emplace_back(node.node_id, weight);
    }

    // Simple weighted random selection
    uint32_t total_weight = 0;
    for (const auto &wc : weighted_candidates) {
      total_weight += wc.second;
    }

    if (total_weight > 0) {
      // Use thread-local RNG for thread safety
      thread_local std::random_device rd;
      thread_local std::mt19937 gen(rd());
      std::uniform_int_distribution<uint32_t> dis(0, total_weight - 1);
      uint32_t random_value = dis(gen);
      uint32_t current_weight = 0;
      for (const auto &wc : weighted_candidates) {
        current_weight += wc.second;
        if (random_value < current_weight) {
          return wc.first;
        }
      }
    }

    return weighted_candidates[0].first;
  } else if (policy.algorithm == "geographic") {
    // Select closest node geographically
    if (!client_region.empty()) {
      auto regional_candidates = get_nodes_by_region(client_region);
      if (!regional_candidates.empty()) {
        return regional_candidates[0].node_id;
      }
    }

    // Fallback to least loaded
    auto min_it =
        std::min_element(candidates.begin(), candidates.end(),
                         [this](const NetworkNode &a, const NetworkNode &b) {
                           return calculate_node_load(a.node_id) <
                                  calculate_node_load(b.node_id);
                         });
    return min_it->node_id;
  }

  return candidates[0].node_id;
}

std::vector<std::string> NetworkTopologyManager::get_nodes_for_load_balancing(
    const std::string &policy_name, uint32_t count) const {
  std::lock_guard<std::mutex> policies_lock(policies_mutex_);
  std::lock_guard<std::mutex> nodes_lock(nodes_mutex_);

  std::vector<std::string> selected_nodes;

  auto policy_it = load_balancing_policies_.find(policy_name);
  if (policy_it == load_balancing_policies_.end()) {
    return selected_nodes;
  }

  const LoadBalancingPolicy &policy = policy_it->second;
  auto candidates = get_active_nodes();

  if (candidates.size() <= count) {
    for (const auto &node : candidates) {
      selected_nodes.push_back(node.node_id);
    }
    return selected_nodes;
  }

  // Sort by load and select top candidates
  std::sort(candidates.begin(), candidates.end(),
            [this](const NetworkNode &a, const NetworkNode &b) {
              return calculate_node_load(a.node_id) <
                     calculate_node_load(b.node_id);
            });

  for (uint32_t i = 0; i < count && i < candidates.size(); ++i) {
    selected_nodes.push_back(candidates[i].node_id);
  }

  return selected_nodes;
}

bool NetworkTopologyManager::register_service(const std::string &service_name,
                                              const std::string &node_id,
                                              uint16_t port) {
  std::lock_guard<std::mutex> lock(services_mutex_);

  service_registry_[service_name].emplace_back(node_id, port);

  notify_network_event("service_registered", node_id,
                       "service=" + service_name +
                           ",port=" + std::to_string(port));

  std::cout << "Registered service: " << service_name << " on node: " << node_id
            << " port: " << port << std::endl;

  return true;
}

bool NetworkTopologyManager::unregister_service(const std::string &service_name,
                                                const std::string &node_id) {
  std::lock_guard<std::mutex> lock(services_mutex_);

  auto service_it = service_registry_.find(service_name);
  if (service_it == service_registry_.end()) {
    return false;
  }

  auto &service_nodes = service_it->second;
  service_nodes.erase(std::remove_if(service_nodes.begin(), service_nodes.end(),
                                     [&node_id](const auto &pair) {
                                       return pair.first == node_id;
                                     }),
                      service_nodes.end());

  notify_network_event("service_unregistered", node_id,
                       "service=" + service_name);

  std::cout << "Unregistered service: " << service_name
            << " from node: " << node_id << std::endl;

  return true;
}

std::vector<std::pair<std::string, uint16_t>>
NetworkTopologyManager::discover_service(const std::string &service_name,
                                         const std::string &region) const {
  std::lock_guard<std::mutex> services_lock(services_mutex_);
  std::lock_guard<std::mutex> nodes_lock(nodes_mutex_);

  std::vector<std::pair<std::string, uint16_t>> service_endpoints;

  auto service_it = service_registry_.find(service_name);
  if (service_it == service_registry_.end()) {
    return service_endpoints;
  }

  for (const auto &endpoint : service_it->second) {
    const std::string &node_id = endpoint.first;

    // Check if node is active
    auto node_it = nodes_.find(node_id);
    if (node_it != nodes_.end() && node_it->second.is_active) {
      // Filter by region if specified
      if (region.empty() || node_it->second.region == region) {
        service_endpoints.push_back(endpoint);
      }
    }
  }

  return service_endpoints;
}

std::string
NetworkTopologyManager::route_request(const std::string &target_node,
                                      const std::string &source_node) const {
  std::lock_guard<std::mutex> nodes_lock(nodes_mutex_);

  auto target_it = nodes_.find(target_node);
  if (target_it == nodes_.end() || !target_it->second.is_active) {
    return "";
  }

  std::string source = source_node.empty() ? node_id_ : source_node;
  auto source_it = nodes_.find(source);
  if (source_it == nodes_.end()) {
    return "";
  }

  const NetworkNode &source_node_obj = source_it->second;
  const NetworkNode &target_node_obj = target_it->second;

  // If in same region, direct routing
  if (source_node_obj.region == target_node_obj.region) {
    return target_node_obj.address + ":" + std::to_string(target_node_obj.port);
  }

  // Cross-region routing - find path
  auto path = find_path_to_region(target_node_obj.region);
  if (path.empty()) {
    return "";
  }

  // Return first hop in the path (simplified)
  if (path.size() > 1) {
    // Find a node in the next region in the path
    auto next_region_nodes = get_nodes_by_region(path[1]);
    if (!next_region_nodes.empty()) {
      const auto &next_node = next_region_nodes[0];
      return next_node.address + ":" + std::to_string(next_node.port);
    }
  }

  return target_node_obj.address + ":" + std::to_string(target_node_obj.port);
}

bool NetworkTopologyManager::perform_health_check(const std::string &node_id) {
  std::lock_guard<std::mutex> nodes_lock(nodes_mutex_);
  std::lock_guard<std::mutex> metrics_lock(metrics_mutex_);

  auto node_it = nodes_.find(node_id);
  if (node_it == nodes_.end()) {
    return false;
  }

  // Record health check timestamp
  last_health_check_[node_id] = std::chrono::system_clock::now();

  // Simulate health check (in real implementation, this would be a network
  // call)
  bool is_healthy = node_it->second.is_active;

  if (!is_healthy) {
    handle_node_failure(node_id);
  }

  return is_healthy;
}

bool NetworkTopologyManager::measure_latency(const std::string &source_node,
                                             const std::string &target_node) {
  std::lock_guard<std::mutex> nodes_lock(nodes_mutex_);
  std::lock_guard<std::mutex> metrics_lock(metrics_mutex_);

  auto source_it = nodes_.find(source_node);
  auto target_it = nodes_.find(target_node);

  if (source_it == nodes_.end() || target_it == nodes_.end()) {
    return false;
  }

  // Simulate latency measurement with thread-local RNG
  thread_local std::random_device rd;
  thread_local std::mt19937 gen(rd());
  std::uniform_int_distribution<uint32_t> dis(10, 110);
  uint32_t latency_ms = dis(gen); // 10-110ms

  std::pair<std::string, std::string> key = {source_node, target_node};
  latency_cache_[key] = latency_ms;

  std::cout << "Measured latency: " << source_node << " -> " << target_node
            << " = " << latency_ms << "ms" << std::endl;

  return true;
}

NetworkMetrics NetworkTopologyManager::get_network_metrics() const {
  std::lock_guard<std::mutex> lock(metrics_mutex_);
  return current_metrics_;
}

std::vector<std::string> NetworkTopologyManager::get_unhealthy_nodes() const {
  std::lock_guard<std::mutex> lock(nodes_mutex_);

  std::vector<std::string> unhealthy_nodes;
  for (const auto &pair : nodes_) {
    if (!pair.second.is_active) {
      unhealthy_nodes.push_back(pair.first);
    }
  }

  return unhealthy_nodes;
}

bool NetworkTopologyManager::detect_network_partition() {
  std::lock_guard<std::mutex> nodes_lock(nodes_mutex_);
  std::lock_guard<std::mutex> partitions_lock(partitions_mutex_);

  // Check if any partition has lost majority of its nodes
  for (auto &partition_pair : partitions_) {
    auto &partition = partition_pair.second;

    uint32_t active_nodes = 0;
    for (const auto &node_id : partition.node_ids) {
      auto node_it = nodes_.find(node_id);
      if (node_it != nodes_.end() && node_it->second.is_active) {
        active_nodes++;
      }
    }

    uint32_t required_nodes = (partition.node_ids.size() / 2) + 1; // Majority

    if (active_nodes < required_nodes) {
      partition.is_healthy = false;
      notify_network_event("partition_unhealthy", partition.partition_id,
                           "active=" + std::to_string(active_nodes) + "/" +
                               std::to_string(partition.node_ids.size()));

      std::cout << "Detected network partition: " << partition.partition_id
                << " (active: " << active_nodes << "/"
                << partition.node_ids.size() << ")" << std::endl;

      return true;
    }
  }

  return false;
}

bool NetworkTopologyManager::initiate_partition_recovery() {
  std::cout << "Initiating network partition recovery" << std::endl;

  // Rebalance partitions and try to restore connectivity
  rebalance_partitions();

  return true;
}

bool NetworkTopologyManager::handle_node_failure(const std::string &node_id) {
  std::cout << "Handling node failure: " << node_id << std::endl;

  update_node_status(node_id, false);

  // Check if this affects any partition
  detect_network_partition();

  notify_network_event("node_failed", node_id);

  return true;
}

bool NetworkTopologyManager::handle_master_failure(
    const std::string &master_id) {
  std::cout << "Handling master failure: " << master_id << std::endl;

  std::lock_guard<std::mutex> lock(partitions_mutex_);

  // Find partitions that had this master
  for (auto &partition_pair : partitions_) {
    auto &partition = partition_pair.second;

    if (partition.primary_master == master_id) {
      partition.primary_master.clear();

      // Promote backup master if available
      if (!partition.backup_masters.empty()) {
        partition.primary_master = partition.backup_masters[0];
        partition.backup_masters.erase(partition.backup_masters.begin());

        notify_network_event("master_promoted", partition.partition_id,
                             "new_master=" + partition.primary_master);
      }
    }
  }

  return true;
}

bool NetworkTopologyManager::update_network_configuration(
    const std::unordered_map<std::string, std::string> &config) {
  for (const auto &pair : config) {
    std::cout << "Updated network config: " << pair.first << " = "
              << pair.second << std::endl;
  }

  return true;
}

std::unordered_map<std::string, std::string>
NetworkTopologyManager::get_network_configuration() const {
  std::unordered_map<std::string, std::string> config;

  std::lock_guard<std::mutex> nodes_lock(nodes_mutex_);
  std::lock_guard<std::mutex> partitions_lock(partitions_mutex_);

  config["total_nodes"] = std::to_string(nodes_.size());
  config["total_partitions"] = std::to_string(partitions_.size());
  config["total_regions"] = std::to_string(nodes_by_region_.size());

  return config;
}

bool NetworkTopologyManager::optimize_network_topology() {
  std::cout << "Optimizing network topology" << std::endl;

  // Rebalance partitions
  rebalance_partitions();

  // Optimize cross-region links
  optimize_cross_region_links();

  return true;
}

// Private methods
void NetworkTopologyManager::topology_monitor_loop() {
  while (running_.load()) {
    optimize_network_topology();
    update_network_metrics();

    std::this_thread::sleep_for(std::chrono::seconds(30));
  }
}

void NetworkTopologyManager::health_checker_loop() {
  while (running_.load()) {
    auto active_nodes = get_active_nodes();

    for (const auto &node : active_nodes) {
      perform_health_check(node.node_id);
    }

    // Reduced sleep time for better responsiveness
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
}

void NetworkTopologyManager::metrics_collector_loop() {
  while (running_.load()) {
    update_network_metrics();

    // Reduced sleep time for more frequent metric updates
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
}

void NetworkTopologyManager::partition_manager_loop() {
  while (running_.load()) {
    check_partition_health();
    detect_network_partition();

    // Reduced sleep time for faster partition management
    std::this_thread::sleep_for(std::chrono::seconds(10));
  }
}

void NetworkTopologyManager::update_network_metrics() {
  std::lock_guard<std::mutex> nodes_lock(nodes_mutex_);
  std::lock_guard<std::mutex> partitions_lock(partitions_mutex_);
  std::lock_guard<std::mutex> metrics_lock(metrics_mutex_);

  current_metrics_.total_nodes = nodes_.size();
  current_metrics_.active_nodes = 0;
  current_metrics_.partitions_count = partitions_.size();
  current_metrics_.healthy_partitions = 0;

  for (const auto &pair : nodes_) {
    if (pair.second.is_active) {
      current_metrics_.active_nodes++;
    }
  }

  for (const auto &pair : partitions_) {
    if (pair.second.is_healthy) {
      current_metrics_.healthy_partitions++;
    }
  }

  // Simulate other metrics
  current_metrics_.total_connections = current_metrics_.active_nodes * 10;
  current_metrics_.messages_per_second = current_metrics_.active_nodes * 100;
  current_metrics_.bytes_per_second =
      current_metrics_.messages_per_second * 1024;
  current_metrics_.average_latency_ms = 25.5;
  current_metrics_.packet_loss_rate = 0.001;
}

void NetworkTopologyManager::check_partition_health() {
  std::lock_guard<std::mutex> partitions_lock(partitions_mutex_);
  std::lock_guard<std::mutex> nodes_lock(nodes_mutex_);

  for (auto &partition_pair : partitions_) {
    auto &partition = partition_pair.second;

    uint32_t active_nodes = 0;
    for (const auto &node_id : partition.node_ids) {
      auto node_it = nodes_.find(node_id);
      if (node_it != nodes_.end() && node_it->second.is_active) {
        active_nodes++;
      }
    }

    bool was_healthy = partition.is_healthy;
    partition.is_healthy =
        (active_nodes >= (partition.node_ids.size() / 2) + 1);

    if (was_healthy && !partition.is_healthy) {
      notify_network_event("partition_became_unhealthy",
                           partition.partition_id);
    } else if (!was_healthy && partition.is_healthy) {
      notify_network_event("partition_became_healthy", partition.partition_id);
    }
  }
}

void NetworkTopologyManager::optimize_cross_region_links() {
  std::lock_guard<std::mutex> lock(links_mutex_);

  for (auto &link_pair : cross_region_links_) {
    auto &link = link_pair.second;

    // Recalculate link reliability
    link.reliability_score = calculate_link_reliability(link);

    // Disable unreliable links
    if (link.reliability_score < 0.8) {
      link.is_active = false;
      notify_network_event("cross_region_link_degraded", link_pair.first,
                           "reliability=" +
                               std::to_string(link.reliability_score));
    }
  }
}

void NetworkTopologyManager::rebalance_partitions() {
  std::lock_guard<std::mutex> partitions_lock(partitions_mutex_);
  std::lock_guard<std::mutex> nodes_lock(nodes_mutex_);

  auto active_nodes = get_active_nodes();

  if (active_nodes.empty()) {
    return;
  }

  // Simple rebalancing - ensure each partition has similar number of nodes
  uint32_t target_partition_size = std::max(
      1u, static_cast<uint32_t>(
              active_nodes.size() /
              std::max(1u, static_cast<uint32_t>(partitions_.size()))));

  for (auto &partition_pair : partitions_) {
    auto &partition = partition_pair.second;

    // Remove inactive nodes from partition
    partition.node_ids.erase(
        std::remove_if(partition.node_ids.begin(), partition.node_ids.end(),
                       [this](const std::string &node_id) {
                         auto it = nodes_.find(node_id);
                         return it == nodes_.end() || !it->second.is_active;
                       }),
        partition.node_ids.end());

    partition.partition_size = partition.node_ids.size();

    std::cout << "Rebalanced partition " << partition.partition_id << " to "
              << partition.partition_size << " nodes" << std::endl;
  }
}

bool NetworkTopologyManager::validate_network_node(
    const NetworkNode &node) const {
  return !node.node_id.empty() && !node.address.empty() && node.port > 0;
}

std::string NetworkTopologyManager::generate_partition_id() const {
  static uint32_t partition_counter = 0;
  return "partition_" + std::to_string(++partition_counter);
}

uint32_t
NetworkTopologyManager::calculate_node_load(const std::string &node_id) const {
  // Simulate node load calculation
  std::hash<std::string> hasher;
  return hasher(node_id) % 100; // 0-99% load
}

double NetworkTopologyManager::calculate_link_reliability(
    const CrossRegionLink &link) const {
  // Calculate reliability based on latency and bandwidth
  double latency_factor =
      std::max(0.0, 1.0 - (link.latency_ms / 1000.0)); // Penalize high latency
  double bandwidth_factor =
      std::min(1.0, link.bandwidth_mbps / 1000.0); // Reward high bandwidth

  return (latency_factor + bandwidth_factor) / 2.0;
}

void NetworkTopologyManager::notify_network_event(const std::string &event,
                                                  const std::string &node_id,
                                                  const std::string &details) {
  if (network_event_callback_) {
    network_event_callback_(event, node_id, details);
  }
}

// Utility functions
std::string network_partition_status_to_string(bool is_healthy) {
  return is_healthy ? "healthy" : "unhealthy";
}

std::string load_balancing_algorithm_to_string(const std::string &algorithm) {
  return algorithm;
}

} // namespace network
} // namespace slonana