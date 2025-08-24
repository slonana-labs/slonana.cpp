#pragma once

#include "common/types.h"
#include "network/gossip.h"
#include "network/rpc_server.h"
#include "ledger/manager.h"
#include "validator/core.h"
#include "staking/manager.h"
#include "svm/engine.h"
#include <memory>
#include <atomic>

namespace slonana {

using namespace slonana::common;

/**
 * Main Solana validator class orchestrating all components
 */
class SolanaValidator {
public:
    explicit SolanaValidator(const ValidatorConfig& config);
    ~SolanaValidator();
    
    // Lifecycle management
    Result<bool> initialize();
    Result<bool> start();
    void stop();
    void shutdown();
    
    // Validator status
    bool is_running() const;
    bool is_initialized() const;
    
    // Component access (for advanced usage)
    std::shared_ptr<network::GossipProtocol> get_gossip_protocol() const;
    std::shared_ptr<network::SolanaRpcServer> get_rpc_server() const;
    std::shared_ptr<ledger::LedgerManager> get_ledger_manager() const;
    std::shared_ptr<validator::ValidatorCore> get_validator_core() const;
    std::shared_ptr<staking::StakingManager> get_staking_manager() const;
    std::shared_ptr<svm::ExecutionEngine> get_execution_engine() const;
    
    // Validator metrics and statistics
    struct ValidatorStats {
        uint64_t blocks_processed = 0;
        uint64_t transactions_processed = 0;
        uint64_t votes_cast = 0;
        uint64_t slots_behind = 0;
        Slot current_slot = 0;
        Hash current_head;
        uint64_t uptime_seconds = 0;
        Lamports total_stake = 0;
        uint32_t connected_peers = 0;
    };
    
    ValidatorStats get_stats() const;
    
    // Configuration updates (some require restart)
    Result<bool> update_config(const ValidatorConfig& new_config);
    const ValidatorConfig& get_config() const;

private:
    // Core components
    std::shared_ptr<network::GossipProtocol> gossip_protocol_;
    std::shared_ptr<network::SolanaRpcServer> rpc_server_;
    std::shared_ptr<ledger::LedgerManager> ledger_manager_;
    std::shared_ptr<validator::ValidatorCore> validator_core_;
    std::shared_ptr<staking::StakingManager> staking_manager_;
    std::shared_ptr<svm::ExecutionEngine> execution_engine_;
    std::shared_ptr<svm::AccountManager> account_manager_;
    
    // Configuration and state
    ValidatorConfig config_;
    PublicKey validator_identity_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    
    // Internal event handlers
    void on_block_received(const ledger::Block& block);
    void on_vote_received(const validator::Vote& vote);
    
    // Identity management helpers
    common::Result<std::vector<uint8_t>> load_validator_identity(const std::string& keypair_path);
    std::vector<uint8_t> generate_validator_identity();
    void on_gossip_message(const network::NetworkMessage& message);
    
    // Component initialization helpers
    Result<bool> initialize_identity();
    Result<bool> initialize_components();
    Result<bool> setup_event_handlers();
    
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace slonana