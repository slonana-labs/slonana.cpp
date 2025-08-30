#pragma once

#include "common/types.h"
#include "network/websocket_server.h"
#include <memory>
#include <functional>
#include <string>
#include <map>

namespace slonana {

// Forward declarations
namespace ledger { class LedgerManager; }
namespace validator { class ValidatorCore; }
namespace staking { class StakingManager; }
namespace svm { 
    class ExecutionEngine; 
    class AccountManager;
    struct ProgramAccount;
}

namespace network {

using namespace slonana::common;

/**
 * JSON-RPC 2.0 request structure
 */
struct RpcRequest {
    std::string jsonrpc = "2.0";
    std::string method;
    std::string params;  // JSON string
    std::string id;
    bool id_is_number = false;  // Track if ID was originally a number
};

/**
 * JSON-RPC 2.0 response structure
 */
struct RpcResponse {
    std::string jsonrpc = "2.0";
    std::string result;  // JSON string
    std::string error;   // JSON string (only if error occurred)
    std::string id;
    bool id_is_number = false;  // Track if ID should be rendered as number
    
    std::string to_json() const;
};

/**
 * Comprehensive Solana RPC server implementation
 */
class SolanaRpcServer {
public:
    using RpcHandler = std::function<RpcResponse(const RpcRequest&)>;
    
    explicit SolanaRpcServer(const ValidatorConfig& config);
    ~SolanaRpcServer();
    
    // Server lifecycle
    Result<bool> start();
    void stop();
    bool is_running() const;
    
    // Component registration for RPC methods
    void set_ledger_manager(std::shared_ptr<ledger::LedgerManager> ledger);
    void set_validator_core(std::shared_ptr<validator::ValidatorCore> validator);
    void set_staking_manager(std::shared_ptr<staking::StakingManager> staking);
    void set_execution_engine(std::shared_ptr<svm::ExecutionEngine> engine);
    void set_account_manager(std::shared_ptr<svm::AccountManager> accounts);
    
    // Custom method registration
    void register_method(const std::string& method, RpcHandler handler);
    
    // Handle JSON-RPC request
    std::string handle_request(const std::string& request_json);
    
    // WebSocket support
    std::shared_ptr<WebSocketServer> get_websocket_server() { return websocket_server_; }
    bool start_websocket_server();
    void stop_websocket_server();

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
    RpcResponse get_account_info(const RpcRequest& request);
    RpcResponse get_balance(const RpcRequest& request);
    RpcResponse get_program_accounts(const RpcRequest& request);
    RpcResponse get_multiple_accounts(const RpcRequest& request);
    RpcResponse get_largest_accounts(const RpcRequest& request);
    RpcResponse get_minimum_balance_for_rent_exemption(const RpcRequest& request);
    RpcResponse get_account_info_and_context(const RpcRequest& request);
    RpcResponse get_balance_and_context(const RpcRequest& request);
    RpcResponse get_multiple_accounts_and_context(const RpcRequest& request);
    RpcResponse get_program_accounts_and_context(const RpcRequest& request);
    RpcResponse get_account_owner(const RpcRequest& request);
    
    // Block Methods
    RpcResponse get_slot(const RpcRequest& request);
    RpcResponse get_block(const RpcRequest& request);
    RpcResponse get_block_height(const RpcRequest& request);
    RpcResponse get_blocks(const RpcRequest& request);
    RpcResponse get_first_available_block(const RpcRequest& request);
    RpcResponse get_genesis_hash(const RpcRequest& request);
    RpcResponse get_slot_leaders(const RpcRequest& request);
    RpcResponse get_block_production(const RpcRequest& request);
    RpcResponse get_block_commitment(const RpcRequest& request);
    RpcResponse get_block_time(const RpcRequest& request);
    RpcResponse get_blocks_with_limit(const RpcRequest& request);
    RpcResponse get_confirmed_block(const RpcRequest& request);
    RpcResponse get_confirmed_blocks(const RpcRequest& request);
    RpcResponse get_confirmed_blocks_with_limit(const RpcRequest& request);
    
    // Transaction Methods
    RpcResponse get_transaction(const RpcRequest& request);
    RpcResponse send_transaction(const RpcRequest& request);
    RpcResponse simulate_transaction(const RpcRequest& request);
    RpcResponse get_signature_statuses(const RpcRequest& request);
    RpcResponse get_confirmed_signatures_for_address2(const RpcRequest& request);
    RpcResponse get_signatures_for_address(const RpcRequest& request);
    RpcResponse get_confirmed_transaction(const RpcRequest& request);
    
    // Network Methods
    RpcResponse get_cluster_nodes(const RpcRequest& request);
    RpcResponse get_version(const RpcRequest& request);
    RpcResponse get_health(const RpcRequest& request);
    RpcResponse get_identity(const RpcRequest& request);
    
    // Validator Methods
    RpcResponse get_vote_accounts(const RpcRequest& request);
    RpcResponse get_leader_schedule(const RpcRequest& request);
    RpcResponse get_epoch_info(const RpcRequest& request);
    RpcResponse get_epoch_schedule(const RpcRequest& request);
    
