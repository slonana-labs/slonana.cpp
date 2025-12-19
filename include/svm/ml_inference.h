#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <array>

namespace slonana {
namespace svm {

/**
 * ML Inference in eBPF/sBPF Runtime
 *
 * This module provides machine learning inference capabilities designed
 * for execution within eBPF/sBPF constrained environments.
 *
 * Key Design Principles:
 * 1. Fixed-point arithmetic - All floating-point converted to INT8/INT16/INT32
 * 2. Bounded operations - All loops have compile-time bounds for verifier safety
 * 3. Verifier-safe - No unbounded loops, limited stack usage
 * 4. TinyML compatible - Models <100KB for eBPF deployment
 *
 * Supported Model Types:
 * - Decision Trees (proven 93ns latency, 7.1x faster than C)
 * - Multi-Layer Perceptrons (small neural networks)
 * - Quantized INT8/INT16 models
 *
 * Performance Targets (based on research):
 * - Decision tree inference: <100ns
 * - Small MLP inference: <1μs
 * - Model size: <100KB
 */

// ============================================================================
// Fixed-Point Arithmetic Configuration
// ============================================================================

namespace fixed_point {
    // Default scale factor for fixed-point representation
    // Scale of 10000 provides 4 decimal places of precision
    constexpr int32_t SCALE = 10000;
    
    // Alternative scale factors for different precision requirements
    constexpr int32_t SCALE_HIGH = 1000000;  // 6 decimal places (INT64 required)
    constexpr int32_t SCALE_LOW = 100;       // 2 decimal places (faster)
    
    // INT8 quantization scale (for memory-efficient models)
    constexpr int16_t SCALE_INT8 = 127;  // Maps [-1, 1] to [-127, 127]
    
    // Maximum values to prevent overflow in multiplication
    constexpr int32_t MAX_SAFE_MUL_INT32 = 46340;  // sqrt(INT32_MAX)
    constexpr int64_t MAX_SAFE_MUL_INT64 = 3037000499LL;  // sqrt(INT64_MAX)
    
    /**
     * Convert floating-point to fixed-point
     */
    constexpr int32_t from_float(float f, int32_t scale = SCALE) {
        return static_cast<int32_t>(f * scale);
    }
    
    /**
     * Convert fixed-point to floating-point (for debugging only)
     */
    constexpr float to_float(int32_t fixed, int32_t scale = SCALE) {
        return static_cast<float>(fixed) / scale;
    }
    
    /**
     * Fixed-point multiplication with overflow protection
     */
    inline int32_t multiply(int32_t a, int32_t b, int32_t scale = SCALE) {
        // Use int64 intermediate to prevent overflow
        int64_t result = (static_cast<int64_t>(a) * static_cast<int64_t>(b)) / scale;
        // Clamp to int32 range
        if (result > INT32_MAX) return INT32_MAX;
        if (result < INT32_MIN) return INT32_MIN;
        return static_cast<int32_t>(result);
    }
    
    /**
     * Fixed-point division with overflow protection
     */
    inline int32_t divide(int32_t a, int32_t b, int32_t scale = SCALE) {
        if (b == 0) return (a >= 0) ? INT32_MAX : INT32_MIN;
        int64_t result = (static_cast<int64_t>(a) * scale) / b;
        if (result > INT32_MAX) return INT32_MAX;
        if (result < INT32_MIN) return INT32_MIN;
        return static_cast<int32_t>(result);
    }
} // namespace fixed_point

// ============================================================================
// Model Structures
// ============================================================================

/**
 * Maximum dimensions for eBPF-safe inference
 * These limits ensure bounded loops pass verifier
 */
constexpr size_t ML_MAX_LAYERS = 8;
constexpr size_t ML_MAX_NEURONS_PER_LAYER = 64;
constexpr size_t ML_MAX_INPUT_SIZE = 64;
constexpr size_t ML_MAX_OUTPUT_SIZE = 16;
constexpr size_t ML_MAX_TREE_DEPTH = 32;
constexpr size_t ML_MAX_TREE_NODES = 256;

/**
 * Activation function types
 */
enum class ActivationType : uint8_t {
    NONE = 0,      // Linear (no activation)
    RELU = 1,      // max(0, x)
    SIGMOID = 2,   // 1 / (1 + exp(-x)) approximated
    TANH = 3,      // tanh(x) approximated
    SOFTMAX = 4,   // exp(x) / sum(exp(x)) approximated
    LEAKY_RELU = 5 // x if x > 0 else 0.01x
};

/**
 * Dense (fully connected) layer weights
 * Stored in row-major order: weights[output_neuron][input_neuron]
 */
struct DenseLayerWeights {
    uint16_t input_size;    // Number of input neurons
    uint16_t output_size;   // Number of output neurons
    ActivationType activation;
    uint8_t padding[3];
    
