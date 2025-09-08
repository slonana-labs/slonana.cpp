# 🧪 Agave Compatibility Test Framework

**Date:** September 8, 2025  
**Related:** [AGAVE_COMPATIBILITY_AUDIT.md](./AGAVE_COMPATIBILITY_AUDIT.md) | [AGAVE_IMPLEMENTATION_PLAN.md](./AGAVE_IMPLEMENTATION_PLAN.md) | [AGAVE_TECHNICAL_SPECS.md](./AGAVE_TECHNICAL_SPECS.md)  
**Repository:** slonana-labs/slonana.cpp  

## 📖 Overview

This document defines the comprehensive test framework for validating Agave network compatibility. The framework ensures that slonana.cpp behaves identically to Agave while maintaining superior performance characteristics.

## 🎯 Testing Strategy

### **Test Categories**

1. **Unit Tests** - Individual component validation
2. **Integration Tests** - Cross-component functionality
3. **Compatibility Tests** - Agave protocol compliance
4. **Performance Tests** - Benchmark validation
5. **Security Tests** - Vulnerability assessment
6. **Stress Tests** - High-load scenarios
7. **Network Tests** - Live network interaction

## 🏗️ Test Infrastructure

### **1. Automated Test Environment**

```bash
#!/bin/bash
# scripts/setup-test-environment.sh

# Setup Agave testnet validator for comparison
setup_agave_validator() {
    echo "Setting up Agave validator for testing..."
    
    # Download latest Agave release
    AGAVE_VERSION=$(curl -s https://api.github.com/repos/anza-xyz/agave/releases/latest | jq -r '.tag_name')
    wget "https://github.com/anza-xyz/agave/releases/download/${AGAVE_VERSION}/solana-release-x86_64-unknown-linux-gnu.tar.bz2"
    tar -xf solana-release-x86_64-unknown-linux-gnu.tar.bz2
    
    # Initialize test ledger
    ./solana-release/bin/solana-test-validator \
        --ledger /tmp/agave-test-ledger \
        --bind-address 127.0.0.1 \
        --rpc-port 8899 \
        --gossip-port 8001 \
        --faucet-port 9900 \
        --enable-rpc-transaction-history \
        --enable-cpi-and-log-storage \
        --reset &
    
    AGAVE_PID=$!
    echo "Agave validator started with PID: $AGAVE_PID"
    
    # Wait for validator to be ready
    while ! curl -s http://127.0.0.1:8899/health > /dev/null; do
        echo "Waiting for Agave validator to be ready..."
        sleep 2
    done
    
    echo "Agave validator is ready"
}

# Setup slonana.cpp validator
setup_slonana_validator() {
    echo "Setting up slonana.cpp validator for testing..."
    
    # Build slonana.cpp
    cd /home/runner/work/slonana.cpp/slonana.cpp
    mkdir -p build && cd build
    cmake .. && make -j$(nproc)
    
    # Start slonana validator
    ./slonana_validator \
        --ledger-path /tmp/slonana-test-ledger \
        --rpc-bind-address 127.0.0.1:8898 \
        --gossip-bind-address 127.0.0.1:8002 \
        --network-id localnet \
        --enable-rpc \
        --reset-ledger &
    
    SLONANA_PID=$!
    echo "slonana.cpp validator started with PID: $SLONANA_PID"
    
    # Wait for validator to be ready
    while ! curl -s http://127.0.0.1:8898/health > /dev/null; do
        echo "Waiting for slonana.cpp validator to be ready..."
        sleep 2
    done
    
    echo "slonana.cpp validator is ready"
}

# Main setup function
main() {
    setup_agave_validator
    setup_slonana_validator
    
    echo "Test environment ready!"
    echo "Agave RPC: http://127.0.0.1:8899"
    echo "slonana.cpp RPC: http://127.0.0.1:8898"
}

main "$@"
```

### **2. Compatibility Test Suite**

