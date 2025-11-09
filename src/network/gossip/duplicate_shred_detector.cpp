#include "network/gossip/duplicate_shred_detector.h"
#include "network/gossip/crds_data.h"
#include <algorithm>

namespace slonana {
namespace network {
namespace gossip {

DuplicateShredDetector::DuplicateShredDetector() {}

bool DuplicateShredDetector::check_and_insert(
    uint64_t slot, uint32_t index, const std::vector<uint8_t> &shred_data,
    const PublicKey &from) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  ShredKey key{slot, index};
  
  auto it = seen_shreds_.find(key);
  if (it == seen_shreds_.end()) {
    // First time seeing this slot/index
    ShredRecord record;
    record.data = shred_data;
    record.from = from;
    record.timestamp = timestamp();
    seen_shreds_[key] = record;
    return false;
  }
  
  // Compare with previously seen shred
  const ShredRecord &prev = it->second;
  
  // Check if data differs (duplicate shred detected!)
  if (prev.data != shred_data) {
    // Record duplicate
    DuplicateShredInfo dup;
    dup.slot = slot;
    dup.index = index;
    dup.from = from;
    dup.chunk1 = prev.data;
    dup.chunk2 = shred_data;
    dup.timestamp = timestamp();
    
    duplicates_.push_back(dup);
    return true;
  }
  
  return false;  // Same data, not a duplicate
}

std::vector<DuplicateShredInfo> DuplicateShredDetector::get_duplicates() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return duplicates_;
}

size_t DuplicateShredDetector::prune_old(uint64_t before_timestamp) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  size_t removed = 0;
  
  // Prune old seen shreds
  auto it = seen_shreds_.begin();
  while (it != seen_shreds_.end()) {
    if (it->second.timestamp < before_timestamp) {
      it = seen_shreds_.erase(it);
      removed++;
    } else {
      ++it;
    }
  }
  
  // Prune old duplicates
  auto dup_it = std::remove_if(
      duplicates_.begin(), duplicates_.end(),
      [before_timestamp](const DuplicateShredInfo &dup) {
        return dup.timestamp < before_timestamp;
      });
  
  size_t dup_removed = std::distance(dup_it, duplicates_.end());
  duplicates_.erase(dup_it, duplicates_.end());
  removed += dup_removed;
  
  return removed;
}

size_t DuplicateShredDetector::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return duplicates_.size();
}

void DuplicateShredDetector::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  seen_shreds_.clear();
  duplicates_.clear();
}

} // namespace gossip
} // namespace network
} // namespace slonana
