#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace slonana {
namespace common {

/**
 * Common types and utilities used across the validator
 */

using Hash = std::vector<uint8_t>;
using PublicKey = std::vector<uint8_t>;
using Signature = std::vector<uint8_t>;
using Slot = uint64_t;
using Epoch = uint64_t;
using Lamports = uint64_t;

/**
 * Configuration structure for the validator
 */
struct ValidatorConfig {
  std::string identity_keypair_path;
  std::string ledger_path;
  std::string rpc_bind_address = "127.0.0.1:8899";
  std::string gossip_bind_address = "127.0.0.1:8001";
  bool enable_rpc = true;
  bool enable_gossip = true;
  uint32_t max_connections = 1000;

  // E2E testing and production configuration
  std::string config_file_path;
  std::string log_level = "info";
  std::string metrics_output_path;

  // PoH and consensus configuration
  bool enable_consensus = true;
  bool enable_proof_of_history = true;
  uint32_t poh_target_tick_duration_us = 400;
  uint32_t poh_ticks_per_slot = 64;
  bool poh_enable_batch_processing = false;
  bool poh_enable_simd_acceleration = false;
  uint32_t poh_hashing_threads = 4;
  uint32_t poh_batch_size = 8;

  // Monitoring configuration
  bool enable_prometheus = false;
  uint32_t prometheus_port = 9090;
  bool enable_health_checks = true;
  uint32_t metrics_export_interval_ms = 1000;
  bool consensus_enable_timing_metrics = true;
  bool consensus_performance_target_validation = true;

  // Mainnet and network configuration
  std::string network_id = "devnet";
  std::string expected_genesis_hash;
  std::vector<std::string> bootstrap_entrypoints;
  std::vector<std::string> known_validators;

  // Security configuration
  bool require_validator_identity = false;
  bool enable_tls = false;
  uint64_t minimum_validator_stake = 0;
  uint64_t minimum_delegation_stake = 0;

  // Economic parameters
  double inflation_rate = 0.08;
  uint32_t epoch_length_slots = 432000;

  // Network discovery
  uint32_t discovery_interval_ms = 30000;
  uint32_t peer_connection_timeout_ms = 10000;
  uint32_t max_peer_discovery_attempts = 3;

  // Snapshot bootstrap configuration
  std::string snapshot_source = "auto"; // auto|mirror|none
  std::string snapshot_mirror = "";     // URL for snapshot mirror
  bool allow_stale_rpc = false;         // Allow RPC before caught up
  std::string upstream_rpc_url = "";    // For auto-discovery and catch-up

  // Faucet configuration (for CLI airdrop support)
  bool enable_faucet = false;              // Enable faucet functionality
  uint32_t faucet_port = 9900;             // Faucet service port
  std::string rpc_faucet_address = "127.0.0.1:9900"; // Faucet bind address
};

// Disambiguation tags
struct success_tag {};
struct error_tag {};

/**
 * Result type for operations that can fail
 */
template <typename T> class Result {
private:
  bool success_;
  T value_;
  std::string error_;

public:
  // Success constructor with disambiguation
  Result(T value, success_tag) : success_(true), value_(std::move(value)) {}
  
  // Error constructors
  explicit Result(const char *error) : success_(false), error_(error) {}
  explicit Result(const std::string &error) : success_(false), error_(error) {}
  
  // Legacy success constructor for non-string types
  template<typename U = T>
  Result(U value, typename std::enable_if<!std::is_same<U, std::string>::value>::type* = nullptr) 
      : success_(true), value_(std::move(value)) {}

  // Copy constructor
  Result(const Result &other)
      : success_(other.success_), value_(other.value_), error_(other.error_) {}

  // Move constructor
  Result(Result &&other) noexcept
      : success_(other.success_), value_(std::move(other.value_)),
        error_(std::move(other.error_)) {
    // Don't invalidate the moved-from object's success state
  }

  // Copy assignment
  Result &operator=(const Result &other) {
    if (this != &other) {
      success_ = other.success_;
      value_ = other.value_;
      error_ = other.error_;
    }
    return *this;
  }

  // Move assignment
  Result &operator=(Result &&other) noexcept {
    if (this != &other) {
      success_ = other.success_;
      value_ = std::move(other.value_);
      error_ = std::move(other.error_);
      // Don't invalidate the moved-from object's success state
    }
    return *this;
  }

  bool is_ok() const { return success_; }
  bool is_err() const { return !success_; }

  const T &value() const & { return value_; }
  T &&value() && { return std::move(value_); }
  const std::string &error() const { return error_; }
};

} // namespace common
} // namespace slonana

// Hash function for std::vector<uint8_t> to enable use in unordered_map
namespace std {
template <> struct hash<std::vector<uint8_t>> {
  size_t operator()(const std::vector<uint8_t> &v) const {
    size_t seed = v.size();
    for (auto &i : v) {
      seed ^= i + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    return seed;
  }
};
} // namespace std