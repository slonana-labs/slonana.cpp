#pragma once

#include "banking/banking_stage.h"
#include "common/types.h"
#include "ledger/manager.h"
#include "network/quic_client.h"
#include "network/quic_server.h"
#include <functional>
#include <memory>

namespace slonana {
namespace validator {

using namespace slonana::common;

/**
 * @file core.h
 * @brief Core validator implementation for Solana consensus and block production
 * 
 * This module provides the central ValidatorCore class that coordinates all
 * validator operations including consensus participation, block validation,
 * transaction processing, and network communication.
 */

/**
 * @brief Validator vote for consensus participation
 * 
 * Represents a validator's vote on a specific block/slot in the blockchain.
 * Votes are used in the consensus mechanism to determine the canonical chain.
 * 
 * @note Thread safety: This struct is not thread-safe. Use external synchronization
 *       when accessing from multiple threads.
 */
struct Vote {
  Slot slot;                    ///< Slot number being voted on
  Hash block_hash;             ///< Hash of the block being voted for
  PublicKey validator_identity; ///< Ed25519 public key of voting validator
  Signature vote_signature;    ///< Ed25519 signature over the vote data
  uint64_t timestamp;          ///< Unix timestamp when vote was created

  /**
   * @brief Serialize vote data for network transmission
   * @return Serialized vote as byte vector
   * @note Uses little-endian encoding for cross-platform compatibility
   */
  std::vector<uint8_t> serialize() const;
  
  /**
   * @brief Verify vote data integrity and format
   * @return true if vote has valid format, false otherwise
   * @note This performs format validation only, not cryptographic verification
   */
  bool verify() const noexcept;
};

/**
 * @brief Fork choice algorithm implementation for canonical chain selection
 * 
 * Implements a production-grade fork choice algorithm based on Agave's Greatest
 * Common Confirmed Depth (GCCD) approach. This algorithm determines the canonical
 * blockchain head by considering validator votes, stake weights, and confirmation depth.
 * 
 * @note Thread safety: All public methods are thread-safe and can be called
 *       concurrently from multiple threads.
 * @note Performance: Operations are optimized for frequent calls with O(log n)
 *       complexity for most operations.
 */
class ForkChoice {
public:
  /**
   * @brief Initialize fork choice with empty state
   * 
   * Creates a new fork choice instance ready to track blocks and votes.
   * Initial state has no known blocks or votes.
   */
  ForkChoice();
  
  /// Destructor - automatically cleans up all internal state
  ~ForkChoice();

  /**
   * @brief Add a new block to the fork choice algorithm
   * 
   * Incorporates a new block into the fork choice decision making. This will
   * update consensus weights and potentially change the canonical head.
   * 
   * @param block The block to add (must have valid hash and slot)
   * @note This is a potentially expensive operation as it recalculates weights
   * @warning Block must have valid parent hash for proper chain building
   */
  void add_block(const ledger::Block &block);
  
  /**
   * @brief Add a validator vote to influence fork choice
   * 
   * Incorporates a validator's vote into the consensus algorithm. The vote's
   * weight is determined by the validator's current stake amount.
   * 
   * @param vote The vote to incorporate (must be properly signed)
   * @note Votes for unknown blocks are cached until the block arrives
   */
  void add_vote(const Vote &vote);

  /**
   * @brief Get the current canonical chain head hash
   * @return Hash of the block considered the canonical head
   * @note This reflects the latest fork choice calculation
   */
  Hash get_head() const;
  
  /**
   * @brief Get the slot number of the canonical head
   * @return Slot number of the current canonical head block
   */
  Slot get_head_slot() const noexcept;

  /**
   * @brief Get all known fork heads in the system
   * @return Vector of block hashes representing different fork heads
   * @note Ordered by descending consensus weight
   */
  std::vector<Hash> get_forks() const;
  
  /**
   * @brief Calculate total stake weight supporting a fork
   * 
   * @param fork_head Hash of the fork head block
   * @return Total lamports staked on this fork chain
   * @note Includes inherited stake from ancestor blocks
   */
  uint64_t get_fork_weight(const Hash &fork_head) const;
  
  /**
   * @brief Get validator's current stake amount
   * 
   * @param validator_pubkey Public key of the validator
   * @return Stake amount in lamports, 0 if validator unknown
   * @note Stake amounts are cached and updated periodically
   */
  uint64_t get_validator_stake(const Hash &validator_pubkey) const;
  
  /**
   * @brief Calculate confirmation depth for a fork
   * 
   * Confirmation depth indicates how many subsequent blocks have been
   * built on top of this fork, providing confidence in its finality.
   * 
   * @param fork_head Hash of the fork head to check
   * @return Number of confirmation blocks (higher = more confident)
   */
  uint64_t get_confirmation_depth(const Hash &fork_head) const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;