```cpp
// tests/test_agave_compatibility.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <nlohmann/json.hpp>
#include "network/rpc_client.h"
#include "validator/core.h"
#include "consensus/tower_bft.h"

using json = nlohmann::json;

class AgaveCompatibilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize both validators
        agave_rpc_client_ = std::make_unique<network::RpcClient>("http://127.0.0.1:8899");
        slonana_rpc_client_ = std::make_unique<network::RpcClient>("http://127.0.0.1:8898");
        
        // Wait for both validators to be ready
        ASSERT_TRUE(wait_for_validator_ready(agave_rpc_client_.get()));
        ASSERT_TRUE(wait_for_validator_ready(slonana_rpc_client_.get()));
    }
    
    void TearDown() override {
        // Cleanup if needed
    }
    
    bool wait_for_validator_ready(network::RpcClient* client, int max_attempts = 30) {
        for (int i = 0; i < max_attempts; ++i) {
            try {
                auto response = client->call("getHealth");
                if (response.contains("result") && response["result"] == "ok") {
                    return true;
                }
            } catch (...) {
                // Ignore errors, validator might not be ready
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return false;
    }
    
    // Helper to compare JSON responses (ignoring order and minor differences)
    bool json_responses_compatible(const json& agave_response, const json& slonana_response) {
        // Compare structure and essential fields
        if (agave_response.contains("error") != slonana_response.contains("error")) {
            return false;
        }
        
        if (agave_response.contains("error")) {
            // Both have errors - compare error codes
            return agave_response["error"]["code"] == slonana_response["error"]["code"];
        }
        
        // Both successful - compare results
        return compare_result_values(agave_response["result"], slonana_response["result"]);
    }
    
    bool compare_result_values(const json& agave_result, const json& slonana_result) {
        // Custom comparison logic for different response types
        if (agave_result.type() != slonana_result.type()) {
            return false;
        }
        
        if (agave_result.is_object()) {
            // Compare object fields (with tolerance for timestamps, etc.)
            return compare_objects(agave_result, slonana_result);
        }
        
        return agave_result == slonana_result;
    }
    
    bool compare_objects(const json& agave_obj, const json& slonana_obj) {
        // Essential fields that must match exactly
        std::set<std::string> critical_fields = {
            "slot", "blockhash", "signatures", "lamports", 
            "owner", "executable", "rentEpoch"
        };
        
        for (const auto& field : critical_fields) {
            if (agave_obj.contains(field) != slonana_obj.contains(field)) {
                return false;
            }
            if (agave_obj.contains(field) && agave_obj[field] != slonana_obj[field]) {
                return false;
            }
        }
        
        return true;
    }
    
    std::unique_ptr<network::RpcClient> agave_rpc_client_;
    std::unique_ptr<network::RpcClient> slonana_rpc_client_;
};

// Test RPC API compatibility
TEST_F(AgaveCompatibilityTest, RpcApiCompatibility) {
    struct RpcTestCase {
        std::string method;
        json params;
        std::string description;
    };
    
    std::vector<RpcTestCase> test_cases = {
        {"getHealth", json::array(), "Health check"},
        {"getVersion", json::array(), "Version info"},
        {"getGenesisHash", json::array(), "Genesis hash"},
        {"getSlot", json::array(), "Current slot"},
        {"getBlockHeight", json::array(), "Block height"},
        {"getEpochInfo", json::array(), "Epoch information"},
        {"getBalance", json::array({"11111111111111111111111111111112"}), "Account balance"},
        {"getAccountInfo", json::array({"11111111111111111111111111111112"}), "Account info"},
        {"getRecentBlockhash", json::array(), "Recent blockhash"},
        {"getFeeForMessage", json::array({"AQABAgIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=="}), "Fee calculation"},
        {"getMinimumBalanceForRentExemption", json::array({0}), "Rent exemption"},
        {"getSupply", json::array(), "Token supply"},
        {"getLargestAccounts", json::array(), "Largest accounts"},
        {"getVoteAccounts", json::array(), "Vote accounts"},
        {"getClusterNodes", json::array(), "Cluster nodes"},
        {"getInflationGovernor", json::array(), "Inflation governor"},
        {"getInflationRate", json::array(), "Inflation rate"},
        {"getEpochSchedule", json::array(), "Epoch schedule"},
        {"getRecentPerformanceSamples", json::array(), "Performance samples"},
        {"getIdentity", json::array(), "Validator identity"}
    };
    
    for (const auto& test_case : test_cases) {
        SCOPED_TRACE("Testing RPC method: " + test_case.method);
        
        try {
            // Call both validators
            auto agave_response = agave_rpc_client_->call(test_case.method, test_case.params);
            auto slonana_response = slonana_rpc_client_->call(test_case.method, test_case.params);
            
            // Verify compatibility
            EXPECT_TRUE(json_responses_compatible(agave_response, slonana_response))
                << "Incompatible responses for " << test_case.method 
                << "\nAgave: " << agave_response.dump(2)
                << "\nslonana.cpp: " << slonana_response.dump(2);
                
        } catch (const std::exception& e) {
            FAIL() << "Exception testing " << test_case.method << ": " << e.what();
        }
    }
}

// Test transaction processing compatibility
TEST_F(AgaveCompatibilityTest, TransactionProcessingCompatibility) {
    // Create test transactions
    std::vector<std::string> test_transactions = {
        // System transfer transaction
        create_system_transfer_transaction(),
        // SPL token transfer transaction
        create_spl_token_transfer_transaction(),
        // Program invocation transaction
        create_program_invocation_transaction()
    };
    
    for (const auto& transaction : test_transactions) {
        SCOPED_TRACE("Testing transaction: " + transaction.substr(0, 20) + "...");
        
        // Send transaction to both validators
        auto agave_result = agave_rpc_client_->call("sendTransaction", 
            json::array({transaction, json::object({{"encoding", "base64"}})}));
        auto slonana_result = slonana_rpc_client_->call("sendTransaction", 
            json::array({transaction, json::object({{"encoding", "base64"}})}));
        
        // Both should succeed or fail in the same way
        EXPECT_EQ(agave_result.contains("error"), slonana_result.contains("error"));
        
        if (!agave_result.contains("error")) {
            // Both succeeded - signature should be valid
            EXPECT_TRUE(agave_result.contains("result"));
            EXPECT_TRUE(slonana_result.contains("result"));
            
            std::string agave_signature = agave_result["result"];
            std::string slonana_signature = slonana_result["result"];
            
            // Signatures will be different, but both should be valid
            EXPECT_EQ(agave_signature.length(), slonana_signature.length());
            EXPECT_EQ(agave_signature.length(), 88); // Base58 signature length
        }
    }
}

// Test consensus participation
TEST_F(AgaveCompatibilityTest, ConsensusParticipation) {
    // Monitor both validators for a period and ensure they reach consensus
    auto start_time = std::chrono::steady_clock::now();
    auto timeout = std::chrono::minutes(5);
    
    uint64_t agave_slot = 0;
    uint64_t slonana_slot = 0;
    
    while (std::chrono::steady_clock::now() - start_time < timeout) {
        // Get current slots
        auto agave_response = agave_rpc_client_->call("getSlot");
        auto slonana_response = slonana_rpc_client_->call("getSlot");
        
        if (agave_response.contains("result")) {
            agave_slot = agave_response["result"];
        }
        if (slonana_response.contains("result")) {
            slonana_slot = slonana_response["result"];
        }
        
        // Slots should be progressing and close to each other
        EXPECT_GT(agave_slot, 0);
        EXPECT_GT(slonana_slot, 0);
        
        // Allow some difference but not too much
        uint64_t slot_diff = std::abs(static_cast<int64_t>(agave_slot - slonana_slot));
        EXPECT_LT(slot_diff, 10) << "Slots too far apart: Agave=" << agave_slot 
                                 << ", slonana=" << slonana_slot;
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    // Final check - both validators should have progressed significantly
    EXPECT_GT(agave_slot, 50);
    EXPECT_GT(slonana_slot, 50);
}

// Helper functions for creating test transactions
std::string AgaveCompatibilityTest::create_system_transfer_transaction() {
    // Create a simple system transfer transaction (encoded as base64)
    return "AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABAAEDAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAEBAgAJAwAAAGQAAAAAAAAA";
}

std::string AgaveCompatibilityTest::create_spl_token_transfer_transaction() {
    // Create SPL token transfer transaction
    return "AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABAAEDAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAIBAgAJAwAAAGQAAAAAAAAA";
}

std::string AgaveCompatibilityTest::create_program_invocation_transaction() {
    // Create program invocation transaction
    return "AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABAAEDAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAMBAgAJAwAAAGQAAAAAAAAA";
}
```

