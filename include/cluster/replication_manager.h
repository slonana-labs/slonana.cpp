#pragma once

#include "common/types.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace slonana {
namespace cluster {

using namespace slonana::common;

struct ReplicationTarget {
  std::string node_id;
  std::string address;
  uint16_t port;
  uint64_t last_applied_index;
  uint64_t last_heartbeat;
  bool is_active;
  int retry_count;
};

struct ReplicationEntry {
  uint64_t index;
  uint64_t term;
  std::vector<uint8_t> data;
  uint64_t timestamp;
  std::string checksum;
};

struct ReplicationBatch {
  std::vector<ReplicationEntry> entries;
  uint64_t start_index;
  uint64_t end_index;
  std::string batch_id;
};

struct ReplicationStats {
  uint64_t total_entries_replicated;
  uint64_t total_bytes_replicated;
  uint64_t active_targets;
  uint64_t failed_replications;
  uint64_t avg_replication_latency_ms;
  double replication_rate_per_second;
};

enum class ReplicationStrategy {
  SYNCHRONOUS,  // Wait for majority acknowledgment
  ASYNCHRONOUS, // Fire and forget
  QUORUM_BASED  // Wait for configurable quorum
};

// Interface for replication transport
class IReplicationTransport {
public:
  virtual ~IReplicationTransport() = default;
  virtual bool send_batch(const std::string &target_id,
                          const ReplicationBatch &batch) = 0;
  virtual bool send_heartbeat(const std::string &target_id) = 0;
  virtual bool request_sync(const std::string &target_id,
                            uint64_t from_index) = 0;
};

// Data replication manager for HA clustering
class ReplicationManager {
private:
  std::string node_id_;
  ValidatorConfig config_;

  // Replication targets
  std::unordered_map<std::string, ReplicationTarget> targets_;
  mutable std::mutex targets_mutex_;

  // Replication queue
  std::queue<ReplicationEntry> pending_entries_;
  mutable std::mutex queue_mutex_;

  // Transport layer
  std::shared_ptr<IReplicationTransport> transport_;

  // Threading
  std::atomic<bool> running_;
  std::thread replication_thread_;
  std::thread heartbeat_thread_;
  std::thread sync_thread_;

  // Configuration
  ReplicationStrategy strategy_;
  size_t batch_size_;
  int heartbeat_interval_ms_;
  int sync_check_interval_ms_;
  size_t quorum_size_;
  int max_retry_count_;

  // Statistics
  ReplicationStats stats_;
  mutable std::mutex stats_mutex_;

  // Callbacks
  std::function<void(const std::string &, bool)> replication_callback_;
  std::function<void(const std::string &)> target_failed_callback_;
  std::function<std::vector<ReplicationEntry>(uint64_t, uint64_t)>
      data_provider_;

  // Private methods
  void replication_loop();
  void heartbeat_loop();
  void sync_loop();
  void process_pending_entries();
  bool replicate_batch(const ReplicationBatch &batch);
  void handle_target_failure(const std::string &target_id);
  void update_target_status(const std::string &target_id, bool success);
  ReplicationBatch create_batch(const std::vector<ReplicationEntry> &entries);
  std::string calculate_checksum(const std::vector<uint8_t> &data);
  void update_stats(bool success, size_t bytes, uint64_t latency_ms);

public:
  ReplicationManager(const std::string &node_id, const ValidatorConfig &config);
  ~ReplicationManager();

  // Lifecycle management
  bool start();
  void stop();
  bool is_running() const { return running_.load(); }

  // Configuration
  void set_strategy(ReplicationStrategy strategy) { strategy_ = strategy; }
  void set_batch_size(size_t size) { batch_size_ = size; }
  void set_quorum_size(size_t size) { quorum_size_ = size; }
  void set_transport(std::shared_ptr<IReplicationTransport> transport) {
    transport_ = transport;
  }

  // Target management
  bool add_target(const std::string &node_id, const std::string &address,
                  uint16_t port);
  bool remove_target(const std::string &node_id);
  void activate_target(const std::string &node_id);
  void deactivate_target(const std::string &node_id);
  std::vector<ReplicationTarget> get_targets() const;

  // Replication operations
  bool replicate_entry(const std::vector<uint8_t> &data, uint64_t index,
                       uint64_t term);
  bool replicate_entries(const std::vector<ReplicationEntry> &entries);
  bool force_sync(const std::string &target_id);
  bool sync_all_targets();

  // Status and monitoring
  ReplicationStats get_stats() const;
  bool is_target_active(const std::string &target_id) const;
  uint64_t get_target_lag(const std::string &target_id) const;
  std::vector<std::string> get_failed_targets() const;

  // Callbacks
  void set_replication_callback(
      std::function<void(const std::string &, bool)> callback);
  void
  set_target_failed_callback(std::function<void(const std::string &)> callback);
  void set_data_provider(
      std::function<std::vector<ReplicationEntry>(uint64_t, uint64_t)>
          provider);

  // Recovery operations
  bool recover_target(const std::string &target_id);
  void reset_target_state(const std::string &target_id);
  bool validate_target_integrity(const std::string &target_id);
};

// Utility functions
namespace replication_utils {
std::string generate_batch_id();
std::vector<uint8_t> serialize_replication_entry(const ReplicationEntry &entry);
ReplicationEntry
deserialize_replication_entry(const std::vector<uint8_t> &data);
std::vector<uint8_t> serialize_replication_batch(const ReplicationBatch &batch);
ReplicationBatch
deserialize_replication_batch(const std::vector<uint8_t> &data);
std::string calculate_data_checksum(const std::vector<uint8_t> &data);
bool verify_batch_integrity(const ReplicationBatch &batch);
} // namespace replication_utils

} // namespace cluster
} // namespace slonana