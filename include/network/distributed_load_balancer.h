#pragma once

#include "cluster/multi_master_manager.h"
#include "common/types.h"
#include "network/topology_manager.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <shared_mutex>

// Lock-free data structures (boost::lockfree)
#ifdef __has_include
#if __has_include(<boost/lockfree/queue.hpp>)
#include <boost/lockfree/queue.hpp>
#define HAS_LOCKFREE_QUEUE 1
#endif
#endif

#ifndef HAS_LOCKFREE_QUEUE
#define HAS_LOCKFREE_QUEUE 0
#endif

namespace slonana {
namespace network {

using namespace slonana::cluster;

// Load balancer types and strategies
enum class LoadBalancingStrategy {
  ROUND_ROBIN,
  LEAST_CONNECTIONS,
  LEAST_RESPONSE_TIME,
  WEIGHTED_ROUND_ROBIN,
  IP_HASH,
  GEOGRAPHIC,
  RESOURCE_BASED,
  ADAPTIVE
};

struct BackendServer {
  std::string server_id;
  std::string address;
  uint16_t port;
  std::string region;
  uint32_t weight;
  uint32_t current_connections;
  uint32_t max_connections;
  std::chrono::milliseconds average_response_time;
  double health_score;
  bool is_active;
  bool is_draining;
  std::chrono::system_clock::time_point last_health_check;
};

struct LoadBalancingRule {
  std::string rule_name;
  std::string service_pattern; // Service name pattern to match
  LoadBalancingStrategy strategy;
  std::vector<std::string> backend_servers;
  std::unordered_map<std::string, uint32_t> server_weights;
  uint32_t health_check_interval_ms;
  uint32_t max_retries;
  bool enable_session_affinity;
  std::string affinity_cookie_name;
};

struct ConnectionRequest {
  std::string request_id;
  std::string client_ip;
  std::string service_name;
  std::string target_region;
  std::unordered_map<std::string, std::string> headers;
  std::chrono::system_clock::time_point timestamp;
  uint32_t retry_count;
};

struct ConnectionResponse {
  std::string request_id;
  std::string selected_server;
  std::string server_address;
  uint16_t server_port;
  bool success;
  std::string error_message;
  std::chrono::milliseconds response_time;
};

struct LoadBalancerStats {
  uint64_t total_requests;
  uint64_t successful_requests;
  uint64_t failed_requests;
  uint64_t retried_requests;
  uint32_t active_backends;
  uint32_t total_backends;
  double average_response_time_ms;
  double requests_per_second;
  std::unordered_map<std::string, uint64_t> requests_by_backend;
  std::unordered_map<std::string, uint64_t> requests_by_region;
};

class DistributedLoadBalancer {
public:
  DistributedLoadBalancer(const std::string &balancer_id,
                          const ValidatorConfig &config,
                          size_t queue_capacity = 1024);
  ~DistributedLoadBalancer();

  // Core management
  bool start();
  void stop();
  bool is_running() const { return running_.load(); }

  // Lock-free queue configuration (when HAS_LOCKFREE_QUEUE is enabled)
  size_t get_queue_capacity() const { return queue_capacity_; }
  void set_queue_capacity(size_t capacity); // Only effective before start()

  // Backend server management
  bool register_backend_server(const BackendServer &server);
  bool unregister_backend_server(const std::string &server_id);
  bool update_server_status(const std::string &server_id, bool is_active);
  bool update_server_load(const std::string &server_id, uint32_t connections,
                          std::chrono::milliseconds response_time);
  std::vector<BackendServer>
  get_backend_servers(const std::string &service_name = "") const;

  // Load balancing rules
  bool add_load_balancing_rule(const LoadBalancingRule &rule);
  bool remove_load_balancing_rule(const std::string &rule_name);
  bool update_load_balancing_rule(const LoadBalancingRule &rule);
  std::vector<LoadBalancingRule> get_load_balancing_rules() const;

