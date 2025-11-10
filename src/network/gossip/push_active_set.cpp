#include "network/gossip/push_active_set.h"
#include "network/gossip/crds_data.h"
#include <algorithm>

namespace slonana {
namespace network {
namespace gossip {

PushActiveSet::PushActiveSet(size_t fanout, uint64_t rotation_interval_ms)
    : fanout_(fanout), rotation_interval_ms_(rotation_interval_ms),
      last_rotation_time_(timestamp()), rng_(std::random_device{}()) {}

std::vector<PublicKey> PushActiveSet::get_active_set() {
  std::lock_guard<std::mutex> lock(mutex_);
  
  // Check if we need to rotate
  if (should_rotate(timestamp())) {
    const_cast<PushActiveSet*>(this)->rotate();
  }
  
  return active_set_;
}

void PushActiveSet::update_peers(const std::vector<PublicKey> &peers) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  peer_pool_ = peers;
  
  // Reselect active set with new peers
  select_active_set();
}

void PushActiveSet::add_peer(const PublicKey &peer) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  // Check if already in pool
  auto it = std::find(peer_pool_.begin(), peer_pool_.end(), peer);
  if (it == peer_pool_.end()) {
    peer_pool_.push_back(peer);
  }
}

void PushActiveSet::remove_peer(const PublicKey &peer) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  // Remove from pool
  peer_pool_.erase(
      std::remove(peer_pool_.begin(), peer_pool_.end(), peer),
      peer_pool_.end());
  
  // Remove from active set
  active_set_.erase(
      std::remove(active_set_.begin(), active_set_.end(), peer),
      active_set_.end());
  
  // Remove from recently used
  auto it = std::find(recently_used_.begin(), recently_used_.end(), peer);
  if (it != recently_used_.end()) {
    recently_used_.erase(it);
  }
}

void PushActiveSet::rotate() {
  last_rotation_time_ = timestamp();
  
  // Move current active set to recently used
  for (const auto &peer : active_set_) {
    recently_used_.push_back(peer);
  }
  
  // Limit recently used size (keep last 2 rotations worth)
  while (recently_used_.size() > fanout_ * 2) {
    recently_used_.pop_front();
  }
  
  select_active_set();
}

bool PushActiveSet::should_rotate(uint64_t now) const {
  return (now - last_rotation_time_) >= rotation_interval_ms_;
}

void PushActiveSet::set_fanout(size_t fanout) {
  std::lock_guard<std::mutex> lock(mutex_);
  fanout_ = fanout;
  select_active_set();
}

size_t PushActiveSet::active_size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_set_.size();
}

size_t PushActiveSet::pool_size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return peer_pool_.size();
}

void PushActiveSet::select_active_set() {
  active_set_.clear();
  
  if (peer_pool_.empty()) {
    return;
  }
  
  // Create list of candidates (excluding recently used)
  std::vector<PublicKey> candidates;
  for (const auto &peer : peer_pool_) {
    if (!is_recently_used(peer)) {
      candidates.push_back(peer);
    }
  }
  
  // If not enough candidates, include some recently used
  if (candidates.size() < fanout_) {
    for (const auto &peer : peer_pool_) {
      if (is_recently_used(peer)) {
        candidates.push_back(peer);
        if (candidates.size() >= fanout_) {
          break;
        }
      }
    }
  }
  
  // Shuffle and select fanout peers
  std::shuffle(candidates.begin(), candidates.end(), rng_);
  
  size_t count = std::min(fanout_, candidates.size());
  active_set_.insert(active_set_.end(), 
                     candidates.begin(), 
                     candidates.begin() + count);
}

bool PushActiveSet::is_recently_used(const PublicKey &peer) const {
  return std::find(recently_used_.begin(), recently_used_.end(), peer) 
         != recently_used_.end();
}

} // namespace gossip
} // namespace network
} // namespace slonana
