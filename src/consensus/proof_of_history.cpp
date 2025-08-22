#include "consensus/proof_of_history.h"
#include <openssl/sha.h>
#include <algorithm>
#include <iostream>

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
        timestamp.time_since_epoch()).count();
    for (int i = 0; i < 8; ++i) {
        result.push_back(static_cast<uint8_t>((time_ns >> (i * 8)) & 0xFF));
    }
    
    // Add mixed data count
    uint32_t mixed_count = static_cast<uint32_t>(mixed_data.size());
    for (int i = 0; i < 4; ++i) {
        result.push_back(static_cast<uint8_t>((mixed_count >> (i * 8)) & 0xFF));
    }
    
    // Add mixed data
    for (const auto& data : mixed_data) {
        result.insert(result.end(), data.begin(), data.end());
    }
    
    return result;
}

bool PohEntry::verify_from_previous(const PohEntry& prev) const {
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
    for (const auto& data : mixed_data) {
        hash_input.insert(hash_input.end(), data.begin(), data.end());
    }
    
    // Compute SHA-256
    SHA256(hash_input.data(), hash_input.size(), expected_hash.data());
    
    return hash == expected_hash;
}

// ProofOfHistory implementation
ProofOfHistory::ProofOfHistory(const PohConfig& config) : config_(config) {
    // Initialize statistics
    stats_.total_ticks = 0;
    stats_.total_hashes = 0;
    stats_.avg_tick_duration = std::chrono::microseconds{0};
    stats_.last_tick_duration = std::chrono::microseconds{0};
    stats_.ticks_per_second = 0.0;
    stats_.pending_data_mixes = 0;
}

ProofOfHistory::~ProofOfHistory() {
    stop();
}

Result<bool> ProofOfHistory::start(const Hash& initial_hash) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (running_.load()) {
        return Result<bool>("PoH generator is already running");
    }
    
    // Initialize current entry
    current_entry_.hash = initial_hash;
    current_entry_.sequence_number = 0;
    current_entry_.timestamp = std::chrono::system_clock::now();
    current_entry_.mixed_data.clear();
    
    current_sequence_.store(0);
    current_slot_.store(0);
    
    // Clear history
    {
        std::lock_guard<std::mutex> hist_lock(history_mutex_);
        entry_history_.clear();
        slot_entries_.clear();
    }
    
    // Initialize timing
    start_time_ = std::chrono::system_clock::now();
    last_tick_time_ = start_time_;
    
    running_.store(true);
    stopping_.store(false);
    
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
    stopping_.store(true);
    
    // Wait for threads to finish
    if (tick_thread_.joinable()) {
        tick_thread_.join();
    }
    
    for (auto& thread : hashing_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    hashing_threads_.clear();
    running_.store(false);
}

bool ProofOfHistory::is_running() const {
    return running_.load();
}

PohEntry ProofOfHistory::get_current_entry() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_entry_;
}

uint64_t ProofOfHistory::get_current_sequence() const {
    return current_sequence_.load();
}

Slot ProofOfHistory::get_current_slot() const {
    return current_slot_.load();
}

uint64_t ProofOfHistory::mix_data(const Hash& data) {
    std::lock_guard<std::mutex> lock(mix_queue_mutex_);
    pending_mix_data_.push_back(data);
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.pending_data_mixes = pending_mix_data_.size();
    }
    
    return current_sequence_.load() + pending_mix_data_.size();
}

std::vector<PohEntry> ProofOfHistory::get_slot_entries(Slot slot) const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    auto it = slot_entries_.find(slot);
    if (it != slot_entries_.end()) {
        return it->second;
    }
    return {};
}

