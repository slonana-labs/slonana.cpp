#include "genesis/manager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <openssl/sha.h>

namespace slonana {
namespace genesis {

GenesisManager::GenesisManager() {}

GenesisConfig GenesisManager::create_network_config(NetworkType network_type) {
    GenesisConfig config;
    config.network_type = network_type;
    config.economics = create_default_economics(network_type);
    config.creation_time = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    switch (network_type) {
        case NetworkType::MAINNET:
            config.network_id = "mainnet";
            config.chain_id = 101;
            config.magic_bytes = {0x4d, 0x41, 0x49, 0x4e}; // "MAIN"
            config.description = "Slonana Mainnet Genesis Configuration";
            config.bootstrap_entrypoints = {
                "mainnet-seed1.slonana.org:8001",
                "mainnet-seed2.slonana.org:8001", 
                "mainnet-seed3.slonana.org:8001"
            };
            break;
            
        case NetworkType::TESTNET:
            config.network_id = "testnet";
            config.chain_id = 102;
            config.magic_bytes = {0x54, 0x45, 0x53, 0x54}; // "TEST"
            config.description = "Slonana Testnet Genesis Configuration";
            config.bootstrap_entrypoints = {
                "testnet-seed1.slonana.org:8001",
                "testnet-seed2.slonana.org:8001"
            };
            break;
            
        case NetworkType::DEVNET:
        default:
            config.network_id = "devnet";
            config.chain_id = 103;
            config.magic_bytes = {0x44, 0x45, 0x56, 0x4e}; // "DEVN"
            config.description = "Slonana Devnet Genesis Configuration";
            config.bootstrap_entrypoints = {
                "devnet-seed1.slonana.org:8001"
            };
            break;
    }
    
    return config;
}

MainnetConfig GenesisManager::create_mainnet_config() {
    MainnetConfig mainnet_config;
    mainnet_config.genesis = create_network_config(NetworkType::MAINNET);
    
    // Network discovery configuration
    mainnet_config.network_discovery.dns_seeds = {
        "mainnet-seeds.slonana.org",
        "seeds.slonana.org"
    };
    
    mainnet_config.network_discovery.entrypoints = get_mainnet_entrypoints();
    
    // Mainnet-specific settings
    mainnet_config.require_tls = true;
    mainnet_config.enable_metrics_reporting = true;
    mainnet_config.metrics_endpoint = "https://metrics.slonana.org/v1/submit";
    mainnet_config.require_validator_identity = true;
    mainnet_config.minimum_stake_for_voting = 1000000; // 1M lamports
    
    return mainnet_config;
}

common::Result<GenesisConfig> GenesisManager::load_genesis_config(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return common::Result<GenesisConfig>("Failed to open genesis config file: " + filepath);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    
    return deserialize_from_json(buffer.str());
}

common::Result<bool> GenesisManager::save_genesis_config(const GenesisConfig& config, const std::string& filepath) {
    auto validation_result = validate_genesis_config(config);
    if (!validation_result.is_ok()) {
        return common::Result<bool>("Invalid genesis configuration: " + validation_result.error());
    }
    
    std::string json = serialize_to_json(config);
    
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return common::Result<bool>("Failed to open file for writing: " + filepath);
    }
    
    file << json;
    file.close();
    
