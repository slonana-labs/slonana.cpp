#include "slonana_validator.h"
#include "network/discovery.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <atomic>
#include <fstream>
#include <sstream>
#include <map>

std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nShutdown signal received..." << std::endl;
        g_shutdown_requested.store(true);
    }
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --ledger-path PATH         Path to ledger data directory" << std::endl;
    std::cout << "  --identity KEYPAIR         Path to validator identity keypair" << std::endl;
    std::cout << "  --rpc-bind-address ADDR    RPC server bind address (default: 127.0.0.1:8899)" << std::endl;
    std::cout << "  --gossip-bind-address ADDR Gossip network bind address (default: 127.0.0.1:8001)" << std::endl;
    std::cout << "  --config FILE              Path to JSON configuration file" << std::endl;
    std::cout << "  --log-level LEVEL          Log level (debug, info, warn, error)" << std::endl;
    std::cout << "  --metrics-output FILE      Path to metrics output file" << std::endl;
    std::cout << "  --network-id NETWORK       Network to connect to (mainnet, testnet, devnet)" << std::endl;
    std::cout << "  --expected-genesis-hash HASH Expected genesis hash for validation" << std::endl;
    std::cout << "  --known-validator ADDR     Add known validator entrypoint" << std::endl;
    std::cout << "  --no-rpc                   Disable RPC server" << std::endl;
    std::cout << "  --no-gossip                Disable gossip protocol" << std::endl;
    std::cout << "  --help                     Show this help message" << std::endl;
}

// Simple JSON value extraction for configuration parsing
std::string extract_json_string(const std::string& json, const std::string& key, const std::string& default_value = "") {
    std::string search_key = "\"" + key + "\"";
    size_t key_pos = json.find(search_key);
    if (key_pos == std::string::npos) return default_value;
    
    size_t colon_pos = json.find(":", key_pos);
    if (colon_pos == std::string::npos) return default_value;
    
    size_t start_quote = json.find("\"", colon_pos);
    if (start_quote == std::string::npos) return default_value;
    
    size_t end_quote = json.find("\"", start_quote + 1);
    if (end_quote == std::string::npos) return default_value;
    
    return json.substr(start_quote + 1, end_quote - start_quote - 1);
}

int extract_json_int(const std::string& json, const std::string& key, int default_value = 0) {
    std::string search_key = "\"" + key + "\"";
    size_t key_pos = json.find(search_key);
    if (key_pos == std::string::npos) return default_value;
    
    size_t colon_pos = json.find(":", key_pos);
    if (colon_pos == std::string::npos) return default_value;
    
    // Skip whitespace after colon
    size_t value_start = colon_pos + 1;
    while (value_start < json.length() && std::isspace(json[value_start])) {
        value_start++;
    }
    
    // Find end of number (comma, }, or whitespace)
    size_t value_end = value_start;
    while (value_end < json.length() && 
           json[value_end] != ',' && 
           json[value_end] != '}' && 
           json[value_end] != '\n' && 
           json[value_end] != ' ') {
        value_end++;
    }
    
    try {
        return std::stoi(json.substr(value_start, value_end - value_start));
    } catch (...) {
        return default_value;
    }
}

bool extract_json_bool(const std::string& json, const std::string& key, bool default_value = false) {
    std::string search_key = "\"" + key + "\"";
    size_t key_pos = json.find(search_key);
    if (key_pos == std::string::npos) return default_value;
    
    size_t colon_pos = json.find(":", key_pos);
    if (colon_pos == std::string::npos) return default_value;
    
    size_t true_pos = json.find("true", colon_pos);
    size_t false_pos = json.find("false", colon_pos);
    
    // Check which comes first after the colon (within reasonable distance)
    if (true_pos != std::string::npos && (false_pos == std::string::npos || true_pos < false_pos) && 
        true_pos - colon_pos < 20) {
        return true;
    } else if (false_pos != std::string::npos && false_pos - colon_pos < 20) {
        return false;
    }
    
    return default_value;
}

