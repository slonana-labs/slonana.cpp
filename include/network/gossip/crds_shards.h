#pragma once

#include "crds_value.h"
#include <cstdint>
#include <vector>
#include <unordered_set>

namespace slonana {
namespace network {
namespace gossip {

/**
 * CrdsShards - Sharding for efficient CRDS data management
 * Based on Agave: gossip/src/crds_shards.rs
 * 
 * Shards CRDS entries by pubkey for efficient lookups and range queries
 * Optimizes performance for large clusters (>1000 nodes)
 */
class CrdsShards {
public:
  /**
   * Constructor
   * @param num_shards Number of shards (power of 2 recommended)
   */
  explicit CrdsShards(size_t num_shards = 256);
  
  /**
   * Insert an entry into the appropriate shard
   * @param index The CRDS table index
   * @param value The versioned CRDS value
   */
  void insert(size_t index, const VersionedCrdsValue *value);
  
  /**
   * Remove an entry from its shard
   * @param index The CRDS table index
   * @param value The versioned CRDS value
   */
  void remove(size_t index, const VersionedCrdsValue *value);
  
  /**
   * Get all indices in a shard for a given pubkey
   * @param pubkey The public key to query
   * @return Set of CRDS table indices
   */
  std::unordered_set<size_t> get_indices(const PublicKey &pubkey) const;
  
  /**
   * Get a random sample of entries from shards
   * @param max_count Maximum number of entries to return
   * @return Vector of CRDS table indices
   */
  std::vector<size_t> sample(size_t max_count) const;
  
  /**
   * Get total number of entries across all shards
   */
  size_t size() const;
  
  /**
   * Clear all shards
   */
  void clear();

private:
  size_t num_shards_;
  std::vector<std::unordered_set<size_t>> shards_;
  mutable std::mutex mutex_;
  
  // Hash a pubkey to a shard index
  size_t shard_index(const PublicKey &pubkey) const;
};

} // namespace gossip
} // namespace network
} // namespace slonana
