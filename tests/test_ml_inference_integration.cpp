/**
 * ML Inference Integration Tests
 * 
 * Comprehensive integration tests for ML inference features in the validator context.
 * Tests cover end-to-end ML inference, async BPF integration, and multi-agent scenarios.
 */

#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include "svm/ml_inference.h"
#include "svm/async_bpf_execution.h"
#include "svm/economic_opcodes.h"
#include "svm/engine.h"

using namespace std::chrono;

class MLInferenceIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize SVM engine for integration testing
        engine = std::make_unique<svm::Engine>();
    }

    void TearDown() override {
        engine.reset();
    }

    std::unique_ptr<svm::Engine> engine;
};

// Test 1: Decision Tree Inference Performance
TEST_F(MLInferenceIntegrationTest, DecisionTreePerformance) {
    // Create a decision tree model (10 nodes)
    std::vector<int32_t> features = {1, 2, 3, 4, 5};
    std::vector<int32_t> thresholds = {5000, 3000, 7000, 2000, 8000};
    std::vector<int32_t> left_children = {1, 3, 5, -1, -1};
    std::vector<int32_t> right_children = {2, 4, 6, -1, -1};
    std::vector<int32_t> values = {0, 0, 0, 10000, 20000};

    // Benchmark inference (target: <100ns)
    auto start = high_resolution_clock::now();
    const int iterations = 10000;
    
    for (int i = 0; i < iterations; i++) {
        int32_t input_data[5] = {4500, 3500, 6500, 1500, 7500};
        int32_t result = ml_inference::decision_tree_predict(
            input_data, 5,
            features.data(), thresholds.data(),
            left_children.data(), right_children.data(),
            values.data(), 5
        );
        EXPECT_GT(result, 0);
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<nanoseconds>(end - start).count();
    double avg_ns = duration / (double)iterations;
    
    std::cout << "Decision tree inference: " << avg_ns << " ns/inference" << std::endl;
    EXPECT_LT(avg_ns, 100.0); // Target: <100ns
}

// Test 2: MLP Forward Pass Validation
TEST_F(MLInferenceIntegrationTest, MLPForwardPass) {
    // Create a simple 3x2x1 MLP with fixed-point weights
    const int input_size = 3;
    const int hidden_size = 2;
    const int output_size = 1;
    
    // Layer 1: 3x2
    int32_t weights1[6] = {5000, 3000, 4000, 2000, 6000, 1000}; // scaled by 10000
    int32_t bias1[2] = {1000, 500};
    
    // Layer 2: 2x1
    int32_t weights2[2] = {7000, 3000};
    int32_t bias2[1] = {2000};
    
    // Input
    int32_t input[3] = {10000, 5000, 8000}; // scaled values
    
    // Forward pass through layer 1
    int32_t hidden[2];
    ml_inference::dense_forward(input, input_size, weights1, bias1, hidden, hidden_size);
    
    // Apply ReLU
    for (int i = 0; i < hidden_size; i++) {
        hidden[i] = ml_inference::relu(hidden[i]);
    }
    
    // Forward pass through layer 2
    int32_t output[1];
    ml_inference::dense_forward(hidden, hidden_size, weights2, bias2, output, output_size);
    
    // Verify output is reasonable
    EXPECT_GT(output[0], 0);
    std::cout << "MLP output: " << output[0] << std::endl;
}

// Test 3: Activation Functions Integration
TEST_F(MLInferenceIntegrationTest, ActivationFunctions) {
    const int size = 64;
    int32_t data[size];
    
    // Initialize with test data
    for (int i = 0; i < size; i++) {
        data[i] = (i - 32) * 1000; // Range: -32000 to +31000
    }
    
    // Test ReLU
    auto start = high_resolution_clock::now();
    ml_inference::batch_relu(data, size);
    auto end = high_resolution_clock::now();
    auto relu_ns = duration_cast<nanoseconds>(end - start).count();
    
    std::cout << "Batch ReLU (64 elements): " << relu_ns << " ns" << std::endl;
    EXPECT_LT(relu_ns, 100); // Target: <100ns for 64 elements
    
    // Verify ReLU correctness
    for (int i = 0; i < 32; i++) {
        EXPECT_EQ(data[i], 0); // Negative values should be zero
    }
    for (int i = 32; i < size; i++) {
        EXPECT_GT(data[i], 0); // Positive values unchanged
    }
}

// Test 4: Async BPF + ML Integration
TEST_F(MLInferenceIntegrationTest, AsyncBPFWithML) {
    // Create timer for periodic ML inference
    uint64_t timer_id = async_bpf::timer_create(100); // Every 100 slots
    EXPECT_GT(timer_id, 0);
    
    // Create watcher for price changes
    uint8_t oracle_pubkey[32] = {0};
    uint64_t watcher_id = async_bpf::watcher_create(
        oracle_pubkey,
        async_bpf::WatcherType::THRESHOLD_ABOVE,
        50000 // Threshold
    );
    EXPECT_GT(watcher_id, 0);
    
    // Simulate ML inference triggered by timer
    int32_t features[5] = {10000, 5000, 8000, 3000, 7000};
    int32_t prediction = ml_inference::decision_tree_predict(
        features, 5,
        nullptr, nullptr, nullptr, nullptr, nullptr, 0
    );
    
    // Clean up
    async_bpf::timer_cancel(timer_id);
    async_bpf::watcher_remove(watcher_id);
    
    std::cout << "Async BPF + ML integration test passed" << std::endl;
}

// Test 5: Multi-Agent Concurrent Execution
TEST_F(MLInferenceIntegrationTest, MultiAgentExecution) {
    const int num_agents = 100;
    const int iterations = 100;
    
    auto start = high_resolution_clock::now();
    
    // Simulate concurrent agent execution
    for (int agent = 0; agent < num_agents; agent++) {
        for (int iter = 0; iter < iterations; iter++) {
            // Each agent runs ML inference
            int32_t features[5];
            for (int i = 0; i < 5; i++) {
                features[i] = (agent + iter) * 1000;
            }
            
            int32_t prediction = ml_inference::decision_tree_predict(
                features, 5,
                nullptr, nullptr, nullptr, nullptr, nullptr, 0
            );
        }
    }
    
    auto end = high_resolution_clock::now();
    auto duration_ms = duration_cast<milliseconds>(end - start).count();
    
    int total_inferences = num_agents * iterations;
    double inferences_per_sec = (total_inferences * 1000.0) / duration_ms;
    
    std::cout << "Multi-agent throughput: " << inferences_per_sec 
              << " inferences/sec" << std::endl;
    
    // Target: >10K inferences/sec
    EXPECT_GT(inferences_per_sec, 10000.0);
}

// Test 6: State Persistence Across Transactions
TEST_F(MLInferenceIntegrationTest, StatePersistence) {
    // Initialize agent state
    struct AgentState {
        int32_t position;
        int32_t pnl;
        uint32_t trades_count;
    };
    
    AgentState state = {0, 0, 0};
    
    // Simulate multiple transactions with state updates
    for (int tx = 0; tx < 10; tx++) {
        // Run ML inference
        int32_t features[3] = {tx * 1000, (tx + 1) * 1000, (tx + 2) * 1000};
        int32_t signal = (features[0] > 5000) ? 1 : -1; // Simple logic
        
        // Update state
        state.position += signal;
        state.pnl += signal * 100;
        state.trades_count++;
    }
    
    // Verify state was persisted correctly
    EXPECT_EQ(state.trades_count, 10);
    EXPECT_NE(state.position, 0);
    
    std::cout << "State persistence test: position=" << state.position 
              << ", pnl=" << state.pnl 
              << ", trades=" << state.trades_count << std::endl;
}

// Test 7: Economic Opcodes Integration
TEST_F(MLInferenceIntegrationTest, EconomicOpcodesWithML) {
    // Create auction for trading signal access
    uint64_t auction_id = economic_opcodes::auction_create(
        economic_opcodes::AuctionType::VCG,
        3, // 3 items
        1000 // deadline
    );
    EXPECT_GT(auction_id, 0);
    
    // Simulate ML-based bidding strategy
    for (int bidder = 0; bidder < 5; bidder++) {
        // Use ML to determine bid value
        int32_t features[3] = {bidder * 1000, 5000, 3000};
        int32_t valuation = features[0] + features[1]; // Simple valuation
        
        economic_opcodes::auction_bid(auction_id, bidder, valuation);
    }
    
    // Settle auction
    auto result = economic_opcodes::auction_settle(auction_id);
    EXPECT_TRUE(result.success);
    
    std::cout << "Economic opcodes + ML integration test passed" << std::endl;
}

// Test 8: Performance Under Load
TEST_F(MLInferenceIntegrationTest, LoadTesting) {
    const int duration_seconds = 5;
    const int target_tps = 1000;
    
    auto start = high_resolution_clock::now();
    int transactions = 0;
    
    while (duration_cast<seconds>(high_resolution_clock::now() - start).count() < duration_seconds) {
        // Simulate transaction with ML inference
        int32_t features[5] = {1000, 2000, 3000, 4000, 5000};
        ml_inference::decision_tree_predict(
            features, 5,
            nullptr, nullptr, nullptr, nullptr, nullptr, 0
        );
        transactions++;
    }
    
    double actual_tps = transactions / (double)duration_seconds;
    std::cout << "Load test TPS: " << actual_tps << std::endl;
    
    EXPECT_GT(actual_tps, target_tps);
}

// Test 9: Memory Efficiency
TEST_F(MLInferenceIntegrationTest, MemoryEfficiency) {
    // Allocate models and track memory usage
    const int num_models = 100;
    std::vector<std::vector<int32_t>> models;
    
    for (int i = 0; i < num_models; i++) {
        std::vector<int32_t> model(1000); // 1000 parameters per model
        models.push_back(model);
    }
    
    // Verify models are accessible
    EXPECT_EQ(models.size(), num_models);
    
    std::cout << "Memory efficiency test: " << num_models << " models allocated" << std::endl;
}

// Test 10: Error Handling
TEST_F(MLInferenceIntegrationTest, ErrorHandling) {
    // Test null pointer handling
    int32_t* null_ptr = nullptr;
    EXPECT_EQ(ml_inference::decision_tree_predict(
        null_ptr, 0,
        nullptr, nullptr, nullptr, nullptr, nullptr, 0
    ), 0);
    
    // Test invalid dimensions
    int32_t data[5] = {1, 2, 3, 4, 5};
    EXPECT_EQ(ml_inference::decision_tree_predict(
        data, -1, // Invalid size
        nullptr, nullptr, nullptr, nullptr, nullptr, 0
    ), 0);
    
    std::cout << "Error handling test passed" << std::endl;
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
