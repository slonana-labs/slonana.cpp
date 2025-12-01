/**
 * AI Trading Agent - sBPF Program Example
 * 
 * This program demonstrates how to build an AI-powered trading agent
 * that runs on the slonana SVM with ML inference capabilities.
 * 
 * Features:
 * - Decision tree inference for trading signals (~10ns latency)
 * - Autonomous execution via async timers
 * - Fixed-point arithmetic for eBPF/sBPF compatibility
 * - Account watching for oracle updates
 * 
 * Compile to sBPF:
 *   clang++ -target bpf -O2 -c ai_trading_agent.cpp -o ai_trading_agent.o
 *   llvm-objcopy -O binary ai_trading_agent.o ai_trading_agent.so
 * 
 * Deploy to slonana:
 *   slonana program deploy ai_trading_agent.so
 */

#include <cstdint>
#include <cstring>

// ============================================================================
// sBPF Syscall Declarations (provided by slonana runtime)
// ============================================================================

extern "C" {

// ML Inference syscalls
uint64_t sol_ml_decision_tree(
    const int32_t* features,
    uint64_t features_len,
    const void* nodes,
    uint64_t nodes_len,
    uint32_t max_depth,
    int32_t* result
);

uint64_t sol_ml_matmul(
    const int32_t* input,
    uint64_t input_len,
    const int16_t* weights,
    uint64_t weights_rows,
    uint64_t weights_cols,
    const int16_t* bias,
    int32_t* output,
    uint64_t output_len
);

uint64_t sol_ml_activation(
    int32_t* data,
    uint64_t data_len,
    uint32_t activation_type
);

// Async execution syscalls
uint64_t sol_timer_create_periodic(
    const uint8_t* program_id,
    uint64_t trigger_slot,
    uint64_t period_slots,
    uint32_t callback_selector,
    uint64_t* timer_id_out
);

uint64_t sol_timer_cancel(uint64_t timer_id);

uint64_t sol_watcher_create(
    const uint8_t* program_id,
    const uint8_t* account,
    uint32_t trigger_type,
    uint32_t callback_selector,
    uint64_t* watcher_id_out
);

// Standard Solana syscalls
void sol_log_(const char* msg, uint64_t len);
void sol_log_64_(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);
uint64_t sol_get_clock_sysvar(void* dst);
uint64_t sol_invoke_signed_c(
    const void* instruction,
    const void* account_infos,
    int num_account_infos,
    const void* signers_seeds,
    int num_signers
);

// Panic handler (required by sBPF)
void sol_panic_(const char* msg, uint64_t len, uint64_t line, uint64_t col);

} // extern "C"

// Helper macros
#define sol_log(msg) sol_log_(msg, sizeof(msg) - 1)
#define sol_panic(msg) sol_panic_(msg, sizeof(msg) - 1, __LINE__, 0)

// ============================================================================
// Constants and Configuration
// ============================================================================

// Fixed-point scale (4 decimal places)
constexpr int32_t FIXED_POINT_SCALE = 10000;

// Activation function types
constexpr uint32_t ACTIVATION_RELU = 0;
constexpr uint32_t ACTIVATION_SIGMOID = 2;
constexpr uint32_t ACTIVATION_SOFTMAX = 4;

// Trading signals
constexpr int32_t SIGNAL_SELL = -1;
constexpr int32_t SIGNAL_HOLD = 0;
constexpr int32_t SIGNAL_BUY = 1;

// Watcher trigger types
constexpr uint32_t TRIGGER_ANY_CHANGE = 0;
constexpr uint32_t TRIGGER_THRESHOLD_ABOVE = 1;

// Callback selectors
constexpr uint32_t CALLBACK_TIMER_TICK = 1;
constexpr uint32_t CALLBACK_ORACLE_UPDATE = 2;

// Rate limiting
constexpr uint64_t MIN_SLOTS_BETWEEN_TRADES = 10;

// ============================================================================
// Program State Structures
// ============================================================================

/**
 * Decision Tree Node (matches slonana::svm::DecisionTreeNode)
 */
struct DecisionTreeNode {
    int16_t feature_index;   // -1 = leaf node
    int16_t left_child;      // Index of left child
    int16_t right_child;     // Index of right child
    int32_t threshold;       // Split threshold (fixed-point)
    int32_t leaf_value;      // Prediction value for leaf nodes
};

/**
 * Agent State stored in program data account
 */
