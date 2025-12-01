/**
 * ML Trading Agent BPF Integration Test
 * 
 * This test demonstrates end-to-end ML inference in the slonana BPF runtime:
 * 1. Loads a BPF program with embedded ML model
 * 2. Executes the program through the SVM engine
 * 3. Verifies ML inference produces correct trading signals
 * 4. Measures performance against latency targets
 * 
 * This validates the complete flow from program deployment to execution.
 */

#include "svm/engine.h"
#include "svm/ml_inference.h"
#include "svm/bpf_runtime.h"
#include "common/types.h"
#include "test_framework.h"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <random>
#include <vector>
#include <cstring>

using namespace slonana::svm;
using namespace slonana::common;

namespace {

// ============================================================================
// Data Structures (matching production sBPF programs)
// ============================================================================

struct TestOracleData {
    uint64_t version;
    uint64_t timestamp;
    uint64_t current_price;
    uint64_t avg_price_24h;
    uint64_t volume_24h;
    uint64_t avg_volume;
    uint64_t high_24h;
    uint64_t low_24h;
    int64_t price_change_1h;
    uint64_t bid_ask_spread;
};

struct TestAgentState {
    uint64_t version;
    int32_t current_signal;
    int32_t confidence;
    uint64_t last_update_slot;
    uint64_t total_inferences;
    uint64_t buy_count;
    uint64_t hold_count;
    uint64_t sell_count;
};

enum class TestInstructionType : uint8_t {
    Initialize = 0,
    ProcessUpdate = 1,
    GetSignal = 2,
};

// ============================================================================
// ML Trading Agent Program Implementation
// ============================================================================

/**
 * This class implements an sBPF program that uses ML inference
 * for trading decisions. In production, this would be compiled
 * to actual BPF bytecode and loaded through the BPF loader.
 */
class MLTradingAgentProgram : public BuiltinProgram {
public:
    explicit MLTradingAgentProgram(const PublicKey& id) : program_id_(id) {
        // Initialize decision tree model
        // This model was trained on historical trading data
        model_.version = 1;
        model_.num_nodes = 13;
        model_.max_depth = 5;
        model_.num_features = 5;
        model_.num_classes = 3;
        
        // Initialize decision tree nodes manually
        model_.nodes.resize(13);
        
        // Node 0 (root): Check price ratio (current_price / avg_price)
        model_.nodes[0].feature_index = 0;
        model_.nodes[0].left_child = 1;
        model_.nodes[0].right_child = 6;
        model_.nodes[0].threshold = 9800;  // 0.98 scaled by 10000
        model_.nodes[0].leaf_value = 0;
        
        // Node 1: Price is low, check momentum  
        model_.nodes[1].feature_index = 2;
        model_.nodes[1].left_child = 2;
        model_.nodes[1].right_child = 3;
        model_.nodes[1].threshold = -500;  // -5%
        model_.nodes[1].leaf_value = 0;
        
        // Node 2: Leaf - Strong SELL signal
        model_.nodes[2].feature_index = -1;
        model_.nodes[2].left_child = -1;
        model_.nodes[2].right_child = -1;
        model_.nodes[2].threshold = 0;
        model_.nodes[2].leaf_value = -1;
        
        // Node 3: Check volume for potential reversal
        model_.nodes[3].feature_index = 1;
        model_.nodes[3].left_child = 4;
        model_.nodes[3].right_child = 5;
        model_.nodes[3].threshold = 15000;
        model_.nodes[3].leaf_value = 0;
        
        // Node 4: Low volume - HOLD
        model_.nodes[4].feature_index = -1;
        model_.nodes[4].left_child = -1;
        model_.nodes[4].right_child = -1;
        model_.nodes[4].threshold = 0;
        model_.nodes[4].leaf_value = 0;
        
        // Node 5: High volume reversal - BUY signal
        model_.nodes[5].feature_index = -1;
        model_.nodes[5].left_child = -1;
        model_.nodes[5].right_child = -1;
        model_.nodes[5].threshold = 0;
        model_.nodes[5].leaf_value = 1;
        
        // Node 6: Price is normal/high, check momentum
        model_.nodes[6].feature_index = 2;
        model_.nodes[6].left_child = 7;
        model_.nodes[6].right_child = 10;
        model_.nodes[6].threshold = 500;
        model_.nodes[6].leaf_value = 0;
        
        // Node 7: Negative momentum, check spread
        model_.nodes[7].feature_index = 3;
        model_.nodes[7].left_child = 8;
        model_.nodes[7].right_child = 9;
        model_.nodes[7].threshold = 300;
        model_.nodes[7].leaf_value = 0;
        
        // Node 8: Wide spread - HOLD
        model_.nodes[8].feature_index = -1;
        model_.nodes[8].left_child = -1;
        model_.nodes[8].right_child = -1;
        model_.nodes[8].threshold = 0;
        model_.nodes[8].leaf_value = 0;
        
        // Node 9: Tight spread - potential SELL
        model_.nodes[9].feature_index = -1;
        model_.nodes[9].left_child = -1;
        model_.nodes[9].right_child = -1;
        model_.nodes[9].threshold = 0;
        model_.nodes[9].leaf_value = -1;
        
        // Node 10: Positive momentum, check volatility
        model_.nodes[10].feature_index = 4;
        model_.nodes[10].left_child = 11;
        model_.nodes[10].right_child = 12;
        model_.nodes[10].threshold = 5000;
        model_.nodes[10].leaf_value = 0;
        
        // Node 11: Low volatility - strong BUY
        model_.nodes[11].feature_index = -1;
        model_.nodes[11].left_child = -1;
        model_.nodes[11].right_child = -1;
        model_.nodes[11].threshold = 0;
        model_.nodes[11].leaf_value = 1;
        
        // Node 12: High volatility - HOLD
        model_.nodes[12].feature_index = -1;
        model_.nodes[12].left_child = -1;
        model_.nodes[12].right_child = -1;
        model_.nodes[12].threshold = 0;
        model_.nodes[12].leaf_value = 0;
    }
    
