#include "test_framework.h"
#include "network/rpc_server.h"
#include <memory>
#include <vector>
#include <string>

// Comprehensive RPC API test suite covering all 35+ methods

void test_rpc_comprehensive_account_methods() {
    slonana::common::ValidatorConfig config;
    config.rpc_bind_address = "127.0.0.1:18899";
    
    slonana::network::SolanaRpcServer rpc_server(config);
    rpc_server.start();
    
    std::vector<std::pair<std::string, std::string>> account_tests = {
        {"getAccountInfo", R"({"jsonrpc":"2.0","method":"getAccountInfo","params":["11111111111111111111111111111112"],"id":"1"})"},
        {"getBalance", R"({"jsonrpc":"2.0","method":"getBalance","params":["11111111111111111111111111111112"],"id":"2"})"},
        {"getProgramAccounts", R"({"jsonrpc":"2.0","method":"getProgramAccounts","params":["11111111111111111111111111111112"],"id":"3"})"},
        {"getMultipleAccounts", R"({"jsonrpc":"2.0","method":"getMultipleAccounts","params":[["11111111111111111111111111111112"]],"id":"4"})"},
        {"getLargestAccounts", R"({"jsonrpc":"2.0","method":"getLargestAccounts","params":"","id":"5"})"},
        {"getMinimumBalanceForRentExemption", R"({"jsonrpc":"2.0","method":"getMinimumBalanceForRentExemption","params":[0],"id":"6"})"}
    };
    
    for (size_t i = 0; i < account_tests.size(); ++i) {
        const auto& test = account_tests[i];
        std::string response = rpc_server.handle_request(test.second);
        ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
        ASSERT_CONTAINS(response, "\"id\":\"" + std::to_string(i + 1) + "\"");
        // Each method should return either a result or a proper error
        ASSERT_TRUE(response.find("\"result\":") != std::string::npos || 
                   response.find("\"error\":") != std::string::npos);
    }
    
    rpc_server.stop();
}

void test_rpc_comprehensive_block_methods() {
    slonana::common::ValidatorConfig config;
    config.rpc_bind_address = "127.0.0.1:18899";
    
    slonana::network::SolanaRpcServer rpc_server(config);
    rpc_server.start();
    
    std::vector<std::pair<std::string, std::string>> block_tests = {
        {"getSlot", R"({"jsonrpc":"2.0","method":"getSlot","params":"","id":"1"})"},
        {"getBlock", R"({"jsonrpc":"2.0","method":"getBlock","params":[0],"id":"2"})"},
        {"getBlockHeight", R"({"jsonrpc":"2.0","method":"getBlockHeight","params":"","id":"3"})"},
        {"getBlocks", R"({"jsonrpc":"2.0","method":"getBlocks","params":[0,10],"id":"4"})"},
        {"getFirstAvailableBlock", R"({"jsonrpc":"2.0","method":"getFirstAvailableBlock","params":"","id":"5"})"},
        {"getGenesisHash", R"({"jsonrpc":"2.0","method":"getGenesisHash","params":"","id":"6"})"},
        {"getSlotLeaders", R"({"jsonrpc":"2.0","method":"getSlotLeaders","params":[0,10],"id":"7"})"},
        {"getBlockProduction", R"({"jsonrpc":"2.0","method":"getBlockProduction","params":"","id":"8"})"}
    };
    
    for (const auto& test : block_tests) {
        std::string response = rpc_server.handle_request(test.second);
        ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
        ASSERT_TRUE(response.find("\"result\":") != std::string::npos || 
                   response.find("\"error\":") != std::string::npos);
    }
    
    rpc_server.stop();
}

