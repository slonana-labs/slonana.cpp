/**
 * @file recovery.h
 * @brief Defines mechanisms for system state recovery and checkpointing.
 *
 * This file provides classes for creating, managing, and restoring system
 * checkpoints. It includes a file-based checkpoint implementation and a
 * recovery manager to coordinate checkpointing across multiple components.
 */
#pragma once

#include "common/fault_tolerance.h"
#include <filesystem>
#include <fstream>
#include <openssl/evp.h>

namespace slonana {
namespace common {

/**
 * @brief A file-based implementation of the Checkpoint interface.
 * @details This class manages checkpoints by storing state data in files within a
 * specified directory. It includes functionality for saving, restoring,
 * verifying, and cleaning up checkpoints, with integrity checks using SHA-256
 * hashes.
 */
class FileCheckpoint : public Checkpoint {
private:
  std::string checkpoint_dir_;
  mutable std::mutex checkpoint_mutex_;

  /**
   * @brief Calculates the SHA-256 hash of a file for integrity verification.
   * @param file_path The path to the file to be hashed.
   * @return A string containing the hex representation of the SHA-256 hash.
   */
  std::string calculate_file_hash(const std::string& file_path) const;

  /**
   * @brief Writes metadata for a checkpoint, such as a timestamp and data hash.
   * @param checkpoint_id The ID of the checkpoint.
   * @param data_hash The SHA-256 hash of the checkpoint data file.
   * @return A Result indicating success or failure.
   */
  Result<bool> write_metadata(const std::string& checkpoint_id, const std::string& data_hash) const;

  /**
   * @brief Reads and verifies checkpoint metadata.
   * @param checkpoint_id The ID of the checkpoint.
   * @return A Result containing the data hash from the metadata if successful.
   */
  // Result<std::string> read_metadata(const std::string& checkpoint_id) const;

public:
  /**
   * @brief Constructs a new FileCheckpoint manager.
   * @param checkpoint_dir The directory where checkpoint files will be stored.
   */
  explicit FileCheckpoint(const std::string& checkpoint_dir);

  /**
   * @brief Saves the current state to a checkpoint.
   * @param checkpoint_id A unique identifier for the checkpoint.
   * @return A Result indicating success or failure.
   */
  Result<bool> save_checkpoint(const std::string& checkpoint_id) override;

  /**
   * @brief Restores the state from a specified checkpoint.
   * @param checkpoint_id The identifier of the checkpoint to restore.
   * @return A Result indicating success or failure.
   */
  Result<bool> restore_checkpoint(const std::string& checkpoint_id) override;

  /**
   * @brief Lists all available checkpoints in the checkpoint directory.
   * @return A Result containing a vector of checkpoint IDs.
   */
  Result<std::vector<std::string>> list_checkpoints() override;

  /**
   * @brief Verifies the integrity of a checkpoint by checking its hash.
   * @param checkpoint_id The identifier of the checkpoint to verify.
   * @return A Result indicating whether the checkpoint is valid.
   */
  Result<bool> verify_checkpoint(const std::string& checkpoint_id) override;

  /**
   * @brief Saves a raw byte vector to a checkpoint file.
   * @param checkpoint_id The identifier for the checkpoint.
   * @param data The byte vector to be saved.
   * @return A Result indicating success or failure.
   */
  Result<bool> save_data(const std::string& checkpoint_id, const std::vector<uint8_t>& data);

  /**
   * @brief Loads raw data from a checkpoint file.
   * @param checkpoint_id The identifier of the checkpoint to load from.
   * @return A Result containing the loaded byte vector, or an error.
   */
  Result<std::vector<uint8_t>> load_data(const std::string& checkpoint_id);

  /**
   * @brief Cleans up old checkpoints, keeping a specified number of recent ones.
   * @param keep_count The number of the most recent checkpoints to retain.
   * @return A Result indicating success or failure.
   */
  Result<bool> cleanup_old_checkpoints(size_t keep_count = 5);
};

/**
 * @brief Manages recovery operations by coordinating checkpoints across multiple components.
 * @details This class orchestrates the creation and restoration of system-wide
 * checkpoints by managing individual Checkpoint objects for different parts of
 * the system. It also supports automatic recovery on startup.
 */
class RecoveryManager {
private:
  std::unordered_map<std::string, std::shared_ptr<Checkpoint>> component_checkpoints_;
  std::string recovery_dir_;
  mutable std::shared_mutex recovery_mutex_;

public:
  /**
   * @brief Constructs a new RecoveryManager.
   * @param recovery_dir The base directory for all recovery operations.
   */
  explicit RecoveryManager(const std::string& recovery_dir);

  /**
   * @brief Registers a component's Checkpoint handler with the manager.
   * @param component_name The name of the component (e.g., "ledger", "state").
   * @param checkpoint A shared pointer to the component's Checkpoint implementation.
   */
  void register_component(const std::string& component_name, std::shared_ptr<Checkpoint> checkpoint);

  /**
   * @brief Creates a system-wide checkpoint by triggering save_checkpoint on all
   * registered components.
   * @param checkpoint_id A unique identifier for the system-wide checkpoint.
   * @return A Result indicating if the overall operation was successful.
   */
  Result<bool> create_system_checkpoint(const std::string& checkpoint_id);

  /**
   * @brief Restores the entire system from a specified checkpoint by triggering
   * restore_checkpoint on all registered components.
   * @param checkpoint_id The identifier of the system-wide checkpoint to restore.
   * @return A Result indicating if the overall restoration was successful.
   */
  Result<bool> restore_system_checkpoint(const std::string& checkpoint_id);

  /**
   * @brief Gets the recovery status of all registered components.
   * @return A map from component name to a boolean indicating if it has
   * successfully recovered or is in a valid state.
   */
  std::unordered_map<std::string, bool> get_recovery_status();

  /**
   * @brief Automatically finds the latest valid checkpoint and restores the system.
   * @details This method is intended to be called on startup to automatically
   * recover from a previous crash or shutdown.
   * @return A Result indicating if auto-recovery was successful.
   */
  Result<bool> auto_recover();
};

} // namespace common
} // namespace slonana