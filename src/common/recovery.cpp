/**
 * @file recovery.cpp
 * @brief Implements the file-based checkpoint and recovery management logic.
 *
 * This file provides the concrete implementations for the `FileCheckpoint`
 * and `RecoveryManager` classes defined in `recovery.h`.
 */
#include "common/recovery.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <openssl/evp.h>
#include <shared_mutex>
#include <sstream>

namespace slonana {
namespace common {

/**
 * @brief Constructs a FileCheckpoint object.
 * @details Initializes the checkpoint manager and creates the checkpoint
 * directory if it does not already exist.
 * @param checkpoint_dir The path to the directory for storing checkpoints.
 */
FileCheckpoint::FileCheckpoint(const std::string &checkpoint_dir)
    : checkpoint_dir_(checkpoint_dir) {
  try {
    std::filesystem::create_directories(checkpoint_dir_);
  } catch (const std::exception &e) {
    std::cerr << "Failed to create checkpoint directory: " << e.what()
              << std::endl;
  }
}

/**
 * @brief Calculates the SHA-256 hash of a file.
 * @param file_path The full path to the file to be hashed.
 * @return A hex-encoded string of the hash, or an empty string on failure.
 */
std::string
FileCheckpoint::calculate_file_hash(const std::string &file_path) const {
  std::ifstream file(file_path, std::ios::binary);
  if (!file.is_open()) {
    return "";
  }

  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  if (!ctx) return "";

  if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
    EVP_MD_CTX_free(ctx);
    return "";
  }

  char buffer[8192];
  while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
    if (EVP_DigestUpdate(ctx, buffer, file.gcount()) != 1) {
      EVP_MD_CTX_free(ctx);
      return "";
    }
  }

  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len;
  if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
    EVP_MD_CTX_free(ctx);
    return "";
  }

  EVP_MD_CTX_free(ctx);

  std::stringstream ss;
  for (unsigned int i = 0; i < hash_len; ++i) {
    ss << std::hex << std::setw(2) << std::setfill('0')
       << static_cast<int>(hash[i]);
  }

  return ss.str();
}

/**
 * @brief Writes a metadata file for a given checkpoint.
 * @details The metadata includes a timestamp, the hash of the data file, a
 * version number, and a magic string for identification.
 * @param checkpoint_id The ID of the checkpoint.
 * @param data_hash The SHA-256 hash of the associated data file.
 * @return A Result indicating success or failure.
 */
Result<bool>
FileCheckpoint::write_metadata(const std::string &checkpoint_id,
                               const std::string &data_hash) const {
  auto meta_path = checkpoint_dir_ + "/" + checkpoint_id + ".meta";

  try {
    std::ofstream meta_file(meta_path);
    if (!meta_file.is_open()) {
      return Result<bool>("Failed to create metadata file");
    }

    auto now = std::chrono::system_clock::now();
    auto timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
            .count();

    meta_file << "timestamp=" << timestamp << std::endl;
    meta_file << "hash=" << data_hash << std::endl;
    meta_file << "version=1.0" << std::endl;
    meta_file << "magic=SLONANA_CHECKPOINT" << std::endl;

    return Result<bool>(true);
  } catch (const std::exception &e) {
    return Result<bool>("Failed to write metadata: " + std::string(e.what()));
  }
}

/**
 * @brief Saves a generic checkpoint marker.
 * @details This is a base implementation. Derived classes should override this
 * to save their specific state.
 * @param checkpoint_id The identifier for the checkpoint.
 * @return A Result indicating success or failure.
 */
Result<bool> FileCheckpoint::save_checkpoint(const std::string &checkpoint_id) {
  std::lock_guard<std::mutex> lock(checkpoint_mutex_);
  auto checkpoint_path = checkpoint_dir_ + "/" + checkpoint_id + ".checkpoint";
  try {
    std::ofstream checkpoint_file(checkpoint_path, std::ios::binary);
    if (!checkpoint_file.is_open()) {
      return Result<bool>("Failed to create checkpoint file");
    }
    std::string marker = "SLONANA_CHECKPOINT_V1";
    checkpoint_file.write(marker.c_str(), marker.size());
    checkpoint_file.close();

    auto hash = calculate_file_hash(checkpoint_path);
    if (hash.empty()) {
      return Result<bool>("Failed to calculate checkpoint hash");
    }

    auto meta_result = write_metadata(checkpoint_id, hash);
    if (meta_result.is_err()) return meta_result;

    std::cout << "Checkpoint saved: " << checkpoint_id << std::endl;
    return Result<bool>(true);
  } catch (const std::exception &e) {
    return Result<bool>("Failed to save checkpoint: " + std::string(e.what()));
  }
}

