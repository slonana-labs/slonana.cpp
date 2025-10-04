/**
 * @file advanced_fork_choice.cpp
 * @brief Implements the logic for the AdvancedForkChoice class.
 *
 * This file contains the implementation of the stake-weighted fork choice rule,
 * including block and vote processing, fork management, and performance
 * optimizations like caching and garbage collection.
 */
#include "consensus/advanced_fork_choice.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <queue>
#include <sstream>
#include <unordered_map>

namespace slonana {
namespace consensus {

/**
 * @brief Constructs an AdvancedForkChoice instance.
 * @param config The configuration settings for the algorithm.
 */
AdvancedForkChoice::AdvancedForkChoice(const Configuration &config)
    : config_(config), current_head_slot_(0), current_root_slot_(0),
      last_weight_update_(std::chrono::steady_clock::now()),
      stake_aggregation_dirty_(true) {
  stats_.last_fork_switch = std::chrono::steady_clock::now();
  stats_.last_gc_run = std::chrono::steady_clock::now();
  std::cout << "AdvancedForkChoice initialized with optimistic confirmation "
               "threshold: " << config_.optimistic_confirmation_threshold << "%" << std::endl;
}

/**
 * @brief Destructor for AdvancedForkChoice.
 */
AdvancedForkChoice::~AdvancedForkChoice() = default;

/**
 * @brief Adds a new block to the fork choice model.
 * @details This method creates metadata for the new block and either extends an
 * existing fork or creates a new one. It then updates the fork weights to
 * potentially select a new canonical head.
 * @param block_hash The hash of the new block.
 * @param parent_hash The hash of the parent block.
 * @param slot The slot number of the new block.
 */
void AdvancedForkChoice::add_block(const Hash &block_hash,
                                   const Hash &parent_hash, Slot slot) {
  std::unique_lock<std::shared_mutex> lock(data_mutex_);
  auto block_meta = std::make_unique<BlockMetadata>(block_hash, parent_hash, slot);
  stats_.total_blocks++;
  blocks_[block_hash] = std::move(block_meta);

  bool fork_created = false;
  Fork *parent_fork = nullptr;
  if (auto it = block_to_fork_map_.find(parent_hash); it != block_to_fork_map_.end()) {
    parent_fork = it->second;
  }

  if (parent_fork && parent_fork->head_hash == parent_hash) {
    parent_fork->blocks.push_back(block_hash);
    parent_fork->head_hash = block_hash;
    parent_fork->head_slot = slot;
    block_to_fork_map_[block_hash] = parent_fork;
  } else {
    auto new_fork = std::make_unique<Fork>(block_hash, parent_hash, slot, parent_fork ? parent_fork->root_slot : slot);
    new_fork->blocks.push_back(block_hash);
    Fork *new_fork_ptr = new_fork.get();
    forks_[block_hash] = std::move(new_fork);
    block_to_fork_map_[block_hash] = new_fork_ptr;
    fork_created = true;
  }

  if (fork_created) stats_.active_forks++;
  add_pending_confirmation_block(block_hash);
  stake_aggregation_dirty_ = true;
  update_fork_weights();

  std::cout << "Added block " << slot << " to fork choice (active forks: " << forks_.size() << ")" << std::endl;
}

/**
 * @brief Processes a single vote from a validator.
 * @details Updates the stake weight for the voted block and its ancestors,
 * records the vote, and then triggers checks for optimistic confirmation and rooting.
 * @param vote The VoteInfo struct containing vote details.
 */
void AdvancedForkChoice::add_vote(const VoteInfo &vote) {
  {
    std::lock_guard<std::mutex> vote_lock(vote_processing_mutex_);
    std::unique_lock<std::shared_mutex> lock(data_mutex_);
    recent_votes_.push_back(vote);
    if (recent_votes_.size() > 10000) { // Limit recent votes memory usage
      recent_votes_.erase(recent_votes_.begin(), recent_votes_.begin() + (recent_votes_.size() - 10000));
    }
    validator_stakes_[vote.validator_identity] = vote.stake_weight;
    stake_aggregation_dirty_ = true;
    update_stake_aggregation_for_vote(vote);
    if (auto it = blocks_.find(vote.block_hash); it != blocks_.end()) {
      auto &block_meta = it->second;
      if (std::find(block_meta->validators_voted.begin(), block_meta->validators_voted.end(), vote.validator_identity) == block_meta->validators_voted.end()) {
        block_meta->validators_voted.push_back(vote.validator_identity);
        block_meta->stake_weight += vote.stake_weight;
      }
    }
    stats_.total_votes++;
  }

  if (config_.enable_optimistic_confirmation) process_pending_confirmations();
  if (config_.enable_aggressive_rooting) process_rooting_candidates();
  update_fork_weights();

  std::cout << "Processed vote for slot " << vote.slot << " (stake: " << vote.stake_weight << ")" << std::endl;
}

/**
 * @brief Processes a batch of votes efficiently.
 * @details This method is an optimized version of `add_vote` for handling multiple
 * votes at once, reducing lock contention.
 * @param votes A vector of VoteInfo structs.
 */
void AdvancedForkChoice::process_votes_batch(const std::vector<VoteInfo> &votes) {
  {
    std::lock_guard<std::mutex> vote_lock(vote_processing_mutex_);
    std::unique_lock<std::shared_mutex> lock(data_mutex_);
    for (const auto &vote : votes) {
      recent_votes_.push_back(vote);
      validator_stakes_[vote.validator_identity] = vote.stake_weight;
      stake_aggregation_dirty_ = true;
      update_stake_aggregation_for_vote(vote);
      if (auto it = blocks_.find(vote.block_hash); it != blocks_.end()) {
        auto &block_meta = it->second;
        if (std::find(block_meta->validators_voted.begin(), block_meta->validators_voted.end(), vote.validator_identity) == block_meta->validators_voted.end()) {
          block_meta->validators_voted.push_back(vote.validator_identity);
          block_meta->stake_weight += vote.stake_weight;
        }
      }
      stats_.total_votes++;
    }
    if (recent_votes_.size() > 10000) {
      recent_votes_.erase(recent_votes_.begin(), recent_votes_.begin() + (recent_votes_.size() - 10000));
    }
  }

  if (config_.enable_optimistic_confirmation) process_pending_confirmations();
  if (config_.enable_aggressive_rooting) process_rooting_candidates();
  update_fork_weights();

  std::cout << "Processed batch of " << votes.size() << " votes" << std::endl;
}

/**
 * @brief Gets the hash of the current canonical head of the chain.
 * @return The hash of the head block.
 */
Hash AdvancedForkChoice::get_head() const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);
  return current_head_;
}

