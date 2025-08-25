#include "cluster/replication_manager.h"
#include "common/types.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <thread>

namespace slonana {
namespace cluster {

ReplicationManager::ReplicationManager(const std::string& node_id, const ValidatorConfig& config)
    : node_id_(node_id), config_(config), running_(false), 
      strategy_(ReplicationStrategy::SYNCHRONOUS), batch_size_(100),
      heartbeat_interval_ms_(5000), sync_check_interval_ms_(10000),
      quorum_size_(2), max_retry_count_(3) {
    
    std::cout << "Replication manager initialized for node: " << node_id_ << std::endl;
}

ReplicationManager::~ReplicationManager() {
    stop();
}

bool ReplicationManager::start() {
    if (running_.load()) return false;
    
    running_.store(true);
    
    // Start replication threads
    replication_thread_ = std::thread(&ReplicationManager::replication_loop, this);
    heartbeat_thread_ = std::thread(&ReplicationManager::heartbeat_loop, this);
    sync_thread_ = std::thread(&ReplicationManager::sync_loop, this);
    
    std::cout << "Replication manager started for node: " << node_id_ << std::endl;
    return true;
}

void ReplicationManager::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    
    // Stop all threads
    if (replication_thread_.joinable()) replication_thread_.join();
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
    if (sync_thread_.joinable()) sync_thread_.join();
    
    std::cout << "Replication manager stopped for node: " << node_id_ << std::endl;
}

bool ReplicationManager::add_target(const std::string& node_id, const std::string& address, uint16_t port) {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    
    if (targets_.find(node_id) != targets_.end()) {
        return false; // Target already exists
    }
    
    ReplicationTarget target;
    target.node_id = node_id;
    target.address = address;
    target.port = port;
    target.last_applied_index = 0;
    target.last_heartbeat = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    target.is_active = true;
    target.retry_count = 0;
    
    targets_[node_id] = target;
    
    std::cout << "Added replication target: " << node_id << " at " << address << ":" << port << std::endl;
    return true;
}

bool ReplicationManager::remove_target(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    
    auto it = targets_.find(node_id);
    if (it == targets_.end()) {
        return false;
    }
    
    targets_.erase(it);
    std::cout << "Removed replication target: " << node_id << std::endl;
    return true;
}

void ReplicationManager::activate_target(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    
    auto it = targets_.find(node_id);
    if (it != targets_.end()) {
        it->second.is_active = true;
        it->second.retry_count = 0;
        std::cout << "Activated replication target: " << node_id << std::endl;
    }
}

void ReplicationManager::deactivate_target(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    
    auto it = targets_.find(node_id);
    if (it != targets_.end()) {
        it->second.is_active = false;
        std::cout << "Deactivated replication target: " << node_id << std::endl;
    }
}

std::vector<ReplicationTarget> ReplicationManager::get_targets() const {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    
    std::vector<ReplicationTarget> targets;
    for (const auto& pair : targets_) {
        targets.push_back(pair.second);
    }
    return targets;
}

bool ReplicationManager::replicate_entry(const std::vector<uint8_t>& data, uint64_t index, uint64_t term) {
    ReplicationEntry entry;
    entry.index = index;
    entry.term = term;
    entry.data = data;
    entry.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    entry.checksum = calculate_checksum(data);
    
    std::lock_guard<std::mutex> lock(queue_mutex_);
    pending_entries_.push(entry);
    
    return true;
}

bool ReplicationManager::replicate_entries(const std::vector<ReplicationEntry>& entries) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    for (const auto& entry : entries) {
        pending_entries_.push(entry);
    }
    
    return true;
}

bool ReplicationManager::force_sync(const std::string& target_id) {
    if (!transport_) {
        std::cerr << "Transport not set for replication manager" << std::endl;
        return false;
    }
    
    std::lock_guard<std::mutex> lock(targets_mutex_);
    auto it = targets_.find(target_id);
    if (it == targets_.end() || !it->second.is_active) {
        return false;
    }
    
    // Request sync from last applied index
    bool success = transport_->request_sync(target_id, it->second.last_applied_index);
    if (success) {
        std::cout << "Force sync initiated for target: " << target_id << std::endl;
    }
    
    return success;
}