void test_rpc_comprehensive_transaction_methods() {
    slonana::common::ValidatorConfig config;
    config.rpc_bind_address = "127.0.0.1:18899";
    
    slonana::network::SolanaRpcServer rpc_server(config);
    rpc_server.start();
    
    std::vector<std::pair<std::string, std::string>> transaction_tests = {
        {"getTransaction", R"({"jsonrpc":"2.0","method":"getTransaction","params":["signature123"],"id":"1"})"},
        {"sendTransaction", R"({"jsonrpc":"2.0","method":"sendTransaction","params":["base64data"],"id":"2"})"},
        {"simulateTransaction", R"({"jsonrpc":"2.0","method":"simulateTransaction","params":["base64data"],"id":"3"})"},
        {"getSignatureStatuses", R"({"jsonrpc":"2.0","method":"getSignatureStatuses","params":[["sig1","sig2"]],"id":"4"})"},
        {"getConfirmedSignaturesForAddress2", R"({"jsonrpc":"2.0","method":"getConfirmedSignaturesForAddress2","params":["11111111111111111111111111111112"],"id":"5"})"}
    };
    
    for (const auto& test : transaction_tests) {
        std::string response = rpc_server.handle_request(test.second);
        ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
        ASSERT_TRUE(response.find("\"result\":") != std::string::npos || 
                   response.find("\"error\":") != std::string::npos);
    }
    
    rpc_server.stop();
}

void test_rpc_comprehensive_network_methods() {
    slonana::common::ValidatorConfig config;
    config.rpc_bind_address = "127.0.0.1:18899";
    
    slonana::network::SolanaRpcServer rpc_server(config);
    rpc_server.start();
    
    std::vector<std::pair<std::string, std::string>> network_tests = {
        {"getVersion", R"({"jsonrpc":"2.0","method":"getVersion","params":"","id":"1"})"},
        {"getClusterNodes", R"({"jsonrpc":"2.0","method":"getClusterNodes","params":"","id":"2"})"},
        {"getIdentity", R"({"jsonrpc":"2.0","method":"getIdentity","params":"","id":"3"})"},
        {"getHealth", R"({"jsonrpc":"2.0","method":"getHealth","params":"","id":"4"})"}
    };
    
    for (const auto& test : network_tests) {
        std::string response = rpc_server.handle_request(test.second);
        ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
        ASSERT_TRUE(response.find("\"result\":") != std::string::npos || 
                   response.find("\"error\":") != std::string::npos);
    }
    
    rpc_server.stop();
}

void test_rpc_comprehensive_validator_methods() {
    slonana::common::ValidatorConfig config;
    config.rpc_bind_address = "127.0.0.1:18899";
    
    slonana::network::SolanaRpcServer rpc_server(config);
    rpc_server.start();
    
    std::vector<std::pair<std::string, std::string>> validator_tests = {
        {"getVoteAccounts", R"({"jsonrpc":"2.0","method":"getVoteAccounts","params":"","id":"1"})"},
        {"getLeaderSchedule", R"({"jsonrpc":"2.0","method":"getLeaderSchedule","params":"","id":"2"})"},
        {"getEpochInfo", R"({"jsonrpc":"2.0","method":"getEpochInfo","params":"","id":"3"})"},
        {"getEpochSchedule", R"({"jsonrpc":"2.0","method":"getEpochSchedule","params":"","id":"4"})"}
    };
    
    for (const auto& test : validator_tests) {
        std::string response = rpc_server.handle_request(test.second);
        ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
        ASSERT_TRUE(response.find("\"result\":") != std::string::npos || 
                   response.find("\"error\":") != std::string::npos);
    }
    
    rpc_server.stop();
}

