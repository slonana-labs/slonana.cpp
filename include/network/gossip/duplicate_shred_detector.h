#pragma once

#include "crds_data.h"
#include "common/types.h"
#include <cstdint>
#include <vector>
#include <map>
#include <mutex>

namespace slonana {
namespace network {
namespace gossip {

/**
 * DuplicateShred - Information about a duplicate shred
 * Already defined in crds_data.h, but this provides detection logic
 */
struct DuplicateShredInfo {
  uint64_t slot;
  uint32_t index;
  PublicKey from;
  std::vector<uint8_t> chunk1;
  std::vector<uint8_t> chunk2;
  uint64_t timestamp;
};

/**
 * DuplicateShredDetector - Detects and tracks duplicate shreds
 * Based on Agave: gossip/src/duplicate_shred.rs
 * 
 * Monitors for conflicting shreds at the same slot/index
 * which indicates potential malicious behavior
 */
class DuplicateShredDetector {
public:
  DuplicateShredDetector();
  
  /**
   * Check if a shred is a duplicate
   * @param slot Slot number
   * @param index Shred index
   * @param shred_data Shred data
   * @param from Validator pubkey
   * @return true if duplicate detected
   */
  bool check_and_insert(uint64_t slot, uint32_t index,
                        const std::vector<uint8_t> &shred_data,
                        const PublicKey &from);
  
  /**
   * Get all detected duplicate shreds
   * @return Vector of duplicate shred information
   */
  std::vector<DuplicateShredInfo> get_duplicates() const;
  
  /**
   * Clear duplicates older than timestamp
   * @param before_timestamp Timestamp threshold
   * @return Number of entries removed
   */
  size_t prune_old(uint64_t before_timestamp);
  
  /**
   * Get number of detected duplicates
   */
  size_t size() const;
  
  /**
   * Clear all tracked shreds
   */
  void clear();

private:
  struct ShredKey {
    uint64_t slot;
    uint32_t index;
    
    bool operator<(const ShredKey &other) const {
      if (slot != other.slot) return slot < other.slot;
      return index < other.index;
    }
  };
  
  struct ShredRecord {
    std::vector<uint8_t> data;
    PublicKey from;
    uint64_t timestamp;
  };
  
  std::map<ShredKey, ShredRecord> seen_shreds_;
  std::vector<DuplicateShredInfo> duplicates_;
  mutable std::mutex mutex_;
};

} // namespace gossip
} // namespace network
} // namespace slonana
