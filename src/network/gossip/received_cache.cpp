#include "network/gossip/received_cache.h"

namespace slonana {
namespace network {
namespace gossip {

ReceivedCache::ReceivedCache(size_t capacity) : capacity_(capacity) {
  cache_.reserve(capacity);
}

bool ReceivedCache::insert(const Hash &hash) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  // Check if already in cache
  if (cache_.find(hash) != cache_.end()) {
    return false;  // Already seen
  }
  
  // Evict oldest if at capacity
  if (cache_.size() >= capacity_) {
    evict_oldest();
  }
  
  // Insert new hash
  cache_.insert(hash);
  insertion_order_.push_back(hash);
  
  return true;  // New hash
}

bool ReceivedCache::contains(const Hash &hash) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cache_.find(hash) != cache_.end();
}

void ReceivedCache::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_.clear();
  insertion_order_.clear();
}

size_t ReceivedCache::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cache_.size();
}

void ReceivedCache::evict_oldest() {
  if (insertion_order_.empty()) {
    return;
  }
  
  // Remove oldest hash
  Hash oldest = insertion_order_.front();
  insertion_order_.pop_front();
  cache_.erase(oldest);
}

} // namespace gossip
} // namespace network
} // namespace slonana
