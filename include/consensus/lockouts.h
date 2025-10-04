/**
 * @file lockouts.h
 * @brief Defines the data structures and management classes for vote lockouts in the consensus algorithm.
 *
 * This file contains the `Lockout` struct, which represents a validator's commitment
 * to a particular fork for a certain duration, and the `LockoutManager` class,
 * which manages a collection of these lockouts. This mechanism is a key part of
 * preventing validators from voting for conflicting forks and ensuring chain safety.
 */
#pragma once

#include "common/types.h"
#include <chrono>
#include <vector>

namespace slonana {
namespace consensus {

using namespace slonana::common;

/**
 * @brief Represents a single vote lockout, compatible with Agave's implementation.
 * @details A lockout indicates that a validator has voted for a block in a
 * certain slot and is "locked" into that fork for an exponentially increasing
 * period of time based on the number of confirmations.
 */
struct Lockout {
  /// @brief The slot number of the block that was voted on.
  uint64_t slot;
  /// @brief The number of confirmations this vote has received. Determines the lockout period.
  uint32_t confirmation_count;

  Lockout() = default;
  Lockout(uint64_t slot_num, uint32_t conf_count = 0)
      : slot(slot_num), confirmation_count(conf_count) {}

  /**
   * @brief Calculates the lockout period for this vote in terms of slots.
   * @details The lockout period doubles for each confirmation, i.e., 2^confirmation_count.
   * This is compatible with Agave's Tower BFT implementation. The result is capped at 2^32.
   * @return The lockout period in number of slots.
   */
  uint64_t lockout_period() const {
    static constexpr uint64_t TOWER_BFT_MAX_LOCKOUT = 1ULL << 32;
    const uint32_t cc = std::min<uint32_t>(confirmation_count, 32u);
    const uint64_t period = (uint64_t{1} << cc);
    return std::min(period, TOWER_BFT_MAX_LOCKOUT);
  }

  /**
   * @brief Checks if this lockout prevents voting on a different slot.
   * @param other_slot The slot number to check against this lockout.
   * @return True if `other_slot` is within the lockout period of this vote, false otherwise.
   */
  bool is_locked_out_at_slot(uint64_t other_slot) const {
    return other_slot > slot && other_slot <= slot + lockout_period();
  }

  /**
   * @brief Checks if this lockout has expired relative to the current slot.
   * @param current_slot The current slot number of the chain.
   * @return True if the lockout period has passed.
   */
  bool is_expired_at_slot(uint64_t current_slot) const {
    return current_slot >= slot + lockout_period();
  }

  /**
   * @brief Gets the slot number at which this lockout expires.
   * @return The expiration slot number.
   */
  uint64_t expiration_slot() const { return slot + lockout_period(); }

  /**
   * @brief Serializes the Lockout object into a byte vector.
   * @return A std::vector<uint8_t> containing the serialized data.
   */
  std::vector<uint8_t> serialize() const;

  /**
   * @brief Deserializes a Lockout object from a byte vector.
   * @param data The byte vector containing the serialized data.
   * @param offset The offset in the data vector to start reading from.
   * @return The number of bytes consumed, or 0 on error.
   */
  size_t deserialize(const std::vector<uint8_t> &data, size_t offset = 0);

  /**
   * @brief Compares two Lockout objects based on their slot number.
   * @param other The other Lockout object to compare against.
   * @return True if this lockout's slot is less than the other's.
   */
  bool operator<(const Lockout &other) const { return slot < other.slot; }

  /**
   * @brief Checks for equality between two Lockout objects.
   * @param other The other Lockout object to compare against.
   * @return True if both slot and confirmation_count are equal.
   */
  bool operator==(const Lockout &other) const {
    return slot == other.slot && confirmation_count == other.confirmation_count;
  }
};

/**
 * @brief Manages a collection of vote lockouts for a validator.
 */
class LockoutManager {
private:
  std::vector<Lockout> lockouts_;

public:
  /**
   * @brief Adds a new lockout to the manager.
   * @param lockout The Lockout to add.
   */
  void add_lockout(const Lockout &lockout);

