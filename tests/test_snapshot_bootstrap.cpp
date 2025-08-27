#include "test_framework.h"
#include "validator/snapshot_bootstrap.h"
#include "common/types.h"
#include <filesystem>
#include <fstream>

using namespace slonana;
using namespace slonana::validator;
using namespace slonana::common;

namespace fs = std::filesystem;

namespace {

void test_snapshot_source_parsing() {
    std::cout << "Running test: Snapshot Source Parsing... ";
    
    ASSERT_TRUE(SnapshotBootstrapManager::parse_snapshot_source("auto") == SnapshotSource::AUTO);
    ASSERT_TRUE(SnapshotBootstrapManager::parse_snapshot_source("mirror") == SnapshotSource::MIRROR);
    ASSERT_TRUE(SnapshotBootstrapManager::parse_snapshot_source("none") == SnapshotSource::NONE);
    ASSERT_TRUE(SnapshotBootstrapManager::parse_snapshot_source("invalid") == SnapshotSource::AUTO);
    
    std::cout << "PASSED" << std::endl;
}

void test_snapshot_source_to_string() {
    std::cout << "Running test: Snapshot Source To String... ";
    
    ASSERT_TRUE(SnapshotBootstrapManager::snapshot_source_to_string(SnapshotSource::AUTO) == "auto");
    ASSERT_TRUE(SnapshotBootstrapManager::snapshot_source_to_string(SnapshotSource::MIRROR) == "mirror");
    ASSERT_TRUE(SnapshotBootstrapManager::snapshot_source_to_string(SnapshotSource::NONE) == "none");
    
    std::cout << "PASSED" << std::endl;
}

void test_bootstrap_manager_initialization() {
    std::cout << "Running test: Bootstrap Manager Initialization... ";
    
    // Create a temporary directory for tests
    fs::path test_dir = fs::temp_directory_path() / "slonana_bootstrap_test";
    fs::create_directories(test_dir);
    
    // Setup test config
    ValidatorConfig config;
    config.ledger_path = test_dir.string();
    config.network_id = "devnet";
    config.enable_rpc = true;
    config.snapshot_source = "auto";
    config.upstream_rpc_url = "https://api.devnet.solana.com";
    
    try {
        SnapshotBootstrapManager manager(config);
        ASSERT_TRUE(manager.get_snapshot_directory() == test_dir.string() + "/snapshots");
        
        // Clean up
        if (fs::exists(test_dir)) {
            fs::remove_all(test_dir);
        }
        
        std::cout << "PASSED" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "FAILED: " << e.what() << std::endl;
        ASSERT_TRUE(false);
    }
}

void test_needs_bootstrap() {
    std::cout << "Running test: Needs Bootstrap Detection... ";
    
    fs::path test_dir = fs::temp_directory_path() / "slonana_bootstrap_needs_test";
    fs::create_directories(test_dir);
    
    ValidatorConfig config;
    config.ledger_path = test_dir.string();
    config.network_id = "devnet";
    config.enable_rpc = true;
    config.snapshot_source = "auto";
    
    try {
        SnapshotBootstrapManager manager(config);
        
        // Should need bootstrap when no local ledger exists
        ASSERT_TRUE(manager.needs_bootstrap());
        
        // Test with empty ledger directory  
        fs::create_directories(test_dir / "snapshots");
        ASSERT_TRUE(manager.needs_bootstrap());
        
        // Clean up
        if (fs::exists(test_dir)) {
            fs::remove_all(test_dir);
        }
        
        std::cout << "PASSED" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "FAILED: " << e.what() << std::endl;
        ASSERT_TRUE(false);
    }
}

void test_bootstrap_with_none_source() {
    std::cout << "Running test: Bootstrap with None Source... ";
    
    fs::path test_dir = fs::temp_directory_path() / "slonana_bootstrap_none_test";
    fs::create_directories(test_dir);
    
    ValidatorConfig config;
    config.ledger_path = test_dir.string();
    config.network_id = "devnet";
    config.enable_rpc = true;
    config.snapshot_source = "none";
    
    try {
        SnapshotBootstrapManager manager(config);
        
        auto result = manager.bootstrap_from_snapshot();
        ASSERT_TRUE(result.is_ok());
        
        // Clean up
        if (fs::exists(test_dir)) {
            fs::remove_all(test_dir);
        }
        
        std::cout << "PASSED" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "FAILED: " << e.what() << std::endl;
        ASSERT_TRUE(false);
    }
}

void test_snapshot_info_structure() {
    std::cout << "Running test: Snapshot Info Structure... ";
    
    SnapshotInfo info;
    ASSERT_TRUE(info.slot == 0);
    ASSERT_TRUE(!info.valid);
    
    info.slot = 12345;
    info.hash = "test_hash";
    info.valid = true;
    
    ASSERT_TRUE(info.slot == 12345);
    ASSERT_TRUE(info.hash == "test_hash");
    ASSERT_TRUE(info.valid);
    
    std::cout << "PASSED" << std::endl;
}

void test_snapshot_directory_creation() {
    std::cout << "Running test: Snapshot Directory Creation... ";
    
    fs::path test_dir = fs::temp_directory_path() / "slonana_bootstrap_dir_test";
    
    // Remove the test directory to ensure it gets created
    if (fs::exists(test_dir)) {
        fs::remove_all(test_dir);
    }
    
    ValidatorConfig config;
    config.ledger_path = test_dir.string();
    config.network_id = "devnet";
    config.enable_rpc = true;
    config.snapshot_source = "auto";
    
    try {
        SnapshotBootstrapManager manager(config);
        
        // Snapshot directory should be created during initialization
        fs::path expected_snapshot_dir = test_dir / "snapshots";
        ASSERT_TRUE(fs::exists(expected_snapshot_dir));
        ASSERT_TRUE(fs::is_directory(expected_snapshot_dir));
        
        // Clean up
        if (fs::exists(test_dir)) {
            fs::remove_all(test_dir);
        }
        
        std::cout << "PASSED" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "FAILED: " << e.what() << std::endl;
        ASSERT_TRUE(false);
    }
}

void test_progress_callback() {
    std::cout << "Running test: Progress Callback... ";
    
    fs::path test_dir = fs::temp_directory_path() / "slonana_bootstrap_callback_test";
    fs::create_directories(test_dir);
    
    ValidatorConfig config;
    config.ledger_path = test_dir.string();
    config.network_id = "devnet";
    config.enable_rpc = true;
    config.snapshot_source = "none";  // Use none to avoid network calls
    
    try {
        SnapshotBootstrapManager manager(config);
        
        std::vector<std::string> progress_messages;
        manager.set_progress_callback([&progress_messages](const std::string& phase, uint64_t current, uint64_t total) {
            progress_messages.push_back(phase + " (" + std::to_string(current) + "/" + std::to_string(total) + ")");
        });
        
        auto result = manager.bootstrap_from_snapshot();
        
        ASSERT_TRUE(result.is_ok());
        ASSERT_TRUE(!progress_messages.empty());
        
        // Clean up
        if (fs::exists(test_dir)) {
            fs::remove_all(test_dir);
        }
        
        std::cout << "PASSED" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "FAILED: " << e.what() << std::endl;
        ASSERT_TRUE(false);
    }
}

} // anonymous namespace

void run_snapshot_bootstrap_tests() {
    std::cout << "\n=== Snapshot Bootstrap Tests ===" << std::endl;
    
    test_snapshot_source_parsing();
    test_snapshot_source_to_string();
    test_bootstrap_manager_initialization();
    test_needs_bootstrap();
    test_bootstrap_with_none_source();
    test_snapshot_info_structure();
    test_snapshot_directory_creation();
    test_progress_callback();
    
    std::cout << "=== Snapshot Bootstrap Tests Complete ===" << std::endl;
}