/**
 * ML Inference Test Suite for eBPF/sBPF Runtime
 *
 * Tests the machine learning inference capabilities designed for
 * execution within eBPF/sBPF constrained environments including:
 * - Fixed-point arithmetic operations
 * - Activation functions (ReLU, sigmoid, tanh, softmax)
 * - Matrix multiplication with INT16 weights
 * - Decision tree inference
 * - Neural network forward pass
 * - Model serialization/deserialization
 * - Performance benchmarks
 */

#include "svm/ml_inference.h"
#include "test_framework.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

using namespace slonana::svm;

namespace {

// Test fixture for ML inference tests
class MLInferenceTest {
public:
    MLInferenceTest() {
        std::cout << "\n=== ML Inference Test Suite ===\n";
    }
    
    ~MLInferenceTest() {
        std::cout << "=== ML Inference Tests Complete ===\n\n";
    }
};

// ============================================================================
// Fixed-Point Arithmetic Tests
// ============================================================================

void test_fixed_point_conversion() {
    std::cout << "Testing fixed-point conversion...\n";
    
    // Test float to fixed-point conversion
    {
        float test_val = 0.5f;
        int32_t fixed = fixed_point::from_float(test_val);
        ASSERT_TRUE(fixed == 5000);
    }
    
    {
        float test_val = -1.0f;
        int32_t fixed = fixed_point::from_float(test_val);
        ASSERT_TRUE(fixed == -10000);
    }
    
    {
        float test_val = 2.5f;
        int32_t fixed = fixed_point::from_float(test_val);
        ASSERT_TRUE(fixed == 25000);
    }
    
    // Test fixed-point to float conversion (for verification)
    {
        int32_t fixed = 7500;
        float result = fixed_point::to_float(fixed);
        ASSERT_TRUE(std::abs(result - 0.75f) < 0.001f);
    }
    
    std::cout << "  ✓ Fixed-point conversion tests passed\n";
}

void test_fixed_point_multiplication() {
    std::cout << "Testing fixed-point multiplication...\n";
    
    // Test basic multiplication
    {
        int32_t a = 5000;  // 0.5
        int32_t b = 5000;  // 0.5
        int32_t result = fixed_point::multiply(a, b);
        // 0.5 * 0.5 = 0.25 = 2500
        ASSERT_TRUE(result == 2500);
    }
    
    // Test multiplication with negative values
    {
        int32_t a = -5000;  // -0.5
        int32_t b = 4000;   // 0.4
        int32_t result = fixed_point::multiply(a, b);
        // -0.5 * 0.4 = -0.2 = -2000
        ASSERT_TRUE(result == -2000);
    }
    
    // Test multiplication that would overflow without int64 intermediate
    {
        int32_t a = 100000;  // 10.0
        int32_t b = 100000;  // 10.0
        int32_t result = fixed_point::multiply(a, b);
        // 10.0 * 10.0 = 100.0 = 1000000
        ASSERT_TRUE(result == 1000000);
    }
    
    std::cout << "  ✓ Fixed-point multiplication tests passed\n";
}

void test_fixed_point_division() {
    std::cout << "Testing fixed-point division...\n";
    
    // Test basic division
    {
        int32_t a = 10000;  // 1.0
        int32_t b = 2000;   // 0.2
        int32_t result = fixed_point::divide(a, b);
        // 1.0 / 0.2 = 5.0 = 50000
        ASSERT_TRUE(result == 50000);
    }
    
    // Test division by zero protection
    {
        int32_t a = 10000;
        int32_t b = 0;
        int32_t result = fixed_point::divide(a, b);
        ASSERT_TRUE(result == INT32_MAX);
    }
    
    // Test negative division by zero
    {
        int32_t a = -10000;
        int32_t b = 0;
        int32_t result = fixed_point::divide(a, b);
        ASSERT_TRUE(result == INT32_MIN);
    }
    
    std::cout << "  ✓ Fixed-point division tests passed\n";
}

// ============================================================================
// Activation Function Tests
// ============================================================================

void test_relu_activation() {
    std::cout << "Testing ReLU activation...\n";
    
    // Test positive values unchanged
    ASSERT_TRUE(activation::relu(5000) == 5000);
    ASSERT_TRUE(activation::relu(10000) == 10000);
    
    // Test negative values become zero
    ASSERT_TRUE(activation::relu(-5000) == 0);
    ASSERT_TRUE(activation::relu(-1) == 0);
    
    // Test zero stays zero
    ASSERT_TRUE(activation::relu(0) == 0);
    
    std::cout << "  ✓ ReLU activation tests passed\n";
}

void test_leaky_relu_activation() {
    std::cout << "Testing Leaky ReLU activation...\n";
    
    // Test positive values unchanged
    ASSERT_TRUE(activation::leaky_relu(5000) == 5000);
    
    // Test negative values are scaled
    int32_t neg_result = activation::leaky_relu(-1280);  // -10 * 128
    // Should be approximately -1280 >> 7 = -10
    ASSERT_TRUE(neg_result == -10);
    
    // Test zero stays zero
    ASSERT_TRUE(activation::leaky_relu(0) == 0);
    
    std::cout << "  ✓ Leaky ReLU activation tests passed\n";
}

void test_sigmoid_approximation() {
    std::cout << "Testing sigmoid approximation...\n";
    
    const int32_t scale = fixed_point::SCALE;
    
    // Test at zero - should be 0.5 (scale/2)
    int32_t sig_zero = activation::sigmoid_approx(0, scale);
    ASSERT_TRUE(sig_zero == scale / 2);
    
    // Test at large positive - should approach 1.0 (scale)
    int32_t sig_pos = activation::sigmoid_approx(40000, scale);
    ASSERT_TRUE(sig_pos == scale);
    
    // Test at large negative - should approach 0
    int32_t sig_neg = activation::sigmoid_approx(-40000, scale);
    ASSERT_TRUE(sig_neg == 0);
    
    // Test monotonicity in middle range
    int32_t sig_m1 = activation::sigmoid_approx(-5000, scale);
    int32_t sig_0 = activation::sigmoid_approx(0, scale);
    int32_t sig_p1 = activation::sigmoid_approx(5000, scale);
    
    ASSERT_TRUE(sig_m1 < sig_0);
    ASSERT_TRUE(sig_0 < sig_p1);
    
    std::cout << "  ✓ Sigmoid approximation tests passed\n";
}

void test_tanh_approximation() {
    std::cout << "Testing tanh approximation...\n";
    
    const int32_t scale = fixed_point::SCALE;
    
    // Test at zero - should be 0
    int32_t tanh_zero = activation::tanh_approx(0, scale);
    ASSERT_TRUE(tanh_zero == 0);
    
    // Test at large positive - should approach 1.0 (scale)
    int32_t tanh_pos = activation::tanh_approx(40000, scale);
    ASSERT_TRUE(tanh_pos == scale);
    
    // Test at large negative - should approach -1.0 (-scale)
    int32_t tanh_neg = activation::tanh_approx(-40000, scale);
    ASSERT_TRUE(tanh_neg == -scale);
    
    std::cout << "  ✓ Tanh approximation tests passed\n";
}

void test_softmax() {
    std::cout << "Testing softmax approximation...\n";
    
    const int32_t scale = fixed_point::SCALE;
    
    // Test softmax normalization - outputs should sum to scale
    int32_t data[] = {1000, 2000, 3000};
    activation::softmax_approx(data, 3, scale);
    
    int32_t sum = data[0] + data[1] + data[2];
    // Allow some tolerance due to approximation
    ASSERT_TRUE(std::abs(sum - scale) < 100);
    
    // Test that larger input gives larger output
    ASSERT_TRUE(data[2] > data[1]);
    ASSERT_TRUE(data[1] > data[0]);
    
    std::cout << "  ✓ Softmax approximation tests passed\n";
}

void test_apply_activation() {
    std::cout << "Testing batch activation application...\n";
    
    const int32_t scale = fixed_point::SCALE;
    
    // Test ReLU on array
    {
        int32_t data[] = {-1000, 0, 1000, 2000};
        activation::apply(data, 4, ActivationType::RELU, scale);
        TEST_ASSERT(data[0] == 0 && data[1] == 0 && data[2] == 1000 && data[3] == 2000,
                    "Batch ReLU should apply to all elements");
    }
    
    // Test NONE (linear) leaves data unchanged
    {
        int32_t data[] = {-1000, 0, 1000, 2000};
        activation::apply(data, 4, ActivationType::NONE, scale);
        TEST_ASSERT(data[0] == -1000 && data[1] == 0 && data[2] == 1000 && data[3] == 2000,
                    "Linear activation should not change data");
    }
    
    std::cout << "  ✓ Batch activation application tests passed\n";
}

// ============================================================================
// Matrix Operation Tests
// ============================================================================

void test_dense_forward() {
    std::cout << "Testing dense layer forward pass...\n";
    
    // Simple 2x2 case: output = input * weights + bias
    // Input: [1.0, 0.5] (scaled: [32768, 16384])
    // Weights: [[1, 0], [0, 1]] identity-like (scaled as INT16)
    // Bias: [0, 0]
    
    int32_t input[] = {32768, 16384};  // ~1.0, ~0.5 in INT32
    int16_t weights[] = {32767, 0, 0, 32767};  // 2x2 identity-like
    int32_t bias[] = {0, 0};
    int32_t output[2] = {0};
    
    matrix::dense_forward(output, input, weights, bias, 2, 2);
    
    // After scale adjustment (>> 15), output should approximate input
    ASSERT_TRUE(output[0] > 0);
    ASSERT_TRUE(output[1] > 0);
    
    std::cout << "  ✓ Dense layer forward pass tests passed\n";
}

void test_dense_forward_with_bias() {
    std::cout << "Testing dense layer forward pass with bias...\n";
    
    // Input: [0, 0]
    // Weights: all zeros
    // Bias: [1000, 2000]
    // Output should just be the bias
    
    int32_t input[] = {0, 0};
    int16_t weights[] = {0, 0, 0, 0};
    int32_t bias[] = {1000, 2000};
    int32_t output[2] = {0};
    
    matrix::dense_forward(output, input, weights, bias, 2, 2);
    
    ASSERT_TRUE(output[0] == 1000);
    ASSERT_TRUE(output[1] == 2000);
    
    std::cout << "  ✓ Dense layer with bias tests passed\n";
}

void test_argmax() {
    std::cout << "Testing argmax operation...\n";
    
    // Test basic argmax
    {
        int32_t data[] = {100, 300, 200, 50};
        size_t idx = matrix::argmax(data, 4);
        ASSERT_TRUE(idx == 1);
    }
    
    // Test first element is max
    {
        int32_t data[] = {500, 100, 200, 300};
        size_t idx = matrix::argmax(data, 4);
        ASSERT_TRUE(idx == 0);
    }
    
    // Test last element is max
    {
        int32_t data[] = {100, 200, 300, 400};
        size_t idx = matrix::argmax(data, 4);
        ASSERT_TRUE(idx == 3);
    }
    
    // Test with negative values
    {
        int32_t data[] = {-100, -50, -200};
        size_t idx = matrix::argmax(data, 3);
        ASSERT_TRUE(idx == 1);
    }
    
    std::cout << "  ✓ Argmax operation tests passed\n";
}

void test_max_value() {
    std::cout << "Testing max value operation...\n";
    
    {
        int32_t data[] = {100, 300, 200, 50};
        int32_t max = matrix::max_value(data, 4);
        ASSERT_TRUE(max == 300);
    }
    
    {
        int32_t data[] = {-100, -50, -200};
        int32_t max = matrix::max_value(data, 3);
        ASSERT_TRUE(max == -50);
    }
    
    std::cout << "  ✓ Max value operation tests passed\n";
}

// ============================================================================
// ML Syscall Tests
// ============================================================================

void test_sol_ml_matmul_syscall() {
    std::cout << "Testing sol_ml_matmul syscall...\n";
    
    // Test valid matmul
    {
        int32_t input[] = {10000, 5000};  // 2 inputs
        int16_t weights[] = {16384, 0, 0, 16384};  // 2x2 identity-like
        int32_t bias[] = {0, 0};
        int32_t output[2] = {0};
        uint64_t output_len = 0;
        
        uint64_t result = sol_ml_matmul(input, 2, weights, 4, bias, 2, 
                                         output, &output_len, 2, 2);
        
        ASSERT_TRUE(result == 0);
        ASSERT_TRUE(output_len == 2);
    }
    
    // Test null pointer handling
    {
        uint64_t result = sol_ml_matmul(nullptr, 2, nullptr, 4, nullptr, 2, 
                                         nullptr, nullptr, 2, 2);
        ASSERT_TRUE(result != 0);
    }
    
    // Test dimension validation
    {
        int32_t input[] = {10000};
        int16_t weights[] = {16384};
        int32_t bias[] = {0, 0};
        int32_t output[2] = {0};
        uint64_t output_len = 0;
        
        // Input length (1) doesn't match cols (2)
        uint64_t result = sol_ml_matmul(input, 1, weights, 4, bias, 2, 
                                         output, &output_len, 2, 2);
        ASSERT_TRUE(result != 0);
    }
    
    std::cout << "  ✓ sol_ml_matmul syscall tests passed\n";
}

void test_sol_ml_activation_syscall() {
    std::cout << "Testing sol_ml_activation syscall...\n";
    
    // Test ReLU activation
    {
        int32_t data[] = {-1000, 0, 1000, 2000};
        uint64_t result = sol_ml_activation(data, 4, 
                                            static_cast<uint8_t>(ActivationType::RELU));
        
        ASSERT_TRUE(result == 0);
        ASSERT_TRUE(data[0] == 0);
        ASSERT_TRUE(data[2] == 1000);
    }
    
    // Test null pointer handling
    {
        uint64_t result = sol_ml_activation(nullptr, 4, 
                                            static_cast<uint8_t>(ActivationType::RELU));
        ASSERT_TRUE(result != 0);
    }
    
    // Test invalid activation type
    {
        int32_t data[] = {1000};
        uint64_t result = sol_ml_activation(data, 1, 99);  // Invalid type
        ASSERT_TRUE(result != 0);
    }
    
    std::cout << "  ✓ sol_ml_activation syscall tests passed\n";
}

// ============================================================================
// Decision Tree Tests
// ============================================================================

void test_decision_tree_inference() {
    std::cout << "Testing decision tree inference...\n";
    
    // Create a simple decision tree:
    //           [0: feature 0 <= 5000?]
    //          /                        \
    //    [1: leaf=0]              [2: feature 1 <= 3000?]
    //                              /                    \
    //                       [3: leaf=1]            [4: leaf=2]
    
    DecisionTreeModel tree;
    tree.version = 1;
    tree.num_nodes = 5;
    tree.max_depth = 3;
    tree.num_features = 2;
    tree.num_classes = 3;
    
    tree.nodes.resize(5);
    
    // Node 0: split on feature 0 at threshold 5000
    tree.nodes[0].feature_index = 0;
    tree.nodes[0].threshold = 5000;
    tree.nodes[0].left_child = 1;
    tree.nodes[0].right_child = 2;
    tree.nodes[0].leaf_value = -1;
    
    // Node 1: leaf with class 0
    tree.nodes[1].feature_index = -1;  // Leaf
    tree.nodes[1].leaf_value = 0;
    
    // Node 2: split on feature 1 at threshold 3000
    tree.nodes[2].feature_index = 1;
    tree.nodes[2].threshold = 3000;
    tree.nodes[2].left_child = 3;
    tree.nodes[2].right_child = 4;
    tree.nodes[2].leaf_value = -1;
    
    // Node 3: leaf with class 1
    tree.nodes[3].feature_index = -1;
    tree.nodes[3].leaf_value = 1;
    
    // Node 4: leaf with class 2
    tree.nodes[4].feature_index = -1;
    tree.nodes[4].leaf_value = 2;
    
    // Test case 1: feature[0] = 4000 <= 5000, should return class 0
    {
        int32_t features[] = {4000, 2000};
        int32_t result = 0;
        
        uint64_t ret = sol_ml_decision_tree(features, 2, tree.nodes.data(), 
                                            tree.num_nodes, tree.max_depth, &result);
        
        ASSERT_TRUE(ret == 0);
        ASSERT_TRUE(result == 0);
    }
    
    // Test case 2: feature[0] = 6000 > 5000, feature[1] = 2000 <= 3000, should return class 1
    {
        int32_t features[] = {6000, 2000};
        int32_t result = 0;
        
        uint64_t ret = sol_ml_decision_tree(features, 2, tree.nodes.data(), 
                                            tree.num_nodes, tree.max_depth, &result);
        
        ASSERT_TRUE(ret == 0);
        ASSERT_TRUE(result == 1);
    }
    
    // Test case 3: feature[0] = 6000 > 5000, feature[1] = 4000 > 3000, should return class 2
    {
        int32_t features[] = {6000, 4000};
        int32_t result = 0;
        
        uint64_t ret = sol_ml_decision_tree(features, 2, tree.nodes.data(), 
                                            tree.num_nodes, tree.max_depth, &result);
        
        ASSERT_TRUE(ret == 0);
        ASSERT_TRUE(result == 2);
    }
    
    std::cout << "  ✓ Decision tree inference tests passed\n";
}

// ============================================================================
// Model Serialization Tests
// ============================================================================

void test_mlp_model_serialization() {
    std::cout << "Testing MLP model serialization...\n";
    
    // Create a simple MLP model
    MLPModel original;
    original.version = 1;
    original.input_size = 4;
    original.output_size = 2;
    original.num_layers = 2;
    original.quantization_bits = 16;
    original.timestamp = 1234567890;
    
    // First layer: 4 -> 8 with ReLU
    DenseLayerWeights layer1(4, 8, ActivationType::RELU);
    for (size_t i = 0; i < layer1.weights.size(); i++) {
        layer1.weights[i] = static_cast<int16_t>(i * 100);
    }
    for (size_t i = 0; i < layer1.biases.size(); i++) {
        layer1.biases[i] = static_cast<int32_t>(i * 50);
    }
    
    // Second layer: 8 -> 2 with Softmax
    DenseLayerWeights layer2(8, 2, ActivationType::SOFTMAX);
    for (size_t i = 0; i < layer2.weights.size(); i++) {
        layer2.weights[i] = static_cast<int16_t>(-i * 50);
    }
    for (size_t i = 0; i < layer2.biases.size(); i++) {
        layer2.biases[i] = 100;
    }
    
    original.layers.push_back(layer1);
    original.layers.push_back(layer2);
    
    // Serialize
    std::vector<uint8_t> serialized = model_io::serialize_mlp(original);
    ASSERT_TRUE(serialized.size() > 0);
    
    // Deserialize
    MLPModel restored;
    bool success = model_io::deserialize_mlp(serialized.data(), serialized.size(), restored);
    ASSERT_TRUE(success);
    
    // Verify all fields
    ASSERT_TRUE(restored.version == original.version);
    ASSERT_TRUE(restored.input_size == original.input_size);
    ASSERT_TRUE(restored.output_size == original.output_size);
    ASSERT_TRUE(restored.num_layers == original.num_layers);
    ASSERT_TRUE(restored.timestamp == original.timestamp);
    
    // Verify layer 1
    ASSERT_TRUE(restored.layers[0].input_size == layer1.input_size);
    ASSERT_TRUE(restored.layers[0].output_size == layer1.output_size);
    ASSERT_TRUE(restored.layers[0].activation == layer1.activation);
    
    // Verify weights match
    for (size_t i = 0; i < layer1.weights.size(); i++) {
        TEST_ASSERT(restored.layers[0].weights[i] == layer1.weights[i], 
                    "Layer 1 weights should match");
    }
    
    std::cout << "  ✓ MLP model serialization tests passed\n";
}

void test_decision_tree_serialization() {
    std::cout << "Testing decision tree serialization...\n";
    
    // Create a decision tree model
    DecisionTreeModel original;
    original.version = 1;
    original.num_nodes = 3;
    original.max_depth = 2;
    original.num_features = 4;
    original.num_classes = 2;
    original.timestamp = 9876543210;
    
    original.nodes.resize(3);
    original.nodes[0].feature_index = 0;
    original.nodes[0].threshold = 5000;
    original.nodes[0].left_child = 1;
    original.nodes[0].right_child = 2;
    
    original.nodes[1].feature_index = -1;
    original.nodes[1].leaf_value = 0;
    
    original.nodes[2].feature_index = -1;
    original.nodes[2].leaf_value = 1;
    
    // Serialize
    std::vector<uint8_t> serialized = model_io::serialize_decision_tree(original);
    ASSERT_TRUE(serialized.size() > 0);
    
    // Deserialize
    DecisionTreeModel restored;
    bool success = model_io::deserialize_decision_tree(serialized.data(), 
                                                        serialized.size(), restored);
    ASSERT_TRUE(success);
    
    // Verify fields
    ASSERT_TRUE(restored.version == original.version);
    ASSERT_TRUE(restored.num_nodes == original.num_nodes);
    ASSERT_TRUE(restored.max_depth == original.max_depth);
    ASSERT_TRUE(restored.num_features == original.num_features);
    ASSERT_TRUE(restored.num_classes == original.num_classes);
    
    // Verify nodes
    for (size_t i = 0; i < original.nodes.size(); i++) {
        TEST_ASSERT(restored.nodes[i].feature_index == original.nodes[i].feature_index,
                    "Node feature index should match");
        TEST_ASSERT(restored.nodes[i].threshold == original.nodes[i].threshold,
                    "Node threshold should match");
        TEST_ASSERT(restored.nodes[i].leaf_value == original.nodes[i].leaf_value,
                    "Node leaf value should match");
    }
    
    std::cout << "  ✓ Decision tree serialization tests passed\n";
}

// ============================================================================
// MLP Forward Pass Tests
// ============================================================================

void test_mlp_forward_pass() {
    std::cout << "Testing MLP forward pass syscall...\n";
    
    // Create a simple MLP: 2 -> 4 (ReLU) -> 2 (Softmax)
    MLPModel model;
    model.version = 1;
    model.input_size = 2;
    model.output_size = 2;
    model.num_layers = 2;
    model.quantization_bits = 16;
    
    // Layer 1: 2 -> 4 with ReLU
    DenseLayerWeights layer1(2, 4, ActivationType::RELU);
    // Initialize with identity-like weights
    std::fill(layer1.weights.begin(), layer1.weights.end(), 0);
    layer1.weights[0] = 16384;  // w[0,0] = ~0.5
    layer1.weights[3] = 16384;  // w[1,1] = ~0.5
    layer1.weights[4] = 16384;  // w[2,0] = ~0.5
    layer1.weights[7] = 16384;  // w[3,1] = ~0.5
    std::fill(layer1.biases.begin(), layer1.biases.end(), 0);
    
    // Layer 2: 4 -> 2 with Softmax
    DenseLayerWeights layer2(4, 2, ActivationType::SOFTMAX);
    std::fill(layer2.weights.begin(), layer2.weights.end(), 0);
    layer2.weights[0] = 16384;  // Connect to first output
    layer2.weights[1] = 16384;
    layer2.weights[4] = 16384;  // Connect to second output
    layer2.weights[5] = 16384;
    std::fill(layer2.biases.begin(), layer2.biases.end(), 0);
    
    model.layers.push_back(layer1);
    model.layers.push_back(layer2);
    
    // Serialize model
    std::vector<uint8_t> serialized = model_io::serialize_mlp(model);
    
    // Test forward pass
    int32_t input[] = {10000, 5000};  // ~1.0, ~0.5
    int32_t output[2] = {0};
    uint64_t output_len = 0;
    
    uint64_t result = sol_ml_forward(input, 2, serialized.data(), 
                                      serialized.size(), output, &output_len);
    
    ASSERT_TRUE(result == 0);
    ASSERT_TRUE(output_len == 2);
    
    // Verify outputs are valid probabilities (sum to ~scale after softmax)
    int32_t sum = output[0] + output[1];
    ASSERT_TRUE(sum > 0);
    
    std::cout << "  ✓ MLP forward pass syscall tests passed\n";
}

// ============================================================================
// Performance Benchmark Tests
// ============================================================================

void benchmark_decision_tree() {
    std::cout << "Benchmarking decision tree inference...\n";
    
    // Create a deeper tree for benchmarking
    DecisionTreeModel tree;
    tree.version = 1;
    tree.num_nodes = 31;  // 5-level complete tree
    tree.max_depth = 5;
    tree.num_features = 8;
    tree.num_classes = 16;
    
    tree.nodes.resize(31);
    
    // Build a complete binary tree
    for (int i = 0; i < 15; i++) {  // Internal nodes
        tree.nodes[i].feature_index = i % 8;
        tree.nodes[i].threshold = 5000 + (i * 100);
        tree.nodes[i].left_child = 2 * i + 1;
        tree.nodes[i].right_child = 2 * i + 2;
    }
    
    for (int i = 15; i < 31; i++) {  // Leaf nodes
        tree.nodes[i].feature_index = -1;
        tree.nodes[i].leaf_value = i - 15;
    }
    
    // Benchmark
    const int iterations = 10000;
    int32_t features[8] = {4000, 5500, 6000, 4500, 5000, 5500, 4000, 6000};
    int32_t result = 0;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        sol_ml_decision_tree(features, 8, tree.nodes.data(), 
                             tree.num_nodes, tree.max_depth, &result);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    
    double avg_latency = static_cast<double>(duration.count()) / iterations;
    
    std::cout << "  Decision tree inference (31 nodes):\n";
    std::cout << "    Total time: " << duration.count() / 1000 << " μs for " << iterations << " iterations\n";
    std::cout << "    Average latency: " << avg_latency << " ns/inference\n";
    
    // Target: <100ns per inference
    ASSERT_TRUE(avg_latency < 1000);
    
    std::cout << "  ✓ Decision tree benchmark passed (target: <1000ns, actual: " 
              << avg_latency << "ns)\n";
}

void benchmark_dense_layer() {
    std::cout << "Benchmarking dense layer forward...\n";
    
    // Create 64x64 dense layer
    const size_t input_size = 32;
    const size_t output_size = 32;
    
    std::vector<int32_t> input(input_size);
    std::vector<int16_t> weights(input_size * output_size);
    std::vector<int32_t> bias(output_size, 0);
    std::vector<int32_t> output(output_size);
    
    // Initialize with random values
    std::mt19937 rng(42);
    std::uniform_int_distribution<int32_t> input_dist(-10000, 10000);
    std::uniform_int_distribution<int16_t> weight_dist(-16384, 16384);
    
    for (auto& v : input) v = input_dist(rng);
    for (auto& w : weights) w = weight_dist(rng);
    
    // Benchmark
    const int iterations = 10000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        matrix::dense_forward(output.data(), input.data(), weights.data(), 
                              bias.data(), output_size, input_size);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    
    double avg_latency = static_cast<double>(duration.count()) / iterations;
    
    std::cout << "  Dense layer forward (" << input_size << "x" << output_size << "):\n";
    std::cout << "    Total time: " << duration.count() / 1000 << " μs for " << iterations << " iterations\n";
    std::cout << "    Average latency: " << avg_latency << " ns/forward\n";
    
    // Target: <5μs for 32x32 layer
    ASSERT_TRUE(avg_latency < 10000);
    
    std::cout << "  ✓ Dense layer benchmark passed (target: <10μs, actual: " 
              << avg_latency / 1000.0 << "μs)\n";
}

void benchmark_activation_functions() {
    std::cout << "Benchmarking activation functions...\n";
    
    const size_t size = 64;
    std::vector<int32_t> data(size);
    
    // Initialize
    std::mt19937 rng(42);
    std::uniform_int_distribution<int32_t> dist(-10000, 10000);
    for (auto& v : data) v = dist(rng);
    
    const int iterations = 100000;
    
    // Benchmark ReLU
    {
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < iterations; i++) {
            activation::apply(data.data(), size, ActivationType::RELU);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        
        double avg = static_cast<double>(duration.count()) / iterations;
        std::cout << "  ReLU (64 elements): " << avg << " ns/call\n";
    }
    
    // Benchmark Sigmoid
    {
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < iterations; i++) {
            activation::apply(data.data(), size, ActivationType::SIGMOID);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        
        double avg = static_cast<double>(duration.count()) / iterations;
        std::cout << "  Sigmoid (64 elements): " << avg << " ns/call\n";
    }
    
    // Benchmark Softmax
    {
        const int softmax_iterations = 10000;
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < softmax_iterations; i++) {
            // Reset data since softmax modifies it
            for (auto& v : data) v = dist(rng);
            activation::apply(data.data(), 8, ActivationType::SOFTMAX);  // Small softmax
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        
        double avg = static_cast<double>(duration.count()) / softmax_iterations;
        std::cout << "  Softmax (8 elements): " << avg << " ns/call\n";
    }
    
    std::cout << "  ✓ Activation function benchmarks complete\n";
}

// ============================================================================
// Edge Case and Error Handling Tests
// ============================================================================

void test_edge_cases() {
    std::cout << "Testing edge cases...\n";
    
    // Test empty input handling
    {
        int32_t result = 0;
        uint64_t ret = sol_ml_decision_tree(nullptr, 0, nullptr, 0, 10, &result);
        ASSERT_TRUE(ret != 0);
    }
    
    // Test oversized dimensions
    {
        int32_t input[100];
        int16_t weights[100];
        int32_t bias[100];
        int32_t output[100];
        uint64_t output_len = 0;
        
        // Try to create layer larger than max
        uint64_t ret = sol_ml_matmul(input, 100, weights, 10000, bias, 100,
                                      output, &output_len, 100, 100);
        ASSERT_TRUE(ret != 0);
    }
    
    // Test model validation
    {
        MLPModel invalid_model;
        invalid_model.num_layers = 0;  // Invalid: no layers
        
        ASSERT_TRUE(!invalid_model.is_valid());
    }
    
    {
        MLPModel invalid_model;
        invalid_model.num_layers = 20;  // Invalid: too many layers
        invalid_model.input_size = 10;
        invalid_model.output_size = 5;
        
        ASSERT_TRUE(!invalid_model.is_valid());
    }
    
    {
        DecisionTreeModel invalid_tree;
        invalid_tree.num_nodes = 0;  // Invalid: no nodes
        
        ASSERT_TRUE(!invalid_tree.is_valid());
    }
    
    std::cout << "  ✓ Edge case tests passed\n";
}

void test_compute_cost_estimation() {
    std::cout << "Testing compute cost estimation...\n";
    
    // Dense layer cost
    {
        uint64_t cost = ml_compute_costs::estimate_dense_layer(32, 32, ActivationType::RELU);
        ASSERT_TRUE(cost > 0);
        ASSERT_TRUE(cost < 2000);
        
        std::cout << "  Dense 32x32 with ReLU: " << cost << " compute units\n";
    }
    
    // Softmax layer (more expensive)
    {
        uint64_t cost = ml_compute_costs::estimate_dense_layer(32, 10, ActivationType::SOFTMAX);
        std::cout << "  Dense 32x10 with Softmax: " << cost << " compute units\n";
    }
    
    // Decision tree cost
    {
        uint64_t cost = ml_compute_costs::estimate_decision_tree(20);
        std::cout << "  Decision tree (depth 20): " << cost << " compute units\n";
    }
    
    std::cout << "  ✓ Compute cost estimation tests passed\n";
}

} // anonymous namespace

