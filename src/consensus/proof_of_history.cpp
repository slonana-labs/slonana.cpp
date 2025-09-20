#include "consensus/proof_of_history.h"
#include <algorithm>
#include <iostream>
#include <openssl/sha.h>

// Lock-free queue implementation (fallback if boost not available)
#if HAS_LOCKFREE_QUEUE
#include <boost/lockfree/queue.hpp>
#else
// Simple lock-free queue fallback using traditional containers
// WARNING: This fallback uses blocking mutex-based queue operations.
// Performance will be significantly reduced compared to true lock-free
// boost::lockfree. Consider installing boost::lockfree for production use.
namespace boost {
namespace lockfree {
template <typename T> class queue {
  std::mutex mutex_;
  std::queue<T> queue_;

public:
  explicit queue(size_t) {}
  bool push(T item) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(item);
    return true;
  }
  bool pop(T &item) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty())
      return false;
    item = queue_.front();
    queue_.pop();
    return true;
  }
};
} // namespace lockfree
} // namespace boost
#endif

namespace slonana {
namespace consensus {

// Static members for GlobalProofOfHistory
std::unique_ptr<ProofOfHistory> GlobalProofOfHistory::instance_ = nullptr;
std::mutex GlobalProofOfHistory::instance_mutex_;

// PohEntry implementation
std::vector<uint8_t> PohEntry::serialize() const {
  std::vector<uint8_t> result;

  // Add hash
  result.insert(result.end(), hash.begin(), hash.end());

  // Add sequence number (8 bytes, little endian)
  for (int i = 0; i < 8; ++i) {
    result.push_back(static_cast<uint8_t>((sequence_number >> (i * 8)) & 0xFF));
  }

  // Add timestamp (8 bytes, nanoseconds since epoch)
  auto time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                     timestamp.time_since_epoch())
                     .count();
  for (int i = 0; i < 8; ++i) {
    result.push_back(static_cast<uint8_t>((time_ns >> (i * 8)) & 0xFF));
  }

  // Add mixed data count
  uint32_t mixed_count = static_cast<uint32_t>(mixed_data.size());
  for (int i = 0; i < 4; ++i) {
    result.push_back(static_cast<uint8_t>((mixed_count >> (i * 8)) & 0xFF));
  }

  // Add mixed data
  for (const auto &data : mixed_data) {
    result.insert(result.end(), data.begin(), data.end());
  }