/**
 * @brief Gets the slot of the current canonical head of the chain.
 * @return The slot number of the head block.
 */
Slot AdvancedForkChoice::get_head_slot() const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);
  return current_head_slot_;
}

/**
 * @brief Gets the hash of the last rooted (finalized) block.
 * @return The hash of the root block.
 */
Hash AdvancedForkChoice::get_root() const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);
  return current_root_;
}

/**
 * @brief Gets the slot of the last rooted (finalized) block.
 * @return The slot number of the root block.
 */
Slot AdvancedForkChoice::get_root_slot() const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);
  return current_root_slot_;
}

/**
 * @brief Retrieves a list of all currently active forks.
 * @return A vector of `Fork` structs.
 */
std::vector<Fork> AdvancedForkChoice::get_active_forks() const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);
  std::vector<Fork> active_forks;
  active_forks.reserve(forks_.size());
  for (const auto &[hash, fork] : forks_) {
    active_forks.push_back(*fork);
  }
  return active_forks;
}

/**
 * @brief Checks if one block is an ancestor of another.
 * @param potential_ancestor The hash of the block to check for ancestry.
 * @param descendant The hash of the descendant block.
 * @return True if `potential_ancestor` is an ancestor of `descendant`.
 */
bool AdvancedForkChoice::is_ancestor(const Hash &potential_ancestor, const Hash &descendant) const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);
  Hash current = descendant;
  while (!current.empty()) {
    if (current == potential_ancestor) return true;
    auto it = blocks_.find(current);
    if (it == blocks_.end()) break;
    current = it->second->parent_hash;
  }
  return false;
}

