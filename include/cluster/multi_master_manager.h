#pragma once

#include "cluster/consensus_manager.h"
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
namespace cluster {

// Multi-master node roles
enum class MasterRole {
  NONE,          // Not a master
  RPC_MASTER,    // Handles RPC requests
  LEDGER_MASTER, // Manages ledger operations
  GOSSIP_MASTER, // Coordinates gossip protocol
  SHARD_MASTER,  // Manages specific shard
  GLOBAL_MASTER  // Coordinates across all masters
};

struct MasterNode {
  std::string node_id;
  std::string address;
  uint16_t port;
  MasterRole role;
  uint32_t shard_id; // For shard masters
  std::string region;
  std::chrono::system_clock::time_point last_heartbeat;
  uint64_t load_score;
  bool is_healthy;

  MasterNode()
      : role(MasterRole::NONE), shard_id(0), load_score(0), is_healthy(true) {}
};

struct ServiceAffinityRule {
  std::string service_type;
  std::vector<std::string> preferred_masters;
  uint32_t min_masters;
  uint32_t max_masters;
  std::string
      load_balancing_strategy; // "round_robin", "least_loaded", "regional"
};

struct CrossMasterMessage {
  std::string source_master;
  std::string target_master;
  std::string message_type;
  std::vector<uint8_t> payload;
  uint64_t sequence_number;
  std::chrono::system_clock::time_point timestamp;
};

class MultiMasterManager {
public:
  MultiMasterManager(const std::string &node_id, const ValidatorConfig &config);
  ~MultiMasterManager();

  // Core management
  bool start();
  void stop();
  bool is_running() const { return running_.load(); }

  // Master node management
  bool register_master(const MasterNode &master);
  bool unregister_master(const std::string &master_id);
  std::vector<MasterNode> get_active_masters() const;
  std::vector<MasterNode> get_masters_by_role(MasterRole role) const;

  // Service affinity and routing
  bool add_service_affinity(const ServiceAffinityRule &rule);
  std::string select_master_for_service(const std::string &service_type) const;
  std::vector<std::string>
  get_masters_for_service(const std::string &service_type) const;

  // Load balancing
  bool update_master_load(const std::string &master_id, uint64_t load_score);
  std::string
  select_least_loaded_master(MasterRole role = MasterRole::NONE) const;
  std::string select_master_by_region(const std::string &region,
                                      MasterRole role = MasterRole::NONE) const;

  // Master promotion and election
  bool promote_to_master(MasterRole role, uint32_t shard_id = 0);
  bool demote_from_master();
  bool initiate_master_election(MasterRole role);
  bool vote_for_master(const std::string &candidate_id, MasterRole role);

  // Cross-master communication
  bool send_cross_master_message(const CrossMasterMessage &message);
  std::vector<CrossMasterMessage> receive_cross_master_messages();
  bool broadcast_to_masters(const std::string &message_type,
                            const std::vector<uint8_t> &payload);

  // Health monitoring
  bool is_master_healthy(const std::string &master_id) const;
  void mark_master_unhealthy(const std::string &master_id);
  void mark_master_healthy(const std::string &master_id);

  // Network topology
  bool add_master_to_region(const std::string &master_id,
                            const std::string &region);
  std::vector<std::string>
  get_regional_masters(const std::string &region) const;
  bool setup_cross_region_replication(const std::string &source_region,
                                      const std::string &target_region);

  // Configuration
  bool update_configuration(
      const std::unordered_map<std::string, std::string> &config);
  std::unordered_map<std::string, std::string> get_configuration() const;

  // Statistics and monitoring
  struct MultiMasterStats {
    uint32_t total_masters;
    uint32_t healthy_masters;
    uint32_t masters_by_role[6]; // Count for each MasterRole
    uint64_t total_cross_master_messages;
    uint64_t average_load_score;
    std::chrono::milliseconds average_response_time;
  };

  MultiMasterStats get_statistics() const;

  // Event callbacks
  using MasterEventCallback =
      std::function<void(const std::string &event, const MasterNode &master)>;
  void set_master_event_callback(MasterEventCallback callback) {
    master_event_callback_ = callback;
  }

private:
  std::string node_id_;
  ValidatorConfig config_;
  std::atomic<bool> running_;

  // Master registry
  mutable std::mutex masters_mutex_;
  std::unordered_map<std::string, MasterNode> masters_;
  std::unordered_map<MasterRole, std::unordered_set<std::string>>
      masters_by_role_;
  std::unordered_map<std::string, std::unordered_set<std::string>>
      masters_by_region_;

  // Service affinity
  mutable std::mutex affinity_mutex_;
  std::unordered_map<std::string, ServiceAffinityRule> service_affinity_rules_;

  // Cross-master messaging
  mutable std::mutex messaging_mutex_;
  std::vector<CrossMasterMessage> incoming_messages_;
  std::vector<CrossMasterMessage> outgoing_messages_;
  uint64_t message_sequence_;

  // Master roles for this node
  std::unordered_set<MasterRole> my_roles_;

  // Background threads
  std::thread master_monitor_thread_;
  std::thread load_balancer_thread_;
  std::thread message_processor_thread_;
  std::thread health_checker_thread_;

  // Event callback
  MasterEventCallback master_event_callback_;

  // Private methods
  void master_monitor_loop();
  void load_balancer_loop();
  void message_processor_loop();
  void health_checker_loop();

  void process_master_heartbeat(const MasterNode &master);
  void handle_master_failure(const std::string &master_id);
  void rebalance_services();
  void update_topology();

  bool validate_master_node(const MasterNode &master) const;
  std::string generate_master_id(const MasterNode &master) const;
  uint64_t calculate_master_priority(const MasterNode &master,
                                     MasterRole role) const;

  void notify_master_event(const std::string &event, const MasterNode &master);
};

// Utility functions
const char *master_role_to_string(MasterRole role);
MasterRole string_to_master_role(const std::string &role_str);

} // namespace cluster
} // namespace slonana