#include "network/gossip/crds_value.h"
#include <algorithm>
#include <cstring>

namespace slonana {
namespace network {
namespace gossip {

// CrdsValue implementations
CrdsValue::CrdsValue() : signature_(), data_(ContactInfo()), hash_() {}

CrdsValue::CrdsValue(const CrdsData &data)
    : signature_(), data_(data), hash_() {
  compute_hash();
}

CrdsValue::CrdsValue(const CrdsData &data, const Signature &sig)
    : signature_(sig), data_(data), hash_() {
  compute_hash();
}

uint64_t CrdsValue::wallclock() const { return get_wallclock(data_); }

PublicKey CrdsValue::pubkey() const { return get_pubkey(data_); }

CrdsValueLabel CrdsValue::label() const {
  PublicKey pk = pubkey();

  // Determine type and index based on variant
  if (std::holds_alternative<ContactInfo>(data_)) {
    return CrdsValueLabel(CrdsValueLabel::Type::ContactInfo, pk);
  } else if (std::holds_alternative<Vote>(data_)) {
    return CrdsValueLabel(CrdsValueLabel::Type::Vote, pk);
  } else if (std::holds_alternative<LowestSlot>(data_)) {
    return CrdsValueLabel(CrdsValueLabel::Type::LowestSlot, pk);
  } else if (std::holds_alternative<EpochSlots>(data_)) {
    return CrdsValueLabel(CrdsValueLabel::Type::EpochSlots, pk);
  } else if (std::holds_alternative<NodeInstance>(data_)) {
    return CrdsValueLabel(CrdsValueLabel::Type::NodeInstance, pk);
  } else if (std::holds_alternative<SnapshotHashes>(data_)) {
    return CrdsValueLabel(CrdsValueLabel::Type::SnapshotHashes, pk);
  } else if (std::holds_alternative<RestartLastVotedForkSlots>(data_)) {
    return CrdsValueLabel(CrdsValueLabel::Type::RestartLastVotedForkSlots, pk);
  } else if (std::holds_alternative<RestartHeaviestFork>(data_)) {
    return CrdsValueLabel(CrdsValueLabel::Type::RestartHeaviestFork, pk);
  }

  // Default to ContactInfo
  return CrdsValueLabel(CrdsValueLabel::Type::ContactInfo, pk);
}

void CrdsValue::sign(const Signature &external_sig) {
  // Store the external signature
  signature_ = external_sig;
  compute_hash();
}

bool CrdsValue::verify() const {
  // In a real implementation, this would:
  // 1. Serialize the data
  // 2. Verify the signature against the pubkey
  // For now, just check signature exists
  return !signature_.empty();
}

size_t CrdsValue::serialized_size() const {
  // Rough estimate: signature (64) + data overhead
  size_t size = 64; // Signature

  std::visit(
      [&size](const auto &value) {
        // Add size for the specific type
        // This is a simplified calculation
        size += sizeof(value);
      },
      data_);

  return size;
}

bool CrdsValue::overrides(const CrdsValue &other) const {
  // Check labels match
  if (label() != other.label()) {
    return false;
  }

  // ContactInfo has special override logic based on outset
  if (std::holds_alternative<ContactInfo>(data_) &&
      std::holds_alternative<ContactInfo>(other.data_)) {
    const auto &this_ci = std::get<ContactInfo>(data_);
    const auto &other_ci = std::get<ContactInfo>(other.data_);

    // Prefer more recent outset (node restart)
    if (this_ci.outset != other_ci.outset) {
      return this_ci.outset > other_ci.outset;
    }
  }

  // Compare wallclocks
  uint64_t this_wc = wallclock();
  uint64_t other_wc = other.wallclock();

  if (this_wc > other_wc) {
    return true;
  } else if (this_wc < other_wc) {
    return false;
  }

  // If wallclocks are equal, compare hashes for deterministic ordering
  return hash_ > other.hash_;
}

void CrdsValue::compute_hash() {
  // In a real implementation, this would compute SHA256(signature || data)
  // For now, create a simple hash from signature and wallclock
  hash_.clear();
  hash_.resize(32, 0);

  // Mix signature into hash
  for (size_t i = 0; i < std::min(signature_.size(), hash_.size()); ++i) {
    hash_[i] ^= signature_[i];
  }

  // Mix wallclock into hash
  uint64_t wc = wallclock();
  for (size_t i = 0; i < 8 && i < hash_.size(); ++i) {
    hash_[i] ^= (wc >> (i * 8)) & 0xFF;
  }
}

// VersionedCrdsValue implementations
VersionedCrdsValue::VersionedCrdsValue()
    : ordinal(0), value(), local_timestamp(0), num_push_recv(0),
      from_pull_response(false) {}

VersionedCrdsValue::VersionedCrdsValue(const CrdsValue &val, uint64_t ord,
                                       uint64_t ts)
    : ordinal(ord), value(val), local_timestamp(ts), num_push_recv(0),
      from_pull_response(false) {}

} // namespace gossip
} // namespace network
} // namespace slonana
