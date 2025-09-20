#pragma once

#include "common/types.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

// Lock-free data structures (optional)
#ifdef BOOST_LOCKFREE_HPP
#include <boost/lockfree/queue.hpp>
#define HAS_LOCKFREE_QUEUE 1
#else
#define HAS_LOCKFREE_QUEUE 0
#include <queue>
#endif

// SIMD acceleration
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#include <wmmintrin.h>
#define SLONANA_HAS_SIMD 1
#else
#define SLONANA_HAS_SIMD 0
#endif

namespace slonana {
namespace consensus {

using namespace slonana::common;

/**
 * Proof of History entry representing a single hash in the sequence
 */
struct PohEntry {
  Hash hash;                                       // Current hash value
  uint64_t sequence_number;                        // Sequential position
  std::chrono::system_clock::time_point timestamp; // Wall clock time
  std::vector<Hash> mixed_data; // Any data mixed into this hash

  std::vector<uint8_t> serialize() const;
  bool verify_from_previous(const PohEntry &prev) const;
};

/**
 * Configuration for the Proof of History generator
 */
struct PohConfig {
  std::chrono::microseconds target_tick_duration{
      200};                     // Target time per tick (200μs for 5,000 TPS)
  uint64_t ticks_per_slot = 64; // Number of ticks per slot
  size_t max_entries_buffer = 2000;        // Maximum buffered entries
  bool enable_hashing_threads = true;      // Use dedicated hashing threads
  uint32_t hashing_threads = 4;            // Number of hashing threads
  bool enable_simd_acceleration = true;    // Use SIMD/hardware acceleration
  bool enable_batch_processing = true;     // Batch process multiple hashes
  uint32_t batch_size = 8;                 // Number of hashes to batch process
  bool enable_lock_free_structures = true; // Use lock-free data structures
  bool enable_lock_contention_tracking = false; // Track lock contention metrics
};

/**
 * Proof of History generator creating verifiable timestamps
 */
class ProofOfHistory {
public:
  using TickCallback = std::function<void(const PohEntry &)>;
  using SlotCallback = std::function<void(Slot, const std::vector<PohEntry> &)>;

  explicit ProofOfHistory(const PohConfig &config = PohConfig{});
  ~ProofOfHistory();

  // Non-copyable, non-movable
  ProofOfHistory(const ProofOfHistory &) = delete;
  ProofOfHistory &operator=(const ProofOfHistory &) = delete;

  /**
   * Initialize and start the PoH generator
   * @param initial_hash Starting hash for the sequence
   * @return true if started successfully
   */
  Result<bool> start(const Hash &initial_hash);

  /**
   * Stop the PoH generator
   */
  void stop();

  /**
   * Check if the generator is running
   */
  bool is_running() const;

  /**
   * Get the current PoH entry
   */
  PohEntry get_current_entry() const;

  /**
   * Get the current sequence number
   */
  uint64_t get_current_sequence() const;

  /**
   * Get the current slot number
   */
  Slot get_current_slot() const;

  /**
   * Mix data into the next PoH hash
   * @param data Data to mix into the hash chain
   * @return Estimated sequence number where data will be mixed (best-effort).
   *         This is an immediate estimate and may not reflect the actual
   *         committed sequence if the data is still pending processing.
   *         Returns 0 if the PoH generator is not running.
   *         
   *         WARNING: This is a best-effort guess, not a committed sequence ID.
   *         The actual sequence may differ due to concurrent processing.
   */
  uint64_t mix_data(const Hash &data);

  /**
   * Get entries for a specific slot
   * @param slot Slot number to query
   * @return vector of PoH entries for that slot
   */
  std::vector<PohEntry> get_slot_entries(Slot slot) const;

  /**
   * Verify a sequence of PoH entries
   * @param entries Entries to verify
   * @return true if sequence is valid
   */
  static bool verify_sequence(const std::vector<PohEntry> &entries);

  /**
   * Register callback for each tick
   * WARNING: Callbacks are invoked while holding internal locks.
   * Callbacks should NOT call back into ProofOfHistory methods to avoid deadlock.
   * Keep callbacks fast and simple.
   */
  void set_tick_callback(TickCallback callback);

  /**
   * Register callback for slot completion
   * WARNING: Callbacks are invoked while holding internal locks.
   * Callbacks should NOT call back into ProofOfHistory methods to avoid deadlock.
   * Keep callbacks fast and simple.
   */
  void set_slot_callback(SlotCallback callback);

  /**
   * Get performance statistics with detailed breakdown
   */
  struct PohStats {
    uint64_t total_ticks;
    uint64_t total_hashes;
    std::chrono::microseconds avg_tick_duration;
    std::chrono::microseconds last_tick_duration;
    std::chrono::microseconds min_tick_duration;
    std::chrono::microseconds max_tick_duration;
    double ticks_per_second;
    double effective_tps; // Theoretical transaction throughput
    size_t pending_data_mixes;
    uint64_t batches_processed;    // Number of batch operations
    double batch_efficiency;       // Average batch utilization
    bool simd_acceleration_active; // Whether SIMD is being used
    double lock_contention_ratio;  // Lock contention metrics
  };