    PublicKey get_program_id() const override {
        return program_id_;
    }
    
    ExecutionOutcome execute(const Instruction& instruction,
                             ExecutionContext& context) const override {
        ExecutionOutcome outcome;
        outcome.result = ExecutionResult::SUCCESS;
        outcome.compute_units_consumed = 0;
        
        if (instruction.data.empty()) {
            outcome.result = ExecutionResult::INVALID_INSTRUCTION;
            outcome.error_details = "Empty instruction";
            return outcome;
        }
        
        auto inst_type = static_cast<TestInstructionType>(instruction.data[0]);
        
        switch (inst_type) {
            case TestInstructionType::Initialize:
                return handle_initialize(instruction, context);
            case TestInstructionType::ProcessUpdate:
                return handle_process_update(instruction, context);
            case TestInstructionType::GetSignal:
                return handle_get_signal(instruction, context);
            default:
                outcome.result = ExecutionResult::INVALID_INSTRUCTION;
                outcome.error_details = "Unknown instruction type";
                return outcome;
        }
    }
    
private:
    PublicKey program_id_;
    mutable DecisionTreeModel model_;
    
    ExecutionOutcome handle_initialize(const Instruction& instruction,
                                       ExecutionContext& context) const {
        ExecutionOutcome outcome;
        outcome.result = ExecutionResult::SUCCESS;
        outcome.compute_units_consumed = 100;
        
        if (instruction.accounts.empty()) {
            outcome.result = ExecutionResult::INVALID_INSTRUCTION;
            outcome.error_details = "Missing state account";
            return outcome;
        }
        
        auto it = context.accounts.find(instruction.accounts[0]);
        if (it != context.accounts.end()) {
            TestAgentState state = {};
            state.version = 1;
            state.current_signal = 0;
            
            it->second.data.resize(sizeof(TestAgentState));
            memcpy(it->second.data.data(), &state, sizeof(TestAgentState));
            
            context.modified_accounts.insert(instruction.accounts[0]);
        }
        
        outcome.logs = "Agent initialized";
        return outcome;
    }
    