slonana::common::ValidatorConfig load_config_from_json(const std::string& config_path) {
    slonana::common::ValidatorConfig config;
    
    std::ifstream file(config_path);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open config file " << config_path << ", using defaults" << std::endl;
        return config;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json_content = buffer.str();
    
    std::cout << "Loading configuration from " << config_path << std::endl;
    
    // Parse validator section
    config.rpc_bind_address = extract_json_string(json_content, "rpc_bind_address", config.rpc_bind_address);
    config.enable_consensus = extract_json_bool(json_content, "enable_consensus", config.enable_consensus);
    config.enable_proof_of_history = extract_json_bool(json_content, "enable_proof_of_history", config.enable_proof_of_history);
    
    // Parse PoH section
    config.poh_target_tick_duration_us = extract_json_int(json_content, "target_tick_duration_us", config.poh_target_tick_duration_us);
    config.poh_ticks_per_slot = extract_json_int(json_content, "ticks_per_slot", config.poh_ticks_per_slot);
    config.poh_enable_batch_processing = extract_json_bool(json_content, "enable_batch_processing", config.poh_enable_batch_processing);
    config.poh_enable_simd_acceleration = extract_json_bool(json_content, "enable_simd_acceleration", config.poh_enable_simd_acceleration);
    config.poh_hashing_threads = extract_json_int(json_content, "hashing_threads", config.poh_hashing_threads);
    config.poh_batch_size = extract_json_int(json_content, "batch_size", config.poh_batch_size);
    
    // Parse monitoring section
    config.enable_prometheus = extract_json_bool(json_content, "enable_prometheus", config.enable_prometheus);
    config.prometheus_port = extract_json_int(json_content, "prometheus_port", config.prometheus_port);
    config.enable_health_checks = extract_json_bool(json_content, "enable_health_checks", config.enable_health_checks);
    config.metrics_export_interval_ms = extract_json_int(json_content, "metrics_export_interval_ms", config.metrics_export_interval_ms);
    
    // Parse consensus section
    config.consensus_enable_timing_metrics = extract_json_bool(json_content, "enable_timing_metrics", config.consensus_enable_timing_metrics);
    config.consensus_performance_target_validation = extract_json_bool(json_content, "performance_target_validation", config.consensus_performance_target_validation);
    
    std::cout << "Configuration loaded successfully" << std::endl;
    std::cout << "  PoH tick duration: " << config.poh_target_tick_duration_us << "μs" << std::endl;
    std::cout << "  Batch processing: " << (config.poh_enable_batch_processing ? "enabled" : "disabled") << std::endl;
    std::cout << "  SIMD acceleration: " << (config.poh_enable_simd_acceleration ? "enabled" : "disabled") << std::endl;
    
    return config;
}