  // Internal helper methods for consensus weight calculation
  uint64_t calculate_consensus_weight(const ledger::Block &block) const;
  void update_consensus_weights() const;
  Hash select_best_fork_head() const;
  bool is_ancestor(const Hash &potential_ancestor,
                   const Hash &descendant) const noexcept;
};

/**
 * @brief Comprehensive block validation engine
 * 
 * Provides all validation logic needed to verify blocks before accepting them
 * into the ledger. This includes structural validation, cryptographic verification,
 * and transaction-level checks.
 * 
 * @note Thread safety: All methods are thread-safe and can be called concurrently
 * @note Performance: Validation operations are optimized for high throughput
 */
class BlockValidator {
public:
  /**
   * @brief Initialize block validator with ledger dependency
   * 
   * @param ledger Shared pointer to ledger manager for state queries
   * @throws std::invalid_argument if ledger is null
   */
  explicit BlockValidator(std::shared_ptr<ledger::LedgerManager> ledger);
  
  /// Destructor - automatically cleans up validation state
  ~BlockValidator();

  /**
   * @brief Validate block structural integrity
   * 
   * Checks that the block has proper format, required fields, and internal
   * consistency without performing expensive cryptographic operations.
   * 
   * @param block Block to validate structurally
   * @return true if structure is valid, false otherwise
   * @note This is a fast pre-check before expensive validation
   */
  bool validate_block_structure(const ledger::Block &block) const noexcept;
  
  /**
   * @brief Verify block's cryptographic signature
   * 
   * Performs Ed25519 signature verification to ensure the block was signed
   * by the claimed validator and hasn't been tampered with.
   * 
   * @param block Block to verify signature for
   * @return true if signature is valid, false otherwise
   * @note This is computationally expensive (~0.5ms per signature)
   */
  bool validate_block_signature(const ledger::Block &block) const;
  
  /**
   * @brief Validate all transactions within the block
   * 
   * Performs comprehensive validation of each transaction including:
   * - Account balance checks
   * - Program execution validation
   * - Nonce and fee verification
   * 
   * @param block Block containing transactions to validate
   * @return true if all transactions are valid, false otherwise
   * @note This can be very expensive for blocks with many transactions
   */
  bool validate_transactions(const ledger::Block &block) const;

  /**
   * @brief Perform complete block validation
   * 
   * Executes all validation checks in the proper order. This is the main
   * entry point for block validation and should be used for production.
   * 
   * @param block Block to fully validate
   * @return Result indicating success or detailed error information
   * @note Combines all validation checks with detailed error reporting
   */
  common::Result<bool> validate_block(const ledger::Block &block) const;

  /**
   * @brief Verify block fits properly in the blockchain
   * 
   * Checks that the block's parent hash matches the expected previous block
   * and that slot numbers form a valid sequence.
   * 
   * @param block Block to check for chain continuity
   * @return true if block maintains chain continuity, false otherwise
   */
  bool validate_chain_continuity(const ledger::Block &block) const;

private:
  std::shared_ptr<ledger::LedgerManager> ledger_;
  
  class Impl;
  std::unique_ptr<Impl> impl_;
};

/**
 * @brief Central coordinator for all validator operations and consensus participation
 * 
 * ValidatorCore is the main class that orchestrates the operation of a Solana validator node.
 * It coordinates between consensus mechanisms, transaction processing, network communication,
 * and ledger management to participate in the Solana network as a validator.
 * 
 * Key responsibilities:
 * - Consensus participation through voting and block production
 * - Transaction processing and validation
 * - Network communication via QUIC and gossip protocols
 * - Integration with banking stage for transaction handling
 * - Performance monitoring and metrics collection
 * 
 * @note Thread safety: All public methods are thread-safe unless explicitly noted
 * @note Exception safety: Strong guarantee - operations either succeed completely or leave
 *       the validator in a valid state
 * @note Performance: Optimized for high throughput with minimal allocation in hot paths
 * 
 * Lifecycle:
 * 1. Construct with required dependencies (ledger, identity)
 * 2. Call start() to begin validator operations
 * 3. Process blocks and votes as they arrive
 * 4. Call stop() for graceful shutdown
 * 
 * Example usage:
 * @code
 * auto ledger = std::make_shared<LedgerManager>(config.ledger_path);
 * ValidatorCore validator(ledger, validator_identity);
 * 
 * auto result = validator.start();
 * if (!result.is_ok()) {
 *     std::cerr << "Failed to start: " << result.error() << std::endl;
 *     return;
 * }
 * 
 * // Validator is now running and participating in consensus
 * // Process incoming data...
 * 
 * validator.stop();  // Graceful shutdown
 * @endcode
 */
class ValidatorCore {
public:
  /// Callback function type for handling new votes
  using VoteCallback = std::function<void(const Vote &)>;
  