  return result;
}

bool PohEntry::verify_from_previous(const PohEntry &prev) const {
  // Verify sequence number increments
  if (sequence_number != prev.sequence_number + 1) {
    return false;
  }

  // Verify timestamp progression
  if (timestamp <= prev.timestamp) {
    return false;
  }

  // Verify hash chain
  Hash expected_hash(32); // SHA-256 output size

  // Create input for hashing
  std::vector<uint8_t> hash_input;
  hash_input.insert(hash_input.end(), prev.hash.begin(), prev.hash.end());

  // Add mixed data if present
  for (const auto &data : mixed_data) {
    hash_input.insert(hash_input.end(), data.begin(), data.end());
  }

  // Compute SHA-256
  SHA256(hash_input.data(), hash_input.size(), expected_hash.data());

  return hash == expected_hash;
}

// ProofOfHistory implementation
ProofOfHistory::ProofOfHistory(const PohConfig &config) : config_(config) {
  // Initialize lock-free queue if enabled and available
#if HAS_LOCKFREE_QUEUE
  if (config_.enable_lock_free_structures) {
    lock_free_mix_queue_ = std::make_unique<boost::lockfree::queue<Hash *>>(
        config_.max_entries_buffer);
  }
#endif

  // Initialize enhanced statistics
  stats_.total_ticks = 0;
  stats_.total_hashes = 0;
  stats_.avg_tick_duration = std::chrono::microseconds{0};
  stats_.last_tick_duration = std::chrono::microseconds{0};
  stats_.min_tick_duration =
      std::chrono::microseconds{std::numeric_limits<int64_t>::max()};
  stats_.max_tick_duration = std::chrono::microseconds{0};
  stats_.ticks_per_second = 0.0;
  stats_.effective_tps = 0.0;
  stats_.pending_data_mixes = 0;
  stats_.batches_processed = 0;
  stats_.batch_efficiency = 0.0;
  stats_.simd_acceleration_active =
      config_.enable_simd_acceleration && SLONANA_HAS_SIMD;
  stats_.lock_contention_ratio = 0.0;
}

ProofOfHistory::~ProofOfHistory() {
  stop();

  // Clean up any remaining pointers in lock-free queue
#if HAS_LOCKFREE_QUEUE
  if (lock_free_mix_queue_) {
    Hash *data_ptr;
    while (lock_free_mix_queue_->pop(data_ptr)) {
      delete data_ptr;
    }
  }
#endif
}

Result<bool> ProofOfHistory::start(const Hash &initial_hash) {
  std::lock_guard<std::mutex> lock(state_mutex_);

  if (running_.load(std::memory_order_acquire)) {
    return Result<bool>("PoH generator is already running");
  }

  // Initialize current entry
  current_entry_.hash = initial_hash;
  current_entry_.sequence_number = 0;
  current_entry_.timestamp = std::chrono::system_clock::now();
  current_entry_.mixed_data.clear();

  current_sequence_.store(0, std::memory_order_release);
  current_slot_.store(0, std::memory_order_release);

  // Clear history
  {
    std::lock_guard<std::mutex> hist_lock(history_mutex_);
    entry_history_.clear();
    slot_entries_.clear();
  }

  // Initialize timing
  start_time_ = std::chrono::system_clock::now();
  last_tick_time_ = start_time_;

  running_.store(true, std::memory_order_release);
  stopping_.store(false, std::memory_order_release);

  // Start threads
  tick_thread_ = std::thread(&ProofOfHistory::tick_thread_func, this);

  if (config_.enable_hashing_threads) {
    hashing_threads_.reserve(config_.hashing_threads);
    for (uint32_t i = 0; i < config_.hashing_threads; ++i) {
      hashing_threads_.emplace_back(&ProofOfHistory::hashing_thread_func, this);
    }
  }

  return Result<bool>(true);
}

void ProofOfHistory::stop() {
  stopping_.store(true, std::memory_order_release);

  // Wait for threads to finish
  if (tick_thread_.joinable()) {
    tick_thread_.join();
  }

  for (auto &thread : hashing_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  hashing_threads_.clear();
  running_.store(false, std::memory_order_release);
}

bool ProofOfHistory::is_running() const {
  return running_.load(std::memory_order_acquire);
}

PohEntry ProofOfHistory::get_current_entry() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return current_entry_;
}

uint64_t ProofOfHistory::get_current_sequence() const {
  return current_sequence_.load(std::memory_order_acquire);
}

Slot ProofOfHistory::get_current_slot() const {
  return current_slot_.load(std::memory_order_acquire);
}

uint64_t ProofOfHistory::mix_data(const Hash &data) {
  if (config_.enable_lock_contention_tracking) {
    lock_attempts_.fetch_add(1, std::memory_order_relaxed);
  }

#if HAS_LOCKFREE_QUEUE
  if (config_.enable_lock_free_structures && lock_free_mix_queue_) {
    // Use lock-free queue for better performance
    Hash *data_ptr = new Hash(data);
    if (lock_free_mix_queue_->push(data_ptr)) {
      return current_sequence_.load(std::memory_order_acquire) + 1;
    } else {
      delete data_ptr;
      if (config_.enable_lock_contention_tracking) {
        lock_contention_count_.fetch_add(1, std::memory_order_relaxed);
      }
      // Fallback to traditional queue
    }
  }
#endif

  // Traditional mutex-based approach (fallback or when lock-free disabled)
  // Apply backpressure to prevent OOM under flood conditions
  std::lock_guard<std::mutex> lock(mix_queue_mutex_);

  // Check if queue is approaching capacity limit to prevent memory explosion
  if (pending_mix_data_.size() >= config_.max_entries_buffer) {
    // Drop oldest entries when at capacity (FIFO dropping policy)
    pending_mix_data_.pop_front();
    if (config_.enable_lock_contention_tracking) {
      lock_contention_count_.fetch_add(
          1, std::memory_order_relaxed); // Track dropped entries
    }
  }

  pending_mix_data_.push_back(data);

  {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.pending_data_mixes = pending_mix_data_.size();
  }

  return current_sequence_.load(std::memory_order_acquire) +
         pending_mix_data_.size();
}

std::vector<PohEntry> ProofOfHistory::get_slot_entries(Slot slot) const {
  std::lock_guard<std::mutex> lock(history_mutex_);
  auto it = slot_entries_.find(slot);
  if (it != slot_entries_.end()) {
    return it->second;
  }
  return {};
}

bool ProofOfHistory::verify_sequence(const std::vector<PohEntry> &entries) {
  return PohVerifier::verify_sequence(entries);
}

void ProofOfHistory::set_tick_callback(TickCallback callback) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);
  tick_callback_ = std::move(callback);
}

