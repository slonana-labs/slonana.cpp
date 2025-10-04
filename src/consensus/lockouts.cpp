/**
 * @file lockouts.cpp
 * @brief Implements the logic for the Lockout and LockoutManager classes.
 */
#include "consensus/lockouts.h"
#include <algorithm>
#include <iostream>

namespace slonana {
namespace consensus {

/**
 * @brief Serializes the Lockout object into a byte vector.
 * @return A std::vector<uint8_t> containing the serialized data.
 */
std::vector<uint8_t> Lockout::serialize() const {
  std::vector<uint8_t> result;
  result.reserve(12); // 8 bytes for slot, 4 for confirmation_count
  for (int i = 0; i < 8; ++i) {
    result.push_back(static_cast<uint8_t>((slot >> (i * 8)) & 0xFF));
  }
  for (int i = 0; i < 4; ++i) {
    result.push_back(static_cast<uint8_t>((confirmation_count >> (i * 8)) & 0xFF));
  }
  return result;
}

/**
 * @brief Deserializes a Lockout object from a byte vector.
 * @param data The byte vector containing the serialized data.
 * @param offset The offset in the data vector to start reading from.
 * @return The number of bytes consumed, or 0 on error.
 */
size_t Lockout::deserialize(const std::vector<uint8_t> &data, size_t offset) {
  if (data.size() < offset + 12) return 0;
  slot = 0;
  for (int i = 0; i < 8; ++i) {
    slot |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
  }
  offset += 8;
  confirmation_count = 0;
  for (int i = 0; i < 4; ++i) {
    confirmation_count |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
  }
  return 12;
}

/**
 * @brief Adds a new lockout or updates an existing one for the same slot.
 * @param lockout The Lockout to add or update.
 */
void LockoutManager::add_lockout(const Lockout &lockout) {
  auto it = std::find_if(lockouts_.begin(), lockouts_.end(),
                         [&](const Lockout &existing) { return existing.slot == lockout.slot; });
  if (it != lockouts_.end()) {
    *it = lockout;
  } else {
    lockouts_.push_back(lockout);
    std::sort(lockouts_.begin(), lockouts_.end());
  }
}

/**
 * @brief Removes all expired lockouts relative to the given slot.
 * @param current_slot The current slot number.
 * @return The number of lockouts removed.
 */
size_t LockoutManager::remove_expired_lockouts(uint64_t current_slot) {
  size_t initial_size = lockouts_.size();
  lockouts_.erase(std::remove_if(lockouts_.begin(), lockouts_.end(),
                                 [current_slot](const Lockout &l) { return l.is_expired_at_slot(current_slot); }),
                  lockouts_.end());
  return initial_size - lockouts_.size();
}

/**
 * @brief Checks if a slot is locked out by any existing lockout.
 * @param slot The slot to check.
 * @return True if the slot is locked out, false otherwise.
 */
bool LockoutManager::is_slot_locked_out(uint64_t slot) const {
  for (const auto &lockout : lockouts_) {
    if (lockout.is_locked_out_at_slot(slot)) return true;
  }
  return false;
}

/**
 * @brief Gets a list of all lockouts that are currently active.
 * @param current_slot The current slot to check against.
 * @return A vector of active Lockout objects.
 */
std::vector<Lockout> LockoutManager::get_active_lockouts(uint64_t current_slot) const {
  std::vector<Lockout> active_lockouts;
  for (const auto &lockout : lockouts_) {
    if (!lockout.is_expired_at_slot(current_slot)) {
      active_lockouts.push_back(lockout);
    }
  }
  return active_lockouts;
}

/**
 * @brief Retrieves a pointer to the lockout for a specific slot, if it exists.
 * @param slot The slot to search for.
 * @return A const pointer to the Lockout, or nullptr if not found.
 */
const Lockout *LockoutManager::get_lockout_for_slot(uint64_t slot) const {
  auto it = std::find_if(lockouts_.begin(), lockouts_.end(),
                       [slot](const Lockout &l) { return l.slot == slot; });
  return (it != lockouts_.end()) ? &(*it) : nullptr;
}

/**
 * @brief Updates the confirmation count for an existing lockout.
 * @param slot The slot of the lockout to update.
 * @param new_count The new confirmation count.
 * @return True if the lockout was found and updated, false otherwise.
 */
bool LockoutManager::update_confirmation_count(uint64_t slot, uint32_t new_count) {
  auto it = std::find_if(lockouts_.begin(), lockouts_.end(),
                       [slot](const Lockout &l) { return l.slot == slot; });
  if (it != lockouts_.end()) {
    it->confirmation_count = new_count;
    return true;
  }
  return false;
}

/**
 * @brief Serializes the entire state of the LockoutManager.
 * @return A byte vector containing the serialized data.
 */
std::vector<uint8_t> LockoutManager::serialize() const {
  std::vector<uint8_t> result;
  uint32_t count = static_cast<uint32_t>(lockouts_.size());
  for (int i = 0; i < 4; ++i) {
    result.push_back(static_cast<uint8_t>((count >> (i * 8)) & 0xFF));
  }
  for (const auto &lockout : lockouts_) {
    auto lockout_data = lockout.serialize();
    result.insert(result.end(), lockout_data.begin(), lockout_data.end());
  }
  return result;
}

/**
 * @brief Deserializes data into the LockoutManager, replacing its current state.
 * @param data The byte vector to deserialize.
 * @return True if deserialization was successful, false on error.
 */
bool LockoutManager::deserialize(const std::vector<uint8_t> &data) {
  if (data.size() < 4) return false;
  size_t offset = 0;
  uint32_t count = 0;
  for (int i = 0; i < 4; ++i) {
    count |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
  }
  offset += 4;
  lockouts_.clear();
  lockouts_.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    Lockout lockout;
    size_t consumed = lockout.deserialize(data, offset);
    if (consumed == 0) return false;
    lockouts_.push_back(lockout);
    offset += consumed;
  }
  return true;
}

// Utility functions
namespace lockout_utils {

/**
 * @brief Calculates the maximum lockout period for a given number of confirmations.
 * @param confirmation_count The number of confirmations.
 * @return The lockout period in slots.
 */
uint64_t calculate_max_lockout_distance(uint32_t confirmation_count) {
  static constexpr uint64_t TOWER_BFT_MAX_LOCKOUT = 1ULL << 32;
  return std::min(static_cast<uint64_t>(1ULL << confirmation_count), TOWER_BFT_MAX_LOCKOUT);
}

/**
 * @brief Checks if voting on `slot2` would violate the lockout from `lockout1`.
 * @param slot1 The slot of the existing lockout.
 * @param lockout1 The existing lockout.
 * @param slot2 The slot of the potential new vote.
 * @return True if a conflict exists, false otherwise.
 */
bool slots_conflict(uint64_t slot1, const Lockout &lockout1, uint64_t slot2) {
  uint64_t lockout_end = slot1 + lockout1.lockout_period();
  return slot2 > slot1 && slot2 <= lockout_end;
}

/**
 * @brief Finds the optimal confirmation count for a new vote to maximize lockout
 * without violating existing commitments.
 * @param current_slot The current slot number.
 * @param target_slot The slot being voted on.
 * @param existing_lockouts The voter's current list of lockouts.
 * @return The highest possible confirmation count for the new vote.
 */
uint32_t
find_optimal_confirmation_count(uint64_t current_slot, uint64_t target_slot,
                                const std::vector<Lockout> &existing_lockouts) {
  uint32_t optimal_count = 0;
  for (const auto &lockout : existing_lockouts) {
    if (lockout.slot < target_slot) {
      uint64_t distance = target_slot - lockout.slot;
      uint32_t required_count = 0;
      while ((1ULL << required_count) < distance && required_count < 32) {
        required_count++;
      }
      optimal_count = std::max(optimal_count, required_count);
    }
  }
  return optimal_count;
}

/**
 * @brief Validates that a set of lockouts is internally consistent.
 * @details Checks that lockouts are sorted and that none of them conflict with each other.
 * @param lockouts The vector of lockouts to validate.
 * @return True if the set is valid, false otherwise.
 */
bool validate_lockouts(const std::vector<Lockout> &lockouts) {
  for (size_t i = 1; i < lockouts.size(); ++i) {
    if (lockouts[i].slot <= lockouts[i - 1].slot) return false;
  }
  for (size_t i = 0; i < lockouts.size(); ++i) {
    for (size_t j = i + 1; j < lockouts.size(); ++j) {
      if (slots_conflict(lockouts[i].slot, lockouts[i], lockouts[j].slot)) {
        return false;
      }
    }
  }
  return true;
}

} // namespace lockout_utils

} // namespace consensus
} // namespace slonana