/**
 * @file slonana_validator.h
 * @brief Main header for the SolanaValidator class, the core orchestrator of the validator node.
 */
#pragma once

#include "banking/banking_stage.h"
#include "common/types.h"
#include "ledger/manager.h"
#include "network/gossip.h"
#include "network/rpc_server.h"
#include "staking/manager.h"
#include "svm/engine.h"
#include "validator/core.h"
#include "validator/snapshot_bootstrap.h"
#include <atomic>
#include <memory>

namespace slonana {

using namespace slonana::common;

/**
 * @brief The main class that orchestrates all components of the Slonana validator.
 * @details This class is the central hub of the validator, responsible for initializing,
 * starting, stopping, and managing all the major subsystems, including networking (Gossip and RPC),
 * the ledger, the SVM execution engine, and the consensus core.
 */
class SolanaValidator {
public:
  /**
   * @brief Constructs a new SolanaValidator instance.
   * @param config The configuration settings for the validator node.
   */
  explicit SolanaValidator(const ValidatorConfig &config);

  /**
   * @brief Destructor for the SolanaValidator. Ensures a graceful shutdown.
   */
  ~SolanaValidator();

  /**
   * @brief Initializes the validator and all its sub-components.
   * @details This includes loading the validator identity, setting up logging,
   * initializing the ledger, and preparing network protocols.
   * @return A Result indicating success or failure.
   */
  Result<bool> initialize();

  /**
   * @brief Starts the validator's main event loops and network services.
   * @details Begins gossip, opens the RPC server, and starts the consensus engine.
   * @return A Result indicating success or failure.
   */
  Result<bool> start();

  /**
   * @brief Stops all active validator components gracefully.
   */
  void stop();

  /**
   * @brief Performs a complete shutdown and cleanup of the validator.
   */
  void shutdown();

  /**
   * @brief Checks if the validator is currently running.
   * @return True if the validator is running, false otherwise.
   */
  bool is_running() const;

  /**
   * @brief Checks if the validator has been successfully initialized.
   * @return True if initialized, false otherwise.
   */
  bool is_initialized() const;

  /**
   * @brief Gets a shared pointer to the GossipProtocol component.
   * @return A shared pointer to the gossip protocol instance.
   */
  std::shared_ptr<network::GossipProtocol> get_gossip_protocol() const;

  /**
   * @brief Gets a shared pointer to the SolanaRpcServer component.
   * @return A shared pointer to the RPC server instance.
   */
  std::shared_ptr<network::SolanaRpcServer> get_rpc_server() const;

  /**
   * @brief Gets a shared pointer to the LedgerManager component.
   * @return A shared pointer to the ledger manager instance.
   */
  std::shared_ptr<ledger::LedgerManager> get_ledger_manager() const;

  /**
   * @brief Gets a shared pointer to the ValidatorCore consensus component.
   * @return A shared pointer to the validator core instance.
   */
  std::shared_ptr<validator::ValidatorCore> get_validator_core() const;

  /**
   * @brief Gets a shared pointer to the StakingManager component.
   * @return A shared pointer to the staking manager instance.
   */
  std::shared_ptr<staking::StakingManager> get_staking_manager() const;

  /**
   * @brief Gets a shared pointer to the SVM ExecutionEngine.
   * @return A shared pointer to the execution engine instance.
   */
  std::shared_ptr<svm::ExecutionEngine> get_execution_engine() const;

  /**
   * @brief A collection of performance and status metrics for the validator.
   */
  struct ValidatorStats {
    /// @brief The total number of blocks processed since startup.
    uint64_t blocks_processed = 0;
    /// @brief The total number of transactions processed since startup.
    uint64_t transactions_processed = 0;
    /// @brief The total number of votes cast by this validator.
    uint64_t votes_cast = 0;
    /// @brief The number of slots this validator is behind the network's current slot.
    uint64_t slots_behind = 0;
    /// @brief The current slot the validator is processing.
    Slot current_slot = 0;
    /// @brief The hash of the latest block processed (the current head of the chain).
    Hash current_head;
    /// @brief The total uptime of the validator in seconds.
    uint64_t uptime_seconds = 0;
    /// @brief The total stake (in lamports) delegated to this validator.
    Lamports total_stake = 0;
    /// @brief The number of peers currently connected via the gossip protocol.
    uint32_t connected_peers = 0;
  };

  /**
   * @brief Retrieves the current statistics for the validator.
   * @return A `ValidatorStats` struct containing the latest metrics.
   */
  ValidatorStats get_stats() const;

  /**
   * @brief Updates the validator's configuration with new settings.
   * @note Some configuration changes may require a restart to take effect.
   * @param new_config The new configuration to apply.
   * @return A Result indicating success or failure.
   */
  Result<bool> update_config(const ValidatorConfig &new_config);

  /**
   * @brief Gets the current configuration of the validator.
   * @return A const reference to the current `ValidatorConfig`.
   */
  const ValidatorConfig &get_config() const;

private:
  // Core components
  std::shared_ptr<network::GossipProtocol> gossip_protocol_;
  std::shared_ptr<network::SolanaRpcServer> rpc_server_;
  std::shared_ptr<ledger::LedgerManager> ledger_manager_;
  std::shared_ptr<validator::ValidatorCore> validator_core_;
  std::shared_ptr<staking::StakingManager> staking_manager_;
  std::shared_ptr<banking::BankingStage> banking_stage_;
  std::shared_ptr<svm::ExecutionEngine> execution_engine_;
  std::shared_ptr<svm::AccountManager> account_manager_;
  std::unique_ptr<validator::SnapshotBootstrapManager> snapshot_bootstrap_;

  // Configuration and state
  ValidatorConfig config_;
  PublicKey validator_identity_;
  std::atomic<bool> initialized_{false};
  std::atomic<bool> running_{false};

  // Internal event handlers
  void on_block_received(const ledger::Block &block);
  void on_vote_received(const validator::Vote &vote);

  // Identity management helpers
  common::Result<std::vector<uint8_t>>
  load_validator_identity(const std::string &keypair_path);
  std::vector<uint8_t> generate_validator_identity();
  void on_gossip_message(const network::NetworkMessage &message);

  // Component initialization helpers
  Result<bool> initialize_identity();
  Result<bool> initialize_components();
  Result<bool> setup_event_handlers();
  Result<bool> bootstrap_ledger();
  
  // Logging and alerting setup
  void setupLoggingAndAlerting();

  // PIMPL idiom to hide implementation details
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace slonana