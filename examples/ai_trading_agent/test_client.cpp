/**
 * Test Client for AI Trading Agent
 * 
 * This client demonstrates how to send transactions to the AI Trading Agent
 * program deployed on a slonana validator.
 * 
 * Usage:
 *   ./test_client init      - Initialize the agent
 *   ./test_client trigger   - Manually trigger inference
 *   ./test_client configure - Enable autonomous execution
 *   ./test_client pause     - Pause the agent
 *   ./test_client resume    - Resume the agent
 *   ./test_client status    - Check agent status
 * 
 * Build:
 *   g++ -std=c++17 -O2 test_client.cpp -o test_client -I../../include -L../../build -lslonana_core
 */

#include <iostream>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>

// Include slonana SVM components
#include "svm/engine.h"
#include "svm/ml_inference.h"
#include "svm/async_bpf_execution.h"
#include "common/types.h"

using namespace slonana;
using namespace slonana::svm;
using namespace slonana::common;

// ============================================================================
// Constants
// ============================================================================

// Instruction types (must match ai_trading_agent.cpp)
constexpr uint8_t INST_INITIALIZE = 0;
constexpr uint8_t INST_UPDATE_MODEL = 1;
constexpr uint8_t INST_TRIGGER = 2;
constexpr uint8_t INST_CONFIGURE = 3;
constexpr uint8_t INST_PAUSE = 4;
constexpr uint8_t INST_RESUME = 5;

// Program ID (would be generated during deployment)
const std::array<uint8_t, 32> PROGRAM_ID = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20
};

// Agent state account (PDA)
const std::array<uint8_t, 32> STATE_ACCOUNT = {
    0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8,
    0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0,
    0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8,
    0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0
};

// Oracle account
const std::array<uint8_t, 32> ORACLE_ACCOUNT = {
    0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8,
    0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0,
    0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8,
    0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0
};

// Escrow account
const std::array<uint8_t, 32> ESCROW_ACCOUNT = {
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30,
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40
};

// ============================================================================
// Helper Functions
// ============================================================================

PublicKey to_pubkey(const std::array<uint8_t, 32>& arr) {
    PublicKey pk;
    std::copy(arr.begin(), arr.end(), pk.begin());
    return pk;
}

/**
 * Create mock oracle data for testing
 */
std::vector<uint8_t> create_mock_oracle_data(
    uint64_t price,
    uint64_t price_24h,
    uint64_t volume_1h,
    int64_t momentum
) {
    std::vector<uint8_t> data(64, 0);
    
    // Pack oracle data
    memcpy(data.data() + 0, &price, sizeof(uint64_t));
    memcpy(data.data() + 8, &price_24h, sizeof(uint64_t));  // confidence placeholder
    memcpy(data.data() + 16, &price_24h, sizeof(uint64_t)); // price_24h_ago
    memcpy(data.data() + 24, &volume_1h, sizeof(uint64_t));
    
    uint64_t volume_24h_avg = volume_1h;  // Simplified
    memcpy(data.data() + 32, &volume_24h_avg, sizeof(uint64_t));
    memcpy(data.data() + 40, &momentum, sizeof(int64_t));
    
    uint64_t slot = 12345;
    memcpy(data.data() + 48, &slot, sizeof(uint64_t));
    
    return data;
}

/**
 * Create agent state account data
 */
std::vector<uint8_t> create_agent_state_data() {
    std::vector<uint8_t> data(256, 0);
    // State will be initialized by the program
    return data;
}

// ============================================================================
// Transaction Builders
// ============================================================================

/**
 * Build Initialize transaction
 */
Instruction build_initialize_instruction(uint32_t model_version) {
    Instruction inst;
    inst.program_id = to_pubkey(PROGRAM_ID);
    
    // Add accounts
    inst.accounts.push_back(to_pubkey(STATE_ACCOUNT));
    inst.accounts.push_back(to_pubkey(ORACLE_ACCOUNT));
    inst.accounts.push_back(to_pubkey(ESCROW_ACCOUNT));
    
    // Build instruction data
    inst.data.resize(8);
    inst.data[0] = INST_INITIALIZE;
    inst.data[1] = 0; // padding
    inst.data[2] = 0;
    inst.data[3] = 0;
    memcpy(inst.data.data() + 4, &model_version, sizeof(uint32_t));
    
    return inst;
}

