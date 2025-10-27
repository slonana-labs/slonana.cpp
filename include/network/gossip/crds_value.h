#pragma once

#include "crds_data.h"
#include "common/types.h"
#include <cstdint>
#include <memory>
#include <string>
#include <variant>

namespace slonana {
namespace network {
namespace gossip {

/**
 * CrdsValueLabel - Label for identifying CRDS values
 * Agave: gossip/src/crds_value.rs (CrdsValueLabel enum)
 */
struct CrdsValueLabel {
  enum class Type {
    ContactInfo,
    Vote,
    LowestSlot,
    EpochSlots,
    NodeInstance,
    SnapshotHashes,
    RestartLastVotedForkSlots,
    RestartHeaviestFork
  };

  Type type;
  PublicKey pubkey;
  uint8_t index; // For indexed types like Vote, EpochSlots

  CrdsValueLabel(Type t, const PublicKey &pk, uint8_t idx = 0)
      : type(t), pubkey(pk), index(idx) {}

  bool operator==(const CrdsValueLabel &other) const {
    return type == other.type && pubkey == other.pubkey && index == other.index;
  }

  bool operator<(const CrdsValueLabel &other) const {
    if (type != other.type)
      return type < other.type;
    if (pubkey != other.pubkey)
      return pubkey < other.pubkey;
    return index < other.index;
  }
};

/**
 * CrdsValue - A signed CRDS value with signature and hash
 * Agave: gossip/src/crds_value.rs (CrdsValue struct)
 */
class CrdsValue {
public:
  CrdsValue();
  explicit CrdsValue(const CrdsData &data);
  CrdsValue(const CrdsData &data, const Signature &sig);

  // Core accessors
  const Signature &get_signature() const { return signature_; }
  const CrdsData &data() const { return data_; }
  const Hash &hash() const { return hash_; }

  // Convenience accessors
  uint64_t wallclock() const;
  PublicKey pubkey() const;
  CrdsValueLabel label() const;

  // Signing and verification
  void sign(const Signature &external_sig);
  bool verify() const;

  // Serialization size
  size_t serialized_size() const;

  // Comparison for conflict resolution
  bool overrides(const CrdsValue &other) const;

private:
  Signature signature_;
  CrdsData data_;
  Hash hash_; // SHA256 hash of [signature, data]

  void compute_hash();
};

/**
 * VersionedCrdsValue - CrdsValue with local metadata
 * Agave: gossip/src/crds.rs (VersionedCrdsValue struct)
 */
struct VersionedCrdsValue {
  uint64_t ordinal;           // Insert order index
  CrdsValue value;            // The actual value
  uint64_t local_timestamp;   // Local time when inserted/updated
  uint8_t num_push_recv;      // Number of times received via push (0 = pull)
  bool from_pull_response;    // True if from pull response

  VersionedCrdsValue();
  explicit VersionedCrdsValue(const CrdsValue &val, uint64_t ord = 0,
                              uint64_t ts = 0);

  bool operator==(const VersionedCrdsValue &other) const {
    return ordinal == other.ordinal;
  }

  bool operator<(const VersionedCrdsValue &other) const {
    return ordinal < other.ordinal;
  }
};

} // namespace gossip
} // namespace network
} // namespace slonana

// Hash function for CrdsValueLabel to use in unordered containers
namespace std {
template <> struct hash<slonana::network::gossip::CrdsValueLabel> {
  size_t
  operator()(const slonana::network::gossip::CrdsValueLabel &label) const {
    size_t h1 = hash<int>()(static_cast<int>(label.type));
    size_t h2 = 0;
    for (auto byte : label.pubkey) {
      h2 = (h2 << 8) | byte;
    }
    size_t h3 = hash<uint8_t>()(label.index);
    return h1 ^ (h2 << 1) ^ (h3 << 2);
  }
};
} // namespace std
