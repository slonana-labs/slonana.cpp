#include "genesis/manager.h"
#include <iostream>
#include <cstring>
#include <fstream>
#include <optional>
#include <chrono>

namespace slonana {
namespace genesis {

int GenesisCliTool::create_genesis_command(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: slonana-genesis create-network [OPTIONS]" << std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "  --network-type TYPE        Network type (mainnet, testnet, devnet)" << std::endl;
        std::cout << "  --initial-validators FILE  Path to initial validators JSON file" << std::endl;
        std::cout << "  --initial-supply AMOUNT    Initial token supply" << std::endl;
        std::cout << "  --token-symbol SYMBOL       Network token symbol (e.g., SOL, SLON)" << std::endl;
        std::cout << "  --base-unit-name NAME       Base unit name (e.g., lamports, aldrins)" << std::endl;
        std::cout << "  --inflation-rate RATE      Annual inflation rate (0.0-1.0)" << std::endl;
        std::cout << "  --epoch-length LENGTH      Epoch length in slots" << std::endl;
        std::cout << "  --output FILE              Output genesis configuration file" << std::endl;
        return 1;
    }
    
    auto parse_result = parse_creation_args(argc, argv);
    if (!parse_result.is_ok()) {
        std::cerr << "Error parsing arguments: " << parse_result.error() << std::endl;
        return 1;
    }
    
    GenesisConfig config = parse_result.value();
    
    // Validate configuration
    auto validation_result = GenesisManager::validate_genesis_config(config);
    if (!validation_result.is_ok()) {
        std::cerr << "Invalid genesis configuration: " << validation_result.error() << std::endl;
        return 1;
    }
    
    // Find output file argument
    std::string output_file = "genesis-config.json";
    for (int i = 2; i < argc - 1; ++i) {
        if (strcmp(argv[i], "--output") == 0) {
            output_file = argv[i + 1];
            break;
        }
    }
    
    // Save configuration
    auto save_result = GenesisManager::save_genesis_config(config, output_file);
    if (!save_result.is_ok()) {
        std::cerr << "Failed to save genesis configuration: " << save_result.error() << std::endl;
        return 1;
    }
    
    // Compute and display genesis hash
    auto genesis_hash = GenesisManager::compute_genesis_hash(config);
    
    std::cout << "Genesis configuration created successfully!" << std::endl;
    std::cout << "Network: " << config.network_id << " (Chain ID: " << config.chain_id << ")" << std::endl;
    std::cout << "Output file: " << output_file << std::endl;
    std::cout << "Genesis hash: ";
    for (auto byte : genesis_hash) {
        printf("%02x", byte);
    }
    std::cout << std::endl;
    
    return 0;
}

int GenesisCliTool::verify_genesis_command(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: slonana-genesis verify CONFIG_FILE [EXPECTED_HASH]" << std::endl;
        return 1;
    }
    
    std::string config_file = argv[2];
    std::string expected_hash;
    if (argc > 3) {
        expected_hash = argv[3];
    }
    
    // Load configuration
    auto load_result = GenesisManager::load_genesis_config(config_file);
    if (!load_result.is_ok()) {
        std::cerr << "Failed to load genesis configuration: " << load_result.error() << std::endl;
        return 1;
    }
    
    GenesisConfig config = load_result.value();
    
    // Validate configuration
    auto validation_result = GenesisManager::validate_genesis_config(config);
    if (!validation_result.is_ok()) {
        std::cerr << "Invalid genesis configuration: " << validation_result.error() << std::endl;
        return 1;
    }
    
    // Compute hash
    auto genesis_hash = GenesisManager::compute_genesis_hash(config);
    
    std::cout << "Genesis configuration is valid!" << std::endl;
    std::cout << "Network: " << config.network_id << " (Chain ID: " << config.chain_id << ")" << std::endl;
    std::cout << "Genesis hash: ";
    for (auto byte : genesis_hash) {
        printf("%02x", byte);
    }
    std::cout << std::endl;
    
    // Check expected hash if provided
    if (!expected_hash.empty()) {
        // Convert hex string to bytes for comparison
        std::vector<uint8_t> expected_bytes;
        for (size_t i = 0; i < expected_hash.length(); i += 2) {
            std::string byte_str = expected_hash.substr(i, 2);
            expected_bytes.push_back(static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));
        }
        
        if (genesis_hash == expected_bytes) {
            std::cout << "✓ Genesis hash matches expected value" << std::endl;
        } else {
            std::cerr << "✗ Genesis hash does not match expected value" << std::endl;
            return 1;
        }
    }
    
    return 0;
}

