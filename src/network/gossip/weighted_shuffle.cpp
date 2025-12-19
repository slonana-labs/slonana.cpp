#include "network/gossip/weighted_shuffle.h"
#include <algorithm>
#include <numeric>

namespace slonana {
namespace network {
namespace gossip {

WeightedShuffle::WeightedShuffle(const std::vector<WeightedNode> &nodes,
                                 uint64_t seed)
    : nodes_(nodes), current_index_(0), seed_(seed), rng_(seed) {
  compute_shuffle();
}

uint64_t WeightedShuffle::compute_weight_sum() const {
  uint64_t sum = 0;
  for (const auto &node : nodes_) {
    sum += node.stake;
  }
  return sum;
}

void WeightedShuffle::compute_shuffle() {
  if (nodes_.empty()) {
    return;
  }
  
  shuffled_indices_.clear();
  shuffled_indices_.reserve(nodes_.size());
  
  // Initialize indices
  for (size_t i = 0; i < nodes_.size(); ++i) {
    shuffled_indices_.push_back(i);
  }
  
  // Weighted shuffle algorithm
  // For each position, select from remaining nodes with probability
  // proportional to stake
  for (size_t i = 0; i < nodes_.size() - 1; ++i) {
    // Calculate total remaining stake
    uint64_t total_stake = 0;
    for (size_t j = i; j < nodes_.size(); ++j) {
      total_stake += nodes_[shuffled_indices_[j]].stake;
    }
    
    if (total_stake == 0) {
      // No stake, just use remaining order
      break;
    }
    
    // Select weighted random position
    uint64_t rand_val = rng_() % total_stake;
    uint64_t cumulative = 0;
    
    for (size_t j = i; j < nodes_.size(); ++j) {
      cumulative += nodes_[shuffled_indices_[j]].stake;
      if (cumulative > rand_val) {
        // Swap selected position to current position
        std::swap(shuffled_indices_[i], shuffled_indices_[j]);
        break;
      }
    }
  }
}

const WeightedShuffle::WeightedNode *WeightedShuffle::next() {
  if (current_index_ >= shuffled_indices_.size()) {
    return nullptr;
  }
  
  size_t node_index = shuffled_indices_[current_index_++];
  return &nodes_[node_index];
}

void WeightedShuffle::reset() {
  current_index_ = 0;
}

std::vector<WeightedShuffle::WeightedNode>
WeightedShuffle::get_shuffled(size_t max_count) {
  std::vector<WeightedNode> result;
  result.reserve(std::min(max_count, nodes_.size()));
  
  reset();
  while (result.size() < max_count) {
    const WeightedNode *node = next();
    if (!node) {
      break;
    }
    result.push_back(*node);
  }
  
  return result;
}

const WeightedShuffle::WeightedNode *
WeightedShuffle::select_random(const std::vector<WeightedNode> &nodes,
                                uint64_t seed) {
  if (nodes.empty()) {
    return nullptr;
  }
  
  uint64_t total_stake = 0;
  for (const auto &node : nodes) {
    total_stake += node.stake;
  }
  
  if (total_stake == 0) {
    // No stake, select uniformly
    std::mt19937_64 rng(seed);
    return &nodes[rng() % nodes.size()];
  }
  
  // Select weighted by stake
  std::mt19937_64 rng(seed);
  uint64_t rand_val = rng() % total_stake;
  uint64_t cumulative = 0;
  
  for (const auto &node : nodes) {
    cumulative += node.stake;
    if (cumulative > rand_val) {
      return &node;
    }
  }
  
  return &nodes.back();
}

} // namespace gossip
} // namespace network
} // namespace slonana