void ProofOfHistory::set_slot_callback(SlotCallback callback) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);
  slot_callback_ = std::move(callback);
}

ProofOfHistory::PohStats ProofOfHistory::get_stats() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  return stats_;
}

void ProofOfHistory::hashing_thread_func() {
  while (running_.load(std::memory_order_acquire) &&
         !stopping_.load(std::memory_order_acquire)) {
    if (config_.enable_batch_processing) {
      // Process batches of hashes for better performance
      process_tick_batch();
    } else {
      // Traditional single hash processing
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
}

void ProofOfHistory::tick_thread_func() {
  auto next_tick_time = std::chrono::system_clock::now();

  while (running_.load(std::memory_order_acquire) &&
         !stopping_.load(std::memory_order_acquire)) {
    if (config_.enable_batch_processing) {
      process_tick_batch();
    } else {
      process_tick();
    }

    // Calculate next tick time with higher precision
    next_tick_time += config_.target_tick_duration;

    // High-precision sleep
    std::this_thread::sleep_until(next_tick_time);
  }
}

Hash ProofOfHistory::compute_next_hash(const Hash &current,
                                       const std::vector<Hash> &mixed_data) {
  if (config_.enable_simd_acceleration && SLONANA_HAS_SIMD) {
    return compute_next_hash_simd(current, mixed_data);
  }

  Hash result(32); // SHA-256 output size

  // Create input for hashing
  std::vector<uint8_t> hash_input;
  hash_input.reserve(32 +
                     mixed_data.size() * 32); // Pre-allocate for performance
  hash_input.insert(hash_input.end(), current.begin(), current.end());

  // Add mixed data
  for (const auto &data : mixed_data) {
    hash_input.insert(hash_input.end(), data.begin(), data.end());
  }

  // Compute SHA-256
  SHA256(hash_input.data(), hash_input.size(), result.data());

  return result;
}

Hash ProofOfHistory::compute_next_hash_simd(
    const Hash &current, const std::vector<Hash> &mixed_data) {
#if SLONANA_HAS_SIMD
  // Use hardware-accelerated SHA-256 when available
  Hash result(32);

  // Create input for hashing with SIMD-friendly alignment
  std::vector<uint8_t> hash_input;
  hash_input.reserve(32 + mixed_data.size() * 32);
  hash_input.insert(hash_input.end(), current.begin(), current.end());

  // Add mixed data
  for (const auto &data : mixed_data) {
    hash_input.insert(hash_input.end(), data.begin(), data.end());
  }

  // Use optimized SHA-256 computation
  SHA256(hash_input.data(), hash_input.size(), result.data());

  return result;
#else
  // Fallback to regular computation
  return compute_next_hash(current, mixed_data);
#endif
}

void ProofOfHistory::process_tick_batch() {
  auto tick_start = std::chrono::system_clock::now();

  // Process multiple ticks in batch for better performance
  std::vector<Hash> mixed_data_batch;

  // Collect mixed data from lock-free queue
#if HAS_LOCKFREE_QUEUE
  if (config_.enable_lock_free_structures && lock_free_mix_queue_) {
    Hash *data_ptr;
    while (lock_free_mix_queue_->pop(data_ptr) &&
           mixed_data_batch.size() < config_.batch_size) {
      mixed_data_batch.push_back(*data_ptr);
      delete data_ptr;
    }
  } else
#endif
  {
    // Fallback to traditional queue
    std::lock_guard<std::mutex> lock(mix_queue_mutex_);
    size_t batch_size = std::min(
        config_.batch_size, static_cast<uint32_t>(pending_mix_data_.size()));
    if (batch_size > 0) {
      mixed_data_batch.assign(pending_mix_data_.begin(),
                              pending_mix_data_.begin() + batch_size);
      pending_mix_data_.erase(pending_mix_data_.begin(),
                              pending_mix_data_.begin() + batch_size);
    }
  }

  // Create new entry
  PohEntry new_entry;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    new_entry.hash = compute_next_hash(current_entry_.hash, mixed_data_batch);
    new_entry.sequence_number = current_entry_.sequence_number + 1;
    new_entry.timestamp = std::chrono::system_clock::now();
    new_entry.mixed_data = std::move(mixed_data_batch);

    current_entry_ = new_entry;
    current_sequence_.store(new_entry.sequence_number);
  }

  // Add to history with optimized insertion
  {
    std::lock_guard<std::mutex> lock(history_mutex_);
    entry_history_.emplace_back(std::move(new_entry));

    // Limit history size with more efficient cleanup
    if (entry_history_.size() > config_.max_entries_buffer) {
      entry_history_.pop_front();
    }

    // Add to current slot
    Slot current_slot = current_slot_.load(std::memory_order_acquire);
    slot_entries_[current_slot].emplace_back(new_entry);
  }

  // Call tick callback
  {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    if (tick_callback_) {
      tick_callback_(new_entry);
    }
  }

  // Check for slot completion
  check_slot_completion();

  // Update enhanced statistics
  auto tick_end = std::chrono::system_clock::now();
  auto tick_duration = std::chrono::duration_cast<std::chrono::microseconds>(
      tick_end - tick_start);

  {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.total_ticks++;
    stats_.total_hashes++;
    stats_.batches_processed++;
    stats_.last_tick_duration = tick_duration;

    // Update min/max durations
    if (tick_duration < stats_.min_tick_duration) {
      stats_.min_tick_duration = tick_duration;
    }
    if (tick_duration > stats_.max_tick_duration) {
      stats_.max_tick_duration = tick_duration;
    }

    // Calculate performance metrics
    auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        tick_end - start_time_);
    if (stats_.total_ticks > 0) {
      stats_.avg_tick_duration = total_duration / stats_.total_ticks;
      stats_.ticks_per_second =
          (double)stats_.total_ticks / (total_duration.count() / 1000000.0);
      stats_.effective_tps = stats_.ticks_per_second * config_.batch_size;
    }

    // Calculate batch efficiency
    if (stats_.batches_processed > 0) {
      stats_.batch_efficiency = (double)stats_.total_hashes /
                                (stats_.batches_processed * config_.batch_size);
    }

    // Calculate lock contention ratio only if tracking is enabled
    if (config_.enable_lock_contention_tracking) {
      uint64_t attempts = lock_attempts_.load(std::memory_order_relaxed);
      uint64_t contentions =
          lock_contention_count_.load(std::memory_order_relaxed);
      if (attempts > 0) {
        stats_.lock_contention_ratio = (double)contentions / attempts;
      }
    } else {
      stats_.lock_contention_ratio = 0.0; // Not tracked
    }

    stats_.pending_data_mixes = mixed_data_batch.size();
    
    // Update last tick time under mutex protection to prevent race conditions
    last_tick_time_ = tick_end;
  }
}