  PohStats get_stats() const;

private:
  void hashing_thread_func();
  void tick_thread_func();
  Hash compute_next_hash(const Hash &current,
                         const std::vector<Hash> &mixed_data = {});
  void process_tick();
  void check_slot_completion();

  // Performance optimizations
  void process_tick_batch();
  Hash compute_next_hash_simd(const Hash &current,
                              const std::vector<Hash> &mixed_data = {});
  void batch_hash_computation(
      std::vector<Hash> &hashes,
      const std::vector<std::vector<Hash>> &mixed_data_batches);

  PohConfig config_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stopping_{false};

  mutable std::mutex state_mutex_;
  PohEntry current_entry_;
  std::atomic<uint64_t> current_sequence_{0};
  std::atomic<Slot> current_slot_{0};

  // Threading
  std::vector<std::thread> hashing_threads_;
  std::thread tick_thread_;

  // Lock-free data mixing queue for high performance (optional - only if boost
  // available)
#if HAS_LOCKFREE_QUEUE
  std::unique_ptr<boost::lockfree::queue<Hash *>> lock_free_mix_queue_;
#endif

  // Fallback traditional data mixing
  mutable std::mutex mix_queue_mutex_;
  std::deque<Hash> pending_mix_data_;

  // Entry history with optimized storage
  mutable std::mutex history_mutex_;
  std::deque<PohEntry> entry_history_;
  std::map<Slot, std::vector<PohEntry>> slot_entries_;

  // Callbacks
  std::mutex callbacks_mutex_;
  TickCallback tick_callback_;
  SlotCallback slot_callback_;

  // Enhanced statistics tracking
  mutable std::mutex stats_mutex_;
  PohStats stats_;
  std::chrono::system_clock::time_point last_tick_time_;
  std::chrono::system_clock::time_point start_time_;
  std::atomic<uint64_t> lock_contention_count_{0};
  std::atomic<uint64_t> lock_attempts_{0};
  
  // Helper class for lock contention tracking
  class InstrumentedLockGuard {
  private:
    std::lock_guard<std::mutex> guard_;
    
  public:
    InstrumentedLockGuard(std::mutex& mutex, std::atomic<uint64_t>& attempts, 
                         std::atomic<uint64_t>& contentions) : guard_(mutex) {
      attempts.fetch_add(1, std::memory_order_relaxed);
      // Note: Real contention detection would require try_lock timing, 
      // but for now we assume any lock attempt could encounter contention
      // This is a simplified implementation for basic monitoring
    }
  };
};

/**
 * Proof of History verifier for validating PoH sequences
 */
class PohVerifier {
public:
  PohVerifier() = default;

  /**
   * Verify a complete PoH sequence
   * @param entries Sequence of PoH entries to verify
   * @return true if sequence is cryptographically valid
   */
  static bool verify_sequence(const std::vector<PohEntry> &entries);

  /**
   * Verify that an entry follows correctly from the previous
   * @param prev Previous entry in sequence
   * @param curr Current entry to verify
   * @return true if transition is valid
   */
  static bool verify_transition(const PohEntry &prev, const PohEntry &curr);

  /**
   * Verify timing constraints for a sequence
   * @param entries Entries to check timing for
   * @param config PoH configuration with timing constraints
   * @return true if timing is within acceptable bounds
   */
  static bool verify_timing(const std::vector<PohEntry> &entries,
                            const PohConfig &config);

  /**
   * Extract and verify mixed data from PoH entries
   * @param entries PoH entries containing mixed data
   * @return vector of extracted data hashes
   */
  static std::vector<Hash>
  extract_mixed_data(const std::vector<PohEntry> &entries);
};

/**
 * Global Proof of History instance
 */
class GlobalProofOfHistory {
public:
  static ProofOfHistory &instance();
  static bool initialize(const PohConfig &config = {}, const Hash &initial_hash = Hash(32, 0x42));
  static void shutdown();
  static bool is_initialized();

  // Convenience methods
  static uint64_t mix_transaction(const Hash &tx_hash);
  static PohEntry get_current_entry();
  static Slot get_current_slot();
  
  // Safe callback setup
  // WARNING: Callbacks are invoked while holding internal locks.
  // Callbacks should NOT call back into ProofOfHistory methods to avoid deadlock.
  static bool set_tick_callback(ProofOfHistory::TickCallback callback);
  static bool set_slot_callback(ProofOfHistory::SlotCallback callback);

private:
  static std::unique_ptr<ProofOfHistory> instance_;
  static std::mutex instance_mutex_;
};

} // namespace consensus
} // namespace slonana