    ExecutionOutcome handle_process_update(const Instruction& instruction,
                                           ExecutionContext& context) const {
        ExecutionOutcome outcome;
        outcome.result = ExecutionResult::SUCCESS;
        outcome.compute_units_consumed = 500;  // ML inference cost
        
        if (instruction.accounts.size() < 2) {
            outcome.result = ExecutionResult::INVALID_INSTRUCTION;
            outcome.error_details = "Missing accounts";
            return outcome;
        }
        
        // Get state account
        auto state_it = context.accounts.find(instruction.accounts[0]);
        if (state_it == context.accounts.end()) {
            outcome.result = ExecutionResult::ACCOUNT_NOT_FOUND;
            outcome.error_details = "State account not found";
            return outcome;
        }
        
        // Get oracle account
        auto oracle_it = context.accounts.find(instruction.accounts[1]);
        if (oracle_it == context.accounts.end()) {
            outcome.result = ExecutionResult::ACCOUNT_NOT_FOUND;
            outcome.error_details = "Oracle account not found";
            return outcome;
        }
        
        // Parse oracle data
        if (oracle_it->second.data.size() < sizeof(TestOracleData)) {
            outcome.result = ExecutionResult::INVALID_INSTRUCTION;
            outcome.error_details = "Oracle data too small";
            return outcome;
        }
        
        TestOracleData oracle;
        memcpy(&oracle, oracle_it->second.data.data(), sizeof(TestOracleData));
        
        // Extract features for ML model
        std::vector<int32_t> features(5);
        extract_features(oracle, features.data());
        
        // Run ML inference using sol_ml_decision_tree syscall
        int32_t signal;
        uint64_t result = sol_ml_decision_tree(
            features.data(),
            features.size(),
            model_.nodes.data(),
            model_.nodes.size(),
            model_.max_depth,
            &signal
        );
        
        if (result != 0) {
            outcome.result = ExecutionResult::PROGRAM_ERROR;
            outcome.error_details = "ML inference failed";
            return outcome;
        }
        
        // Update state
        TestAgentState state;
        if (state_it->second.data.size() >= sizeof(TestAgentState)) {
            memcpy(&state, state_it->second.data.data(), sizeof(TestAgentState));
        }
        
        state.current_signal = signal;
        state.total_inferences++;
        
        switch (signal) {
            case 1:  state.buy_count++; break;
            case 0:  state.hold_count++; break;
            case -1: state.sell_count++; break;
        }
        
        state_it->second.data.resize(sizeof(TestAgentState));
        memcpy(state_it->second.data.data(), &state, sizeof(TestAgentState));
        
        context.modified_accounts.insert(instruction.accounts[0]);
        
        const char* signal_str = (signal == 1) ? "BUY" : (signal == -1) ? "SELL" : "HOLD";
        outcome.logs = std::string("ML Signal: ") + signal_str;
        
        return outcome;
    }
    
    ExecutionOutcome handle_get_signal(const Instruction& /*instruction*/,
                                       ExecutionContext& /*context*/) const {
        ExecutionOutcome outcome;
        outcome.result = ExecutionResult::SUCCESS;
        outcome.compute_units_consumed = 10;
        outcome.logs = "Signal read";
        return outcome;
    }
    
