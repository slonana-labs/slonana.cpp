/**
 * ML Trading Agent - sBPF Program
 * 
 * This program demonstrates on-chain ML inference using the slonana SVM.
 * It reads market data from oracle accounts, runs decision tree inference,
 * and updates a state account with trading signals.
 * 
 * Compilation:
 *   This file is compiled to sBPF bytecode using clang with BPF target.
 *   The resulting .so file can be deployed to the slonana validator.
 * 
 * Usage:
 *   1. Deploy this program to get a program_id
 *   2. Create a state account owned by this program
 *   3. Send transactions with instruction data to invoke the program
 */

#include <cstdint>
#include <cstring>

// ============================================================================
// sBPF Program ABI Definitions
// ============================================================================

/**
 * Account metadata passed to the program
 */
struct SolAccountMeta {
    uint8_t* pubkey;           // 32-byte public key
    uint8_t* data;             // Account data
    uint64_t data_len;         // Length of account data
    uint64_t lamports;         // Account balance
    uint8_t* owner;            // 32-byte owner public key
    bool is_signer;            // True if account signed the transaction
    bool is_writable;          // True if account data can be modified
    bool executable;           // True if account is executable
};

/**
 * Parameters passed to the program entrypoint
 */
struct SolParameters {
    SolAccountMeta* accounts;  // Array of account metadata
    uint64_t num_accounts;     // Number of accounts
    uint8_t* instruction_data; // Instruction data
    uint64_t instruction_len;  // Length of instruction data
    uint8_t* program_id;       // 32-byte program ID
};

// ============================================================================
// ML Inference Syscalls (provided by slonana runtime)
// ============================================================================

/**
 * Decision tree node structure (matches slonana::svm::DecisionTreeNode)
 */
struct DecisionTreeNode {
    int16_t feature_index;     // Which feature to compare (-1 = leaf)
    int16_t left_child;        // Index of left child (-1 = none)
    int16_t right_child;       // Index of right child (-1 = none)
    int16_t padding;
    int32_t threshold;         // Split threshold (fixed-point)
    int32_t leaf_value;        // Classification result (if leaf)
};

// Syscall declarations (implemented in slonana runtime)
extern "C" {
    /**
     * Decision tree inference syscall
     * Returns: 0 on success, error code otherwise
     */
    uint64_t sol_ml_decision_tree(
        const int32_t* features,
        uint64_t features_len,
        const DecisionTreeNode* nodes,
        uint64_t num_nodes,
        uint32_t max_depth,
        int32_t* result
    );
    
    /**
     * Matrix multiplication syscall
     */
    uint64_t sol_ml_matmul(
        const int32_t* input,
        uint64_t input_len,
        const int16_t* weights,
        uint64_t weights_len,
        const int32_t* bias,
        uint64_t bias_len,
        int32_t* output,
        uint64_t* output_len,
        uint32_t rows,
        uint32_t cols
    );
    
    /**
     * Activation function syscall
     */
    uint64_t sol_ml_activation(
        int32_t* data,
        uint64_t len,
        uint8_t activation
    );
    
    /**
     * Logging syscall for debugging
     */
    void sol_log(const char* message);
    void sol_log_64(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);
}

// ============================================================================
// Fixed-Point Arithmetic Helpers
// ============================================================================

namespace fixed_point {
    constexpr int32_t SCALE = 10000;  // 4 decimal places
    
    /**
     * Convert ratio of two uint64 values to fixed-point
     * Result is (numerator / denominator) * SCALE
     */
    inline int32_t ratio(uint64_t numerator, uint64_t denominator) {
        if (denominator == 0) return SCALE;
        // Compute ratio as fixed-point
        return static_cast<int32_t>((numerator * SCALE) / denominator);
    }
    
    inline int32_t multiply(int32_t a, int32_t b) {
        int64_t result = (static_cast<int64_t>(a) * static_cast<int64_t>(b)) / SCALE;
        if (result > INT32_MAX) return INT32_MAX;
        if (result < INT32_MIN) return INT32_MIN;
        return static_cast<int32_t>(result);
    }
}

// ============================================================================
// Oracle Data Structure
// ============================================================================

/**
 * Oracle account data format
 * This matches the format written by oracle programs
 */
struct OracleData {
    uint64_t version;          // Data version
    uint64_t timestamp;        // Last update timestamp
    uint64_t current_price;    // Current price (raw)
    uint64_t avg_price_24h;    // 24-hour average price
    uint64_t volume_24h;       // 24-hour volume
    uint64_t avg_volume;       // Average volume
    uint64_t high_24h;         // 24-hour high
    uint64_t low_24h;          // 24-hour low
    int64_t price_change_1h;   // 1-hour price change (basis points)
    uint64_t bid_ask_spread;   // Bid-ask spread (basis points)
};

