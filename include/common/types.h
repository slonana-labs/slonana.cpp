#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace slonana {
namespace common {

/**
 * @file types.h
 * @brief Fundamental types and utilities used throughout the Slonana validator
 * 
 * This header defines core data types, configuration structures, and utility
 * classes that form the foundation of the validator implementation.
 */

/// @brief Cryptographic hash representation (typically SHA-256, 32 bytes)
using Hash = std::vector<uint8_t>;

/// @brief Ed25519 public key representation (32 bytes)
using PublicKey = std::vector<uint8_t>;

/// @brief Ed25519 signature representation (64 bytes)
using Signature = std::vector<uint8_t>;

/// @brief Slot number representing blockchain height/time
using Slot = uint64_t;

/// @brief Epoch number for staking and validator rotation periods
using Epoch = uint64_t;

/// @brief Native token amount in smallest unit (1 SOL = 1,000,000,000 lamports)
using Lamports = uint64_t;

/**
 * @brief Comprehensive configuration for validator node operation
 * 
 * Contains all parameters needed to configure a Solana validator instance,
 * including network settings, consensus parameters, monitoring options,
 * and security configurations.
 * 
 * @note All string paths should be absolute or relative to working directory
 * @note Default values are suitable for development; production requires customization
 */
struct ValidatorConfig {
  // Core identity and storage
  std::string identity_keypair_path;          ///< Path to validator's Ed25519 keypair file
  std::string ledger_path;                    ///< Directory for blockchain data storage
  std::string rpc_bind_address = "127.0.0.1:8899";   ///< JSON-RPC server bind address
  std::string gossip_bind_address = "127.0.0.1:8001"; ///< Gossip protocol bind address
  
  // Network service toggles
  bool enable_rpc = true;                     ///< Enable JSON-RPC API server
  bool enable_gossip = true;                  ///< Enable gossip network participation
  uint32_t max_connections = 1000;            ///< Maximum concurrent network connections

  // Runtime configuration
  std::string config_file_path;               ///< Path to additional config file (optional)
  std::string log_level = "info";             ///< Logging level: trace/debug/info/warn/error
  std::string metrics_output_path;            ///< File path for metrics export (optional)

  // Proof of History and consensus parameters
  bool enable_consensus = true;               ///< Participate in consensus (false for RPC-only)
  bool enable_proof_of_history = true;       ///< Enable PoH clock generation
  uint32_t poh_target_tick_duration_us = 400; ///< Target microseconds per PoH tick
  uint32_t poh_ticks_per_slot = 64;          ///< Number of PoH ticks per slot
  bool poh_enable_batch_processing = false;  ///< Batch PoH operations for efficiency
  bool poh_enable_simd_acceleration = false; ///< Use SIMD instructions for PoH hashing
  uint32_t poh_hashing_threads = 4;          ///< Number of threads for PoH computation
  uint32_t poh_batch_size = 8;               ///< PoH batch size when batching enabled

  // Monitoring and observability
  bool enable_prometheus = false;            ///< Enable Prometheus metrics endpoint
  uint32_t prometheus_port = 9090;           ///< Port for Prometheus metrics server
  bool enable_health_checks = true;         ///< Enable HTTP health check endpoints
  uint32_t metrics_export_interval_ms = 1000; ///< Metrics collection interval
  bool consensus_enable_timing_metrics = true; ///< Collect detailed consensus timing
  bool consensus_performance_target_validation = true; ///< Validate performance budgets

  // Network and cluster configuration
  std::string network_id = "devnet";          ///< Network identifier (mainnet/testnet/devnet)
  std::string expected_genesis_hash;          ///< Expected genesis block hash for validation
  std::vector<std::string> bootstrap_entrypoints; ///< Initial cluster discovery endpoints
  std::vector<std::string> known_validators;  ///< Trusted validator public keys

  // Security and validation parameters
  bool require_validator_identity = false;   ///< Require identity verification for peers
  bool enable_tls = false;                   ///< Use TLS for network communication
  uint64_t minimum_validator_stake = 0;      ///< Minimum stake required to validate
  uint64_t minimum_delegation_stake = 0;     ///< Minimum stake required for delegation

  // Economic and staking parameters
  double inflation_rate = 0.08;              ///< Annual inflation rate (8% default)
  uint32_t epoch_length_slots = 432000;      ///< Number of slots per epoch (~2 days)

  // Peer discovery and connection management
  uint32_t discovery_interval_ms = 30000;    ///< Peer discovery interval (30 seconds)
  uint32_t peer_connection_timeout_ms = 10000; ///< Connection timeout (10 seconds)
  uint32_t max_peer_discovery_attempts = 3;  ///< Maximum discovery retry attempts