void test_rpc_comprehensive_staking_methods() {
    slonana::common::ValidatorConfig config;
    config.rpc_bind_address = "127.0.0.1:18899";
    
    slonana::network::SolanaRpcServer rpc_server(config);
    rpc_server.start();
    
    std::vector<std::pair<std::string, std::string>> staking_tests = {
        {"getStakeActivation", R"({"jsonrpc":"2.0","method":"getStakeActivation","params":["11111111111111111111111111111112"],"id":"1"})"},
        {"getInflationGovernor", R"({"jsonrpc":"2.0","method":"getInflationGovernor","params":"","id":"2"})"},
        {"getInflationRate", R"({"jsonrpc":"2.0","method":"getInflationRate","params":"","id":"3"})"},
        {"getInflationReward", R"({"jsonrpc":"2.0","method":"getInflationReward","params":[["11111111111111111111111111111112"]],"id":"4"})"}
    };
    
    for (const auto& test : staking_tests) {
        std::string response = rpc_server.handle_request(test.second);
        ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
        ASSERT_TRUE(response.find("\"result\":") != std::string::npos || 
                   response.find("\"error\":") != std::string::npos);
    }
    
    rpc_server.stop();
}

void test_rpc_comprehensive_utility_methods() {
    slonana::common::ValidatorConfig config;
    config.rpc_bind_address = "127.0.0.1:18899";
    
    slonana::network::SolanaRpcServer rpc_server(config);
    rpc_server.start();
    
    std::vector<std::pair<std::string, std::string>> utility_tests = {
        {"getRecentBlockhash", R"({"jsonrpc":"2.0","method":"getRecentBlockhash","params":"","id":"1"})"},
        {"getLatestBlockhash", R"({"jsonrpc":"2.0","method":"getLatestBlockhash","params":"","id":"2"})"},
        {"getFeeForMessage", R"({"jsonrpc":"2.0","method":"getFeeForMessage","params":["base64message"],"id":"3"})"},
        {"isBlockhashValid", R"({"jsonrpc":"2.0","method":"isBlockhashValid","params":["blockhash123"],"id":"4"})"}
    };
    
    for (const auto& test : utility_tests) {
        std::string response = rpc_server.handle_request(test.second);
        ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
        ASSERT_TRUE(response.find("\"result\":") != std::string::npos || 
                   response.find("\"error\":") != std::string::npos);
    }
    
    rpc_server.stop();
}

void test_rpc_error_handling() {
    slonana::common::ValidatorConfig config;
    config.rpc_bind_address = "127.0.0.1:18899";
    
    slonana::network::SolanaRpcServer rpc_server(config);
    rpc_server.start();
    
    // Test various error conditions
    std::vector<std::pair<std::string, std::string>> error_tests = {
        {"Invalid JSON", "{invalid json}"},
        {"Missing method", R"({"jsonrpc":"2.0","params":"","id":"1"})"},
        {"Unknown method", R"({"jsonrpc":"2.0","method":"unknownMethod","params":"","id":"2"})"},
        {"Invalid params", R"({"jsonrpc":"2.0","method":"getBalance","params":123,"id":"3"})"},
        {"Missing id", R"({"jsonrpc":"2.0","method":"getHealth","params":""})"}
    };
    
    for (const auto& test : error_tests) {
        std::string response = rpc_server.handle_request(test.second);
        ASSERT_CONTAINS(response, "\"error\":");
        // Should contain proper JSON-RPC error codes
        ASSERT_TRUE(response.find("-32") != std::string::npos); // JSON-RPC error codes start with -32
    }
    
    rpc_server.stop();
}

