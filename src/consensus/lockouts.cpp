#include "consensus/lockouts.h"
#include <algorithm>
#include <iostream>

namespace slonana {
namespace consensus {

// Lockout implementation
std::vector<uint8_t> Lockout::serialize() const {
  std::vector<uint8_t> result;

  // Serialize slot (8 bytes)
  for (int i = 0; i < 8; ++i) {
    result.push_back(static_cast<uint8_t>((slot >> (i * 8)) & 0xFF));
  }

  // Serialize confirmation count (4 bytes)
  for (int i = 0; i < 4; ++i) {
    result.push_back(
        static_cast<uint8_t>((confirmation_count >> (i * 8)) & 0xFF));
  }

  return result;
}

size_t Lockout::deserialize(const std::vector<uint8_t> &data, size_t offset) {
  if (data.size() < offset + 12) { // Need 8 + 4 = 12 bytes
    return 0;
  }

  // Deserialize slot
  slot = 0;
  for (int i = 0; i < 8; ++i) {
    slot |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
  }
  offset += 8;

  // Deserialize confirmation count
  confirmation_count = 0;
  for (int i = 0; i < 4; ++i) {
    confirmation_count |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
  }

  return 12; // Consumed 12 bytes
}

// LockoutManager implementation
void LockoutManager::add_lockout(const Lockout &lockout) {
  // Check if lockout for this slot already exists
  auto it = std::find_if(lockouts_.begin(), lockouts_.end(),
                         [&lockout](const Lockout &existing) {
                           return existing.slot == lockout.slot;
                         });

  if (it != lockouts_.end()) {
    // Update existing lockout
    *it = lockout;
  } else {
    // Add new lockout
    lockouts_.push_back(lockout);

    // Keep lockouts sorted by slot
    std::sort(lockouts_.begin(), lockouts_.end());
  }
}

size_t LockoutManager::remove_expired_lockouts(uint64_t current_slot) {
  size_t initial_size = lockouts_.size();

  lockouts_.erase(std::remove_if(lockouts_.begin(), lockouts_.end(),
                                 [current_slot](const Lockout &lockout) {
                                   return lockout.is_expired_at_slot(
                                       current_slot);
                                 }),
                  lockouts_.end());

  return initial_size - lockouts_.size();
}

bool LockoutManager::is_slot_locked_out(uint64_t slot) const {
  for (const auto &lockout : lockouts_) {
    if (lockout.is_locked_out_at_slot(slot)) {
      return true;
    }
  }
  return false;
}

std::vector<Lockout>
LockoutManager::get_active_lockouts(uint64_t current_slot) const {
  std::vector<Lockout> active_lockouts;

  for (const auto &lockout : lockouts_) {
    if (!lockout.is_expired_at_slot(current_slot)) {
      active_lockouts.push_back(lockout);
    }
  }

  return active_lockouts;
}

const Lockout *LockoutManager::get_lockout_for_slot(uint64_t slot) const {
  auto it = std::find_if(
      lockouts_.begin(), lockouts_.end(),
      [slot](const Lockout &lockout) { return lockout.slot == slot; });

  return (it != lockouts_.end()) ? &(*it) : nullptr;
}

bool LockoutManager::update_confirmation_count(uint64_t slot,
                                               uint32_t new_count) {
  auto it = std::find_if(
      lockouts_.begin(), lockouts_.end(),
      [slot](const Lockout &lockout) { return lockout.slot == slot; });

  if (it != lockouts_.end()) {
    it->confirmation_count = new_count;
    return true;
  }

  return false;
}

std::vector<uint8_t> LockoutManager::serialize() const {
  std::vector<uint8_t> result;

  // Serialize count (4 bytes)
  uint32_t count = static_cast<uint32_t>(lockouts_.size());
  for (int i = 0; i < 4; ++i) {
    result.push_back(static_cast<uint8_t>((count >> (i * 8)) & 0xFF));
  }

  // Serialize each lockout
  for (const auto &lockout : lockouts_) {
    auto lockout_data = lockout.serialize();
    result.insert(result.end(), lockout_data.begin(), lockout_data.end());
  }

  return result;
}

bool LockoutManager::deserialize(const std::vector<uint8_t> &data) {
  if (data.size() < 4) {
    return false;
  }

  size_t offset = 0;

  // Deserialize count
  uint32_t count = 0;
  for (int i = 0; i < 4; ++i) {
    count |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
  }
  offset += 4;

  // Clear existing lockouts
  lockouts_.clear();
  lockouts_.reserve(count);

  // Deserialize each lockout
  for (uint32_t i = 0; i < count; ++i) {
    Lockout lockout;
    size_t consumed = lockout.deserialize(data, offset);
    if (consumed == 0) {
      return false;
    }

    lockouts_.push_back(lockout);
    offset += consumed;
  }

  return true;
}

// Utility functions
namespace lockout_utils {

uint64_t calculate_max_lockout_distance(uint32_t confirmation_count) {
  static constexpr uint64_t TOWER_BFT_MAX_LOCKOUT = 1ULL << 32;
  return std::min(static_cast<uint64_t>(1ULL << confirmation_count),
                  TOWER_BFT_MAX_LOCKOUT);
}

bool slots_conflict(uint64_t slot1, const Lockout &lockout1, uint64_t slot2) {
  // Check if slot2 falls within the lockout period of slot1
  uint64_t lockout_end = slot1 + lockout1.lockout_period();
  return slot2 > slot1 && slot2 <= lockout_end;
}

uint32_t
find_optimal_confirmation_count(uint64_t current_slot, uint64_t target_slot,
                                const std::vector<Lockout> &existing_lockouts) {

  // Start with minimum confirmation count
  uint32_t optimal_count = 0;

  // Check against existing lockouts
  for (const auto &lockout : existing_lockouts) {
    if (lockout.slot < target_slot) {
      // This lockout is before our target, calculate minimum confirmation
      // count needed to not conflict with future slots
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

bool validate_lockouts(const std::vector<Lockout> &lockouts) {
  // Check that lockouts are sorted by slot
  for (size_t i = 1; i < lockouts.size(); ++i) {
    if (lockouts[i].slot <= lockouts[i - 1].slot) {
      return false;
    }
  }

  // Check that no lockouts conflict with each other
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