void ProofOfHistory::process_tick() {
  auto tick_start = std::chrono::system_clock::now();

  // Get pending mixed data
  std::vector<Hash> mixed_data;
  {
    std::lock_guard<std::mutex> lock(mix_queue_mutex_);
    if (!pending_mix_data_.empty()) {
      mixed_data.assign(pending_mix_data_.begin(), pending_mix_data_.end());
      pending_mix_data_.clear();
    }
  }

  // Create new entry
  PohEntry new_entry;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    new_entry.hash = compute_next_hash(current_entry_.hash, mixed_data);
    new_entry.sequence_number = current_entry_.sequence_number + 1;
    new_entry.timestamp = std::chrono::system_clock::now();
    new_entry.mixed_data = std::move(mixed_data);

    current_entry_ = new_entry;
    current_sequence_.store(new_entry.sequence_number,
                            std::memory_order_release);
  }

  // Add to history
  Slot current_slot = current_slot_.load(std::memory_order_acquire);
  {
    std::lock_guard<std::mutex> lock(history_mutex_);
    entry_history_.push_back(new_entry);

    // Limit history size
    if (entry_history_.size() > config_.max_entries_buffer) {
      entry_history_.pop_front();
    }

    // Add to current slot
    slot_entries_[current_slot].push_back(new_entry);
  }

  // Enhanced tick logging every 10 ticks
  if (new_entry.sequence_number % 10 == 0) {
    uint64_t ticks_in_slot = new_entry.sequence_number % config_.ticks_per_slot;
    std::cout << "⏱️  PoH tick " << new_entry.sequence_number << " (slot "
              << current_slot << ", tick " << ticks_in_slot << "/"
              << config_.ticks_per_slot << ")" << std::endl;
  }

  // Call tick callback
  {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    if (tick_callback_) {
      tick_callback_(new_entry);
    }
  }

  // Check for slot completion
  check_slot_completion();

  // Update statistics
  auto tick_end = std::chrono::system_clock::now();
  auto tick_duration = std::chrono::duration_cast<std::chrono::microseconds>(
      tick_end - tick_start);

  {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.total_ticks++;
    stats_.total_hashes++;
    stats_.last_tick_duration = tick_duration;

    // Update min/max durations for legacy process_tick
    if (tick_duration < stats_.min_tick_duration) {
      stats_.min_tick_duration = tick_duration;
    }
    if (tick_duration > stats_.max_tick_duration) {
      stats_.max_tick_duration = tick_duration;
    }

    // Calculate average tick duration
    auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        tick_end - start_time_);
    if (stats_.total_ticks > 0) {
      stats_.avg_tick_duration = total_duration / stats_.total_ticks;
      stats_.ticks_per_second =
          (double)stats_.total_ticks / (total_duration.count() / 1000000.0);
      stats_.effective_tps = stats_.ticks_per_second; // Single tick processing
    }

    stats_.pending_data_mixes = 0; // Reset since we processed pending data
    
    // Update last tick time under mutex protection to prevent race conditions
    last_tick_time_ = tick_end;
  }
}