struct AgentState {
    uint8_t is_initialized;        // 0 = false, 1 = true
    uint8_t is_paused;             // 0 = running, 1 = paused
    uint8_t padding[2];            // Alignment
    uint32_t model_version;        // Current model version
    uint64_t last_trade_slot;      // Slot of last trade
    int8_t current_position;       // -1 = short, 0 = neutral, 1 = long
    int8_t reserved[7];            // Reserved for future use
    int64_t accumulated_pnl;       // Accumulated profit/loss (fixed-point)
    uint64_t total_trades;         // Total number of trades executed
    uint64_t timer_id;             // Active timer ID
    uint64_t watcher_id;           // Active watcher ID
    uint8_t owner[32];             // Owner public key
    uint8_t oracle_account[32];    // Oracle data account
    uint8_t escrow_account[32];    // Trading escrow account
};

/**
 * Oracle Data format (example - matches Pyth/Switchboard)
 */
struct OracleData {
    uint64_t price;              // Current price (fixed-point)
    uint64_t confidence;         // Price confidence interval
    uint64_t price_24h_ago;      // 24h moving average
    uint64_t volume_1h;          // 1-hour volume
    uint64_t volume_24h_avg;     // 24h average volume
    int64_t  momentum;           // Price momentum indicator
    uint64_t last_update_slot;   // When oracle was last updated
};

/**
 * Instruction data structures
 */
struct InitializeInstruction {
    uint8_t instruction_type;    // 0
    uint8_t padding[3];
    uint32_t model_version;
};

struct TriggerInstruction {
    uint8_t instruction_type;    // 2
};

struct ConfigureInstruction {
    uint8_t instruction_type;    // 3
    uint8_t enable_timer;        // 1 = enable auto-execution
    uint8_t enable_watcher;      // 1 = enable oracle watching
    uint8_t period_slots;        // Timer period in slots
};

// ============================================================================
// Fixed-Point Helpers
// ============================================================================

/**
 * Convert raw oracle price to normalized fixed-point
 */
inline int32_t normalize_price(uint64_t current, uint64_t baseline) {
    if (baseline == 0) return FIXED_POINT_SCALE; // Return 1.0
    // Ratio = current / baseline * SCALE
    return static_cast<int32_t>(
        (static_cast<uint64_t>(current) * FIXED_POINT_SCALE) / baseline
    );
}

/**
 * Safe fixed-point multiplication
 */
inline int32_t fp_multiply(int32_t a, int32_t b) {
    int64_t result = (static_cast<int64_t>(a) * static_cast<int64_t>(b)) / FIXED_POINT_SCALE;
    if (result > INT32_MAX) return INT32_MAX;
    if (result < INT32_MIN) return INT32_MIN;
    return static_cast<int32_t>(result);
}

// ============================================================================
// Trading Decision Tree Model
// ============================================================================

/**
 * Embedded decision tree model
 * 
 * This is a simple 5-node tree for demonstration:
 *   - Feature 0: Normalized price (current/24h_avg)
 *   - Feature 1: Volume ratio
 *   - Feature 2: Momentum
 * 
 * Tree structure:
 *           [price < 0.95?]
 *          /              \
 *   [momentum < -500?]   [price > 1.05?]
 *      /       \           /         \
 *    SELL    HOLD       HOLD        BUY
 */
constexpr DecisionTreeNode TRADING_MODEL[] = {
    // Node 0 (root): Check if price < 0.95 (9500 fixed-point)
    { 0, 1, 2, 9500, 0 },
    
    // Node 1: Check if momentum < -500 (bearish)
    { 2, 3, 4, -500, 0 },
    
    // Node 2: Check if price > 1.05 (10500 fixed-point)
    { 0, 5, 6, 10500, 0 },
    
    // Node 3: Leaf - SELL (price low + momentum negative)
    { -1, -1, -1, 0, SIGNAL_SELL },
    
    // Node 4: Leaf - HOLD (price low but momentum neutral)
    { -1, -1, -1, 0, SIGNAL_HOLD },
    
    // Node 5: Leaf - HOLD (price normal)
    { -1, -1, -1, 0, SIGNAL_HOLD },
    
    // Node 6: Leaf - BUY (price high)
    { -1, -1, -1, 0, SIGNAL_BUY },
};

constexpr uint32_t MODEL_MAX_DEPTH = 10;
constexpr size_t MODEL_NUM_NODES = sizeof(TRADING_MODEL) / sizeof(DecisionTreeNode);

// ============================================================================
// Feature Extraction
// ============================================================================

/**
 * Extract ML features from oracle data
 * 
 * Features:
 *   [0] Normalized price (current / 24h average)
 *   [1] Volume ratio (1h / 24h average)
 *   [2] Momentum indicator
 */
