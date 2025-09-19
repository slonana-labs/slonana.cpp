#include "consensus/advanced_fork_choice.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <queue>
#include <unordered_map>
#include <chrono>

namespace slonana {
namespace consensus {

AdvancedForkChoice::AdvancedForkChoice(const Configuration& config) 
    : config_(config), current_head_slot_(0), current_root_slot_(0) {
  
  stats_.last_fork_switch = std::chrono::steady_clock::now();
  stats_.last_gc_run = std::chrono::steady_clock::now();
  
  std::cout << "AdvancedForkChoice initialized with optimistic confirmation threshold: " 
            << config_.optimistic_confirmation_threshold << "%" << std::endl;
}

AdvancedForkChoice::~AdvancedForkChoice() = default;

void AdvancedForkChoice::add_block(const Hash& block_hash, const Hash& parent_hash, Slot slot) {
  std::unique_lock<std::shared_mutex> lock(data_mutex_);
  
  // Create block metadata
  auto block_meta = std::make_unique<BlockMetadata>(block_hash, parent_hash, slot);
  
  // Update statistics
  stats_.total_blocks++;
  
  // Add to blocks collection
  blocks_[block_hash] = std::move(block_meta);
  
  // Create or update fork
  bool fork_created = false;
  
  // Find parent fork efficiently using block-to-fork mapping
  Fork* parent_fork = nullptr;
  auto parent_block_it = blocks_.find(parent_hash);
  if (parent_block_it != blocks_.end()) {
    // Use cached fork reference from block metadata
    for (auto& [fork_hash, fork] : forks_) {
      if (fork->head_hash == parent_hash || 
          (fork->blocks.size() > 0 && fork->blocks.back() == parent_hash)) {
        parent_fork = fork.get();
        break;
      }
    }
  }
  
  if (parent_fork && parent_fork->head_hash == parent_hash) {
    // Extend existing fork
    parent_fork->blocks.push_back(block_hash);
    parent_fork->head_hash = block_hash;
    parent_fork->head_slot = slot;
  } else {
    // Create new fork
    auto new_fork = std::make_unique<Fork>(block_hash, parent_hash, slot, 
                                          parent_fork ? parent_fork->root_slot : slot);
    new_fork->blocks.push_back(block_hash);
    forks_[block_hash] = std::move(new_fork);
    fork_created = true;
  }
  
  if (fork_created) {
    stats_.active_forks++;
  }
  
  // Update fork weights and head selection
  update_fork_weights();
  
  std::cout << "Added block " << slot << " to fork choice (active forks: " 
            << forks_.size() << ")" << std::endl;
}

void AdvancedForkChoice::add_vote(const VoteInfo& vote) {
  // First, add the vote data
  {
    std::lock_guard<std::mutex> vote_lock(vote_processing_mutex_);
    std::unique_lock<std::shared_mutex> lock(data_mutex_);
    
    // Add to recent votes
    recent_votes_.push_back(vote);
    
    // Update validator stake
    validator_stakes_[vote.validator_identity] = vote.stake_weight;
    
    // Update block metadata if exists
    auto block_it = blocks_.find(vote.block_hash);
    if (block_it != blocks_.end()) {
      auto& block_meta = block_it->second;
      
      // Add validator to voting list if not already present
      if (std::find(block_meta->validators_voted.begin(), 
                    block_meta->validators_voted.end(), 
                    vote.validator_identity) == block_meta->validators_voted.end()) {
        block_meta->validators_voted.push_back(vote.validator_identity);
        block_meta->stake_weight += vote.stake_weight;
      }
    }
    
    stats_.total_votes++;
  }
  
  // Now process optimistic confirmations and rooting without holding locks
  if (config_.enable_optimistic_confirmation) {
    process_optimistic_confirmations();
  }
  
  if (config_.enable_aggressive_rooting) {
    process_rooting_candidates();
  }
  
  // Update fork weights
  update_fork_weights();
  
  std::cout << "Processed vote for slot " << vote.slot 
            << " (stake: " << vote.stake_weight << ")" << std::endl;
}

void AdvancedForkChoice::process_votes_batch(const std::vector<VoteInfo>& votes) {
  // First, add all vote data
  {
    std::lock_guard<std::mutex> vote_lock(vote_processing_mutex_);
    std::unique_lock<std::shared_mutex> lock(data_mutex_);
    
    // Process all votes
    for (const auto& vote : votes) {
      recent_votes_.push_back(vote);
      validator_stakes_[vote.validator_identity] = vote.stake_weight;
      
      auto block_it = blocks_.find(vote.block_hash);
      if (block_it != blocks_.end()) {
        auto& block_meta = block_it->second;
        if (std::find(block_meta->validators_voted.begin(), 
                      block_meta->validators_voted.end(), 
                      vote.validator_identity) == block_meta->validators_voted.end()) {
          block_meta->validators_voted.push_back(vote.validator_identity);
          block_meta->stake_weight += vote.stake_weight;
        }
      }
      
      stats_.total_votes++;
    }
  }
  
  // Now batch update operations without holding locks
  if (config_.enable_optimistic_confirmation) {
    process_optimistic_confirmations();
  }
  
  if (config_.enable_aggressive_rooting) {
    process_rooting_candidates();
  }
  
  update_fork_weights();
  
  std::cout << "Processed batch of " << votes.size() << " votes" << std::endl;
}

Hash AdvancedForkChoice::get_head() const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);
  return current_head_;
}