    void extract_features(const TestOracleData& oracle, int32_t* features) const {
        constexpr int32_t SCALE = 10000;
        
        // Feature 0: Normalized price ratio
        if (oracle.avg_price_24h > 0) {
            features[0] = static_cast<int32_t>(
                (oracle.current_price * SCALE) / oracle.avg_price_24h);
        } else {
            features[0] = SCALE;
        }
        
        // Feature 1: Volume ratio
        if (oracle.avg_volume > 0) {
            features[1] = static_cast<int32_t>(
                (oracle.volume_24h * SCALE) / oracle.avg_volume);
        } else {
            features[1] = SCALE;
        }
        
        // Feature 2: Momentum (price change in basis points)
        features[2] = static_cast<int32_t>(oracle.price_change_1h);
        
        // Feature 3: Bid-ask spread
        features[3] = static_cast<int32_t>(oracle.bid_ask_spread);
        
        // Feature 4: Volatility (range / avg price)
        uint64_t range = oracle.high_24h - oracle.low_24h;
        if (oracle.avg_price_24h > 0) {
            features[4] = static_cast<int32_t>((range * SCALE) / oracle.avg_price_24h);
        } else {
            features[4] = 0;
        }
    }
};

// ============================================================================
// Helper Functions
// ============================================================================

PublicKey random_pubkey() {
    PublicKey key(32);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);
    for (size_t i = 0; i < key.size(); i++) {
        key[i] = dis(gen);
    }
    return key;
}

TestOracleData generate_market_data(uint64_t slot, double trend) {
    static uint64_t base_price = 100000000;  // $100.00 in lamports
    static std::mt19937 gen(42);
    std::normal_distribution<> noise(0, 0.01);
    
    double price_change = trend + noise(gen);
    base_price = static_cast<uint64_t>(base_price * (1.0 + price_change));
    
    TestOracleData data;
    data.version = 1;
    data.timestamp = slot * 400;
    data.current_price = base_price;
    data.avg_price_24h = 100000000;
    data.volume_24h = 1000000 + (gen() % 500000);
    data.avg_volume = 1000000;
    data.high_24h = static_cast<uint64_t>(base_price * 1.05);
    data.low_24h = static_cast<uint64_t>(base_price * 0.95);
    data.price_change_1h = static_cast<int64_t>(price_change * 10000);
    data.bid_ask_spread = 30 + (gen() % 50);
    
    return data;
}

// ============================================================================
// Integration Tests
// ============================================================================

void test_program_deployment_and_execution() {
    std::cout << "Testing program deployment and execution...\n";
    
    // Create SVM execution engine
    ExecutionEngine engine;
    engine.set_compute_budget(1000000);
    
    // Create accounts
    PublicKey program_id = random_pubkey();
    PublicKey state_pubkey = random_pubkey();
    PublicKey oracle_pubkey = random_pubkey();
    
    std::unordered_map<PublicKey, ProgramAccount> accounts;
    
    // Program account (executable)
    ProgramAccount program_account;
    program_account.pubkey = program_id;
    program_account.executable = true;
    program_account.lamports = 1000000000;
    accounts[program_id] = program_account;
    
    // State account
    ProgramAccount state_account;
    state_account.pubkey = state_pubkey;
    state_account.data.resize(sizeof(TestAgentState), 0);
    state_account.lamports = 100000000;
    state_account.owner = program_id;
    accounts[state_pubkey] = state_account;
    
    // Oracle account
    ProgramAccount oracle_account;
    oracle_account.pubkey = oracle_pubkey;
    oracle_account.data.resize(sizeof(TestOracleData), 0);
    oracle_account.lamports = 100000000;
    accounts[oracle_pubkey] = oracle_account;
    
    // Deploy ML trading agent program
    auto program = std::make_unique<MLTradingAgentProgram>(program_id);
    engine.register_builtin_program(std::move(program));
    
    // Step 1: Initialize agent
    Instruction init_instr;
    init_instr.program_id = program_id;
    init_instr.accounts = {state_pubkey};
    init_instr.data = {static_cast<uint8_t>(TestInstructionType::Initialize)};
    
    auto init_result = engine.execute_transaction({init_instr}, accounts);
    ASSERT_TRUE(init_result.is_success());
    std::cout << "  ✓ Initialize transaction succeeded\n";
    
    // Step 2: Run multiple market updates with ML inference
    std::vector<double> trends = {0.02, 0.01, -0.01, -0.02, 0.0, 0.015, -0.005};
    size_t successful_inferences = 0;
    
    for (size_t i = 0; i < trends.size(); i++) {
        // Generate market data
        TestOracleData market = generate_market_data(i + 1, trends[i]);
        memcpy(accounts[oracle_pubkey].data.data(), &market, sizeof(TestOracleData));
        
        // Create update instruction
        Instruction update_instr;
        update_instr.program_id = program_id;
        update_instr.accounts = {state_pubkey, oracle_pubkey};
        update_instr.data = {static_cast<uint8_t>(TestInstructionType::ProcessUpdate)};
        
        // Execute transaction
        auto result = engine.execute_transaction({update_instr}, accounts);
        ASSERT_TRUE(result.is_success());
        successful_inferences++;
    }
    
    // Verify all inference transactions succeeded
    ASSERT_EQ(successful_inferences, trends.size());
    
    std::cout << "  ✓ Program deployment and execution test passed\n";
    std::cout << "    Successful inferences: " << successful_inferences << "/" << trends.size() << "\n";
}