/**
 * @brief Restores state from a generic checkpoint marker.
 * @details This is a base implementation. Derived classes should override this
 * to restore their specific state.
 * @param checkpoint_id The identifier for the checkpoint to restore.
 * @return A Result indicating success or failure.
 */
Result<bool>
FileCheckpoint::restore_checkpoint(const std::string &checkpoint_id) {
  std::lock_guard<std::mutex> lock(checkpoint_mutex_);
  auto verify_result = verify_checkpoint(checkpoint_id);
  if (verify_result.is_err()) {
    return Result<bool>("Checkpoint verification failed: " + verify_result.error());
  }
  auto checkpoint_path = checkpoint_dir_ + "/" + checkpoint_id + ".checkpoint";
  try {
    std::ifstream checkpoint_file(checkpoint_path, std::ios::binary);
    if (!checkpoint_file.is_open()) {
      return Result<bool>("Checkpoint file not found");
    }
    std::string marker(22, '\0');
    checkpoint_file.read(marker.data(), 22);

    if (marker != "SLONANA_CHECKPOINT_V1") {
      return Result<bool>("Invalid checkpoint format");
    }
    std::cout << "Checkpoint restored: " << checkpoint_id << std::endl;
    return Result<bool>(true);
  } catch (const std::exception &e) {
    return Result<bool>("Failed to restore checkpoint: " + std::string(e.what()));
  }
}

/**
 * @brief Lists available checkpoints, sorted with the newest first.
 * @return A Result containing a sorted vector of checkpoint IDs, or an error.
 */
Result<std::vector<std::string>> FileCheckpoint::list_checkpoints() {
  try {
    std::vector<std::string> checkpoints;
    for (const auto &entry : std::filesystem::directory_iterator(checkpoint_dir_)) {
      if (entry.is_regular_file() && entry.path().extension() == ".checkpoint") {
        checkpoints.push_back(entry.path().stem().string());
      }
    }
    std::sort(checkpoints.begin(), checkpoints.end(),
              [this](const std::string &a, const std::string &b) {
                auto path_a = checkpoint_dir_ + "/" + a + ".checkpoint";
                auto path_b = checkpoint_dir_ + "/" + b + ".checkpoint";
                try {
                  return std::filesystem::last_write_time(path_a) >
                         std::filesystem::last_write_time(path_b);
                } catch (...) {
                  return false;
                }
              });
    return Result<std::vector<std::string>>(checkpoints);
  } catch (const std::exception &e) {
    return Result<std::vector<std::string>>("Failed to list checkpoints: " + std::string(e.what()));
  }
}

/**
 * @brief Verifies the integrity of a checkpoint file.
 * @details Currently, this is a simplified check. A full implementation would
 * compare the file's hash against the hash stored in the metadata.
 * @param checkpoint_id The ID of the checkpoint to verify.
 * @return A Result indicating if the checkpoint is valid.
 */
Result<bool>
FileCheckpoint::verify_checkpoint(const std::string &checkpoint_id) {
  auto checkpoint_path = checkpoint_dir_ + "/" + checkpoint_id + ".checkpoint";
  if (!std::filesystem::exists(checkpoint_path)) {
    return Result<bool>("Checkpoint file does not exist");
  }
  // TODO: Add full verification by reading metadata and comparing hash.
  return Result<bool>(true);
}

/**
 * @brief Saves raw binary data to a checkpoint file.
 * @param checkpoint_id The ID for the new checkpoint.
 * @param data The vector of bytes to save.
 * @return A Result indicating success or failure.
 */