void test_rpc_performance_batch() {
    slonana::common::ValidatorConfig config;
    config.rpc_bind_address = "127.0.0.1:18899";
    
    slonana::network::SolanaRpcServer rpc_server(config);
    rpc_server.start();
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Test performance with multiple rapid requests
    for (int i = 0; i < 100; ++i) {
        std::string request = R"({"jsonrpc":"2.0","method":"getHealth","params":"","id":")" + std::to_string(i) + R"("})";
        std::string response = rpc_server.handle_request(request);
        ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "Performance: Processed 100 RPC requests in " << duration.count() << "ms" << std::endl;
    
    rpc_server.stop();
}

void test_rpc_concurrent_requests() {
    slonana::common::ValidatorConfig config;
    config.rpc_bind_address = "127.0.0.1:18899";
    
    slonana::network::SolanaRpcServer rpc_server(config);
    rpc_server.start();
    
    // Test concurrent request handling (simulated by rapid sequential calls)
    std::vector<std::string> methods = {
        "getHealth", "getVersion", "getSlot", "getBlockHeight", "getEpochInfo"
    };
    
    for (int round = 0; round < 5; ++round) {
        for (const auto& method : methods) {
            std::string request = R"({"jsonrpc":"2.0","method":")" + method + R"(","params":"","id":")" + std::to_string(round) + R"("})";
            std::string response = rpc_server.handle_request(request);
            ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
        }
    }
    
    rpc_server.stop();
}

void test_rpc_method_coverage() {
    slonana::common::ValidatorConfig config;
    config.rpc_bind_address = "127.0.0.1:18899";
    
    slonana::network::SolanaRpcServer rpc_server(config);
    rpc_server.start();
    
    // Test that all major method categories are implemented
    std::vector<std::string> core_methods = {
        // Basic methods
        "getHealth", "getVersion", "getSlot", "getBlockHeight",
        // Account methods
        "getAccountInfo", "getBalance", "getProgramAccounts",
        // Block methods
        "getBlock", "getGenesisHash", "getFirstAvailableBlock",
        // Transaction methods
        "getTransaction", "sendTransaction", "simulateTransaction",
        // Network methods
        "getClusterNodes", "getIdentity",
        // Validator methods
        "getVoteAccounts", "getEpochInfo", "getLeaderSchedule",
        // Staking methods
        "getStakeActivation", "getInflationGovernor",
        // Utility methods
        "getRecentBlockhash", "getLatestBlockhash"
    };
    
    int successful_methods = 0;
    
    for (const auto& method : core_methods) {
        std::string request = R"({"jsonrpc":"2.0","method":")" + method + R"(","params":"","id":"test"})";
        std::string response = rpc_server.handle_request(request);
        
        // Method should either return a result or a proper error (not "method not found")
        if (response.find("\"result\":") != std::string::npos ||
            (response.find("\"error\":") != std::string::npos && response.find("-32601") == std::string::npos)) {
            successful_methods++;
        }
    }
    
    std::cout << "Method coverage: " << successful_methods << "/" << core_methods.size() << " methods implemented" << std::endl;
    
    // We expect most methods to be implemented (at least 80% coverage)
    ASSERT_GT(successful_methods, static_cast<int>(core_methods.size() * 0.8));
    
    rpc_server.stop();
}

void run_rpc_comprehensive_tests(TestRunner& runner) {
    std::cout << "\n=== Comprehensive RPC API Tests ===" << std::endl;
    
    runner.run_test("RPC Account Methods", test_rpc_comprehensive_account_methods);
    runner.run_test("RPC Block Methods", test_rpc_comprehensive_block_methods);
    runner.run_test("RPC Transaction Methods", test_rpc_comprehensive_transaction_methods);
    runner.run_test("RPC Network Methods", test_rpc_comprehensive_network_methods);
    runner.run_test("RPC Validator Methods", test_rpc_comprehensive_validator_methods);
    runner.run_test("RPC Staking Methods", test_rpc_comprehensive_staking_methods);
    runner.run_test("RPC Utility Methods", test_rpc_comprehensive_utility_methods);
    runner.run_test("RPC Error Handling", test_rpc_error_handling);
    runner.run_test("RPC Performance Batch", test_rpc_performance_batch);
    runner.run_test("RPC Concurrent Requests", test_rpc_concurrent_requests);
    runner.run_test("RPC Method Coverage", test_rpc_method_coverage);
}

// Standalone test main for RPC comprehensive tests
#ifndef COMPREHENSIVE_TESTS
int main() {
    std::cout << "=== RPC Comprehensive Test Suite ===" << std::endl;
    TestRunner runner;
    run_rpc_comprehensive_tests(runner);
    runner.print_summary();
    return runner.all_passed() ? 0 : 1;
}
#endif