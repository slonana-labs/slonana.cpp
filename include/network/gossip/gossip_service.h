#pragma once

#include "crds.h"
#include "protocol.h"
#include "common/types.h"
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace slonana {
namespace network {
namespace gossip {

/**
 * GossipService - Main gossip protocol service
 * Agave: gossip/src/gossip_service.rs (GossipService struct)
 *
 * This service implements the Solana gossip protocol for:
 * - Peer discovery
 * - Cluster membership tracking
 * - Vote propagation
 * - Snapshot hash distribution
 * - Duplicate shred detection
 */
class GossipService {
public:
  struct Config {
    std::string bind_address;       // Gossip bind address
    uint16_t bind_port;             // Gossip port
    PublicKey node_pubkey;          // This node's public key
    uint16_t shred_version;         // Shred version for compatibility
    std::vector<std::string> entrypoints; // Bootstrap nodes
    size_t gossip_push_fanout = 6;  // Number of peers to push to
    size_t gossip_pull_fanout = 3;  // Number of peers to pull from
    uint64_t push_interval_ms = 100;     // Push interval
    uint64_t pull_interval_ms = 1000;    // Pull interval
    uint64_t trim_interval_ms = 10000;   // Trim old entries interval
    uint64_t entry_timeout_ms = 30000;   // Entry timeout
    bool enable_ping_pong = true;   // Enable ping/pong for latency
  };

  explicit GossipService(const Config &config);
  ~GossipService();

  /**
   * Start the gossip service
   */
  Result<bool> start();

  /**
   * Stop the gossip service
   */
  void stop();

  /**
   * Check if service is running
   */
  bool is_running() const { return running_.load(); }

  /**
   * Insert a local value into CRDS
   */
  Result<bool> insert_local_value(const CrdsValue &value);

  /**
   * Get contact info for all known nodes
   */
  std::vector<ContactInfo> get_contact_infos() const;

  /**
   * Get contact info for a specific node
   */
  const ContactInfo *get_contact_info(const PublicKey &pubkey) const;

  /**
   * Get all known peers
   */
  std::vector<PublicKey> get_known_peers() const;

  /**
   * Get votes for a validator
   */
  std::vector<Vote> get_votes(const PublicKey &pubkey) const;

  /**
   * Get statistics
   */
  struct Stats {
    size_t num_nodes = 0;
    size_t num_votes = 0;
    size_t num_entries = 0;
    size_t push_messages_sent = 0;
    size_t pull_requests_sent = 0;
    size_t pull_responses_sent = 0;
    size_t prune_messages_sent = 0;
    size_t ping_messages_sent = 0;
    size_t pong_messages_sent = 0;
    size_t messages_received = 0;
    size_t messages_failed = 0;
  };
  Stats get_stats() const;

  /**
   * Register callback for new contact info
   */
  using ContactInfoCallback = std::function<void(const ContactInfo &)>;
  void register_contact_info_callback(ContactInfoCallback callback);

  /**
   * Register callback for new votes
   */
  using VoteCallback = std::function<void(const Vote &)>;
  void register_vote_callback(VoteCallback callback);

private:
  Config config_;
  std::unique_ptr<Crds> crds_;
  std::atomic<bool> running_;
  std::atomic<bool> shutdown_requested_;

  // Thread handles
  std::vector<std::thread> threads_;

  // Statistics
  mutable std::mutex stats_mutex_;
  Stats stats_;

  // Callbacks
  std::vector<ContactInfoCallback> contact_info_callbacks_;
  std::vector<VoteCallback> vote_callbacks_;
  std::mutex callbacks_mutex_;

  // Network socket
  int gossip_socket_;
  std::mutex socket_mutex_;

  // Push gossip state
  struct PushState {
    std::vector<PublicKey> active_set; // Active push peers
    uint64_t last_push_time = 0;
    std::mutex mutex;
  };
  PushState push_state_;

  // Pull gossip state
  struct PullState {
    std::vector<PublicKey> pull_peers; // Peers to pull from
    uint64_t last_pull_time = 0;
    CrdsFilter filter; // Bloom filter of known values
    std::mutex mutex;
  };
  PullState pull_state_;

  // Ping/Pong state
  struct PingPongState {
    std::map<PublicKey, uint64_t> pending_pings; // token -> timestamp
    std::map<PublicKey, uint64_t> latencies;     // peer -> latency_ms
    std::mutex mutex;
  };
  PingPongState ping_pong_state_;

  // Service threads
  void receiver_thread();
  void push_gossip_thread();
  void pull_gossip_thread();
  void trim_thread();
  void ping_pong_thread();

  // Message handling
  void handle_pull_request(const Protocol &msg, const std::string &from_addr);
  void handle_pull_response(const Protocol &msg);
  void handle_push_message(const Protocol &msg);
  void handle_prune_message(const Protocol &msg);
  void handle_ping_message(const Protocol &msg, const std::string &from_addr);
  void handle_pong_message(const Protocol &msg);

  // Push gossip logic
  void do_push_gossip();
  std::vector<PublicKey> select_push_peers(size_t count);
  std::vector<CrdsValue> select_values_to_push();

  // Pull gossip logic
  void do_pull_gossip();
  std::vector<PublicKey> select_pull_peers(size_t count);
  Protocol build_pull_request();

  // Network operations
  bool send_message(const Protocol &msg, const std::string &dest_addr);
  Result<Protocol> receive_message();

  // Helper methods
  void update_active_set();
  void notify_callbacks(const CrdsValue &value);
  bool setup_socket();
  void close_socket();
};

} // namespace gossip
} // namespace network
} // namespace slonana