Slot AdvancedForkChoice::get_head_slot() const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);
  return current_head_slot_;
}

Hash AdvancedForkChoice::get_root() const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);
  return current_root_;
}

Slot AdvancedForkChoice::get_root_slot() const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);
  return current_root_slot_;
}

std::vector<Fork> AdvancedForkChoice::get_active_forks() const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);
  
  std::vector<Fork> active_forks;
  for (const auto& [hash, fork] : forks_) {
    active_forks.push_back(*fork);
  }
  
  return active_forks;
}

bool AdvancedForkChoice::is_ancestor(const Hash& potential_ancestor, const Hash& descendant) const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);
  
  // Start from descendant and walk up the chain
  Hash current = descendant;
  
  while (!current.empty()) {
    if (current == potential_ancestor) {
      return true;
    }
    
    auto block_it = blocks_.find(current);
    if (block_it == blocks_.end()) {
      break;
    }
    
    current = block_it->second->parent_hash;
  }
  
  return false;
}

bool AdvancedForkChoice::is_optimistically_confirmed(const Hash& block_hash) const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);
  
  auto block_it = blocks_.find(block_hash);
  if (block_it == blocks_.end()) {
    return false;
  }
  
  // Check if block meets optimistic confirmation threshold
  uint64_t supporting_stake = count_stake_supporting_block(block_hash);
  uint64_t threshold_stake = (config_.total_stake * config_.optimistic_confirmation_threshold) / 100;
  
  return supporting_stake >= threshold_stake;
}

uint64_t AdvancedForkChoice::get_stake_weight(const Hash& block_hash) const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);
  
  auto block_it = blocks_.find(block_hash);
  if (block_it != blocks_.end()) {
    return block_it->second->stake_weight;
  }
  
  return 0;
}

bool AdvancedForkChoice::try_optimistic_confirmation(const Hash& block_hash) {
  std::unique_lock<std::shared_mutex> lock(data_mutex_);
  
  if (!check_optimistic_confirmation_conditions(block_hash)) {
    return false;
  }
  
  auto block_it = blocks_.find(block_hash);
  if (block_it != blocks_.end()) {
    block_it->second->is_confirmed = true;
    stats_.optimistic_confirmations++;
    
    std::cout << "Block optimistically confirmed: " << block_it->second->slot << std::endl;
    return true;
  }
  
  return false;
}

void AdvancedForkChoice::update_fork_weights() {
  // Cache fork weights to avoid recalculation
  static std::unordered_map<Hash, uint64_t> cached_weights;
  static auto last_update = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  
  // Only recalculate if significant time has passed
  if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count() < 100) {
    return;
  }
  last_update = now;
  
  // Find the best fork based on stake weight and other criteria
  Fork* best_fork = find_best_fork();
  
  if (best_fork && best_fork->head_hash != current_head_) {
    // Fork switch detected
    Hash old_head = current_head_;
    Slot old_slot = current_head_slot_;
    
    current_head_ = best_fork->head_hash;
    current_head_slot_ = best_fork->head_slot;
    
    stats_.fork_switches++;
    stats_.last_fork_switch = std::chrono::steady_clock::now();
    
    std::cout << "Fork switch: " << old_slot << " -> " << current_head_slot_ 
              << " (weight: " << best_fork->stake_weight << ")" << std::endl;
  }
}

