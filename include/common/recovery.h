#pragma once

#include "common/fault_tolerance.h"
#include <filesystem>
#include <fstream>
#include <openssl/evp.h>

namespace slonana {
namespace common {

/**
 * File-based checkpoint implementation for state recovery
 */
class FileCheckpoint : public Checkpoint {
private:
  std::string checkpoint_dir_;
  mutable std::mutex checkpoint_mutex_;
  
  /**
   * Calculate SHA-256 hash of file for integrity verification
   */
  std::string calculate_file_hash(const std::string& file_path) const;
  
  /**
   * Write checkpoint metadata (timestamp, hash, etc.)
   */
  Result<bool> write_metadata(const std::string& checkpoint_id, const std::string& data_hash) const;
  
  /**
   * Read and verify checkpoint metadata (temporarily returns empty for Result<string> ambiguity)
   */
  // Result<std::string> read_metadata(const std::string& checkpoint_id) const;

public:
  explicit FileCheckpoint(const std::string& checkpoint_dir);
  
  Result<bool> save_checkpoint(const std::string& checkpoint_id) override;
  Result<bool> restore_checkpoint(const std::string& checkpoint_id) override;
  Result<std::vector<std::string>> list_checkpoints() override;
  Result<bool> verify_checkpoint(const std::string& checkpoint_id) override;
  
  /**
   * Save arbitrary data to checkpoint
   */
  Result<bool> save_data(const std::string& checkpoint_id, const std::vector<uint8_t>& data);
  
  /**
   * Load data from checkpoint
   */
  Result<std::vector<uint8_t>> load_data(const std::string& checkpoint_id);
  
  /**
   * Clean up old checkpoints (keep only the latest N)
   */
  Result<bool> cleanup_old_checkpoints(size_t keep_count = 5);
};

/**
 * Recovery manager that coordinates checkpoint operations across components
 */
class RecoveryManager {
private:
  std::unordered_map<std::string, std::shared_ptr<Checkpoint>> component_checkpoints_;
  std::string recovery_dir_;
  mutable std::shared_mutex recovery_mutex_;

public:
  explicit RecoveryManager(const std::string& recovery_dir);
  
  /**
   * Register a component for checkpoint/recovery operations
   */
  void register_component(const std::string& component_name, std::shared_ptr<Checkpoint> checkpoint);
  
  /**
   * Create system-wide checkpoint
   */
  Result<bool> create_system_checkpoint(const std::string& checkpoint_id);
  
  /**
   * Restore system from checkpoint
   */
  Result<bool> restore_system_checkpoint(const std::string& checkpoint_id);
  
  /**
   * Get recovery status for all components
   */
  std::unordered_map<std::string, bool> get_recovery_status();
  
  /**
   * Automatic recovery on startup - find and restore latest valid checkpoint
   */
  Result<bool> auto_recover();
};

} // namespace common
} // namespace slonana