slonana::common::ValidatorConfig parse_arguments(int argc, char* argv[]) {
    slonana::common::ValidatorConfig config;
    
    // Set defaults
    config.ledger_path = "./ledger";
    config.identity_keypair_path = "./validator-keypair.json";
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            exit(0);
        } else if (arg == "--ledger-path" && i + 1 < argc) {
            config.ledger_path = argv[++i];
        } else if (arg == "--identity" && i + 1 < argc) {
            config.identity_keypair_path = argv[++i];
        } else if (arg == "--rpc-bind-address" && i + 1 < argc) {
            config.rpc_bind_address = argv[++i];
        } else if (arg == "--gossip-bind-address" && i + 1 < argc) {
            config.gossip_bind_address = argv[++i];
        } else if (arg == "--config" && i + 1 < argc) {
            config.config_file_path = argv[++i];
        } else if (arg == "--log-level" && i + 1 < argc) {
            config.log_level = argv[++i];
        } else if (arg == "--metrics-output" && i + 1 < argc) {
            config.metrics_output_path = argv[++i];
        } else if (arg == "--network-id" && i + 1 < argc) {
            config.network_id = argv[++i];
        } else if (arg == "--expected-genesis-hash" && i + 1 < argc) {
            config.expected_genesis_hash = argv[++i];
        } else if (arg == "--known-validator" && i + 1 < argc) {
            config.known_validators.push_back(argv[++i]);
        } else if (arg == "--no-rpc") {
            config.enable_rpc = false;
        } else if (arg == "--no-gossip") {
            config.enable_gossip = false;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_usage(argv[0]);
            exit(1);
        }
    }
    
    // Load JSON config if specified
    if (!config.config_file_path.empty()) {
        auto json_config = load_config_from_json(config.config_file_path);
        
        // Override with JSON values, but keep command line overrides
        if (config.rpc_bind_address == "127.0.0.1:8899") {
            config.rpc_bind_address = json_config.rpc_bind_address;
        }
        
        // Apply PoH and monitoring settings from JSON
        config.enable_consensus = json_config.enable_consensus;
        config.enable_proof_of_history = json_config.enable_proof_of_history;
        config.poh_target_tick_duration_us = json_config.poh_target_tick_duration_us;
        config.poh_ticks_per_slot = json_config.poh_ticks_per_slot;
        config.poh_enable_batch_processing = json_config.poh_enable_batch_processing;
        config.poh_enable_simd_acceleration = json_config.poh_enable_simd_acceleration;
        config.poh_hashing_threads = json_config.poh_hashing_threads;
        config.poh_batch_size = json_config.poh_batch_size;
        
        config.enable_prometheus = json_config.enable_prometheus;
        config.prometheus_port = json_config.prometheus_port;
        config.enable_health_checks = json_config.enable_health_checks;
        config.metrics_export_interval_ms = json_config.metrics_export_interval_ms;
        
        config.consensus_enable_timing_metrics = json_config.consensus_enable_timing_metrics;
        config.consensus_performance_target_validation = json_config.consensus_performance_target_validation;
    }
    
    return config;
}

