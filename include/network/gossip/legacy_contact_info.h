#pragma once

#include "crds_data.h"
#include "common/types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace slonana {
namespace network {
namespace gossip {

using namespace slonana::common;

/**
 * LegacyContactInfo - Backward compatible contact info format
 * Based on Agave: gossip/src/legacy_contact_info.rs
 * 
 * Supports older protocol versions for smooth network upgrades
 */
struct LegacyContactInfo {
  PublicKey pubkey;
  uint64_t wallclock;
  uint64_t outset;
  uint16_t shred_version;
  std::string version;
  
  // Legacy address fields (v1 format)
  std::string gossip_addr;
  std::string tpu_addr;
  std::string tpu_forwards_addr;
  std::string tvu_addr;
  std::string tvu_forwards_addr;
  std::string repair_addr;
  std::string serve_repair_addr;
  
  LegacyContactInfo();
  explicit LegacyContactInfo(const PublicKey &pk);
  
  /**
   * Convert from modern ContactInfo
   */
  static LegacyContactInfo from_contact_info(const ContactInfo &ci);
  
  /**
   * Convert to modern ContactInfo
   */
  ContactInfo to_contact_info() const;
  
  /**
   * Check if this legacy format is compatible with a version
   */
  bool is_compatible(const std::string &other_version) const;
  
  /**
   * Serialize legacy format
   */
  std::vector<uint8_t> serialize() const;
  
  /**
   * Deserialize legacy format
   */
  static Result<LegacyContactInfo> deserialize(const std::vector<uint8_t> &data);
};

/**
 * LegacyVersion - Version compatibility helpers
 */
class LegacyVersion {
public:
  /**
   * Parse version string
   */
  static Result<std::tuple<uint16_t, uint16_t, uint16_t>> 
  parse_version(const std::string &version);
  
  /**
   * Check if two versions are compatible
   */
  static bool is_compatible(const std::string &v1, const std::string &v2);
  
  /**
   * Get minimum compatible version
   */
  static std::string get_minimum_version();
  
  /**
   * Get current version
   */
  static std::string get_current_version();
};

} // namespace gossip
} // namespace network
} // namespace slonana
