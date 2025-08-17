#include "slonana_validator.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <atomic>

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
    std::cout << "  --no-rpc                   Disable RPC server" << std::endl;
    std::cout << "  --no-gossip                Disable gossip protocol" << std::endl;
    std::cout << "  --help                     Show this help message" << std::endl;
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
        std::cout << "Press Ctrl+C to stop..." << std::endl;
        
        // Main event loop
        auto last_stats_time = std::chrono::steady_clock::now();
        
        while (!g_shutdown_requested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Print stats every 30 seconds
            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_time);
            
            if (duration.count() >= 30) {
                print_validator_stats(validator);
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