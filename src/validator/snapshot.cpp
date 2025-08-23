#include "validator/snapshot.h"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <random>
#include <thread>
#include <chrono>
#include <iostream>
#include <cstring>

namespace slonana {
namespace validator {

namespace fs = std::filesystem;

// SnapshotManager Implementation

SnapshotManager::SnapshotManager(const std::string& snapshot_dir)
    : snapshot_dir_(snapshot_dir)
    , compression_enabled_(true)
    , max_chunk_size_(1024 * 1024) // 1MB default
    , auto_snapshot_interval_(1000) // Every 1000 slots
{
    // Ensure snapshot directory exists
    if (!fs::exists(snapshot_dir_)) {
        fs::create_directories(snapshot_dir_);
    }
    
    std::cout << "Snapshot Manager: Initialized at " << snapshot_dir_ << std::endl;
}

SnapshotManager::~SnapshotManager() = default;

bool SnapshotManager::create_full_snapshot(uint64_t slot, const std::string& ledger_path) {
    auto start_time = std::chrono::steady_clock::now();
    
    std::cout << "Snapshot Manager: Creating full snapshot for slot " << slot << std::endl;
    
    try {
        // Generate snapshot filename
        std::string snapshot_file = generate_snapshot_filename(slot, false);
        std::string snapshot_path = snapshot_dir_ + "/" + snapshot_file;
        
        // Create snapshot metadata
        SnapshotMetadata metadata;
        metadata.slot = slot;
        metadata.block_hash = "mock_block_hash_" + std::to_string(slot);
        metadata.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        metadata.lamports_total = 1000000000000ULL; // Mock 1000 SOL
        metadata.account_count = 10000; // Mock account count
        metadata.version = "1.0.0";
        metadata.is_incremental = false;
        metadata.base_slot = 0;
        
        // Simulate account data collection
        std::vector<AccountSnapshot> accounts;
        for (uint64_t i = 0; i < 100; ++i) { // Mock 100 accounts for demo
            AccountSnapshot account;
            account.pubkey.resize(32);
            std::fill(account.pubkey.begin(), account.pubkey.end(), static_cast<uint8_t>(i % 256));
            account.lamports = 1000000 + i * 1000; // Mock balance
            account.data = std::vector<uint8_t>(64, static_cast<uint8_t>(i)); // Mock data
            account.owner.resize(32);
            std::fill(account.owner.begin(), account.owner.end(), 0x01); // System program
            account.executable = false;
            account.rent_epoch = 200 + i;
            accounts.push_back(account);
        }
        
        // Serialize snapshot data
        std::ofstream file(snapshot_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to create snapshot file: " << snapshot_path << std::endl;
            return false;
        }
        
        // Write metadata
        auto metadata_bytes = serialize_metadata(metadata);
        uint32_t metadata_size = static_cast<uint32_t>(metadata_bytes.size());
        file.write(reinterpret_cast<const char*>(&metadata_size), sizeof(metadata_size));
        file.write(reinterpret_cast<const char*>(metadata_bytes.data()), metadata_bytes.size());
        
        // Write accounts
        uint32_t account_count = static_cast<uint32_t>(accounts.size());
        file.write(reinterpret_cast<const char*>(&account_count), sizeof(account_count));
        
        size_t total_size = 0;
        for (const auto& account : accounts) {
            auto account_bytes = serialize_account(account);
            uint32_t account_size = static_cast<uint32_t>(account_bytes.size());
            file.write(reinterpret_cast<const char*>(&account_size), sizeof(account_size));
            file.write(reinterpret_cast<const char*>(account_bytes.data()), account_bytes.size());
            total_size += account_bytes.size();
        }
        
        file.close();
        
        // Update metadata with actual sizes
        auto file_size = fs::file_size(snapshot_path);
        metadata.compressed_size = file_size;
        metadata.uncompressed_size = total_size;
        
        // Update statistics
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        double duration_ms = duration.count() / 1000.0;
        
        stats_.total_snapshots_created++;
        stats_.total_bytes_written += file_size;
        stats_.average_creation_time_ms = 
            (stats_.average_creation_time_ms * (stats_.total_snapshots_created - 1) + duration_ms) / 
            stats_.total_snapshots_created;
        stats_.last_snapshot_slot = slot;
        stats_.last_snapshot_time = std::chrono::system_clock::now();
        
        std::cout << "Snapshot Manager: Full snapshot created successfully" << std::endl;
        std::cout << "  File: " << snapshot_file << std::endl;
        std::cout << "  Size: " << file_size << " bytes" << std::endl;
        std::cout << "  Accounts: " << accounts.size() << std::endl;
        std::cout << "  Duration: " << duration_ms << "ms" << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Snapshot Manager: Failed to create snapshot: " << e.what() << std::endl;
        return false;
    }
}

bool SnapshotManager::create_incremental_snapshot(uint64_t slot, uint64_t base_slot, const std::string& ledger_path) {
    auto start_time = std::chrono::steady_clock::now();
    
    std::cout << "Snapshot Manager: Creating incremental snapshot for slot " << slot 
              << " (base: " << base_slot << ")" << std::endl;
    
    try {
        // Generate snapshot filename
        std::string snapshot_file = generate_snapshot_filename(slot, true, base_slot);
        std::string snapshot_path = snapshot_dir_ + "/" + snapshot_file;
        
        // Create incremental snapshot metadata
        SnapshotMetadata metadata;
        metadata.slot = slot;
        metadata.block_hash = "mock_incremental_hash_" + std::to_string(slot);
        metadata.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        metadata.lamports_total = 0; // Only changes
        metadata.account_count = 50; // Mock changed accounts
        metadata.version = "1.0.0";
        metadata.is_incremental = true;
        metadata.base_slot = base_slot;
        
        // Simulate changed accounts (smaller set)
        std::vector<AccountSnapshot> changed_accounts;
        for (uint64_t i = 0; i < 20; ++i) { // Mock 20 changed accounts
            AccountSnapshot account;
            account.pubkey.resize(32);
            std::fill(account.pubkey.begin(), account.pubkey.end(), static_cast<uint8_t>(i % 256));
            account.lamports = 2000000 + i * 1500; // Different balances
            account.data = std::vector<uint8_t>(32, static_cast<uint8_t>(i + 128)); // Different data
            account.owner.resize(32);
            std::fill(account.owner.begin(), account.owner.end(), 0x01);
            account.executable = false;
            account.rent_epoch = 300 + i;
            changed_accounts.push_back(account);
        }
        
        // Write incremental snapshot
        std::ofstream file(snapshot_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to create incremental snapshot file: " << snapshot_path << std::endl;
            return false;
        }
        
        // Write metadata
        auto metadata_bytes = serialize_metadata(metadata);
        uint32_t metadata_size = static_cast<uint32_t>(metadata_bytes.size());
        file.write(reinterpret_cast<const char*>(&metadata_size), sizeof(metadata_size));
        file.write(reinterpret_cast<const char*>(metadata_bytes.data()), metadata_bytes.size());
        
        // Write changed accounts
        uint32_t account_count = static_cast<uint32_t>(changed_accounts.size());
        file.write(reinterpret_cast<const char*>(&account_count), sizeof(account_count));
        
        for (const auto& account : changed_accounts) {
            auto account_bytes = serialize_account(account);
            uint32_t account_size = static_cast<uint32_t>(account_bytes.size());
            file.write(reinterpret_cast<const char*>(&account_size), sizeof(account_size));
            file.write(reinterpret_cast<const char*>(account_bytes.data()), account_bytes.size());
        }
        
        file.close();
        
        auto file_size = fs::file_size(snapshot_path);
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        double duration_ms = duration.count() / 1000.0;
        
        // Update statistics
        stats_.total_snapshots_created++;
        stats_.total_bytes_written += file_size;
        stats_.average_creation_time_ms = 
            (stats_.average_creation_time_ms * (stats_.total_snapshots_created - 1) + duration_ms) / 
            stats_.total_snapshots_created;
        stats_.last_snapshot_slot = slot;
        stats_.last_snapshot_time = std::chrono::system_clock::now();
        
        std::cout << "Snapshot Manager: Incremental snapshot created successfully" << std::endl;
        std::cout << "  File: " << snapshot_file << std::endl;
        std::cout << "  Size: " << file_size << " bytes" << std::endl;
        std::cout << "  Changed accounts: " << changed_accounts.size() << std::endl;
        std::cout << "  Duration: " << duration_ms << "ms" << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Snapshot Manager: Failed to create incremental snapshot: " << e.what() << std::endl;
        return false;
    }
}

bool SnapshotManager::restore_from_snapshot(const std::string& snapshot_path, const std::string& ledger_path) {
    auto start_time = std::chrono::steady_clock::now();
    
    std::cout << "Snapshot Manager: Restoring from snapshot " << snapshot_path << std::endl;
    
    try {
        if (!fs::exists(snapshot_path)) {
            std::cerr << "Snapshot file does not exist: " << snapshot_path << std::endl;
            return false;
        }
        
        std::ifstream file(snapshot_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open snapshot file: " << snapshot_path << std::endl;
            return false;
        }
        
        // Read metadata
        uint32_t metadata_size;
        file.read(reinterpret_cast<char*>(&metadata_size), sizeof(metadata_size));
        
        std::vector<uint8_t> metadata_bytes(metadata_size);
        file.read(reinterpret_cast<char*>(metadata_bytes.data()), metadata_size);
        
        SnapshotMetadata metadata = deserialize_metadata(metadata_bytes);
        
        std::cout << "Snapshot metadata:" << std::endl;
        std::cout << "  Slot: " << metadata.slot << std::endl;
        std::cout << "  Block hash: " << metadata.block_hash << std::endl;
        std::cout << "  Account count: " << metadata.account_count << std::endl;
        std::cout << "  Is incremental: " << (metadata.is_incremental ? "yes" : "no") << std::endl;
        if (metadata.is_incremental) {
            std::cout << "  Base slot: " << metadata.base_slot << std::endl;
        }
        
        // Read accounts
        uint32_t account_count;
        file.read(reinterpret_cast<char*>(&account_count), sizeof(account_count));
        
        std::vector<AccountSnapshot> accounts;
        accounts.reserve(account_count);
        
        for (uint32_t i = 0; i < account_count; ++i) {
            uint32_t account_size;
            file.read(reinterpret_cast<char*>(&account_size), sizeof(account_size));
            
            std::vector<uint8_t> account_bytes(account_size);
            file.read(reinterpret_cast<char*>(account_bytes.data()), account_size);
            
            size_t offset = 0;
            AccountSnapshot account = deserialize_account(account_bytes, offset);
            accounts.push_back(account);
        }
        
        file.close();
        
        // Simulate restoration process
        // In a real implementation, this would restore the ledger state
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        double duration_ms = duration.count() / 1000.0;
        
        // Update statistics
        stats_.total_snapshots_restored++;
        stats_.total_bytes_read += fs::file_size(snapshot_path);
        stats_.average_restoration_time_ms = 
            (stats_.average_restoration_time_ms * (stats_.total_snapshots_restored - 1) + duration_ms) / 
            stats_.total_snapshots_restored;
        
        std::cout << "Snapshot Manager: Restoration completed successfully" << std::endl;
        std::cout << "  Restored accounts: " << accounts.size() << std::endl;
        std::cout << "  Duration: " << duration_ms << "ms" << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Snapshot Manager: Failed to restore snapshot: " << e.what() << std::endl;
        return false;
    }
}

std::vector<AccountSnapshot> SnapshotManager::load_accounts_from_snapshot(const std::string& snapshot_path) {
    std::vector<AccountSnapshot> accounts;
    
    try {
        if (!fs::exists(snapshot_path)) {
            return accounts;
        }
        
        std::ifstream file(snapshot_path, std::ios::binary);
        if (!file.is_open()) {
            return accounts;
        }
        
        // Skip metadata
        uint32_t metadata_size;
        file.read(reinterpret_cast<char*>(&metadata_size), sizeof(metadata_size));
        file.seekg(metadata_size, std::ios::cur);
        
        // Read accounts
        uint32_t account_count;
        file.read(reinterpret_cast<char*>(&account_count), sizeof(account_count));
        
        accounts.reserve(account_count);
        
        for (uint32_t i = 0; i < account_count; ++i) {
            uint32_t account_size;
            file.read(reinterpret_cast<char*>(&account_size), sizeof(account_size));
            
            std::vector<uint8_t> account_bytes(account_size);
            file.read(reinterpret_cast<char*>(account_bytes.data()), account_size);
            
            size_t offset = 0;
            AccountSnapshot account = deserialize_account(account_bytes, offset);
            accounts.push_back(account);
        }
        
        file.close();
    } catch (const std::exception& e) {
        std::cerr << "Failed to load accounts from snapshot: " << e.what() << std::endl;
    }
    
    return accounts;
}

std::vector<SnapshotMetadata> SnapshotManager::list_available_snapshots() const {
    std::vector<SnapshotMetadata> snapshots;
    
    try {
        for (const auto& entry : fs::directory_iterator(snapshot_dir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".snapshot") {
                std::string snapshot_path = entry.path().string();
                
                std::ifstream file(snapshot_path, std::ios::binary);
                if (file.is_open()) {
                    uint32_t metadata_size;
                    file.read(reinterpret_cast<char*>(&metadata_size), sizeof(metadata_size));
                    
                    std::vector<uint8_t> metadata_bytes(metadata_size);
                    file.read(reinterpret_cast<char*>(metadata_bytes.data()), metadata_size);
                    
                    SnapshotMetadata metadata = deserialize_metadata(metadata_bytes);
                    snapshots.push_back(metadata);
                    
                    file.close();
                }
            }
        }
        
        // Sort by slot
        std::sort(snapshots.begin(), snapshots.end(), 
                 [](const SnapshotMetadata& a, const SnapshotMetadata& b) {
                     return a.slot < b.slot;
                 });
    } catch (const std::exception& e) {
        std::cerr << "Failed to list snapshots: " << e.what() << std::endl;
    }
    
    return snapshots;
}

SnapshotMetadata SnapshotManager::get_latest_snapshot() const {
    auto snapshots = list_available_snapshots();
    if (snapshots.empty()) {
        return SnapshotMetadata{}; // Return empty metadata
    }
    return snapshots.back(); // Latest by slot
}

bool SnapshotManager::delete_old_snapshots(uint64_t keep_count) {
    try {
        auto snapshots = list_available_snapshots();
        if (snapshots.size() <= keep_count) {
            return true; // Nothing to delete
        }
        
        size_t to_delete = snapshots.size() - keep_count;
        for (size_t i = 0; i < to_delete; ++i) {
            std::string filename = generate_snapshot_filename(
                snapshots[i].slot, 
                snapshots[i].is_incremental, 
                snapshots[i].base_slot
            );
            std::string snapshot_path = snapshot_dir_ + "/" + filename;
            
            if (fs::exists(snapshot_path)) {
                fs::remove(snapshot_path);
                std::cout << "Deleted old snapshot: " << filename << std::endl;
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to delete old snapshots: " << e.what() << std::endl;
        return false;
    }
}

bool SnapshotManager::verify_snapshot_integrity(const std::string& snapshot_path) {
    try {
        if (!fs::exists(snapshot_path)) {
            return false;
        }
        
        // Basic verification - check if file can be read
        std::ifstream file(snapshot_path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        // Verify metadata can be read
        uint32_t metadata_size;
        file.read(reinterpret_cast<char*>(&metadata_size), sizeof(metadata_size));
        
        if (metadata_size == 0 || metadata_size > 1024 * 1024) { // Sanity check
            return false;
        }
        
        std::vector<uint8_t> metadata_bytes(metadata_size);
        file.read(reinterpret_cast<char*>(metadata_bytes.data()), metadata_size);
        
        // Try to deserialize metadata
        SnapshotMetadata metadata = deserialize_metadata(metadata_bytes);
        
        // Basic validation
        if (metadata.slot == 0 || metadata.version.empty()) {
            return false;
        }
        
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Snapshot verification failed: " << e.what() << std::endl;
        return false;
    }
}

std::string SnapshotManager::calculate_snapshot_hash(const std::string& snapshot_path) {
    try {
        std::ifstream file(snapshot_path, std::ios::binary);
        if (!file.is_open()) {
            return "";
        }
        
        std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        file.close();
        
        return calculate_hash(buffer);
    } catch (const std::exception& e) {
        std::cerr << "Failed to calculate snapshot hash: " << e.what() << std::endl;
        return "";
    }
}

// Private helper methods

std::string SnapshotManager::generate_snapshot_filename(uint64_t slot, bool is_incremental, uint64_t base_slot) const {
    std::stringstream ss;
    ss << "snapshot-" << std::setfill('0') << std::setw(12) << slot;
    if (is_incremental) {
        ss << "-incremental-" << std::setfill('0') << std::setw(12) << base_slot;
    }
    ss << ".snapshot";
    return ss.str();
}

std::string SnapshotManager::calculate_hash(const std::vector<uint8_t>& data) const {
    // Simple hash calculation (in production, use SHA-256 or similar)
    uint64_t hash = 0;
    for (uint8_t byte : data) {
        hash = hash * 31 + byte;
    }
    
    std::stringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

std::vector<uint8_t> SnapshotManager::serialize_metadata(const SnapshotMetadata& metadata) const {
    std::vector<uint8_t> result;
    
    // This is a simplified serialization - in production use a proper format
    result.insert(result.end(), reinterpret_cast<const uint8_t*>(&metadata.slot), 
                  reinterpret_cast<const uint8_t*>(&metadata.slot) + sizeof(metadata.slot));
    
    // Store string lengths and data
    uint32_t hash_len = static_cast<uint32_t>(metadata.block_hash.length());
    result.insert(result.end(), reinterpret_cast<const uint8_t*>(&hash_len), 
                  reinterpret_cast<const uint8_t*>(&hash_len) + sizeof(hash_len));
    result.insert(result.end(), metadata.block_hash.begin(), metadata.block_hash.end());
    
    result.insert(result.end(), reinterpret_cast<const uint8_t*>(&metadata.timestamp), 
                  reinterpret_cast<const uint8_t*>(&metadata.timestamp) + sizeof(metadata.timestamp));
    result.insert(result.end(), reinterpret_cast<const uint8_t*>(&metadata.lamports_total), 
                  reinterpret_cast<const uint8_t*>(&metadata.lamports_total) + sizeof(metadata.lamports_total));
    result.insert(result.end(), reinterpret_cast<const uint8_t*>(&metadata.account_count), 
                  reinterpret_cast<const uint8_t*>(&metadata.account_count) + sizeof(metadata.account_count));
    
    uint32_t version_len = static_cast<uint32_t>(metadata.version.length());
    result.insert(result.end(), reinterpret_cast<const uint8_t*>(&version_len), 
                  reinterpret_cast<const uint8_t*>(&version_len) + sizeof(version_len));
    result.insert(result.end(), metadata.version.begin(), metadata.version.end());
    
    uint8_t is_incremental = metadata.is_incremental ? 1 : 0;
    result.push_back(is_incremental);
    
    result.insert(result.end(), reinterpret_cast<const uint8_t*>(&metadata.base_slot), 
                  reinterpret_cast<const uint8_t*>(&metadata.base_slot) + sizeof(metadata.base_slot));
    
    return result;
}

SnapshotMetadata SnapshotManager::deserialize_metadata(const std::vector<uint8_t>& data) const {
    SnapshotMetadata metadata;
    size_t offset = 0;
    
    // Read slot
    std::memcpy(&metadata.slot, data.data() + offset, sizeof(metadata.slot));
    offset += sizeof(metadata.slot);
    
    // Read block hash
    uint32_t hash_len;
    std::memcpy(&hash_len, data.data() + offset, sizeof(hash_len));
    offset += sizeof(hash_len);
    metadata.block_hash = std::string(reinterpret_cast<const char*>(data.data() + offset), hash_len);
    offset += hash_len;
    
    // Read other fields
    std::memcpy(&metadata.timestamp, data.data() + offset, sizeof(metadata.timestamp));
    offset += sizeof(metadata.timestamp);
    std::memcpy(&metadata.lamports_total, data.data() + offset, sizeof(metadata.lamports_total));
    offset += sizeof(metadata.lamports_total);
    std::memcpy(&metadata.account_count, data.data() + offset, sizeof(metadata.account_count));
    offset += sizeof(metadata.account_count);
    
    // Read version
    uint32_t version_len;
    std::memcpy(&version_len, data.data() + offset, sizeof(version_len));
    offset += sizeof(version_len);
    metadata.version = std::string(reinterpret_cast<const char*>(data.data() + offset), version_len);
    offset += version_len;
    
    // Read is_incremental
    uint8_t is_incremental;
    std::memcpy(&is_incremental, data.data() + offset, sizeof(is_incremental));
    metadata.is_incremental = (is_incremental == 1);
    offset += sizeof(is_incremental);
    
    // Read base_slot
    std::memcpy(&metadata.base_slot, data.data() + offset, sizeof(metadata.base_slot));
    
    return metadata;
}

std::vector<uint8_t> SnapshotManager::serialize_account(const AccountSnapshot& account) const {
    std::vector<uint8_t> result;
    
    // Serialize pubkey (32 bytes)
    result.insert(result.end(), account.pubkey.begin(), account.pubkey.end());
    
    // Serialize lamports
    result.insert(result.end(), reinterpret_cast<const uint8_t*>(&account.lamports), 
                  reinterpret_cast<const uint8_t*>(&account.lamports) + sizeof(account.lamports));
    
    // Serialize data length and data
    uint32_t data_len = static_cast<uint32_t>(account.data.size());
    result.insert(result.end(), reinterpret_cast<const uint8_t*>(&data_len), 
                  reinterpret_cast<const uint8_t*>(&data_len) + sizeof(data_len));
    result.insert(result.end(), account.data.begin(), account.data.end());
    
    // Serialize owner (32 bytes)
    result.insert(result.end(), account.owner.begin(), account.owner.end());
    
    // Serialize executable flag
    uint8_t executable = account.executable ? 1 : 0;
    result.push_back(executable);
    
    // Serialize rent_epoch
    result.insert(result.end(), reinterpret_cast<const uint8_t*>(&account.rent_epoch), 
                  reinterpret_cast<const uint8_t*>(&account.rent_epoch) + sizeof(account.rent_epoch));
    
    return result;
}

AccountSnapshot SnapshotManager::deserialize_account(const std::vector<uint8_t>& data, size_t& offset) const {
    AccountSnapshot account;
    
    // Read pubkey (32 bytes)
    account.pubkey.resize(32);
    std::memcpy(account.pubkey.data(), data.data() + offset, 32);
    offset += 32;
    
    // Read lamports
    std::memcpy(&account.lamports, data.data() + offset, sizeof(account.lamports));
    offset += sizeof(account.lamports);
    
    // Read data
    uint32_t data_len;
    std::memcpy(&data_len, data.data() + offset, sizeof(data_len));
    offset += sizeof(data_len);
    
    account.data.resize(data_len);
    std::memcpy(account.data.data(), data.data() + offset, data_len);
    offset += data_len;
    
    // Read owner (32 bytes)
    account.owner.resize(32);
    std::memcpy(account.owner.data(), data.data() + offset, 32);
    offset += 32;
    
    // Read executable flag
    uint8_t executable;
    std::memcpy(&executable, data.data() + offset, sizeof(executable));
    account.executable = (executable == 1);
    offset += sizeof(executable);
    
    // Read rent_epoch
    std::memcpy(&account.rent_epoch, data.data() + offset, sizeof(account.rent_epoch));
    offset += sizeof(account.rent_epoch);
    
    return account;
}

// AutoSnapshotService Implementation

AutoSnapshotService::AutoSnapshotService(std::shared_ptr<SnapshotManager> snapshot_manager)
    : snapshot_manager_(snapshot_manager)
    , running_(false)
    , should_stop_(false)
    , full_snapshot_interval_(10000) // Every 10,000 slots
    , incremental_snapshot_interval_(1000) // Every 1,000 slots
    , cleanup_enabled_(true)
    , max_snapshots_to_keep_(10)
    , last_snapshot_slot_(0)
    , next_snapshot_slot_(0)
{
    std::cout << "Auto Snapshot Service: Initialized" << std::endl;
}

AutoSnapshotService::~AutoSnapshotService() {
    stop();
}

void AutoSnapshotService::start(uint64_t interval_slots) {
    if (running_) {
        return;
    }
    
    incremental_snapshot_interval_ = interval_slots;
    should_stop_ = false;
    running_ = true;
    
    service_thread_ = std::make_unique<std::thread>(&AutoSnapshotService::service_loop, this);
    
    std::cout << "Auto Snapshot Service: Started" << std::endl;
    std::cout << "  Full snapshot interval: " << full_snapshot_interval_ << " slots" << std::endl;
    std::cout << "  Incremental snapshot interval: " << incremental_snapshot_interval_ << " slots" << std::endl;
}

void AutoSnapshotService::stop() {
    if (!running_) {
        return;
    }
    
    should_stop_ = true;
    running_ = false;
    
    if (service_thread_ && service_thread_->joinable()) {
        service_thread_->join();
    }
    
    std::cout << "Auto Snapshot Service: Stopped" << std::endl;
}

void AutoSnapshotService::service_loop() {
    std::cout << "Auto Snapshot Service: Service loop started" << std::endl;
    
    uint64_t current_slot = 1000; // Mock starting slot
    
    while (!should_stop_) {
        try {
            // Simulate slot progression
            current_slot += 1;
            
            // Check if we should create a snapshot
            if (should_create_snapshot(current_slot)) {
                bool is_full = should_create_full_snapshot(current_slot);
                
                if (is_full) {
                    std::cout << "Auto Snapshot Service: Creating full snapshot at slot " << current_slot << std::endl;
                    if (snapshot_manager_->create_full_snapshot(current_slot, "/tmp/mock_ledger")) {
                        last_snapshot_slot_ = current_slot;
                        last_snapshot_time_ = std::chrono::system_clock::now();
                    }
                } else {
                    uint64_t base_slot = last_snapshot_slot_;
                    std::cout << "Auto Snapshot Service: Creating incremental snapshot at slot " << current_slot 
                              << " (base: " << base_slot << ")" << std::endl;
                    if (snapshot_manager_->create_incremental_snapshot(current_slot, base_slot, "/tmp/mock_ledger")) {
                        last_snapshot_slot_ = current_slot;
                        last_snapshot_time_ = std::chrono::system_clock::now();
                    }
                }
                
                // Cleanup old snapshots if enabled
                if (cleanup_enabled_) {
                    cleanup_old_snapshots();
                }
                
                next_snapshot_slot_ = current_slot + incremental_snapshot_interval_;
            }
            
            // Sleep for a bit (simulating slot time)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } catch (const std::exception& e) {
            std::cerr << "Auto Snapshot Service: Error in service loop: " << e.what() << std::endl;
        }
        
        // For demo purposes, stop after creating a few snapshots
        if (current_slot > 1050) {
            break;
        }
    }
    
    std::cout << "Auto Snapshot Service: Service loop ended" << std::endl;
}

bool AutoSnapshotService::should_create_snapshot(uint64_t current_slot) const {
    if (last_snapshot_slot_ == 0) {
        return true; // First snapshot
    }
    
    return (current_slot - last_snapshot_slot_) >= incremental_snapshot_interval_;
}

bool AutoSnapshotService::should_create_full_snapshot(uint64_t current_slot) const {
    if (last_snapshot_slot_ == 0) {
        return true; // First snapshot should be full
    }
    
    return (current_slot - last_snapshot_slot_) >= full_snapshot_interval_;
}

void AutoSnapshotService::cleanup_old_snapshots() {
    try {
        snapshot_manager_->delete_old_snapshots(max_snapshots_to_keep_);
    } catch (const std::exception& e) {
        std::cerr << "Auto Snapshot Service: Failed to cleanup old snapshots: " << e.what() << std::endl;
    }
}

// SnapshotStreamingService Implementation

SnapshotStreamingService::SnapshotStreamingService(std::shared_ptr<SnapshotManager> snapshot_manager)
    : snapshot_manager_(snapshot_manager) {
    std::cout << "Snapshot Streaming Service: Initialized" << std::endl;
}

bool SnapshotStreamingService::start_snapshot_stream(const std::string& snapshot_path, const std::string& peer_address, size_t chunk_size) {
    auto start_time = std::chrono::steady_clock::now();
    
    std::cout << "Snapshot Streaming Service: Starting stream of " << snapshot_path 
              << " to " << peer_address << std::endl;
    
    try {
        // Generate chunks
        auto chunks = get_snapshot_chunks(snapshot_path, chunk_size);
        
        // Simulate streaming chunks
        for (const auto& chunk : chunks) {
            std::cout << "Streaming chunk " << chunk.chunk_index + 1 
                      << "/" << chunk.total_chunks 
                      << " (" << chunk.compressed_data.size() << " bytes)" << std::endl;
            
            // Simulate network delay
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        // Update statistics
        streaming_stats_.total_chunks_sent += chunks.size();
        streaming_stats_.streaming_duration_ms += duration.count();
        
        size_t total_bytes = 0;
        for (const auto& chunk : chunks) {
            total_bytes += chunk.compressed_data.size();
        }
        streaming_stats_.total_bytes_streamed += total_bytes;
        
        if (chunks.size() > 0) {
            streaming_stats_.average_chunk_size = total_bytes / chunks.size();
        }
        
        // Calculate throughput (MB/s)
        if (duration.count() > 0) {
            streaming_stats_.throughput_mbps = 
                (static_cast<double>(total_bytes) / (1024 * 1024)) / (duration.count() / 1000.0);
        }
        
        std::cout << "Snapshot Streaming Service: Stream completed successfully" << std::endl;
        std::cout << "  Chunks sent: " << chunks.size() << std::endl;
        std::cout << "  Total bytes: " << total_bytes << std::endl;
        std::cout << "  Duration: " << duration.count() << "ms" << std::endl;
        std::cout << "  Throughput: " << streaming_stats_.throughput_mbps << " MB/s" << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Snapshot Streaming Service: Failed to stream snapshot: " << e.what() << std::endl;
        return false;
    }
}

std::vector<SnapshotChunk> SnapshotStreamingService::get_snapshot_chunks(const std::string& snapshot_path, size_t chunk_size) const {
    std::vector<SnapshotChunk> chunks;
    
    try {
        auto snapshot_data = load_snapshot_data(snapshot_path);
        if (snapshot_data.empty()) {
            return chunks;
        }
        
        size_t total_chunks = (snapshot_data.size() + chunk_size - 1) / chunk_size;
        
        for (size_t i = 0; i < total_chunks; ++i) {
            SnapshotChunk chunk;
            chunk.chunk_index = i;
            chunk.total_chunks = total_chunks;
            
            size_t start_offset = i * chunk_size;
            size_t end_offset = std::min(start_offset + chunk_size, snapshot_data.size());
            
            chunk.compressed_data.assign(
                snapshot_data.begin() + start_offset,
                snapshot_data.begin() + end_offset
            );
            
            // Calculate chunk hash
            chunk.chunk_hash = snapshot_manager_->calculate_snapshot_hash(snapshot_path);
            
            chunks.push_back(chunk);
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to create snapshot chunks: " << e.what() << std::endl;
    }
    
    return chunks;
}

bool SnapshotStreamingService::receive_snapshot_chunk(const SnapshotChunk& chunk, const std::string& output_path) {
    try {
        std::ofstream file(output_path + ".part" + std::to_string(chunk.chunk_index), 
                          std::ios::binary | std::ios::app);
        if (!file.is_open()) {
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(chunk.compressed_data.data()), 
                  chunk.compressed_data.size());
        file.close();
        
        streaming_stats_.total_chunks_received++;
        streaming_stats_.total_bytes_streamed += chunk.compressed_data.size();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to receive snapshot chunk: " << e.what() << std::endl;
        return false;
    }
}

std::vector<uint8_t> SnapshotStreamingService::load_snapshot_data(const std::string& snapshot_path) const {
    try {
        std::ifstream file(snapshot_path, std::ios::binary);
        if (!file.is_open()) {
            return {};
        }
        
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
    } catch (const std::exception& e) {
        std::cerr << "Failed to load snapshot data: " << e.what() << std::endl;
        return {};
    }
}

} // namespace validator
} // namespace slonana