void extract_features(const OracleData* oracle, int32_t* features, size_t* num_features) {
    // Feature 0: Price ratio
    features[0] = normalize_price(oracle->price, oracle->price_24h_ago);
    
    // Feature 1: Volume ratio
    if (oracle->volume_24h_avg > 0) {
        features[1] = normalize_price(oracle->volume_1h, oracle->volume_24h_avg / 24);
    } else {
        features[1] = FIXED_POINT_SCALE; // Default to 1.0
    }
    
    // Feature 2: Momentum (already fixed-point from oracle)
    features[2] = static_cast<int32_t>(oracle->momentum);
    
    *num_features = 3;
}

// ============================================================================
// Trade Execution
// ============================================================================

/**
 * Execute a trade based on the ML signal
 */
void execute_trade(AgentState* state, int32_t signal, uint64_t current_slot) {
    // Rate limiting check
    if (current_slot - state->last_trade_slot < MIN_SLOTS_BETWEEN_TRADES) {
        sol_log("Trade rate limited");
        return;
    }
    
    // Check if position change is needed
    if (signal == state->current_position) {
        return; // No change needed
    }
    
    // Log the trade
    sol_log_64_(
        static_cast<uint64_t>('T'),  // Trade marker
        static_cast<uint64_t>(signal),
        current_slot,
        state->total_trades,
        0
    );
    
    // Update state
    state->current_position = static_cast<int8_t>(signal);
    state->last_trade_slot = current_slot;
    state->total_trades++;
    
    // TODO: In production, invoke token transfer instruction here
    // This would use sol_invoke_signed_c to transfer tokens
}

// ============================================================================
// Instruction Handlers
// ============================================================================

/**
 * Initialize the trading agent
 */
uint64_t handle_initialize(
    uint8_t* state_data,
    const uint8_t* instruction_data,
    const uint8_t (*accounts)[32],
    size_t num_accounts
) {
    if (num_accounts < 3) {
        sol_panic("Initialize requires 3 accounts");
        return 1;
    }
    
    AgentState* state = reinterpret_cast<AgentState*>(state_data);
    
    // Check not already initialized
    if (state->is_initialized) {
        sol_log("Already initialized");
        return 2;
    }
    
    // Parse instruction
    const InitializeInstruction* init = 
        reinterpret_cast<const InitializeInstruction*>(instruction_data);
    
    // Initialize state
    state->is_initialized = 1;
    state->is_paused = 0;
    state->model_version = init->model_version;
    state->last_trade_slot = 0;
    state->current_position = SIGNAL_HOLD;
    state->accumulated_pnl = 0;
    state->total_trades = 0;
    state->timer_id = 0;
    state->watcher_id = 0;
    
    // Store account references
    memcpy(state->owner, accounts[0], 32);
    memcpy(state->oracle_account, accounts[1], 32);
    memcpy(state->escrow_account, accounts[2], 32);
    
    sol_log("AI Trading Agent initialized");
    
    return 0;
}

/**
 * Process a trading trigger (timer tick or manual)
 */
uint64_t handle_trigger(
    AgentState* state,
    const OracleData* oracle,
    uint64_t current_slot
) {
    // Check initialized and not paused
    if (!state->is_initialized) {
        sol_log("Not initialized");
        return 1;
    }
    
    if (state->is_paused) {
        sol_log("Agent paused");
        return 2;
    }
    
    // Extract features from oracle data
    int32_t features[3];
    size_t num_features;
    extract_features(oracle, features, &num_features);
    
    // Run ML inference
    int32_t signal;
    uint64_t result = sol_ml_decision_tree(
        features,
        num_features,
        TRADING_MODEL,
        MODEL_NUM_NODES,
        MODEL_MAX_DEPTH,
        &signal
    );
    
    if (result != 0) {
        sol_log("ML inference failed");
        return 3;
    }
    
    // Log inference result
    sol_log_64_(
        static_cast<uint64_t>('I'),  // Inference marker
        static_cast<uint64_t>(features[0]),
        static_cast<uint64_t>(features[1]),
        static_cast<uint64_t>(signal),
        0
    );
    
    // Execute trade if signal differs from position
    execute_trade(state, signal, current_slot);
    
    return 0;
}

/**
 * Configure autonomous execution
 */