Result<bool> FileCheckpoint::save_data(const std::string &checkpoint_id,
                                       const std::vector<uint8_t> &data) {
  std::lock_guard<std::mutex> lock(checkpoint_mutex_);
  auto checkpoint_path = checkpoint_dir_ + "/" + checkpoint_id + ".checkpoint";
  try {
    std::ofstream checkpoint_file(checkpoint_path, std::ios::binary);
    if (!checkpoint_file.is_open()) {
      return Result<bool>("Failed to create checkpoint file");
    }
    checkpoint_file.write(reinterpret_cast<const char *>(data.data()), data.size());
    checkpoint_file.close();

    auto hash = calculate_file_hash(checkpoint_path);
    if (hash.empty()) {
      return Result<bool>("Failed to calculate checkpoint hash");
    }
    return write_metadata(checkpoint_id, hash);
  } catch (const std::exception &e) {
    return Result<bool>("Failed to save data: " + std::string(e.what()));
  }
}

/**
 * @brief Loads raw binary data from a checkpoint file.
 * @param checkpoint_id The ID of the checkpoint to load.
 * @return A Result containing the loaded data as a vector of bytes, or an error.
 */
Result<std::vector<uint8_t>>
FileCheckpoint::load_data(const std::string &checkpoint_id) {
  std::lock_guard<std::mutex> lock(checkpoint_mutex_);
  auto verify_result = verify_checkpoint(checkpoint_id);
  if (verify_result.is_err()) {
    return Result<std::vector<uint8_t>>("Checkpoint verification failed");
  }
  auto checkpoint_path = checkpoint_dir_ + "/" + checkpoint_id + ".checkpoint";
  try {
    std::ifstream checkpoint_file(checkpoint_path, std::ios::binary | std::ios::ate);
    if (!checkpoint_file.is_open()) {
      return Result<std::vector<uint8_t>>("Failed to open checkpoint file");
    }
    auto size = checkpoint_file.tellg();
    checkpoint_file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    checkpoint_file.read(reinterpret_cast<char *>(data.data()), size);
    return Result<std::vector<uint8_t>>(std::move(data));
  } catch (const std::exception &e) {
    return Result<std::vector<uint8_t>>("Failed to load data: " + std::string(e.what()));
  }
}

/**
 * @brief Deletes the oldest checkpoints, retaining a specified number.
 * @param keep_count The number of recent checkpoints to keep.
 * @return A Result indicating success or failure.
 */
Result<bool> FileCheckpoint::cleanup_old_checkpoints(size_t keep_count) {
  auto list_result = list_checkpoints();
  if (list_result.is_err()) {
    return Result<bool>("Failed to list checkpoints for cleanup");
  }
  auto checkpoints = list_result.value();
  if (checkpoints.size() <= keep_count) {
    return Result<bool>(true);
  }
  for (size_t i = keep_count; i < checkpoints.size(); ++i) {
    auto checkpoint_path = checkpoint_dir_ + "/" + checkpoints[i] + ".checkpoint";
    auto meta_path = checkpoint_dir_ + "/" + checkpoints[i] + ".meta";
    try {
      std::filesystem::remove(checkpoint_path);
      std::filesystem::remove(meta_path);
      std::cout << "Removed old checkpoint: " << checkpoints[i] << std::endl;
    } catch (const std::exception &e) {
      std::cerr << "Failed to remove checkpoint " << checkpoints[i] << ": " << e.what() << std::endl;
    }
  }
  return Result<bool>(true);
}

/**
 * @brief Constructs a RecoveryManager object.
 * @param recovery_dir The base directory for recovery operations.
 */
RecoveryManager::RecoveryManager(const std::string &recovery_dir)
    : recovery_dir_(recovery_dir) {
  try {
    std::filesystem::create_directories(recovery_dir_);
  } catch (const std::exception &e) {
    std::cerr << "Failed to create recovery directory: " << e.what()
              << std::endl;
  }
}

/**
 * @brief Registers a component's checkpoint handler with the manager.
 * @param component_name A unique name for the component.
 * @param checkpoint A shared pointer to the component's checkpoint implementation.
 */