// ============================================================================
// Trading Agent State
// ============================================================================

/**
 * Agent state stored in a program-owned account
 */
struct AgentState {
    uint64_t version;          // State version
    int32_t current_signal;    // Current trading signal: -1=SELL, 0=HOLD, 1=BUY
    int32_t confidence;        // Confidence level (fixed-point)
    uint64_t last_update_slot; // Slot of last update
    uint64_t total_inferences; // Total inference count
    uint64_t buy_count;        // Number of BUY signals
    uint64_t hold_count;       // Number of HOLD signals
    uint64_t sell_count;       // Number of SELL signals
};

// ============================================================================
// Instruction Types
// ============================================================================

enum class InstructionType : uint8_t {
    Initialize = 0,    // Initialize agent state
    ProcessUpdate = 1, // Process oracle update and generate signal
    GetSignal = 2,     // Read current signal (no-op, state is public)
};

// ============================================================================
// Decision Tree Model
// ============================================================================

/**
 * Embedded decision tree for trading signals
 * 
 * Tree structure (5 features → signal):
 *   Feature 0: Normalized price (current/average)
 *   Feature 1: Volume ratio (current/average)
 *   Feature 2: Price momentum (1h change)
 *   Feature 3: Spread indicator
 *   Feature 4: Volatility (high-low range)
 * 
 * Output:
 *   -1 = SELL
 *    0 = HOLD
 *    1 = BUY
 */
constexpr DecisionTreeNode TRADING_MODEL[] = {
    // Node 0 (root): Check price ratio
    // If price < 0.98 (bearish), go left; else right
    {0, 1, 6, 0, 9800, 0},
    
    // Node 1: Price is low, check momentum
    // If momentum < -50bp, go left (SELL); else check volume
    {2, 2, 3, 0, -500, 0},
    
    // Node 2: Leaf - Strong SELL signal
    {-1, -1, -1, 0, 0, -1},
    
    // Node 3: Check volume for potential reversal
    // If volume > 1.5x average, might be reversal
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
    
    // Node 12: High volatility - HOLD (too risky)
    {-1, -1, -1, 0, 0, 0},
};

constexpr uint32_t MODEL_NODE_COUNT = sizeof(TRADING_MODEL) / sizeof(DecisionTreeNode);
constexpr uint32_t MODEL_MAX_DEPTH = 5;

// ============================================================================
// Feature Extraction
// ============================================================================

/**
 * Extract features from oracle data
 * All features are in fixed-point format (scale 10000)
 */
void extract_features(const OracleData* oracle, int32_t* features) {
    // Feature 0: Normalized price (current / 24h average)
    // Result is around 10000 for stable price
    features[0] = fixed_point::ratio(oracle->current_price, oracle->avg_price_24h);
    
    // Feature 1: Volume ratio (current / average)
    features[1] = fixed_point::ratio(oracle->volume_24h, oracle->avg_volume);
    
    // Feature 2: Price momentum (1h change in basis points)
    // Convert to fixed-point (already in bp, scale to match)
    features[2] = static_cast<int32_t>(oracle->price_change_1h);
    
    // Feature 3: Bid-ask spread in basis points
    features[3] = static_cast<int32_t>(oracle->bid_ask_spread);
    
    // Feature 4: Volatility (24h range / average price)
    uint64_t range = oracle->high_24h - oracle->low_24h;
    features[4] = fixed_point::ratio(range, oracle->avg_price_24h);
}

// ============================================================================
// Instruction Handlers
// ============================================================================

/**
 * Initialize agent state
 */
uint64_t handle_initialize(SolParameters* params) {
    // Verify we have the state account
    if (params->num_accounts < 1) {
        sol_log("Error: Missing state account");
        return 1;
    }
    
    SolAccountMeta* state_account = &params->accounts[0];
    
    // Verify account is writable
    if (!state_account->is_writable) {
        sol_log("Error: State account not writable");
        return 2;
    }
    
    // Verify account is owned by this program
    if (memcmp(state_account->owner, params->program_id, 32) != 0) {
        sol_log("Error: State account not owned by program");
        return 3;
    }
    
    // Verify account has enough space
    if (state_account->data_len < sizeof(AgentState)) {
        sol_log("Error: State account too small");
        return 4;
    }
    
    // Initialize state
    AgentState* state = reinterpret_cast<AgentState*>(state_account->data);
    state->version = 1;
    state->current_signal = 0;  // HOLD
    state->confidence = 0;
    state->last_update_slot = 0;
    state->total_inferences = 0;
    state->buy_count = 0;
    state->hold_count = 0;
    state->sell_count = 0;
    
    sol_log("Agent initialized");
    return 0;
}