  /// Callback function type for handling new blocks
  using BlockCallback = std::function<void(const ledger::Block &)>;

  /**
   * @brief Construct validator with required dependencies
   * 
   * @param ledger Shared pointer to ledger manager (must not be null)
   * @param validator_identity Ed25519 public key for network identification
   * @throws std::invalid_argument if ledger is null or identity is invalid
   * @note Validator starts in stopped state - call start() to begin operations
   */
  ValidatorCore(std::shared_ptr<ledger::LedgerManager> ledger,
                const PublicKey &validator_identity);
  
  /**
   * @brief Destructor with automatic cleanup
   * 
   * Automatically stops validator if running and cleans up all resources.
   * Waits for background threads to complete gracefully.
   */
  ~ValidatorCore();

  // === Core Lifecycle Management ===
  
  /**
   * @brief Start validator operations and begin consensus participation
   * 
   * Initializes all subsystems including:
   * - Proof of History clock
   * - Fork choice algorithm
   * - Banking stage for transactions
   * - Network communication layers
   * - Background processing threads
   * 
   * @return Result indicating success or detailed error information
   * @note Must be called before any other validator operations
   * @note Safe to call multiple times (subsequent calls are no-ops)
   */
  common::Result<bool> start();
  
  /**
   * @brief Stop validator operations gracefully
   * 
   * Cleanly shuts down all subsystems:
   * - Stops accepting new transactions
   * - Completes processing of pending operations
   * - Closes network connections
   * - Joins all background threads
   * 
   * @note Safe to call multiple times
   * @note Blocks until clean shutdown is complete
   */
  void stop() noexcept;

  // === Block and Vote Processing ===
  
  /**
   * @brief Process an incoming block from the network
   * 
   * Validates and incorporates a new block into the validator's view of
   * the blockchain. This may trigger fork choice updates and vote generation.
   * 
   * @param block The block to process (must pass validation)
   * @note This operation may be expensive for blocks with many transactions
   * @warning Invalid blocks are rejected but errors are logged, not thrown
   */
  void process_block(const ledger::Block &block);
  
  /**
   * @brief Process an incoming vote from another validator
   * 
   * Incorporates another validator's vote into the local fork choice algorithm.
   * This influences which fork is considered canonical.
   * 
   * @param vote The vote to process (signature will be verified)
   * @note Votes for unknown blocks are cached until the block arrives
   */
  void process_vote(const Vote &vote);

  // === Transaction Processing ===
  
  /**
   * @brief Process a single transaction
   * 
   * Routes the transaction to the banking stage for validation and execution.
   * The transaction will be included in future blocks if valid.
   * 
   * @param transaction Shared pointer to transaction data
   * @note Transaction validation happens asynchronously
   * @warning Null transactions are ignored
   */
  void process_transaction(std::shared_ptr<ledger::Transaction> transaction);
  
  /**
   * @brief Process multiple transactions efficiently
   * 
   * Batch processes multiple transactions for better performance.
   * Transactions are validated and executed in parallel where possible.
   * 
   * @param transactions Vector of shared pointers to transaction data
   * @note Uses parallel processing for better throughput
   * @note Empty vector is handled gracefully
   */
  void process_transactions(
      std::vector<std::shared_ptr<ledger::Transaction>> transactions);

  // === Event Callbacks ===
  
  /**
   * @brief Register callback for vote events
   * 
   * @param callback Function to call when validator generates a new vote
   * @note Callback is invoked from background thread - ensure thread safety
   * @note Only one callback can be registered (replaces previous)
   */
  void set_vote_callback(VoteCallback callback);
  
  /**
   * @brief Register callback for block events
   * 
   * @param callback Function to call when validator produces a new block
   * @note Callback is invoked from background thread - ensure thread safety
   * @note Only one callback can be registered (replaces previous)
   */
  void set_block_callback(BlockCallback callback);

  // === Status and Information ===
  
  /**
   * @brief Check if validator is currently running
   * @return true if validator is active and processing, false otherwise
   * @note Thread-safe query of validator state
   */
  bool is_running() const noexcept;
  
  /**
   * @brief Get current slot from Proof of History clock
   * @return Current slot number based on PoH progression
   * @note This is the "official" network time in slots
   */
  Slot get_current_slot() const noexcept;
  