### **3. Performance Benchmark Tests**

```cpp
// tests/test_performance_benchmarks.cpp
#include <gtest/gtest.h>
#include <benchmark/benchmark.h>
#include <chrono>
#include <thread>
#include <atomic>

class PerformanceBenchmarkTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize test environment
        setup_test_environment();
    }
    
    void setup_test_environment() {
        // Setup both validators for performance comparison
        // Implementation details...
    }
};

// RPC latency benchmark
static void BM_RpcLatency_Agave(benchmark::State& state) {
    network::RpcClient client("http://127.0.0.1:8899");
    
    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();
        auto response = client.call("getSlot");
        auto end = std::chrono::high_resolution_clock::now();
        
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        state.SetIterationTime(elapsed.count() / 1000000.0); // Convert to seconds
    }
    
    state.SetComplexityN(state.iterations());
}

static void BM_RpcLatency_Slonana(benchmark::State& state) {
    network::RpcClient client("http://127.0.0.1:8898");
    
    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();
        auto response = client.call("getSlot");
        auto end = std::chrono::high_resolution_clock::now();
        
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        state.SetIterationTime(elapsed.count() / 1000000.0);
    }
    
    state.SetComplexityN(state.iterations());
}

BENCHMARK(BM_RpcLatency_Agave)->UseManualTime()->Iterations(1000);
BENCHMARK(BM_RpcLatency_Slonana)->UseManualTime()->Iterations(1000);

// Transaction throughput benchmark
static void BM_TransactionThroughput_Agave(benchmark::State& state) {
    network::RpcClient client("http://127.0.0.1:8899");
    std::atomic<uint64_t> transactions_sent{0};
    
    for (auto _ : state) {
        // Send batch of transactions
        std::vector<std::thread> workers;
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < 10; ++i) {
            workers.emplace_back([&client, &transactions_sent]() {
                for (int j = 0; j < 100; ++j) {
                    try {
                        // Send test transaction
                        auto transaction = create_test_transaction();
                        client.call("sendTransaction", json::array({transaction}));
                        transactions_sent++;
                    } catch (...) {
                        // Continue on errors
                    }
                }
            });
        }
        
        for (auto& worker : workers) {
            worker.join();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - start);
        
        state.counters["TPS"] = transactions_sent.load() / elapsed.count();
    }
}

static void BM_TransactionThroughput_Slonana(benchmark::State& state) {
    network::RpcClient client("http://127.0.0.1:8898");
    std::atomic<uint64_t> transactions_sent{0};
    
    for (auto _ : state) {
        // Same test as Agave but with slonana.cpp
        std::vector<std::thread> workers;
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < 10; ++i) {
            workers.emplace_back([&client, &transactions_sent]() {
                for (int j = 0; j < 100; ++j) {
                    try {
                        auto transaction = create_test_transaction();
                        client.call("sendTransaction", json::array({transaction}));
                        transactions_sent++;
                    } catch (...) {
                        // Continue on errors
                    }
                }
            });
        }
        
        for (auto& worker : workers) {
            worker.join();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - start);
        
        state.counters["TPS"] = transactions_sent.load() / elapsed.count();
    }
}

BENCHMARK(BM_TransactionThroughput_Agave)->Iterations(10);
BENCHMARK(BM_TransactionThroughput_Slonana)->Iterations(10);

// Memory usage benchmark
TEST_F(PerformanceBenchmarkTest, MemoryUsageComparison) {
    auto measure_memory_usage = [](const std::string& process_name) -> uint64_t {
        std::string command = "ps -C " + process_name + " -o rss= | awk '{sum+=$1} END {print sum}'";
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) return 0;
        
        char buffer[128];
        std::string result;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        pclose(pipe);
        
        return std::stoull(result) * 1024; // Convert KB to bytes
    };
    
    // Wait for validators to be fully loaded
    std::this_thread::sleep_for(std::chrono::seconds(30));
    
    // Measure memory usage
    uint64_t agave_memory = measure_memory_usage("solana-test-validator");
    uint64_t slonana_memory = measure_memory_usage("slonana_validator");
    
    std::cout << "Memory usage comparison:" << std::endl;
    std::cout << "Agave: " << agave_memory / (1024 * 1024) << " MB" << std::endl;
    std::cout << "slonana.cpp: " << slonana_memory / (1024 * 1024) << " MB" << std::endl;
    
    // slonana.cpp should use significantly less memory
    EXPECT_LT(slonana_memory, agave_memory);
    EXPECT_LT(slonana_memory, 100 * 1024 * 1024); // Less than 100MB target
}

// CPU usage benchmark
TEST_F(PerformanceBenchmarkTest, CpuUsageComparison) {
    auto measure_cpu_usage = [](const std::string& process_name) -> double {
        std::string command = "ps -C " + process_name + " -o %cpu= | awk '{sum+=$1} END {print sum}'";
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) return 0.0;
        
        char buffer[128];
        std::string result;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        pclose(pipe);
        
        return std::stod(result);
    };
    
    // Generate load for 60 seconds and measure CPU usage
    std::vector<std::thread> load_generators;
    for (int i = 0; i < 5; ++i) {
        load_generators.emplace_back([this]() {
            auto end_time = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            while (std::chrono::steady_clock::now() < end_time) {
                // Generate transaction load
                try {
                    auto transaction = create_test_transaction();
                    agave_rpc_client_->call("sendTransaction", json::array({transaction}));
                    slonana_rpc_client_->call("sendTransaction", json::array({transaction}));
                } catch (...) {
                    // Continue on errors
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    
    // Wait for load generation to complete
    for (auto& generator : load_generators) {
        generator.join();
    }
    
    // Measure CPU usage during load
    double agave_cpu = measure_cpu_usage("solana-test-validator");
    double slonana_cpu = measure_cpu_usage("slonana_validator");
    
    std::cout << "CPU usage comparison:" << std::endl;
    std::cout << "Agave: " << agave_cpu << "%" << std::endl;
    std::cout << "slonana.cpp: " << slonana_cpu << "%" << std::endl;
    
    // slonana.cpp should use significantly less CPU
    EXPECT_LT(slonana_cpu, agave_cpu);
    EXPECT_LT(slonana_cpu, 10.0); // Less than 10% target
}
```