/**
 * Process oracle update and generate trading signal
 */
uint64_t handle_process_update(SolParameters* params, uint64_t current_slot) {
    // Verify accounts: [state, oracle]
    if (params->num_accounts < 2) {
        sol_log("Error: Missing accounts");
        return 1;
    }
    
    SolAccountMeta* state_account = &params->accounts[0];
    SolAccountMeta* oracle_account = &params->accounts[1];
    
    // Verify state account
    if (!state_account->is_writable) {
        sol_log("Error: State account not writable");
        return 2;
    }
    
    if (state_account->data_len < sizeof(AgentState)) {
        sol_log("Error: State account too small");
        return 3;
    }
    
    // Verify oracle account
    if (oracle_account->data_len < sizeof(OracleData)) {
        sol_log("Error: Oracle account too small");
        return 4;
    }
    
    // Parse accounts
    AgentState* state = reinterpret_cast<AgentState*>(state_account->data);
    const OracleData* oracle = reinterpret_cast<const OracleData*>(oracle_account->data);
    
    // Extract features from oracle data
    int32_t features[5];
    extract_features(oracle, features);
    
    // Log features for debugging
    sol_log_64(features[0], features[1], features[2], features[3], features[4]);
    
    // Run decision tree inference
    int32_t signal;
    uint64_t result = sol_ml_decision_tree(
        features,
        5,
        TRADING_MODEL,
        MODEL_NODE_COUNT,
        MODEL_MAX_DEPTH,
        &signal
    );
    
    if (result != 0) {
        sol_log("Error: ML inference failed");
        return 5;
    }
    
    // Update state
    state->current_signal = signal;
    state->last_update_slot = current_slot;
    state->total_inferences++;
    
    // Update signal counters
    switch (signal) {
        case 1:  state->buy_count++; break;
        case 0:  state->hold_count++; break;
        case -1: state->sell_count++; break;
    }
    
    // Log result
    sol_log_64(signal, state->total_inferences, state->buy_count, 
               state->hold_count, state->sell_count);
    
    return 0;
}

// ============================================================================
// Program Entrypoint
// ============================================================================

/**
 * sBPF program entrypoint
 * 
 * Called by the slonana runtime when a transaction invokes this program.
 * The input buffer contains serialized SolParameters.
 */
extern "C" uint64_t entrypoint(const uint8_t* input) {
    // Parse parameters (simplified - real impl uses BPF deserialization)
    SolParameters params;
    
    // In a real sBPF program, we would deserialize from input
    // For this example, we assume the runtime provides parsed params
    // The actual parsing is done by the slonana BPF loader
    
    // For demonstration, we access params directly
    // (In production, use sol_deserialize or similar)
    
    // Get instruction type from first byte of instruction data
    if (params.instruction_len < 1) {
        sol_log("Error: Empty instruction");
        return 1;
    }
    
    InstructionType instruction = static_cast<InstructionType>(params.instruction_data[0]);
    
    // Dispatch to handler
    switch (instruction) {
        case InstructionType::Initialize:
            return handle_initialize(&params);
            
        case InstructionType::ProcessUpdate:
            // Current slot would come from runtime in real implementation
            return handle_process_update(&params, 0);
            
        case InstructionType::GetSignal:
            // No-op - state is publicly readable
            return 0;
            
        default:
            sol_log("Error: Unknown instruction");
            return 100;
    }
}

// ============================================================================
// For native testing (not compiled to sBPF)
// ============================================================================

#ifndef __BPF__

#include <iostream>
#include <vector>