  // Request routing
  ConnectionResponse route_request(const ConnectionRequest &request);
  std::vector<std::string>
  get_available_servers(const std::string &service_name,
                        const std::string &region = "") const;
  bool validate_server_capacity(const std::string &server_id) const;

  // Health monitoring
  bool perform_health_check(const std::string &server_id);
  bool set_server_draining(const std::string &server_id, bool draining);
  std::vector<std::string> get_unhealthy_servers() const;
  bool trigger_failover(const std::string &failed_server);

  // Session affinity
  bool create_session_affinity(const std::string &session_id,
                               const std::string &server_id);
  bool remove_session_affinity(const std::string &session_id);
  std::string get_affinity_server(const std::string &session_id) const;

  // Circuit breaker
  bool enable_circuit_breaker(const std::string &server_id);
  bool disable_circuit_breaker(const std::string &server_id);
  bool is_circuit_breaker_open(const std::string &server_id) const;

  // Statistics and monitoring
  LoadBalancerStats get_statistics() const;
  std::unordered_map<std::string, double> get_server_health_scores() const;
  std::vector<std::pair<std::string, uint32_t>> get_server_loads() const;

  // Lock-free queue monitoring (when HAS_LOCKFREE_QUEUE is enabled)
  // These methods provide visibility into queue health and backpressure scenarios
  struct QueueMetrics {
    size_t allocated_count;      // Current number of allocated items in queue
    size_t capacity;              // Maximum queue capacity
    size_t push_failure_count;    // Number of failed push attempts (queue full)
    double utilization_percent;   // Queue utilization percentage
  };
  QueueMetrics get_queue_metrics() const;

  // Configuration
  bool update_configuration(
      const std::unordered_map<std::string, std::string> &config);
  std::unordered_map<std::string, std::string> get_configuration() const;

  // Integration with multi-master and topology managers
  void set_multi_master_manager(std::shared_ptr<MultiMasterManager> manager) {
    multi_master_manager_ = manager;
  }
  void set_topology_manager(std::shared_ptr<NetworkTopologyManager> manager) {
    topology_manager_ = manager;
  }

  // Event callbacks
  using LoadBalancerEventCallback =
      std::function<void(const std::string &event, const std::string &server_id,
                         const std::string &details)>;
  void set_event_callback(LoadBalancerEventCallback callback) {
    event_callback_ = callback;
  }

private:
  std::string balancer_id_;
  ValidatorConfig config_;
  std::atomic<bool> running_;

  // Backend servers registry
  mutable std::mutex servers_mutex_;
  std::unordered_map<std::string, BackendServer> backend_servers_;
  std::unordered_map<std::string, std::vector<std::string>> servers_by_service_;

  // Load balancing rules
  mutable std::mutex rules_mutex_;
  std::unordered_map<std::string, LoadBalancingRule> load_balancing_rules_;

  // Session affinity
  mutable std::mutex affinity_mutex_;
  std::unordered_map<std::string, std::string> session_affinity_;
  std::unordered_map<std::string, std::chrono::system_clock::time_point>
      affinity_timestamps_;

  // Circuit breakers
  mutable std::mutex circuit_breaker_mutex_;
  std::unordered_map<std::string, bool> circuit_breakers_;
  std::unordered_map<std::string, uint32_t> failure_counts_;
  std::unordered_map<std::string, std::chrono::system_clock::time_point>
      circuit_breaker_timestamps_;

  // Round robin counters - using atomic with shared_mutex for concurrent reads
  mutable std::shared_mutex round_robin_mutex_;
  std::unordered_map<std::string, std::atomic<uint32_t>> round_robin_counters_;

  // Statistics - using atomics to reduce lock contention
  mutable std::mutex stats_mutex_;
  LoadBalancerStats current_stats_;
  std::atomic<uint64_t> atomic_total_requests_{0};
  std::atomic<uint64_t> atomic_failed_requests_{0};