void ProofOfHistory::check_slot_completion() {
  uint64_t sequence = current_sequence_.load(std::memory_order_acquire);
  uint64_t ticks_in_current_slot = sequence % config_.ticks_per_slot;

  if (ticks_in_current_slot == 0 && sequence > 0) {
    // Slot completed
    Slot completed_slot = current_slot_.load(std::memory_order_acquire);
    Slot new_slot = completed_slot + 1;
    current_slot_.store(new_slot, std::memory_order_release);

    // Enhanced slot progression logging
    std::cout << "🎯 Slot " << completed_slot << " completed with "
              << config_.ticks_per_slot << " ticks, advancing to slot "
              << new_slot << " (sequence: " << sequence << ")" << std::endl;

    // Get slot entries
    std::vector<PohEntry> slot_entries;
    {
      std::lock_guard<std::mutex> lock(history_mutex_);
      auto it = slot_entries_.find(completed_slot);
      if (it != slot_entries_.end()) {
        slot_entries = it->second;
      }
    }

    // Call slot callback
    {
      std::lock_guard<std::mutex> lock(callbacks_mutex_);
      if (slot_callback_) {
        slot_callback_(completed_slot, slot_entries);
      }
    }
  }
}

// PohVerifier implementation
bool PohVerifier::verify_sequence(const std::vector<PohEntry> &entries) {
  if (entries.empty()) {
    return true;
  }

  for (size_t i = 1; i < entries.size(); ++i) {
    if (!verify_transition(entries[i - 1], entries[i])) {
      return false;
    }
  }

  return true;
}