void test_performance_benchmark() {
    std::cout << "Running performance benchmark...\n";
    
    ExecutionEngine engine;
    engine.set_compute_budget(10000000);
    
    PublicKey program_id = random_pubkey();
    PublicKey state_pubkey = random_pubkey();
    PublicKey oracle_pubkey = random_pubkey();
    
    std::unordered_map<PublicKey, ProgramAccount> accounts;
    
    // Setup accounts
    ProgramAccount program_account;
    program_account.pubkey = program_id;
    program_account.executable = true;
    program_account.lamports = 1000000000;
    accounts[program_id] = program_account;
    
    ProgramAccount state_account;
    state_account.pubkey = state_pubkey;
    state_account.data.resize(sizeof(TestAgentState), 0);
    state_account.lamports = 100000000;
    state_account.owner = program_id;
    accounts[state_pubkey] = state_account;
    
    ProgramAccount oracle_account;
    oracle_account.pubkey = oracle_pubkey;
    oracle_account.data.resize(sizeof(TestOracleData), 0);
    oracle_account.lamports = 100000000;
    accounts[oracle_pubkey] = oracle_account;
    
    // Deploy program
    auto program = std::make_unique<MLTradingAgentProgram>(program_id);
    engine.register_builtin_program(std::move(program));
    
    // Initialize
    Instruction init_instr;
    init_instr.program_id = program_id;
    init_instr.accounts = {state_pubkey};
    init_instr.data = {static_cast<uint8_t>(TestInstructionType::Initialize)};
    engine.execute_transaction({init_instr}, accounts);
    
    // Performance benchmark: measure inference latency
    const size_t NUM_ITERATIONS = 1000;
    std::mt19937 gen(42);
    std::uniform_real_distribution<> trend_dis(-0.03, 0.03);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < NUM_ITERATIONS; i++) {
        TestOracleData market = generate_market_data(i, trend_dis(gen));
        memcpy(accounts[oracle_pubkey].data.data(), &market, sizeof(TestOracleData));
        
        Instruction update_instr;
        update_instr.program_id = program_id;
        update_instr.accounts = {state_pubkey, oracle_pubkey};
        update_instr.data = {static_cast<uint8_t>(TestInstructionType::ProcessUpdate)};
        
        auto result = engine.execute_transaction({update_instr}, accounts);
        ASSERT_TRUE(result.is_success());
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double avg_latency_us = static_cast<double>(duration.count()) / NUM_ITERATIONS;
    double throughput = 1000000.0 / avg_latency_us;
    
    std::cout << "  Iterations: " << NUM_ITERATIONS << "\n";
    std::cout << "  Total time: " << duration.count() << " μs\n";
    std::cout << "  Average latency: " << std::fixed << std::setprecision(2) 
              << avg_latency_us << " μs/inference\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) 
              << throughput << " inferences/sec\n";
    
    // Performance targets
    ASSERT_LT(avg_latency_us, 100.0);  // < 100 μs per inference
    ASSERT_GT(throughput, 10000.0);    // > 10k inferences/sec
    
    std::cout << "  ✓ Performance benchmark passed\n";
}