    // Weights stored as INT16 (quantized from float)
    // Total size: input_size * output_size * 2 bytes
    std::vector<int16_t> weights;
    
    // Bias stored as INT32 for precision
    std::vector<int32_t> biases;
    
    DenseLayerWeights() : input_size(0), output_size(0), 
                          activation(ActivationType::NONE) {}
    
    DenseLayerWeights(uint16_t in, uint16_t out, ActivationType act = ActivationType::RELU)
        : input_size(in), output_size(out), activation(act) {
        weights.resize(static_cast<size_t>(in) * out, 0);
        biases.resize(out, 0);
    }
    
    size_t get_weight_count() const {
        return static_cast<size_t>(input_size) * output_size;
    }
    
    size_t get_memory_size() const {
        return weights.size() * sizeof(int16_t) + biases.size() * sizeof(int32_t);
    }
};

/**
 * Multi-Layer Perceptron model structure
 */
struct MLPModel {
    uint32_t version;           // Model version for updates
    uint32_t input_size;        // Input feature count
    uint32_t output_size;       // Output class/value count
    uint8_t num_layers;         // Number of dense layers
    uint8_t quantization_bits;  // 8 or 16
    uint16_t padding;
    uint64_t timestamp;         // Last update timestamp
    
    std::vector<DenseLayerWeights> layers;
    
    MLPModel() : version(0), input_size(0), output_size(0), 
                 num_layers(0), quantization_bits(16), timestamp(0) {}
    
    bool is_valid() const {
        return num_layers > 0 && 
               num_layers <= ML_MAX_LAYERS &&
               input_size <= ML_MAX_INPUT_SIZE &&
               output_size <= ML_MAX_OUTPUT_SIZE;
    }
    
    size_t get_total_parameters() const {
        size_t total = 0;
        for (const auto& layer : layers) {
            total += layer.get_weight_count() + layer.biases.size();
        }
        return total;
    }
    
    size_t get_memory_size() const {
        size_t total = sizeof(MLPModel) - sizeof(layers);
        for (const auto& layer : layers) {
            total += layer.get_memory_size();
        }
        return total;
    }
};

/**
 * Decision tree node structure
 * Optimized for cache-friendly traversal
 */
struct DecisionTreeNode {
    int16_t feature_index;     // Which feature to compare (-1 = leaf)
    int16_t left_child;        // Index of left child (-1 = none)
    int16_t right_child;       // Index of right child (-1 = none)
    int16_t padding;
    int32_t threshold;         // Split threshold (fixed-point)
    int32_t leaf_value;        // Classification result (if leaf)
    
    DecisionTreeNode() : feature_index(-1), left_child(-1), right_child(-1),
                         threshold(0), leaf_value(0) {}
    
    bool is_leaf() const { return feature_index < 0; }
};

/**
 * Decision tree model structure
 */
struct DecisionTreeModel {
    uint32_t version;
    uint16_t num_nodes;
    uint16_t max_depth;
    uint32_t num_features;
    uint32_t num_classes;
    uint64_t timestamp;
    
    std::vector<DecisionTreeNode> nodes;
    
    DecisionTreeModel() : version(0), num_nodes(0), max_depth(0),
                          num_features(0), num_classes(0), timestamp(0) {}
    
    bool is_valid() const {
        return num_nodes > 0 && 
               num_nodes <= ML_MAX_TREE_NODES &&
               max_depth <= ML_MAX_TREE_DEPTH &&
               num_features <= ML_MAX_INPUT_SIZE;
    }
};

// ============================================================================
// Inference Context
// ============================================================================

/**
 * ML Inference execution context
 */
struct MLInferenceContext {
    // Input/output buffers (fixed-point values)
    std::array<int32_t, ML_MAX_INPUT_SIZE> input;
    std::array<int32_t, ML_MAX_OUTPUT_SIZE> output;
    