    return common::Result<bool>(true);
}

common::Result<ledger::Block> GenesisManager::create_genesis_block(const GenesisConfig& config) {
    auto validation_result = validate_genesis_config(config);
    if (!validation_result.is_ok()) {
        return common::Result<ledger::Block>("Invalid genesis configuration: " + validation_result.error());
    }
    
    ledger::Block genesis_block;
    genesis_block.slot = 0;
    genesis_block.parent_hash.resize(32, 0x00); // Genesis has no parent
    genesis_block.validator.resize(32, 0x00); // System validator
    genesis_block.block_signature.resize(64, 0x00); // No signature for genesis
    
    // Compute genesis hash from configuration
    genesis_block.block_hash = compute_genesis_hash(config);
    
    // Set creation timestamp
    genesis_block.timestamp = config.creation_time;
    
    return common::Result<ledger::Block>(std::move(genesis_block));
}

common::Result<bool> GenesisManager::validate_genesis_config(const GenesisConfig& config) {
    // Validate economic parameters
    if (!validate_economics(config.economics)) {
        return common::Result<bool>("Invalid economic configuration");
    }
    
    // Validate network parameters
    if (config.network_id.empty()) {
        return common::Result<bool>("Network ID cannot be empty");
    }
    
    if (config.magic_bytes.size() != 4) {
        return common::Result<bool>("Magic bytes must be exactly 4 bytes");
    }
    
    // Validate consensus parameters
    if (config.epoch_length == 0 || config.slots_per_epoch == 0) {
        return common::Result<bool>("Epoch parameters must be positive");
    }
    
    if (config.target_tick_duration_us == 0 || config.ticks_per_slot == 0) {
        return common::Result<bool>("PoH parameters must be positive");
    }
    
    // Validate validator allocations sum to expected total
    double total_allocation = config.economics.foundation_allocation + 
                            config.economics.team_allocation +
                            config.economics.community_allocation +
                            config.economics.validator_allocation;
    
    if (std::abs(total_allocation - 1.0) > 0.01) { // Allow 1% tolerance
        return common::Result<bool>("Total allocation percentages must sum to 1.0");
    }
    
    return common::Result<bool>(true);
}

common::Hash GenesisManager::compute_genesis_hash(const GenesisConfig& config) {
    std::string serialized_config = serialize_to_json(config);
    
    // Compute SHA256 hash
    common::Hash hash(32);
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, serialized_config.data(), serialized_config.size());
    SHA256_Final(hash.data(), &ctx);
    
    return hash;
}

std::vector<std::pair<common::PublicKey, uint64_t>> GenesisManager::create_genesis_accounts(
    const GenesisConfig& config) {
    
    std::vector<std::pair<common::PublicKey, uint64_t>> accounts;
    
    // Add predefined genesis accounts
    for (const auto& account : config.genesis_accounts) {
        accounts.push_back({account.first, account.second});
    }
    
    // Add validator stake accounts
    for (const auto& validator : config.genesis_validators) {
        accounts.push_back({validator.stake_account, validator.stake_amount});
    }
    
    return accounts;
}

std::string GenesisManager::network_type_to_string(NetworkType type) {
    switch (type) {
        case NetworkType::MAINNET: return "mainnet";
        case NetworkType::TESTNET: return "testnet";
        case NetworkType::DEVNET: return "devnet";
        case NetworkType::CUSTOM: return "custom";
        default: return "unknown";
    }
}

NetworkType GenesisManager::string_to_network_type(const std::string& str) {
    if (str == "mainnet") return NetworkType::MAINNET;
    if (str == "testnet") return NetworkType::TESTNET;
    if (str == "devnet") return NetworkType::DEVNET;
    if (str == "custom") return NetworkType::CUSTOM;
    return NetworkType::DEVNET; // Default
}

std::vector<NetworkEntrypoint> GenesisManager::get_mainnet_entrypoints() {
    return {
        {"mainnet-seed1.slonana.org", 8001, true, "us-east", "Primary US East seed"},
        {"mainnet-seed2.slonana.org", 8001, true, "eu-west", "Primary EU West seed"},
        {"mainnet-seed3.slonana.org", 8001, true, "asia-pac", "Primary Asia Pacific seed"},
        {"seed1.slonana.org", 8001, false, "us-west", "Secondary US West seed"},
        {"seed2.slonana.org", 8001, false, "eu-central", "Secondary EU Central seed"}
    };
}

std::vector<NetworkEntrypoint> GenesisManager::get_testnet_entrypoints() {
    return {
        {"testnet-seed1.slonana.org", 8001, true, "us-east", "Primary testnet seed"},
        {"testnet-seed2.slonana.org", 8001, true, "eu-west", "Secondary testnet seed"}
    };
}

// Private implementation methods

