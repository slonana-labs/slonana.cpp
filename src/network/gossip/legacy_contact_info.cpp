#include "network/gossip/legacy_contact_info.h"
#include "network/gossip/serializer.h"
#include <sstream>

namespace slonana {
namespace network {
namespace gossip {

LegacyContactInfo::LegacyContactInfo()
    : wallclock(0), outset(0), shred_version(0) {}

LegacyContactInfo::LegacyContactInfo(const PublicKey &pk)
    : pubkey(pk), wallclock(timestamp()), outset(timestamp()),
      shred_version(0) {}

LegacyContactInfo LegacyContactInfo::from_contact_info(const ContactInfo &ci) {
  LegacyContactInfo legacy(ci.pubkey);
  legacy.wallclock = ci.wallclock;
  legacy.outset = ci.outset;
  legacy.shred_version = ci.shred_version;
  legacy.version = ci.version;
  
  // Map modern addresses to legacy format
  if (ci.addrs.size() > 0) legacy.gossip_addr = ci.addrs[0];
  if (ci.addrs.size() > 1) legacy.tpu_addr = ci.addrs[1];
  if (ci.addrs.size() > 2) legacy.tpu_forwards_addr = ci.addrs[2];
  if (ci.addrs.size() > 3) legacy.tvu_addr = ci.addrs[3];
  if (ci.addrs.size() > 4) legacy.tvu_forwards_addr = ci.addrs[4];
  if (ci.addrs.size() > 5) legacy.repair_addr = ci.addrs[5];
  if (ci.addrs.size() > 6) legacy.serve_repair_addr = ci.addrs[6];
  
  return legacy;
}

ContactInfo LegacyContactInfo::to_contact_info() const {
  ContactInfo ci(pubkey);
  ci.wallclock = wallclock;
  ci.outset = outset;
  ci.shred_version = shred_version;
  ci.version = version;
  
  // Map legacy addresses to modern format
  if (!gossip_addr.empty()) ci.addrs.push_back(gossip_addr);
  if (!tpu_addr.empty()) ci.addrs.push_back(tpu_addr);
  if (!tpu_forwards_addr.empty()) ci.addrs.push_back(tpu_forwards_addr);
  if (!tvu_addr.empty()) ci.addrs.push_back(tvu_addr);
  if (!tvu_forwards_addr.empty()) ci.addrs.push_back(tvu_forwards_addr);
  if (!repair_addr.empty()) ci.addrs.push_back(repair_addr);
  if (!serve_repair_addr.empty()) ci.addrs.push_back(serve_repair_addr);
  
  return ci;
}

bool LegacyContactInfo::is_compatible(const std::string &other_version) const {
  return LegacyVersion::is_compatible(version, other_version);
}

std::vector<uint8_t> LegacyContactInfo::serialize() const {
  std::vector<uint8_t> buf;
  
  // Simple serialization for legacy format
  // In production, would match exact wire format
  Serializer::write_bytes(buf, pubkey);
  Serializer::write_u64(buf, wallclock);
  Serializer::write_u64(buf, outset);
  Serializer::write_u16(buf, shred_version);
  Serializer::write_string(buf, version);
  Serializer::write_string(buf, gossip_addr);
  Serializer::write_string(buf, tpu_addr);
  Serializer::write_string(buf, tpu_forwards_addr);
  Serializer::write_string(buf, tvu_addr);
  Serializer::write_string(buf, tvu_forwards_addr);
  Serializer::write_string(buf, repair_addr);
  Serializer::write_string(buf, serve_repair_addr);
  
  return buf;
}

Result<LegacyContactInfo> LegacyContactInfo::deserialize(
    const std::vector<uint8_t> &data) {
  // Simplified deserialization
  // In production, would parse exact wire format
  LegacyContactInfo legacy;
  return Result<LegacyContactInfo>(legacy);
}

Result<std::tuple<uint16_t, uint16_t, uint16_t>>
LegacyVersion::parse_version(const std::string &version) {
  // Parse version string like "1.14.18"
  std::istringstream iss(version);
  uint16_t major = 0, minor = 0, patch = 0;
  char dot;
  
  if (iss >> major >> dot >> minor >> dot >> patch) {
    return Result<std::tuple<uint16_t, uint16_t, uint16_t>>(
        std::make_tuple(major, minor, patch));
  }
  
  return Result<std::tuple<uint16_t, uint16_t, uint16_t>>(
      std::string("Invalid version format"));
}

bool LegacyVersion::is_compatible(const std::string &v1, 
                                  const std::string &v2) {
  auto ver1 = parse_version(v1);
  auto ver2 = parse_version(v2);
  
  if (!ver1.is_ok() || !ver2.is_ok()) {
    return false;
  }
  
  auto [major1, minor1, patch1] = ver1.value();
  auto [major2, minor2, patch2] = ver2.value();
  
  // Compatible if major versions match
  return major1 == major2;
}

std::string LegacyVersion::get_minimum_version() {
  return "1.14.0";
}

std::string LegacyVersion::get_current_version() {
  return "1.18.0";
}

} // namespace gossip
} // namespace network
} // namespace slonana
