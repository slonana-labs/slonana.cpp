#pragma once

#include "common/types.h"
#include <atomic>
#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace slonana {
namespace storage {

using namespace slonana::common;

/**
 * Account state with versioning support
 */
struct AccountData {
  std::vector<uint8_t> data;
  uint64_t lamports;
  PublicKey owner;
  bool executable;
  uint64_t rent_epoch;
  
  // Versioning information
  uint64_t version;
  std::chrono::steady_clock::time_point created_at;
  std::chrono::steady_clock::time_point updated_at;
  
  AccountData() : lamports(0), executable(false), rent_epoch(0), version(0),
                  created_at(std::chrono::steady_clock::now()),
                  updated_at(std::chrono::steady_clock::now()) {}
  
  // Serialization
  std::vector<uint8_t> serialize() const;
  bool deserialize(const std::vector<uint8_t>& data);
  
  // Validation
  bool is_valid() const;
  size_t get_size() const;
};

/**
 * Account version entry for multi-version storage
 */
struct AccountVersion {
  uint64_t slot;
  uint64_t version;
  AccountData data;
  bool is_deleted;
  
  AccountVersion(uint64_t s, uint64_t v, const AccountData& d) 
      : slot(s), version(v), data(d), is_deleted(false) {}
};

/**
 * Account index for fast lookups
 */
struct AccountIndex {
  PublicKey account_key;
  uint64_t current_version;
  uint64_t current_slot;
  std::vector<std::shared_ptr<AccountVersion>> versions;
  
  AccountIndex(const PublicKey& key) : account_key(key), current_version(0), current_slot(0) {}
};

/**
 * Advanced accounts database with versioning, optimization and garbage collection
 * Compatible with Agave's account storage design
 */
class AccountsDB {
public:
  struct Configuration {
    size_t max_versions_per_account;
    size_t garbage_collection_threshold;
    std::chrono::minutes gc_interval;
    size_t index_cache_size;
    bool enable_compression;
    bool enable_snapshots;
    size_t write_batch_size;
    
    Configuration() 
        : max_versions_per_account(32)
        , garbage_collection_threshold(1000)
        , gc_interval(std::chrono::minutes(5))
        , index_cache_size(10000)
        , enable_compression(true)
        , enable_snapshots(true)
        , write_batch_size(100) {}
  };
  
  struct Statistics {
    std::atomic<size_t> total_accounts{0};
    std::atomic<size_t> total_versions{0};
    std::atomic<size_t> cache_hits{0};
    std::atomic<size_t> cache_misses{0};
    std::atomic<size_t> gc_runs{0};
    std::atomic<size_t> gc_cleaned_versions{0};
    std::atomic<size_t> index_size{0};
    std::atomic<size_t> storage_size_bytes{0};
    std::chrono::steady_clock::time_point last_gc_run;
    
    // Copy constructor
    Statistics(const Statistics& other) 
        : total_accounts(other.total_accounts.load())
        , total_versions(other.total_versions.load())
        , cache_hits(other.cache_hits.load())
        , cache_misses(other.cache_misses.load())
        , gc_runs(other.gc_runs.load())
        , gc_cleaned_versions(other.gc_cleaned_versions.load())
        , index_size(other.index_size.load())
        , storage_size_bytes(other.storage_size_bytes.load())
        , last_gc_run(other.last_gc_run) {}
    
    // Assignment operator
    Statistics& operator=(const Statistics& other) {
      if (this != &other) {
        total_accounts.store(other.total_accounts.load());
        total_versions.store(other.total_versions.load());
        cache_hits.store(other.cache_hits.load());
        cache_misses.store(other.cache_misses.load());
        gc_runs.store(other.gc_runs.load());
        gc_cleaned_versions.store(other.gc_cleaned_versions.load());
        index_size.store(other.index_size.load());
        storage_size_bytes.store(other.storage_size_bytes.load());
        last_gc_run = other.last_gc_run;
      }
      return *this;
    }
    
    // Default constructor
    Statistics() : last_gc_run(std::chrono::steady_clock::now()) {}
  };
  
  explicit AccountsDB(const Configuration& config = Configuration{});
  ~AccountsDB();
  
  // Core operations
  bool store_account(const PublicKey& account_key, const AccountData& data, uint64_t slot);
  std::optional<AccountData> load_account(const PublicKey& account_key, uint64_t slot = UINT64_MAX);
  bool delete_account(const PublicKey& account_key, uint64_t slot);
  bool account_exists(const PublicKey& account_key, uint64_t slot = UINT64_MAX);
  
  // Versioning operations  
  std::vector<AccountData> get_account_versions(const PublicKey& account_key, size_t max_versions = 10);
  std::optional<AccountData> get_account_at_slot(const PublicKey& account_key, uint64_t slot);
  bool purge_old_versions(const PublicKey& account_key, uint64_t before_slot);
  