std::string GenesisManager::serialize_to_json(const GenesisConfig& config) {
    std::ostringstream json;
    json << "{";
    json << "\"network_id\":\"" << config.network_id << "\",";
    json << "\"chain_id\":" << config.chain_id << ",";
    json << "\"network_type\":\"" << network_type_to_string(config.network_type) << "\",";
    json << "\"version\":\"" << config.version << "\",";
    json << "\"description\":\"" << config.description << "\",";
    json << "\"creation_time\":" << config.creation_time << ",";
    
    // Magic bytes
    json << "\"magic_bytes\":[";
    for (size_t i = 0; i < config.magic_bytes.size(); ++i) {
        if (i > 0) json << ",";
        json << static_cast<int>(config.magic_bytes[i]);
    }
    json << "],";
    
    // Economic configuration
    json << "\"economics\":{";
    json << "\"total_supply\":" << config.economics.total_supply << ",";
    json << "\"initial_inflation_rate\":" << config.economics.initial_inflation_rate << ",";
    json << "\"foundation_rate\":" << config.economics.foundation_rate << ",";
    json << "\"taper_rate\":" << config.economics.taper_rate << ",";
    json << "\"foundation_allocation\":" << config.economics.foundation_allocation << ",";
    json << "\"team_allocation\":" << config.economics.team_allocation << ",";
    json << "\"community_allocation\":" << config.economics.community_allocation << ",";
    json << "\"validator_allocation\":" << config.economics.validator_allocation << ",";
    json << "\"min_validator_stake\":" << config.economics.min_validator_stake << ",";
    json << "\"min_delegation\":" << config.economics.min_delegation << ",";
    json << "\"vote_timeout_slashing\":" << config.economics.vote_timeout_slashing << ",";
    json << "\"equivocation_slashing\":" << config.economics.equivocation_slashing;
    json << "},";
    
    // Consensus parameters
    json << "\"consensus\":{";
    json << "\"epoch_length\":" << config.epoch_length << ",";
    json << "\"slots_per_epoch\":" << config.slots_per_epoch << ",";
    json << "\"target_tick_duration_us\":" << config.target_tick_duration_us << ",";
    json << "\"ticks_per_slot\":" << config.ticks_per_slot;
    json << "},";
    
    // Bootstrap entrypoints
    json << "\"bootstrap_entrypoints\":[";
    for (size_t i = 0; i < config.bootstrap_entrypoints.size(); ++i) {
        if (i > 0) json << ",";
        json << "\"" << config.bootstrap_entrypoints[i] << "\"";
    }
    json << "]";
    
    json << "}";
    return json.str();
}

common::Result<GenesisConfig> GenesisManager::deserialize_from_json(const std::string& json) {
    // Simple JSON parsing implementation
    // In a production system, use a proper JSON library like nlohmann/json
    
    GenesisConfig config;
    
    // Extract basic fields (simplified parsing)
    size_t pos = json.find("\"network_id\":");
    if (pos != std::string::npos) {
        size_t start = json.find("\"", pos + 13) + 1;
        size_t end = json.find("\"", start);
        if (end != std::string::npos) {
            config.network_id = json.substr(start, end - start);
        }
    }
    
    pos = json.find("\"chain_id\":");
    if (pos != std::string::npos) {
        size_t start = pos + 11;
        size_t end = json.find_first_of(",}", start);
        if (end != std::string::npos) {
            config.chain_id = std::stoi(json.substr(start, end - start));
        }
    }
    
    return common::Result<GenesisConfig>(std::move(config));
}

EconomicConfig GenesisManager::create_default_economics(NetworkType network_type) {
    EconomicConfig economics;
    
    switch (network_type) {
        case NetworkType::MAINNET:
            economics.total_supply = 1000000000; // 1 billion tokens
            economics.initial_inflation_rate = 0.05; // 5% for mainnet
            economics.min_validator_stake = 1000000; // 1M lamports
            break;
            
        case NetworkType::TESTNET:
            economics.total_supply = 100000000; // 100 million tokens
            economics.initial_inflation_rate = 0.08; // 8% for testnet
            economics.min_validator_stake = 100000; // 100K lamports
            break;
            
        case NetworkType::DEVNET:
        default:
            economics.total_supply = 10000000; // 10 million tokens
            economics.initial_inflation_rate = 0.10; // 10% for devnet
            economics.min_validator_stake = 10000; // 10K lamports
            break;
    }
    
    return economics;
}

bool GenesisManager::validate_economics(const EconomicConfig& economics) {
    if (economics.total_supply == 0) return false;
    if (economics.initial_inflation_rate < 0.0 || economics.initial_inflation_rate > 1.0) return false;
    if (economics.foundation_rate < 0.0 || economics.foundation_rate > 1.0) return false;
    if (economics.min_validator_stake == 0) return false;
    if (economics.min_delegation == 0) return false;
    if (economics.vote_timeout_slashing < 0.0 || economics.vote_timeout_slashing > 1.0) return false;
    if (economics.equivocation_slashing < 0.0 || economics.equivocation_slashing > 1.0) return false;
    
    return true;
}

} // namespace genesis
} // namespace slonana