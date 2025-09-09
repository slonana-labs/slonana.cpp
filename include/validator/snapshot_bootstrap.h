#pragma once

#include "common/types.h"
#include "network/http_client.h"
#include "validator/snapshot.h"
#include <functional>
#include <memory>
#include <string>

// Forward declaration
namespace slonana {
namespace validator {
class SnapshotFinder;
}
} // namespace slonana

namespace slonana {
namespace validator {

/**
 * Snapshot source configuration
 */
enum class SnapshotSource {
  AUTO,   // Auto-discover from RPC
  MIRROR, // Use specific mirror URL
  NONE    // Skip snapshot bootstrap
};

/**
 * Snapshot information from RPC
 */
struct SnapshotInfo {
  uint64_t slot = 0;
  std::string hash;
  bool valid = false;
};

/**
 * Bootstrap progress callback
 */
using BootstrapProgressCallback = std::function<void(
    const std::string &phase, uint64_t current, uint64_t total)>;

/**
 * Snapshot Bootstrap Manager
 *
 * Handles automatic discovery, download, and application of Solana snapshots
 * for fast ledger synchronization in RPC node mode.
 */
class SnapshotBootstrapManager {
public:
  explicit SnapshotBootstrapManager(const common::ValidatorConfig &config);
  ~SnapshotBootstrapManager();

  // Main bootstrap workflow
  common::Result<bool> bootstrap_from_snapshot();

  // Individual bootstrap steps
  common::Result<SnapshotInfo> discover_latest_snapshot();
  common::Result<bool> download_snapshot(const SnapshotInfo &info,
                                         std::string &local_path_out);
  common::Result<bool> verify_snapshot(const std::string &local_path);
  common::Result<bool> apply_snapshot(const std::string &local_path);

  // Fallback methods for simple discovery/download
  common::Result<SnapshotInfo> discover_latest_snapshot_simple();
  common::Result<bool> download_snapshot_simple(const SnapshotInfo &info,
                                                std::string &local_path_out);

  // Progress tracking
  void set_progress_callback(BootstrapProgressCallback callback) {
    progress_callback_ = std::move(callback);
  }

  // Status and configuration
  bool needs_bootstrap() const;
  uint64_t get_local_ledger_slot() const;
  std::string get_snapshot_directory() const { return snapshot_dir_; }

  // Static utility methods
  static SnapshotSource parse_snapshot_source(const std::string &source_str);
  static std::string snapshot_source_to_string(SnapshotSource source);

private:
  const common::ValidatorConfig &config_;
  std::unique_ptr<network::HttpClient> http_client_;
  std::unique_ptr<SnapshotManager> snapshot_manager_;
  std::unique_ptr<SnapshotFinder>
      snapshot_finder_; // Advanced multi-threaded finder
  std::string snapshot_dir_;
  BootstrapProgressCallback progress_callback_;

  // Helper methods
  std::string build_snapshot_url(const SnapshotInfo &info) const;
  std::string generate_snapshot_filename(const SnapshotInfo &info) const;
  bool extract_snapshot_archive(const std::string &archive_path,
                                const std::string &extract_dir);
  common::Result<uint64_t> query_upstream_slot() const;
  std::vector<std::string> get_default_rpc_endpoints() const;

  // Default snapshot mirrors for devnet
  std::vector<std::string> get_devnet_snapshot_mirrors() const;

  // Progress reporting helpers
  void report_progress(const std::string &phase, uint64_t current = 0,
                       uint64_t total = 0) const;

  // Additional helper methods for production implementation
  std::string generate_slot_hash(uint64_t slot) const;
  common::Result<bool>
  restore_ledger_from_snapshot(const std::string &extract_dir);
};

} // namespace validator
} // namespace slonana