    // Intermediate buffers for layer outputs
    std::array<int32_t, ML_MAX_NEURONS_PER_LAYER> hidden1;
    std::array<int32_t, ML_MAX_NEURONS_PER_LAYER> hidden2;
    
    // Execution stats
    uint64_t compute_units_consumed;
    uint64_t inference_latency_ns;
    bool success;
    
    // Input/output sizes for current inference
    uint16_t input_size;
    uint16_t output_size;
    
    MLInferenceContext() : compute_units_consumed(0), inference_latency_ns(0),
                           success(false), input_size(0), output_size(0) {
        input.fill(0);
        output.fill(0);
        hidden1.fill(0);
        hidden2.fill(0);
    }
};

// ============================================================================
// ML Inference Syscalls
// ============================================================================

/**
 * Matrix multiplication syscall (fixed-point)
 *
 * Performs: output = input * weights + bias
 *
 * @param input Pointer to input vector (int32_t[cols])
 * @param input_len Number of input elements
 * @param weights Pointer to weight matrix (int16_t[rows * cols] in row-major order)
 * @param weights_len Total weight count
 * @param bias Pointer to bias vector (int32_t[rows])
 * @param bias_len Number of bias elements
 * @param output Pointer to output vector (int32_t[rows])
 * @param output_len Pointer to output size
 * @param rows Number of output neurons
 * @param cols Number of input neurons
 * @return 0 on success, error code otherwise
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
    uint32_t cols);

/**
 * Apply activation function in-place
 *
 * @param data Pointer to data vector
 * @param len Number of elements
 * @param activation Activation type
 * @return 0 on success, error code otherwise
 */
uint64_t sol_ml_activation(
    int32_t* data,
    uint64_t len,
    uint8_t activation);

/**
 * Decision tree inference syscall
 *
 * @param features Pointer to feature vector (int32_t[num_features])
 * @param features_len Number of features
 * @param tree_nodes Pointer to tree node array
 * @param num_nodes Number of nodes in tree
 * @param max_depth Maximum tree depth (for bounded traversal)
 * @param result Pointer to classification result
 * @return 0 on success, error code otherwise
 */
uint64_t sol_ml_decision_tree(
    const int32_t* features,
    uint64_t features_len,
    const DecisionTreeNode* tree_nodes,
    uint64_t num_nodes,
    uint32_t max_depth,
    int32_t* result);

/**
 * MLP forward pass syscall
 *
 * Performs complete forward pass through a multi-layer perceptron.
 *
 * @param input Pointer to input features (int32_t[input_size])
 * @param input_len Number of input features
 * @param model Pointer to serialized MLP model
 * @param model_len Size of model data in bytes
 * @param output Pointer to output buffer
 * @param output_len Pointer to output size
 * @return 0 on success, error code otherwise
 */
uint64_t sol_ml_forward(
    const int32_t* input,
    uint64_t input_len,
    const uint8_t* model,
    uint64_t model_len,
    int32_t* output,
    uint64_t* output_len);

// ============================================================================
// Activation Function Implementations
// ============================================================================

namespace activation {
    /**
     * ReLU activation (fast, no approximation needed)
     */
    inline int32_t relu(int32_t x) {
        return x > 0 ? x : 0;
    }
    
    /**
     * Leaky ReLU activation
     * For x < 0: returns x * 0.01 (approximated as x >> 7)
     */
    inline int32_t leaky_relu(int32_t x) {
        return x > 0 ? x : (x >> 7);  // Approximately 0.0078x for negative
    }
    
    /**
     * Sigmoid approximation using piecewise linear
     * Maps input to [0, SCALE] range
     *
     * Approximation regions:
     * - x < -3*SCALE: return 0
     * - x > 3*SCALE: return SCALE
     * - otherwise: linear interpolation
     */
    inline int32_t sigmoid_approx(int32_t x, int32_t scale = fixed_point::SCALE) {
        const int32_t threshold = 3 * scale;
        
        if (x < -threshold) return 0;
        if (x > threshold) return scale;
        
        // Linear approximation in middle range
        // sigmoid ≈ 0.5 + x/6 for small x
        return (scale / 2) + (x / 6);
    }
    
    /**
     * Sigmoid using lookup table (more accurate)
     * Table has 256 entries mapping input range to [0, SCALE]
     */
    extern const int32_t SIGMOID_LUT[256];
    