/**
 * @brief Checks if a block is considered optimistically confirmed.
 * @param block_hash The hash of the block to check.
 * @return True if the block has received enough stake-weighted votes to be
 * considered optimistically confirmed.
 */
bool AdvancedForkChoice::is_optimistically_confirmed(const Hash &block_hash) const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);
  auto it = blocks_.find(block_hash);
  if (it == blocks_.end()) return false;
  uint64_t supporting_stake = count_stake_supporting_block(block_hash);
  uint64_t threshold_stake = (config_.total_stake * config_.optimistic_confirmation_threshold) / 100;
  return supporting_stake >= threshold_stake;
}

/**
 * @brief Gets the total stake weight that has voted for a specific block and its descendants.
 * @param block_hash The hash of the block.
 * @return The cumulative stake weight.
 */
uint64_t AdvancedForkChoice::get_stake_weight(const Hash &block_hash) const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);
  auto it = blocks_.find(block_hash);
  return (it != blocks_.end()) ? it->second->stake_weight : 0;
}

/**
 * @brief Manually triggers an optimistic confirmation check for a block.
 * @param block_hash The hash of the block to check.
 * @return True if the block was successfully confirmed, false otherwise.
 */
bool AdvancedForkChoice::try_optimistic_confirmation(const Hash &block_hash) {
  std::unique_lock<std::shared_mutex> lock(data_mutex_);
  if (!check_optimistic_confirmation_conditions(block_hash)) {
    return false;
  }
  if (auto it = blocks_.find(block_hash); it != blocks_.end()) {
    it->second->is_confirmed = true;
    stats_.optimistic_confirmations++;
    std::cout << "Block optimistically confirmed: " << it->second->slot << std::endl;
    return true;
  }
  return false;
}

/**
 * @brief Updates the weights of all forks and determines the new canonical head.
 * @details This method is rate-limited to avoid excessive recalculations. It
 * finds the best fork using `find_best_fork` and switches the canonical head if
 * a better fork is found.
 */
void AdvancedForkChoice::update_fork_weights() {
  std::lock_guard<std::mutex> weights_lock(fork_weights_mutex_);
  auto now = std::chrono::steady_clock::now();
  if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_weight_update_).count() < 100) {
    return;
  }
  last_weight_update_ = now;

  Fork *best_fork = find_best_fork();
  if (best_fork && best_fork->head_hash != current_head_) {
    current_head_ = best_fork->head_hash;
    current_head_slot_ = best_fork->head_slot;
    stats_.fork_switches++;
    stats_.last_fork_switch = std::chrono::steady_clock::now();
    std::cout << "Fork switch: " << current_head_slot_ << " (weight: " << best_fork->stake_weight << ")" << std::endl;
  }
}

/**
 * @brief Finds the best fork from the current set of active forks.
 * @return A pointer to the best `Fork` object, or nullptr if no forks exist.
 */
Fork *AdvancedForkChoice::find_best_fork() const {
  Fork *best_fork = nullptr;
  uint64_t best_weight = 0;
  for (const auto &[hash, fork] : forks_) {
    uint64_t weight = calculate_fork_weight(*fork);
    if (weight > best_weight) {
      best_weight = weight;
      best_fork = fork.get();
    }
  }
  return best_fork;
}

/**
 * @brief Calculates the total weight of a given fork.
 * @details The weight is a combination of slot height, stake weight on the head
 * block, and bonuses for optimistic confirmation, rooting, and confirmation count.
 * This method uses a thread-safe cache with LRU eviction to improve performance.
 * @param fork The fork for which to calculate the weight.
 * @return The calculated weight of the fork.
 */