### **4. Network Protocol Tests**

```cpp
// tests/test_network_protocols.cpp
#include <gtest/gtest.h>
#include "network/gossip.h"
#include "network/turbine.h"
#include "consensus/tower_bft.h"

class NetworkProtocolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup test network environment
    }
};

// Test gossip protocol compatibility
TEST_F(NetworkProtocolTest, GossipProtocolCompatibility) {
    // Create test cluster info message
    network::ContactInfo contact_info;
    contact_info.pubkey = generate_test_pubkey();
    contact_info.gossip_addr = encode_socket_addr("127.0.0.1", 8001);
    contact_info.tvu_addr = encode_socket_addr("127.0.0.1", 8002);
    contact_info.tpu_addr = encode_socket_addr("127.0.0.1", 8003);
    contact_info.rpc_addr = encode_socket_addr("127.0.0.1", 8899);
    contact_info.wallclock = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    contact_info.shred_version = 1;
    
    // Create gossip message
    network::GossipMessage message;
    message.type = network::GossipMessage::CONTACT_INFO;
    message.data = serialize_contact_info(contact_info);
    message.wallclock = contact_info.wallclock;
    message.signature = sign_message(message.data);
    
    // Test serialization/deserialization
    auto serialized = message.serialize();
    auto deserialized = network::GossipMessage::deserialize(serialized);
    
    ASSERT_TRUE(deserialized.has_value());
    EXPECT_EQ(message.type, deserialized->type);
    EXPECT_EQ(message.wallclock, deserialized->wallclock);
    EXPECT_EQ(message.signature, deserialized->signature);
    
    // Test with Agave gossip format
    EXPECT_TRUE(verify_agave_gossip_compatibility(serialized));
}

// Test turbine protocol implementation
TEST_F(NetworkProtocolTest, TurbineProtocolCompatibility) {
    // Create test shred
    network::Shred shred;
    shred.header_.slot = 12345;
    shred.header_.index = 0;
    shred.header_.version = 1;
    shred.header_.fec_set_index = 0;
    shred.payload_ = std::vector<uint8_t>(1000, 0x42); // Test data
    
    // Sign shred
    auto signature = sign_shred(shred);
    std::copy(signature.begin(), signature.end(), shred.header_.signature);
    
    // Test shred serialization
    auto serialized = shred.serialize();
    auto deserialized = network::Shred::deserialize(serialized);
    
    ASSERT_TRUE(deserialized.has_value());
    EXPECT_EQ(shred.slot(), deserialized->slot());
    EXPECT_EQ(shred.index(), deserialized->index());
    
    // Test Agave compatibility
    EXPECT_TRUE(verify_agave_shred_compatibility(serialized));
    
    // Test turbine tree construction
    std::vector<network::TurbineNode> validators = create_test_validators(100);
    network::TurbineTree tree;
    tree.construct_tree(validators);
    
    // Verify tree properties match Agave
    for (const auto& validator : validators) {
        auto children = tree.get_children(validator);
        auto retransmit_peers = tree.get_retransmit_peers(validator);
        
        EXPECT_LE(children.size(), network::TurbineTree::DATA_PLANE_FANOUT);
        EXPECT_LE(retransmit_peers.size(), network::TurbineTree::MAX_RETRANSMIT_PEERS);
    }
}

// Test Tower BFT consensus
TEST_F(NetworkProtocolTest, TowerBftCompatibility) {
    consensus::Tower tower;
    
    // Test vote sequence
    std::vector<uint64_t> vote_slots = {100, 101, 102, 104, 108, 116, 132, 164};
    
    for (uint64_t slot : vote_slots) {
        EXPECT_TRUE(tower.can_vote_on_slot(slot));
        tower.record_vote(slot);
    }
    
    // Test lockout calculations
    EXPECT_EQ(tower.calculate_lockout_period(0), 2);   // 2^0 = 2
    EXPECT_EQ(tower.calculate_lockout_period(1), 4);   // 2^1 = 4
    EXPECT_EQ(tower.calculate_lockout_period(5), 64);  // 2^5 = 64
    
    // Test slot lockout
    EXPECT_TRUE(tower.is_slot_locked_out(99));   // Before last vote
    EXPECT_FALSE(tower.is_slot_locked_out(200)); // Future slot
    
    // Test with conflicting fork
    uint64_t conflicting_slot = 103; // Conflicts with 104
    EXPECT_FALSE(tower.can_vote_on_slot(conflicting_slot));
}

// Test QUIC protocol integration  
TEST_F(NetworkProtocolTest, QuicProtocolCompatibility) {
    // Start QUIC server
    network::QuicServer server;
    bool connection_received = false;
    
    server.set_connection_handler([&](std::unique_ptr<network::QuicConnection> conn) {
        connection_received = true;
        
        // Test stream operations
        uint64_t stream_id = conn->open_stream();
        EXPECT_GT(stream_id, 0);
        
        // Test data exchange
        std::vector<uint8_t> test_data = {1, 2, 3, 4, 5};
        EXPECT_TRUE(conn->send_data(stream_id, test_data));
        
        auto received_data = conn->receive_data(stream_id);
        ASSERT_TRUE(received_data.has_value());
        EXPECT_EQ(test_data, *received_data);
        
        conn->close_stream(stream_id);
    });
    
    ASSERT_TRUE(server.start("127.0.0.1", 9000));
    
    // Test client connection
    network::QuicConnection client("127.0.0.1", 9000);
    ASSERT_TRUE(client.connect());
    
    uint64_t stream_id = client.open_stream();
    std::vector<uint8_t> test_data = {1, 2, 3, 4, 5};
    EXPECT_TRUE(client.send_data(stream_id, test_data));
    
    auto received_data = client.receive_data(stream_id);
    ASSERT_TRUE(received_data.has_value());
    EXPECT_EQ(test_data, *received_data);
    
    client.disconnect();
    server.stop();
    
    EXPECT_TRUE(connection_received);
}
```

