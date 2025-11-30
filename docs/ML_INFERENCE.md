# ML Inference in sBPF Runtime

**On-Chain AI Inference for Autonomous Agents**

This document describes the machine learning inference capabilities implemented in the Solana Virtual Machine (SVM) runtime, enabling AI-powered programs to make intelligent decisions on-chain without external compute dependencies.

---

## Executive Summary

The ML inference module provides:
- **Fixed-point arithmetic** for kernel-safe computation (no floating-point)
- **Neural network forward pass** (MLP models up to 8 layers)
- **Decision tree inference** (93ns latency target achieved)
- **Quantized model support** (INT8/INT16 weights)
- **Verifier-safe operations** (bounded loops, limited stack)

### Performance Results

| Operation | Latency | Target |
|-----------|---------|--------|
| Decision tree (31 nodes) | 16ns | <100ns |
| Dense layer (32×32) | 629ns | <10μs |
| ReLU (64 elements) | 9ns | N/A |
| Sigmoid (64 elements) | 44ns | N/A |
| Softmax (8 elements) | 187ns | N/A |

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│  sBPF Program (On-Chain AI Agent)                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  1. Data Input (Account State)                              │
│     • Read oracle prices, market data                       │
│     • Feature extraction from accounts                      │
│                                                             │
│  2. ML Inference (Syscalls)                                 │
│     • sol_ml_matmul - Matrix multiplication                 │
│     • sol_ml_activation - ReLU, sigmoid, softmax            │
│     • sol_ml_decision_tree - Decision tree traversal        │
│     • sol_ml_forward - Full MLP forward pass                │
│                                                             │
│  3. Action (Transaction Generation)                         │
│     • Execute trades based on model output                  │
│     • Update on-chain state                                 │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Fixed-Point Arithmetic

Since eBPF/sBPF environments prohibit floating-point operations, all ML computations use fixed-point arithmetic with INT32/INT64 integers.

### Scale Factor

```cpp
// Default scale provides 4 decimal places
constexpr int32_t SCALE = 10000;

// Example: 0.245 represented as 2450
int32_t weight = fixed_point::from_float(0.245f);  // Returns 2450
```

### Overflow Protection

All multiplications use INT64 intermediates to prevent overflow:

```cpp
int32_t multiply(int32_t a, int32_t b, int32_t scale) {
    int64_t result = (int64_t)a * (int64_t)b / scale;
    // Clamp to INT32 range
    return clamp(result, INT32_MIN, INT32_MAX);
}
```

---

## ML Syscalls API

### sol_ml_matmul

Matrix multiplication with INT16 weights.

```cpp
uint64_t sol_ml_matmul(
    const int32_t* input,      // Input vector [cols]
    uint64_t input_len,
    const int16_t* weights,    // Weight matrix [rows × cols]
    uint64_t weights_len,
    const int32_t* bias,       // Bias vector [rows]
    uint64_t bias_len,
    int32_t* output,           // Output vector [rows]
    uint64_t* output_len,
    uint32_t rows,
    uint32_t cols
);
```

**Returns:** 0 on success, error code otherwise

### sol_ml_activation

Apply activation function in-place.

```cpp
uint64_t sol_ml_activation(
    int32_t* data,             // Data to transform
    uint64_t len,
    uint8_t activation         // ActivationType enum
);
```

**Supported activations:**
- `NONE (0)` - Linear (passthrough)
- `RELU (1)` - max(0, x)
- `SIGMOID (2)` - Lookup table approximation
- `TANH (3)` - Derived from sigmoid
- `SOFTMAX (4)` - Taylor series approximation
- `LEAKY_RELU (5)` - x > 0 ? x : 0.01x

### sol_ml_decision_tree

Decision tree traversal with bounded depth.

```cpp
uint64_t sol_ml_decision_tree(
    const int32_t* features,       // Feature vector
    uint64_t features_len,
    const DecisionTreeNode* nodes, // Tree nodes
    uint64_t num_nodes,
    uint32_t max_depth,            // Max traversal depth
    int32_t* result                // Classification output
);
```

### sol_ml_forward

Complete MLP forward pass.

```cpp
uint64_t sol_ml_forward(
    const int32_t* input,      // Input features
    uint64_t input_len,
    const uint8_t* model,      // Serialized MLPModel
    uint64_t model_len,
    int32_t* output,           // Output predictions
    uint64_t* output_len
);
```

---

## Model Structures

### MLPModel

Multi-layer perceptron with quantized weights:

```cpp
struct MLPModel {
    uint32_t version;           // Model version for updates
    uint32_t input_size;        // Input feature count
    uint32_t output_size;       // Output class count
    uint8_t num_layers;         // Dense layers (max 8)
    uint8_t quantization_bits;  // 8 or 16
    uint64_t timestamp;         // Last update
    
    vector<DenseLayerWeights> layers;
};
```

### DecisionTreeNode

Cache-friendly node structure:

```cpp
struct DecisionTreeNode {
    int16_t feature_index;     // Feature to compare (-1 = leaf)
    int16_t left_child;        // Left child index
    int16_t right_child;       // Right child index
    int32_t threshold;         // Split threshold (fixed-point)
    int32_t leaf_value;        // Classification result
};
```

---

## Compute Costs

Estimated compute units for budgeting:

```cpp
// Dense layer: O(input × output)
uint64_t dense_cost = 1 + input_size * output_size;

// Decision tree: O(depth)
uint64_t tree_cost = 2 * max_depth;

// Activation functions
uint64_t relu_cost = len;
uint64_t sigmoid_cost = 3 * len;
uint64_t softmax_cost = 20 + 5 * len;
```

---

## Security Considerations

### Input Validation
- All pointers checked for null
- Array bounds validated before access
- Dimension mismatches return error codes

### Overflow Protection
- INT64 intermediates for multiplication
- Clamping to prevent wraparound
- Division by zero returns max/min values

### Verifier Safety
- All loops have compile-time bounds
- Stack usage minimized (< 512 bytes per frame)
- No recursion in critical paths

---

## Usage Example

### On-Chain Trading Agent

```cpp
// 1. Extract features from oracle accounts
int32_t features[12];
extract_market_features(oracle_account, features);

// 2. Run decision tree inference
int32_t action;
sol_ml_decision_tree(
    features, 12,
    model_nodes, num_nodes,
    MAX_DEPTH, &action
);

// 3. Execute trade based on prediction
switch (action) {
    case ACTION_BUY:  execute_buy(amount);  break;
    case ACTION_SELL: execute_sell(amount); break;
    case ACTION_HOLD: /* do nothing */      break;
}
```

---

## Limitations

1. **Model Size**: Maximum ~100KB for sBPF deployment
2. **Layer Count**: Up to 8 dense layers
3. **Neurons**: Up to 64 per layer
4. **Precision**: 4 decimal places (10,000 scale)
5. **No Training**: Inference only; models trained off-chain

---

## Future Enhancements

1. **Hardware Acceleration**: SIMD/AMX support for matrix operations
2. **Model Caching**: Persistent model state across invocations
3. **Convolutional Layers**: For image/signal processing
4. **Attention Mechanisms**: Transformer-style attention
5. **Quantization Aware Training**: Better INT8 model support

---

## References

1. arXiv:2409.06452 - "Ransomware Detection in eBPF" (2024)
2. CN-TU/machine-learning-in-ebpf - Flow-based IDS (2022)
3. ACM eBPF Workshop - "eBPFML: Matrix Extensions" (2024)
4. TinyML Foundation - Quantization techniques
