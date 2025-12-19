#pragma once

#include "common/types.h"
#include <cstdint>
#include <vector>
#include <unordered_set>
#include <deque>
#include <mutex>
#include <random>

namespace slonana {
namespace network {
namespace gossip {

using namespace slonana::common;

/**
 * PushActiveSet - Manages active peers for push gossip
 * Based on Agave: gossip/src/push_active_set.rs
 * 
 * Maintains a rotating set of active peers for push gossip
 * to ensure good coverage while limiting bandwidth
 */
class PushActiveSet {
public:
  /**
   * Constructor
   * @param fanout Number of peers to push to each round
   * @param rotation_interval_ms Interval to rotate active set
   */
  PushActiveSet(size_t fanout = 6, uint64_t rotation_interval_ms = 30000);
  
  /**
   * Get current active peers for push gossip
   * @return Vector of active peer pubkeys
   */
  std::vector<PublicKey> get_active_set();
  
  /**
   * Update the pool of available peers
   * @param peers All known peers
   */
  void update_peers(const std::vector<PublicKey> &peers);
  
  /**
   * Add a new peer to the pool
   * @param peer Peer pubkey to add
   */
  void add_peer(const PublicKey &peer);
  
  /**
   * Remove a peer from the pool
   * @param peer Peer pubkey to remove
   */
  void remove_peer(const PublicKey &peer);
  
  /**
   * Force rotation of active set
   */
  void rotate();
  
  /**
   * Check if rotation is needed based on time
   * @param now Current timestamp
   * @return true if rotation should occur
   */
  bool should_rotate(uint64_t now) const;
  
  /**
   * Get fanout size
   */
  size_t get_fanout() const { return fanout_; }
  
  /**
   * Set fanout size
   */
  void set_fanout(size_t fanout);
  
  /**
   * Get current active set size
   */
  size_t active_size() const;
  
  /**
   * Get total peer pool size
   */
  size_t pool_size() const;

private:
  size_t fanout_;
  uint64_t rotation_interval_ms_;
  uint64_t last_rotation_time_;
  
  std::vector<PublicKey> peer_pool_;
  std::vector<PublicKey> active_set_;
  std::deque<PublicKey> recently_used_;  // Track recently used to avoid immediate reuse
  
  mutable std::mutex mutex_;
  std::mt19937 rng_;
  
  void select_active_set();
  bool is_recently_used(const PublicKey &peer) const;
};

} // namespace gossip
} // namespace network
} // namespace slonana