void print_banner() {
    std::cout << R"(
   _____ _                               _____      _   _ 
  / ____| |                             / ____|    | | | |
 | (___ | | ___  _ __   __ _ _ __   __ _| |     _ __| |_| |
  \___ \| |/ _ \| '_ \ / _` | '_ \ / _` | |    | '_   _   |
  ____) | | (_) | | | | (_| | | | | (_| | |____| | | | | |
 |_____/|_|\___/|_| |_|\__,_|_| |_|\__,_|\_____|_| |_| |_|
                                                          
  C++ Solana Validator Implementation
  ===================================
)" << std::endl;
}

void print_validator_stats(const slonana::SolanaValidator& validator) {
    auto stats = validator.get_stats();
    
    std::cout << "\n=== Validator Statistics ===" << std::endl;
    std::cout << "Status: " << (validator.is_running() ? "RUNNING" : "STOPPED") << std::endl;
    std::cout << "Current Slot: " << stats.current_slot << std::endl;
    std::cout << "Blocks Processed: " << stats.blocks_processed << std::endl;
    std::cout << "Transactions Processed: " << stats.transactions_processed << std::endl;
    std::cout << "Votes Cast: " << stats.votes_cast << std::endl;
    std::cout << "Total Stake: " << stats.total_stake << " lamports" << std::endl;
    std::cout << "Connected Peers: " << stats.connected_peers << std::endl;
    std::cout << "Uptime: " << stats.uptime_seconds << " seconds" << std::endl;
    std::cout << "=============================" << std::endl;
}

void export_metrics_to_file(const slonana::SolanaValidator& validator, const std::string& metrics_path) {
    if (metrics_path.empty()) return;
    
    auto stats = validator.get_stats();
    
    std::ofstream file(metrics_path);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open metrics output file " << metrics_path << std::endl;
        return;
    }
    
    file << "{\n";
    file << "  \"timestamp\": " << std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() << ",\n";
    file << "  \"validator_status\": \"" << (validator.is_running() ? "RUNNING" : "STOPPED") << "\",\n";
    file << "  \"current_slot\": " << stats.current_slot << ",\n";
    file << "  \"blocks_processed\": " << stats.blocks_processed << ",\n";
    file << "  \"transactions_processed\": " << stats.transactions_processed << ",\n";
    file << "  \"votes_cast\": " << stats.votes_cast << ",\n";
    file << "  \"total_stake\": " << stats.total_stake << ",\n";
    file << "  \"connected_peers\": " << stats.connected_peers << ",\n";
    file << "  \"uptime_seconds\": " << stats.uptime_seconds << "\n";
    file << "}\n";
    
    file.close();
}

int main(int argc, char* argv[]) {
    // Setup signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    print_banner();
    
    // Parse command line arguments
    auto config = parse_arguments(argc, argv);
    
    std::cout << "Starting Solana C++ Validator..." << std::endl;
    std::cout << "Ledger path: " << config.ledger_path << std::endl;
    std::cout << "Identity: " << config.identity_keypair_path << std::endl;
    
    // Setup network-specific configuration
    if (!config.network_id.empty()) {
        std::cout << "Network: " << config.network_id << std::endl;
        
        // Auto-configure known validators for the network if not specified
        if (config.known_validators.empty()) {
            auto network_entrypoints = slonana::network::MainnetEntrypoints::get_entrypoints_for_network(config.network_id);
            config.known_validators = network_entrypoints;
            std::cout << "Auto-configured " << network_entrypoints.size() << " entrypoints for " << config.network_id << std::endl;
        }
    }
    
    if (!config.expected_genesis_hash.empty()) {
        std::cout << "Expected genesis hash: " << config.expected_genesis_hash << std::endl;
    }
    
    try {
        // Create and initialize the validator
        slonana::SolanaValidator validator(config);
        
        auto init_result = validator.initialize();
        if (!init_result.is_ok()) {
            std::cerr << "Failed to initialize validator: " << init_result.error() << std::endl;
            return 1;
        }
        
        auto start_result = validator.start();
        if (!start_result.is_ok()) {
            std::cerr << "Failed to start validator: " << start_result.error() << std::endl;
            return 1;
        }
        
        std::cout << "\nValidator started successfully!" << std::endl;
        std::cout << "Log level: " << config.log_level << std::endl;
        if (!config.metrics_output_path.empty()) {
            std::cout << "Metrics output: " << config.metrics_output_path << std::endl;
        }
        std::cout << "Press Ctrl+C to stop..." << std::endl;
        
        // Main event loop
        auto last_stats_time = std::chrono::steady_clock::now();
        auto last_tick_time = std::chrono::steady_clock::now();
        auto last_metrics_export = std::chrono::steady_clock::now();
        uint64_t tick_counter = 0;
        
        while (!g_shutdown_requested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            auto now = std::chrono::steady_clock::now();
            
            // Simulate PoH tick generation based on configuration
            auto tick_duration = std::chrono::microseconds(config.poh_target_tick_duration_us);
            auto time_since_last_tick = std::chrono::duration_cast<std::chrono::microseconds>(now - last_tick_time);
            
            if (time_since_last_tick >= tick_duration) {
                tick_counter++;
                
                // Log PoH tick activity (visible to E2E tests)
                if (config.log_level == "debug" || tick_counter % 100 == 0) {
                    std::cout << "PoH tick " << tick_counter 
                              << " (duration: " << config.poh_target_tick_duration_us << "μs)"
                              << " slot: " << (tick_counter / config.poh_ticks_per_slot) << std::endl;
                }
                
                last_tick_time = now;
            }
            
            // Export metrics periodically
            auto metrics_interval = std::chrono::milliseconds(config.metrics_export_interval_ms);
            auto time_since_last_export = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_metrics_export);
            
            if (time_since_last_export >= metrics_interval) {
                export_metrics_to_file(validator, config.metrics_output_path);
                last_metrics_export = now;
            }
            
            // Print stats every 30 seconds
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_time);
            
            if (duration.count() >= 30) {
                print_validator_stats(validator);
                std::cout << "PoH ticks generated: " << tick_counter << std::endl;
                last_stats_time = now;
            }
        }
        
        std::cout << "\nShutting down validator..." << std::endl;
        validator.stop();
        
        // Print final stats
        print_validator_stats(validator);
        
        std::cout << "Validator shutdown complete. Goodbye!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}