  // Request queue for async processing - lock-free when available
  // OWNERSHIP MODEL (lock-free queue):
  // - Producer: Allocates ConnectionRequest* on heap, pushes to queue, increments queue_allocated_count_
  // - Consumer: Pops from queue, wraps in unique_ptr (RAII), decrements queue_allocated_count_
  // - Shutdown: stop() drains remaining items and verifies no leaks via queue_allocated_count_
  // - Memory Order: relaxed for counter (protected by happens-before relationship of queue operations)
  // - Leak Safety: If push fails, producer must delete and not increment counter
  // 
  // PUSH FAILURE HANDLING PROTOCOL:
  // - When push() returns false (queue full), caller MUST:
  //   1. Immediately delete the allocated ConnectionRequest*
  //   2. NOT increment queue_allocated_count_
  //   3. Implement backpressure or drop policy
  // - Example:
  //     ConnectionRequest* req = new ConnectionRequest(...);
  //     if (!lock_free_request_queue_->push(req)) {
  //       delete req;  // Mandatory cleanup
  //       queue_push_failure_count_.fetch_add(1, std::memory_order_relaxed);
  //       // Handle backpressure (return error, drop, retry later, etc.)
  //     } else {
  //       queue_allocated_count_.fetch_add(1, std::memory_order_relaxed);
  //     }
#if HAS_LOCKFREE_QUEUE
  std::unique_ptr<boost::lockfree::queue<ConnectionRequest*>> lock_free_request_queue_;
  std::atomic<size_t> queue_allocated_count_{0};  // Track allocations for leak detection
  std::atomic<size_t> queue_push_failure_count_{0};  // Track push failures for monitoring
#endif
  size_t queue_capacity_;  // Configurable queue capacity (used by lock-free queue)
  // Fallback mutex-protected queue with condition variable
  mutable std::mutex request_queue_mutex_;
  std::queue<ConnectionRequest> request_queue_;
  std::condition_variable request_queue_cv_;  // Event-driven wakeup

  // Background threads
  std::thread health_monitor_thread_;
  std::thread stats_collector_thread_;
  std::thread circuit_breaker_thread_;
  std::thread request_processor_thread_;

  // Integration managers
  std::shared_ptr<MultiMasterManager> multi_master_manager_;
  std::shared_ptr<NetworkTopologyManager> topology_manager_;

  // Event callback
  LoadBalancerEventCallback event_callback_;

  // Private methods
  void health_monitor_loop();
  void stats_collector_loop();
  void circuit_breaker_loop();
  void request_processor_loop();

  std::string select_server_round_robin(const std::vector<std::string> &servers,
                                        const std::string &service_name);
  std::string
  select_server_least_connections(const std::vector<std::string> &servers);
  std::string
  select_server_least_response_time(const std::vector<std::string> &servers);
  std::string select_server_weighted(const std::vector<std::string> &servers,
                                     const LoadBalancingRule &rule);
  std::string select_server_ip_hash(const std::vector<std::string> &servers,
                                    const std::string &client_ip);
  std::string select_server_geographic(const std::vector<std::string> &servers,
                                       const std::string &target_region);
  std::string
  select_server_resource_based(const std::vector<std::string> &servers);
  std::string select_server_adaptive(const std::vector<std::string> &servers,
                                     const ConnectionRequest &request);

  bool validate_backend_server(const BackendServer &server) const;
  LoadBalancingRule *find_matching_rule(const std::string &service_name);
  std::vector<std::string>
  filter_healthy_servers(const std::vector<std::string> &servers) const;
  double calculate_server_health_score(const BackendServer &server) const;

  void update_request_statistics(const ConnectionRequest &request,
                                 const ConnectionResponse &response);
  void cleanup_expired_affinities();
  void reset_circuit_breaker(const std::string &server_id);

  void notify_event(const std::string &event, const std::string &server_id,
                    const std::string &details = "");
};

// Utility functions
const char *load_balancing_strategy_to_string(LoadBalancingStrategy strategy);
LoadBalancingStrategy
string_to_load_balancing_strategy(const std::string &strategy_str);
std::string generate_request_id();

} // namespace network
} // namespace slonana