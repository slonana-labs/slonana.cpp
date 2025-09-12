#pragma once

#include "common/types.h"
#include "consensus/lockouts.h"
#include <chrono>
#include <memory>
#include <functional>
#include <mutex>
#include <vector>
namespace slonana {
namespace consensus {

using namespace slonana::common;

/**
 * Tower slot representing a vote in the Tower BFT algorithm
 */
struct TowerSlot {
  uint64_t slot;
  uint64_t confirmation_count;
  std::chrono::steady_clock::time_point timestamp;

  TowerSlot() = default;
  TowerSlot(uint64_t slot_num, uint64_t conf_count = 0)
      : slot(slot_num), confirmation_count(conf_count),
        timestamp(std::chrono::steady_clock::now()) {}
};

/**
 * Tower BFT consensus algorithm implementation compatible with Agave
 */
class Tower {
private:
  mutable std::mutex tower_mutex_;
  std::vector<TowerSlot> tower_slots_;
  uint64_t root_slot_;
  uint64_t last_vote_slot_;

  // Tower BFT constants (match Agave)
  static constexpr uint64_t TOWER_BFT_MAX_LOCKOUT = 1ULL << 32;
  static constexpr size_t TOWER_BFT_THRESHOLD = 32;
  static constexpr size_t MAX_TOWER_HEIGHT = 32;

public:
  Tower();
  explicit Tower(uint64_t root_slot);

  /**
   * Check if we can vote on a specific slot based on Tower BFT rules
   * @param slot Slot number to check
   * @return true if voting is allowed
   */
  bool can_vote_on_slot(uint64_t slot) const;

  /**
   * Calculate the lockout period for a given confirmation count
   * @param confirmation_count Number of confirmations
   * @return lockout period in ticks
   */
  uint64_t calculate_lockout_period(size_t confirmation_count) const;

  /**
   * Check if a slot is locked out (cannot be voted on)
   * @param slot Slot to check
   * @return true if slot is locked out
   */
  bool is_slot_locked_out(uint64_t slot) const;

  /**
   * Record a vote on a slot, updating the tower
   * @param slot Slot being voted on
   */
  void record_vote(uint64_t slot);

  /**
   * Get the vote threshold for a slot
   * @param slot Slot to get threshold for
   * @return vote threshold
   */
  uint64_t get_vote_threshold(uint64_t slot) const;

  /**
   * Get the current tower height
   * @return number of slots in tower
   */
  size_t get_tower_height() const;

  /**
   * Get the root slot
   * @return root slot number
   */
  uint64_t get_root_slot() const;

  /**
   * Get the last voted slot
   * @return last voted slot number
   */
  uint64_t get_last_vote_slot() const;

  /**
   * Get all tower slots
   * @return vector of tower slots
   */
  std::vector<TowerSlot> get_tower_slots() const;

  /**
   * Check if the tower is valid (no conflicting votes)
   * @return true if tower is valid
   */
  bool is_valid() const;

  /**
   * Reset the tower to a new root slot
   * @param new_root_slot New root slot
   */
  void reset_to_root(uint64_t new_root_slot);

  /**
   * Get lockout period for a specific slot in the tower
   * @param slot Slot to get lockout for
   * @return lockout period, 0 if slot not in tower
   */
  uint64_t get_slot_lockout(uint64_t slot) const;

  /**
   * Check if we can switch to a different fork
   * @param slot Slot on the new fork
   * @return true if fork switch is allowed
   */
  bool can_switch_to_fork(uint64_t slot) const;

  /**
   * Update confirmation count for a slot
   * @param slot Slot to update
   * @param new_count New confirmation count
   */
  void update_confirmation_count(uint64_t slot, uint64_t new_count);

  /**
   * Serialize tower state
   * @return serialized tower data
   */
  std::vector<uint8_t> serialize() const;

  /**
   * Deserialize tower state
   * @param data Serialized tower data
   * @return true if deserialization successful
   */
  bool deserialize(const std::vector<uint8_t> &data);
};

/**
 * Vote State Management compatible with Agave
 */
struct VoteState {
  std::vector<Lockout> votes;
  uint64_t root_slot;
  std::vector<uint8_t> authorized_voter; // 32-byte pubkey
  std::vector<uint8_t> node_pubkey;      // 32-byte pubkey
  uint64_t commission;

  VoteState();
  explicit VoteState(const std::vector<uint8_t> &voter_pubkey,
                     const std::vector<uint8_t> &node_pubkey);

  /**
   * Check if a vote on the given slot is valid
   * @param slot Slot to validate
   * @return true if vote is valid
   */
  bool is_valid_vote(uint64_t slot) const;

  /**
   * Get the last voted slot
   * @return last voted slot number
   */
  uint64_t last_voted_slot() const;

  /**
   * Process a vote on a slot
   * @param slot Slot being voted on
   * @param timestamp Vote timestamp
   */
  void process_vote(uint64_t slot, uint64_t timestamp);

  /**
   * Get all slots currently in lockout
   * @param current_slot Current slot number
   * @return vector of locked out slots
   */
  std::vector<uint64_t> slots_in_lockout(uint64_t current_slot) const;

  /**
   * Update the root slot, removing older votes
   * @param new_root_slot New root slot
   */
  void update_root_slot(uint64_t new_root_slot);

  /**
   * Serialize vote state
   * @return serialized vote state data
   */
  std::vector<uint8_t> serialize() const;

  /**
   * Deserialize vote state
   * @param data Serialized vote state data
   * @return true if deserialization successful
   */
  bool deserialize(const std::vector<uint8_t> &data);
};

/**
 * Tower BFT Manager integrating with existing consensus
 */
class TowerBftManager {
private:
  std::unique_ptr<Tower> tower_;
  std::unique_ptr<VoteState> vote_state_;
  mutable std::mutex manager_mutex_;

  // Integration with existing consensus
  std::function<void(uint64_t slot, bool can_vote)> vote_callback_;

public:
  explicit TowerBftManager(uint64_t initial_root_slot = 0);
  ~TowerBftManager() = default;

  /**
   * Initialize the Tower BFT manager
   * @param voter_pubkey Authorized voter public key
   * @param node_pubkey Node public key
   * @return true if initialization successful
   */
  bool initialize(const std::vector<uint8_t> &voter_pubkey,
                  const std::vector<uint8_t> &node_pubkey);

  /**
   * Process a new slot for voting consideration
   * @param slot Slot number
   * @param parent_slot Parent slot number
   * @return true if voting is recommended
   */
  bool process_slot(uint64_t slot, uint64_t parent_slot);

  /**
   * Cast a vote on a slot
   * @param slot Slot to vote on
   * @return true if vote was cast
   */
  bool cast_vote(uint64_t slot);

  /**
   * Set callback for vote decisions
   * @param callback Function to call when vote decision is made
   */
  void
  set_vote_callback(std::function<void(uint64_t slot, bool can_vote)> callback);

  /**
   * Get current tower statistics
   */
  struct TowerStats {
    size_t tower_height;
    uint64_t root_slot;
    uint64_t last_vote_slot;
    size_t lockout_count;
  };

  TowerStats get_stats() const;

  /**
   * Get the underlying tower (for testing/debugging)
   * @return const reference to tower
   */
  const Tower &get_tower() const;

  /**
   * Get the vote state (for testing/debugging)
   * @return const reference to vote state
   */
  const VoteState &get_vote_state() const;
};

} // namespace consensus
} // namespace slonana