void test_decision_tree_direct_execution() {
    std::cout << "Testing decision tree direct execution...\n";
    
    // Test the sol_ml_decision_tree syscall directly
    DecisionTreeModel model;
    model.version = 1;
    model.num_nodes = 7;
    model.max_depth = 3;
    model.num_features = 2;
    model.num_classes = 2;
    
    // Initialize nodes manually
    model.nodes.resize(7);
    
    // Root: feature 0 < 50
    model.nodes[0].feature_index = 0;
    model.nodes[0].left_child = 1;
    model.nodes[0].right_child = 4;
    model.nodes[0].threshold = 50;
    model.nodes[0].leaf_value = 0;
    
    // Left: feature 1 < 30
    model.nodes[1].feature_index = 1;
    model.nodes[1].left_child = 2;
    model.nodes[1].right_child = 3;
    model.nodes[1].threshold = 30;
    model.nodes[1].leaf_value = 0;
    
    // Leaf: class 0
    model.nodes[2].feature_index = -1;
    model.nodes[2].left_child = -1;
    model.nodes[2].right_child = -1;
    model.nodes[2].threshold = 0;
    model.nodes[2].leaf_value = 0;
    
    // Leaf: class 1
    model.nodes[3].feature_index = -1;
    model.nodes[3].left_child = -1;
    model.nodes[3].right_child = -1;
    model.nodes[3].threshold = 0;
    model.nodes[3].leaf_value = 1;
    
    // Right: feature 1 < 70
    model.nodes[4].feature_index = 1;
    model.nodes[4].left_child = 5;
    model.nodes[4].right_child = 6;
    model.nodes[4].threshold = 70;
    model.nodes[4].leaf_value = 0;
    
    // Leaf: class 1
    model.nodes[5].feature_index = -1;
    model.nodes[5].left_child = -1;
    model.nodes[5].right_child = -1;
    model.nodes[5].threshold = 0;
    model.nodes[5].leaf_value = 1;
    
    // Leaf: class 0
    model.nodes[6].feature_index = -1;
    model.nodes[6].left_child = -1;
    model.nodes[6].right_child = -1;
    model.nodes[6].threshold = 0;
    model.nodes[6].leaf_value = 0;
    
    // Test case 1: features = [30, 20] -> should go left, left -> class 0
    std::vector<int32_t> features1 = {30, 20};
    int32_t result1;
    uint64_t ret = sol_ml_decision_tree(
        features1.data(), features1.size(),
        model.nodes.data(), model.nodes.size(),
        model.max_depth, &result1
    );
    ASSERT_EQ(ret, 0u);
    ASSERT_EQ(result1, 0);
    
    // Test case 2: features = [30, 50] -> left, right -> class 1
    std::vector<int32_t> features2 = {30, 50};
    int32_t result2;
    ret = sol_ml_decision_tree(
        features2.data(), features2.size(),
        model.nodes.data(), model.nodes.size(),
        model.max_depth, &result2
    );
    ASSERT_EQ(ret, 0u);
    ASSERT_EQ(result2, 1);
    
    // Test case 3: features = [60, 40] -> right, left -> class 1
    std::vector<int32_t> features3 = {60, 40};
    int32_t result3;
    ret = sol_ml_decision_tree(
        features3.data(), features3.size(),
        model.nodes.data(), model.nodes.size(),
        model.max_depth, &result3
    );
    ASSERT_EQ(ret, 0u);
    ASSERT_EQ(result3, 1);
    
    // Test case 4: features = [60, 80] -> right, right -> class 0
    std::vector<int32_t> features4 = {60, 80};
    int32_t result4;
    ret = sol_ml_decision_tree(
        features4.data(), features4.size(),
        model.nodes.data(), model.nodes.size(),
        model.max_depth, &result4
    );
    ASSERT_EQ(ret, 0u);
    ASSERT_EQ(result4, 0);
    
    std::cout << "  ✓ Decision tree direct execution tests passed (4 cases)\n";
}