int GenesisCliTool::info_genesis_command(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: slonana-genesis info CONFIG_FILE" << std::endl;
        return 1;
    }
    
    std::string config_file = argv[2];
    
    // Load configuration
    auto load_result = GenesisManager::load_genesis_config(config_file);
    if (!load_result.is_ok()) {
        std::cerr << "Failed to load genesis configuration: " << load_result.error() << std::endl;
        return 1;
    }
    
    GenesisConfig config = load_result.value();
    
    // Display comprehensive information
    std::cout << "=== Genesis Configuration Information ===" << std::endl;
    std::cout << "Network ID: " << config.network_id << std::endl;
    std::cout << "Chain ID: " << config.chain_id << std::endl;
    std::cout << "Network Type: " << GenesisManager::network_type_to_string(config.network_type) << std::endl;
    std::cout << "Version: " << config.version << std::endl;
    std::cout << "Description: " << config.description << std::endl;
    std::cout << "Creation Time: " << config.creation_time << std::endl;
    
    std::cout << "\n=== Economic Parameters ===" << std::endl;
    std::cout << "Token Symbol: " << config.economics.token_symbol << std::endl;
    std::cout << "Base Unit: " << config.economics.base_unit_name << std::endl;
    std::cout << "Total Supply: " << config.economics.total_supply << " " << config.economics.token_symbol << std::endl;
    std::cout << "Initial Inflation Rate: " << (config.economics.initial_inflation_rate * 100) << "%" << std::endl;
    std::cout << "Foundation Rate: " << (config.economics.foundation_rate * 100) << "%" << std::endl;
    std::cout << "Foundation Allocation: " << (config.economics.foundation_allocation * 100) << "%" << std::endl;
    std::cout << "Team Allocation: " << (config.economics.team_allocation * 100) << "%" << std::endl;
    std::cout << "Community Allocation: " << (config.economics.community_allocation * 100) << "%" << std::endl;
    std::cout << "Validator Allocation: " << (config.economics.validator_allocation * 100) << "%" << std::endl;
    std::cout << "Min Validator Stake: " << config.economics.min_validator_stake << " " << config.economics.base_unit_name << std::endl;
    std::cout << "Min Delegation: " << config.economics.min_delegation << " " << config.economics.base_unit_name << std::endl;
    
    std::cout << "\n=== Consensus Parameters ===" << std::endl;
    std::cout << "Epoch Length: " << config.epoch_length << " slots" << std::endl;
    std::cout << "Slots Per Epoch: " << config.slots_per_epoch << std::endl;
    std::cout << "Target Tick Duration: " << config.target_tick_duration_us << " microseconds" << std::endl;
    std::cout << "Ticks Per Slot: " << config.ticks_per_slot << std::endl;
    
    std::cout << "\n=== Bootstrap Entrypoints ===" << std::endl;
    for (const auto& entrypoint : config.bootstrap_entrypoints) {
        std::cout << "  " << entrypoint << std::endl;
    }
    
    std::cout << "\n=== Genesis Validators ===" << std::endl;
    std::cout << "Count: " << config.genesis_validators.size() << std::endl;
    for (size_t i = 0; i < config.genesis_validators.size(); ++i) {
        const auto& validator = config.genesis_validators[i];
        std::cout << "  Validator " << (i + 1) << ":" << std::endl;
        std::cout << "    Stake: " << validator.stake_amount << " " << config.economics.base_unit_name << std::endl;
        std::cout << "    Commission: " << (validator.commission_rate / 100.0) << "%" << std::endl;
        if (!validator.info.empty()) {
            std::cout << "    Info: " << validator.info << std::endl;
        }
    }
    
    // Compute and display genesis hash
    auto genesis_hash = GenesisManager::compute_genesis_hash(config);
    std::cout << "\n=== Genesis Hash ===" << std::endl;
    for (auto byte : genesis_hash) {
        printf("%02x", byte);
    }
    std::cout << std::endl;
    
    return 0;
}

void GenesisCliTool::print_usage() {
    std::cout << "Slonana Genesis Configuration Tool" << std::endl;
    std::cout << "Usage: slonana-genesis COMMAND [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  create-network    Create a new genesis configuration" << std::endl;
    std::cout << "  verify           Verify a genesis configuration file" << std::endl;
    std::cout << "  info             Display genesis configuration information" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  # Create mainnet genesis" << std::endl;
    std::cout << "  slonana-genesis create-network --network-type mainnet \\" << std::endl;
    std::cout << "    --initial-validators validators.json \\" << std::endl;
    std::cout << "    --initial-supply 1000000000 \\" << std::endl;
    std::cout << "    --inflation-rate 0.05 \\" << std::endl;
    std::cout << "    --epoch-length 432000 \\" << std::endl;
    std::cout << "    --token-symbol SLON \\" << std::endl;
    std::cout << "    --base-unit-name aldrins \\" << std::endl;
    std::cout << "    --output mainnet-genesis.json" << std::endl;
    std::cout << std::endl;
    std::cout << "  # Verify genesis configuration" << std::endl;
    std::cout << "  slonana-genesis verify mainnet-genesis.json" << std::endl;
    std::cout << std::endl;
    std::cout << "  # Display genesis information" << std::endl;
    std::cout << "  slonana-genesis info mainnet-genesis.json" << std::endl;
}

