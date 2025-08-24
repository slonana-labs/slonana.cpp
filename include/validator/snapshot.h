#pragma once

#include "common/types.h"
#include "ledger/manager.h"
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <unordered_map>

namespace slonana {
namespace validator {

/**
 * Validator Snapshot System
 * 
 * Provides state snapshotting capabilities for validator synchronization
 * and recovery. Enables incremental snapshots and full state restoration.
 */

struct SnapshotMetadata {
    uint64_t slot;
    std::string block_hash;
    uint64_t timestamp;
    uint64_t lamports_total;
    uint64_t account_count;
    uint64_t compressed_size;
    uint64_t uncompressed_size;
    std::string version;
    bool is_incremental;
    uint64_t base_slot; // For incremental snapshots
};

struct AccountSnapshot {
    common::PublicKey pubkey;
    uint64_t lamports;
    std::vector<uint8_t> data;
    common::PublicKey owner;
    bool executable;
    uint64_t rent_epoch;
};

struct SnapshotChunk {
    uint64_t chunk_index;
    uint64_t total_chunks;
    std::vector<AccountSnapshot> accounts;
    std::vector<uint8_t> compressed_data;
    std::string chunk_hash;
};

/**
 * Snapshot Manager
 * 
 * Handles creation, storage, and restoration of validator state snapshots.
 * Supports both full and incremental snapshots for efficient synchronization.
 */
class SnapshotManager {
public:
    explicit SnapshotManager(const std::string& snapshot_dir);
    ~SnapshotManager();

    // Snapshot creation
    bool create_full_snapshot(uint64_t slot, const std::string& ledger_path);
    bool create_incremental_snapshot(uint64_t slot, uint64_t base_slot, const std::string& ledger_path);
    
    // Snapshot restoration
    bool restore_from_snapshot(const std::string& snapshot_path, const std::string& ledger_path);
    std::vector<AccountSnapshot> load_accounts_from_snapshot(const std::string& snapshot_path);
    
    // Snapshot management
    std::vector<SnapshotMetadata> list_available_snapshots() const;
    SnapshotMetadata get_latest_snapshot() const;
    bool delete_old_snapshots(uint64_t keep_count = 5);
    
    // Snapshot verification
    bool verify_snapshot_integrity(const std::string& snapshot_path);
    std::string calculate_snapshot_hash(const std::string& snapshot_path);
    
    // Configuration
    void set_compression_enabled(bool enabled) { compression_enabled_ = enabled; }
    void set_max_chunk_size(size_t size) { max_chunk_size_ = size; }
    void set_auto_snapshot_interval(uint64_t slots) { auto_snapshot_interval_ = slots; }
    
    // Statistics
    struct SnapshotStats {
        uint64_t total_snapshots_created = 0;
        uint64_t total_snapshots_restored = 0;
        uint64_t total_bytes_written = 0;
        uint64_t total_bytes_read = 0;
        uint64_t average_creation_time_ms = 0;
        uint64_t average_restoration_time_ms = 0;
        uint64_t last_snapshot_slot = 0;
        std::chrono::system_clock::time_point last_snapshot_time;
    };
    
    SnapshotStats get_statistics() const { return stats_; }

private:
    std::string snapshot_dir_;
    bool compression_enabled_;
    size_t max_chunk_size_;
    uint64_t auto_snapshot_interval_;
    mutable SnapshotStats stats_;
    
    // Helper methods
    std::string generate_snapshot_filename(uint64_t slot, bool is_incremental = false, uint64_t base_slot = 0) const;
    bool compress_data(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) const;
    bool decompress_data(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) const;
    std::string calculate_hash(const std::vector<uint8_t>& data) const;
    
