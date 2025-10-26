#pragma once

#include "common/types.h"
#include "common/fault_tolerance.h"
#include "network/websocket_server.h"
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace slonana {

// Forward declarations
namespace ledger {
class LedgerManager;
}
namespace validator {
class ValidatorCore;
}
namespace staking {
class StakingManager;
}
namespace banking {
class BankingStage;
}
namespace svm {
class ExecutionEngine;
class AccountManager;
struct ProgramAccount;
} // namespace svm

namespace network {

using namespace slonana::common;

/**
 * @file rpc_server.h
 * @brief Comprehensive Solana-compatible JSON-RPC 2.0 server implementation
 * 
 * This module provides a production-ready RPC server that implements the complete
 * Solana JSON-RPC API with 35+ methods for account queries, transaction handling,
 * blockchain data access, and real-time subscriptions via WebSocket.
 */

/**
 * @brief JSON-RPC 2.0 request structure for standardized communication
 * 
 * Represents an incoming JSON-RPC request with all required and optional fields.
 * Follows the JSON-RPC 2.0 specification exactly.
 * 
 * @note Thread safety: This struct is not thread-safe
 * @note All string fields use UTF-8 encoding
 */
struct RpcRequest {
  std::string jsonrpc = "2.0";  ///< JSON-RPC version (always "2.0")
  std::string method;           ///< RPC method name (e.g., "getAccountInfo")
  std::string params;           ///< JSON string containing method parameters
  std::string id;               ///< Request identifier for correlation
  bool id_is_number = false;    ///< True if ID was originally numeric
};

/**
 * @brief JSON-RPC 2.0 response structure for standardized replies
 * 
 * Represents a JSON-RPC response containing either successful result data
 * or error information. Maintains correlation with requests via ID field.
 * 
 * @note Thread safety: This struct is not thread-safe
 * @note Either result or error should be set, never both
 */
struct RpcResponse {
  std::string jsonrpc = "2.0";   ///< JSON-RPC version (always "2.0")
  std::string result;            ///< JSON string containing successful response data
  std::string error;             ///< JSON string containing error information (only if error occurred)
  std::string id;                ///< Request ID for correlation (copied from request)
  bool id_is_number = false;     ///< True if ID should be rendered as number in JSON

  /**
   * @brief Convert response to JSON string for transmission
   * @return Properly formatted JSON-RPC 2.0 response string
   * @note Automatically includes result or error based on which field is populated
   */
  std::string to_json() const;
};

/**
 * @brief Production-ready Solana-compatible JSON-RPC 2.0 server
 * 
 * Implements the complete Solana RPC API with 35+ methods covering:
 * - Account information and balance queries  
 * - Block and transaction data access
 * - Network and validator status information
 * - Staking and economic parameter queries
 * - Real-time WebSocket subscriptions
 * - Transaction simulation and submission
 * 
 * Key features:
 * - Full JSON-RPC 2.0 compliance with proper error handling
 * - High-performance HTTP server with connection pooling
 * - WebSocket support for real-time subscriptions
 * - Configurable binding address and port
 * - Comprehensive request/response validation
 * - Built-in rate limiting and security features
 * 
 * @note Thread safety: All public methods are thread-safe and can be called
 *       concurrently from multiple threads
 * @note Performance: Optimized for high throughput with connection reuse
 * @note Compatibility: Maintains compatibility with official Solana RPC API
 * 
 * Lifecycle:
 * 1. Construct with ValidatorConfig containing bind address and settings
 * 2. Set component dependencies (ledger, validator, banking, etc.)
 * 3. Call start() to begin serving requests
 * 4. Handle incoming requests via HTTP or WebSocket
 * 5. Call stop() for graceful shutdown
 * 
 * Example usage:
 * @code
 * ValidatorConfig config;
 * config.rpc_bind_address = "0.0.0.0:8899";
 * 
 * SolanaRpcServer rpc_server(config);
 * rpc_server.set_ledger_manager(ledger);
 * rpc_server.set_validator_core(validator);
 * 
 * auto result = rpc_server.start();
 * if (!result.is_ok()) {
 *     std::cerr << "Failed to start RPC: " << result.error() << std::endl;
 *     return;
 * }
 * 
 * // Server is now accepting requests on the configured address
 * // ... application logic ...
 * 
 * rpc_server.stop();  // Graceful shutdown
 * @endcode
 */
class SolanaRpcServer {
public:
  /// Function type for custom RPC method handlers
  using RpcHandler = std::function<RpcResponse(const RpcRequest &)>;

  /**
   * @brief Construct RPC server with configuration
   * 
   * @param config Validator configuration containing RPC settings
   * @note Server starts in stopped state - call start() to begin serving
   * @note Configuration is copied and cannot be changed after construction
   */
  explicit SolanaRpcServer(const ValidatorConfig &config);
  
  /**
   * @brief Destructor with automatic cleanup
   * 
   * Automatically stops the server if running and cleans up all resources.
   * Waits for active connections to complete gracefully.
   */
  ~SolanaRpcServer();

  // === Server Lifecycle Management ===
  
  /**
   * @brief Start the RPC server and begin accepting requests
   * 
   * Initializes the HTTP server on the configured bind address and port.
   * Sets up all RPC method handlers and starts background threads for
   * request processing.
   * 
   * @return Result indicating success or detailed error information
   * @note Must be called before the server can handle any requests
   * @note Safe to call multiple times (subsequent calls are no-ops)
   * @note Blocks briefly during initialization but returns quickly
   */
  common::Result<bool> start();
  
  /**
   * @brief Stop the RPC server gracefully
   * 
   * Stops accepting new connections and waits for active requests to complete.
   * Cleans up all server resources and background threads.
   * 
   * @note Safe to call multiple times
   * @note Blocks until all active connections are closed
   * @note Does not interrupt requests in progress
   */
  void stop() noexcept;
  
  /**
   * @brief Check if the RPC server is currently running
   * @return true if server is accepting requests, false otherwise
   * @note Thread-safe query of server state
   */
  bool is_running() const noexcept;

  // === Component Dependencies ===
  
  /**
   * @brief Set ledger manager for blockchain data access
   * 
   * @param ledger Shared pointer to ledger manager
   * @note Required for block/account/transaction queries
   * @note Can be called before or after start(), takes effect immediately
   */
  void set_ledger_manager(std::shared_ptr<ledger::LedgerManager> ledger);
  
  /**
   * @brief Set validator core for consensus information
   * 
   * @param validator Shared pointer to validator core
   * @note Required for slot/epoch/leader schedule queries
   * @note Enables validator-specific RPC methods
   */
  void set_validator_core(std::shared_ptr<validator::ValidatorCore> validator);
  
  /**
   * @brief Set staking manager for delegation and rewards data
   * 
   * @param staking Shared pointer to staking manager
   * @note Required for stake account and inflation queries
   * @note Enables staking-related RPC methods
   */
  void set_staking_manager(std::shared_ptr<staking::StakingManager> staking);
  
  /**
   * @brief Set banking stage for transaction processing
   * 
   * @param banking Shared pointer to banking stage
   * @note Required for transaction submission and simulation
   * @note Enables sendTransaction and related methods
   */
  void set_banking_stage(std::shared_ptr<banking::BankingStage> banking);
  
  /**
   * @brief Set SVM execution engine for program interaction
   * 
   * @param engine Shared pointer to execution engine
   * @note Required for transaction simulation
   * @note Enables advanced transaction analysis features
   */
  void set_execution_engine(std::shared_ptr<svm::ExecutionEngine> engine);
  
  /**
   * @brief Set account manager for account state queries
   * 
   * @param accounts Shared pointer to account manager
   * @note Required for account information and program account queries
   * @note Critical for most account-related RPC methods
   */
  void set_account_manager(std::shared_ptr<svm::AccountManager> accounts);

  // === Custom Method Registration ===
  
  /**
   * @brief Register a custom RPC method handler
   * 
   * Allows registration of application-specific RPC methods beyond the
   * standard Solana API. Useful for debugging, monitoring, or custom features.
   * 
   * @param method RPC method name (e.g., "customGetInfo")
   * @param handler Function to handle requests for this method
   * @note Method names should follow camelCase convention
   * @note Custom methods can override built-in methods (use with caution)
   * @warning Handler function must be thread-safe
   */
  void register_method(const std::string &method, RpcHandler handler);

  // === Request Processing ===
  
  /**
   * @brief Process a JSON-RPC request and return response
   * 
   * Main entry point for processing RPC requests. Handles JSON parsing,
   * method dispatch, error handling, and response formatting.
   * 
   * @param request_json JSON-RPC 2.0 request as string
   * @return JSON-RPC 2.0 response as formatted string
   * @note Thread-safe and can handle concurrent requests
   * @note Invalid JSON returns proper JSON-RPC error response
   * @note All processing errors are caught and returned as RPC errors
   */
  std::string handle_request(const std::string &request_json);

  // === WebSocket Support ===
  
  /**
   * @brief Get access to WebSocket server for subscriptions
   * 
   * @return Shared pointer to WebSocket server or nullptr if not started
   * @note WebSocket server enables real-time subscriptions
   * @note Required for accountSubscribe, blockSubscribe, etc.
   */
  std::shared_ptr<WebSocketServer> get_websocket_server() noexcept {
    return websocket_server_;
  }
  
  /**
   * @brief Start WebSocket server for real-time subscriptions
   * 
   * @return true if WebSocket server started successfully
   * @note Starts on a port adjacent to main RPC port
   * @note Required for subscription-based RPC methods
   */
  bool start_websocket_server();
  
  /**
   * @brief Stop WebSocket server and close all subscription connections
   * 
   * @note Gracefully closes all active subscription connections
   * @note Safe to call multiple times
   */
  void stop_websocket_server() noexcept;

private:
  void register_account_methods();
  void register_block_methods();
  void register_transaction_methods();
  void register_network_methods();
  void register_validator_methods();
  void register_staking_methods();
  void register_utility_methods();
  void register_system_methods();
  void register_token_methods();
  void register_websocket_methods();
  void register_network_management_methods();

  // Account Methods
  RpcResponse get_account_info(const RpcRequest &request);
  RpcResponse get_balance(const RpcRequest &request);
  RpcResponse get_program_accounts(const RpcRequest &request);
  RpcResponse get_multiple_accounts(const RpcRequest &request);
  RpcResponse get_largest_accounts(const RpcRequest &request);
  RpcResponse get_minimum_balance_for_rent_exemption(const RpcRequest &request);
  RpcResponse get_account_info_and_context(const RpcRequest &request);
  RpcResponse get_balance_and_context(const RpcRequest &request);
  RpcResponse get_multiple_accounts_and_context(const RpcRequest &request);
  RpcResponse get_program_accounts_and_context(const RpcRequest &request);
  RpcResponse get_account_owner(const RpcRequest &request);

  // Block Methods
  RpcResponse get_slot(const RpcRequest &request);
  RpcResponse get_block(const RpcRequest &request);
  RpcResponse get_block_height(const RpcRequest &request);
  RpcResponse get_blocks(const RpcRequest &request);
  RpcResponse get_first_available_block(const RpcRequest &request);
  RpcResponse get_genesis_hash(const RpcRequest &request);
  RpcResponse get_slot_leaders(const RpcRequest &request);
  RpcResponse get_block_production(const RpcRequest &request);
  RpcResponse get_block_commitment(const RpcRequest &request);
  RpcResponse get_block_time(const RpcRequest &request);
  RpcResponse get_blocks_with_limit(const RpcRequest &request);
  RpcResponse get_confirmed_block(const RpcRequest &request);
  RpcResponse get_confirmed_blocks(const RpcRequest &request);
  RpcResponse get_confirmed_blocks_with_limit(const RpcRequest &request);

  // Transaction Methods
  RpcResponse get_transaction(const RpcRequest &request);
  RpcResponse send_transaction(const RpcRequest &request);
  RpcResponse send_bundle(const RpcRequest &request);
  RpcResponse simulate_transaction(const RpcRequest &request);
  RpcResponse get_signature_statuses(const RpcRequest &request);
  RpcResponse get_confirmed_signatures_for_address2(const RpcRequest &request);
  RpcResponse get_signatures_for_address(const RpcRequest &request);
  RpcResponse get_confirmed_transaction(const RpcRequest &request);

  // Network Methods
  RpcResponse get_cluster_nodes(const RpcRequest &request);
  RpcResponse get_version(const RpcRequest &request);
  RpcResponse get_health(const RpcRequest &request);
  RpcResponse get_identity(const RpcRequest &request);

  // Validator Methods
  RpcResponse get_vote_accounts(const RpcRequest &request);
  RpcResponse get_validator_info(const RpcRequest &request);
  RpcResponse get_leader_schedule(const RpcRequest &request);
  RpcResponse get_epoch_info(const RpcRequest &request);
  RpcResponse get_epoch_schedule(const RpcRequest &request);

  // Staking Methods
  RpcResponse get_stake_activation(const RpcRequest &request);
  RpcResponse get_inflation_governor(const RpcRequest &request);
  RpcResponse get_inflation_rate(const RpcRequest &request);
  RpcResponse get_inflation_reward(const RpcRequest &request);

  // Utility Methods
  RpcResponse get_recent_blockhash(const RpcRequest &request);
  RpcResponse get_fee_for_message(const RpcRequest &request);
  RpcResponse get_latest_blockhash(const RpcRequest &request);
  RpcResponse is_blockhash_valid(const RpcRequest &request);

  // System Methods
  RpcResponse get_slot_leader(const RpcRequest &request);
  RpcResponse minimum_ledger_slot(const RpcRequest &request);
  RpcResponse get_max_retransmit_slot(const RpcRequest &request);
  RpcResponse get_max_shred_insert_slot(const RpcRequest &request);
  RpcResponse get_highest_snapshot_slot(const RpcRequest &request);
  RpcResponse get_recent_performance_samples(const RpcRequest &request);
  RpcResponse get_recent_prioritization_fees(const RpcRequest &request);
  RpcResponse get_supply(const RpcRequest &request);
  RpcResponse get_transaction_count(const RpcRequest &request);
  RpcResponse request_airdrop(const RpcRequest &request);
  RpcResponse get_stake_minimum_delegation(const RpcRequest &request);
  RpcResponse get_snapshot_slot(const RpcRequest &request);
  RpcResponse get_fees(const RpcRequest &request);

  // Token Methods
  RpcResponse get_token_accounts_by_owner(const RpcRequest &request);
  RpcResponse get_token_supply(const RpcRequest &request);
  RpcResponse get_token_account_balance(const RpcRequest &request);
  RpcResponse get_token_accounts_by_delegate(const RpcRequest &request);
  RpcResponse get_token_largest_accounts(const RpcRequest &request);
  RpcResponse get_token_accounts_by_mint(const RpcRequest &request);

  // WebSocket Subscription Methods
  RpcResponse account_subscribe(const RpcRequest &request);
  RpcResponse account_unsubscribe(const RpcRequest &request);
  RpcResponse block_subscribe(const RpcRequest &request);
  RpcResponse block_unsubscribe(const RpcRequest &request);
  RpcResponse logs_subscribe(const RpcRequest &request);
  RpcResponse logs_unsubscribe(const RpcRequest &request);
  RpcResponse program_subscribe(const RpcRequest &request);
  RpcResponse program_unsubscribe(const RpcRequest &request);
  RpcResponse root_subscribe(const RpcRequest &request);
  RpcResponse root_unsubscribe(const RpcRequest &request);
  RpcResponse signature_subscribe(const RpcRequest &request);
  RpcResponse signature_unsubscribe(const RpcRequest &request);
  RpcResponse slot_subscribe(const RpcRequest &request);
  RpcResponse slot_unsubscribe(const RpcRequest &request);
  RpcResponse slots_updates_subscribe(const RpcRequest &request);
  RpcResponse slots_updates_unsubscribe(const RpcRequest &request);
  RpcResponse vote_subscribe(const RpcRequest &request);
  RpcResponse vote_unsubscribe(const RpcRequest &request);

  // Network Management Methods
  RpcResponse list_svm_networks(const RpcRequest &request);
  RpcResponse enable_svm_network(const RpcRequest &request);
  RpcResponse disable_svm_network(const RpcRequest &request);
  RpcResponse set_network_rpc_url(const RpcRequest &request);

  // Helper methods
  RpcResponse create_error_response(const std::string &id, int code,
                                    const std::string &message,
                                    bool id_is_number = false);
  std::string get_current_context() const;
  std::string format_account_info(const PublicKey &address,
                                  const svm::ProgramAccount &account) const;

  // Production utility methods
  std::string calculate_genesis_hash() const;
  std::string get_validator_identity() const;
  std::string process_transaction_submission(const RpcRequest &request) const;
  std::string compute_block_hash(const std::vector<uint8_t> &block_data) const;
  std::string encode_base58(const std::vector<uint8_t> &data) const;
  std::vector<uint8_t> decode_base58(const std::string &encoded) const;
  std::string
  compute_signature_hash(const std::vector<uint8_t> &signature_base) const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;

  ValidatorConfig config_;
  std::map<std::string, RpcHandler> methods_;

  // WebSocket server for real-time subscriptions
  std::shared_ptr<WebSocketServer> websocket_server_;

  // Component references
  std::shared_ptr<ledger::LedgerManager> ledger_manager_;
  std::shared_ptr<validator::ValidatorCore> validator_core_;
  std::shared_ptr<staking::StakingManager> staking_manager_;
  std::shared_ptr<banking::BankingStage> banking_stage_;
  std::shared_ptr<svm::ExecutionEngine> execution_engine_;
  std::shared_ptr<svm::AccountManager> account_manager_;

  // Performance optimization caches
  mutable std::map<std::string, std::pair<std::string, uint64_t>>
      account_cache_; // address -> (data, timestamp)
  mutable std::map<uint64_t, std::string> block_cache_; // slot -> block_data
  mutable std::string cached_supply_data_;
  mutable uint64_t cached_supply_timestamp_;
  mutable uint64_t cache_ttl_ms_ = 5000; // 5 second cache TTL

  // Cache management
  bool is_cache_valid(uint64_t timestamp) const;
  uint64_t get_current_timestamp_ms() const;
  
  // Transaction utilities
  std::string generate_transaction_signature(const std::string &transaction_data) const;
  std::string encode_base58_signature(const std::vector<uint8_t> &signature_bytes) const;
  
  // Fault tolerance mechanisms
  CircuitBreaker external_service_breaker_;
  DegradationManager degradation_manager_;
  RetryPolicy rpc_retry_policy_;
  
  // Fault-tolerant operation wrappers
  template<typename F>
  auto execute_with_fault_tolerance(F&& operation, const std::string& operation_name) -> decltype(operation()) {
    // Check if operation is allowed in current degradation mode
    if (!degradation_manager_.is_operation_allowed("rpc", operation_name)) {
      using ReturnType = decltype(operation());
      return ReturnType("Service unavailable due to degraded mode");
    }
    
    // Execute with circuit breaker protection
    return external_service_breaker_.execute([&]() {
      return FaultTolerance::retry_with_backoff(std::forward<F>(operation), rpc_retry_policy_);
    });
  }
};

} // namespace network
} // namespace slonana