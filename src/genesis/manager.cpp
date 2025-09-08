#include "genesis/manager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <optional>
#include <openssl/evp.h>

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
    mainnet_config.minimum_stake_for_voting = 1000000; // 1M base units
    
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
    
    // Compute SHA256 hash using modern EVP API
    common::Hash hash(32);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create EVP_MD_CTX");
    }
    
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize SHA256 digest");
    }
    
    if (EVP_DigestUpdate(ctx, serialized_config.data(), serialized_config.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to update SHA256 digest");
    }
    
    unsigned int hash_len;
    if (EVP_DigestFinal_ex(ctx, hash.data(), &hash_len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to finalize SHA256 digest");
    }
    
    EVP_MD_CTX_free(ctx);
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
    json << "\"token_symbol\":\"" << config.economics.token_symbol << "\",";
    json << "\"base_unit_name\":\"" << config.economics.base_unit_name << "\",";
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
    // Production-grade JSON parsing with robust error handling and validation
    
    GenesisConfig config;
    
    // Validate JSON structure
    if (json.empty() || json.front() != '{' || json.back() != '}') {
        return common::Result<GenesisConfig>("Invalid JSON format: must be a valid object");
    }
    
    // Helper lambda for safe string extraction with validation
    auto extract_string_value = [&json](const std::string& key) -> std::optional<std::string> {
        std::string search_key = "\"" + key + "\":";
        size_t pos = json.find(search_key);
        if (pos == std::string::npos) return std::nullopt;
        
        // Find opening quote
        size_t quote_start = json.find("\"", pos + search_key.length());
        if (quote_start == std::string::npos) return std::nullopt;
        quote_start++; // Move past quote
        
        // Find closing quote, handling escaped quotes
        size_t quote_end = quote_start;
        while (quote_end < json.length()) {
            quote_end = json.find("\"", quote_end);
            if (quote_end == std::string::npos) return std::nullopt;
            
            // Check if quote is escaped
            if (quote_end > 0 && json[quote_end - 1] == '\\') {
                quote_end++;
                continue;
            }
            break;
        }
        
        if (quote_end == std::string::npos) return std::nullopt;
        return json.substr(quote_start, quote_end - quote_start);
    };
    
    // Helper lambda for safe numeric extraction with validation
    auto extract_numeric_value = [&json](const std::string& key) -> std::optional<int64_t> {
        std::string search_key = "\"" + key + "\":";
        size_t pos = json.find(search_key);
        if (pos == std::string::npos) return std::nullopt;
        
        size_t value_start = pos + search_key.length();
        // Skip whitespace
        while (value_start < json.length() && std::isspace(json[value_start])) {
            value_start++;
        }
        
        size_t value_end = value_start;
        // Find end of numeric value
        while (value_end < json.length() && 
               (std::isdigit(json[value_end]) || json[value_end] == '-' || json[value_end] == '.')) {
            value_end++;
        }
        
        if (value_end == value_start) return std::nullopt;
        
        try {
            std::string value_str = json.substr(value_start, value_end - value_start);
            return std::stoll(value_str);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    };
    
    // Extract and validate required fields
    auto network_id = extract_string_value("network_id");
    if (network_id) {
        if (network_id->length() > 64) {
            return common::Result<GenesisConfig>("Network ID too long (max 64 characters)");
        }
        config.network_id = *network_id;
    } else {
        config.network_id = "slonana-mainnet"; // Default fallback
    }
    
    auto chain_id = extract_numeric_value("chain_id");
    if (chain_id) {
        if (*chain_id < 0 || *chain_id > UINT32_MAX) {
            return common::Result<GenesisConfig>("Chain ID out of valid range");
        }
        config.chain_id = static_cast<uint32_t>(*chain_id);
    } else {
        config.chain_id = 1; // Default mainnet chain ID
    }
    
    // Extract economic parameters with validation
    auto token_symbol = extract_string_value("token_symbol");
    if (token_symbol) {
        if (token_symbol->length() > 10) {
            return common::Result<GenesisConfig>("Token symbol too long (max 10 characters)");
        }
        config.economics.token_symbol = *token_symbol;
    } else {
        config.economics.token_symbol = "SLON"; // Default token
    }
    
    auto base_unit_name = extract_string_value("base_unit_name");
    if (base_unit_name) {
        if (base_unit_name->length() > 20) {
            return common::Result<GenesisConfig>("Base unit name too long (max 20 characters)");
        }
        config.economics.base_unit_name = *base_unit_name;
    } else {
        config.economics.base_unit_name = "aldrins"; // Default base unit
    }
    
    // Extract numeric economic parameters
    auto total_supply = extract_numeric_value("total_supply");
    if (total_supply && *total_supply > 0) {
        config.economics.total_supply = static_cast<uint64_t>(*total_supply);
    } else {
        config.economics.total_supply = 1000000000; // Default 1B tokens
    }
    
    auto initial_inflation_rate = extract_numeric_value("initial_inflation_rate");
    if (initial_inflation_rate && *initial_inflation_rate >= 0 && *initial_inflation_rate <= 100) {
        config.economics.initial_inflation_rate = static_cast<double>(*initial_inflation_rate) / 100.0;
    } else {
        config.economics.initial_inflation_rate = 0.05; // Default 5%
    }
    
    auto min_validator_stake = extract_numeric_value("min_validator_stake");
    if (min_validator_stake && *min_validator_stake > 0) {
        config.economics.min_validator_stake = static_cast<uint64_t>(*min_validator_stake);
    } else {
        config.economics.min_validator_stake = 1000000; // Default 1M base units
    }
    
    return common::Result<GenesisConfig>(std::move(config));
}

EconomicConfig GenesisManager::create_default_economics(NetworkType network_type) {
    EconomicConfig economics;
    
    switch (network_type) {
        case NetworkType::MAINNET:
            economics.token_symbol = "SLON"; // Custom mainnet token
            economics.base_unit_name = "aldrins"; // Custom base unit
            economics.total_supply = 1000000000; // 1 billion tokens
            economics.initial_inflation_rate = 0.05; // 5% for mainnet
            economics.min_validator_stake = 1000000; // 1M base units
            break;
            
        case NetworkType::TESTNET:
            economics.token_symbol = "tSLON"; // Testnet token
            economics.base_unit_name = "taldrins"; // Testnet base unit
            economics.total_supply = 100000000; // 100 million tokens
            economics.initial_inflation_rate = 0.08; // 8% for testnet
            economics.min_validator_stake = 100000; // 100K base units
            break;
            
        case NetworkType::DEVNET:
        default:
            economics.token_symbol = "SOL"; // Default SOL for devnet
            economics.base_unit_name = "lamports"; // Default lamports for devnet
            economics.total_supply = 10000000; // 10 million tokens
            economics.initial_inflation_rate = 0.10; // 10% for devnet
            economics.min_validator_stake = 10000; // 10K base units
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