uint64_t AdvancedForkChoice::calculate_fork_weight(const Fork &fork) const {
  std::lock_guard<std::mutex> cache_lock(weight_cache_mutex_);
  auto now = std::chrono::steady_clock::now();
  if (auto it = weight_cache_.find(fork.head_hash); it != weight_cache_.end()) {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.second).count() < 500) {
      update_weight_cache_lru(fork.head_hash);
      return it->second.first;
    }
  }

  uint64_t weight = fork.head_slot * 1000;
  if (!fork.blocks.empty()) {
    if (auto it = blocks_.find(fork.head_hash); it != blocks_.end()) {
      weight += it->second->stake_weight;
    }
  }
  if (fork.is_optimistically_confirmed) weight += 50000;
  if (fork.is_rooted) weight += 100000;
  weight += fork.confirmation_count * 1000;

  weight_cache_[fork.head_hash] = {weight, now};
  update_weight_cache_lru(fork.head_hash);
  if (weight_cache_.size() > config_.max_cache_entries) {
    evict_weight_cache_lru();
  }
  return weight;
}

/**
 * @brief Checks if a block meets the conditions for optimistic confirmation.
 * @details This is a helper method that calculates the stake supporting a block
 * and compares it against the configured threshold. It does not acquire locks.
 * @param block_hash The hash of the block to check.
 * @return True if the conditions are met, false otherwise.
 */
bool AdvancedForkChoice::check_optimistic_confirmation_conditions(const Hash &block_hash) const {
  uint64_t supporting_stake = count_stake_supporting_block_unsafe(block_hash);
  uint64_t threshold_stake = (config_.total_stake * config_.optimistic_confirmation_threshold) / 100;
  return supporting_stake >= threshold_stake;
}

/**
 * @brief Iterates through blocks and marks them as optimistically confirmed if they meet the criteria.
 */
void AdvancedForkChoice::process_optimistic_confirmations() {
  std::unique_lock<std::shared_mutex> lock(data_mutex_);
  for (const auto &[hash, block] : blocks_) {
    if (!block->is_confirmed && check_optimistic_confirmation_conditions(hash)) {
      block->is_confirmed = true;
      stats_.optimistic_confirmations++;
      for (auto &[fork_hash, fork] : forks_) {
        if (std::find(fork->blocks.begin(), fork->blocks.end(), hash) != fork->blocks.end()) {
          fork->is_optimistically_confirmed = true;
          break;
        }
      }
    }
  }
}

/**
 * @brief Identifies and processes blocks that are candidates for rooting (finalization).
 * @details This method separates the collection of candidates (read-only) from
 * the processing (write) to minimize lock contention.
 */
void AdvancedForkChoice::process_rooting_candidates() {
  std::vector<Hash> candidates_to_root;
  {
    std::shared_lock<std::shared_mutex> lock(data_mutex_);
    for (const auto &[hash, block] : blocks_) {
      if (!block->is_confirmed && check_rooting_conditions(hash)) {
        candidates_to_root.push_back(hash);
      }
    }
  }
  for (const Hash &candidate : candidates_to_root) {
    try_root_block(candidate);
  }
}

/**
 * @brief Checks if a block meets the conditions for rooting.
 * @details This is a helper method that calculates the stake supporting a block
 * and compares it against the configured rooting threshold. It does not acquire locks.
 * @param block_hash The hash of the block to check.
 * @return True if the conditions are met, false otherwise.
 */
bool AdvancedForkChoice::check_rooting_conditions(const Hash &block_hash) const {
  uint64_t supporting_stake = count_stake_supporting_block_unsafe(block_hash);
  uint64_t threshold_stake = (config_.total_stake * config_.rooting_threshold) / 100;
  return supporting_stake >= threshold_stake;
}

/**
 * @brief Attempts to root a block, finalizing it and its ancestors.
 * @param block_hash The hash of the block to try to root.
 * @return True if the block was successfully rooted, false otherwise.
 */
