#pragma once

#include "common/types.h"
#include <cstdint>
#include <unordered_set>
#include <deque>
#include <mutex>

namespace slonana {
namespace network {
namespace gossip {

using namespace slonana::common;

/**
 * ReceivedCache - Deduplication cache for received gossip messages
 * Based on Agave: gossip/src/received_cache.rs
 * 
 * Tracks recently received CRDS value hashes to prevent processing
 * duplicate messages, reducing CPU and network overhead
 */
class ReceivedCache {
public:
  /**
   * Constructor
   * @param capacity Maximum number of hashes to cache
   */
  explicit ReceivedCache(size_t capacity = 10000);
  
  /**
   * Check if a hash has been received and add it
   * @param hash The CRDS value hash
   * @return true if this is a new hash (not seen before)
   */
  bool insert(const Hash &hash);
  
  /**
   * Check if a hash has been received (without inserting)
   * @param hash The CRDS value hash
   * @return true if hash has been seen before
   */
  bool contains(const Hash &hash) const;
  
  /**
   * Clear the cache
   */
  void clear();
  
  /**
   * Get current cache size
   */
  size_t size() const;
  
  /**
   * Get cache capacity
   */
  size_t capacity() const { return capacity_; }

private:
  size_t capacity_;
  std::unordered_set<Hash> cache_;
  std::deque<Hash> insertion_order_;  // For LRU eviction
  mutable std::mutex mutex_;
  
  void evict_oldest();
};

} // namespace gossip
} // namespace network
} // namespace slonana