### **5. Security & Validation Tests**

```cpp
// tests/test_security_validation.cpp
#include <gtest/gtest.h>
#include "validation/transaction_validator.h"
#include "validation/network_validator.h"

class SecurityValidationTest : public ::testing::Test {
protected:
    void SetUp() override {
        validator_ = std::make_unique<validation::TransactionValidator>();
        network_validator_ = std::make_unique<validation::NetworkValidator>();
    }
    
    std::unique_ptr<validation::TransactionValidator> validator_;
    std::unique_ptr<validation::NetworkValidator> network_validator_;
};

// Test input validation
TEST_F(SecurityValidationTest, InputValidation) {
    // Test malformed transaction
    common::Transaction malformed_tx;
    malformed_tx.signatures.clear(); // No signatures
    
    auto result = validator_->validate_transaction(malformed_tx);
    EXPECT_FALSE(result.is_valid);
    EXPECT_TRUE(std::find(result.errors.begin(), result.errors.end(), 
                         validation::TransactionValidator::ValidationError::INVALID_SIGNATURE) 
                != result.errors.end());
    
    // Test oversized transaction
    common::Transaction oversized_tx = create_valid_transaction();
    oversized_tx.message.instructions[0].data.resize(1024 * 1024 * 2); // 2MB data
    
    result = validator_->validate_transaction(oversized_tx);
    EXPECT_FALSE(result.is_valid);
    
    // Test valid transaction
    common::Transaction valid_tx = create_valid_transaction();
    result = validator_->validate_transaction(valid_tx);
    EXPECT_TRUE(result.is_valid);
    EXPECT_TRUE(result.errors.empty());
}

// Test buffer overflow prevention
TEST_F(SecurityValidationTest, BufferOverflowPrevention) {
    // Test gossip message with oversized data
    network::GossipMessage oversized_msg;
    oversized_msg.type = network::GossipMessage::CONTACT_INFO;
    oversized_msg.data.resize(10 * 1024 * 1024); // 10MB - too large
    
    EXPECT_FALSE(network_validator_->validate_gossip_message(oversized_msg));
    
    // Test shred with invalid size
    network::Shred oversized_shred;
    oversized_shred.payload_.resize(network::Shred::MAX_SHRED_SIZE + 1);
    
    EXPECT_FALSE(network_validator_->validate_shred(oversized_shred));
    
    // Test normal-sized messages
    network::GossipMessage normal_msg = create_valid_gossip_message();
    EXPECT_TRUE(network_validator_->validate_gossip_message(normal_msg));
    
    network::Shred normal_shred = create_valid_shred();
    EXPECT_TRUE(network_validator_->validate_shred(normal_shred));
}

// Test rate limiting
TEST_F(SecurityValidationTest, RateLimiting) {
    std::string peer_address = "192.168.1.100";
    
    // Test normal rate
    for (int i = 0; i < 50; ++i) {
        EXPECT_TRUE(network_validator_->check_rate_limit(
            peer_address, network::GossipMessage::CONTACT_INFO));
    }
    
    // Test rate limit exceeded
    for (int i = 0; i < 100; ++i) {
        network_validator_->check_rate_limit(
            peer_address, network::GossipMessage::CONTACT_INFO);
    }
    
    // Should be rate limited now
    EXPECT_FALSE(network_validator_->check_rate_limit(
        peer_address, network::GossipMessage::CONTACT_INFO));
    
    // Different peer should not be affected
    EXPECT_TRUE(network_validator_->check_rate_limit(
        "192.168.1.101", network::GossipMessage::CONTACT_INFO));
}

// Test cryptographic validation
TEST_F(SecurityValidationTest, CryptographicValidation) {
    // Test signature validation
    auto keypair = generate_test_keypair();
    std::vector<uint8_t> message = {1, 2, 3, 4, 5};
    auto signature = sign_message(message, keypair.private_key);
    
    EXPECT_TRUE(verify_signature(message, signature, keypair.public_key));
    
    // Test invalid signature
    signature[0] ^= 0xFF; // Corrupt signature
    EXPECT_FALSE(verify_signature(message, signature, keypair.public_key));
    
    // Test hash validation
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    auto hash = calculate_hash(data);
    EXPECT_EQ(hash.size(), 32); // SHA-256
    
    // Verify hash consistency
    auto hash2 = calculate_hash(data);
    EXPECT_EQ(hash, hash2);
    
    // Different data should produce different hash
    data[0] = 0xFF;
    auto hash3 = calculate_hash(data);
    EXPECT_NE(hash, hash3);
}
```

