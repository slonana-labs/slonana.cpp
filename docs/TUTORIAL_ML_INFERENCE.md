# Tutorial: Calling ML Inference from SVM BPF Programs

**A Step-by-Step Guide to On-Chain AI Inference**

This tutorial walks you through integrating machine learning inference into your Solana BPF programs, enabling AI-powered decision making directly on-chain.

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Quick Start](#quick-start)
3. [Step-by-Step Guide](#step-by-step-guide)
   - [Creating Fixed-Point Features](#1-creating-fixed-point-features)
   - [Decision Tree Inference](#2-decision-tree-inference)
   - [Neural Network Forward Pass](#3-neural-network-forward-pass)
   - [Activation Functions](#4-activation-functions)
4. [Complete Trading Agent Example](#complete-trading-agent-example)
5. [Model Serialization](#model-serialization)
6. [Error Handling](#error-handling)
7. [Performance Optimization](#performance-optimization)
8. [Testing Your Integration](#testing-your-integration)

---

## Prerequisites

Before starting, ensure you have:

1. **Development Environment**: slonana C++ validator built with ML inference support
2. **Model**: Pre-trained model (decision tree or MLP) exported from Python
3. **Account Data**: Access to oracle or market data accounts

### Building with ML Inference Support

```bash
cd /path/to/slonana.cpp
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

The ML inference module is included in `libslonana_core.a`.

---

## Quick Start

Here's a minimal example of running inference:

```cpp
#include "svm/ml_inference.h"

using namespace slonana::svm;

// 1. Prepare input features (fixed-point, scale 10000)
std::vector<int32_t> features = {
    5000,   // 0.5 normalized price
    12000,  // 1.2 volume ratio
    -2500   // -0.25 momentum
};

// 2. Create decision tree model
DecisionTreeModel model;
model.nodes = {
    {0, 1, 2, 7500, 0},   // root: if feature[0] < 0.75
    {1, 3, 4, 10000, 0},  // left: if feature[1] < 1.0
    {-1, -1, -1, 0, 1},   // leaf: BUY
    {-1, -1, -1, 0, -1},  // leaf: SELL
    {-1, -1, -1, 0, 0}    // leaf: HOLD
};
model.max_depth = 10;

// 3. Run inference
int32_t action;
uint64_t result = sol_ml_decision_tree(
    features.data(), features.size(),
    model.nodes.data(), model.nodes.size(),
    model.max_depth, &action
);

// 4. Use the result
if (result == 0) {
    // action contains: -1 (SELL), 0 (HOLD), 1 (BUY)
    printf("Model prediction: %d\n", action);
}
```

---

## Step-by-Step Guide

### 1. Creating Fixed-Point Features

The ML syscalls use fixed-point arithmetic with a scale of 10000 (4 decimal places).

```cpp
#include "svm/ml_inference.h"

using namespace slonana::svm;

// Convert floating-point to fixed-point
float raw_price = 1.234f;
int32_t fixed_price = fixed_point::from_float(raw_price);
// Result: 12340

// Convert back to floating-point for debugging
float restored = fixed_point::to_float(fixed_price);
// Result: 1.234f

// Different scales available
constexpr int32_t SCALE_LOW = 100;       // 2 decimal places
constexpr int32_t SCALE_DEFAULT = 10000; // 4 decimal places (default)
constexpr int32_t SCALE_HIGH = 1000000;  // 6 decimal places

// Multiplication with overflow protection
int32_t a = 25000;  // 2.5
int32_t b = 40000;  // 4.0
int32_t result = fixed_point::multiply(a, b);
// Result: 100000 (10.0)

// Division
int32_t quotient = fixed_point::divide(100000, 25000);
// Result: 40000 (4.0)
```

### 2. Decision Tree Inference

Decision trees are ideal for low-latency decisions (9.7ns/inference).

```cpp
#include "svm/ml_inference.h"

using namespace slonana::svm;

// Define a decision tree for trading decisions
// Tree structure:
//           [price < 1.0?]
//          /             \
//   [volume > 2.0?]    SELL
//    /         \
//  BUY        HOLD

std::vector<DecisionTreeNode> tree = {
    // node 0: root - check price (feature 0)
    {
        .feature_index = 0,          // Compare feature index 0 (price)
        .left_child = 1,             // If true, go to node 1
        .right_child = 2,            // If false, go to node 2
        .threshold = 10000,          // Threshold: 1.0 in fixed-point
        .leaf_value = 0              // Not a leaf
    },
    // node 1: check volume (feature 1)
    {
        .feature_index = 1,          // Compare feature index 1 (volume)
        .left_child = 3,             // If true, go to node 3
        .right_child = 4,            // If false, go to node 4
        .threshold = 20000,          // Threshold: 2.0
        .leaf_value = 0              // Not a leaf
    },
    // node 2: leaf - SELL
    {
        .feature_index = -1,         // -1 indicates leaf node
        .left_child = -1,
        .right_child = -1,
        .threshold = 0,
        .leaf_value = -1             // Action: SELL
    },
    // node 3: leaf - BUY
    {
        .feature_index = -1,
        .left_child = -1,
        .right_child = -1,
        .threshold = 0,
        .leaf_value = 1              // Action: BUY
    },
    // node 4: leaf - HOLD
    {
        .feature_index = -1,
        .left_child = -1,
        .right_child = -1,
        .threshold = 0,
        .leaf_value = 0              // Action: HOLD
    }
};

// Run inference
std::vector<int32_t> features = {
    8000,   // price = 0.8 (< 1.0, so go left)
    25000,  // volume = 2.5 (> 2.0, so go right)
};

int32_t prediction;
uint64_t status = sol_ml_decision_tree(
    features.data(),
    features.size(),
    tree.data(),
    tree.size(),
    10,  // max depth
    &prediction
);

if (status == 0) {
    switch (prediction) {
        case 1:  printf("BUY\n"); break;
        case 0:  printf("HOLD\n"); break;
        case -1: printf("SELL\n"); break;
    }
}
```

### 3. Neural Network Forward Pass

For more complex patterns, use MLPs (626ns for 32×32).

```cpp
#include "svm/ml_inference.h"

using namespace slonana::svm;

// Create a simple 2-layer MLP: 4 inputs → 8 hidden → 3 outputs
MLPModel model;
model.version = 1;
model.input_size = 4;
model.output_size = 3;
model.num_layers = 2;
model.quantization_bits = 16;
model.timestamp = std::time(nullptr);

// Layer 1: 4 → 8 with ReLU
DenseLayerWeights layer1;
layer1.input_size = 4;
layer1.output_size = 8;
layer1.activation = ActivationType::RELU;

// Initialize weights (normally from trained model)
layer1.weights.resize(4 * 8);
layer1.bias.resize(8);
for (int i = 0; i < 32; i++) {
    layer1.weights[i] = (int16_t)(rand() % 2000 - 1000);  // [-0.1, 0.1]
}
for (int i = 0; i < 8; i++) {
    layer1.bias[i] = 0;  // Zero bias initialization
}

// Layer 2: 8 → 3 with Softmax
DenseLayerWeights layer2;
layer2.input_size = 8;
layer2.output_size = 3;
layer2.activation = ActivationType::SOFTMAX;
layer2.weights.resize(8 * 3);
layer2.bias.resize(3);
for (int i = 0; i < 24; i++) {
    layer2.weights[i] = (int16_t)(rand() % 2000 - 1000);
}
for (int i = 0; i < 3; i++) {
    layer2.bias[i] = 0;
}

model.layers = {layer1, layer2};

// Serialize model
std::vector<uint8_t> serialized = model.serialize();

// Prepare input
std::vector<int32_t> input = {10000, 5000, -3000, 8000};

// Run forward pass
std::vector<int32_t> output(3);
uint64_t output_len = output.size();

uint64_t status = sol_ml_forward(
    input.data(), input.size(),
    serialized.data(), serialized.size(),
    output.data(), &output_len
);

if (status == 0) {
    // Find argmax for classification
    int32_t max_idx = argmax(output.data(), output.size());
    printf("Predicted class: %d\n", max_idx);
    printf("Confidence: %.2f%%\n", 
           fixed_point::to_float(output[max_idx]) * 100);
}
```

### 4. Activation Functions

Apply activation functions to intermediate results.

```cpp
#include "svm/ml_inference.h"

using namespace slonana::svm;

// ReLU: max(0, x)
std::vector<int32_t> data = {-5000, 0, 5000, 10000};
sol_ml_activation(data.data(), data.size(), ActivationType::RELU);
// Result: {0, 0, 5000, 10000}

// Leaky ReLU: x > 0 ? x : 0.01 * x
data = {-5000, 0, 5000, 10000};
sol_ml_activation(data.data(), data.size(), ActivationType::LEAKY_RELU);
// Result: {-50, 0, 5000, 10000}

// Sigmoid: 1 / (1 + exp(-x))
data = {-30000, 0, 30000};
sol_ml_activation(data.data(), data.size(), ActivationType::SIGMOID);
// Result: {~100, 5000, ~9900} (0.01, 0.5, 0.99 in fixed-point)

// Softmax: exp(xi) / sum(exp(x))
data = {10000, 5000, 0};  // {1.0, 0.5, 0.0}
sol_ml_activation(data.data(), data.size(), ActivationType::SOFTMAX);
// Result: normalized probabilities that sum to 1.0 (10000 in fixed-point)
```

---

## Complete Trading Agent Example

Here's a full example of an on-chain trading agent:

```cpp
#include "svm/ml_inference.h"
#include "svm/async_bpf_execution.h"

using namespace slonana::svm;

// Trading agent state
struct TradingAgent {
    DecisionTreeModel model;
    uint64_t last_trade_slot;
    int32_t position;  // -1 short, 0 neutral, 1 long
};

// Feature extraction from oracle data
void extract_features(
    const uint8_t* oracle_data,
    size_t oracle_len,
    int32_t* features,
    size_t* features_len
) {
    // Example: Extract 5 features from oracle account
    // Feature 0: Normalized price (current / 24h average)
    // Feature 1: Volume ratio
    // Feature 2: Price momentum (1h change)
    // Feature 3: Bid-ask spread
    // Feature 4: Trade imbalance
    
    // Parse oracle data (implementation depends on oracle format)
    uint64_t current_price = *(uint64_t*)(oracle_data + 0);
    uint64_t avg_price_24h = *(uint64_t*)(oracle_data + 8);
    uint64_t volume_1h = *(uint64_t*)(oracle_data + 16);
    uint64_t avg_volume = *(uint64_t*)(oracle_data + 24);
    
    // Convert to fixed-point features
    features[0] = (int32_t)((current_price * 10000) / avg_price_24h);
    features[1] = (int32_t)((volume_1h * 10000) / avg_volume);
    // ... extract other features
    
    *features_len = 5;
}

// Main trading logic
void process_trading_signal(TradingAgent& agent, const uint8_t* oracle_data) {
    // Step 1: Extract features
    int32_t features[5];
    size_t features_len;
    extract_features(oracle_data, 64, features, &features_len);
    
    // Step 2: Run ML inference
    int32_t signal;
    uint64_t status = sol_ml_decision_tree(
        features, features_len,
        agent.model.nodes.data(), agent.model.nodes.size(),
        agent.model.max_depth,
        &signal
    );
    
    if (status != 0) {
        // Inference failed - maintain current position
        return;
    }
    
    // Step 3: Risk management
    // Only change position if signal differs from current
    if (signal == agent.position) {
        return;  // No action needed
    }
    
    // Step 4: Execute trade
    switch (signal) {
        case 1:   // BUY signal
            if (agent.position <= 0) {
                execute_buy_order(/* params */);
                agent.position = 1;
            }
            break;
            
        case -1:  // SELL signal
            if (agent.position >= 0) {
                execute_sell_order(/* params */);
                agent.position = -1;
            }
            break;
            
        case 0:   // NEUTRAL signal
            if (agent.position != 0) {
                close_position(/* params */);
                agent.position = 0;
            }
            break;
    }
}

// Set up autonomous execution with timers
void setup_autonomous_agent(TradingAgent& agent) {
    // Initialize async execution engine
    AsyncBPFEngine engine;
    engine.initialize(4);  // 4 worker threads
    
    // Create periodic timer to check markets every slot
    sol_timer_create_periodic(
        /* program_id */ agent_pubkey,
        /* trigger_slot */ current_slot + 1,
        /* period */ 1,  // Every slot
        /* callback_selector */ TRADE_SIGNAL_CALLBACK,
        /* timer_id_out */ &timer_id
    );
    
    // Create watcher for oracle updates
    sol_watcher_create_threshold(
        /* program_id */ agent_pubkey,
        /* account */ oracle_pubkey,
        /* threshold */ 100,  // 1% change
        WatcherTriggerType::THRESHOLD_ABOVE,
        /* callback_selector */ ORACLE_UPDATE_CALLBACK,
        /* watcher_id_out */ &watcher_id
    );
}
```

---

## Model Serialization

Export and import models between Python and C++:

### Python Export (scikit-learn)

```python
import numpy as np
import struct

def export_decision_tree(clf, filename):
    """Export scikit-learn decision tree to slonana format."""
    tree = clf.tree_
    
    with open(filename, 'wb') as f:
        # Write header
        f.write(struct.pack('<I', 1))  # version
        f.write(struct.pack('<I', tree.node_count))  # num nodes
        f.write(struct.pack('<I', tree.max_depth))  # max depth
        
        # Write nodes
        for i in range(tree.node_count):
            if tree.children_left[i] == -1:  # Leaf
                f.write(struct.pack('<h', -1))  # feature_index
                f.write(struct.pack('<h', -1))  # left_child
                f.write(struct.pack('<h', -1))  # right_child
                f.write(struct.pack('<i', 0))   # threshold
                # Convert class prediction to fixed-point
                leaf_value = int(np.argmax(tree.value[i]) - 1)  # -1, 0, 1
                f.write(struct.pack('<i', leaf_value))
            else:  # Internal node
                f.write(struct.pack('<h', tree.feature[i]))
                f.write(struct.pack('<h', tree.children_left[i]))
                f.write(struct.pack('<h', tree.children_right[i]))
                # Convert threshold to fixed-point (scale 10000)
                threshold = int(tree.threshold[i] * 10000)
                f.write(struct.pack('<i', threshold))
                f.write(struct.pack('<i', 0))  # leaf_value (unused)
```

### C++ Import

```cpp
DecisionTreeModel load_model(const std::string& filename) {
    DecisionTreeModel model;
    
    std::ifstream file(filename, std::ios::binary);
    
    // Read header
    uint32_t version, num_nodes, max_depth;
    file.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(&num_nodes), sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(&max_depth), sizeof(uint32_t));
    
    model.max_depth = max_depth;
    model.nodes.resize(num_nodes);
    
    // Read nodes
    for (size_t i = 0; i < num_nodes; i++) {
        file.read(reinterpret_cast<char*>(&model.nodes[i].feature_index), sizeof(int16_t));
        file.read(reinterpret_cast<char*>(&model.nodes[i].left_child), sizeof(int16_t));
        file.read(reinterpret_cast<char*>(&model.nodes[i].right_child), sizeof(int16_t));
        file.read(reinterpret_cast<char*>(&model.nodes[i].threshold), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&model.nodes[i].leaf_value), sizeof(int32_t));
    }
    
    return model;
}
```

---

## Error Handling

All syscalls return error codes:

```cpp
#include "svm/ml_inference.h"

// Error codes
constexpr uint64_t SUCCESS = 0;
constexpr uint64_t ERROR_NULL_POINTER = 1;
constexpr uint64_t ERROR_INVALID_DIMENSIONS = 2;
constexpr uint64_t ERROR_INVALID_MODEL = 3;
constexpr uint64_t ERROR_MAX_DEPTH_EXCEEDED = 4;

// Proper error handling
int32_t result;
uint64_t status = sol_ml_decision_tree(
    features.data(), features.size(),
    model.nodes.data(), model.nodes.size(),
    model.max_depth, &result
);

switch (status) {
    case SUCCESS:
        // Use result
        break;
    case ERROR_NULL_POINTER:
        log_error("Null pointer in inference");
        break;
    case ERROR_INVALID_MODEL:
        log_error("Invalid model structure");
        break;
    default:
        log_error("Unknown error: %lu", status);
        break;
}
```

---

## Performance Optimization

### Tips for Maximum Performance

1. **Use Decision Trees for Simple Decisions**
   - 9.7ns vs 626ns for MLP
   - Sufficient for trading signals

2. **Minimize Feature Count**
   - Each feature adds computation
   - 5-10 features often sufficient

3. **Batch Operations**
   - Use `sol_ml_forward` for multiple layers
   - Avoids syscall overhead

4. **Quantize Aggressively**
   - INT8 weights reduce memory
   - Minimal accuracy loss for most models

### Compute Cost Estimation

```cpp
uint64_t estimate_compute_units(
    uint32_t input_size,
    uint32_t output_size,
    ActivationType activation
) {
    uint64_t base_cost = 1 + input_size * output_size;
    
    switch (activation) {
        case ActivationType::RELU:
            return base_cost + output_size;
        case ActivationType::SIGMOID:
            return base_cost + 3 * output_size;
        case ActivationType::SOFTMAX:
            return base_cost + 20 + 5 * output_size;
        default:
            return base_cost;
    }
}

// Example usage
uint64_t layer1_cost = estimate_compute_units(4, 8, ActivationType::RELU);
uint64_t layer2_cost = estimate_compute_units(8, 3, ActivationType::SOFTMAX);
printf("Total model cost: %lu CU\n", layer1_cost + layer2_cost);
```

---

## Testing Your Integration

### Unit Test Example

```cpp
#include "test_framework.h"
#include "svm/ml_inference.h"

void test_decision_tree_buy_signal() {
    // Arrange
    std::vector<DecisionTreeNode> tree = {
        {0, 1, 2, 10000, 0},  // root
        {-1, -1, -1, 0, 1},   // leaf: BUY
        {-1, -1, -1, 0, -1}   // leaf: SELL
    };
    
    std::vector<int32_t> features = {5000};  // 0.5 < 1.0, goes left
    int32_t result;
    
    // Act
    uint64_t status = sol_ml_decision_tree(
        features.data(), features.size(),
        tree.data(), tree.size(),
        10, &result
    );
    
    // Assert
    ASSERT_EQ(status, 0);
    ASSERT_EQ(result, 1);  // BUY signal
}

void test_mlp_forward_pass() {
    // Arrange
    MLPModel model;
    model.input_size = 2;
    model.output_size = 2;
    model.num_layers = 1;
    
    DenseLayerWeights layer;
    layer.input_size = 2;
    layer.output_size = 2;
    layer.weights = {10000, 0, 0, 10000};  // Identity-ish
    layer.bias = {0, 0};
    layer.activation = ActivationType::RELU;
    model.layers = {layer};
    
    auto serialized = model.serialize();
    std::vector<int32_t> input = {5000, 10000};
    std::vector<int32_t> output(2);
    uint64_t output_len = 2;
    
    // Act
    uint64_t status = sol_ml_forward(
        input.data(), input.size(),
        serialized.data(), serialized.size(),
        output.data(), &output_len
    );
    
    // Assert
    ASSERT_EQ(status, 0);
    ASSERT_EQ(output_len, 2);
    // With identity weights and no bias, output ≈ input
}
```

### Running Tests

```bash
cd build
./slonana_ml_inference_tests
```

Expected output:
```
=== ML Inference Tests ===
Testing fixed-point arithmetic...
  ✓ Fixed-point tests passed
...
====================================
Test Results: 24/24 passed
====================================
```

---

## Next Steps

1. **Read the API Reference**: See `docs/ML_INFERENCE.md` for complete API documentation
2. **Explore Async Execution**: See `docs/ASYNC_BPF_EXECUTION.md` for autonomous agents
3. **Economic Opcodes**: See `docs/ECONOMIC_OPCODES.md` for auctions, escrow, staking
4. **Example Programs**: Check the `examples/` directory for complete agent implementations

---

## Getting Help

- **GitHub Issues**: Report bugs or request features
- **Discord**: Join the slonana community for real-time help
- **Documentation**: Full API reference at `docs/`

Happy building! 🚀
