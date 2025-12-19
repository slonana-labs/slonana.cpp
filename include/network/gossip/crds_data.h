#pragma once

#include "common/types.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace slonana {
namespace network {
namespace gossip {

using namespace slonana::common;

// Constants from Agave
constexpr uint64_t MAX_WALLCLOCK = 1'000'000'000'000'000;
constexpr uint64_t MAX_SLOT = 1'000'000'000'000'000;
constexpr uint8_t MAX_VOTES = 12;
constexpr uint8_t MAX_EPOCH_SLOTS = 255;

using VoteIndex = uint8_t;
using EpochSlotsIndex = uint8_t;

// Forward declarations
struct ContactInfo;
struct Vote;
struct LowestSlot;
struct EpochSlots;
struct NodeInstance;
struct SnapshotHashes;
struct RestartLastVotedForkSlots;
struct RestartHeaviestFork;

/**
 * ContactInfo - Node network addresses and metadata
 * Agave: gossip/src/contact_info.rs
 */
struct ContactInfo {
  PublicKey pubkey;
  uint64_t wallclock;
  uint64_t outset; // When node instance was first created
  uint16_t shred_version;
  std::string version;
  std::vector<std::string> addrs;    // IP addresses
  std::vector<uint16_t> socket_tags; // Socket tags (gossip, rpc, tpu, etc)

  ContactInfo();
  explicit ContactInfo(const PublicKey &pk);

  uint64_t get_wallclock() const { return wallclock; }
  const PublicKey &get_pubkey() const { return pubkey; }
  bool is_valid() const;
};

/**
 * Vote - Validator vote information
 * Agave: gossip/src/crds_data.rs (Vote struct)
 */
struct Vote {
  PublicKey from;
  std::vector<uint64_t> slots;
  Hash vote_hash;
  uint64_t wallclock;
  uint64_t vote_timestamp;

  Vote();
  explicit Vote(const PublicKey &from_pubkey);

  uint64_t get_wallclock() const { return wallclock; }
  const PublicKey &get_pubkey() const { return from; }
};

/**
 * LowestSlot - Lowest slot information from a node
 * Agave: gossip/src/crds_data.rs (LowestSlot struct)
 */
struct LowestSlot {
  PublicKey from;
  uint64_t lowest;
  uint64_t wallclock;

  LowestSlot();
  explicit LowestSlot(const PublicKey &from_pubkey);

  uint64_t get_wallclock() const { return wallclock; }
  const PublicKey &get_pubkey() const { return from; }
};

/**
 * EpochSlots - Slots in an epoch
 * Agave: gossip/src/epoch_slots.rs
 */
struct EpochSlots {
  PublicKey from;
  std::vector<uint64_t> slots;
  uint64_t wallclock;

  EpochSlots();
  explicit EpochSlots(const PublicKey &from_pubkey);

  uint64_t get_wallclock() const { return wallclock; }
  const PublicKey &get_pubkey() const { return from; }
};

/**
 * NodeInstance - Node instance identifier
 * Agave: gossip/src/crds_data.rs (NodeInstance struct)
 */
struct NodeInstance {
  PublicKey from;
  uint64_t instance_timestamp;
  uint64_t wallclock;

  NodeInstance();
  explicit NodeInstance(const PublicKey &from_pubkey);

  uint64_t get_wallclock() const { return wallclock; }
  const PublicKey &get_pubkey() const { return from; }
};

/**
 * SnapshotHashes - Snapshot hash information
 * Agave: gossip/src/crds_data.rs (SnapshotHashes struct)
 */
struct SnapshotHashes {
  PublicKey from;
  std::vector<std::pair<uint64_t, Hash>> hashes; // (slot, hash) pairs
  uint64_t wallclock;

  SnapshotHashes();
  explicit SnapshotHashes(const PublicKey &from_pubkey);

  uint64_t get_wallclock() const { return wallclock; }
  const PublicKey &get_pubkey() const { return from; }
};

/**
 * RestartLastVotedForkSlots - Restart last voted fork slots
 * Agave: gossip/src/restart_crds_values.rs
 */
struct RestartLastVotedForkSlots {
  PublicKey from;
  std::vector<uint64_t> slots;
  Hash hash;
  uint64_t wallclock;

  RestartLastVotedForkSlots();
  explicit RestartLastVotedForkSlots(const PublicKey &from_pubkey);

  uint64_t get_wallclock() const { return wallclock; }
  const PublicKey &get_pubkey() const { return from; }
};

/**
 * RestartHeaviestFork - Restart heaviest fork information
 * Agave: gossip/src/restart_crds_values.rs
 */
struct RestartHeaviestFork {
  PublicKey from;
  uint64_t slot;
  Hash hash;
  uint64_t wallclock;

  RestartHeaviestFork();
  explicit RestartHeaviestFork(const PublicKey &from_pubkey);

  uint64_t get_wallclock() const { return wallclock; }
  const PublicKey &get_pubkey() const { return from; }
};

/**
 * CrdsData - All possible types of CRDS data
 * Agave: gossip/src/crds_data.rs (CrdsData enum)
 */
using CrdsData = std::variant<ContactInfo, Vote, LowestSlot, EpochSlots,
                               NodeInstance, SnapshotHashes,
                               RestartLastVotedForkSlots, RestartHeaviestFork>;

// Helper functions for CrdsData
uint64_t get_wallclock(const CrdsData &data);
PublicKey get_pubkey(const CrdsData &data);
bool is_deprecated(const CrdsData &data);

// Utility to get current timestamp
inline uint64_t timestamp() {
  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(duration)
      .count();
}

} // namespace gossip
} // namespace network
} // namespace slonana