bool AdvancedForkChoice::try_root_block(const Hash &block_hash) {
  auto it = blocks_.find(block_hash);
  if (it == blocks_.end()) return false;
  if (check_rooting_conditions(block_hash)) {
    current_root_ = block_hash;
    current_root_slot_ = it->second->slot;
    stats_.rooted_slots++;
    for (auto &[fork_hash, fork] : forks_) {
      if (std::find(fork->blocks.begin(), fork->blocks.end(), block_hash) != fork->blocks.end()) {
        fork->is_rooted = true;
        fork->root_hash = block_hash;
        fork->root_slot = it->second->slot;
        break;
      }
    }
    std::cout << "Block rooted: " << it->second->slot << std::endl;
    return true;
  }
  return false;
}

/**
 * @brief A thread-safe wrapper for `count_stake_supporting_block_unsafe`.
 * @param block_hash The hash of the block.
 * @return The total stake supporting the block.
 */
uint64_t AdvancedForkChoice::count_stake_supporting_block(const Hash &block_hash) const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);
  return count_stake_supporting_block_unsafe(block_hash);
}

/**
 * @brief Counts the total stake supporting a given block and its descendants.
 * @details This is the non-locking version, intended for internal use by methods
 * that already hold the necessary lock. It uses a cached stake aggregation for
 * performance.
 * @param block_hash The hash of the block.
 * @return The total stake weight.
 */
uint64_t AdvancedForkChoice::count_stake_supporting_block_unsafe(const Hash &block_hash) const {
  if (stake_aggregation_dirty_) {
    rebuild_stake_aggregation();
    stake_aggregation_dirty_ = false;
  }
  auto it = block_stake_aggregation_.find(block_hash);
  if (it != block_stake_aggregation_.end()) {
    return it->second;
  }
  uint64_t total_stake = 0;
  for (const auto &vote : recent_votes_) {
    if (vote.block_hash == block_hash || is_ancestor_unsafe(block_hash, vote.block_hash)) {
      total_stake += vote.stake_weight;
    }
  }
  return total_stake;
}

/**
 * @brief A non-locking version of `is_ancestor` for internal use.
 * @param potential_ancestor The hash of the potential ancestor block.
 * @param descendant The hash of the descendant block.
 * @return True if `potential_ancestor` is an ancestor of `descendant`.
 */
bool AdvancedForkChoice::is_ancestor_unsafe(const Hash &potential_ancestor, const Hash &descendant) const {
  Hash current = descendant;
  while (!current.empty()) {
    if (current == potential_ancestor) return true;
    auto it = blocks_.find(current);
    if (it == blocks_.end()) break;
    current = it->second->parent_hash;
  }
  return false;
}

/**
 * @brief Runs the main garbage collection process.
 * @details This method cleans up expired votes, old blocks that are not part of
 * any active fork, and forks that are too far behind the current head.
 */
void AdvancedForkChoice::garbage_collect() {
  std::unique_lock<std::shared_mutex> lock(data_mutex_);
  cleanup_expired_votes();
  prune_old_blocks();
  cleanup_old_forks();
  stats_.gc_runs++;
  stats_.last_gc_run = std::chrono::steady_clock::now();
  std::cout << "Garbage collection completed (active forks: " << forks_.size() << ", blocks: " << blocks_.size() << ")" << std::endl;
}

/**
 * @brief Removes old votes from the `recent_votes_` collection.
 */
void AdvancedForkChoice::cleanup_expired_votes() {
  auto now = std::chrono::steady_clock::now();
  auto cutoff = now - std::chrono::hours(1);
  recent_votes_.erase(std::remove_if(recent_votes_.begin(), recent_votes_.end(),
                                     [cutoff](const VoteInfo &vote) { return vote.timestamp < cutoff; }),
                      recent_votes_.end());
}

/**
 * @brief Prunes old, unreferenced blocks from the blocktree.
 */
void AdvancedForkChoice::prune_old_blocks() {
  std::unordered_set<Hash> active_blocks;
  for (const auto &[hash, fork] : forks_) {
    active_blocks.insert(fork->blocks.begin(), fork->blocks.end());
  }
  for (auto it = blocks_.begin(); it != blocks_.end();) {
    if (active_blocks.find(it->first) == active_blocks.end() && is_block_expired(*(it->second))) {
      block_to_fork_map_.erase(it->first);
      it = blocks_.erase(it);
    } else {
      ++it;
    }
  }
}