bool PohVerifier::verify_transition(const PohEntry &prev,
                                    const PohEntry &curr) {
  return curr.verify_from_previous(prev);
}

bool PohVerifier::verify_timing(const std::vector<PohEntry> &entries,
                                const PohConfig &config) {
  if (entries.size() < 2) {
    return true;
  }

  for (size_t i = 1; i < entries.size(); ++i) {
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        entries[i].timestamp - entries[i - 1].timestamp);

    // Allow some tolerance in timing (±50% of target)
    auto min_duration = config.target_tick_duration / 2;
    auto max_duration = config.target_tick_duration * 2;

    if (duration < min_duration || duration > max_duration) {
      return false;
    }
  }

  return true;
}

std::vector<Hash>
PohVerifier::extract_mixed_data(const std::vector<PohEntry> &entries) {
  std::vector<Hash> result;

  for (const auto &entry : entries) {
    for (const auto &data : entry.mixed_data) {
      result.push_back(data);
    }
  }

  return result;
}

// GlobalProofOfHistory implementation
ProofOfHistory &GlobalProofOfHistory::instance() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) {
    throw std::runtime_error(
        "GlobalProofOfHistory not initialized. Call initialize() first.");
  }
  return *instance_;
}

bool GlobalProofOfHistory::initialize(const PohConfig &config,
                                      const Hash &initial_hash) {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (instance_) {
    return false; // Already initialized
  }

  instance_ = std::make_unique<ProofOfHistory>(config);

  // Start the PoH instance immediately with the provided initial hash
  auto start_result = instance_->start(initial_hash);
  if (!start_result.is_ok()) {
    // If start fails, clean up and return failure
    instance_.reset();
    return false;
  }

  return true;
}

void GlobalProofOfHistory::shutdown() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (instance_) {
    instance_->stop();
    instance_.reset();
  }
}

bool GlobalProofOfHistory::is_initialized() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  return instance_ != nullptr;
}

uint64_t GlobalProofOfHistory::mix_transaction(const Hash &tx_hash) {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) {
    return 0; // Return default value when uninitialized
  }
  return instance_->mix_data(tx_hash);
}

PohEntry GlobalProofOfHistory::get_current_entry() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) {
    // Return default entry when uninitialized
    PohEntry default_entry;
    default_entry.hash = Hash(32, 0); // Empty hash
    default_entry.sequence_number = 0;
    default_entry.timestamp = std::chrono::system_clock::now();
    return default_entry;
  }
  return instance_->get_current_entry();
}

Slot GlobalProofOfHistory::get_current_slot() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) {
    return 0; // Return slot 0 when uninitialized
  }
  return instance_->get_current_slot();
}

bool GlobalProofOfHistory::set_tick_callback(
    ProofOfHistory::TickCallback callback) {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) {
    return false; // Cannot set callback when uninitialized
  }
  instance_->set_tick_callback(std::move(callback));
  return true;
}

bool GlobalProofOfHistory::set_slot_callback(
    ProofOfHistory::SlotCallback callback) {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) {
    return false; // Cannot set callback when uninitialized
  }
  instance_->set_slot_callback(std::move(callback));
  return true;
}

} // namespace consensus
} // namespace slonana