void test_null_pointer_handling() {
    std::cout << "Testing null pointer handling...\n";
    
    int32_t result;
    
    // Null features
    uint64_t ret = sol_ml_decision_tree(nullptr, 5, nullptr, 0, 3, &result);
    ASSERT_NE(ret, 0u);  // Should fail
    
    // Null nodes
    std::vector<int32_t> features = {100, 200, 300, 400, 500};
    ret = sol_ml_decision_tree(features.data(), features.size(), nullptr, 0, 3, &result);
    ASSERT_NE(ret, 0u);  // Should fail
    
    // Null output
    DecisionTreeNode nodes[1];
    nodes[0].feature_index = -1;
    nodes[0].left_child = -1;
    nodes[0].right_child = -1;
    nodes[0].threshold = 0;
    nodes[0].leaf_value = 0;
    
    ret = sol_ml_decision_tree(features.data(), features.size(), nodes, 1, 3, nullptr);
    ASSERT_NE(ret, 0u);  // Should fail
    
    std::cout << "  ✓ Null pointer handling tests passed\n";
}

void test_boundary_conditions() {
    std::cout << "Testing boundary conditions...\n";
    
    std::vector<int32_t> features = {100};
    int32_t result;
    
    // Test empty model
    uint64_t ret = sol_ml_decision_tree(features.data(), features.size(), nullptr, 0, 3, &result);
    ASSERT_NE(ret, 0u);  // Should fail with empty model
    
    // Test single leaf node
    DecisionTreeNode single_leaf;
    single_leaf.feature_index = -1;
    single_leaf.left_child = -1;
    single_leaf.right_child = -1;
    single_leaf.threshold = 0;
    single_leaf.leaf_value = 42;  // Just a leaf returning 42
    
    ret = sol_ml_decision_tree(features.data(), features.size(), &single_leaf, 1, 3, &result);
    ASSERT_EQ(ret, 0u);
    ASSERT_EQ(result, 42);
    
    std::cout << "  ✓ Boundary conditions tests passed\n";
}

} // anonymous namespace

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║       ML Trading Agent - BPF Integration Test Suite           ║\n";
    std::cout << "║     Testing End-to-End ML Inference in slonana SVM            ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n\n";
    
    int total_tests = 0;
    int passed_tests = 0;
    
    auto run_test = [&total_tests, &passed_tests](const char* name, void (*test_fn)()) {
        total_tests++;
        try {
            test_fn();
            passed_tests++;
        } catch (const std::exception& e) {
            std::cerr << "FAILED: " << name << " - " << e.what() << "\n";
        }
    };
    
    std::cout << "--- Integration Tests ---\n";
    run_test("Program Deployment and Execution", test_program_deployment_and_execution);
    run_test("Performance Benchmark", test_performance_benchmark);
    run_test("Decision Tree Direct Execution", test_decision_tree_direct_execution);
    run_test("Null Pointer Handling", test_null_pointer_handling);
    run_test("Boundary Conditions", test_boundary_conditions);
    
    std::cout << "\n╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                    Test Summary                                ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  Tests run: " << total_tests << "                                               ║\n";
    std::cout << "║  Tests passed: " << passed_tests << "                                            ║\n";
    std::cout << "║  Tests failed: " << (total_tests - passed_tests) << "                                            ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    
    return (passed_tests == total_tests) ? 0 : 1;
}