Fork* AdvancedForkChoice::find_best_fork() const {
  Fork* best_fork = nullptr;
  uint64_t best_weight = 0;
  
  for (const auto& [hash, fork] : forks_) {
    uint64_t weight = calculate_fork_weight(*fork);
    
    if (weight > best_weight) {
      best_weight = weight;
      best_fork = fork.get();
    }
  }
  
  return best_fork;
}

uint64_t AdvancedForkChoice::calculate_fork_weight(const Fork& fork) const {
  // Use cached weight if available and recent
  static std::unordered_map<Hash, std::pair<uint64_t, std::chrono::steady_clock::time_point>> weight_cache;
  auto now = std::chrono::steady_clock::now();
  
  auto cache_it = weight_cache.find(fork.head_hash);
  if (cache_it != weight_cache.end()) {
    auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - cache_it->second.second);
    if (age.count() < 500) { // 500ms cache validity
      return cache_it->second.first;
    }
  }
  
  uint64_t weight = 0;
  
  // Base weight from slot height
  weight += fork.head_slot * 1000;
  
  // Add stake weight only from head block (most recent)
  if (!fork.blocks.empty()) {
    auto block_it = blocks_.find(fork.head_hash);
    if (block_it != blocks_.end()) {
      weight += block_it->second->stake_weight;
    }
  }
  
  // Bonus for optimistic confirmation
  if (fork.is_optimistically_confirmed) {
    weight += 50000;  // Significant bonus
  }
  
  // Bonus for being rooted
  if (fork.is_rooted) {
    weight += 100000;  // Even higher bonus
  }
  
  // Bonus for confirmation count
  weight += fork.confirmation_count * 1000;
  
  // Cache the result
  weight_cache[fork.head_hash] = {weight, now};
  
  return weight;
}

bool AdvancedForkChoice::check_optimistic_confirmation_conditions(const Hash& block_hash) const {
  uint64_t supporting_stake = count_stake_supporting_block_unsafe(block_hash);
  uint64_t threshold_stake = (config_.total_stake * config_.optimistic_confirmation_threshold) / 100;
  
  return supporting_stake >= threshold_stake;
}

void AdvancedForkChoice::process_optimistic_confirmations() {
  std::unique_lock<std::shared_mutex> lock(data_mutex_);
  
  for (const auto& [hash, block] : blocks_) {
    if (!block->is_confirmed && check_optimistic_confirmation_conditions(hash)) {
      block->is_confirmed = true;
      stats_.optimistic_confirmations++;
      
      // Mark fork as optimistically confirmed
      for (auto& [fork_hash, fork] : forks_) {
        if (std::find(fork->blocks.begin(), fork->blocks.end(), hash) != fork->blocks.end()) {
          fork->is_optimistically_confirmed = true;
          break;
        }
      }
    }
  }
}

void AdvancedForkChoice::process_rooting_candidates() {
  std::unique_lock<std::shared_mutex> lock(data_mutex_);
  
  for (const auto& [hash, block] : blocks_) {
    if (!block->is_confirmed && check_rooting_conditions(hash)) {
      // Release lock before calling try_root_block to avoid recursive locking
      lock.unlock();
      try_root_block(hash);
      lock.lock();
    }
  }
}

bool AdvancedForkChoice::check_rooting_conditions(const Hash& block_hash) const {
  // Check if block meets rooting threshold
  uint64_t supporting_stake = count_stake_supporting_block_unsafe(block_hash);
  uint64_t threshold_stake = (config_.total_stake * config_.rooting_threshold) / 100;
  
  return supporting_stake >= threshold_stake;
}

bool AdvancedForkChoice::try_root_block(const Hash& block_hash) {
  auto block_it = blocks_.find(block_hash);
  if (block_it == blocks_.end()) {
    return false;
  }
  
  if (check_rooting_conditions(block_hash)) {
    current_root_ = block_hash;
    current_root_slot_ = block_it->second->slot;
    stats_.rooted_slots++;
    
    // Mark fork as rooted
    for (auto& [fork_hash, fork] : forks_) {
      if (std::find(fork->blocks.begin(), fork->blocks.end(), block_hash) != fork->blocks.end()) {
        fork->is_rooted = true;
        fork->root_hash = block_hash;
        fork->root_slot = block_it->second->slot;
        break;
      }
    }
    
    std::cout << "Block rooted: " << block_it->second->slot << std::endl;
    return true;
  }
  
  return false;
}