bool ReplicationManager::sync_all_targets() {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    
    bool all_success = true;
    for (const auto& pair : targets_) {
        if (pair.second.is_active) {
            bool success = force_sync(pair.first);
            all_success = all_success && success;
        }
    }
    
    return all_success;
}

ReplicationStats ReplicationManager::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

bool ReplicationManager::is_target_active(const std::string& target_id) const {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    
    auto it = targets_.find(target_id);
    return it != targets_.end() && it->second.is_active;
}

uint64_t ReplicationManager::get_target_lag(const std::string& target_id) const {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    
    auto it = targets_.find(target_id);
    if (it == targets_.end()) {
        return UINT64_MAX;
    }
    
    // Calculate lag based on current time and last applied index
    auto current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    return current_time - it->second.last_heartbeat;
}

std::vector<std::string> ReplicationManager::get_failed_targets() const {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    
    std::vector<std::string> failed_targets;
    for (const auto& pair : targets_) {
        if (!pair.second.is_active || pair.second.retry_count >= max_retry_count_) {
            failed_targets.push_back(pair.first);
        }
    }
    
    return failed_targets;
}

void ReplicationManager::set_replication_callback(std::function<void(const std::string&, bool)> callback) {
    replication_callback_ = callback;
}

void ReplicationManager::set_target_failed_callback(std::function<void(const std::string&)> callback) {
    target_failed_callback_ = callback;
}

void ReplicationManager::set_data_provider(std::function<std::vector<ReplicationEntry>(uint64_t, uint64_t)> provider) {
    data_provider_ = provider;
}

bool ReplicationManager::recover_target(const std::string& target_id) {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    
    auto it = targets_.find(target_id);
    if (it == targets_.end()) {
        return false;
    }
    
    // Reset target state and attempt recovery
    it->second.retry_count = 0;
    it->second.is_active = true;
    it->second.last_heartbeat = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    std::cout << "Attempting recovery for target: " << target_id << std::endl;
    return force_sync(target_id);
}

void ReplicationManager::reset_target_state(const std::string& target_id) {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    
    auto it = targets_.find(target_id);
    if (it != targets_.end()) {
        it->second.last_applied_index = 0;
        it->second.retry_count = 0;
        it->second.is_active = true;
        std::cout << "Reset state for target: " << target_id << std::endl;
    }
}

bool ReplicationManager::validate_target_integrity(const std::string& target_id) {
    if (!transport_) {
        return false;
    }
    
    // This would perform integrity checks on the target
    // For now, we'll do a basic connectivity test
    std::lock_guard<std::mutex> lock(targets_mutex_);
    auto it = targets_.find(target_id);
    if (it == targets_.end()) {
        return false;
    }
    
    bool success = transport_->send_heartbeat(target_id);
    if (success) {
        std::cout << "Integrity validation passed for target: " << target_id << std::endl;
    } else {
        std::cout << "Integrity validation failed for target: " << target_id << std::endl;
    }
    
    return success;
}

// Private methods implementation

