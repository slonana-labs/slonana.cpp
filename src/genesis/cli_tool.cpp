#include "genesis/manager.h"
#include <iostream>
#include <cstring>
#include <fstream>

namespace slonana {
namespace genesis {

int GenesisCliTool::create_genesis_command(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: slonana-genesis create-network [OPTIONS]" << std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "  --network-type TYPE        Network type (mainnet, testnet, devnet)" << std::endl;
        std::cout << "  --initial-validators FILE  Path to initial validators JSON file" << std::endl;
        std::cout << "  --initial-supply AMOUNT    Initial token supply" << std::endl;
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
    std::cout << "Total Supply: " << config.economics.total_supply << " tokens" << std::endl;
    std::cout << "Initial Inflation Rate: " << (config.economics.initial_inflation_rate * 100) << "%" << std::endl;
    std::cout << "Foundation Rate: " << (config.economics.foundation_rate * 100) << "%" << std::endl;
    std::cout << "Foundation Allocation: " << (config.economics.foundation_allocation * 100) << "%" << std::endl;
    std::cout << "Team Allocation: " << (config.economics.team_allocation * 100) << "%" << std::endl;
    std::cout << "Community Allocation: " << (config.economics.community_allocation * 100) << "%" << std::endl;
    std::cout << "Validator Allocation: " << (config.economics.validator_allocation * 100) << "%" << std::endl;
    std::cout << "Min Validator Stake: " << config.economics.min_validator_stake << " lamports" << std::endl;
    std::cout << "Min Delegation: " << config.economics.min_delegation << " lamports" << std::endl;
    
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
        std::cout << "    Stake: " << validator.stake_amount << " lamports" << std::endl;
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
    
    // Simple validator list parsing (in production, use proper JSON library)
    std::vector<GenesisValidator> validators;
    
    // For now, create a simple default validator for demonstration
    GenesisValidator validator;
    validator.identity.resize(32, 0x01);
    validator.vote_account.resize(32, 0x02);
    validator.stake_account.resize(32, 0x03);
    validator.stake_amount = 1000000; // 1M lamports
    validator.commission_rate = 500; // 5%
    validator.info = "Genesis Validator";
    
    validators.push_back(validator);
    
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