    // Staking Methods
    RpcResponse get_stake_activation(const RpcRequest& request);
    RpcResponse get_inflation_governor(const RpcRequest& request);
    RpcResponse get_inflation_rate(const RpcRequest& request);
    RpcResponse get_inflation_reward(const RpcRequest& request);
    
    // Utility Methods
    RpcResponse get_recent_blockhash(const RpcRequest& request);
    RpcResponse get_fee_for_message(const RpcRequest& request);
    RpcResponse get_latest_blockhash(const RpcRequest& request);
    RpcResponse is_blockhash_valid(const RpcRequest& request);
    
    // System Methods
    RpcResponse get_slot_leader(const RpcRequest& request);
    RpcResponse minimum_ledger_slot(const RpcRequest& request);
    RpcResponse get_max_retransmit_slot(const RpcRequest& request);
    RpcResponse get_max_shred_insert_slot(const RpcRequest& request);
    RpcResponse get_highest_snapshot_slot(const RpcRequest& request);
    RpcResponse get_recent_performance_samples(const RpcRequest& request);
    RpcResponse get_recent_prioritization_fees(const RpcRequest& request);
    RpcResponse get_supply(const RpcRequest& request);
    RpcResponse get_transaction_count(const RpcRequest& request);
    RpcResponse request_airdrop(const RpcRequest& request);
    RpcResponse get_stake_minimum_delegation(const RpcRequest& request);
    RpcResponse get_snapshot_slot(const RpcRequest& request);
    RpcResponse get_fees(const RpcRequest& request);
    
    // Token Methods
    RpcResponse get_token_accounts_by_owner(const RpcRequest& request);
    RpcResponse get_token_supply(const RpcRequest& request);
    RpcResponse get_token_account_balance(const RpcRequest& request);
    RpcResponse get_token_accounts_by_delegate(const RpcRequest& request);
    RpcResponse get_token_largest_accounts(const RpcRequest& request);
    RpcResponse get_token_accounts_by_mint(const RpcRequest& request);
    
    // WebSocket Subscription Methods
    RpcResponse account_subscribe(const RpcRequest& request);
    RpcResponse account_unsubscribe(const RpcRequest& request);
    RpcResponse block_subscribe(const RpcRequest& request);
    RpcResponse block_unsubscribe(const RpcRequest& request);
    RpcResponse logs_subscribe(const RpcRequest& request);
    RpcResponse logs_unsubscribe(const RpcRequest& request);
    RpcResponse program_subscribe(const RpcRequest& request);
    RpcResponse program_unsubscribe(const RpcRequest& request);
    RpcResponse root_subscribe(const RpcRequest& request);
    RpcResponse root_unsubscribe(const RpcRequest& request);
    RpcResponse signature_subscribe(const RpcRequest& request);
    RpcResponse signature_unsubscribe(const RpcRequest& request);
    RpcResponse slot_subscribe(const RpcRequest& request);
    RpcResponse slot_unsubscribe(const RpcRequest& request);
    RpcResponse slots_updates_subscribe(const RpcRequest& request);
    RpcResponse slots_updates_unsubscribe(const RpcRequest& request);
    RpcResponse vote_subscribe(const RpcRequest& request);
    RpcResponse vote_unsubscribe(const RpcRequest& request);
    
    // Network Management Methods
    RpcResponse list_svm_networks(const RpcRequest& request);
    RpcResponse enable_svm_network(const RpcRequest& request);
    RpcResponse disable_svm_network(const RpcRequest& request);
    RpcResponse set_network_rpc_url(const RpcRequest& request);
    
    // Helper methods
    RpcResponse create_error_response(const std::string& id, int code, const std::string& message, bool id_is_number = false);
    std::string get_current_context() const;
    std::string format_account_info(const PublicKey& address, const svm::ProgramAccount& account) const;
    
    // Production utility methods
    std::string calculate_genesis_hash() const;
    std::string get_validator_identity() const;
    std::string process_transaction_submission(const RpcRequest& request) const;
    std::string compute_block_hash(const std::vector<uint8_t>& block_data) const;
    std::string encode_base58(const std::vector<uint8_t>& data) const;
    std::string compute_signature_hash(const std::vector<uint8_t>& signature_base) const;
    
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
    std::shared_ptr<svm::ExecutionEngine> execution_engine_;
    std::shared_ptr<svm::AccountManager> account_manager_;
    
    // Performance optimization caches
    mutable std::map<std::string, std::pair<std::string, uint64_t>> account_cache_; // address -> (data, timestamp)
    mutable std::map<uint64_t, std::string> block_cache_; // slot -> block_data
    mutable std::string cached_supply_data_;
    mutable uint64_t cached_supply_timestamp_;
    mutable uint64_t cache_ttl_ms_ = 5000; // 5 second cache TTL
    
    // Cache management
    bool is_cache_valid(uint64_t timestamp) const;
    uint64_t get_current_timestamp_ms() const;
};

} // namespace network
} // namespace slonana