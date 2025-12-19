#pragma once

#include "crds_value.h"
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace slonana {
namespace network {
namespace gossip {

/**
 * GossipRoute - How a CRDS value was received
 * Agave: gossip/src/crds.rs (GossipRoute enum)
 */
enum class GossipRoute {
  LocalMessage,  // Generated locally
  PullRequest,   // Received via pull request
  PullResponse,  // Received via pull response
  PushMessage    // Received via push message
};

/**
 * CrdsError - Errors that can occur during CRDS operations
 */
enum class CrdsError {
  InsertFailed,
  BadSignature,
  DuplicateValue,
  OutdatedValue,
  InvalidData
};

/**
 * Crds - Conflict-free Replicated Data Store
 * Agave: gossip/src/crds.rs (Crds struct)
 *
 * This is the main data structure for the gossip protocol.
 * It stores versioned CRDS values and handles conflict resolution.
 */
class Crds {
public:
  Crds();
  ~Crds();

  /**
   * Insert a new value into the CRDS table
   * Returns CrdsError if insertion fails
   */
  Result<bool> insert(const CrdsValue &value, uint64_t now,
                      GossipRoute route);

  /**
   * Check if a value would update an existing entry
   */
  bool upserts(const CrdsValue &value) const;

  /**
   * Get a value by its label
   */
  const VersionedCrdsValue *get(const CrdsValueLabel &label) const;

  /**
   * Get all values from a specific pubkey
   */
  std::vector<VersionedCrdsValue> get_records(const PublicKey &pubkey) const;

  /**
   * Get all entries after a given ordinal (for replication)
   */
  std::vector<VersionedCrdsValue> get_entries_after(uint64_t ordinal,
                                                     size_t limit) const;

  /**
   * Get recent contact info entries
   */
  std::vector<ContactInfo> get_contact_infos() const;

  /**
   * Get a specific contact info
   */
  const ContactInfo *get_contact_info(const PublicKey &pubkey) const;

  /**
   * Get all votes for a pubkey
   */
  std::vector<Vote> get_votes(const PublicKey &pubkey) const;

  /**
   * Remove entries older than timeout
   */
  size_t trim(uint64_t now, uint64_t timeout);

  /**
   * Get statistics
   */
  size_t len() const;
  size_t num_nodes() const;
  size_t num_votes() const;

  /**
   * Check if table contains a specific label
   */
  bool contains(const CrdsValueLabel &label) const;

  /**
   * Get all labels in the table
   */
  std::vector<CrdsValueLabel> get_labels() const;

  /**
   * Clear all entries
   */
  void clear();

private:
  // Main table: label -> versioned value
  std::unordered_map<CrdsValueLabel, VersionedCrdsValue> table_;

  // Index: pubkey -> set of labels
  std::unordered_map<PublicKey, std::unordered_set<CrdsValueLabel>> records_;

  // Ordinal counter for insertion order
  uint64_t ordinal_counter_;

  // Mutex for thread-safe operations
  mutable std::mutex mutex_;

  // Statistics
  struct Stats {
    size_t num_inserts = 0;
    size_t num_updates = 0;
    size_t num_failures = 0;
    size_t num_trims = 0;
  };
  Stats stats_;

  // Helper: Check if value overrides existing
  bool overrides(const CrdsValue &value,
                 const VersionedCrdsValue &existing) const;

  // Helper: Update indices when inserting
  void update_indices(const CrdsValueLabel &label,
                      const VersionedCrdsValue &value);

  // Helper: Remove from indices
  void remove_from_indices(const CrdsValueLabel &label,
                           const VersionedCrdsValue &value);
};

} // namespace gossip
} // namespace network
} // namespace slonana
