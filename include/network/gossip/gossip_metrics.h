#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <mutex>
#include <chrono>
#include <functional>

namespace slonana {
namespace network {
namespace gossip {

/**
 * GossipMetrics - Advanced metrics collection for gossip protocol
 * Based on Agave: gossip/src/cluster_info_metrics.rs
 * 
 * Tracks detailed performance and behavior metrics for monitoring
 * and debugging the gossip protocol
 */
class GossipMetrics {
public:
  GossipMetrics();
  
  // Message metrics
  void record_push_message_sent(size_t num_values);
  void record_pull_request_sent();
  void record_pull_response_sent(size_t num_values);
  void record_prune_message_sent(size_t num_prunes);
  void record_ping_sent();
  void record_pong_sent();
  
  void record_push_message_received(size_t num_values);
  void record_pull_request_received();
  void record_pull_response_received(size_t num_values);
  void record_prune_message_received(size_t num_prunes);
  void record_ping_received();
  void record_pong_received();
  
  // CRDS metrics
  void record_crds_insert(bool success);
  void record_crds_update();
  void record_crds_duplicate();
  void record_crds_trim(size_t count);
  
  // Network metrics
  void record_packet_sent(size_t bytes);
  void record_packet_received(size_t bytes);
  void record_packet_error();
  
  // Performance metrics
  void record_push_duration_us(uint64_t duration_us);
  void record_pull_duration_us(uint64_t duration_us);
  void record_verify_duration_us(uint64_t duration_us);
  
  // Peer metrics
  void record_active_peers(size_t count);
  void record_peer_added();
  void record_peer_removed();
  
  // Get metrics as map (for export)
  std::map<std::string, uint64_t> get_metrics() const;
  
  // Get metrics as formatted string
  std::string to_string() const;
  
  // Reset all metrics
  void reset();
  
  // Snapshot for rate calculations
  void snapshot();
  
  // Get rate metrics (per second since last snapshot)
  std::map<std::string, double> get_rates() const;

private:
  mutable std::mutex mutex_;
  
  // Counters
  uint64_t push_messages_sent_ = 0;
  uint64_t pull_requests_sent_ = 0;
  uint64_t pull_responses_sent_ = 0;
  uint64_t prune_messages_sent_ = 0;
  uint64_t ping_messages_sent_ = 0;
  uint64_t pong_messages_sent_ = 0;
  
  uint64_t push_messages_received_ = 0;
  uint64_t pull_requests_received_ = 0;
  uint64_t pull_responses_received_ = 0;
  uint64_t prune_messages_received_ = 0;
  uint64_t ping_messages_received_ = 0;
  uint64_t pong_messages_received_ = 0;
  
  uint64_t push_values_sent_ = 0;
  uint64_t pull_values_received_ = 0;
  
  uint64_t crds_inserts_ = 0;
  uint64_t crds_insert_failures_ = 0;
  uint64_t crds_updates_ = 0;
  uint64_t crds_duplicates_ = 0;
  uint64_t crds_trims_ = 0;
  
  uint64_t bytes_sent_ = 0;
  uint64_t bytes_received_ = 0;
  uint64_t packet_errors_ = 0;
  
  uint64_t total_push_duration_us_ = 0;
  uint64_t push_count_ = 0;
  uint64_t total_pull_duration_us_ = 0;
  uint64_t pull_count_ = 0;
  uint64_t total_verify_duration_us_ = 0;
  uint64_t verify_count_ = 0;
  
  uint64_t active_peers_ = 0;
  uint64_t peers_added_ = 0;
  uint64_t peers_removed_ = 0;
  
  // Snapshot data for rate calculations
  std::chrono::steady_clock::time_point last_snapshot_;
  std::map<std::string, uint64_t> snapshot_values_;
};

/**
 * ScopedTimer - RAII timer for measuring operation duration
 */
class ScopedTimer {
public:
  explicit ScopedTimer(std::function<void(uint64_t)> callback)
      : callback_(callback), start_(std::chrono::steady_clock::now()) {}
  
  ~ScopedTimer() {
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        end - start_).count();
    if (callback_) {
      callback_(duration);
    }
  }

private:
  std::function<void(uint64_t)> callback_;
  std::chrono::steady_clock::time_point start_;
};

} // namespace gossip
} // namespace network
} // namespace slonana