## 📊 Continuous Integration Framework

### **6. GitHub Actions Workflow**

```yaml
# .github/workflows/agave-compatibility.yml
name: Agave Compatibility Testing

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main ]
  schedule:
    # Run weekly compatibility tests
    - cron: '0 6 * * 0'

jobs:
  compatibility-test:
    runs-on: ubuntu-latest
    timeout-minutes: 120
    
    steps:
    - name: Checkout repository
      uses: actions/checkout@v3
      
    - name: Setup build environment
      run: |
        sudo apt-get update
        sudo apt-get install -y \
          libssl-dev libudev-dev pkg-config zlib1g-dev \
          llvm clang cmake make libprotobuf-dev \
          protobuf-compiler libclang-dev \
          jq curl wget
    
    - name: Cache dependencies
      uses: actions/cache@v3
      with:
        path: |
          ~/.cargo
          ./build
        key: ${{ runner.os }}-deps-${{ hashFiles('**/CMakeLists.txt') }}
    
    - name: Setup Agave validator
      run: |
        ./scripts/setup-test-environment.sh setup_agave
      
    - name: Build slonana.cpp
      run: |
        mkdir -p build
        cd build
        cmake .. -DCMAKE_BUILD_TYPE=Release
        make -j$(nproc)
        
    - name: Setup slonana validator
      run: |
        ./scripts/setup-test-environment.sh setup_slonana
        
    - name: Run compatibility tests
      run: |
        cd build
        ./slonana_agave_compatibility_tests --gtest_output=xml:compatibility_results.xml
        
    - name: Run performance benchmarks
      run: |
        cd build
        ./slonana_performance_benchmarks --benchmark_format=json --benchmark_out=benchmark_results.json
        
    - name: Run security tests
      run: |
        cd build
        ./slonana_security_tests --gtest_output=xml:security_results.xml
        
    - name: Generate compatibility report
      run: |
        python3 scripts/generate-compatibility-report.py \
          --compatibility-results build/compatibility_results.xml \
          --benchmark-results build/benchmark_results.json \
          --security-results build/security_results.xml \
          --output compatibility-report.html
          
    - name: Upload test results
      uses: actions/upload-artifact@v3
      with:
        name: test-results
        path: |
          build/*_results.xml
          build/benchmark_results.json
          compatibility-report.html
          
    - name: Upload compatibility report
      uses: actions/upload-artifact@v3
      with:
        name: compatibility-report
        path: compatibility-report.html
        
    - name: Comment PR with results
      if: github.event_name == 'pull_request'
      uses: actions/github-script@v6
      with:
        script: |
          const fs = require('fs');
          const path = 'compatibility-report.html';
          if (fs.existsSync(path)) {
            const report = fs.readFileSync(path, 'utf8');
            // Extract summary from report and post as comment
            github.rest.issues.createComment({
              issue_number: context.issue.number,
              owner: context.repo.owner,
              repo: context.repo.repo,
              body: '## Agave Compatibility Test Results\n\n' + 
                    'Full report available in artifacts.\n\n' +
                    '### Summary\n' +
                    '- Compatibility tests: See artifacts\n' +
                    '- Performance benchmarks: See artifacts\n' +
                    '- Security validation: See artifacts'
            });
          }

  nightly-stress-test:
    runs-on: ubuntu-latest
    if: github.event_name == 'schedule'
    timeout-minutes: 480  # 8 hours
    
    steps:
    - name: Checkout repository
      uses: actions/checkout@v3
      
    - name: Setup environment
      run: ./scripts/setup-test-environment.sh
      
    - name: Run 24-hour stress test
      run: |
        cd build
        timeout 24h ./slonana_stress_tests --duration=24h --output=stress_results.json
        
    - name: Analyze stress test results
      run: |
        python3 scripts/analyze-stress-results.py stress_results.json
        
    - name: Upload stress test results
      uses: actions/upload-artifact@v3
      with:
        name: stress-test-results
        path: stress_results.json
```

