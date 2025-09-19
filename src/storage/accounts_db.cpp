#include "storage/accounts_db.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <openssl/evp.h>
#include <zlib.h>

namespace slonana {
namespace storage {

// AccountData implementation
std::vector<uint8_t> AccountData::serialize() const {
  std::vector<uint8_t> result;
  
  // Serialize lamports (8 bytes)
  for (int i = 0; i < 8; ++i) {
    result.push_back(static_cast<uint8_t>((lamports >> (i * 8)) & 0xFF));
  }
  
  // Serialize owner (32 bytes)
  result.insert(result.end(), owner.begin(), owner.end());
  
  // Serialize executable (1 byte)
  result.push_back(executable ? 1 : 0);
  
  // Serialize rent_epoch (8 bytes)
  for (int i = 0; i < 8; ++i) {
    result.push_back(static_cast<uint8_t>((rent_epoch >> (i * 8)) & 0xFF));
  }
  
  // Serialize version (8 bytes)
  for (int i = 0; i < 8; ++i) {
    result.push_back(static_cast<uint8_t>((version >> (i * 8)) & 0xFF));
  }
  
  // Serialize data length (4 bytes)
  uint32_t data_len = data.size();
  for (int i = 0; i < 4; ++i) {
    result.push_back(static_cast<uint8_t>((data_len >> (i * 8)) & 0xFF));
  }
  
  // Serialize data
  result.insert(result.end(), data.begin(), data.end());
  
  return result;
}

bool AccountData::deserialize(const std::vector<uint8_t>& raw_data) {
  if (raw_data.size() < 61) { // Minimum size: 8+32+1+8+8+4 = 61 bytes
    return false;
  }
  
  size_t offset = 0;
  
  // Deserialize lamports
  lamports = 0;
  for (int i = 0; i < 8; ++i) {
    lamports |= static_cast<uint64_t>(raw_data[offset + i]) << (i * 8);
  }
  offset += 8;
  
  // Deserialize owner
  owner.assign(raw_data.begin() + offset, raw_data.begin() + offset + 32);
  offset += 32;
  
  // Deserialize executable
  executable = raw_data[offset] != 0;
  offset += 1;
  
  // Deserialize rent_epoch
  rent_epoch = 0;
  for (int i = 0; i < 8; ++i) {
    rent_epoch |= static_cast<uint64_t>(raw_data[offset + i]) << (i * 8);
  }
  offset += 8;
  
  // Deserialize version
  version = 0;
  for (int i = 0; i < 8; ++i) {
    version |= static_cast<uint64_t>(raw_data[offset + i]) << (i * 8);
  }
  offset += 8;
  
  // Deserialize data length
  uint32_t data_len = 0;
  for (int i = 0; i < 4; ++i) {
    data_len |= static_cast<uint32_t>(raw_data[offset + i]) << (i * 8);
  }
  offset += 4;
  
  // Deserialize data
  if (offset + data_len > raw_data.size()) {
    return false;
  }
  
  data.assign(raw_data.begin() + offset, raw_data.begin() + offset + data_len);
  
  return true;
}

bool AccountData::is_valid() const {
  // Basic validation checks
  if (owner.size() != 32) {
    return false;
  }
  
  // Reasonable data size limit (10MB)
  if (data.size() > 10 * 1024 * 1024) {
    return false;
  }
  
  // Version should be reasonable
  if (version > 1000000) {
    return false;
  }
  
  // Validate lamports (should not be negative or unreasonably large)
  // SOL has 9 decimal places, so max reasonable is ~500M SOL = 500M * 10^9 lamports
  const uint64_t MAX_REASONABLE_LAMPORTS = 500000000ULL * 1000000000ULL;
  if (lamports > MAX_REASONABLE_LAMPORTS) {
    return false;
  }
  
  // Validate rent_epoch (should be reasonable - not in far future)
  // Current epoch is typically < 500, allow some headroom
  if (rent_epoch > 100000) {
    return false;
  }
  
  return true;
}

size_t AccountData::get_size() const {
  return sizeof(lamports) + sizeof(rent_epoch) + sizeof(version) + 
         sizeof(executable) + owner.size() + data.size();
}

// AccountsDB implementation
AccountsDB::AccountsDB(const Configuration& config) : config_(config) {
  stats_.last_gc_run = std::chrono::steady_clock::now();
  
  if (gc_enabled_) {
    gc_thread_ = std::thread(&AccountsDB::gc_worker_loop, this);
  }
}

AccountsDB::~AccountsDB() {
  should_stop_gc_ = true;
  if (gc_thread_.joinable()) {
    gc_thread_.join();
  }
}

bool AccountsDB::store_account(const PublicKey& account_key, const AccountData& data, uint64_t slot) {
  if (!data.is_valid()) {
    return false;
  }
  
  std::unique_lock<std::shared_mutex> lock(index_mutex_);
  
  auto index = get_or_create_index(account_key);
  
  // Create new version
  auto new_version = std::make_shared<AccountVersion>(slot, index->current_version + 1, data);
  index->versions.push_back(new_version);
  index->current_version++;
  index->current_slot = slot;
  
  // Limit versions per account
  if (index->versions.size() > config_.max_versions_per_account) {
    index->versions.erase(index->versions.begin());
    stats_.gc_cleaned_versions++;
  }
  
  // Update cache
  auto cached_data = std::make_shared<AccountData>(data);
  update_cache(account_key, cached_data);
  
  // Update statistics
  stats_.total_versions++;
  stats_.storage_size_bytes += data.get_size();
  
  return true;
}

std::optional<AccountData> AccountsDB::load_account(const PublicKey& account_key, uint64_t slot) {
  // Try cache first
  auto cached = get_from_cache(account_key);
  if (cached) {
    stats_.cache_hits++;
    return **cached;
  }
  
  stats_.cache_misses++;
  
  std::shared_lock<std::shared_mutex> lock(index_mutex_);
  
  auto it = account_index_.find(account_key);
  if (it == account_index_.end()) {
    return std::nullopt;
  }
  
  auto& index = it->second;
  
  // Find the appropriate version
  if (slot == UINT64_MAX) {
    // Get latest version
    if (index->versions.empty()) {
      return std::nullopt;
    }
    
    auto latest = index->versions.back();
    if (latest->is_deleted) {
      return std::nullopt;
    }
    
    // Update cache
    auto cached_data = std::make_shared<AccountData>(latest->data);
    update_cache(account_key, cached_data);
    
    return latest->data;
  } else {
    // Find version at specific slot
    for (auto it = index->versions.rbegin(); it != index->versions.rend(); ++it) {
      if ((*it)->slot <= slot) {
        if ((*it)->is_deleted) {
          return std::nullopt;
        }
        
        // Update cache
        auto cached_data = std::make_shared<AccountData>((*it)->data);
        update_cache(account_key, cached_data);
        
        return (*it)->data;
      }
    }
  }
  
  return std::nullopt;
}

bool AccountsDB::delete_account(const PublicKey& account_key, uint64_t slot) {
  std::unique_lock<std::shared_mutex> lock(index_mutex_);
  
  auto index = get_or_create_index(account_key);
  
  // Create deletion marker
  AccountData empty_data;
  auto deletion_version = std::make_shared<AccountVersion>(slot, index->current_version + 1, empty_data);
  deletion_version->is_deleted = true;
  
  index->versions.push_back(deletion_version);
  index->current_version++;
  index->current_slot = slot;
  
  // Remove from cache
  std::lock_guard<std::mutex> cache_lock(cache_mutex_);
  auto map_it = cache_map_.find(account_key);
  if (map_it != cache_map_.end()) {
    cache_list_.erase(map_it->second);
    cache_map_.erase(map_it);
  }
  
  return true;
}

bool AccountsDB::account_exists(const PublicKey& account_key, uint64_t slot) {
  auto account = load_account(account_key, slot);
  return account.has_value();
}

std::optional<AccountData> AccountsDB::get_account_at_slot(const PublicKey& account_key, uint64_t slot) {
  std::shared_lock<std::shared_mutex> lock(index_mutex_);
  
  auto it = account_index_.find(account_key);
  if (it == account_index_.end()) {
    return std::nullopt;
  }
  
  auto& index = it->second;
  
  // Find version at specific slot (latest version at or before the slot)
  for (auto version_it = index->versions.rbegin(); version_it != index->versions.rend(); ++version_it) {
    if ((*version_it)->slot <= slot) {
      if ((*version_it)->is_deleted) {
        return std::nullopt;
      }
      return (*version_it)->data;
    }
  }
  
  return std::nullopt;
}

std::vector<AccountData> AccountsDB::get_account_versions(const PublicKey& account_key, size_t max_versions) {
  std::shared_lock<std::shared_mutex> lock(index_mutex_);
  
  std::vector<AccountData> result;
  
  auto it = account_index_.find(account_key);
  if (it == account_index_.end()) {
    return result;
  }
  
  auto& index = it->second;
  size_t count = 0;
  
  for (auto it = index->versions.rbegin(); it != index->versions.rend() && count < max_versions; ++it, ++count) {
    if (!(*it)->is_deleted) {
      result.push_back((*it)->data);
    }
  }
  
  return result;
}

bool AccountsDB::store_accounts_batch(const std::vector<std::pair<PublicKey, AccountData>>& accounts, uint64_t slot) {
  std::unique_lock<std::shared_mutex> lock(index_mutex_);
  
  for (const auto& [account_key, data] : accounts) {
    if (!data.is_valid()) {
      std::cout << "Invalid account data in batch, skipping" << std::endl;
      continue;
    }
    
    auto index = get_or_create_index(account_key);
    auto new_version = std::make_shared<AccountVersion>(slot, index->current_version + 1, data);
    index->versions.push_back(new_version);
    index->current_version++;
    index->current_slot = slot;
    
    // Update cache
    auto cached_data = std::make_shared<AccountData>(data);
    update_cache(account_key, cached_data);
    
    stats_.total_versions++;
    stats_.storage_size_bytes += data.get_size();
  }
  
  return true;
}

std::vector<PublicKey> AccountsDB::get_accounts_by_owner(const PublicKey& owner_key) {
  std::shared_lock<std::shared_mutex> lock(index_mutex_);
  
  std::vector<PublicKey> result;
  
  for (const auto& [account_key, index] : account_index_) {
    if (!index->versions.empty()) {
      auto latest = index->versions.back();
      if (!latest->is_deleted && latest->data.owner == owner_key) {
        result.push_back(account_key);
      }
    }
  }
  
  return result;
}

std::vector<PublicKey> AccountsDB::get_executable_accounts() {
  std::shared_lock<std::shared_mutex> lock(index_mutex_);
  
  std::vector<PublicKey> result;
  
  for (const auto& [account_key, index] : account_index_) {
    if (!index->versions.empty()) {
      auto latest = index->versions.back();
      if (!latest->is_deleted && latest->data.executable) {
        result.push_back(account_key);
      }
    }
  }
  
  return result;
}

size_t AccountsDB::get_account_count() const {
  std::shared_lock<std::shared_mutex> lock(index_mutex_);
  return account_index_.size();
}

void AccountsDB::run_garbage_collection() {
  if (!gc_enabled_) {
    return;
  }
  
  std::unique_lock<std::shared_mutex> lock(index_mutex_);
  
  size_t cleaned_versions = 0;
  uint64_t current_slot = 0;
  
  // Find the current slot (highest slot across all accounts)
  for (const auto& [account_key, index] : account_index_) {
    if (index->current_slot > current_slot) {
      current_slot = index->current_slot;
    }
  }
  
  // Clean up old versions
  for (auto& [account_key, index] : account_index_) {
    auto& versions = index->versions;
    
    versions.erase(
      std::remove_if(versions.begin(), versions.end(),
        [this, current_slot, &cleaned_versions](const std::shared_ptr<AccountVersion>& version) {
          if (is_version_eligible_for_gc(*version, current_slot)) {
            cleaned_versions++;
            return true;
          }
          return false;
        }),
      versions.end()
    );
  }
  
  stats_.gc_runs++;
  stats_.gc_cleaned_versions += cleaned_versions;
  stats_.last_gc_run = std::chrono::steady_clock::now();
  
  std::cout << "GC cleaned " << cleaned_versions << " old account versions" << std::endl;
}

double AccountsDB::get_cache_hit_ratio() const {
  uint64_t hits = stats_.cache_hits.load();
  uint64_t misses = stats_.cache_misses.load();
  
  if (hits + misses == 0) {
    return 0.0;
  }
  
  return static_cast<double>(hits) / (hits + misses);
}

AccountsDB::Statistics AccountsDB::get_statistics() const {
  std::shared_lock<std::shared_mutex> lock(index_mutex_);
  stats_.total_accounts = account_index_.size();
  stats_.index_size = account_index_.size() * sizeof(std::pair<PublicKey, std::shared_ptr<AccountIndex>>);
  return stats_;
}

void AccountsDB::reset_statistics() {
  stats_.cache_hits = 0;
  stats_.cache_misses = 0;
  stats_.gc_runs = 0;
  stats_.gc_cleaned_versions = 0;
}

// Private methods
void AccountsDB::gc_worker_loop() {
  while (!should_stop_gc_) {
    std::this_thread::sleep_for(config_.gc_interval);
    
    if (!should_stop_gc_ && gc_enabled_) {
      run_garbage_collection();
    }
  }
}

bool AccountsDB::is_version_eligible_for_gc(const AccountVersion& version, uint64_t current_slot) {
  // Keep versions from recent slots
  const uint64_t keep_recent_slots = 100;
  
  if (current_slot > keep_recent_slots && version.slot < current_slot - keep_recent_slots) {
    return true;
  }
  
  return false;
}

void AccountsDB::update_cache(const PublicKey& account_key, std::shared_ptr<AccountData> data) {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  
  auto map_it = cache_map_.find(account_key);
  if (map_it != cache_map_.end()) {
    // Update existing entry and move to front safely
    auto list_it = map_it->second;
    list_it->data = data;
    list_it->access_time = std::chrono::steady_clock::now();
    
    // Safe move to front - splice preserves iterator validity
    cache_list_.splice(cache_list_.begin(), cache_list_, list_it);
    // Update map to point to new position (beginning)
    cache_map_[account_key] = cache_list_.begin();
  } else {
    // Add new entry at front
    cache_list_.emplace_front(account_key, data);
    cache_map_[account_key] = cache_list_.begin();
    
    // Check if we need to evict
    if (cache_list_.size() > config_.index_cache_size) {
      evict_cache_if_needed();
    }
  }
}

std::optional<std::shared_ptr<AccountData>> AccountsDB::get_from_cache(const PublicKey& account_key) {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  
  auto map_it = cache_map_.find(account_key);
  if (map_it != cache_map_.end()) {
    // Update access time and move to front (most recently used)
    auto list_it = map_it->second;
    list_it->access_time = std::chrono::steady_clock::now();
    
    // Safe move to front of list for LRU ordering
    cache_list_.splice(cache_list_.begin(), cache_list_, list_it);
    // Update map to point to new position (beginning)
    cache_map_[account_key] = cache_list_.begin();
    
    return list_it->data;
  }
  
  return std::nullopt;
}

void AccountsDB::evict_cache_if_needed() {
  // Proper LRU eviction - remove least recently used entries
  while (cache_list_.size() > config_.index_cache_size) {
    // Remove from back (least recently used)
    auto last_it = std::prev(cache_list_.end());
    cache_map_.erase(last_it->key);
    cache_list_.erase(last_it);
  }
}

std::shared_ptr<AccountIndex> AccountsDB::get_or_create_index(const PublicKey& account_key) {
  auto it = account_index_.find(account_key);
  if (it != account_index_.end()) {
    return it->second;
  }
  
  auto new_index = std::make_shared<AccountIndex>(account_key);
  account_index_[account_key] = new_index;
  return new_index;
}

// AccountStorageManager implementation
AccountStorageManager::AccountStorageManager(const std::string& storage_path) 
    : storage_path_(storage_path), initialized_(false) {}

AccountStorageManager::~AccountStorageManager() {
  shutdown();
}

bool AccountStorageManager::initialize() {
  if (initialized_) {
    return true;
  }
  
  // Create storage directory if it doesn't exist
  try {
    std::filesystem::create_directories(storage_path_);
  } catch (const std::exception& e) {
    std::cerr << "Failed to create storage directory: " << e.what() << std::endl;
    return false;
  }
  
  accounts_db_ = std::make_unique<AccountsDB>();
  initialized_ = true;
  
  std::cout << "AccountStorageManager initialized at: " << storage_path_ << std::endl;
  return true;
}

bool AccountStorageManager::shutdown() {
  if (!initialized_) {
    return true;
  }
  
  accounts_db_.reset();
  initialized_ = false;
  return true;
}

bool AccountStorageManager::store_account(const PublicKey& account_key, const AccountData& data, uint64_t slot) {
  if (!initialized_) {
    return false;
  }
  
  return accounts_db_->store_account(account_key, data, slot);
}

std::optional<AccountData> AccountStorageManager::load_account(const PublicKey& account_key) {
  if (!initialized_) {
    return std::nullopt;
  }
  
  return accounts_db_->load_account(account_key);
}

bool AccountStorageManager::account_exists(const PublicKey& account_key) {
  if (!initialized_) {
    return false;
  }
  
  return accounts_db_->account_exists(account_key);
}

AccountsDB::Statistics AccountStorageManager::get_statistics() const {
  if (!initialized_) {
    return AccountsDB::Statistics{};
  }
  
  return accounts_db_->get_statistics();
}

} // namespace storage
} // namespace slonana