/**
 * Build Trigger transaction
 */
Instruction build_trigger_instruction() {
    Instruction inst;
    inst.program_id = to_pubkey(PROGRAM_ID);
    
    inst.accounts.push_back(to_pubkey(STATE_ACCOUNT));
    inst.accounts.push_back(to_pubkey(ORACLE_ACCOUNT));
    
    inst.data.resize(1);
    inst.data[0] = INST_TRIGGER;
    
    return inst;
}

/**
 * Build Configure transaction
 */
Instruction build_configure_instruction(
    bool enable_timer,
    bool enable_watcher,
    uint8_t period_slots
) {
    Instruction inst;
    inst.program_id = to_pubkey(PROGRAM_ID);
    
    inst.accounts.push_back(to_pubkey(STATE_ACCOUNT));
    
    inst.data.resize(4);
    inst.data[0] = INST_CONFIGURE;
    inst.data[1] = enable_timer ? 1 : 0;
    inst.data[2] = enable_watcher ? 1 : 0;
    inst.data[3] = period_slots;
    
    return inst;
}

/**
 * Build Pause/Resume transaction
 */
Instruction build_pause_resume_instruction(bool pause) {
    Instruction inst;
    inst.program_id = to_pubkey(PROGRAM_ID);
    
    inst.accounts.push_back(to_pubkey(STATE_ACCOUNT));
    
    inst.data.resize(1);
    inst.data[0] = pause ? INST_PAUSE : INST_RESUME;
    
    return inst;
}

// ============================================================================
// Command Handlers
// ============================================================================

void cmd_initialize() {
    std::cout << "\n╔════════════════════════════════════════════╗\n";
    std::cout << "║  Initializing AI Trading Agent              ║\n";
    std::cout << "╚════════════════════════════════════════════╝\n\n";
    
    // Create execution context
    ExecutionContext ctx;
    ctx.compute_budget = 100000;
    ctx.max_compute_units = 200000;
    
    // Create accounts
    ProgramAccount state_account;
    state_account.pubkey = to_pubkey(STATE_ACCOUNT);
    state_account.program_id = to_pubkey(PROGRAM_ID);
    state_account.data = create_agent_state_data();
    state_account.lamports = 1000000;
    state_account.executable = false;
    
    ProgramAccount oracle_account;
    oracle_account.pubkey = to_pubkey(ORACLE_ACCOUNT);
    oracle_account.data = create_mock_oracle_data(
        100000000,  // price: $100.00 (in micro-units)
        100000000,  // price_24h: $100.00
        50000,      // volume_1h: 50K
        100         // momentum: slightly positive
    );
    oracle_account.lamports = 1000000;
    oracle_account.executable = false;
    
    ctx.accounts[to_pubkey(STATE_ACCOUNT)] = state_account;
    ctx.accounts[to_pubkey(ORACLE_ACCOUNT)] = oracle_account;
    
    // Build and add instruction
    ctx.instructions.push_back(build_initialize_instruction(1));
    
    std::cout << "📦 State Account: ";
    for (int i = 0; i < 8; i++) std::cout << std::hex << (int)STATE_ACCOUNT[i];
    std::cout << "...\n";
    
    std::cout << "📊 Oracle Account: ";
    for (int i = 0; i < 8; i++) std::cout << std::hex << (int)ORACLE_ACCOUNT[i];
    std::cout << "...\n";
    
    std::cout << "🤖 Model Version: 1\n";
    
    // Note: In production, would send transaction to validator
    std::cout << "\n✅ Transaction built successfully!\n";
    std::cout << "   Would send to validator RPC endpoint\n";
}