  /**
   * @brief Get highest processed block slot
   * @return Slot number of the latest block processed by this validator
   * @note May lag behind current_slot during catch-up
   */
  Slot get_blockchain_head_slot() const noexcept;
  
  /**
   * @brief Get hash of current canonical head block
   * @return Hash of the block considered the current chain head
   * @note Reflects latest fork choice decision
   */
  Hash get_current_head() const;

  // === RPC and External Interface ===
  
  /**
   * @brief Get validator's Ed25519 public key
   * @return Copy of the validator's identity public key
   * @note Used for network identification and voting
   */
  PublicKey get_validator_identity() const noexcept { return validator_identity_; }
  
  /**
   * @brief Retrieve genesis block data
   * @return Result containing genesis block bytes or error message
   * @note Genesis block is required for network compatibility verification
   */
  common::Result<std::vector<uint8_t>> get_genesis_block() const;
  
  /**
   * @brief Determine slot leader for a given slot
   * 
   * Uses the validator schedule to determine which validator should produce
   * a block for the specified slot.
   * 
   * @param slot Slot number to query
   * @return Base58-encoded public key of the slot leader
   * @note Returns empty string if slot is too far in future/past
   */
  std::string get_slot_leader(Slot slot) const;

  // === Component Access ===
  
  /**
   * @brief Get mutable access to banking stage
   * @return Pointer to banking stage or nullptr if not initialized
   * @warning Use carefully - banking stage has its own thread safety requirements
   */
  banking::BankingStage *get_banking_stage() noexcept { 
    return banking_stage_.get(); 
  }
  
  /**
   * @brief Get immutable access to banking stage
   * @return Const pointer to banking stage or nullptr if not initialized
   * @note Safe for read-only operations from any thread
   */
  const banking::BankingStage *get_banking_stage() const noexcept {
    return banking_stage_.get();
  }

  // === QUIC Network Management ===
  
  /**
   * @brief Enable QUIC networking for high-performance communication
   * 
   * Initializes QUIC server and client for UDP-based network communication.
   * QUIC provides better performance than TCP for validator networking.
   * 
   * @param port Port number for QUIC server (default: 8000)
   * @return true if QUIC networking was successfully enabled
   * @note QUIC provides multiplexed, low-latency communication
   * @note Safe to call multiple times with same port
   */
  bool enable_quic_networking(uint16_t port = 8000);
  
  /**
   * @brief Disable QUIC networking and close connections
   * @return true if QUIC was successfully disabled
   * @note Gracefully closes all active QUIC connections
   */
  bool disable_quic_networking() noexcept;
  
  /**
   * @brief Get access to QUIC server component
   * @return Pointer to QUIC server or nullptr if not enabled
   * @warning Direct access requires understanding of QUIC internals
   */
  network::QuicServer *get_quic_server() noexcept { 
    return quic_server_.get(); 
  }
  
  /**
   * @brief Get access to QUIC client component
   * @return Pointer to QUIC client or nullptr if not enabled
   * @warning Direct access requires understanding of QUIC internals
   */
  network::QuicClient *get_quic_client() noexcept { 
    return quic_client_.get(); 
  }

  // === Performance Monitoring ===
  
  /**
   * @brief Get transaction processing statistics
   * 
   * @return Statistics including throughput, latency, and error rates
   * @note Statistics are reset periodically to provide recent performance data
   */
  banking::BankingStage::Statistics get_transaction_statistics() const;
  
  /**
   * @brief Get QUIC networking performance statistics
   * 
   * @return Statistics including connection count, bandwidth, and packet loss
   * @note Returns empty statistics if QUIC is not enabled
   */
  network::QuicServer::Statistics get_quic_statistics() const;

private:
  // === Core Dependencies ===
  std::shared_ptr<ledger::LedgerManager> ledger_;    ///< Blockchain state management
  std::unique_ptr<ForkChoice> fork_choice_;          ///< Consensus algorithm implementation  
  std::unique_ptr<BlockValidator> block_validator_;  ///< Block validation engine
  PublicKey validator_identity_;                     ///< Ed25519 public key for network identity

  // === Transaction Processing ===
  std::unique_ptr<banking::BankingStage> banking_stage_; ///< High-throughput transaction processor

  // === Network Communication ===
  std::unique_ptr<network::QuicServer> quic_server_;  ///< QUIC server for incoming connections
  std::unique_ptr<network::QuicClient> quic_client_;  ///< QUIC client for outgoing connections
  bool quic_enabled_;                                 ///< Track QUIC networking state

  // === Implementation Details ===
  class Impl;
  std::unique_ptr<Impl> impl_;  ///< Pimpl pattern for implementation details
};

} // namespace validator
} // namespace slonana