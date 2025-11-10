#include "network/gossip/gossip_metrics.h"
#include <sstream>
#include <iomanip>

namespace slonana {
namespace network {
namespace gossip {

GossipMetrics::GossipMetrics()
    : last_snapshot_(std::chrono::steady_clock::now()) {}

// Message metrics - Sent
void GossipMetrics::record_push_message_sent(size_t num_values) {
  std::lock_guard<std::mutex> lock(mutex_);
  push_messages_sent_++;
  push_values_sent_ += num_values;
}

void GossipMetrics::record_pull_request_sent() {
  std::lock_guard<std::mutex> lock(mutex_);
  pull_requests_sent_++;
}

void GossipMetrics::record_pull_response_sent(size_t num_values) {
  std::lock_guard<std::mutex> lock(mutex_);
  pull_responses_sent_++;
}

void GossipMetrics::record_prune_message_sent(size_t num_prunes) {
  std::lock_guard<std::mutex> lock(mutex_);
  prune_messages_sent_++;
}

void GossipMetrics::record_ping_sent() {
  std::lock_guard<std::mutex> lock(mutex_);
  ping_messages_sent_++;
}

void GossipMetrics::record_pong_sent() {
  std::lock_guard<std::mutex> lock(mutex_);
  pong_messages_sent_++;
}

// Message metrics - Received
void GossipMetrics::record_push_message_received(size_t num_values) {
  std::lock_guard<std::mutex> lock(mutex_);
  push_messages_received_++;
}

void GossipMetrics::record_pull_request_received() {
  std::lock_guard<std::mutex> lock(mutex_);
  pull_requests_received_++;
}

void GossipMetrics::record_pull_response_received(size_t num_values) {
  std::lock_guard<std::mutex> lock(mutex_);
  pull_responses_received_++;
  pull_values_received_ += num_values;
}

void GossipMetrics::record_prune_message_received(size_t num_prunes) {
  std::lock_guard<std::mutex> lock(mutex_);
  prune_messages_received_++;
}

void GossipMetrics::record_ping_received() {
  std::lock_guard<std::mutex> lock(mutex_);
  ping_messages_received_++;
}

void GossipMetrics::record_pong_received() {
  std::lock_guard<std::mutex> lock(mutex_);
  pong_messages_received_++;
}

// CRDS metrics
void GossipMetrics::record_crds_insert(bool success) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (success) {
    crds_inserts_++;
  } else {
    crds_insert_failures_++;
  }
}

void GossipMetrics::record_crds_update() {
  std::lock_guard<std::mutex> lock(mutex_);
  crds_updates_++;
}

void GossipMetrics::record_crds_duplicate() {
  std::lock_guard<std::mutex> lock(mutex_);
  crds_duplicates_++;
}

void GossipMetrics::record_crds_trim(size_t count) {
  std::lock_guard<std::mutex> lock(mutex_);
  crds_trims_ += count;
}

// Network metrics
void GossipMetrics::record_packet_sent(size_t bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  bytes_sent_ += bytes;
}

void GossipMetrics::record_packet_received(size_t bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  bytes_received_ += bytes;
}

void GossipMetrics::record_packet_error() {
  std::lock_guard<std::mutex> lock(mutex_);
  packet_errors_++;
}

// Performance metrics
void GossipMetrics::record_push_duration_us(uint64_t duration_us) {
  std::lock_guard<std::mutex> lock(mutex_);
  total_push_duration_us_ += duration_us;
  push_count_++;
}

void GossipMetrics::record_pull_duration_us(uint64_t duration_us) {
  std::lock_guard<std::mutex> lock(mutex_);
  total_pull_duration_us_ += duration_us;
  pull_count_++;
}

void GossipMetrics::record_verify_duration_us(uint64_t duration_us) {
  std::lock_guard<std::mutex> lock(mutex_);
  total_verify_duration_us_ += duration_us;
  verify_count_++;
}

// Peer metrics
void GossipMetrics::record_active_peers(size_t count) {
  std::lock_guard<std::mutex> lock(mutex_);
  active_peers_ = count;
}

void GossipMetrics::record_peer_added() {
  std::lock_guard<std::mutex> lock(mutex_);
  peers_added_++;
}

void GossipMetrics::record_peer_removed() {
  std::lock_guard<std::mutex> lock(mutex_);
  peers_removed_++;
}

std::map<std::string, uint64_t> GossipMetrics::get_metrics() const {
  std::lock_guard<std::mutex> lock(mutex_);
  
  std::map<std::string, uint64_t> metrics;
  
  // Messages sent
  metrics["push_messages_sent"] = push_messages_sent_;
  metrics["pull_requests_sent"] = pull_requests_sent_;
  metrics["pull_responses_sent"] = pull_responses_sent_;
  metrics["prune_messages_sent"] = prune_messages_sent_;
  metrics["ping_messages_sent"] = ping_messages_sent_;
  metrics["pong_messages_sent"] = pong_messages_sent_;
  
  // Messages received
  metrics["push_messages_received"] = push_messages_received_;
  metrics["pull_requests_received"] = pull_requests_received_;
  metrics["pull_responses_received"] = pull_responses_received_;
  metrics["prune_messages_received"] = prune_messages_received_;
  metrics["ping_messages_received"] = ping_messages_received_;
  metrics["pong_messages_received"] = pong_messages_received_;
  
  // Values
  metrics["push_values_sent"] = push_values_sent_;
  metrics["pull_values_received"] = pull_values_received_;
  
  // CRDS
  metrics["crds_inserts"] = crds_inserts_;
  metrics["crds_insert_failures"] = crds_insert_failures_;
  metrics["crds_updates"] = crds_updates_;
  metrics["crds_duplicates"] = crds_duplicates_;
  metrics["crds_trims"] = crds_trims_;
  
  // Network
  metrics["bytes_sent"] = bytes_sent_;
  metrics["bytes_received"] = bytes_received_;
  metrics["packet_errors"] = packet_errors_;
  
  // Performance (averages)
  if (push_count_ > 0) {
    metrics["avg_push_duration_us"] = total_push_duration_us_ / push_count_;
  }
  if (pull_count_ > 0) {
    metrics["avg_pull_duration_us"] = total_pull_duration_us_ / pull_count_;
  }
  if (verify_count_ > 0) {
    metrics["avg_verify_duration_us"] = total_verify_duration_us_ / verify_count_;
  }
  
  // Peers
  metrics["active_peers"] = active_peers_;
  metrics["peers_added"] = peers_added_;
  metrics["peers_removed"] = peers_removed_;
  
  return metrics;
}

std::string GossipMetrics::to_string() const {
  auto metrics = get_metrics();
  
  std::ostringstream oss;
  oss << "=== Gossip Metrics ===\n";
  
  for (const auto &[key, value] : metrics) {
    oss << std::setw(30) << std::left << key << ": " << value << "\n";
  }
  
  return oss.str();
}

void GossipMetrics::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  
  push_messages_sent_ = 0;
  pull_requests_sent_ = 0;
  pull_responses_sent_ = 0;
  prune_messages_sent_ = 0;
  ping_messages_sent_ = 0;
  pong_messages_sent_ = 0;
  
