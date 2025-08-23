#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <map>

namespace slonana {
namespace genesis {

/**
 * Network-specific parameters for different blockchain networks
 */
enum class NetworkType {
    DEVNET,
    TESTNET,
    MAINNET,
    CUSTOM
};

/**
 * Economic parameters for the network
 */
struct EconomicConfig {
    uint64_t total_supply = 1000000000; // 1 billion tokens
    double initial_inflation_rate = 0.08; // 8% annual
    double foundation_rate = 0.05; // 5% to foundation
    double taper_rate = -0.15; // -15% annual reduction
    
    // Token naming configuration
    std::string token_symbol = "SOL"; // Main token symbol (e.g., "SLON", "SOL")
    std::string base_unit_name = "lamports"; // Base unit name (e.g., "aldrins", "lamports")
    
    // Initial distribution percentages (should sum to 1.0)
    double foundation_allocation = 0.20;
    double team_allocation = 0.15;
    double community_allocation = 0.15;
    double validator_allocation = 0.50;
    
    // Staking parameters
    uint64_t min_validator_stake = 1000000; // 1M base units minimum
    uint64_t min_delegation = 1000; // 1K base units minimum
    
    // Slashing conditions
    double vote_timeout_slashing = 0.05; // 5% slash for downtime
    double equivocation_slashing = 1.0; // 100% slash for double voting
};

/**
 * Initial validator configuration for genesis
 */
struct GenesisValidator {
    common::PublicKey identity;
    common::PublicKey vote_account;
    common::PublicKey stake_account;
    uint64_t stake_amount;
    uint16_t commission_rate; // Basis points (e.g., 500 = 5%)
    std::string info;
};

/**
 * Genesis block configuration
 */
struct GenesisConfig {
    // Network identification
    NetworkType network_type = NetworkType::DEVNET;
    std::string network_id = "devnet";
    uint16_t chain_id = 103; // Devnet default
    
    // Network magic bytes for protocol identification
    std::vector<uint8_t> magic_bytes = {0x01, 0x02, 0x03, 0x04};
    
    // Economic configuration
    EconomicConfig economics;
    
    // Consensus parameters
    uint64_t epoch_length = 432000; // ~2 days at 400ms slots
    uint32_t slots_per_epoch = 432000;
    uint32_t target_tick_duration_us = 400; // 400 microseconds
    uint32_t ticks_per_slot = 64;
    
    // Genesis validators
    std::vector<GenesisValidator> genesis_validators;
    
    // Genesis accounts (predefined accounts with balances)
    std::map<common::PublicKey, uint64_t> genesis_accounts;
    
    // Timestamp and versioning
    uint64_t creation_time = 0; // Unix timestamp
    std::string version = "1.0.0";
    std::string description;
    
    // Bootstrap entrypoints for network discovery
    std::vector<std::string> bootstrap_entrypoints;
    
    // Expected genesis hash (for validation)
    common::Hash expected_genesis_hash;
};

/**
 * Network entrypoint configuration
 */
struct NetworkEntrypoint {
    std::string address;
    uint16_t port;
    bool is_primary = false;
    std::string region;
    std::string description;
};

/**
 * Network discovery configuration
 */
struct NetworkDiscoveryConfig {
    // DNS seed servers
    std::vector<std::string> dns_seeds;
    
    // Hard-coded entrypoints for bootstrap
    std::vector<NetworkEntrypoint> entrypoints;
    
    // Discovery settings
    uint32_t discovery_interval_ms = 30000; // 30 seconds
    uint32_t max_peers = 1000;
    uint32_t connection_timeout_ms = 10000;
    uint32_t retry_attempts = 3;
};

/**
 * Complete mainnet configuration
 */
struct MainnetConfig {
    GenesisConfig genesis;
    NetworkDiscoveryConfig network_discovery;
    
    // Mainnet-specific settings
    bool require_tls = true;
    bool enable_metrics_reporting = true;
    std::string metrics_endpoint;
    
    // Security settings
    bool require_validator_identity = true;
    uint64_t minimum_stake_for_voting = 1000000;
    std::vector<common::PublicKey> trusted_validators;
};

} // namespace genesis
} // namespace slonana