bool ProofOfHistory::verify_sequence(const std::vector<PohEntry>& entries) {
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
    while (running_.load() && !stopping_.load()) {
        // This thread can be used for parallel hash computation
        // For now, we'll just yield to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void ProofOfHistory::tick_thread_func() {
    auto next_tick_time = std::chrono::high_resolution_clock::now();
    
    while (running_.load() && !stopping_.load()) {
        process_tick();
        
        // Calculate next tick time
        next_tick_time += config_.target_tick_duration;
        
        // Sleep until next tick
        std::this_thread::sleep_until(next_tick_time);
    }
}

Hash ProofOfHistory::compute_next_hash(const Hash& current, const std::vector<Hash>& mixed_data) {
    Hash result(32); // SHA-256 output size
    
    // Create input for hashing
    std::vector<uint8_t> hash_input;
    hash_input.insert(hash_input.end(), current.begin(), current.end());
    
    // Add mixed data
    for (const auto& data : mixed_data) {
        hash_input.insert(hash_input.end(), data.begin(), data.end());
    }
    
    // Compute SHA-256
    SHA256(hash_input.data(), hash_input.size(), result.data());
    
    return result;
}

void ProofOfHistory::process_tick() {
    auto tick_start = std::chrono::high_resolution_clock::now();
    
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
        current_sequence_.store(new_entry.sequence_number);
    }
    
    // Add to history
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        entry_history_.push_back(new_entry);
        
        // Limit history size
        if (entry_history_.size() > config_.max_entries_buffer) {
            entry_history_.pop_front();
        }
        
        // Add to current slot
        Slot current_slot = current_slot_.load();
        slot_entries_[current_slot].push_back(new_entry);
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
    auto tick_end = std::chrono::high_resolution_clock::now();
    auto tick_duration = std::chrono::duration_cast<std::chrono::microseconds>(tick_end - tick_start);
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_ticks++;
        stats_.total_hashes++;
        stats_.last_tick_duration = tick_duration;
        
        // Calculate average tick duration
        auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(tick_end - start_time_);
        if (stats_.total_ticks > 0) {
            stats_.avg_tick_duration = total_duration / stats_.total_ticks;
            stats_.ticks_per_second = (double)stats_.total_ticks / (total_duration.count() / 1000000.0);
        }
        
        stats_.pending_data_mixes = 0; // Reset since we processed pending data
    }
    
    last_tick_time_ = tick_end;
}

void ProofOfHistory::check_slot_completion() {
    uint64_t sequence = current_sequence_.load();
    uint64_t ticks_in_current_slot = sequence % config_.ticks_per_slot;
    
    if (ticks_in_current_slot == 0 && sequence > 0) {
        // Slot completed
        Slot completed_slot = current_slot_.load();
        current_slot_.store(completed_slot + 1);
        
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
bool PohVerifier::verify_sequence(const std::vector<PohEntry>& entries) {
    if (entries.empty()) {
        return true;
    }
    
    for (size_t i = 1; i < entries.size(); ++i) {
        if (!verify_transition(entries[i-1], entries[i])) {
            return false;
        }
    }
    
    return true;
}

bool PohVerifier::verify_transition(const PohEntry& prev, const PohEntry& curr) {
    return curr.verify_from_previous(prev);
}

bool PohVerifier::verify_timing(const std::vector<PohEntry>& entries, const PohConfig& config) {
    if (entries.size() < 2) {
        return true;
    }
    
    for (size_t i = 1; i < entries.size(); ++i) {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            entries[i].timestamp - entries[i-1].timestamp);
        
        // Allow some tolerance in timing (±50% of target)
        auto min_duration = config.target_tick_duration / 2;
        auto max_duration = config.target_tick_duration * 2;
        
        if (duration < min_duration || duration > max_duration) {
            return false;
        }
    }
    
    return true;
}

std::vector<Hash> PohVerifier::extract_mixed_data(const std::vector<PohEntry>& entries) {
    std::vector<Hash> result;
    
    for (const auto& entry : entries) {
        for (const auto& data : entry.mixed_data) {
            result.push_back(data);
        }
    }
    
    return result;
}

// GlobalProofOfHistory implementation
ProofOfHistory& GlobalProofOfHistory::instance() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (!instance_) {
        throw std::runtime_error("GlobalProofOfHistory not initialized. Call initialize() first.");
    }
    return *instance_;
}

bool GlobalProofOfHistory::initialize(const PohConfig& config) {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (instance_) {
        return false; // Already initialized
    }
    
    instance_ = std::make_unique<ProofOfHistory>(config);
    return true;
}

void GlobalProofOfHistory::shutdown() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (instance_) {
        instance_->stop();
        instance_.reset();
    }
}

uint64_t GlobalProofOfHistory::mix_transaction(const Hash& tx_hash) {
    return instance().mix_data(tx_hash);
}

PohEntry GlobalProofOfHistory::get_current_entry() {
    return instance().get_current_entry();
}

Slot GlobalProofOfHistory::get_current_slot() {
    return instance().get_current_slot();
}

} // namespace consensus
} // namespace slonana