// Mock syscall implementations for native testing
extern "C" {
    uint64_t sol_ml_decision_tree(
        const int32_t* features,
        uint64_t features_len,
        const DecisionTreeNode* nodes,
        uint64_t num_nodes,
        uint32_t max_depth,
        int32_t* result
    ) {
        // Traverse tree
        uint32_t node_idx = 0;
        for (uint32_t depth = 0; depth < max_depth && node_idx < num_nodes; depth++) {
            const DecisionTreeNode& node = nodes[node_idx];
            
            if (node.feature_index < 0) {
                // Leaf node
                *result = node.leaf_value;
                return 0;
            }
            
            if (static_cast<uint64_t>(node.feature_index) >= features_len) {
                return 1; // Invalid feature index
            }
            
            // Compare and traverse
            if (features[node.feature_index] < node.threshold) {
                node_idx = node.left_child;
            } else {
                node_idx = node.right_child;
            }
        }
        
        return 2; // Max depth exceeded
    }
    
    uint64_t sol_ml_matmul(
        const int32_t*, uint64_t, const int16_t*, uint64_t,
        const int32_t*, uint64_t, int32_t*, uint64_t*, uint32_t, uint32_t
    ) {
        return 0;
    }
    
    uint64_t sol_ml_activation(int32_t*, uint64_t, uint8_t) {
        return 0;
    }
    
    void sol_log(const char* message) {
        std::cout << "[LOG] " << message << std::endl;
    }
    
    void sol_log_64(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
        std::cout << "[LOG] " << a1 << " " << a2 << " " << a3 << " " << a4 << " " << a5 << std::endl;
    }
}

// Native test harness
void test_feature_extraction() {
    std::cout << "\n=== Testing Feature Extraction ===\n";
    
    OracleData oracle = {
        .version = 1,
        .timestamp = 12345678,
        .current_price = 105000000,    // $105.00 (scaled by 1e6)
        .avg_price_24h = 100000000,    // $100.00
        .volume_24h = 1500000,         // 1.5M
        .avg_volume = 1000000,         // 1.0M average
        .high_24h = 110000000,         // $110.00
        .low_24h = 98000000,           // $98.00
        .price_change_1h = 250,        // +2.5%
        .bid_ask_spread = 50,          // 0.5%
    };
    
    int32_t features[5];
    extract_features(&oracle, features);
    
    std::cout << "Feature 0 (price ratio): " << features[0] << " (expect ~10500)\n";
    std::cout << "Feature 1 (volume ratio): " << features[1] << " (expect ~15000)\n";
    std::cout << "Feature 2 (momentum): " << features[2] << " (expect 250)\n";
    std::cout << "Feature 3 (spread): " << features[3] << " (expect 50)\n";
    std::cout << "Feature 4 (volatility): " << features[4] << " (expect ~1200)\n";
}

void test_inference() {
    std::cout << "\n=== Testing Decision Tree Inference ===\n";
    
    // Test case 1: Strong bullish scenario (price up, high momentum, low volatility)
    // Path: node0 (price 10200 >= 9800 → right) → node6 (momentum 800 >= 500 → right) 
    //     → node10 (volatility 2000 < 5000 → left) → node11 = BUY
    {
        int32_t features[] = {10200, 12000, 800, 20, 2000};
        int32_t signal;
        uint64_t result = sol_ml_decision_tree(features, 5, TRADING_MODEL, MODEL_NODE_COUNT, MODEL_MAX_DEPTH, &signal);
        std::cout << "Strong bullish scenario: signal=" << signal << " (expect 1=BUY), result=" << result << "\n";
    }
    
    // Test case 2: Bearish scenario (price down, momentum negative)
    // Path: node0 (price 9500 < 9800 → left) → node1 (momentum -600 < -500 → left) → node2 = SELL
    {
        int32_t features[] = {9500, 8000, -600, 100, 3000};
        int32_t signal;
        uint64_t result = sol_ml_decision_tree(features, 5, TRADING_MODEL, MODEL_NODE_COUNT, MODEL_MAX_DEPTH, &signal);
        std::cout << "Bearish scenario: signal=" << signal << " (expect -1=SELL), result=" << result << "\n";
    }
    
    // Test case 3: Neutral/cautious scenario (normal price, low momentum, normal spread)
    // Path: node0 (price 10000 >= 9800 → right) → node6 (momentum 0 < 500 → left) 
    //     → node7 (spread 150 < 300 → left) → node8 = HOLD
    {
        int32_t features[] = {10000, 10000, 0, 150, 2500};
        int32_t signal;
        uint64_t result = sol_ml_decision_tree(features, 5, TRADING_MODEL, MODEL_NODE_COUNT, MODEL_MAX_DEPTH, &signal);
        std::cout << "Neutral scenario: signal=" << signal << " (expect 0=HOLD), result=" << result << "\n";
    }
}

int main() {
    std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║       ML Trading Agent - Native Test Mode                  ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n";
    
    test_feature_extraction();
    test_inference();
    
    std::cout << "\n✓ All tests completed\n";
    return 0;
}

#endif // __BPF__
