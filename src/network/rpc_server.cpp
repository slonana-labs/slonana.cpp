#include "network/rpc_server.h"
#include "ledger/manager.h"
#include "validator/core.h"
#include "staking/manager.h"
#include "svm/engine.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <regex>
#include <thread>
#include <atomic>

namespace slonana {
namespace network {

// Helper function to parse JSON (simplified implementation)
namespace {
    std::string extract_json_value(const std::string& json, const std::string& key) {
        std::regex pattern("\"" + key + "\"\\s*:\\s*([^,}]+)");
        std::smatch match;
        if (std::regex_search(json, match, pattern)) {
            std::string value = match[1].str();
            // Remove quotes if it's a string
            if (value.front() == '"' && value.back() == '"') {
                return value.substr(1, value.length() - 2);
            }
            return value;
        }
        return "";
    }
    
    std::string extract_json_array(const std::string& json, const std::string& key) {
        std::regex pattern("\"" + key + "\"\\s*:\\s*(\\[[^\\]]*\\])");
        std::smatch match;
        if (std::regex_search(json, match, pattern)) {
            return match[1].str();
        }
        return "[]";
    }
}

std::string RpcResponse::to_json() const {
    std::ostringstream oss;
    oss << "{\"jsonrpc\":\"" << jsonrpc << "\",";
    if (!error.empty()) {
        oss << "\"error\":" << error << ",";
    } else {
        oss << "\"result\":" << result << ",";
    }
    oss << "\"id\":\"" << id << "\"}";
    return oss.str();
}

class SolanaRpcServer::Impl {
public:
    explicit Impl(const ValidatorConfig& config) : config_(config), running_(false) {}
    