uint64_t AdvancedForkChoice::count_stake_supporting_block(const Hash& block_hash) const {
  std::shared_lock<std::shared_mutex> lock(data_mutex_);
  return count_stake_supporting_block_unsafe(block_hash);
}

uint64_t AdvancedForkChoice::count_stake_supporting_block_unsafe(const Hash& block_hash) const {
  uint64_t total_stake = 0;
  
  // Count stake from votes for this block and its descendants
  for (const auto& vote : recent_votes_) {
    if (vote.block_hash == block_hash || is_ancestor_unsafe(block_hash, vote.block_hash)) {
      total_stake += vote.stake_weight;
    }
  }
  
  return total_stake;
}

bool AdvancedForkChoice::is_ancestor_unsafe(const Hash& potential_ancestor, const Hash& descendant) const {
  // Start from descendant and walk up the chain
  Hash current = descendant;
  
  while (!current.empty()) {
    if (current == potential_ancestor) {
      return true;
    }
    
    auto block_it = blocks_.find(current);
    if (block_it == blocks_.end()) {
      break;
    }
    
    current = block_it->second->parent_hash;
  }
  
  return false;
}

void AdvancedForkChoice::garbage_collect() {
  std::unique_lock<std::shared_mutex> lock(data_mutex_);
  
  cleanup_expired_votes();
  prune_old_blocks();
  cleanup_old_forks();
  
  stats_.gc_runs++;
  stats_.last_gc_run = std::chrono::steady_clock::now();
  
  std::cout << "Garbage collection completed (active forks: " << forks_.size() 
            << ", blocks: " << blocks_.size() << ")" << std::endl;
}

void AdvancedForkChoice::cleanup_expired_votes() {
  auto now = std::chrono::steady_clock::now();
  auto cutoff = now - std::chrono::hours(1);  // Keep votes for 1 hour
  
  recent_votes_.erase(
    std::remove_if(recent_votes_.begin(), recent_votes_.end(),
      [cutoff](const VoteInfo& vote) {
        return vote.timestamp < cutoff;
      }),
    recent_votes_.end()
  );
}

void AdvancedForkChoice::prune_old_blocks() {
  // Remove blocks that are too old and not on active forks
  std::unordered_set<Hash> active_blocks;
  
  // Collect all blocks on active forks
  for (const auto& [hash, fork] : forks_) {
    for (const auto& block_hash : fork->blocks) {
      active_blocks.insert(block_hash);
    }
  }
  
  // Remove blocks not on active forks and older than threshold
  auto it = blocks_.begin();
  while (it != blocks_.end()) {
    const auto& [hash, block] = *it;
    
    if (active_blocks.find(hash) == active_blocks.end() && is_block_expired(*block)) {
      it = blocks_.erase(it);
    } else {
      ++it;
    }
  }
}

void AdvancedForkChoice::cleanup_old_forks() {
  // Remove forks that are significantly behind the current head
  Slot cutoff_slot = current_head_slot_ > 1000 ? current_head_slot_ - 1000 : 0;
  
  auto it = forks_.begin();
  while (it != forks_.end()) {
    const auto& [hash, fork] = *it;
    
    if (fork->head_slot < cutoff_slot && !fork->is_rooted) {
      stats_.active_forks--;
      it = forks_.erase(it);
    } else {
      ++it;
    }
  }
}

bool AdvancedForkChoice::is_block_expired(const BlockMetadata& block) const {
  auto now = std::chrono::steady_clock::now();
  auto age = now - block.arrival_time;
  
  return age > std::chrono::hours(2);  // Expire blocks after 2 hours
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
  
  for (const auto& [hash, fork] : forks_) {
    std::cout << "Fork [" << fork->root_slot << " -> " << fork->head_slot << "] ";
    std::cout << "weight: " << fork->stake_weight;
    if (fork->is_optimistically_confirmed) std::cout << " [OPT]";
    if (fork->is_rooted) std::cout << " [ROOT]";
    std::cout << std::endl;
  }
  std::cout << "=================" << std::endl;
}

} // namespace consensus
} // namespace slonana