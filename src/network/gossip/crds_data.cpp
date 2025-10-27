#include "network/gossip/crds_data.h"
#include <algorithm>

namespace slonana {
namespace network {
namespace gossip {

// ContactInfo implementations
ContactInfo::ContactInfo() : wallclock(0), outset(0), shred_version(0) {}

ContactInfo::ContactInfo(const PublicKey &pk)
    : pubkey(pk), wallclock(timestamp()), outset(timestamp()),
      shred_version(0) {}

bool ContactInfo::is_valid() const {
  return !pubkey.empty() && wallclock > 0 && wallclock < MAX_WALLCLOCK;
}

// Vote implementations
Vote::Vote() : wallclock(0), vote_timestamp(0) {}

Vote::Vote(const PublicKey &from_pubkey)
    : from(from_pubkey), wallclock(timestamp()), vote_timestamp(timestamp()) {}

// LowestSlot implementations
LowestSlot::LowestSlot() : lowest(0), wallclock(0) {}

LowestSlot::LowestSlot(const PublicKey &from_pubkey)
    : from(from_pubkey), lowest(0), wallclock(timestamp()) {}

// EpochSlots implementations
EpochSlots::EpochSlots() : wallclock(0) {}

EpochSlots::EpochSlots(const PublicKey &from_pubkey)
    : from(from_pubkey), wallclock(timestamp()) {}

// NodeInstance implementations
NodeInstance::NodeInstance() : instance_timestamp(0), wallclock(0) {}

NodeInstance::NodeInstance(const PublicKey &from_pubkey)
    : from(from_pubkey), instance_timestamp(timestamp()), wallclock(timestamp()) {}

// SnapshotHashes implementations
SnapshotHashes::SnapshotHashes() : wallclock(0) {}

SnapshotHashes::SnapshotHashes(const PublicKey &from_pubkey)
    : from(from_pubkey), wallclock(timestamp()) {}

// RestartLastVotedForkSlots implementations
RestartLastVotedForkSlots::RestartLastVotedForkSlots() : wallclock(0) {}

RestartLastVotedForkSlots::RestartLastVotedForkSlots(
    const PublicKey &from_pubkey)
    : from(from_pubkey), wallclock(timestamp()) {}

// RestartHeaviestFork implementations
RestartHeaviestFork::RestartHeaviestFork() : slot(0), wallclock(0) {}

RestartHeaviestFork::RestartHeaviestFork(const PublicKey &from_pubkey)
    : from(from_pubkey), slot(0), wallclock(timestamp()) {}

// Helper functions for CrdsData
uint64_t get_wallclock(const CrdsData &data) {
  return std::visit(
      [](const auto &value) -> uint64_t { return value.get_wallclock(); },
      data);
}

PublicKey get_pubkey(const CrdsData &data) {
  return std::visit([](const auto &value) -> PublicKey { return value.get_pubkey(); },
                    data);
}

bool is_deprecated(const CrdsData &data) {
  // In the current implementation, nothing is deprecated yet
  // This would be extended as the protocol evolves
  return false;
}

} // namespace gossip
} // namespace network
} // namespace slonana