void cmd_trigger() {
    std::cout << "\n╔════════════════════════════════════════════╗\n";
    std::cout << "║  Triggering ML Inference                    ║\n";
    std::cout << "╚════════════════════════════════════════════╝\n\n";
    
    // Simulate different market conditions
    struct TestCase {
        const char* name;
        uint64_t price;
        uint64_t price_24h;
        int64_t momentum;
        const char* expected;
    };
    
    TestCase cases[] = {
        {"Bullish", 110000000, 100000000, 500, "BUY"},
        {"Bearish", 90000000, 100000000, -600, "SELL"},
        {"Neutral", 100000000, 100000000, 100, "HOLD"},
        {"Strong Bear", 85000000, 100000000, -1000, "SELL"},
    };
    
    std::cout << "Running inference with different market conditions:\n\n";
    
    for (const auto& tc : cases) {
        std::cout << "  Market: " << tc.name << "\n";
        std::cout << "    Price:    " << tc.price / 1000000 << " (24h: " << tc.price_24h / 1000000 << ")\n";
        std::cout << "    Momentum: " << tc.momentum << "\n";
        
        // Create mock oracle data
        auto oracle_data = create_mock_oracle_data(
            tc.price, tc.price_24h, 50000, tc.momentum
        );
        
        // Calculate features
        int32_t features[3];
        features[0] = static_cast<int32_t>(
            (tc.price * 10000) / tc.price_24h  // Normalized price
        );
        features[1] = 10000;  // Volume ratio = 1.0
        features[2] = static_cast<int32_t>(tc.momentum);
        
        std::cout << "    Features: [" << features[0] << ", " << features[1] << ", " << features[2] << "]\n";
        
        // Run actual inference using slonana ML syscall
        DecisionTreeModel model;
        model.max_depth = 10;
        model.nodes = {
            {0, 1, 2, 9500, 0},     // root
            {2, 3, 4, -500, 0},     // momentum check
            {0, 5, 6, 10500, 0},    // price high check
            {-1, -1, -1, 0, -1},    // SELL
            {-1, -1, -1, 0, 0},     // HOLD
            {-1, -1, -1, 0, 0},     // HOLD
            {-1, -1, -1, 0, 1},     // BUY
        };
        
        int32_t signal;
        uint64_t result = sol_ml_decision_tree(
            features, 3,
            model.nodes.data(), model.nodes.size(),
            model.max_depth, &signal
        );
        
        const char* signal_str = signal == 1 ? "BUY" : (signal == -1 ? "SELL" : "HOLD");
        std::cout << "    Signal:   " << signal_str;
        if (strcmp(signal_str, tc.expected) == 0) {
            std::cout << " ✓\n";
        } else {
            std::cout << " (expected " << tc.expected << ")\n";
        }
        std::cout << "\n";
    }
    
    std::cout << "✅ Inference tests complete!\n";
}

void cmd_configure() {
    std::cout << "\n╔════════════════════════════════════════════╗\n";
    std::cout << "║  Configuring Autonomous Execution           ║\n";
    std::cout << "╚════════════════════════════════════════════╝\n\n";
    
    auto inst = build_configure_instruction(
        true,   // enable_timer
        true,   // enable_watcher
        1       // period: every slot
    );
    
    std::cout << "Configuration:\n";
    std::cout << "  ⏱️  Timer: ENABLED (every 1 slot)\n";
    std::cout << "  👁️  Watcher: ENABLED (oracle changes)\n";
    std::cout << "\n✅ Configuration transaction built!\n";
}

void cmd_pause() {
    std::cout << "\n╔════════════════════════════════════════════╗\n";
    std::cout << "║  Pausing Agent                              ║\n";
    std::cout << "╚════════════════════════════════════════════╝\n\n";
    
    auto inst = build_pause_resume_instruction(true);
    std::cout << "✅ Pause transaction built!\n";
}

void cmd_resume() {
    std::cout << "\n╔════════════════════════════════════════════╗\n";
    std::cout << "║  Resuming Agent                             ║\n";
    std::cout << "╚════════════════════════════════════════════╝\n\n";
    
    auto inst = build_pause_resume_instruction(false);
    std::cout << "✅ Resume transaction built!\n";
}

