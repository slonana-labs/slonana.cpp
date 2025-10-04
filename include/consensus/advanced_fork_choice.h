/**
 * @file advanced_fork_choice.h
 * @brief Defines an advanced, Agave-compatible fork choice rule implementation.
 *
 * This file contains the data structures and class definition for a sophisticated
 * fork choice algorithm. It uses stake-weighting, optimistic confirmation, and
 * rooting mechanisms to determine the canonical head of the blockchain.
 */
#pragma once

#include "common/types.h"
#include "consensus/tower_bft.h"
#include <atomic>
#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace slonana {
namespace consensus {

using namespace slonana::common;

/**
 * @brief Represents a validator's vote on a specific block.
 * @details This structure contains all necessary information to process a vote
 * within the fork choice rule, including the target block, the voter's identity
 * and stake, and lockout information.
 */
struct VoteInfo {
  /// @brief The slot number of the block being voted on.
  Slot slot;
  /// @brief The hash of the block being voted on.
  Hash block_hash;
  /// @brief The public key of the voting validator.
  PublicKey validator_identity;
  /// @brief The stake weight of the validator at the time of voting.
  uint64_t stake_weight;
  /// @brief The lockout distance for this vote, affecting how long the validator is committed to this fork.
  uint64_t lockout_distance;
  /// @brief The time when the vote was received by this node.
  std::chrono::steady_clock::time_point timestamp;
  
  VoteInfo(Slot s, const Hash& hash, const PublicKey& validator, uint64_t stake, uint64_t lockout = 1)
      : slot(s), block_hash(hash), validator_identity(validator), 
        stake_weight(stake), lockout_distance(lockout),
        timestamp(std::chrono::steady_clock::now()) {}
};

/**
 * @brief Represents a potential chain of blocks (a fork) in the blocktree.
 * @details Each fork is a candidate for the canonical chain, and this struct
 * tracks its properties, such as total stake weight and confirmation status.
 */
struct Fork {
  /// @brief The hash of the block at the head of this fork.
  Hash head_hash;
  /// @brief The hash of the root block from which this fork diverges.
  Hash root_hash;
  /// @brief The slot number of the head block.
  Slot head_slot;
  /// @brief The slot number of the root block.
  Slot root_slot;
  /// @brief The total stake weight that has voted for this fork.
  uint64_t stake_weight;
  /// @brief A count of confirmations, used for rooting and finality.
  uint64_t confirmation_count;
  /// @brief A flag indicating if the fork is considered confirmed under optimistic rules.
  bool is_optimistically_confirmed;
  /// @brief A flag indicating if the fork has been rooted (finalized).
  bool is_rooted;
  /// @brief A list of all block hashes in this fork.
  std::vector<Hash> blocks;
  /// @brief The timestamp of the last vote received for this fork.
  std::chrono::steady_clock::time_point last_vote_time;
  
  Fork(const Hash& head, const Hash& root, Slot head_s, Slot root_s)
      : head_hash(head), root_hash(root), head_slot(head_s), root_slot(root_s),
        stake_weight(0), confirmation_count(0), is_optimistically_confirmed(false),
        is_rooted(false), last_vote_time(std::chrono::steady_clock::now()) {}
};

/**
 * @brief Stores consensus-related metadata for a single block.
 * @details This information is used by the fork choice algorithm to track votes,
 * stake weight, and confirmation status for each block.
 */
struct BlockMetadata {
  /// @brief The hash of the block.
  Hash block_hash;
  /// @brief The hash of the parent block.
  Hash parent_hash;
  /// @brief The slot number of the block.
  Slot slot;
  /// @brief The cumulative stake weight that has voted for this block and its descendants.
  uint64_t stake_weight;
  /// @brief The number of confirmations this block has received.
  uint64_t confirmation_count;
  /// @brief A flag indicating if the block has been processed by the fork choice rule.
  bool is_processed;
  /// @brief A flag indicating if the block is considered confirmed.
  bool is_confirmed;
  /// @brief A list of public keys of validators that have voted for this block.
  std::vector<PublicKey> validators_voted;
  /// @brief The time when the block was received by this node.
  std::chrono::steady_clock::time_point arrival_time;
  