### **7. Test Result Analysis**

```python
#!/usr/bin/env python3
# scripts/generate-compatibility-report.py

import argparse
import json
import xml.etree.ElementTree as ET
from datetime import datetime
import sys

def parse_gtest_xml(xml_file):
    """Parse Google Test XML results."""
    tree = ET.parse(xml_file)
    root = tree.getroot()
    
    results = {
        'total_tests': 0,
        'passed_tests': 0,
        'failed_tests': 0,
        'failures': []
    }
    
    for testcase in root.iter('testcase'):
        results['total_tests'] += 1
        
        failure = testcase.find('failure')
        if failure is not None:
            results['failed_tests'] += 1
            results['failures'].append({
                'name': testcase.get('name'),
                'classname': testcase.get('classname'),
                'message': failure.get('message', ''),
                'details': failure.text or ''
            })
        else:
            results['passed_tests'] += 1
    
    return results

def parse_benchmark_json(json_file):
    """Parse benchmark results."""
    with open(json_file, 'r') as f:
        data = json.load(f)
    
    benchmarks = {}
    for benchmark in data.get('benchmarks', []):
        name = benchmark['name']
        benchmarks[name] = {
            'cpu_time': benchmark.get('cpu_time', 0),
            'real_time': benchmark.get('real_time', 0),
            'iterations': benchmark.get('iterations', 0),
            'time_unit': benchmark.get('time_unit', 'ns')
        }
    
    return benchmarks

def generate_html_report(compatibility_results, benchmark_results, security_results, output_file):
    """Generate HTML compatibility report."""
    
    html_template = f"""
<!DOCTYPE html>
<html>
<head>
    <title>Agave Compatibility Report</title>
    <style>
        body {{ font-family: Arial, sans-serif; margin: 20px; }}
        .header {{ background-color: #f0f0f0; padding: 20px; border-radius: 5px; }}
        .section {{ margin: 20px 0; }}
        .passed {{ color: green; }}
        .failed {{ color: red; }}
        .warning {{ color: orange; }}
        table {{ border-collapse: collapse; width: 100%; }}
        th, td {{ border: 1px solid #ddd; padding: 8px; text-align: left; }}
        th {{ background-color: #f2f2f2; }}
        .failure-details {{ background-color: #ffe6e6; padding: 10px; margin: 5px 0; border-radius: 3px; }}
    </style>
</head>
<body>
    <div class="header">
        <h1>Agave Compatibility Report</h1>
        <p>Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}</p>
    </div>
    
    <div class="section">
        <h2>Test Summary</h2>
        <table>
            <tr>
                <th>Test Category</th>
                <th>Total Tests</th>
                <th>Passed</th>
                <th>Failed</th>
                <th>Success Rate</th>
            </tr>
            <tr>
                <td>Compatibility Tests</td>
                <td>{compatibility_results['total_tests']}</td>
                <td class="passed">{compatibility_results['passed_tests']}</td>
                <td class="failed">{compatibility_results['failed_tests']}</td>
                <td>{(compatibility_results['passed_tests'] / compatibility_results['total_tests'] * 100):.1f}%</td>
            </tr>
            <tr>
                <td>Security Tests</td>
                <td>{security_results['total_tests']}</td>
                <td class="passed">{security_results['passed_tests']}</td>
                <td class="failed">{security_results['failed_tests']}</td>
                <td>{(security_results['passed_tests'] / security_results['total_tests'] * 100):.1f}%</td>
            </tr>
        </table>
    </div>
    
    <div class="section">
        <h2>Performance Benchmarks</h2>
        <table>
            <tr>
                <th>Benchmark</th>
                <th>Time (ms)</th>
                <th>Iterations</th>
                <th>Status</th>
            </tr>
    """
    
    # Add benchmark results
    performance_targets = {
        'BM_RpcLatency_Slonana': 4.0,  # Target: <4ms
        'BM_TransactionThroughput_Slonana': 4000  # Target: >4000 TPS
    }
    
    for name, result in benchmark_results.items():
        if 'Slonana' in name:
            time_ms = result['real_time'] / 1000000  # Convert ns to ms
            target = performance_targets.get(name, None)
            status = "✅ PASS" if target and time_ms < target else "❌ FAIL"
            
            html_template += f"""
            <tr>
                <td>{name}</td>
                <td>{time_ms:.2f}</td>
                <td>{result['iterations']}</td>
                <td>{status}</td>
            </tr>
            """
    
    html_template += """
        </table>
    </div>
    """
    
    # Add failure details if any
    if compatibility_results['failures'] or security_results['failures']:
        html_template += """
        <div class="section">
            <h2>Test Failures</h2>
        """
        
        for failure in compatibility_results['failures'] + security_results['failures']:
            html_template += f"""
            <div class="failure-details">
                <h4>{failure['classname']}.{failure['name']}</h4>
                <p><strong>Message:</strong> {failure['message']}</p>
                <pre>{failure['details']}</pre>
            </div>
            """
        
        html_template += "</div>"
    
    html_template += """
    </body>
    </html>
    """
    
    with open(output_file, 'w') as f:
        f.write(html_template)

def main():
    parser = argparse.ArgumentParser(description='Generate Agave compatibility report')
    parser.add_argument('--compatibility-results', required=True, help='Compatibility test XML results')
    parser.add_argument('--benchmark-results', required=True, help='Benchmark JSON results')
    parser.add_argument('--security-results', required=True, help='Security test XML results')
    parser.add_argument('--output', required=True, help='Output HTML file')
    
    args = parser.parse_args()
    
    try:
        compatibility_results = parse_gtest_xml(args.compatibility_results)
        benchmark_results = parse_benchmark_json(args.benchmark_results)
        security_results = parse_gtest_xml(args.security_results)
        
        generate_html_report(compatibility_results, benchmark_results, security_results, args.output)
        
        print(f"Report generated: {args.output}")
        
        # Exit with error code if tests failed
        total_failures = compatibility_results['failed_tests'] + security_results['failed_tests']
        if total_failures > 0:
            print(f"WARNING: {total_failures} tests failed!")
            sys.exit(1)
            
    except Exception as e:
        print(f"Error generating report: {e}")
        sys.exit(1)

if __name__ == '__main__':
    main()
```