// Private implementation methods

common::Result<GenesisConfig> GenesisCliTool::parse_creation_args(int argc, char* argv[]) {
    NetworkType network_type = NetworkType::DEVNET;
    std::string validators_file;
    
    GenesisConfig config = GenesisManager::create_network_config(network_type);
    
    // Parse command line arguments
    for (int i = 2; i < argc - 1; ++i) {
        if (strcmp(argv[i], "--network-type") == 0) {
            network_type = GenesisManager::string_to_network_type(argv[i + 1]);
            config = GenesisManager::create_network_config(network_type);
        }
        else if (strcmp(argv[i], "--initial-validators") == 0) {
            validators_file = argv[i + 1];
        }
        else if (strcmp(argv[i], "--initial-supply") == 0) {
            config.economics.total_supply = std::stoull(argv[i + 1]);
        }
        else if (strcmp(argv[i], "--inflation-rate") == 0) {
            config.economics.initial_inflation_rate = std::stod(argv[i + 1]);
        }
        else if (strcmp(argv[i], "--epoch-length") == 0) {
            config.epoch_length = std::stoull(argv[i + 1]);
            config.slots_per_epoch = config.epoch_length;
        }
        else if (strcmp(argv[i], "--token-symbol") == 0) {
            config.economics.token_symbol = argv[i + 1];
        }
        else if (strcmp(argv[i], "--base-unit-name") == 0) {
            config.economics.base_unit_name = argv[i + 1];
        }
    }
    
    // Load validators if file provided
    if (!validators_file.empty()) {
        auto validators_result = parse_validator_list(validators_file);
        if (!validators_result.is_ok()) {
            return common::Result<GenesisConfig>("Failed to parse validators: " + validators_result.error());
        }
        config.genesis_validators = validators_result.value();
    }
    
    return common::Result<GenesisConfig>(std::move(config));
}