    inline int32_t sigmoid_lut(int32_t x, int32_t scale = fixed_point::SCALE) {
        // Map x from approximately [-32768, 32767] to [0, 255]
        int index = ((x + 32768) >> 8) & 0xFF;
        // Scale the lookup result
        return (SIGMOID_LUT[index] * scale) / 10000;
    }
    
    /**
     * Tanh approximation using sigmoid
     * tanh(x) = 2*sigmoid(2x) - 1
     */
    inline int32_t tanh_approx(int32_t x, int32_t scale = fixed_point::SCALE) {
        int32_t sig = sigmoid_approx(2 * x, scale);
        return 2 * sig - scale;
    }
    
    /**
     * Softmax approximation for small arrays
     * Computes normalized exponential distribution
     * Warning: Modifies input array in-place
     */
    void softmax_approx(int32_t* data, size_t len, int32_t scale = fixed_point::SCALE);
    
    /**
     * Apply activation function to array
     */
    void apply(int32_t* data, size_t len, ActivationType type, 
               int32_t scale = fixed_point::SCALE);
} // namespace activation

// ============================================================================
// Matrix Operations
// ============================================================================

namespace matrix {
    /**
     * Dense matrix-vector multiplication with bias
     * output[i] = sum_j(input[j] * weights[i*cols + j]) + bias[i]
     *
     * @param output Output vector (size: rows)
     * @param input Input vector (size: cols)
     * @param weights Weight matrix in row-major order (size: rows * cols)
     * @param bias Bias vector (size: rows)
     * @param rows Number of output neurons
     * @param cols Number of input neurons
     */
    void dense_forward(
        int32_t* output,
        const int32_t* input,
        const int16_t* weights,
        const int32_t* bias,
        size_t rows,
        size_t cols);
    
    /**
     * Argmax - find index of maximum value
     */
    size_t argmax(const int32_t* data, size_t len);
    
    /**
     * Find maximum value in array
     */
    int32_t max_value(const int32_t* data, size_t len);
} // namespace matrix

// ============================================================================
// Compute Unit Costs for ML Operations
// ============================================================================

namespace ml_compute_costs {
    // Per-operation costs (tuned for sBPF budget)
    constexpr uint64_t MATMUL_BASE = 10;          // Base cost for matmul setup
    constexpr uint64_t MATMUL_PER_MULT = 1;       // Cost per multiplication
    constexpr uint64_t ACTIVATION_PER_ELEMENT = 1; // Cost per activation
    constexpr uint64_t TREE_TRAVERSAL_PER_NODE = 2; // Cost per tree node visited
    constexpr uint64_t SOFTMAX_BASE = 20;         // Base cost for softmax
    constexpr uint64_t SOFTMAX_PER_ELEMENT = 5;   // Per-element softmax cost
    
    /**
     * Estimate compute units for dense layer
     */
    inline uint64_t estimate_dense_layer(size_t input_size, size_t output_size, 
                                         ActivationType activation) {
        uint64_t matmul_cost = MATMUL_BASE + 
                              (input_size * output_size * MATMUL_PER_MULT);
        uint64_t activation_cost = output_size * ACTIVATION_PER_ELEMENT;
        
        if (activation == ActivationType::SOFTMAX) {
            activation_cost = SOFTMAX_BASE + (output_size * SOFTMAX_PER_ELEMENT);
        }
        
        return matmul_cost + activation_cost;
    }
    
    /**
     * Estimate compute units for decision tree
     */
    inline uint64_t estimate_decision_tree(size_t max_depth) {
        return max_depth * TREE_TRAVERSAL_PER_NODE;
    }
} // namespace ml_compute_costs

// ============================================================================
// Model Serialization
// ============================================================================

namespace model_io {
    /**
     * Serialize MLP model to byte array
     */
    std::vector<uint8_t> serialize_mlp(const MLPModel& model);
    
    /**
     * Deserialize MLP model from byte array
     */
    bool deserialize_mlp(const uint8_t* data, size_t len, MLPModel& model);
    
    /**
     * Serialize decision tree model to byte array
     */
    std::vector<uint8_t> serialize_decision_tree(const DecisionTreeModel& model);
    
    /**
     * Deserialize decision tree model from byte array
     */
    bool deserialize_decision_tree(const uint8_t* data, size_t len, 
                                   DecisionTreeModel& model);
} // namespace model_io

} // namespace svm
} // namespace slonana