uint64_t handle_configure(
    AgentState* state,
    const ConfigureInstruction* config,
    const uint8_t* program_id
) {
    if (!state->is_initialized) {
        sol_log("Not initialized");
        return 1;
    }
    
    // Set up periodic timer
    if (config->enable_timer) {
        // Cancel existing timer
        if (state->timer_id != 0) {
            sol_timer_cancel(state->timer_id);
        }
        
        // Get current slot (simplified - would use sol_get_clock_sysvar)
        uint64_t current_slot = 1000; // Placeholder
        
        // Create new periodic timer
        uint64_t timer_id;
        uint64_t result = sol_timer_create_periodic(
            program_id,
            current_slot + 1,           // Start next slot
            config->period_slots,       // Period
            CALLBACK_TIMER_TICK,        // Callback selector
            &timer_id
        );
        
        if (result == 0) {
            state->timer_id = timer_id;
            sol_log("Timer created");
        }
    } else if (state->timer_id != 0) {
        sol_timer_cancel(state->timer_id);
        state->timer_id = 0;
        sol_log("Timer cancelled");
    }
    
    // Set up oracle watcher
    if (config->enable_watcher) {
        if (state->watcher_id == 0) {
            uint64_t watcher_id;
            uint64_t result = sol_watcher_create(
                program_id,
                state->oracle_account,
                TRIGGER_ANY_CHANGE,
                CALLBACK_ORACLE_UPDATE,
                &watcher_id
            );
            
            if (result == 0) {
                state->watcher_id = watcher_id;
                sol_log("Watcher created");
            }
        }
    }
    
    return 0;
}

/**
 * Pause/Resume the agent
 */
uint64_t handle_pause_resume(AgentState* state, bool pause) {
    if (!state->is_initialized) {
        sol_log("Not initialized");
        return 1;
    }
    
    state->is_paused = pause ? 1 : 0;
    
    if (pause) {
        sol_log("Agent paused");
    } else {
        sol_log("Agent resumed");
    }
    
    return 0;
}

// ============================================================================
// Program Entry Point
// ============================================================================

/**
 * sBPF program entry point
 * 
 * This function is called by the SVM when a transaction invokes this program.
 * 
 * @param input Pointer to serialized instruction data
 * @return 0 on success, non-zero error code on failure
 */
extern "C" uint64_t entrypoint(const uint8_t* input) {
    // Parse input format:
    // [0..7]   = num_accounts
    // [8..15]  = data_len
    // [16..]   = accounts data (32 bytes each pubkey + metadata)
    // [...]    = instruction data
    
    // Note: This is a simplified parsing example. Production code would
    // use the full Solana account info deserialization.
    
    uint64_t num_accounts;
    memcpy(&num_accounts, input, sizeof(uint64_t));
    
    if (num_accounts == 0) {
        sol_panic("No accounts provided");
        return 1;
    }
    
    // For this example, we'll use a simplified data layout
    const uint8_t* instruction_data = input + 16 + (num_accounts * 96);
    uint8_t instruction_type = instruction_data[0];
    
    // Get program data account (first account is always state)
    uint8_t* state_data = const_cast<uint8_t*>(input + 16 + 64); // Skip to data portion
    AgentState* state = reinterpret_cast<AgentState*>(state_data);
    
    // Dispatch based on instruction type
    switch (instruction_type) {
        case 0: // Initialize
            sol_log("Processing: Initialize");
            // Note: Would extract accounts array properly
            return 0;
            
        case 1: // UpdateModel
            sol_log("Processing: UpdateModel");
            // Would update model weights from instruction data
            return 0;
            
        case 2: // Trigger
            sol_log("Processing: Trigger");
            // Would get oracle data from accounts
            // handle_trigger(state, oracle, current_slot);
            return 0;
            
        case 3: // Configure
            sol_log("Processing: Configure");
            // handle_configure(state, config, program_id);
            return 0;
            
        case 4: // Pause
            sol_log("Processing: Pause");
            return handle_pause_resume(state, true);
            
        case 5: // Resume
            sol_log("Processing: Resume");
            return handle_pause_resume(state, false);
            
        default:
            sol_log("Unknown instruction");
            return 1;
    }
}

// ============================================================================
// Timer/Watcher Callback Entry Point
// ============================================================================

/**
 * Callback entry point for async triggers
 * 
 * Called by the runtime when a timer fires or watcher triggers.
 */
extern "C" uint64_t callback_entrypoint(const uint8_t* input, uint32_t selector) {
    sol_log_64_(
        static_cast<uint64_t>('C'),  // Callback marker
        static_cast<uint64_t>(selector),
        0, 0, 0
    );
    
    switch (selector) {
        case CALLBACK_TIMER_TICK:
            // Periodic timer fired - run inference
            sol_log("Timer tick callback");
            // Would call handle_trigger with current state
            return 0;
            
        case CALLBACK_ORACLE_UPDATE:
            // Oracle data changed - run inference
            sol_log("Oracle update callback");
            // Would call handle_trigger with updated oracle
            return 0;
            
        default:
            sol_log("Unknown callback");
            return 1;
    }
}
