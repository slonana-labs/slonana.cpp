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
 * Vote information for advanced fork choice
 */
struct VoteInfo {
  Slot slot;
  Hash block_hash;
  PublicKey validator_identity;
  uint64_t stake_weight;
  uint64_t lockout_distance;
  std::chrono::steady_clock::time_point timestamp;
  
  VoteInfo(Slot s, const Hash& hash, const PublicKey& validator, uint64_t stake, uint64_t lockout = 1)
      : slot(s), block_hash(hash), validator_identity(validator), 
        stake_weight(stake), lockout_distance(lockout),
        timestamp(std::chrono::steady_clock::now()) {}
};

/**
 * Fork representation with advanced metrics
 */
struct Fork {
  Hash head_hash;
  Hash root_hash;
  Slot head_slot;
  Slot root_slot;
  uint64_t stake_weight;
  uint64_t confirmation_count;
  bool is_optimistically_confirmed;
  bool is_rooted;
  std::vector<Hash> blocks;  // All blocks in this fork
  std::chrono::steady_clock::time_point last_vote_time;
  
  Fork(const Hash& head, const Hash& root, Slot head_s, Slot root_s)
      : head_hash(head), root_hash(root), head_slot(head_s), root_slot(root_s),
        stake_weight(0), confirmation_count(0), is_optimistically_confirmed(false),
        is_rooted(false), last_vote_time(std::chrono::steady_clock::now()) {}
};

/**
 * Block metadata for fork choice
 */
struct BlockMetadata {
  Hash block_hash;
  Hash parent_hash;
  Slot slot;
  uint64_t stake_weight;
  uint64_t confirmation_count;
  bool is_processed;
  bool is_confirmed;
  std::vector<PublicKey> validators_voted;
  std::chrono::steady_clock::time_point arrival_time;
  
  BlockMetadata(const Hash& hash, const Hash& parent, Slot s)
      : block_hash(hash), parent_hash(parent), slot(s), stake_weight(0),
        confirmation_count(0), is_processed(false), is_confirmed(false),
        arrival_time(std::chrono::steady_clock::now()) {}
};

/**
 * Advanced Fork Choice Algorithm
 * Implements Agave-compatible weighted fork selection with optimistic confirmation
 * 
 * CONCURRENCY MODEL DOCUMENTATION:
 * ================================
 * 
 * LOCK HIERARCHY (acquire in this order to prevent deadlocks):
 * 1. vote_processing_mutex_     - Protects vote processing operations
 * 2. data_mutex_ (shared/unique) - Protects main data structures (blocks_, forks_, etc.)
 * 3. weight_cache_mutex_        - Protects weight_cache_ operations
 * 4. fork_weights_mutex_        - Protects cached_weights_ and related state
 * 
 * LOCK RESPONSIBILITIES:
 * ---------------------
 * - vote_processing_mutex_: Serializes add_vote() and process_votes_batch()
 * - data_mutex_: Guards blocks_, forks_, block_to_fork_map_, recent_votes_, current_head_, etc.
 *   * Shared lock: For read operations (get_head, get_statistics, etc.)
 *   * Unique lock: For write operations (add_block, garbage_collect, etc.)
 * - weight_cache_mutex_: Protects the weight_cache_ from concurrent access
 * - fork_weights_mutex_: Protects instance-based weight caching state
 * 
 * THREAD SAFETY RULES:
 * --------------------
 * 1. All public methods are thread-safe
 * 2. Multiple readers can proceed concurrently (shared_mutex usage)
 * 3. Writers are exclusive (unique_lock usage)
 * 4. Cache operations use separate mutexes to reduce contention
 * 5. No lock upgrades/downgrades - prevents deadlocks
 * 6. RAII lock guards ensure exception safety
 * 
 * PERFORMANCE OPTIMIZATIONS:
 * -------------------------
 * - Reader-writer locks allow concurrent reads
 * - Separate cache mutexes reduce lock contention
 * - O(1) block-to-fork mapping eliminates linear searches
 * - Configurable TTL-based cache expiry prevents stale data
 * - Bounded data structures prevent unbounded memory growth
 */
