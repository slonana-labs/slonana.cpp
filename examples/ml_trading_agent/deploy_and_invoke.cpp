/**
 * Deploy and Invoke ML Trading Agent
 * 
 * This client demonstrates how to:
 * 1. Create accounts for the program and state
 * 2. Deploy the sBPF program to slonana validator
 * 3. Initialize the trading agent
 * 4. Send transactions to invoke the agent
 * 5. Read and display trading signals
 * 
 * This is a complete end-to-end example of using ML inference on-chain.
 */

#include <iostream>
#include <vector>
#include <memory>
#include <chrono>
#include <random>
#include <iomanip>

// slonana includes
#include "svm/engine.h"
#include "svm/ml_inference.h"
#include "common/types.h"

using namespace slonana::svm;
using namespace slonana::common;

// ============================================================================
// Data Structures (matching the sBPF program)
// ============================================================================

struct OracleData {
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

struct AgentState {
    uint64_t version;
    int32_t current_signal;
    int32_t confidence;
    uint64_t last_update_slot;
    uint64_t total_inferences;
    uint64_t buy_count;
    uint64_t hold_count;
    uint64_t sell_count;
};

enum class InstructionType : uint8_t {
    Initialize = 0,
    ProcessUpdate = 1,
    GetSignal = 2,
};

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Generate a random public key for testing
 */
PublicKey random_pubkey() {
    PublicKey key;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);
    
    for (size_t i = 0; i < sizeof(key.data); i++) {
        key.data[i] = dis(gen);
    }
    return key;
}

/**
 * Create a program account with executable sBPF code
 */
ProgramAccount create_program_account(const PublicKey& program_id, 
                                       const std::vector<uint8_t>& program_data) {
    ProgramAccount account;
    account.pubkey = program_id;
    account.program_id = program_id;  // Self-owned for program accounts
    account.data = program_data;
    account.lamports = 1000000000;  // 1 SOL for rent
    account.owner = program_id;      // BPF loader in real impl
    account.executable = true;
    account.rent_epoch = 0;
    return account;
}

/**
 * Create a data account owned by a program
 */
ProgramAccount create_data_account(const PublicKey& pubkey,
                                    const PublicKey& owner,
                                    size_t data_size) {
    ProgramAccount account;
    account.pubkey = pubkey;
    account.program_id = owner;
    account.data.resize(data_size, 0);
    account.lamports = 100000000;  // 0.1 SOL
    account.owner = owner;
    account.executable = false;
    account.rent_epoch = 0;
    return account;
}

/**
 * Generate simulated market data
 */
OracleData generate_market_data(uint64_t slot, double trend_strength) {
    static uint64_t base_price = 100000000;  // $100.00
    static std::mt19937 gen(42);
    std::normal_distribution<> price_noise(0, 0.01);
    std::normal_distribution<> volume_noise(0, 0.2);
    
    // Apply trend with noise
    double price_change = trend_strength + price_noise(gen);
    base_price = static_cast<uint64_t>(base_price * (1.0 + price_change));
    
    OracleData data;
    data.version = 1;
    data.timestamp = slot * 400;  // ~400ms per slot
    data.current_price = base_price;
    data.avg_price_24h = 100000000;  // Reference price
    data.volume_24h = static_cast<uint64_t>(1000000 * (1.0 + volume_noise(gen)));
    data.avg_volume = 1000000;
    data.high_24h = static_cast<uint64_t>(base_price * 1.05);
    data.low_24h = static_cast<uint64_t>(base_price * 0.95);
    data.price_change_1h = static_cast<int64_t>(price_change * 10000);  // In basis points
    data.bid_ask_spread = 30 + (gen() % 50);  // 30-80 bp spread
    
    return data;
}

/**
 * Serialize oracle data to bytes
 */
std::vector<uint8_t> serialize_oracle_data(const OracleData& data) {
    std::vector<uint8_t> bytes(sizeof(OracleData));
    memcpy(bytes.data(), &data, sizeof(OracleData));
    return bytes;
}

/**
 * Parse agent state from account data
 */
AgentState parse_agent_state(const std::vector<uint8_t>& data) {
    AgentState state;
    if (data.size() >= sizeof(AgentState)) {
        memcpy(&state, data.data(), sizeof(AgentState));
    }
    return state;
}

/**
 * Get signal name from value
 */
