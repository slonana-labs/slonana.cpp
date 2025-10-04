/**
 * @file proof_of_history.h
 * @brief Defines the core components for generating and verifying Proof of History (PoH).
 *
 * This file contains the classes and data structures for creating a verifiable,
 * sequential record of events over time. This is the fundamental clock for the
 * Slonana validator, ensuring a canonical ordering of transactions.
 */
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

// Optional lock-free data structures for performance
#ifdef BOOST_LOCKFREE_HPP
#include <boost/lockfree/queue.hpp>
#define HAS_LOCKFREE_QUEUE 1
#else
#define HAS_LOCKFREE_QUEUE 0
#include <queue>
#endif

// SIMD acceleration for hashing
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
 * @brief Represents a single entry (a "tick") in the Proof of History sequence.
 * @details Each entry contains a hash that is dependent on the previous entry's
 * hash, forming a verifiable chain.
 */
struct PohEntry {
  /// @brief The hash value of this entry.
  Hash hash;
  /// @brief The sequential position of this entry in the chain.
  uint64_t sequence_number;
  /// @brief The wall-clock timestamp when this entry was generated.
  std::chrono::system_clock::time_point timestamp;
  /// @brief Any external data (like transaction hashes) mixed into this entry's hash.
  std::vector<Hash> mixed_data;

  std::vector<uint8_t> serialize() const;
  bool verify_from_previous(const PohEntry &prev) const;
};

/**
 * @brief Configuration settings for the Proof of History generator.
 */
struct PohConfig {
  /// @brief The desired time duration for each PoH tick.
  std::chrono::microseconds target_tick_duration{200};
  /// @brief The number of ticks that constitute a single slot.
  uint64_t ticks_per_slot = 64;
  /// @brief The maximum number of PoH entries to buffer in memory.
  size_t max_entries_buffer = 2000;
  /// @brief If true, use dedicated threads for hashing to offload the main thread.
  bool enable_hashing_threads = true;
  /// @brief The number of dedicated hashing threads to use.
  uint32_t hashing_threads = 4;
  /// @brief If true, use SIMD instructions (if available) to accelerate hashing.
  bool enable_simd_acceleration = true;
  /// @brief If true, process multiple hashes in a single batch operation.
  bool enable_batch_processing = true;
  /// @brief The number of hashes to process in a single batch.
  uint32_t batch_size = 8;
  /// @brief If true, use lock-free queues for data mixing to reduce contention.
  bool enable_lock_free_structures = true;
  /// @brief If true, track metrics on lock contention (can have performance overhead).
  bool enable_lock_contention_tracking = false;
  /// @brief If true, allow runtime enabling/disabling of contention tracking.
  bool enable_dynamic_contention_tracking = true;
};

/**
 * @brief A high-performance generator for creating a verifiable sequence of hashes over time (Proof of History).
 * @details This class manages the continuous generation of PoH "ticks", groups them
 * into slots, and allows external data to be mixed into the hash chain to timestamp events.
 */
class ProofOfHistory {
public:
  using TickCallback = std::function<void(const PohEntry &)>;
  using SlotCallback = std::function<void(Slot, const std::vector<PohEntry> &)>;

  explicit ProofOfHistory(const PohConfig &config = PohConfig{});
  ~ProofOfHistory();

  ProofOfHistory(const ProofOfHistory &) = delete;
  ProofOfHistory &operator=(const ProofOfHistory &) = delete;

  Result<bool> start(const Hash &initial_hash);
  void stop();
  bool is_running() const;

  PohEntry get_current_entry() const;
  uint64_t get_current_sequence() const;
  Slot get_current_slot() const;

  uint64_t mix_data(const Hash &data);
  std::vector<PohEntry> get_slot_entries(Slot slot) const;
  static bool verify_sequence(const std::vector<PohEntry> &entries);

  void set_tick_callback(TickCallback callback);
  void set_slot_callback(SlotCallback callback);

  /**
   * @brief A collection of performance and status metrics for the PoH generator.
   */
  struct PohStats {
    uint64_t total_ticks;
    uint64_t total_hashes;
    std::chrono::microseconds avg_tick_duration;
    std::chrono::microseconds last_tick_duration;
    std::chrono::microseconds min_tick_duration;
    std::chrono::microseconds max_tick_duration;
    double ticks_per_second;
    double effective_tps;
    size_t pending_data_mixes;
    uint64_t batches_processed;
    double batch_efficiency;
    bool simd_acceleration_active;
    double lock_contention_ratio;
    uint64_t dropped_mixes;
  };