class AdvancedForkChoice {
public:
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
    
    // Weight caching configuration with configurable TTLs
    std::chrono::milliseconds weight_cache_ttl;
    std::chrono::milliseconds fork_weights_cache_ttl;
    std::chrono::milliseconds confirmation_cache_ttl;
    bool enable_weight_caching;
    size_t max_cache_entries;
    
    Configuration() 
        : optimistic_confirmation_threshold(67)
        , supermajority_threshold(67)
        , rooting_threshold(67)
        , max_lockout_distance(32)
        , confirmation_depth(32)
        , gc_frequency_slots(100)
        , enable_optimistic_confirmation(true)
        , enable_aggressive_rooting(true)
        , total_stake(1000000)
        , weight_cache_ttl(std::chrono::milliseconds(500))
        , fork_weights_cache_ttl(std::chrono::milliseconds(1000))
        , confirmation_cache_ttl(std::chrono::milliseconds(2000))
        , enable_weight_caching(true)
        , max_cache_entries(10000) {}
  };
  
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
    
    // Copy constructor
    Statistics(const Statistics& other) 
        : total_blocks(other.total_blocks.load())
        , total_votes(other.total_votes.load())
        , active_forks(other.active_forks.load())
        , rooted_slots(other.rooted_slots.load())
        , optimistic_confirmations(other.optimistic_confirmations.load())
        , gc_runs(other.gc_runs.load())
        , fork_switches(other.fork_switches.load())
        , last_fork_switch(other.last_fork_switch)
        , last_gc_run(other.last_gc_run) {}
    
    // Assignment operator
    Statistics& operator=(const Statistics& other) {
      if (this != &other) {
        total_blocks.store(other.total_blocks.load());
        total_votes.store(other.total_votes.load());
        active_forks.store(other.active_forks.load());
        rooted_slots.store(other.rooted_slots.load());
        optimistic_confirmations.store(other.optimistic_confirmations.load());
        gc_runs.store(other.gc_runs.load());
        fork_switches.store(other.fork_switches.load());
        last_fork_switch = other.last_fork_switch;
        last_gc_run = other.last_gc_run;
      }
      return *this;
    }
    
    // Default constructor
    Statistics() 
        : last_fork_switch(std::chrono::steady_clock::now())
        , last_gc_run(std::chrono::steady_clock::now()) {}
  };
  
  explicit AdvancedForkChoice(const Configuration& config = Configuration{});
  ~AdvancedForkChoice();
  
  // Core fork choice operations
  void add_block(const Hash& block_hash, const Hash& parent_hash, Slot slot);
  void add_vote(const VoteInfo& vote);
  void process_votes_batch(const std::vector<VoteInfo>& votes);
  
  // Fork selection
  Hash get_head() const;
  Slot get_head_slot() const;
  Hash get_root() const;
  Slot get_root_slot() const;
  
  // Fork management
  std::vector<Fork> get_active_forks() const;
  std::vector<Hash> get_ancestors(const Hash& block_hash, size_t max_count = 100) const;
  std::vector<Hash> get_descendants(const Hash& block_hash) const;
  bool is_ancestor(const Hash& potential_ancestor, const Hash& descendant) const;
  
  // Advanced features
  bool is_optimistically_confirmed(const Hash& block_hash) const;
  bool is_rooted(const Hash& block_hash) const;
  uint64_t get_stake_weight(const Hash& block_hash) const;
  uint64_t get_confirmation_count(const Hash& block_hash) const;
  
  // Optimistic confirmation
  std::vector<Hash> get_optimistically_confirmed_blocks() const;
  bool try_optimistic_confirmation(const Hash& block_hash);
  
  // Rooting operations
  bool try_root_block(const Hash& block_hash);
  void advance_root();
  void cleanup_old_forks();
  
  // Performance and maintenance
  void garbage_collect();
  void compact_data_structures();
  void update_stake_weights(const std::unordered_map<PublicKey, uint64_t>& stake_map);
  
  // Cache management with configurable TTLs and automated expiry
  void expire_stale_cache_entries();
  void clear_weight_cache();
  void clear_confirmation_cache();
  size_t get_cache_size() const;
  
  // Statistics and monitoring
  Statistics get_statistics() const;
  void reset_statistics();
  Configuration get_configuration() const { return config_; }
  void update_configuration(const Configuration& new_config);
  
  // Debugging and analysis
  void print_fork_tree() const;
  std::string get_fork_tree_json() const;
  bool verify_consistency() const;
  