  push_messages_received_ = 0;
  pull_requests_received_ = 0;
  pull_responses_received_ = 0;
  prune_messages_received_ = 0;
  ping_messages_received_ = 0;
  pong_messages_received_ = 0;
  
  push_values_sent_ = 0;
  pull_values_received_ = 0;
  
  crds_inserts_ = 0;
  crds_insert_failures_ = 0;
  crds_updates_ = 0;
  crds_duplicates_ = 0;
  crds_trims_ = 0;
  
  bytes_sent_ = 0;
  bytes_received_ = 0;
  packet_errors_ = 0;
  
  total_push_duration_us_ = 0;
  push_count_ = 0;
  total_pull_duration_us_ = 0;
  pull_count_ = 0;
  total_verify_duration_us_ = 0;
  verify_count_ = 0;
  
  peers_added_ = 0;
  peers_removed_ = 0;
}

void GossipMetrics::snapshot() {
  std::lock_guard<std::mutex> lock(mutex_);
  
  last_snapshot_ = std::chrono::steady_clock::now();
  snapshot_values_ = get_metrics();
}

std::map<std::string, double> GossipMetrics::get_rates() const {
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto now = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::seconds>(
      now - last_snapshot_).count();
  
  if (duration == 0) {
    return {};
  }
  
  auto current_metrics = get_metrics();
  std::map<std::string, double> rates;
  
  for (const auto &[key, value] : current_metrics) {
    auto it = snapshot_values_.find(key);
    if (it != snapshot_values_.end()) {
      double diff = static_cast<double>(value - it->second);
      rates[key + "_per_sec"] = diff / duration;
    }
  }
  
  return rates;
}

} // namespace gossip
} // namespace network
} // namespace slonana