/**
 * @brief Removes forks that are too far behind the current head and are not rooted.
 */
void AdvancedForkChoice::cleanup_old_forks() {
  Slot cutoff_slot = current_head_slot_ > 1000 ? current_head_slot_ - 1000 : 0;
  for (auto it = forks_.begin(); it != forks_.end();) {
    if (it->second->head_slot < cutoff_slot && !it->second->is_rooted) {
      for (const Hash &block_hash : it->second->blocks) {
        block_to_fork_map_.erase(block_hash);
      }
      stats_.active_forks--;
      it = forks_.erase(it);
    } else {
      ++it;
    }
  }
}

/**
 * @brief Checks if a block is expired based on its arrival time.
 * @param block The block metadata to check.
 * @return True if the block is older than the expiration threshold (2 hours).
 */
bool AdvancedForkChoice::is_block_expired(const BlockMetadata &block) const {
  auto now = std::chrono::steady_clock::now();
  auto age = now - block.arrival_time;
  return age > std::chrono::hours(2);
}

AdvancedForkChoice::Statistics AdvancedForkChoice::get_statistics() const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);

  // Update dynamic statistics
  Statistics current_stats = stats_;
  current_stats.active_forks = forks_.size();

  return current_stats;
}

void AdvancedForkChoice::print_fork_tree() const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);

  std::cout << "\n=== Fork Tree ===" << std::endl;
  std::cout << "Current Head: " << current_head_slot_ << std::endl;
  std::cout << "Current Root: " << current_root_slot_ << std::endl;
  std::cout << "Active Forks: " << forks_.size() << std::endl;

  for (const auto &[hash, fork] : forks_) {
    std::cout << "Fork [" << fork->root_slot << " -> " << fork->head_slot
              << "] ";
    std::cout << "weight: " << fork->stake_weight;
    if (fork->is_optimistically_confirmed)
      std::cout << " [OPT]";
    if (fork->is_rooted)
      std::cout << " [ROOT]";
    std::cout << std::endl;
  }
  std::cout << "=================" << std::endl;
}

// Cache management with configurable TTLs and automated expiry
void AdvancedForkChoice::expire_stale_cache_entries() {
  auto now = std::chrono::steady_clock::now();

  // Expire weight cache entries based on TTL
  {
    std::lock_guard<std::mutex> lock(weight_cache_mutex_);
    auto it = weight_cache_.begin();
    while (it != weight_cache_.end()) {
      auto age = now - it->second.second;
      if (age > config_.weight_cache_ttl) {
        it = weight_cache_.erase(it);
      } else {
        ++it;
      }
    }
  }

  // Expire fork weights cache based on TTL
  {
    std::lock_guard<std::mutex> lock(fork_weights_mutex_);
    auto age_since_update = now - last_weight_update_;
    if (age_since_update > config_.fork_weights_cache_ttl) {
      cached_weights_.clear();
      last_weight_update_ = now;
    }
  }

  // Limit cache sizes to prevent unbounded growth using proper LRU eviction
  {
    std::lock_guard<std::mutex> lock(weight_cache_mutex_);
    while (weight_cache_.size() > config_.max_cache_entries) {
      evict_weight_cache_lru();
    }
  }
}

void AdvancedForkChoice::clear_weight_cache() {
  std::lock_guard<std::mutex> lock(weight_cache_mutex_);
  weight_cache_.clear();
  weight_cache_lru_list_.clear();
  weight_cache_lru_map_.clear();
}

void AdvancedForkChoice::clear_confirmation_cache() {
  std::lock_guard<std::mutex> lock(fork_weights_mutex_);
  cached_weights_.clear();
  last_weight_update_ = std::chrono::steady_clock::now();
}

size_t AdvancedForkChoice::get_cache_size() const {
  std::lock_guard<std::mutex> weight_lock(weight_cache_mutex_);
  std::lock_guard<std::mutex> fork_lock(fork_weights_mutex_);
  return weight_cache_.size() + cached_weights_.size();
}