private:
  Configuration config_;
  mutable Statistics stats_;
  
  // Data structures
  std::unordered_map<Hash, std::unique_ptr<BlockMetadata>> blocks_;
  std::unordered_map<Hash, std::unique_ptr<Fork>> forks_;
  std::vector<VoteInfo> recent_votes_;
  std::unordered_map<PublicKey, uint64_t> validator_stakes_;
  
  // Event-driven confirmation tracking
  std::unordered_set<Hash> pending_confirmation_blocks_;
  
  // Stake aggregation for efficient stake counting
  mutable std::unordered_map<Hash, uint64_t> block_stake_aggregation_;
  mutable bool stake_aggregation_dirty_;
  
  // Optimization: Block-to-Fork mapping for O(1) fork lookups
  std::unordered_map<Hash, Fork*> block_to_fork_map_;
  
  // Current state
  Hash current_head_;
  Hash current_root_;
  Slot current_head_slot_;
  Slot current_root_slot_;
  
  // Thread safety
  mutable std::shared_mutex data_mutex_;
  mutable std::mutex vote_processing_mutex_;
  
  // Cache for fork weights with LRU eviction (thread-safe)
  mutable std::unordered_map<Hash, std::pair<uint64_t, std::chrono::steady_clock::time_point>> weight_cache_;
  mutable std::list<Hash> weight_cache_lru_list_;  // For LRU tracking
  mutable std::unordered_map<Hash, std::list<Hash>::iterator> weight_cache_lru_map_;  // Hash to iterator mapping
  mutable std::mutex weight_cache_mutex_;
  
  // Fork weights update state (moved from static to instance members)
  mutable std::unordered_map<Hash, uint64_t> cached_weights_;
  mutable std::chrono::steady_clock::time_point last_weight_update_;
  mutable std::mutex fork_weights_mutex_;
  
  // Internal operations
  void update_fork_weights();
  Fork* find_best_fork() const;
  uint64_t calculate_fork_weight(const Fork& fork) const;
  bool meets_supermajority(const Hash& block_hash, uint64_t threshold) const;
  
  // Optimistic confirmation logic
  bool check_optimistic_confirmation_conditions(const Hash& block_hash) const;
  void process_optimistic_confirmations();
  
  // Rooting logic
  bool check_rooting_conditions(const Hash& block_hash) const;
  void process_rooting_candidates();
  
  // Maintenance operations
  void cleanup_expired_votes();
  void prune_old_blocks();
  bool is_block_expired(const BlockMetadata& block) const;
  
  // Helper methods
  std::vector<Hash> build_chain_to_root(const Hash& block_hash) const;
  uint64_t count_stake_supporting_block(const Hash& block_hash) const;
  uint64_t count_stake_supporting_block_unsafe(const Hash& block_hash) const;
  bool is_ancestor_unsafe(const Hash& potential_ancestor, const Hash& descendant) const;
  void update_fork_metrics(Fork& fork);
  bool is_valid_fork_transition(const Hash& from, const Hash& to) const;
  
  // LRU cache management
  void update_weight_cache_lru(const Hash& block_hash) const;
  void evict_weight_cache_lru() const;
  
  // Event-driven confirmation
  void add_pending_confirmation_block(const Hash& block_hash);
  void process_pending_confirmations();
  
  // Stake aggregation
  void rebuild_stake_aggregation() const;
  void update_stake_aggregation_for_vote(const VoteInfo& vote) const;
};

} // namespace consensus
} // namespace slonana