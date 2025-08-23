#pragma once

#include "genesis/config.h"
#include "common/types.h"
#include "ledger/manager.h"
#include <memory>

namespace slonana {
namespace genesis {

/**
 * Genesis block creation and management utilities
 */
class GenesisManager {
public:
    GenesisManager();
    ~GenesisManager() = default;

    /**
     * Create a genesis configuration for a specific network type
     */
    static GenesisConfig create_network_config(NetworkType network_type);
    
    /**
     * Create a mainnet genesis configuration
     */
    static MainnetConfig create_mainnet_config();
    
    /**
     * Load genesis configuration from JSON file
     */
    static common::Result<GenesisConfig> load_genesis_config(const std::string& filepath);
    
    /**
     * Save genesis configuration to JSON file
     */
    static common::Result<bool> save_genesis_config(const GenesisConfig& config, const std::string& filepath);
    
    /**
     * Create the actual genesis block from configuration
     */
    static common::Result<ledger::Block> create_genesis_block(const GenesisConfig& config);
    
    /**
     * Validate a genesis configuration
     */
    static common::Result<bool> validate_genesis_config(const GenesisConfig& config);
    
    /**
     * Compute the genesis hash from configuration
     */
    static common::Hash compute_genesis_hash(const GenesisConfig& config);
    
    /**
     * Create initial validator accounts from genesis configuration
     */
    static std::vector<std::pair<common::PublicKey, uint64_t>> create_genesis_accounts(
        const GenesisConfig& config);
    
    /**
     * Network type helpers
     */
    static std::string network_type_to_string(NetworkType type);
    static NetworkType string_to_network_type(const std::string& str);
    
    /**
     * Get predefined mainnet entrypoints
     */
    static std::vector<NetworkEntrypoint> get_mainnet_entrypoints();
    
    /**
     * Get predefined testnet entrypoints  
     */
    static std::vector<NetworkEntrypoint> get_testnet_entrypoints();

private:
    /**
     * Internal JSON serialization helpers
     */
    static std::string serialize_to_json(const GenesisConfig& config);
    static common::Result<GenesisConfig> deserialize_from_json(const std::string& json);
    
    /**
     * Create default economic configuration for network type
     */
    static EconomicConfig create_default_economics(NetworkType network_type);
    
    /**
     * Validate economic parameters
     */
    static bool validate_economics(const EconomicConfig& economics);
};

/**
 * Genesis CLI tool functionality
 */
class GenesisCliTool {
public:
    /**
     * Create genesis configuration command
     */
    static int create_genesis_command(int argc, char* argv[]);
    
    /**
     * Verify genesis configuration command
     */
    static int verify_genesis_command(int argc, char* argv[]);
    
    /**
     * Print genesis information command
     */
    static int info_genesis_command(int argc, char* argv[]);
    
    /**
     * Show CLI usage
     */
    static void print_usage();

private:
    /**
     * Parse command line arguments for genesis creation
     */
    static common::Result<GenesisConfig> parse_creation_args(int argc, char* argv[]);
    
    /**
     * Parse validator list from file
     */
    static common::Result<std::vector<GenesisValidator>> parse_validator_list(const std::string& filepath);
};

} // namespace genesis
} // namespace slonana