## 📋 Test Execution Plan

### **Phase 1: Core Compatibility Tests (Weeks 1-4)**
- [ ] Tower BFT consensus implementation tests
- [ ] Turbine protocol compatibility tests  
- [ ] QUIC integration validation tests
- [ ] Basic banking stage functionality tests

### **Phase 2: Advanced Feature Tests (Weeks 5-8)**
- [ ] Complete RPC API compatibility tests
- [ ] Enhanced accounts database tests
- [ ] Advanced fork choice algorithm tests
- [ ] Program cache optimization tests

### **Phase 3: Performance & Security Tests (Weeks 9-12)**
- [ ] Performance benchmark validation
- [ ] Security vulnerability assessment
- [ ] Stress testing and reliability validation
- [ ] Production readiness evaluation

## 🎯 Success Criteria

### **Functional Compatibility**
- [ ] 100% RPC API compatibility with Agave
- [ ] Successful consensus participation in Agave networks
- [ ] All transaction types processed identically
- [ ] Network protocol messages compatible

### **Performance Requirements**  
- [ ] RPC latency < 4ms (better than Agave's 5ms)
- [ ] Transaction throughput > 4,000 TPS
- [ ] Memory usage < 100MB
- [ ] CPU usage < 10%

### **Security & Reliability**
- [ ] All security tests passing
- [ ] No buffer overflows or memory leaks
- [ ] Proper input validation and rate limiting
- [ ] 24/7 operation without crashes

---

## 🎉 Conclusion

This comprehensive test framework ensures that slonana.cpp achieves perfect Agave compatibility while maintaining its performance advantages. The multi-layered testing approach validates functionality, performance, security, and reliability at every level.

**Key Testing Principles:**
1. **Continuous Validation** - Automated testing on every change
2. **Real-World Scenarios** - Testing with actual Agave validators
3. **Performance Focus** - Maintaining efficiency advantages
4. **Security First** - Comprehensive vulnerability assessment

With this test framework, slonana.cpp will be validated as the definitive high-performance Agave-compatible validator implementation.

---

*This test framework provides the quality assurance foundation for achieving and maintaining Agave compatibility.*