  BlockMetadata(const Hash& hash, const Hash& parent, Slot s)
      : block_hash(hash), parent_hash(parent), slot(s), stake_weight(0),
        confirmation_count(0), is_processed(false), is_confirmed(false),
        arrival_time(std::chrono::steady_clock::now()) {}
};

/**
 * @brief Implements an advanced, Agave-compatible weighted fork choice algorithm.
 * @details This class manages the blocktree, processes votes, and uses a stake-weighted
 * algorithm to determine the canonical head of the chain. It incorporates features
 * like optimistic confirmation and rooting for finality. It is designed to be
 * thread-safe and performant, with a detailed concurrency model.
 *
 * @section afc_concurrency Concurrency Model
 *
 * The class uses a combination of `std::shared_mutex` and `std::mutex` to
 * allow for concurrent reads while ensuring safe writes.
 *
 * **Lock Hierarchy (to prevent deadlocks):**
 * 1. `vote_processing_mutex_`
 * 2. `data_mutex_` (shared/unique)
 * 3. `weight_cache_mutex_`
 * 4. `fork_weights_mutex_`
 *
 * **Thread Safety Guarantees:**
 * - All public methods are thread-safe.
 * - Multiple readers can access the data structures concurrently.
 * - Writers have exclusive access.
 * - Caching mechanisms use separate locks to reduce contention.
 */
class AdvancedForkChoice {
public:
  /**
   * @brief Configuration settings for the AdvancedForkChoice algorithm.
   */
  struct Configuration {
    uint64_t optimistic_confirmation_threshold;
    uint64_t supermajority_threshold;
    uint64_t rooting_threshold;
    uint32_t max_lockout_distance;
    uint32_t confirmation_depth;
    uint32_t gc_frequency_slots;
    bool enable_optimistic_confirmation;
    bool enable_aggressive_rooting;
    uint64_t total_stake;
    std::chrono::milliseconds weight_cache_ttl;
    std::chrono::milliseconds fork_weights_cache_ttl;
    std::chrono::milliseconds confirmation_cache_ttl;
    bool enable_weight_caching;
    size_t max_cache_entries;
    
    Configuration() 
        : optimistic_confirmation_threshold(67), supermajority_threshold(67),
          rooting_threshold(67), max_lockout_distance(32), confirmation_depth(32),
          gc_frequency_slots(100), enable_optimistic_confirmation(true),
          enable_aggressive_rooting(true), total_stake(1000000),
          weight_cache_ttl(std::chrono::milliseconds(500)),
          fork_weights_cache_ttl(std::chrono::milliseconds(1000)),
          confirmation_cache_ttl(std::chrono::milliseconds(2000)),
          enable_weight_caching(true), max_cache_entries(10000) {}
  };
  
  /**
   * @brief A collection of performance and status metrics for the fork choice rule.
   */
  struct Statistics {
    std::atomic<size_t> total_blocks{0};
    std::atomic<size_t> total_votes{0};
    std::atomic<size_t> active_forks{0};
    std::atomic<size_t> rooted_slots{0};
    std::atomic<size_t> optimistic_confirmations{0};
    std::atomic<size_t> gc_runs{0};
    std::atomic<size_t> fork_switches{0};
    std::chrono::steady_clock::time_point last_fork_switch;
    std::chrono::steady_clock::time_point last_gc_run;
    
    Statistics(const Statistics& other);
    Statistics& operator=(const Statistics& other);
    Statistics();
  };
  
  explicit AdvancedForkChoice(const Configuration& config = Configuration{});
  ~AdvancedForkChoice();
  
  void add_block(const Hash& block_hash, const Hash& parent_hash, Slot slot);
  void add_vote(const VoteInfo& vote);
  void process_votes_batch(const std::vector<VoteInfo>& votes);
  
  Hash get_head() const;
  Slot get_head_slot() const;
  Hash get_root() const;
  Slot get_root_slot() const;
  