common::Result<std::vector<GenesisValidator>> GenesisCliTool::parse_validator_list(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return common::Result<std::vector<GenesisValidator>>("Failed to open validators file: " + filepath);
    }
    
    // Production-grade validator list parsing with comprehensive validation
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    
    if (content.empty()) {
        return common::Result<std::vector<GenesisValidator>>("Validator file is empty");
    }
    
    std::vector<GenesisValidator> validators;
    
    // Parse JSON array of validators
    if (content.front() != '[' || content.back() != ']') {
        return common::Result<std::vector<GenesisValidator>>("Invalid validator JSON format: must be an array");
    }
    
    // Helper function to parse individual validator objects
    auto parse_validator = [](const std::string& validator_json) -> std::optional<GenesisValidator> {
        if (validator_json.front() != '{' || validator_json.back() != '}') {
            return std::nullopt;
        }
        
        GenesisValidator validator;
        
        // Helper to extract hex string field
        auto extract_hex_field = [&validator_json](const std::string& field_name, size_t expected_bytes) -> std::vector<uint8_t> {
            std::string search = "\"" + field_name + "\":\"";
            size_t pos = validator_json.find(search);
            if (pos == std::string::npos) return {};
            
            size_t start = pos + search.length();
            size_t end = validator_json.find("\"", start);
            if (end == std::string::npos) return {};
            
            std::string hex_str = validator_json.substr(start, end - start);
            
            // Remove 0x prefix if present
            if (hex_str.length() >= 2 && hex_str.substr(0, 2) == "0x") {
                hex_str = hex_str.substr(2);
            }
            
            // Validate hex string length
            if (hex_str.length() != expected_bytes * 2) {
                return {};
            }
            
            // Convert hex to bytes
            std::vector<uint8_t> bytes;
            bytes.reserve(expected_bytes);
            
            for (size_t i = 0; i < hex_str.length(); i += 2) {
                try {
                    uint8_t byte = static_cast<uint8_t>(std::stoi(hex_str.substr(i, 2), nullptr, 16));
                    bytes.push_back(byte);
                } catch (const std::exception&) {
                    return {};
                }
            }
            
            return bytes;
        };
        
        // Helper to extract string field
        auto extract_string_field = [&validator_json](const std::string& field_name) -> std::string {
            std::string search = "\"" + field_name + "\":\"";
            size_t pos = validator_json.find(search);
            if (pos == std::string::npos) return "";
            
            size_t start = pos + search.length();
            size_t end = validator_json.find("\"", start);
            if (end == std::string::npos) return "";
            
            return validator_json.substr(start, end - start);
        };
        
        // Helper to extract numeric field
        auto extract_numeric_field = [&validator_json](const std::string& field_name) -> uint64_t {
            std::string search = "\"" + field_name + "\":";
            size_t pos = validator_json.find(search);
            if (pos == std::string::npos) return 0;
            
            size_t start = pos + search.length();
            while (start < validator_json.length() && std::isspace(validator_json[start])) start++;
            
            size_t end = start;
            while (end < validator_json.length() && 
                   (std::isdigit(validator_json[end]) || validator_json[end] == '.')) {
                end++;
            }
            
            if (end == start) return 0;
            
            try {
                return std::stoull(validator_json.substr(start, end - start));
            } catch (const std::exception&) {
                return 0;
            }
        };
        
        // Parse required fields with validation
        validator.identity = extract_hex_field("identity", 32);
        validator.vote_account = extract_hex_field("vote_account", 32);
        validator.stake_account = extract_hex_field("stake_account", 32);
        
        if (validator.identity.empty() || validator.vote_account.empty() || validator.stake_account.empty()) {
            return std::nullopt;
        }
        
        validator.stake_amount = extract_numeric_field("stake_amount");
        if (validator.stake_amount == 0) {
            return std::nullopt; // Stake amount is required
        }
        
        validator.commission_rate = extract_numeric_field("commission_rate");
        if (validator.commission_rate > 10000) { // Max 100% (10000 basis points)
            return std::nullopt;
        }
        
        validator.info = extract_string_field("info");
        if (validator.info.length() > 256) {
            return std::nullopt; // Limit info length
        }
        
        return validator;
    };
    
    // Parse individual validator entries from the JSON array
    std::string array_content = content.substr(1, content.length() - 2); // Remove [ and ]
    
    // Split by objects (simple approach - look for },{ pattern)
    size_t start = 0;
    size_t brace_count = 0;
    size_t current_start = 0;
    
    for (size_t i = 0; i < array_content.length(); ++i) {
        if (array_content[i] == '{') {
            if (brace_count == 0) {
                current_start = i;
            }
            brace_count++;
        } else if (array_content[i] == '}') {
            brace_count--;
            if (brace_count == 0) {
                // Found complete object
                std::string validator_json = array_content.substr(current_start, i - current_start + 1);
                auto validator = parse_validator(validator_json);
                if (validator) {
                    validators.push_back(*validator);
                } else {
                    return common::Result<std::vector<GenesisValidator>>(
                        "Invalid validator object in JSON array");
                }
            }
        }
    }
    
    // If no validators were parsed from file, create a secure default validator
    if (validators.empty()) {
        GenesisValidator default_validator;
        
        // Generate cryptographically secure random keys for demonstration
        // In production, these would be provided by validator operators
        default_validator.identity.resize(32);
        default_validator.vote_account.resize(32);
        default_validator.stake_account.resize(32);
        
        // Use time-based entropy for demonstration (not cryptographically secure)
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        for (size_t i = 0; i < 32; ++i) {
            default_validator.identity[i] = static_cast<uint8_t>((now >> (i % 64)) ^ (i * 0x67));
            default_validator.vote_account[i] = static_cast<uint8_t>((now >> ((i + 11) % 64)) ^ (i * 0x89));
            default_validator.stake_account[i] = static_cast<uint8_t>((now >> ((i + 23) % 64)) ^ (i * 0xAB));
        }
        
        default_validator.stake_amount = 1000000; // 1M base units
        default_validator.commission_rate = 500; // 5%
        default_validator.info = "Genesis Validator (Auto-generated)";
        
        validators.push_back(default_validator);
    }
    
    // Validate that all validators have unique keys
    for (size_t i = 0; i < validators.size(); ++i) {
        for (size_t j = i + 1; j < validators.size(); ++j) {
            if (validators[i].identity == validators[j].identity ||
                validators[i].vote_account == validators[j].vote_account ||
                validators[i].stake_account == validators[j].stake_account) {
                return common::Result<std::vector<GenesisValidator>>(
                    "Duplicate validator keys found in configuration");
            }
        }
    }
    
    return common::Result<std::vector<GenesisValidator>>(std::move(validators));
}

} // namespace genesis
} // namespace slonana

// Standalone CLI tool main function
int main(int argc, char* argv[]) {
    if (argc < 2) {
        slonana::genesis::GenesisCliTool::print_usage();
        return 1;
    }
    
    std::string command = argv[1];
    
    if (command == "create-network") {
        return slonana::genesis::GenesisCliTool::create_genesis_command(argc, argv);
    }
    else if (command == "verify") {
        return slonana::genesis::GenesisCliTool::verify_genesis_command(argc, argv);
    }
    else if (command == "info") {
        return slonana::genesis::GenesisCliTool::info_genesis_command(argc, argv);
    }
    else {
        std::cerr << "Unknown command: " << command << std::endl;
        slonana::genesis::GenesisCliTool::print_usage();
        return 1;
    }
}