void cmd_status() {
    std::cout << "\n╔════════════════════════════════════════════╗\n";
    std::cout << "║  Agent Status                               ║\n";
    std::cout << "╚════════════════════════════════════════════╝\n\n";
    
    // In production, would fetch from validator
    std::cout << "Status: Active\n";
    std::cout << "Model Version: 1\n";
    std::cout << "Position: NEUTRAL\n";
    std::cout << "Total Trades: 0\n";
    std::cout << "Timer: Active (ID: 12345)\n";
    std::cout << "Watcher: Active (ID: 67890)\n";
    std::cout << "\n✅ Status retrieved!\n";
}

void cmd_benchmark() {
    std::cout << "\n╔════════════════════════════════════════════╗\n";
    std::cout << "║  Performance Benchmark                      ║\n";
    std::cout << "╚════════════════════════════════════════════╝\n\n";
    
    // Create test model
    DecisionTreeModel model;
    model.max_depth = 10;
    model.nodes = {
        {0, 1, 2, 9500, 0},
        {2, 3, 4, -500, 0},
        {0, 5, 6, 10500, 0},
        {-1, -1, -1, 0, -1},
        {-1, -1, -1, 0, 0},
        {-1, -1, -1, 0, 0},
        {-1, -1, -1, 0, 1},
    };
    
    int32_t features[3] = {9000, 10000, -200};
    int32_t signal;
    
    // Warmup
    for (int i = 0; i < 1000; i++) {
        sol_ml_decision_tree(
            features, 3,
            model.nodes.data(), model.nodes.size(),
            model.max_depth, &signal
        );
    }
    
    // Benchmark
    constexpr int ITERATIONS = 100000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < ITERATIONS; i++) {
        features[0] = 8000 + (i % 4000);  // Vary input
        sol_ml_decision_tree(
            features, 3,
            model.nodes.data(), model.nodes.size(),
            model.max_depth, &signal
        );
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    double ns_per_inference = static_cast<double>(duration.count()) / ITERATIONS;
    
    std::cout << "Decision Tree Inference Benchmark\n";
    std::cout << "─────────────────────────────────\n";
    std::cout << "  Iterations: " << ITERATIONS << "\n";
    std::cout << "  Total Time: " << duration.count() / 1000000.0 << " ms\n";
    std::cout << "  Per Inference: " << ns_per_inference << " ns\n";
    std::cout << "  Throughput: " << 1000000000.0 / ns_per_inference << " inferences/sec\n";
    std::cout << "\n✅ Benchmark complete!\n";
}

void print_usage() {
    std::cout << "\n╔════════════════════════════════════════════╗\n";
    std::cout << "║  AI Trading Agent Test Client               ║\n";
    std::cout << "╚════════════════════════════════════════════╝\n\n";
    
    std::cout << "Usage: test_client <command>\n\n";
    std::cout << "Commands:\n";
    std::cout << "  init       Initialize the agent\n";
    std::cout << "  trigger    Manually trigger inference\n";
    std::cout << "  configure  Enable autonomous execution\n";
    std::cout << "  pause      Pause the agent\n";
    std::cout << "  resume     Resume the agent\n";
    std::cout << "  status     Check agent status\n";
    std::cout << "  benchmark  Run performance benchmark\n";
    std::cout << "\n";
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    
    std::string cmd = argv[1];
    
    if (cmd == "init") {
        cmd_initialize();
    } else if (cmd == "trigger") {
        cmd_trigger();
    } else if (cmd == "configure") {
        cmd_configure();
    } else if (cmd == "pause") {
        cmd_pause();
    } else if (cmd == "resume") {
        cmd_resume();
    } else if (cmd == "status") {
        cmd_status();
    } else if (cmd == "benchmark") {
        cmd_benchmark();
    } else {
        std::cout << "Unknown command: " << cmd << "\n";
        print_usage();
        return 1;
    }
    
    return 0;
}
