/**
 * ML Trading Agent - Real sBPF Program
 * 
 * This is a pure C program that compiles to real BPF bytecode using:
 *   clang -target bpfel-unknown-none -march=bpfel -O2 -c ml_trading_agent_sbpf.c
 * 
 * It demonstrates ML inference using fixed-point arithmetic,
 * compatible with eBPF/sBPF verifier constraints.
 */

/* sBPF types - no stdint.h in BPF freestanding environment */
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;

/* Fixed-point arithmetic constants */
#define FIXED_POINT_SCALE 10000
#define MAX_TREE_DEPTH 16
#define MAX_FEATURES 8

/* Trading signals */
#define SIGNAL_HOLD 0
#define SIGNAL_BUY 1
#define SIGNAL_SELL 2

/* Decision tree node structure */
struct DecisionTreeNode {
    int32_t feature_index;  /* Which feature to check (-1 = leaf) */
    int32_t threshold;      /* Threshold value in fixed-point */
    int32_t left_child;     /* Index of left child or class (if leaf) */
    int32_t right_child;    /* Index of right child */
};

/* Agent state structure */
struct AgentState {
    uint64_t last_execution_slot;
    int32_t last_signal;
    uint64_t total_inferences;
    int32_t accumulated_pnl;  /* Fixed-point */
};

/* Market data structure (from oracle) */
struct MarketData {
    int32_t price;           /* Fixed-point price */
    int32_t volume;          /* Fixed-point volume */
    int32_t volatility;      /* Fixed-point volatility */
    int32_t momentum;        /* Fixed-point momentum */
    int32_t rsi;             /* Fixed-point RSI */
    int32_t macd;            /* Fixed-point MACD */
    int32_t bollinger_pos;   /* Fixed-point Bollinger position */
    int32_t trend;           /* Fixed-point trend indicator */
};

/* Embedded decision tree model - trained for trading signals */
static const struct DecisionTreeNode DECISION_TREE[] = {
    /* Node 0: Root - check momentum */
    { .feature_index = 3, .threshold = 500, .left_child = 1, .right_child = 2 },
    
    /* Node 1: momentum <= 0.05, check volatility */
    { .feature_index = 2, .threshold = 2000, .left_child = 3, .right_child = 4 },
    
    /* Node 2: momentum > 0.05, check RSI */
    { .feature_index = 4, .threshold = 7000, .left_child = 5, .right_child = 6 },
    
    /* Node 3: low volatility, low momentum -> HOLD */
    { .feature_index = -1, .threshold = 0, .left_child = SIGNAL_HOLD, .right_child = 0 },
    
    /* Node 4: high volatility, low momentum -> check trend */
    { .feature_index = 7, .threshold = 0, .left_child = 7, .right_child = 8 },
    
    /* Node 5: high momentum, RSI <= 70 -> BUY */
    { .feature_index = -1, .threshold = 0, .left_child = SIGNAL_BUY, .right_child = 0 },
    
    /* Node 6: high momentum, RSI > 70 (overbought) -> HOLD */
    { .feature_index = -1, .threshold = 0, .left_child = SIGNAL_HOLD, .right_child = 0 },
    
    /* Node 7: negative trend -> SELL */
    { .feature_index = -1, .threshold = 0, .left_child = SIGNAL_SELL, .right_child = 0 },
    
    /* Node 8: positive trend -> HOLD */
    { .feature_index = -1, .threshold = 0, .left_child = SIGNAL_HOLD, .right_child = 0 },
};

#define TREE_SIZE (sizeof(DECISION_TREE) / sizeof(DECISION_TREE[0]))

/**
 * Decision tree inference - runs in O(depth) time
 * All operations are verifier-safe (bounded loop, no floating point)
 */
static int32_t decision_tree_infer(const int32_t* features, int32_t num_features) {
    int32_t node_idx = 0;
    
    /* Bounded loop for verifier safety */
    for (int32_t depth = 0; depth < MAX_TREE_DEPTH; depth++) {
        if (node_idx < 0 || node_idx >= (int32_t)TREE_SIZE) {
            return SIGNAL_HOLD;  /* Safety fallback */
        }
        
        const struct DecisionTreeNode* node = &DECISION_TREE[node_idx];
        
        /* Leaf node - return prediction */
        if (node->feature_index < 0) {
            return node->left_child;  /* Class stored in left_child */
        }
        
        /* Bounds check on feature index */
        if (node->feature_index >= num_features) {
            return SIGNAL_HOLD;  /* Safety fallback */
        }
        
        /* Traverse tree based on threshold comparison */
        int32_t feature_value = features[node->feature_index];
        if (feature_value <= node->threshold) {
            node_idx = node->left_child;
        } else {
            node_idx = node->right_child;
        }
    }
    
    return SIGNAL_HOLD;  /* Default if max depth reached */
}

/**
 * Extract features from market data
 * Returns array of fixed-point feature values
 */
static void extract_features(const struct MarketData* market, int32_t* features) {
    features[0] = market->price;
    features[1] = market->volume;
    features[2] = market->volatility;
    features[3] = market->momentum;
    features[4] = market->rsi;
    features[5] = market->macd;
    features[6] = market->bollinger_pos;
    features[7] = market->trend;
}

/**
 * Main sBPF entry point
 * Called by the validator when processing transactions
 * 
 * Parameters (passed via memory/registers):
 *   - instruction_data: pointer to instruction bytes
 *   - accounts: array of account pointers
 *   - num_accounts: number of accounts
 * 
 * Returns: 0 on success, error code on failure
 */
uint64_t entrypoint(
    uint8_t* instruction_data,
    uint64_t instruction_len,
    uint8_t** accounts,
    uint64_t num_accounts
) {
    /* Validate minimum accounts */
    if (num_accounts < 2) {
        return 1;  /* Error: insufficient accounts */
    }
    
    /* Parse instruction type */
    if (instruction_len < 1) {
        return 2;  /* Error: no instruction data */
    }
    
    uint8_t instruction_type = instruction_data[0];
    
    /* Account layout:
     * [0] = Program account (read-only)
     * [1] = Agent state account (writable)
     * [2] = Oracle/market data account (read-only, optional)
     */
    
    struct AgentState* state = (struct AgentState*)accounts[1];
    
    switch (instruction_type) {
        case 0: {  /* Initialize */
            state->last_execution_slot = 0;
            state->last_signal = SIGNAL_HOLD;
            state->total_inferences = 0;
            state->accumulated_pnl = 0;
            return 0;
        }
        
        case 1: {  /* Process market update and run inference */
            if (num_accounts < 3) {
                return 3;  /* Error: need oracle account */
            }
            
            struct MarketData* market = (struct MarketData*)accounts[2];
            
            /* Extract features */
            int32_t features[MAX_FEATURES];
            extract_features(market, features);
            
            /* Run ML inference */
            int32_t signal = decision_tree_infer(features, MAX_FEATURES);
            
            /* Update state */
            state->last_signal = signal;
            state->total_inferences++;
            
            return 0;
        }
        
        case 2: {  /* Query state (no-op, state is readable) */
            return 0;
        }
        
        default:
            return 4;  /* Error: unknown instruction */
    }
}

/* BPF program metadata section */
__attribute__((section(".text.entrypoint")))
uint64_t _start(uint8_t* data, uint64_t len, uint8_t** accounts, uint64_t n) {
    return entrypoint(data, len, accounts, n);
}