  // Snapshot and bootstrap configuration
  std::string snapshot_source = "auto";      ///< Snapshot source: auto|mirror|none
  std::string snapshot_mirror = "";          ///< URL for custom snapshot mirror
  bool allow_stale_rpc = false;             ///< Allow RPC before full sync completion
  std::string upstream_rpc_url = "";         ///< RPC URL for catch-up and auto-discovery

  // Development and testing features
  bool enable_faucet = false;               ///< Enable built-in faucet (devnet only)
  uint32_t faucet_port = 9900;              ///< Port for faucet service
  std::string rpc_faucet_address = "127.0.0.1:9900"; ///< Faucet service bind address
};

/**
 * @brief Type-safe result wrapper for operations that can fail
 * 
 * This class implements a Result<T> pattern similar to Rust's Result type,
 * providing a safe way to handle operations that may fail without using exceptions.
 * 
 * @tparam T The type of the success value
 * 
 * @note Thread safety: This class is not thread-safe. Each instance should be
 *       used by only one thread at a time.
 * @note Exception safety: Strong guarantee - if an operation throws, the object
 *       remains in a valid state.
 * 
 * Example usage:
 * @code
 * auto result = validate_transaction(tx);
 * if (result.is_ok()) {
 *     bool valid = result.value();
 *     // Handle success case
 * } else {
 *     std::cerr << "Validation failed: " << result.error() << std::endl;
 * }
 * @endcode
 */
template <typename T> 
class Result {
private:
  bool success_;
  T value_;
  std::string error_;

public:
  /**
   * @brief Construct a successful result with a value
   * @param value The success value to store
   */
  explicit Result(T value) : success_(true), value_(std::move(value)) {}
  
  /**
   * @brief Construct a failed result with an error message
   * @param error C-string describing the error
   */
  explicit Result(const char *error) : success_(false), error_(error) {}
  
  /**
   * @brief Construct a failed result with an error message
   * @param error String describing the error
   */
  explicit Result(const std::string &error) : success_(false), error_(error) {}

  /// Copy constructor
  Result(const Result &other)
      : success_(other.success_), value_(other.value_), error_(other.error_) {}

  /// Move constructor
  Result(Result &&other) noexcept
      : success_(other.success_), value_(std::move(other.value_)),
        error_(std::move(other.error_)) {}

  /// Copy assignment operator
  Result &operator=(const Result &other) {
    if (this != &other) {
      success_ = other.success_;
      value_ = other.value_;
      error_ = other.error_;
    }
    return *this;
  }

  /// Move assignment operator
  Result &operator=(Result &&other) noexcept {
    if (this != &other) {
      success_ = other.success_;
      value_ = std::move(other.value_);
      error_ = std::move(other.error_);
    }
    return *this;
  }

  /**
   * @brief Check if the result represents success
   * @return true if the operation succeeded, false otherwise
   */
  bool is_ok() const noexcept { return success_; }
  
  /**
   * @brief Check if the result represents failure
   * @return true if the operation failed, false otherwise
   */
  bool is_err() const noexcept { return !success_; }

  /**
   * @brief Get the success value (lvalue reference)
   * @return Const reference to the stored value
   * @warning Only call this if is_ok() returns true
   */
  const T &value() const & { return value_; }
  
  /**
   * @brief Get the success value (rvalue reference)
   * @return Rvalue reference to the stored value for move semantics
   * @warning Only call this if is_ok() returns true
   */
  T &&value() && { return std::move(value_); }
  
  /**
   * @brief Get the error message
   * @return Const reference to the error string
   * @warning Only call this if is_err() returns true
   */
  const std::string &error() const noexcept { return error_; }
  
  /**
   * @brief Check if result contains a value (for conditional usage)
   * @return true if the operation succeeded, false otherwise
   */
  explicit operator bool() const noexcept { return success_; }
  
  /**
   * @brief Get value or return default on error
   * @param default_value Value to return if result is an error
   * @return The success value or the provided default
   */
  T value_or(const T& default_value) const {
    return success_ ? value_ : default_value;
  }
};

} // namespace common
} // namespace slonana

/**
 * @brief Standard library hash specialization for byte vectors
 * 
 * Enables use of Hash, PublicKey, and Signature types as keys in
 * std::unordered_map and std::unordered_set containers.
 */
namespace std {
template <> 
struct hash<std::vector<uint8_t>> {
  /**
   * @brief Compute hash value for a byte vector
   * 
   * Uses a simple but effective hash function that combines vector size
   * with content. Based on the boost::hash_combine algorithm.
   * 
   * @param v The byte vector to hash
   * @return Hash value suitable for use in hash tables
   * @note Time complexity: O(n) where n is vector size
   */
  std::size_t operator()(const std::vector<uint8_t> &v) const noexcept {
    std::size_t seed = v.size();
    for (const auto &byte : v) {
      // Boost-style hash combine
      seed ^= byte + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    return seed;
  }
};
} // namespace std