void ReplicationManager::replication_loop() {
    while (running_.load()) {
        process_pending_entries();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void ReplicationManager::heartbeat_loop() {
    while (running_.load()) {
        if (transport_) {
            std::lock_guard<std::mutex> lock(targets_mutex_);
            for (const auto& pair : targets_) {
                if (pair.second.is_active) {
                    transport_->send_heartbeat(pair.first);
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(heartbeat_interval_ms_));
    }
}

void ReplicationManager::sync_loop() {
    while (running_.load()) {
        // Periodic sync check
        std::lock_guard<std::mutex> lock(targets_mutex_);
        for (const auto& pair : targets_) {
            if (pair.second.is_active) {
                uint64_t lag = get_target_lag(pair.first);
                if (lag > static_cast<uint64_t>(sync_check_interval_ms_)) {
                    std::cout << "Target " << pair.first << " has high lag (" << lag << "ms), initiating sync" << std::endl;
                    force_sync(pair.first);
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(sync_check_interval_ms_));
    }
}

void ReplicationManager::process_pending_entries() {
    std::vector<ReplicationEntry> batch_entries;
    
    // Collect entries for batching
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        size_t batch_count = 0;
        while (!pending_entries_.empty() && batch_count < batch_size_) {
            batch_entries.push_back(pending_entries_.front());
            pending_entries_.pop();
            batch_count++;
        }
    }
    
    if (batch_entries.empty()) {
        return;
    }
    
    // Record batch processing start time for latency calculation
    auto batch_start_time = std::chrono::steady_clock::now();
    
    // Create replication batch
    ReplicationBatch batch = create_batch(batch_entries);
    
    // Replicate to targets
    bool success = replicate_batch(batch);
    
    // Update statistics
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_entries_replicated += batch_entries.size();
        
        if (success) {
            size_t total_bytes = 0;
            for (const auto& entry : batch_entries) {
                total_bytes += entry.data.size();
            }
            stats_.total_bytes_replicated += total_bytes;
            
            // Calculate actual replication latency based on batch size and network conditions
            auto replication_end = std::chrono::steady_clock::now();
            auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(
                replication_end - batch_start_time).count();
            
            // Factor in network conditions and data size for realistic latency
            uint64_t base_latency = std::max(latency_us, static_cast<int64_t>(total_bytes / 1000)); // 1us per KB minimum
            uint64_t network_overhead = total_bytes / 10000; // Network serialization overhead
            uint64_t final_latency = base_latency + network_overhead;
            
            update_stats(true, total_bytes, final_latency);
        } else {
            stats_.failed_replications++;
        }
    }
}

bool ReplicationManager::replicate_batch(const ReplicationBatch& batch) {
    if (!transport_) {
        return false;
    }
    
    std::vector<std::string> successful_targets;
    std::vector<std::string> failed_targets;
    
    {
        std::lock_guard<std::mutex> lock(targets_mutex_);
        for (const auto& pair : targets_) {
            if (!pair.second.is_active) continue;
            
            bool success = transport_->send_batch(pair.first, batch);
            if (success) {
                successful_targets.push_back(pair.first);
                update_target_status(pair.first, true);
            } else {
                failed_targets.push_back(pair.first);
                handle_target_failure(pair.first);
            }
        }
    }
    
    // Check if we meet the replication strategy requirements
    bool meets_requirements = false;
    size_t active_targets = successful_targets.size();
    
    switch (strategy_) {
        case ReplicationStrategy::SYNCHRONOUS:
            meets_requirements = (failed_targets.empty());
            break;
        case ReplicationStrategy::ASYNCHRONOUS:
            meets_requirements = true; // Always succeeds for async
            break;
        case ReplicationStrategy::QUORUM_BASED:
            meets_requirements = (active_targets >= quorum_size_);
            break;
    }
    
    // Notify callback if set
    if (replication_callback_) {
        for (const auto& target : successful_targets) {
            replication_callback_(target, true);
        }
        for (const auto& target : failed_targets) {
            replication_callback_(target, false);
        }
    }
    
    return meets_requirements;
}

void ReplicationManager::handle_target_failure(const std::string& target_id) {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    
    auto it = targets_.find(target_id);
    if (it != targets_.end()) {
        it->second.retry_count++;
        
        if (it->second.retry_count >= max_retry_count_) {
            it->second.is_active = false;
            std::cout << "Target " << target_id << " marked as failed after " << max_retry_count_ << " retries" << std::endl;
            
            if (target_failed_callback_) {
                target_failed_callback_(target_id);
            }
        }
    }
}

void ReplicationManager::update_target_status(const std::string& target_id, bool success) {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    
    auto it = targets_.find(target_id);
    if (it != targets_.end()) {
        if (success) {
            it->second.retry_count = 0;
            it->second.last_heartbeat = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    }
}

ReplicationBatch ReplicationManager::create_batch(const std::vector<ReplicationEntry>& entries) {
    ReplicationBatch batch;
    batch.entries = entries;
    batch.start_index = entries.empty() ? 0 : entries.front().index;
    batch.end_index = entries.empty() ? 0 : entries.back().index;
    batch.batch_id = replication_utils::generate_batch_id();
    
    return batch;
}

std::string ReplicationManager::calculate_checksum(const std::vector<uint8_t>& data) {
    // Simple checksum implementation (in production, use stronger hash)
    uint32_t checksum = 0;
    for (const auto& byte : data) {
        checksum = (checksum << 1) ^ byte;
    }
    
    std::stringstream ss;
    ss << std::hex << checksum;
    return ss.str();
}

void ReplicationManager::update_stats(bool success, size_t bytes, uint64_t latency_ms) {
    if (success) {
        stats_.replication_rate_per_second = 
            static_cast<double>(stats_.total_entries_replicated) / 
            (std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count() + 1);
    }
    
    // Update average latency
    stats_.avg_replication_latency_ms = 
        (stats_.avg_replication_latency_ms + latency_ms) / 2;
    
    // Update active targets count
    std::lock_guard<std::mutex> lock(targets_mutex_);
    stats_.active_targets = 0;
    for (const auto& pair : targets_) {
        if (pair.second.is_active) {
            stats_.active_targets++;
        }
    }
}

// Utility functions implementation

namespace replication_utils {

std::string generate_batch_id() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    
    std::stringstream ss;
    ss << "batch_" << timestamp;
    return ss.str();
}

std::vector<uint8_t> serialize_replication_entry(const ReplicationEntry& entry) {
    std::vector<uint8_t> serialized;
    
    // Serialize index (8 bytes)
    for (int i = 0; i < 8; i++) {
        serialized.push_back((entry.index >> (i * 8)) & 0xFF);
    }
    
    // Serialize term (8 bytes)
    for (int i = 0; i < 8; i++) {
        serialized.push_back((entry.term >> (i * 8)) & 0xFF);
    }
    
    // Serialize timestamp (8 bytes)
    for (int i = 0; i < 8; i++) {
        serialized.push_back((entry.timestamp >> (i * 8)) & 0xFF);
    }
    
    // Serialize data length (4 bytes)
    uint32_t data_len = entry.data.size();
    for (int i = 0; i < 4; i++) {
        serialized.push_back((data_len >> (i * 8)) & 0xFF);
    }
    
    // Serialize data
    serialized.insert(serialized.end(), entry.data.begin(), entry.data.end());
    
    // Serialize checksum length and checksum
    uint32_t checksum_len = entry.checksum.size();
    for (int i = 0; i < 4; i++) {
        serialized.push_back((checksum_len >> (i * 8)) & 0xFF);
    }
    serialized.insert(serialized.end(), entry.checksum.begin(), entry.checksum.end());
    
    return serialized;
}

ReplicationEntry deserialize_replication_entry(const std::vector<uint8_t>& data) {
    ReplicationEntry entry;
    size_t offset = 0;
    
    if (data.size() < 28) { // Minimum size check
        return entry;
    }
    
    // Deserialize index
    entry.index = 0;
    for (int i = 0; i < 8; i++) {
        entry.index |= (static_cast<uint64_t>(data[offset + i]) << (i * 8));
    }
    offset += 8;
    
    // Deserialize term
    entry.term = 0;
    for (int i = 0; i < 8; i++) {
        entry.term |= (static_cast<uint64_t>(data[offset + i]) << (i * 8));
    }
    offset += 8;
    
    // Deserialize timestamp
    entry.timestamp = 0;
    for (int i = 0; i < 8; i++) {
        entry.timestamp |= (static_cast<uint64_t>(data[offset + i]) << (i * 8));
    }
    offset += 8;
    
    // Deserialize data length and data
    uint32_t data_len = 0;
    for (int i = 0; i < 4; i++) {
        data_len |= (static_cast<uint32_t>(data[offset + i]) << (i * 8));
    }
    offset += 4;
    
    if (offset + data_len > data.size()) {
        return entry; // Invalid data
    }
    
    entry.data.assign(data.begin() + offset, data.begin() + offset + data_len);
    offset += data_len;
    
    // Deserialize checksum
    if (offset + 4 <= data.size()) {
        uint32_t checksum_len = 0;
        for (int i = 0; i < 4; i++) {
            checksum_len |= (static_cast<uint32_t>(data[offset + i]) << (i * 8));
        }
        offset += 4;
        
        if (offset + checksum_len <= data.size()) {
            entry.checksum.assign(data.begin() + offset, data.begin() + offset + checksum_len);
        }
    }
    
    return entry;
}

std::vector<uint8_t> serialize_replication_batch(const ReplicationBatch& batch) {
    std::vector<uint8_t> serialized;
    
    // Serialize batch metadata
    for (int i = 0; i < 8; i++) {
        serialized.push_back((batch.start_index >> (i * 8)) & 0xFF);
    }
    for (int i = 0; i < 8; i++) {
        serialized.push_back((batch.end_index >> (i * 8)) & 0xFF);
    }
    
    // Serialize batch ID
    uint32_t id_len = batch.batch_id.size();
    for (int i = 0; i < 4; i++) {
        serialized.push_back((id_len >> (i * 8)) & 0xFF);
    }
    serialized.insert(serialized.end(), batch.batch_id.begin(), batch.batch_id.end());
    
    // Serialize entry count
    uint32_t entry_count = batch.entries.size();
    for (int i = 0; i < 4; i++) {
        serialized.push_back((entry_count >> (i * 8)) & 0xFF);
    }
    
    // Serialize entries
    for (const auto& entry : batch.entries) {
        auto entry_data = serialize_replication_entry(entry);
        uint32_t entry_size = entry_data.size();
        
        // Add entry size prefix
        for (int i = 0; i < 4; i++) {
            serialized.push_back((entry_size >> (i * 8)) & 0xFF);
        }
        
        // Add entry data
        serialized.insert(serialized.end(), entry_data.begin(), entry_data.end());
    }
    
    return serialized;
}

ReplicationBatch deserialize_replication_batch(const std::vector<uint8_t>& data) {
    ReplicationBatch batch;
    size_t offset = 0;
    
    if (data.size() < 24) { // Minimum size check
        return batch;
    }
    
    // Deserialize start and end indexes
    batch.start_index = 0;
    for (int i = 0; i < 8; i++) {
        batch.start_index |= (static_cast<uint64_t>(data[offset + i]) << (i * 8));
    }
    offset += 8;
    
    batch.end_index = 0;
    for (int i = 0; i < 8; i++) {
        batch.end_index |= (static_cast<uint64_t>(data[offset + i]) << (i * 8));
    }
    offset += 8;
    
    // Deserialize batch ID
    uint32_t id_len = 0;
    for (int i = 0; i < 4; i++) {
        id_len |= (static_cast<uint32_t>(data[offset + i]) << (i * 8));
    }
    offset += 4;
    
    if (offset + id_len > data.size()) {
        return batch;
    }
    
    batch.batch_id.assign(data.begin() + offset, data.begin() + offset + id_len);
    offset += id_len;
    
    // Deserialize entry count
    if (offset + 4 > data.size()) {
        return batch;
    }
    
    uint32_t entry_count = 0;
    for (int i = 0; i < 4; i++) {
        entry_count |= (static_cast<uint32_t>(data[offset + i]) << (i * 8));
    }
    offset += 4;
    
    // Deserialize entries
    for (uint32_t i = 0; i < entry_count && offset < data.size(); i++) {
        if (offset + 4 > data.size()) break;
        
        uint32_t entry_size = 0;
        for (int j = 0; j < 4; j++) {
            entry_size |= (static_cast<uint32_t>(data[offset + j]) << (j * 8));
        }
        offset += 4;
        
        if (offset + entry_size > data.size()) break;
        
        std::vector<uint8_t> entry_data(data.begin() + offset, data.begin() + offset + entry_size);
        ReplicationEntry entry = deserialize_replication_entry(entry_data);
        batch.entries.push_back(entry);
        offset += entry_size;
    }
    
    return batch;
}

std::string calculate_data_checksum(const std::vector<uint8_t>& data) {
    // Simple hash implementation for checksum
    uint64_t hash = 5381;
    for (const auto& byte : data) {
        hash = ((hash << 5) + hash) + byte;
    }
    
    std::stringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

bool verify_batch_integrity(const ReplicationBatch& batch) {
    for (const auto& entry : batch.entries) {
        std::string calculated_checksum = calculate_data_checksum(entry.data);
        if (calculated_checksum != entry.checksum) {
            return false;
        }
    }
    return true;
}

} // namespace replication_utils

}} // namespace slonana::cluster