// LRU cache management implementation
void AdvancedForkChoice::update_weight_cache_lru(const Hash &block_hash) const {
  // Remove from current position if exists
  auto lru_map_it = weight_cache_lru_map_.find(block_hash);
  if (lru_map_it != weight_cache_lru_map_.end()) {
    weight_cache_lru_list_.erase(lru_map_it->second);
  }

  // Add to front (most recently used)
  weight_cache_lru_list_.push_front(block_hash);
  weight_cache_lru_map_[block_hash] = weight_cache_lru_list_.begin();
}

void AdvancedForkChoice::evict_weight_cache_lru() const {
  if (weight_cache_lru_list_.empty()) {
    return;
  }

  // Remove least recently used (back of list)
  Hash lru_hash = weight_cache_lru_list_.back();
  weight_cache_lru_list_.pop_back();
  weight_cache_lru_map_.erase(lru_hash);
  weight_cache_.erase(lru_hash);
}

// Event-driven confirmation implementation
void AdvancedForkChoice::add_pending_confirmation_block(
    const Hash &block_hash) {
  // Add block to pending confirmation set (already under data_mutex_)
  pending_confirmation_blocks_.insert(block_hash);
}

void AdvancedForkChoice::process_pending_confirmations() {
  std::unique_lock<std::shared_mutex> lock(data_mutex_);

  // Process only blocks that need confirmation check
  auto it = pending_confirmation_blocks_.begin();
  while (it != pending_confirmation_blocks_.end()) {
    const Hash &block_hash = *it;
    auto block_it = blocks_.find(block_hash);

    if (block_it != blocks_.end() && !block_it->second->is_confirmed) {
      if (check_optimistic_confirmation_conditions(block_hash)) {
        block_it->second->is_confirmed = true;
        stats_.optimistic_confirmations++;

        // Mark fork as optimistically confirmed
        for (auto &[fork_hash, fork] : forks_) {
          if (std::find(fork->blocks.begin(), fork->blocks.end(), block_hash) !=
              fork->blocks.end()) {
            fork->is_optimistically_confirmed = true;
            break;
          }
        }

        std::cout << "Block optimistically confirmed: "
                  << block_it->second->slot << std::endl;
      }
    }

    // Remove from pending set if confirmed or no longer exists
    if (block_it == blocks_.end() || block_it->second->is_confirmed) {
      it = pending_confirmation_blocks_.erase(it);
    } else {
      ++it;
    }
  }
}

// Stake aggregation implementation
void AdvancedForkChoice::rebuild_stake_aggregation() const {
  block_stake_aggregation_.clear();

  // Aggregate stake for each block from all recent votes
  for (const auto &vote : recent_votes_) {
    // Add stake for the voted block
    block_stake_aggregation_[vote.block_hash] += vote.stake_weight;

    // Also add stake for all ancestors (blocks this vote supports)
    Hash current = vote.block_hash;
    while (!current.empty()) {
      auto block_it = blocks_.find(current);
      if (block_it == blocks_.end()) {
        break;
      }

      Hash parent = block_it->second->parent_hash;
      if (!parent.empty()) {
        block_stake_aggregation_[parent] += vote.stake_weight;
      }
      current = parent;
    }
  }
}

void AdvancedForkChoice::update_stake_aggregation_for_vote(
    const VoteInfo &vote) const {
  if (!stake_aggregation_dirty_) {
    // Incrementally update aggregation for this vote
    block_stake_aggregation_[vote.block_hash] += vote.stake_weight;

    // Also update stake for all ancestors
    Hash current = vote.block_hash;
    while (!current.empty()) {
      auto block_it = blocks_.find(current);
      if (block_it == blocks_.end()) {
        break;
      }

      Hash parent = block_it->second->parent_hash;
      if (!parent.empty()) {
        block_stake_aggregation_[parent] += vote.stake_weight;
      }
      current = parent;
    }
  }
}

} // namespace consensus
} // namespace slonana