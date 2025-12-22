#include "network/gossip/crds_shards.h"
#include <algorithm>
#include <random>

namespace slonana {
namespace network {
namespace gossip {

CrdsShards::CrdsShards(size_t num_shards)
    : num_shards_(num_shards), shards_(num_shards) {}

size_t CrdsShards::shard_index(const PublicKey &pubkey) const {
  if (pubkey.empty() || num_shards_ == 0) {
    return 0;
  }
  
  // Hash the pubkey to determine shard
  uint64_t hash = 0;
  for (size_t i = 0; i < std::min(pubkey.size(), size_t(8)); ++i) {
    hash = (hash << 8) | pubkey[i];
  }
  
  return hash % num_shards_;
}

void CrdsShards::insert(size_t index, const VersionedCrdsValue *value) {
  if (!value) return;
  
  std::lock_guard<std::mutex> lock(mutex_);
  
  PublicKey pubkey = value->value.pubkey();
  size_t shard = shard_index(pubkey);
  shards_[shard].insert(index);
}

void CrdsShards::remove(size_t index, const VersionedCrdsValue *value) {
  if (!value) return;
  
  std::lock_guard<std::mutex> lock(mutex_);
  
  PublicKey pubkey = value->value.pubkey();
  size_t shard = shard_index(pubkey);
  shards_[shard].erase(index);
}

std::unordered_set<size_t> CrdsShards::get_indices(const PublicKey &pubkey) const {
  std::lock_guard<std::mutex> lock(mutex_);
  
  size_t shard = shard_index(pubkey);
  return shards_[shard];
}

std::vector<size_t> CrdsShards::sample(size_t max_count) const {
  std::lock_guard<std::mutex> lock(mutex_);
  
  std::vector<size_t> result;
  result.reserve(max_count);
  
  // Collect all indices from all shards
  for (const auto &shard : shards_) {
    for (size_t index : shard) {
      result.push_back(index);
      if (result.size() >= max_count) {
        break;
      }
    }
    if (result.size() >= max_count) {
      break;
    }
  }
  
  // Shuffle for randomness
  std::random_device rd;
  std::mt19937 gen(rd());
  std::shuffle(result.begin(), result.end(), gen);
  
  return result;
}

size_t CrdsShards::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  
  size_t total = 0;
  for (const auto &shard : shards_) {
    total += shard.size();
  }
  return total;
}

void CrdsShards::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  
  for (auto &shard : shards_) {
    shard.clear();
  }
}

} // namespace gossip
} // namespace network
} // namespace slonana