    // Serialization helpers
    std::vector<uint8_t> serialize_metadata(const SnapshotMetadata& metadata) const;
    SnapshotMetadata deserialize_metadata(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> serialize_account(const AccountSnapshot& account) const;
    AccountSnapshot deserialize_account(const std::vector<uint8_t>& data, size_t& offset) const;
    
    // Ledger restoration helpers
    bool validate_account_integrity(const AccountSnapshot& account) const;
    bool restore_account_to_ledger(const AccountSnapshot& account) const;
    void update_ledger_metadata(size_t total_accounts, size_t restored_accounts) const;
    void verify_ledger_consistency() const;
};

/**
 * Automatic Snapshot Service
 * 
 * Background service that automatically creates snapshots at regular intervals
 * and manages snapshot cleanup to prevent disk space issues.
 */
class AutoSnapshotService {
public:
    explicit AutoSnapshotService(std::shared_ptr<SnapshotManager> snapshot_manager);
    ~AutoSnapshotService();

    // Service control
    void start(uint64_t interval_slots = 1000);
    void stop();
    bool is_running() const { return running_; }
    
    // Configuration
    void set_full_snapshot_interval(uint64_t slots) { full_snapshot_interval_ = slots; }
    void set_incremental_snapshot_interval(uint64_t slots) { incremental_snapshot_interval_ = slots; }
    void set_cleanup_enabled(bool enabled) { cleanup_enabled_ = enabled; }
    void set_max_snapshots_to_keep(uint64_t count) { max_snapshots_to_keep_ = count; }
    
    // Status
    uint64_t get_last_snapshot_slot() const { return last_snapshot_slot_; }
    uint64_t get_next_snapshot_slot() const { return next_snapshot_slot_; }
    std::chrono::system_clock::time_point get_last_snapshot_time() const { return last_snapshot_time_; }

private:
    std::shared_ptr<SnapshotManager> snapshot_manager_;
    std::unique_ptr<std::thread> service_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> should_stop_;
    
    uint64_t full_snapshot_interval_;
    uint64_t incremental_snapshot_interval_;
    bool cleanup_enabled_;
    uint64_t max_snapshots_to_keep_;
    
    std::atomic<uint64_t> last_snapshot_slot_;
    std::atomic<uint64_t> next_snapshot_slot_;
    std::chrono::system_clock::time_point last_snapshot_time_;
    
    // Service loop
    void service_loop();
    bool should_create_snapshot(uint64_t current_slot) const;
    bool should_create_full_snapshot(uint64_t current_slot) const;
    void cleanup_old_snapshots();
};

/**
 * Snapshot Streaming Service
 * 
 * Enables efficient snapshot streaming for validator bootstrapping
 * and synchronization with peer validators.
 */
class SnapshotStreamingService {
public:
    explicit SnapshotStreamingService(std::shared_ptr<SnapshotManager> snapshot_manager);
    ~SnapshotStreamingService() = default;

    // Streaming operations
    bool start_snapshot_stream(const std::string& snapshot_path, const std::string& peer_address, size_t chunk_size = 1024 * 1024);
    std::vector<SnapshotChunk> get_snapshot_chunks(const std::string& snapshot_path, size_t chunk_size = 1024 * 1024) const;
    bool receive_snapshot_chunk(const SnapshotChunk& chunk, const std::string& output_path);
    
    // Verification
    bool verify_stream_integrity(const std::vector<SnapshotChunk>& chunks) const;
    std::string calculate_stream_hash(const std::vector<SnapshotChunk>& chunks) const;
    
    // Statistics
    struct StreamingStats {
        uint64_t total_chunks_sent = 0;
        uint64_t total_chunks_received = 0;
        uint64_t total_bytes_streamed = 0;
        uint64_t average_chunk_size = 0;
        uint64_t streaming_duration_ms = 0;
        double throughput_mbps = 0.0;
    };
    
    StreamingStats get_streaming_statistics() const { return streaming_stats_; }

private:
    std::shared_ptr<SnapshotManager> snapshot_manager_;
    mutable StreamingStats streaming_stats_;
    
    // Helper methods
    std::vector<uint8_t> load_snapshot_data(const std::string& snapshot_path) const;
    bool save_snapshot_data(const std::string& output_path, const std::vector<uint8_t>& data) const;
};

} // namespace validator
} // namespace slonana