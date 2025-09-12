#pragma once

#include "cluster/multi_master_manager.h"
#include "common/types.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace slonana {
namespace network {

using namespace slonana::cluster;

// Network topology structures
struct NetworkNode {
  std::string node_id;
  std::string address;
  uint16_t port;
  std::string region;
  std::string datacenter;
  std::vector<std::string> capabilities;
  uint32_t bandwidth_mbps;
  uint32_t latency_ms;
  bool is_active;
  std::chrono::system_clock::time_point last_seen;
};

struct NetworkPartition {
  std::string partition_id;
  std::vector<std::string> node_ids;
  std::string primary_master;
  std::vector<std::string> backup_masters;
  uint32_t partition_size;
  bool is_healthy;
};

struct CrossRegionLink {
  std::string source_region;
  std::string target_region;
  std::vector<std::string> bridge_nodes;
  uint32_t bandwidth_mbps;
  uint32_t latency_ms;
  double reliability_score;
  bool is_active;
};

struct LoadBalancingPolicy {
  std::string policy_name;
  std::string
      algorithm; // "round_robin", "least_connections", "weighted", "geographic"
  std::unordered_map<std::string, uint32_t> weights;
  uint32_t max_connections_per_node;
  uint32_t health_check_interval_ms;
  bool enable_sticky_sessions;
};

struct NetworkMetrics {
  uint64_t total_nodes;
  uint64_t active_nodes;
  uint64_t total_connections;
  uint64_t messages_per_second;
  uint64_t bytes_per_second;
  double average_latency_ms;
  double packet_loss_rate;
  uint32_t partitions_count;
  uint32_t healthy_partitions;
};

class NetworkTopologyManager {
public:
  NetworkTopologyManager(const std::string &node_id,
                         const ValidatorConfig &config);
  ~NetworkTopologyManager();

  // Core management
  bool start();
  void stop();
  bool is_running() const { return running_.load(); }

  // Node management
  bool register_node(const NetworkNode &node);
  bool unregister_node(const std::string &node_id);
  bool update_node_status(const std::string &node_id, bool is_active);
  std::vector<NetworkNode> get_active_nodes() const;
  std::vector<NetworkNode> get_nodes_by_region(const std::string &region) const;
  std::vector<NetworkNode>
  get_nodes_by_capability(const std::string &capability) const;

  // Network partitioning
  bool create_partition(const std::string &partition_id,
                        const std::vector<std::string> &node_ids);
  bool delete_partition(const std::string &partition_id);
  bool assign_master_to_partition(const std::string &partition_id,
                                  const std::string &master_id);
  std::vector<NetworkPartition> get_partitions() const;
  std::string get_partition_for_node(const std::string &node_id) const;

  // Cross-region networking
  bool establish_cross_region_link(const CrossRegionLink &link);
  bool remove_cross_region_link(const std::string &source_region,
                                const std::string &target_region);
  std::vector<CrossRegionLink> get_cross_region_links() const;
  std::vector<std::string>
  find_path_to_region(const std::string &target_region) const;

  // Load balancing
  bool add_load_balancing_policy(const LoadBalancingPolicy &policy);
  bool remove_load_balancing_policy(const std::string &policy_name);
  std::string
  select_node_for_service(const std::string &service_type,
                          const std::string &client_region = "") const;
  std::vector<std::string>
  get_nodes_for_load_balancing(const std::string &policy_name,
                               uint32_t count) const;

  // Service discovery and routing
  bool register_service(const std::string &service_name,
                        const std::string &node_id, uint16_t port);
  bool unregister_service(const std::string &service_name,
                          const std::string &node_id);
  std::vector<std::pair<std::string, uint16_t>>
  discover_service(const std::string &service_name,
                   const std::string &region = "") const;
  std::string route_request(const std::string &target_node,
                            const std::string &source_node = "") const;

  // Network health and monitoring
  bool perform_health_check(const std::string &node_id);
  bool measure_latency(const std::string &source_node,
                       const std::string &target_node);
  NetworkMetrics get_network_metrics() const;
  std::vector<std::string> get_unhealthy_nodes() const;

  // Failure detection and recovery
  bool detect_network_partition();
  bool initiate_partition_recovery();
  bool handle_node_failure(const std::string &node_id);
  bool handle_master_failure(const std::string &master_id);

  // Configuration and management
  bool update_network_configuration(
      const std::unordered_map<std::string, std::string> &config);
  std::unordered_map<std::string, std::string>
  get_network_configuration() const;
  bool optimize_network_topology();

  // Event callbacks
  using NetworkEventCallback =
      std::function<void(const std::string &event, const std::string &node_id,
                         const std::string &details)>;
  void set_network_event_callback(NetworkEventCallback callback) {
    network_event_callback_ = callback;
  }

private:
  std::string node_id_;
  ValidatorConfig config_;
  std::atomic<bool> running_;

  // Network registry
  mutable std::mutex nodes_mutex_;
  std::unordered_map<std::string, NetworkNode> nodes_;
  std::unordered_map<std::string, std::unordered_set<std::string>>
      nodes_by_region_;
  std::unordered_map<std::string, std::unordered_set<std::string>>
      nodes_by_capability_;

  // Partitioning
  mutable std::mutex partitions_mutex_;
  std::unordered_map<std::string, NetworkPartition> partitions_;
  std::unordered_map<std::string, std::string> node_to_partition_;

  // Cross-region links
  mutable std::mutex links_mutex_;
  std::unordered_map<std::string, CrossRegionLink> cross_region_links_;

  // Load balancing
  mutable std::mutex policies_mutex_;
  std::unordered_map<std::string, LoadBalancingPolicy> load_balancing_policies_;

  // Service registry
  mutable std::mutex services_mutex_;
  std::unordered_map<std::string, std::vector<std::pair<std::string, uint16_t>>>
      service_registry_;

  // Network metrics and monitoring
  mutable std::mutex metrics_mutex_;
  NetworkMetrics current_metrics_;
  std::unordered_map<std::string, std::chrono::system_clock::time_point>
      last_health_check_;
  // Hash function for pair keys
  struct PairHash {
    std::size_t operator()(const std::pair<std::string, std::string> &p) const {
      return std::hash<std::string>{}(p.first) ^
             (std::hash<std::string>{}(p.second) << 1);
    }
  };

  std::unordered_map<std::pair<std::string, std::string>, uint32_t, PairHash>
      latency_cache_;

  // Background threads
  std::thread topology_monitor_thread_;
  std::thread health_checker_thread_;
  std::thread metrics_collector_thread_;
  std::thread partition_manager_thread_;

  // Event callback
  NetworkEventCallback network_event_callback_;

  // Private methods
  void topology_monitor_loop();
  void health_checker_loop();
  void metrics_collector_loop();
  void partition_manager_loop();

  void update_network_metrics();
  void check_partition_health();
  void optimize_cross_region_links();
  void rebalance_partitions();

  bool validate_network_node(const NetworkNode &node) const;
  std::string generate_partition_id() const;
  uint32_t calculate_node_load(const std::string &node_id) const;
  double calculate_link_reliability(const CrossRegionLink &link) const;

  void notify_network_event(const std::string &event,
                            const std::string &node_id,
                            const std::string &details = "");
};

// Utility functions
std::string network_partition_status_to_string(bool is_healthy);
std::string load_balancing_algorithm_to_string(const std::string &algorithm);

} // namespace network
} // namespace slonana