  /**
   * @brief Removes all lockouts that have expired as of the given slot.
   * @param current_slot The current slot number to check for expiration against.
   * @return The number of lockouts that were removed.
   */
  size_t remove_expired_lockouts(uint64_t current_slot);

  /**
   * @brief Checks if a given slot is locked out by any of the managed lockouts.
   * @param slot The slot number to check.
   * @return True if the slot is locked out, false otherwise.
   */
  bool is_slot_locked_out(uint64_t slot) const;

  /**
   * @brief Gets all lockouts that are still active at a given slot.
   * @param current_slot The current slot number.
   * @return A vector of active Lockout objects.
   */
  std::vector<Lockout> get_active_lockouts(uint64_t current_slot) const;

  /**
   * @brief Finds the lockout corresponding to a specific slot.
   * @param slot The slot to find the lockout for.
   * @return A const pointer to the Lockout if found, otherwise nullptr.
   */
  const Lockout *get_lockout_for_slot(uint64_t slot) const;

  /**
   * @brief Updates the confirmation count for a lockout on a specific slot.
   * @param slot The slot of the lockout to update.
   * @param new_count The new confirmation count.
   * @return True if the lockout was found and updated, false otherwise.
   */
  bool update_confirmation_count(uint64_t slot, uint32_t new_count);

  /**
   * @brief Gets a constant reference to the internal vector of all lockouts.
   * @return A const reference to the lockouts vector.
   */
  const std::vector<Lockout> &get_lockouts() const { return lockouts_; }

  /**
   * @brief Removes all lockouts from the manager.
   */
  void clear() { lockouts_.clear(); }

  /**
   * @brief Gets the total number of lockouts being managed.
   * @return The number of lockouts.
   */
  size_t size() const { return lockouts_.size(); }

  /**
   * @brief Checks if the manager contains any lockouts.
   * @return True if there are no lockouts, false otherwise.
   */
  bool empty() const { return lockouts_.empty(); }

  /**
   * @brief Serializes all managed lockouts into a single byte vector.
   * @return A std::vector<uint8_t> containing the serialized data.
   */
  std::vector<uint8_t> serialize() const;

  /**
   * @brief Deserializes and replaces the current lockouts from a byte vector.
   * @param data The byte vector containing the serialized data.
   * @return True if deserialization was successful, false otherwise.
   */
  bool deserialize(const std::vector<uint8_t> &data);
};

/**
 * @brief A namespace for utility functions related to lockout calculations.
 */
namespace lockout_utils {
/**
 * @brief Calculates the maximum lockout distance for a given confirmation count.
 * @param confirmation_count The number of confirmations.
 * @return The maximum lockout period in slots.
 */
uint64_t calculate_max_lockout_distance(uint32_t confirmation_count);

/**
 * @brief Checks if two slots would conflict based on lockout rules.
 * @param slot1 The first slot.
 * @param lockout1 The lockout associated with the first slot.
 * @param slot2 The second slot to check for conflict.
 * @return True if voting on `slot2` would be forbidden by `lockout1`.
 */
bool slots_conflict(uint64_t slot1, const Lockout &lockout1, uint64_t slot2);

/**
 * @brief Finds the optimal confirmation count for a new vote.
 * @details This can be used to maximize lockout duration without violating
 * existing lockout commitments.
 * @param current_slot The current slot number.
 * @param target_slot The slot on which a new vote is being considered.
 * @param existing_lockouts The voter's current set of lockouts.
 * @return The optimal confirmation count to use for the new vote.
 */
uint32_t
find_optimal_confirmation_count(uint64_t current_slot, uint64_t target_slot,
                                const std::vector<Lockout> &existing_lockouts);

/**
 * @brief Validates a set of lockouts for internal consistency.
 * @details Checks for violations, such as overlapping lockouts that should
 * have been pruned.
 * @param lockouts The vector of lockouts to validate.
 * @return True if the set of lockouts is consistent and valid.
 */
bool validate_lockouts(const std::vector<Lockout> &lockouts);
} // namespace lockout_utils

} // namespace consensus
} // namespace slonana