  // Batch operations
  bool store_accounts_batch(const std::vector<std::pair<PublicKey, AccountData>>& accounts, uint64_t slot);
  std::unordered_map<PublicKey, AccountData> load_accounts_batch(const std::vector<PublicKey>& account_keys, uint64_t slot = UINT64_MAX);
  
  // Index operations
  std::vector<PublicKey> get_accounts_by_owner(const PublicKey& owner_key);
  std::vector<PublicKey> get_executable_accounts();
  size_t get_account_count() const;
  
  // Garbage collection
  void run_garbage_collection();
  void set_gc_enabled(bool enabled) { gc_enabled_ = enabled; }
  size_t get_gc_threshold() const { return config_.garbage_collection_threshold; }
  
  // Snapshot operations
  bool create_snapshot(uint64_t slot, const std::string& snapshot_path);
  bool load_from_snapshot(const std::string& snapshot_path);
  std::vector<std::string> list_snapshots() const;
  
  // Cache management
  void clear_cache();
  void warm_cache(const std::vector<PublicKey>& frequently_accessed);
  double get_cache_hit_ratio() const;
  
  // Statistics and monitoring
  Statistics get_statistics() const;
  void reset_statistics();
  
  // Configuration
  void update_configuration(const Configuration& new_config);
  const Configuration& get_configuration() const { return config_; }
  
  // Database maintenance
  bool compact_database();
  bool verify_integrity();
  void optimize_indexes();
  
private:
  Configuration config_;
  mutable Statistics stats_;
  
  // LRU Cache implementation for accounts
  struct LRUCacheEntry {
    PublicKey key;
    std::shared_ptr<AccountData> data;
    std::chrono::steady_clock::time_point access_time;
    
    LRUCacheEntry(const PublicKey& k, std::shared_ptr<AccountData> d)
        : key(k), data(d), access_time(std::chrono::steady_clock::now()) {}
  };
  
  // Account storage
  std::unordered_map<PublicKey, std::shared_ptr<AccountIndex>> account_index_;
  mutable std::shared_mutex index_mutex_;
  
  // LRU Cache for frequently accessed accounts
  mutable std::list<LRUCacheEntry> cache_list_;
  mutable std::unordered_map<PublicKey, std::list<LRUCacheEntry>::iterator> cache_map_;
  mutable std::mutex cache_mutex_;
  
  // Garbage collection
  std::atomic<bool> gc_enabled_{true};
  std::thread gc_thread_;
  std::atomic<bool> should_stop_gc_{false};
  
  // Background operations
  void gc_worker_loop();
  void cleanup_expired_versions();
  bool is_version_eligible_for_gc(const AccountVersion& version, uint64_t current_slot);
  
  // Cache operations
  void update_cache(const PublicKey& account_key, std::shared_ptr<AccountData> data);
  std::optional<std::shared_ptr<AccountData>> get_from_cache(const PublicKey& account_key);
  void evict_cache_if_needed();
  
  // Index operations
  std::shared_ptr<AccountIndex> get_or_create_index(const PublicKey& account_key);
  void update_index_statistics();
  
  // Serialization helpers
  std::vector<uint8_t> serialize_account_version(const AccountVersion& version);
  std::optional<AccountVersion> deserialize_account_version(const std::vector<uint8_t>& data);
  
  // Compression (if enabled)
  std::vector<uint8_t> compress_data(const std::vector<uint8_t>& data);
  std::vector<uint8_t> decompress_data(const std::vector<uint8_t>& compressed_data);
  
  // Snapshot implementation
  bool write_snapshot_metadata(const std::string& snapshot_path, uint64_t slot);
  bool read_snapshot_metadata(const std::string& snapshot_path, uint64_t& slot);
};

/**
 * Account storage manager - high-level interface for account operations
 */
class AccountStorageManager {
public:
  explicit AccountStorageManager(const std::string& storage_path);
  ~AccountStorageManager();
  
  // Initialize/shutdown
  bool initialize();
  bool shutdown();
  
  // Account operations
  bool store_account(const PublicKey& account_key, const AccountData& data, uint64_t slot);
  std::optional<AccountData> load_account(const PublicKey& account_key);
  bool account_exists(const PublicKey& account_key);
  
  // Bulk operations
  bool store_accounts_batch(const std::vector<std::pair<PublicKey, AccountData>>& accounts, uint64_t slot);
  
  // Snapshot management
  bool create_checkpoint(uint64_t slot);
  bool restore_from_checkpoint(uint64_t slot);
  
  // Statistics
  AccountsDB::Statistics get_statistics() const;
  
private:
  std::string storage_path_;
  std::unique_ptr<AccountsDB> accounts_db_;
  bool initialized_;
};

} // namespace storage
} // namespace slonana