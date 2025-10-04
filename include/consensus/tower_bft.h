/**
 * @file tower_bft.h
 * @brief Defines the core data structures and classes for the Tower BFT consensus algorithm.
 *
 * This file provides an Agave-compatible implementation of Tower BFT, a variant of
 * Practical Byzantine Fault Tolerance (PBFT) optimized for blockchains. It includes
 * the `Tower` class, which represents the stack of votes a validator has made,
 * and the `TowerBftManager`, which orchestrates the voting and lockout logic.
 */
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
 * @brief Represents a single vote for a specific slot within the Tower BFT structure.
 */
struct TowerSlot {
  /// @brief The slot number that was voted on.
  uint64_t slot;
  /// @brief The number of confirmations this vote has, which determines its lockout period.
  uint64_t confirmation_count;
  /// @brief The timestamp when this vote was recorded.
  std::chrono::steady_clock::time_point timestamp;

  TowerSlot() = default;
  TowerSlot(uint64_t slot_num, uint64_t conf_count = 0)
      : slot(slot_num), confirmation_count(conf_count),
        timestamp(std::chrono::steady_clock::now()) {}
};

/**
 * @brief Implements the core logic of the Tower BFT consensus algorithm.
 * @details The Tower is a data structure that maintains a validator's voting history.
 * It is used to enforce lockout rules, preventing the validator from voting on
 * conflicting forks and ensuring the safety of the consensus protocol.
 */
class Tower {
private:
  mutable std::mutex tower_mutex_;
  std::vector<TowerSlot> tower_slots_;
  uint64_t root_slot_;
  uint64_t last_vote_slot_;

  static constexpr uint64_t TOWER_BFT_MAX_LOCKOUT = 1ULL << 32;
  static constexpr size_t TOWER_BFT_THRESHOLD = 32;
  static constexpr size_t MAX_TOWER_HEIGHT = 32;

public:
  Tower();
  explicit Tower(uint64_t root_slot);

  bool can_vote_on_slot(uint64_t slot) const;
  uint64_t calculate_lockout_period(size_t confirmation_count) const;
  bool is_slot_locked_out(uint64_t slot) const;
  void record_vote(uint64_t slot);
  uint64_t get_vote_threshold(uint64_t slot) const;
  size_t get_tower_height() const;
  uint64_t get_root_slot() const;
  uint64_t get_last_vote_slot() const;
  std::vector<TowerSlot> get_tower_slots() const;
  bool is_valid() const;
  void reset_to_root(uint64_t new_root_slot);
  uint64_t get_slot_lockout(uint64_t slot) const;
  bool can_switch_to_fork(uint64_t slot) const;
  void update_confirmation_count(uint64_t slot, uint64_t new_count);
  std::vector<uint8_t> serialize() const;
  bool deserialize(const std::vector<uint8_t> &data);
};

/**
 * @brief Manages the overall voting state of a validator, including lockouts.
 * @details This struct is compatible with Agave's `VoteState` and integrates the
 * `LockoutManager` to enforce voting rules.
 */
struct VoteState {
  std::vector<Lockout> votes;
  uint64_t root_slot;
  std::vector<uint8_t> authorized_voter;
  std::vector<uint8_t> node_pubkey;
  uint64_t commission;

  VoteState();
  explicit VoteState(const std::vector<uint8_t> &voter_pubkey,
                     const std::vector<uint8_t> &node_pubkey);

  bool is_valid_vote(uint64_t slot) const;
  uint64_t last_voted_slot() const;
  void process_vote(uint64_t slot, uint64_t timestamp);
  std::vector<uint64_t> slots_in_lockout(uint64_t current_slot) const;
  void update_root_slot(uint64_t new_root_slot);
  std::vector<uint8_t> serialize() const;
  bool deserialize(const std::vector<uint8_t> &data);
};

/**
 * @brief A high-level manager that integrates the Tower BFT logic with the rest
 * of the consensus mechanism.
 */
class TowerBftManager {
private:
  std::unique_ptr<Tower> tower_;
  std::unique_ptr<VoteState> vote_state_;
  mutable std::mutex manager_mutex_;
  std::function<void(uint64_t slot, bool can_vote)> vote_callback_;

public:
  explicit TowerBftManager(uint64_t initial_root_slot = 0);
  ~TowerBftManager() = default;

  bool initialize(const std::vector<uint8_t> &voter_pubkey, const std::vector<uint8_t> &node_pubkey);
  bool process_slot(uint64_t slot, uint64_t parent_slot);
  bool cast_vote(uint64_t slot);
  void set_vote_callback(std::function<void(uint64_t slot, bool can_vote)> callback);

  /**
   * @brief A collection of statistics about the current state of the Tower.
   */
  struct TowerStats {
    size_t tower_height;
    uint64_t root_slot;
    uint64_t last_vote_slot;
    size_t lockout_count;
  };

  TowerStats get_stats() const;
  const Tower &get_tower() const;
  const VoteState &get_vote_state() const;
};

} // namespace consensus
} // namespace slonana