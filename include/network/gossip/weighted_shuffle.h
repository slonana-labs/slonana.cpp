#pragma once

#include "common/types.h"
#include <cstdint>
#include <vector>
#include <random>

namespace slonana {
namespace network {
namespace gossip {

using namespace slonana::common;

/**
 * WeightedShuffle - Stake-weighted random peer selection
 * Based on Agave: gossip/src/weighted_shuffle.rs
 * 
 * Selects peers with probability proportional to their stake
 * for improved network security and efficiency
 */
class WeightedShuffle {
public:
  /**
   * Node with stake weight
   */
  struct WeightedNode {
    PublicKey pubkey;
    uint64_t stake;  // Stake weight in lamports
    
    WeightedNode(const PublicKey &pk, uint64_t s) : pubkey(pk), stake(s) {}
  };
  
  /**
   * Constructor
   * @param nodes Vector of nodes with their stake weights
   * @param seed Random seed for deterministic shuffling
   */
  WeightedShuffle(const std::vector<WeightedNode> &nodes, uint64_t seed = 0);
  
  /**
   * Get next node in weighted random order
   * @return Pointer to next node, or nullptr if exhausted
   */
  const WeightedNode *next();
  
  /**
   * Reset iterator to beginning
   */
  void reset();
  
  /**
   * Get shuffled nodes in order
   * @param max_count Maximum number to return
   * @return Vector of nodes in weighted random order
   */
  std::vector<WeightedNode> get_shuffled(size_t max_count = SIZE_MAX);
  
  /**
   * Static helper: Select random node weighted by stake
   * @param nodes Nodes to select from
   * @param seed Random seed
   * @return Selected node or nullptr
   */
  static const WeightedNode *select_random(
      const std::vector<WeightedNode> &nodes, uint64_t seed);

private:
  std::vector<WeightedNode> nodes_;
  std::vector<size_t> shuffled_indices_;
  size_t current_index_;
  uint64_t seed_;
  std::mt19937_64 rng_;
  
  void compute_shuffle();
  uint64_t compute_weight_sum() const;
};

} // namespace gossip
} // namespace network
} // namespace slonana