  PohStats get_stats() const;

private:
  void hashing_thread_func();
  void tick_thread_func();
  Hash compute_next_hash(const Hash &current, const std::vector<Hash> &mixed_data = {});
  void process_tick();
  void check_slot_completion();
  void process_tick_batch();
  Hash compute_next_hash_simd(const Hash &current, const std::vector<Hash> &mixed_data = {});
  void batch_hash_computation(std::vector<Hash> &hashes, const std::vector<std::vector<Hash>> &mixed_data_batches);
  void update_stats_locked(std::chrono::microseconds tick_duration, std::chrono::system_clock::time_point tick_end, bool is_batch_processing, size_t pending_mixes_count = 0);
  void update_stats_impl(std::chrono::microseconds tick_duration, std::chrono::system_clock::time_point tick_end, bool is_batch_processing, size_t pending_mixes_count);

  PohConfig config_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stopping_{false};
  mutable std::mutex state_mutex_;
  PohEntry current_entry_;
  std::atomic<uint64_t> current_sequence_{0};
  std::atomic<Slot> current_slot_{0};
  std::vector<std::thread> hashing_threads_;
  std::thread tick_thread_;

#if HAS_LOCKFREE_QUEUE
  std::unique_ptr<boost::lockfree::queue<Hash *>> lock_free_mix_queue_;
#endif
  mutable std::mutex mix_queue_mutex_;
  std::deque<Hash> pending_mix_data_;

  mutable std::mutex history_mutex_;
  std::deque<PohEntry> entry_history_;
  std::map<Slot, std::vector<PohEntry>> slot_entries_;
  std::mutex callbacks_mutex_;
  TickCallback tick_callback_;
  SlotCallback slot_callback_;
  mutable std::mutex stats_mutex_;
  PohStats stats_;
  std::chrono::system_clock::time_point last_tick_time_;
  std::chrono::system_clock::time_point start_time_;
  std::atomic<uint64_t> lock_contention_count_{0};
  std::atomic<uint64_t> lock_attempts_{0};
  std::atomic<uint64_t> dropped_mixes_{0};
  
  static constexpr size_t MAX_SLOT_HISTORY = 1000;
  
  /**
   * @brief An instrumented lock guard for tracking mutex contention.
   * @details This helper class wraps `std::unique_lock` and increments counters
   * to measure how often threads have to wait for a lock, which is useful for
   * performance profiling. It is only active when contention tracking is enabled.
   */
  class InstrumentedLockGuard {
  private:
    std::unique_lock<std::mutex> guard_;
  public:
    InstrumentedLockGuard(std::mutex& mutex, std::atomic<uint64_t>& attempts, std::atomic<uint64_t>& contentions);
  };
};

/**
 * @brief Provides static methods for verifying a Proof of History sequence.
 */
class PohVerifier {
public:
  PohVerifier() = default;
  static bool verify_sequence(const std::vector<PohEntry> &entries);
  static bool verify_transition(const PohEntry &prev, const PohEntry &curr);
  static bool verify_timing(const std::vector<PohEntry> &entries, const PohConfig &config);
  static std::vector<Hash> extract_mixed_data(const std::vector<PohEntry> &entries);
};

/**
 * @brief Provides global singleton access to the Proof of History generator.
 * @details This class ensures that there is only one instance of the PoH
 * generator running in the system and provides a safe way to access it.
 */
class GlobalProofOfHistory {
public:
  static ProofOfHistory &instance();
  static bool initialize(const PohConfig &config = {}, const Hash &initial_hash = Hash(32, 0x42));
  static void shutdown();
  static bool is_initialized();
  static uint64_t mix_transaction(const Hash &tx_hash);
  static PohEntry get_current_entry();
  static Slot get_current_slot();
  static bool set_tick_callback(ProofOfHistory::TickCallback callback);
  static bool set_slot_callback(ProofOfHistory::SlotCallback callback);

private:
  static std::unique_ptr<ProofOfHistory> instance_;
  static std::mutex instance_mutex_;
};

} // namespace consensus
} // namespace slonana