  std::vector<Fork> get_active_forks() const;
  std::vector<Hash> get_ancestors(const Hash& block_hash, size_t max_count = 100) const;
  std::vector<Hash> get_descendants(const Hash& block_hash) const;
  bool is_ancestor(const Hash& potential_ancestor, const Hash& descendant) const;
  
  bool is_optimistically_confirmed(const Hash& block_hash) const;
  bool is_rooted(const Hash& block_hash) const;
  uint64_t get_stake_weight(const Hash& block_hash) const;
  uint64_t get_confirmation_count(const Hash& block_hash) const;
  
  std::vector<Hash> get_optimistically_confirmed_blocks() const;
  bool try_optimistic_confirmation(const Hash& block_hash);
  
  bool try_root_block(const Hash& block_hash);
  void advance_root();
  void cleanup_old_forks();
  
  void garbage_collect();
  void compact_data_structures();
  void update_stake_weights(const std::unordered_map<PublicKey, uint64_t>& stake_map);
  
  void expire_stale_cache_entries();
  void clear_weight_cache();
  void clear_confirmation_cache();
  size_t get_cache_size() const;
  
  Statistics get_statistics() const;
  void reset_statistics();
  Configuration get_configuration() const { return config_; }
  void update_configuration(const Configuration& new_config);
  
  void print_fork_tree() const;
  std::string get_fork_tree_json() const;
  bool verify_consistency() const;
  
private:
  Configuration config_;
  mutable Statistics stats_;
  std::unordered_map<Hash, std::unique_ptr<BlockMetadata>> blocks_;
  std::unordered_map<Hash, std::unique_ptr<Fork>> forks_;
  std::vector<VoteInfo> recent_votes_;
  std::unordered_map<PublicKey, uint64_t> validator_stakes_;
  std::unordered_set<Hash> pending_confirmation_blocks_;
  mutable std::unordered_map<Hash, uint64_t> block_stake_aggregation_;
  mutable bool stake_aggregation_dirty_;
  std::unordered_map<Hash, Fork*> block_to_fork_map_;
  Hash current_head_;
  Hash current_root_;
  Slot current_head_slot_;
  Slot current_root_slot_;
  mutable std::shared_mutex data_mutex_;
  mutable std::mutex vote_processing_mutex_;
  mutable std::unordered_map<Hash, std::pair<uint64_t, std::chrono::steady_clock::time_point>> weight_cache_;
  mutable std::list<Hash> weight_cache_lru_list_;
  mutable std::unordered_map<Hash, std::list<Hash>::iterator> weight_cache_lru_map_;
  mutable std::mutex weight_cache_mutex_;
  mutable std::unordered_map<Hash, uint64_t> cached_weights_;
  mutable std::chrono::steady_clock::time_point last_weight_update_;
  mutable std::mutex fork_weights_mutex_;
  
  void update_fork_weights();
  Fork* find_best_fork() const;
  uint64_t calculate_fork_weight(const Fork& fork) const;
  bool meets_supermajority(const Hash& block_hash, uint64_t threshold) const;
  bool check_optimistic_confirmation_conditions(const Hash& block_hash) const;
  void process_optimistic_confirmations();
  bool check_rooting_conditions(const Hash& block_hash) const;
  void process_rooting_candidates();
  void cleanup_expired_votes();
  void prune_old_blocks();
  bool is_block_expired(const BlockMetadata& block) const;
  std::vector<Hash> build_chain_to_root(const Hash& block_hash) const;
  uint64_t count_stake_supporting_block(const Hash& block_hash) const;
  uint64_t count_stake_supporting_block_unsafe(const Hash& block_hash) const;
  bool is_ancestor_unsafe(const Hash& potential_ancestor, const Hash& descendant) const;
  void update_fork_metrics(Fork& fork);
  bool is_valid_fork_transition(const Hash& from, const Hash& to) const;
  void update_weight_cache_lru(const Hash& block_hash) const;
  void evict_weight_cache_lru() const;
  void add_pending_confirmation_block(const Hash& block_hash);
  void process_pending_confirmations();
  void rebuild_stake_aggregation() const;
  void update_stake_aggregation_for_vote(const VoteInfo& vote) const;
};

} // namespace consensus
} // namespace slonana