void RecoveryManager::register_component(
    const std::string &component_name, std::shared_ptr<Checkpoint> checkpoint) {
  std::unique_lock<std::shared_mutex> lock(recovery_mutex_);
  component_checkpoints_[component_name] = checkpoint;
  std::cout << "Registered component for recovery: " << component_name << std::endl;
}

/**
 * @brief Creates a system-wide checkpoint by invoking `save_checkpoint` on all
 * registered components.
 * @param checkpoint_id The unique ID for this system-wide checkpoint.
 * @return A Result indicating if all components were checkpointed successfully.
 */
Result<bool>
RecoveryManager::create_system_checkpoint(const std::string &checkpoint_id) {
  std::shared_lock<std::shared_mutex> lock(recovery_mutex_);
  bool all_success = true;
  std::vector<std::string> failed_components;
  for (const auto &[component_name, checkpoint] : component_checkpoints_) {
    auto component_checkpoint_id = checkpoint_id + "_" + component_name;
    auto result = checkpoint->save_checkpoint(component_checkpoint_id);
    if (result.is_err()) {
      all_success = false;
      failed_components.push_back(component_name);
      std::cerr << "Failed to checkpoint component " << component_name << ": " << result.error() << std::endl;
    }
  }
  if (!all_success) {
    return Result<bool>("Failed to checkpoint components: " + std::to_string(failed_components.size()) + " failures");
  }
  std::cout << "System checkpoint created successfully: " << checkpoint_id << std::endl;
  return Result<bool>(true);
}

/**
 * @brief Restores a system-wide checkpoint by invoking `restore_checkpoint` on
 * all registered components.
 * @param checkpoint_id The ID of the system-wide checkpoint to restore.
 * @return A Result indicating if all components were restored successfully.
 */
Result<bool>
RecoveryManager::restore_system_checkpoint(const std::string &checkpoint_id) {
  std::shared_lock<std::shared_mutex> lock(recovery_mutex_);
  bool all_success = true;
  std::vector<std::string> failed_components;
  for (const auto &[component_name, checkpoint] : component_checkpoints_) {
    auto component_checkpoint_id = checkpoint_id + "_" + component_name;
    auto result = checkpoint->restore_checkpoint(component_checkpoint_id);
    if (result.is_err()) {
      all_success = false;
      failed_components.push_back(component_name);
      std::cerr << "Failed to restore component " << component_name << ": " << result.error() << std::endl;
    }
  }
  if (!all_success) {
    return Result<bool>("Failed to restore components: " + std::to_string(failed_components.size()) + " failures");
  }
  std::cout << "System checkpoint restored successfully: " << checkpoint_id << std::endl;
  return Result<bool>(true);
}

/**
 * @brief Gets the recovery status of all registered components.
 * @return A map from component name to a boolean indicating its recovery status.
 * @note In a real implementation, this would involve more sophisticated checks.
 */
std::unordered_map<std::string, bool> RecoveryManager::get_recovery_status() {
  std::shared_lock<std::shared_mutex> lock(recovery_mutex_);
  std::unordered_map<std::string, bool> status;
  for (const auto &[component_name, checkpoint] : component_checkpoints_) {
    status[component_name] = true;
  }
  return status;
}

/**
 * @brief Attempts to automatically recover the system by restoring the latest
 * available checkpoint.
 * @return A Result indicating if auto-recovery was successful.
 */
Result<bool> RecoveryManager::auto_recover() {
  std::shared_lock<std::shared_mutex> lock(recovery_mutex_);
  if (component_checkpoints_.empty()) {
    return Result<bool>("No components registered for recovery");
  }
  auto first_component = component_checkpoints_.begin();
  auto list_result = first_component->second->list_checkpoints();
  if (list_result.is_err() || list_result.value().empty()) {
    return Result<bool>("No checkpoints available for auto-recovery");
  }
  auto latest_checkpoint = list_result.value()[0];
  auto pos = latest_checkpoint.find('_');
  if (pos != std::string::npos) {
    latest_checkpoint = latest_checkpoint.substr(0, pos);
  }
  std::cout << "Attempting auto-recovery from checkpoint: " << latest_checkpoint << std::endl;
  return restore_system_checkpoint(latest_checkpoint);
}

} // namespace common
} // namespace slonana