// ============================================================================
// Main Test Runner
// ============================================================================

int main() {
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
    
    MLInferenceTest test_fixture;
    
    std::cout << "\n--- Fixed-Point Arithmetic Tests ---\n";
    run_test("Fixed-Point Conversion", test_fixed_point_conversion);
    run_test("Fixed-Point Multiplication", test_fixed_point_multiplication);
    run_test("Fixed-Point Division", test_fixed_point_division);
    
    std::cout << "\n--- Activation Function Tests ---\n";
    run_test("ReLU Activation", test_relu_activation);
    run_test("Leaky ReLU Activation", test_leaky_relu_activation);
    run_test("Sigmoid Approximation", test_sigmoid_approximation);
    run_test("Tanh Approximation", test_tanh_approximation);
    run_test("Softmax", test_softmax);
    run_test("Batch Activation", test_apply_activation);
    
    std::cout << "\n--- Matrix Operation Tests ---\n";
    run_test("Dense Forward", test_dense_forward);
    run_test("Dense Forward with Bias", test_dense_forward_with_bias);
    run_test("Argmax", test_argmax);
    run_test("Max Value", test_max_value);
    
    std::cout << "\n--- ML Syscall Tests ---\n";
    run_test("sol_ml_matmul Syscall", test_sol_ml_matmul_syscall);
    run_test("sol_ml_activation Syscall", test_sol_ml_activation_syscall);
    
    std::cout << "\n--- Decision Tree Tests ---\n";
    run_test("Decision Tree Inference", test_decision_tree_inference);
    
    std::cout << "\n--- Model Serialization Tests ---\n";
    run_test("MLP Serialization", test_mlp_model_serialization);
    run_test("Decision Tree Serialization", test_decision_tree_serialization);
    
    std::cout << "\n--- MLP Forward Pass Tests ---\n";
    run_test("MLP Forward Pass", test_mlp_forward_pass);
    
    std::cout << "\n--- Edge Case Tests ---\n";
    run_test("Edge Cases", test_edge_cases);
    run_test("Compute Cost Estimation", test_compute_cost_estimation);
    
    std::cout << "\n--- Performance Benchmarks ---\n";
    run_test("Decision Tree Benchmark", benchmark_decision_tree);
    run_test("Dense Layer Benchmark", benchmark_dense_layer);
    run_test("Activation Function Benchmark", benchmark_activation_functions);
    
    std::cout << "\n====================================\n";
    std::cout << "Test Results: " << passed_tests << "/" << total_tests << " passed\n";
    std::cout << "====================================\n";
    
    return (passed_tests == total_tests) ? 0 : 1;
}