const char* signal_name(int32_t signal) {
    switch (signal) {
        case 1:  return "BUY";
        case 0:  return "HOLD";
        case -1: return "SELL";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// Mock sBPF Program Execution
// ============================================================================

/**
 * Since we can't actually compile to sBPF in this example, we simulate
 * the program execution using the same logic as ml_trading_agent.cpp.
 * 
 * In production:
 * 1. Compile ml_trading_agent.cpp with clang -target bpf
 * 2. Deploy the resulting .so file
 * 3. The slonana BPF runtime executes it
 */
class MockTradingAgentProgram : public BuiltinProgram {
public:
    explicit MockTradingAgentProgram(const PublicKey& id) : program_id_(id) {
        // Initialize the decision tree model (same as in ml_trading_agent.cpp)
        model_.version = 1;
        model_.num_nodes = 13;
        model_.max_depth = 5;
        model_.num_features = 5;
        model_.num_classes = 3;
        
        model_.nodes = {
            // Node 0 (root): Check price ratio
            {0, 1, 6, 0, 9800, 0},
            // Node 1: Price is low, check momentum
            {2, 2, 3, 0, -500, 0},
            // Node 2: Leaf - Strong SELL signal
            {-1, -1, -1, 0, 0, -1},
            // Node 3: Check volume for potential reversal
            {1, 4, 5, 0, 15000, 0},
            // Node 4: Low volume - HOLD
            {-1, -1, -1, 0, 0, 0},
            // Node 5: High volume reversal - BUY
            {-1, -1, -1, 0, 0, 1},
            // Node 6: Price is normal/high, check momentum
            {2, 7, 10, 0, 500, 0},
            // Node 7: Negative momentum, check spread
            {3, 8, 9, 0, 300, 0},
            // Node 8: Wide spread - HOLD
            {-1, -1, -1, 0, 0, 0},
            // Node 9: Tight spread - potential SELL
            {-1, -1, -1, 0, 0, -1},
            // Node 10: Positive momentum, check volatility
            {4, 11, 12, 0, 5000, 0},
            // Node 11: Low volatility - strong BUY
            {-1, -1, -1, 0, 0, 1},
            // Node 12: High volatility - HOLD
            {-1, -1, -1, 0, 0, 0},
        };
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
        
        auto inst_type = static_cast<InstructionType>(instruction.data[0]);
        
        switch (inst_type) {
            case InstructionType::Initialize:
                return handle_initialize(instruction, context);
            case InstructionType::ProcessUpdate:
                return handle_process_update(instruction, context);
            case InstructionType::GetSignal:
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
        
        // Initialize state account
        auto it = context.accounts.find(instruction.accounts[0]);
        if (it != context.accounts.end()) {
            AgentState state = {};
            state.version = 1;
            state.current_signal = 0;
            
            it->second.data.resize(sizeof(AgentState));
            memcpy(it->second.data.data(), &state, sizeof(AgentState));
            
            context.modified_accounts.insert(instruction.accounts[0]);
        }
        
        outcome.logs = "Agent initialized successfully";
        return outcome;
    }
    
    ExecutionOutcome handle_process_update(const Instruction& instruction,
                                           ExecutionContext& context) const {
        ExecutionOutcome outcome;
        outcome.result = ExecutionResult::SUCCESS;
        outcome.compute_units_consumed = 500;
        
        if (instruction.accounts.size() < 2) {
            outcome.result = ExecutionResult::INVALID_INSTRUCTION;
            outcome.error_details = "Missing accounts (need state and oracle)";
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
        if (oracle_it->second.data.size() < sizeof(OracleData)) {
            outcome.result = ExecutionResult::INVALID_INSTRUCTION;
            outcome.error_details = "Oracle data too small";
            return outcome;
        }
        
        OracleData oracle;
        memcpy(&oracle, oracle_it->second.data.data(), sizeof(OracleData));
        
        // Extract features
        std::vector<int32_t> features(5);
        extract_features(oracle, features.data());
        
        // Run ML inference
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
        AgentState state;
        if (state_it->second.data.size() >= sizeof(AgentState)) {
            memcpy(&state, state_it->second.data.data(), sizeof(AgentState));
        }
        
        state.current_signal = signal;
        state.total_inferences++;
        
        switch (signal) {
            case 1:  state.buy_count++; break;
            case 0:  state.hold_count++; break;
            case -1: state.sell_count++; break;
        }
        
        state_it->second.data.resize(sizeof(AgentState));
        memcpy(state_it->second.data.data(), &state, sizeof(AgentState));
        
        context.modified_accounts.insert(instruction.accounts[0]);
        outcome.logs = std::string("Signal: ") + signal_name(signal);
        
        return outcome;
    }
    
    ExecutionOutcome handle_get_signal(const Instruction& instruction,
                                       ExecutionContext& context) const {
        ExecutionOutcome outcome;
        outcome.result = ExecutionResult::SUCCESS;
        outcome.compute_units_consumed = 10;
        outcome.logs = "Signal read (state is public)";
        return outcome;
    }
    
    void extract_features(const OracleData& oracle, int32_t* features) const {
        constexpr int32_t SCALE = 10000;
        
        // Feature 0: Normalized price
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
        
        // Feature 2: Momentum
        features[2] = static_cast<int32_t>(oracle.price_change_1h);
        
        // Feature 3: Spread
        features[3] = static_cast<int32_t>(oracle.bid_ask_spread);
        
        // Feature 4: Volatility
        uint64_t range = oracle.high_24h - oracle.low_24h;
        if (oracle.avg_price_24h > 0) {
            features[4] = static_cast<int32_t>((range * SCALE) / oracle.avg_price_24h);
        } else {
            features[4] = 0;
        }
    }
};

// ============================================================================
// Main Demo
// ============================================================================

int main() {
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║     ML Trading Agent - Deploy and Invoke Demo                  ║\n";
    std::cout << "║     Demonstrating On-Chain ML Inference with slonana SVM      ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n\n";
    
    // Create execution engine
    ExecutionEngine engine;
    engine.set_compute_budget(1000000);  // 1M compute units
    
    // ========================================================================
    // Step 1: Create accounts
    // ========================================================================
    std::cout << "1. Creating accounts...\n";
    
    PublicKey program_id = random_pubkey();
    PublicKey state_pubkey = random_pubkey();
    PublicKey oracle_pubkey = random_pubkey();
    
    // In real deployment, program data would be the compiled sBPF .so file
    std::vector<uint8_t> program_data(1024, 0);  // Placeholder
    
    auto program_account = create_program_account(program_id, program_data);
    auto state_account = create_data_account(state_pubkey, program_id, sizeof(AgentState));
    auto oracle_account = create_data_account(oracle_pubkey, program_id, sizeof(OracleData));
    
    std::cout << "   ✓ Program account created\n";
    std::cout << "   ✓ State account created\n";
    std::cout << "   ✓ Oracle account created (mock)\n\n";
    
    // ========================================================================
    // Step 2: Register the mock program
    // ========================================================================
    std::cout << "2. Deploying sBPF program...\n";
    
    auto mock_program = std::make_unique<MockTradingAgentProgram>(program_id);
    engine.register_builtin_program(std::move(mock_program));
    
    std::cout << "   ✓ Program deployed successfully\n";
    std::cout << "   Program ID: [" << std::hex;
    for (int i = 0; i < 4; i++) std::cout << static_cast<int>(program_id.data[i]);
    std::cout << "...]\n" << std::dec << "\n";
    
    // ========================================================================
    // Step 3: Initialize agent
    // ========================================================================
    std::cout << "3. Initializing agent state...\n";
    
    std::unordered_map<PublicKey, ProgramAccount> accounts;
    accounts[program_id] = program_account;
    accounts[state_pubkey] = state_account;
    accounts[oracle_pubkey] = oracle_account;
    
    // Create initialize instruction
    Instruction init_instr;
    init_instr.program_id = program_id;
    init_instr.accounts = {state_pubkey};
    init_instr.data = {static_cast<uint8_t>(InstructionType::Initialize)};
    
    auto init_result = engine.execute_transaction({init_instr}, accounts);
    
    if (init_result.is_success()) {
        std::cout << "   ✓ Agent initialized\n";
        std::cout << "   Logs: " << init_result.logs << "\n\n";
    } else {
        std::cout << "   ✗ Initialization failed: " << init_result.error_details << "\n";
        return 1;
    }
    
    // ========================================================================
    // Step 4: Simulate market updates
    // ========================================================================
    std::cout << "4. Simulating market updates...\n";
    std::cout << "   Running " << 20 << " trading cycles...\n\n";
    
    std::cout << "   ┌────────┬──────────────┬────────────┬──────────┐\n";
    std::cout << "   │  Slot  │    Price     │  Change    │  Signal  │\n";
    std::cout << "   ├────────┼──────────────┼────────────┼──────────┤\n";
    
    // Market simulation with different trends
    std::vector<double> trends = {
        0.02, 0.01, 0.015, -0.005, -0.02,  // Bullish start, then correction
        -0.01, 0.0, 0.005, 0.01, 0.02,     // Recovery
        0.015, 0.01, -0.01, -0.015, -0.02, // Bull trap
        -0.01, 0.0, 0.01, 0.015, 0.02      // Final rally
    };
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (uint64_t slot = 1; slot <= 20; slot++) {
        // Generate market data
        double trend = trends[slot - 1];
        OracleData market_data = generate_market_data(slot, trend);
        
        // Update oracle account
        auto oracle_bytes = serialize_oracle_data(market_data);
        accounts[oracle_pubkey].data = oracle_bytes;
        
        // Create process update instruction
        Instruction update_instr;
        update_instr.program_id = program_id;
        update_instr.accounts = {state_pubkey, oracle_pubkey};
        update_instr.data = {static_cast<uint8_t>(InstructionType::ProcessUpdate)};
        
        // Execute transaction
        auto result = engine.execute_transaction({update_instr}, accounts);
        
        // Read state
        AgentState state = parse_agent_state(accounts[state_pubkey].data);
        
        // Display result
        double price = market_data.current_price / 1000000.0;
        double change = market_data.price_change_1h / 100.0;
        
        std::cout << "   │ " << std::setw(6) << slot << " │ "
                  << std::fixed << std::setprecision(2)
                  << "$" << std::setw(10) << price << " │ "
                  << std::setw(8) << std::showpos << change << "% │ "
                  << std::noshowpos
                  << std::setw(8) << signal_name(state.current_signal) << " │\n";
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    std::cout << "   └────────┴──────────────┴────────────┴──────────┘\n\n";
    
    // ========================================================================
    // Step 5: Display performance metrics
    // ========================================================================
    std::cout << "5. Performance metrics:\n";
    
    AgentState final_state = parse_agent_state(accounts[state_pubkey].data);
    
    std::cout << "   Total inferences: " << final_state.total_inferences << "\n";
    std::cout << "   Total time: " << duration.count() << " μs\n";
    std::cout << "   Average latency: " << std::fixed << std::setprecision(1) 
              << (double)duration.count() / final_state.total_inferences << " μs/inference\n";
    std::cout << "   Signal distribution:\n";
    std::cout << "     - BUY:  " << final_state.buy_count 
              << " (" << (100.0 * final_state.buy_count / final_state.total_inferences) << "%)\n";
    std::cout << "     - HOLD: " << final_state.hold_count 
              << " (" << (100.0 * final_state.hold_count / final_state.total_inferences) << "%)\n";
    std::cout << "     - SELL: " << final_state.sell_count 
              << " (" << (100.0 * final_state.sell_count / final_state.total_inferences) << "%)\n";
    
    // ========================================================================
    // Summary
    // ========================================================================
    std::cout << "\n╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                        Summary                                 ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ This demo showed:                                             ║\n";
    std::cout << "║                                                               ║\n";
    std::cout << "║ 1. Account Creation                                           ║\n";
    std::cout << "║    - Program account (executable sBPF code)                   ║\n";
    std::cout << "║    - State account (agent data)                               ║\n";
    std::cout << "║    - Oracle account (market data)                             ║\n";
    std::cout << "║                                                               ║\n";
    std::cout << "║ 2. Program Deployment                                         ║\n";
    std::cout << "║    - Register sBPF program with SVM                           ║\n";
    std::cout << "║                                                               ║\n";
    std::cout << "║ 3. Transaction Invocation                                     ║\n";
    std::cout << "║    - Initialize instruction                                   ║\n";
    std::cout << "║    - ProcessUpdate instruction (triggers ML inference)        ║\n";
    std::cout << "║                                                               ║\n";
    std::cout << "║ 4. ML Inference                                               ║\n";
    std::cout << "║    - Feature extraction from oracle data                      ║\n";
    std::cout << "║    - Decision tree traversal (sol_ml_decision_tree)          ║\n";
    std::cout << "║    - Signal generation (BUY/HOLD/SELL)                        ║\n";
    std::cout << "║                                                               ║\n";
    std::cout << "║ In production:                                                ║\n";
    std::cout << "║ - Compile ml_trading_agent.cpp with clang -target bpf        ║\n";
    std::cout << "║ - Deploy the .so file using slonana deploy program            ║\n";
    std::cout << "║ - Send real transactions via RPC                             ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    
    return 0;
}
