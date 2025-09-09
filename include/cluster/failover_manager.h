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
namespace cluster {

using namespace slonana::common;

enum class FailoverTrigger {
  NODE_UNRESPONSIVE,
  NETWORK_PARTITION,
  HEALTH_CHECK_FAILED,
  MANUAL_FAILOVER,
  LOAD_THRESHOLD_EXCEEDED
};

enum class FailoverState {
  NORMAL,
  DETECTING_FAILURE,
  ELECTING_REPLACEMENT,
  SWITCHING_TRAFFIC,
  RECOVERY_IN_PROGRESS,
  FAILED_OVER,
  EMERGENCY
};

struct NodeHealth {
  std::string node_id;
  bool is_responsive;
  uint64_t last_heartbeat;
  double cpu_usage;
  double memory_usage;
  double disk_usage;
  double network_latency_ms;
  uint64_t error_count;
  bool is_leader;
  bool is_available;
};

struct FailoverEvent {
  std::string event_id;
  FailoverTrigger trigger;
  std::string failed_node_id;
  std::string replacement_node_id;
  uint64_t timestamp;
  FailoverState state;
  std::string reason;
  uint64_t duration_ms;
};

struct FailoverConfig {
  int health_check_interval_ms = 5000;
  int failure_detection_timeout_ms = 15000;
  int max_consecutive_failures = 3;
  double cpu_threshold = 90.0;
  double memory_threshold = 90.0;
  double network_latency_threshold_ms = 1000.0;
  bool enable_automatic_failover = true;
  bool enable_load_based_failover = false;
  int failover_cooldown_ms = 30000;
};

struct FailoverStats {
  uint64_t total_failovers;
  uint64_t successful_failovers;
  uint64_t failed_failovers;
  uint64_t avg_failover_time_ms;
  uint64_t active_nodes;
  uint64_t failed_nodes;
  std::string current_leader;
  FailoverState current_state;
};

// Interface for failover actions
class IFailoverActionHandler {
public:
  virtual ~IFailoverActionHandler() = default;
  virtual bool promote_node_to_leader(const std::string &node_id) = 0;
  virtual bool demote_node_from_leader(const std::string &node_id) = 0;
  virtual bool redirect_traffic(const std::string &from_node,
                                const std::string &to_node) = 0;
  virtual bool isolate_failed_node(const std::string &node_id) = 0;
  virtual bool restore_node_to_cluster(const std::string &node_id) = 0;
  virtual NodeHealth get_node_health(const std::string &node_id) = 0;
  virtual bool handle_failover(const std::string &failed_node,
                               const std::string &replacement_node) = 0;
};

// Automatic failover and recovery manager
class FailoverManager {
private:
  std::string node_id_;
  ValidatorConfig config_;
  FailoverConfig failover_config_;

  // Node tracking
  std::unordered_map<std::string, NodeHealth> node_health_;
  std::unordered_map<std::string, int> failure_counts_;
  std::string current_leader_;
  mutable std::mutex nodes_mutex_;

  // Failover state
  FailoverState current_state_;
  std::vector<FailoverEvent> failover_history_;
  mutable std::mutex state_mutex_;

  // Action handler
  std::shared_ptr<IFailoverActionHandler> action_handler_;

  // Threading
  std::atomic<bool> running_;
  std::thread monitoring_thread_;
  std::thread failover_thread_;
  std::thread recovery_thread_;

  // Statistics
  FailoverStats stats_;
  mutable std::mutex stats_mutex_;

  // Callbacks
  std::function<void(const FailoverEvent &)> failover_callback_;
  std::function<void(const std::string &, NodeHealth)> health_change_callback_;

  // Last failover time for cooldown
  std::chrono::steady_clock::time_point last_failover_time_;

  // Private methods
  void monitoring_loop();
  void failover_loop();
  void recovery_loop();
  bool check_node_health(const std::string &node_id);
  bool is_node_failed(const std::string &node_id);
  bool should_trigger_failover(const std::string &node_id,
                               FailoverTrigger &trigger);
  bool execute_failover(const std::string &failed_node,
                        FailoverTrigger trigger);
  std::string select_replacement_node(const std::string &failed_node);
  bool is_failover_cooldown_active();
  void record_failover_event(const FailoverEvent &event);
  void cleanup_old_events();
  bool validate_cluster_state();
  bool handle_node_failure(const std::string &node_id);
  bool handle_network_partition(const std::string &node_id);
  void update_cluster_topology_after_partition(
      const std::vector<std::string> &isolated_nodes);

public:
  FailoverManager(const std::string &node_id, const ValidatorConfig &config);
  ~FailoverManager();

  // Lifecycle management
  bool start();
  void stop();
  bool is_running() const { return running_.load(); }

  // Configuration
  void set_config(const FailoverConfig &config) { failover_config_ = config; }
  FailoverConfig get_config() const { return failover_config_; }
  void set_action_handler(std::shared_ptr<IFailoverActionHandler> handler) {
    action_handler_ = handler;
  }

  // Node management
  void register_node(const std::string &node_id);
  void unregister_node(const std::string &node_id);
  void update_node_health(const std::string &node_id, const NodeHealth &health);
  std::vector<NodeHealth> get_all_node_health() const;
  NodeHealth get_node_health(const std::string &node_id) const;

  // Leader management
  void set_current_leader(const std::string &leader_id);
  std::string get_current_leader() const;
  bool is_leader_healthy() const;

  // Failover operations
  bool trigger_manual_failover(const std::string &node_id,
                               const std::string &reason = "");
  bool force_leader_election();
  void enable_automatic_failover(bool enable);
  bool is_automatic_failover_enabled() const;

  // Recovery operations
  bool attempt_node_recovery(const std::string &node_id);
  void reset_failure_count(const std::string &node_id);
  bool validate_node_recovery(const std::string &node_id);

  // Status and monitoring
  FailoverStats get_stats() const;
  FailoverState get_current_state() const;
  std::vector<FailoverEvent> get_failover_history() const;
  std::vector<std::string> get_failed_nodes() const;
  std::vector<std::string> get_healthy_nodes() const;

  // Callbacks
  void
  set_failover_callback(std::function<void(const FailoverEvent &)> callback);
  void set_health_change_callback(
      std::function<void(const std::string &, NodeHealth)> callback);

  // Testing and diagnostics
  void trigger_node_failure_test(const std::string &node_id);
  void trigger_network_partition_test(
      const std::vector<std::string> &isolated_nodes);
  bool run_failover_test();
};

// Utility functions
namespace failover_utils {
std::string failover_trigger_to_string(FailoverTrigger trigger);
FailoverTrigger string_to_failover_trigger(const std::string &trigger);
std::string failover_state_to_string(FailoverState state);
FailoverState string_to_failover_state(const std::string &state);
std::string generate_event_id();
bool is_node_health_critical(const NodeHealth &health,
                             const FailoverConfig &config);
double calculate_node_score(const NodeHealth &health);
std::vector<uint8_t> serialize_failover_event(const FailoverEvent &event);
FailoverEvent deserialize_failover_event(const std::vector<uint8_t> &data);
} // namespace failover_utils

} // namespace cluster
} // namespace slonana