    ValidatorConfig config_;
    std::atomic<bool> running_;
    std::thread server_thread_;
};

SolanaRpcServer::SolanaRpcServer(const ValidatorConfig& config)
    : impl_(std::make_unique<Impl>(config)), config_(config) {
    
    // Register all Solana RPC methods
    register_account_methods();
    register_block_methods();
    register_transaction_methods();
    register_network_methods();
    register_validator_methods();
    register_staking_methods();
    register_utility_methods();
}

SolanaRpcServer::~SolanaRpcServer() {
    stop();
}

Result<bool> SolanaRpcServer::start() {
    if (impl_->running_.load()) {
        return Result<bool>("RPC server already running");
    }
    
    std::cout << "Starting Solana RPC server on " << config_.rpc_bind_address << std::endl;
    std::cout << "Registered " << methods_.size() << " RPC methods" << std::endl;
    
    impl_->running_.store(true);
    
    // In a real implementation, this would start an HTTP server
    // For now, we'll simulate the server running
    impl_->server_thread_ = std::thread([this]() {
        while (impl_->running_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    
    return Result<bool>(true);
}

void SolanaRpcServer::stop() {
    if (impl_->running_.load()) {
        std::cout << "Stopping Solana RPC server" << std::endl;
        impl_->running_.store(false);
        
        if (impl_->server_thread_.joinable()) {
            impl_->server_thread_.join();
        }
    }
}

bool SolanaRpcServer::is_running() const {
    return impl_->running_.load();
}

void SolanaRpcServer::set_ledger_manager(std::shared_ptr<ledger::LedgerManager> ledger) {
    ledger_manager_ = ledger;
}

void SolanaRpcServer::set_validator_core(std::shared_ptr<validator::ValidatorCore> validator) {
    validator_core_ = validator;
}

void SolanaRpcServer::set_staking_manager(std::shared_ptr<staking::StakingManager> staking) {
    staking_manager_ = staking;
}

void SolanaRpcServer::set_execution_engine(std::shared_ptr<svm::ExecutionEngine> engine) {
    execution_engine_ = engine;
}

void SolanaRpcServer::set_account_manager(std::shared_ptr<svm::AccountManager> accounts) {
    account_manager_ = accounts;
}

void SolanaRpcServer::register_method(const std::string& method, RpcHandler handler) {
    methods_[method] = std::move(handler);
}

std::string SolanaRpcServer::handle_request(const std::string& request_json) {
    try {
        RpcRequest request;
        request.jsonrpc = extract_json_value(request_json, "jsonrpc");
        request.method = extract_json_value(request_json, "method");
        request.params = extract_json_value(request_json, "params");
        request.id = extract_json_value(request_json, "id");
        
        auto it = methods_.find(request.method);
        if (it == methods_.end()) {
            return create_error_response(request.id, -32601, "Method not found").to_json();
        }
        
        return it->second(request).to_json();
        
    } catch (const std::exception& e) {
        return create_error_response("", -32700, "Parse error").to_json();
    }
}

void SolanaRpcServer::register_account_methods() {
    // Account information methods
    register_method("getAccountInfo", [this](const RpcRequest& req) { return get_account_info(req); });
    register_method("getBalance", [this](const RpcRequest& req) { return get_balance(req); });
    register_method("getProgramAccounts", [this](const RpcRequest& req) { return get_program_accounts(req); });
    register_method("getMultipleAccounts", [this](const RpcRequest& req) { return get_multiple_accounts(req); });
    register_method("getLargestAccounts", [this](const RpcRequest& req) { return get_largest_accounts(req); });
    register_method("getMinimumBalanceForRentExemption", [this](const RpcRequest& req) { return get_minimum_balance_for_rent_exemption(req); });
}

void SolanaRpcServer::register_block_methods() {
    // Block and slot information methods
    register_method("getSlot", [this](const RpcRequest& req) { return get_slot(req); });
    register_method("getBlock", [this](const RpcRequest& req) { return get_block(req); });
    register_method("getBlockHeight", [this](const RpcRequest& req) { return get_block_height(req); });
    register_method("getBlocks", [this](const RpcRequest& req) { return get_blocks(req); });
    register_method("getFirstAvailableBlock", [this](const RpcRequest& req) { return get_first_available_block(req); });
    register_method("getGenesisHash", [this](const RpcRequest& req) { return get_genesis_hash(req); });
    register_method("getSlotLeaders", [this](const RpcRequest& req) { return get_slot_leaders(req); });
    register_method("getBlockProduction", [this](const RpcRequest& req) { return get_block_production(req); });
}

void SolanaRpcServer::register_transaction_methods() {
    // Transaction methods
    register_method("getTransaction", [this](const RpcRequest& req) { return get_transaction(req); });
    register_method("sendTransaction", [this](const RpcRequest& req) { return send_transaction(req); });
    register_method("simulateTransaction", [this](const RpcRequest& req) { return simulate_transaction(req); });
    register_method("getSignatureStatuses", [this](const RpcRequest& req) { return get_signature_statuses(req); });
    register_method("getConfirmedSignaturesForAddress2", [this](const RpcRequest& req) { return get_confirmed_signatures_for_address2(req); });
}

void SolanaRpcServer::register_network_methods() {
    // Network and cluster methods
    register_method("getClusterNodes", [this](const RpcRequest& req) { return get_cluster_nodes(req); });
    register_method("getVersion", [this](const RpcRequest& req) { return get_version(req); });
    register_method("getHealth", [this](const RpcRequest& req) { return get_health(req); });
    register_method("getIdentity", [this](const RpcRequest& req) { return get_identity(req); });
}

void SolanaRpcServer::register_validator_methods() {
    // Validator and consensus methods
    register_method("getVoteAccounts", [this](const RpcRequest& req) { return get_vote_accounts(req); });
    register_method("getLeaderSchedule", [this](const RpcRequest& req) { return get_leader_schedule(req); });
    register_method("getEpochInfo", [this](const RpcRequest& req) { return get_epoch_info(req); });
    register_method("getEpochSchedule", [this](const RpcRequest& req) { return get_epoch_schedule(req); });
}

void SolanaRpcServer::register_staking_methods() {
    // Staking and rewards methods
    register_method("getStakeActivation", [this](const RpcRequest& req) { return get_stake_activation(req); });
    register_method("getInflationGovernor", [this](const RpcRequest& req) { return get_inflation_governor(req); });
    register_method("getInflationRate", [this](const RpcRequest& req) { return get_inflation_rate(req); });
    register_method("getInflationReward", [this](const RpcRequest& req) { return get_inflation_reward(req); });
}

void SolanaRpcServer::register_utility_methods() {
    // Utility and fee methods
    register_method("getRecentBlockhash", [this](const RpcRequest& req) { return get_recent_blockhash(req); });
    register_method("getFeeForMessage", [this](const RpcRequest& req) { return get_fee_for_message(req); });
    register_method("getLatestBlockhash", [this](const RpcRequest& req) { return get_latest_blockhash(req); });
    register_method("isBlockhashValid", [this](const RpcRequest& req) { return is_blockhash_valid(req); });
}

// Account Methods Implementation
RpcResponse SolanaRpcServer::get_account_info(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    try {
        // Extract account address from params
        std::string address = extract_json_value(request.params, "address");
        if (address.empty()) {
            return create_error_response(request.id, -32602, "Invalid params");
        }
        
        // Get account info from account manager
        if (account_manager_) {
            PublicKey pubkey(address.begin(), address.end());
            auto account_info = account_manager_->get_account(pubkey);
            
            if (account_info.has_value()) {
                response.result = format_account_info(pubkey, account_info.value());
            } else {
                response.result = "null";
            }
        } else {
            response.result = "null";
        }
        
    } catch (const std::exception& e) {
        return create_error_response(request.id, -32603, "Internal error");
    }
    
    return response;
}

RpcResponse SolanaRpcServer::get_balance(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    try {
        std::string address = extract_json_value(request.params, "address");
        if (address.empty()) {
            return create_error_response(request.id, -32602, "Invalid params");
        }
        
        if (account_manager_) {
            PublicKey pubkey(address.begin(), address.end());
            auto account_info = account_manager_->get_account(pubkey);
            
            uint64_t balance = account_info.has_value() ? account_info.value().lamports : 0;
            
            std::ostringstream oss;
            oss << "{\"context\":" << get_current_context() << ",\"value\":" << balance << "}";
            response.result = oss.str();
        } else {
            response.result = "{\"context\":" + get_current_context() + ",\"value\":0}";
        }
        
    } catch (const std::exception& e) {
        return create_error_response(request.id, -32603, "Internal error");
    }
    
    return response;
}

RpcResponse SolanaRpcServer::get_program_accounts(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    // Stub implementation - would query accounts owned by program
    response.result = "[]";
    return response;
}

RpcResponse SolanaRpcServer::get_multiple_accounts(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    // Stub implementation - would get multiple accounts
    response.result = "{\"context\":" + get_current_context() + ",\"value\":[]}";
    return response;
}

RpcResponse SolanaRpcServer::get_largest_accounts(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    // Stub implementation
    response.result = "{\"context\":" + get_current_context() + ",\"value\":[]}";
    return response;
}

RpcResponse SolanaRpcServer::get_minimum_balance_for_rent_exemption(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    // Default rent exemption threshold (simplified)
    response.result = "890880";  // Default minimum balance in lamports
    return response;
}

// Block Methods Implementation
RpcResponse SolanaRpcServer::get_slot(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    uint64_t slot = validator_core_ ? validator_core_->get_current_slot() : 0;
    response.result = std::to_string(slot);
    
    return response;
}

RpcResponse SolanaRpcServer::get_block(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    try {
        std::string slot_str = extract_json_value(request.params, "slot");
        if (slot_str.empty()) {
            return create_error_response(request.id, -32602, "Invalid params");
        }
        
        uint64_t slot = std::stoull(slot_str);
        
        if (ledger_manager_) {
            auto block = ledger_manager_->get_block_by_slot(slot);
            if (block.has_value()) {
                // Convert hash to hex string for JSON
                auto hash_to_hex = [](const Hash& hash) {
                    std::ostringstream oss;
                    for (auto byte : hash) {
                        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte);
                    }
                    return oss.str();
                };
                
                std::ostringstream oss;
                oss << "{\"blockHash\":\"" << hash_to_hex(block.value().block_hash) << "\","
                    << "\"blockHeight\":" << slot << ","
                    << "\"blockhash\":\"" << hash_to_hex(block.value().parent_hash) << "\","
                    << "\"parentSlot\":" << (slot > 0 ? slot - 1 : 0) << ","
                    << "\"transactions\":[]}";
                response.result = oss.str();
            } else {
                response.result = "null";
            }
        } else {
            response.result = "null";
        }
        
    } catch (const std::exception& e) {
        return create_error_response(request.id, -32603, "Internal error");
    }
    
    return response;
}

RpcResponse SolanaRpcServer::get_block_height(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    uint64_t height = validator_core_ ? validator_core_->get_current_slot() : 0;
    response.result = std::to_string(height);
    
    return response;
}

RpcResponse SolanaRpcServer::get_blocks(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    // Stub implementation - would return block slots in range
    response.result = "[]";
    return response;
}

RpcResponse SolanaRpcServer::get_first_available_block(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    response.result = "0";  // Genesis block
    return response;
}

RpcResponse SolanaRpcServer::get_genesis_hash(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    // Genesis hash (placeholder)
    response.result = "\"5eykt4UsFv8P8NJdTREpY1vzqKqZKvdpKuc147dw2N9d\"";
    return response;
}

RpcResponse SolanaRpcServer::get_slot_leaders(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    // Stub implementation
    response.result = "[]";
    return response;
}

RpcResponse SolanaRpcServer::get_block_production(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    // Stub implementation
    response.result = "{\"context\":" + get_current_context() + ",\"value\":{\"byIdentity\":{},\"range\":{\"firstSlot\":0,\"lastSlot\":0}}}";
    return response;
}

// Network Methods Implementation
RpcResponse SolanaRpcServer::get_health(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    response.result = impl_->running_.load() ? "\"ok\"" : "\"unhealthy\"";
    return response;
}

RpcResponse SolanaRpcServer::get_version(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    response.result = "{\"solana-core\":\"1.17.0\",\"feature-set\":\"3746818610\"}";
    return response;
}

RpcResponse SolanaRpcServer::get_identity(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    // Placeholder identity
    response.result = "{\"identity\":\"5eykt4UsFv8P8NJdTREpY1vzqKqZKvdpKuc147dw2N9d\"}";
    return response;
}

RpcResponse SolanaRpcServer::get_cluster_nodes(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    // Stub implementation
    response.result = "[]";
    return response;
}

// Transaction Methods Implementation (stubs for now)
RpcResponse SolanaRpcServer::get_transaction(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    response.result = "null";
    return response;
}

RpcResponse SolanaRpcServer::send_transaction(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    response.result = "\"transaction_signature_placeholder\"";
    return response;
}

RpcResponse SolanaRpcServer::simulate_transaction(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    response.result = "{\"context\":" + get_current_context() + ",\"value\":{\"err\":null,\"logs\":[]}}";
    return response;
}

RpcResponse SolanaRpcServer::get_signature_statuses(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    response.result = "{\"context\":" + get_current_context() + ",\"value\":[]}";
    return response;
}

RpcResponse SolanaRpcServer::get_confirmed_signatures_for_address2(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    response.result = "[]";
    return response;
}

// Validator Methods Implementation (stubs for now)
RpcResponse SolanaRpcServer::get_vote_accounts(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    response.result = "{\"current\":[],\"delinquent\":[]}";
    return response;
}

RpcResponse SolanaRpcServer::get_leader_schedule(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    response.result = "{}";
    return response;
}

RpcResponse SolanaRpcServer::get_epoch_info(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    uint64_t current_slot = validator_core_ ? validator_core_->get_current_slot() : 0;
    
    std::ostringstream oss;
    oss << "{\"absoluteSlot\":" << current_slot << ","
        << "\"blockHeight\":" << current_slot << ","
        << "\"epoch\":0,"
        << "\"slotIndex\":" << current_slot << ","
        << "\"slotsInEpoch\":432000,"
        << "\"transactionCount\":0}";
    
    response.result = oss.str();
    return response;
}

RpcResponse SolanaRpcServer::get_epoch_schedule(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    response.result = "{\"firstNormalEpoch\":0,\"firstNormalSlot\":0,\"leaderScheduleSlotOffset\":432000,\"slotsPerEpoch\":432000,\"warmup\":false}";
    return response;
}

// Staking Methods Implementation (stubs for now)
RpcResponse SolanaRpcServer::get_stake_activation(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    response.result = "{\"active\":0,\"inactive\":0,\"state\":\"inactive\"}";
    return response;
}

RpcResponse SolanaRpcServer::get_inflation_governor(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    response.result = "{\"foundation\":0.05,\"foundationTerm\":7.0,\"initial\":0.08,\"taper\":0.15,\"terminal\":0.015}";
    return response;
}

RpcResponse SolanaRpcServer::get_inflation_rate(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    response.result = "{\"epoch\":0,\"foundation\":0.05,\"total\":0.08,\"validator\":0.03}";
    return response;
}

RpcResponse SolanaRpcServer::get_inflation_reward(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    response.result = "[]";
    return response;
}

// Utility Methods Implementation
RpcResponse SolanaRpcServer::get_recent_blockhash(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    std::string recent_hash = "11111111111111111111111111111111";
    if (validator_core_) {
        auto hash_vec = validator_core_->get_current_head();
        if (!hash_vec.empty()) {
            std::ostringstream oss;
            for (auto byte : hash_vec) {
                oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte);
            }
            recent_hash = oss.str();
        }
    }
    
    std::ostringstream oss;
    oss << "{\"context\":" << get_current_context() << ","
        << "\"value\":{\"blockhash\":\"" << recent_hash << "\",\"feeCalculator\":{\"lamportsPerSignature\":5000}}}";
    
    response.result = oss.str();
    return response;
}

RpcResponse SolanaRpcServer::get_latest_blockhash(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    std::string latest_hash = "11111111111111111111111111111111";
    if (validator_core_) {
        auto hash_vec = validator_core_->get_current_head();
        if (!hash_vec.empty()) {
            std::ostringstream oss;
            for (auto byte : hash_vec) {
                oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte);
            }
            latest_hash = oss.str();
        }
    }
    
    std::ostringstream oss;
    oss << "{\"context\":" << get_current_context() << ","
        << "\"value\":{\"blockhash\":\"" << latest_hash << "\",\"lastValidBlockHeight\":" 
        << (validator_core_ ? validator_core_->get_current_slot() : 0) << "}}";
    
    response.result = oss.str();
    return response;
}

RpcResponse SolanaRpcServer::get_fee_for_message(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    response.result = "{\"context\":" + get_current_context() + ",\"value\":5000}";
    return response;
}

RpcResponse SolanaRpcServer::is_blockhash_valid(const RpcRequest& request) {
    RpcResponse response;
    response.id = request.id;
    
    response.result = "{\"context\":" + get_current_context() + ",\"value\":true}";
    return response;
}

// Helper Methods Implementation
RpcResponse SolanaRpcServer::create_error_response(const std::string& id, int code, const std::string& message) {
    RpcResponse response;
    response.id = id;
    
    std::ostringstream oss;
    oss << "{\"code\":" << code << ",\"message\":\"" << message << "\"}";
    response.error = oss.str();
    
    return response;
}

std::string SolanaRpcServer::get_current_context() const {
    uint64_t slot = validator_core_ ? validator_core_->get_current_slot() : 0;
    return "{\"slot\":" + std::to_string(slot) + "}";
}

std::string SolanaRpcServer::format_account_info(const PublicKey& address, const svm::ProgramAccount& account) const {
    std::ostringstream oss;
    oss << "{\"context\":" << get_current_context() << ","
        << "\"value\":{"
        << "\"data\":[\"" << std::string(account.data.begin(), std::min(account.data.end(), account.data.begin() + 32)) << "\",\"base58\"],"
        << "\"executable\":" << (account.executable ? "true" : "false") << ","
        << "\"lamports\":" << account.lamports << ","
        << "\"owner\":\"" << std::string(account.owner.begin(), std::min(account.owner.end(), account.owner.begin() + 32)) << "\","
        << "\"rentEpoch\":" << account.rent_epoch
        << "}}";
    return oss.str();
}

} // namespace network
} // namespace slonana