#include "svm/ml_inference.h"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace slonana {
namespace svm {

// Error codes for ML inference syscalls
constexpr uint64_t ML_SUCCESS = 0;
constexpr uint64_t ML_ERROR_INVALID_INPUT_LENGTH = 1;
constexpr uint64_t ML_ERROR_INVALID_WEIGHTS = 2;
constexpr uint64_t ML_ERROR_DIMENSION_MISMATCH = 3;
constexpr uint64_t ML_ERROR_OVERFLOW = 4;
constexpr uint64_t ML_ERROR_INVALID_MODEL = 5;
constexpr uint64_t ML_ERROR_TREE_DEPTH_EXCEEDED = 6;
constexpr uint64_t ML_ERROR_NULL_POINTER = 7;
constexpr uint64_t ML_ERROR_INVALID_ACTIVATION = 8;

// ============================================================================
// Sigmoid Lookup Table
// ============================================================================

// 256-entry sigmoid lookup table mapping input index [0, 255] to [0, 10000]
// Index 0 = very negative, Index 128 = 0, Index 255 = very positive
// sigmoid(x) = 1 / (1 + exp(-x))
const int32_t activation::SIGMOID_LUT[256] = {
    // Indices 0-15: very negative inputs -> values near 0
    5, 6, 8, 10, 12, 15, 18, 22, 27, 33, 40, 49, 60, 73, 89, 109,
    // Indices 16-31
    133, 162, 198, 241, 294, 358, 437, 532, 648, 789, 959, 1165, 1413, 1710, 2066, 2488,
    // Indices 32-47
    2983, 3557, 4212, 4947, 5753, 6617, 7519, 8436, 9342, 10212, 11022, 11756, 12404, 12960, 13425, 13804,
    // Indices 48-63
    14103, 14333, 14504, 14625, 14707, 14759, 14789, 14803, 14808, 14805, 14798, 14788, 14777, 14765, 14753, 14742,
    // Indices 64-79
    6000, 6100, 6200, 6300, 6400, 6500, 6600, 6700, 6800, 6900, 7000, 7100, 7200, 7300, 7400, 7500,
    // Indices 80-95
    7600, 7700, 7800, 7900, 8000, 8100, 8200, 8300, 8400, 8500, 8600, 8700, 8800, 8900, 9000, 9100,
    // Indices 96-111
    9150, 9200, 9250, 9300, 9350, 9400, 9450, 9500, 9550, 9600, 9650, 9700, 9750, 9800, 9850, 9900,
    // Indices 112-127 (approaching center)
    9920, 9940, 9960, 9980, 9990, 9995, 9998, 5000, 5000, 5002, 5005, 5010, 5020, 5040, 5060, 5080,
    // Indices 128-143 (center and above)
    5100, 5150, 5200, 5250, 5300, 5350, 5400, 5450, 5500, 5550, 5600, 5650, 5700, 5750, 5800, 5850,
    // Indices 144-159
    5900, 5950, 6000, 6050, 6100, 6150, 6200, 6250, 6300, 6350, 6400, 6450, 6500, 6550, 6600, 6650,
    // Indices 160-175
    6700, 6750, 6800, 6850, 6900, 6950, 7000, 7050, 7100, 7150, 7200, 7250, 7300, 7350, 7400, 7450,
    // Indices 176-191
    7500, 7550, 7600, 7650, 7700, 7750, 7800, 7850, 7900, 7950, 8000, 8050, 8100, 8150, 8200, 8250,
    // Indices 192-207
    8300, 8400, 8500, 8600, 8700, 8800, 8900, 9000, 9100, 9200, 9300, 9400, 9500, 9600, 9700, 9800,
    // Indices 208-223
    9850, 9880, 9900, 9920, 9940, 9955, 9970, 9980, 9985, 9990, 9992, 9994, 9995, 9996, 9997, 9998,
    // Indices 224-239: very positive inputs -> values near 10000
    9999, 9999, 9999, 9999, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000,
    // Indices 240-255
    10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000
};

// ============================================================================
// Activation Functions Implementation
// ============================================================================

void activation::softmax_approx(int32_t* data, size_t len, int32_t scale) {
    if (len == 0 || len > ML_MAX_OUTPUT_SIZE) return;
    
    // Find max for numerical stability
    int32_t max_val = data[0];
    for (size_t i = 1; i < len; i++) {
        if (data[i] > max_val) max_val = data[i];
    }
    
    // Compute exp approximation and sum
    // Using exp(x) ≈ 1 + x + x²/2 for small x (after normalization)
    int64_t sum = 0;
    int32_t exp_vals[ML_MAX_OUTPUT_SIZE];
    
    for (size_t i = 0; i < len; i++) {
        int32_t x = data[i] - max_val;  // Normalized value (always <= 0)
        
        // Simplified exponential approximation
        // For very negative x, result approaches 0
        // For x near 0, result approaches scale
        if (x < -5 * scale) {
            exp_vals[i] = 1;  // Tiny but non-zero
        } else {
            // exp(x) ≈ scale * (1 + x/scale + (x/scale)²/2)
            int32_t normalized = x;
            int64_t exp_approx = scale;
            exp_approx += normalized;
            exp_approx += (static_cast<int64_t>(normalized) * normalized) / (2 * scale);
            exp_vals[i] = (exp_approx > 0) ? static_cast<int32_t>(exp_approx) : 1;
        }
        sum += exp_vals[i];
    }
    
    // Normalize to get probabilities
    if (sum > 0) {
        for (size_t i = 0; i < len; i++) {
            data[i] = static_cast<int32_t>((static_cast<int64_t>(exp_vals[i]) * scale) / sum);
        }
    }
}

void activation::apply(int32_t* data, size_t len, ActivationType type, int32_t scale) {
    switch (type) {
        case ActivationType::NONE:
            // No operation
            break;
            
        case ActivationType::RELU:
            for (size_t i = 0; i < len; i++) {
                data[i] = relu(data[i]);
            }
            break;
            
        case ActivationType::LEAKY_RELU:
            for (size_t i = 0; i < len; i++) {
                data[i] = leaky_relu(data[i]);
            }
            break;
            
        case ActivationType::SIGMOID:
            for (size_t i = 0; i < len; i++) {
                data[i] = sigmoid_approx(data[i], scale);
            }
            break;
            
        case ActivationType::TANH:
            for (size_t i = 0; i < len; i++) {
                data[i] = tanh_approx(data[i], scale);
            }
            break;
            
        case ActivationType::SOFTMAX:
            softmax_approx(data, len, scale);
            break;
    }
}

// ============================================================================
// Matrix Operations Implementation
// ============================================================================

void matrix::dense_forward(
    int32_t* output,
    const int32_t* input,
    const int16_t* weights,
    const int32_t* bias,
    size_t rows,
    size_t cols)
{
    // Bounded loop for verifier safety
    const size_t safe_rows = std::min(rows, ML_MAX_NEURONS_PER_LAYER);
    const size_t safe_cols = std::min(cols, ML_MAX_INPUT_SIZE);
    
    for (size_t i = 0; i < safe_rows; i++) {
        // Use int64 accumulator to prevent overflow
        int64_t sum = 0;
        
        for (size_t j = 0; j < safe_cols; j++) {
            // weights are INT16, input is INT32
            // Product fits in int64
            sum += static_cast<int64_t>(input[j]) * 
                   static_cast<int64_t>(weights[i * cols + j]);
        }
        
        // Scale back to INT32 (weights are scaled by 127 for INT8 or 32767 for INT16)
        // Assuming INT16 weights with scale ~32767, we divide by 32768
        sum = sum >> 15;  // Fast division by 32768
        
        // Add bias
        sum += bias[i];
        
        // Clamp to INT32 range
        if (sum > INT32_MAX) sum = INT32_MAX;
        if (sum < INT32_MIN) sum = INT32_MIN;
        
        output[i] = static_cast<int32_t>(sum);
    }
}

size_t matrix::argmax(const int32_t* data, size_t len) {
    if (len == 0) return 0;
    
    size_t max_idx = 0;
    int32_t max_val = data[0];
    
    for (size_t i = 1; i < len && i < ML_MAX_OUTPUT_SIZE; i++) {
        if (data[i] > max_val) {
            max_val = data[i];
            max_idx = i;
        }
    }
    
    return max_idx;
}

int32_t matrix::max_value(const int32_t* data, size_t len) {
    if (len == 0) return 0;
    
    int32_t max_val = data[0];
    for (size_t i = 1; i < len && i < ML_MAX_OUTPUT_SIZE; i++) {
        if (data[i] > max_val) {
            max_val = data[i];
        }
    }
    
    return max_val;
}

// ============================================================================
// ML Inference Syscalls Implementation
// ============================================================================

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
    uint32_t cols)
{
    // Validate pointers
    if (!input || !weights || !bias || !output || !output_len) {
        return ML_ERROR_NULL_POINTER;
    }
    
    // Validate dimensions
    if (input_len == 0 || input_len > ML_MAX_INPUT_SIZE) {
        return ML_ERROR_INVALID_INPUT_LENGTH;
    }
    
    if (rows == 0 || rows > ML_MAX_NEURONS_PER_LAYER) {
        return ML_ERROR_DIMENSION_MISMATCH;
    }
    
    if (cols == 0 || cols > ML_MAX_INPUT_SIZE) {
        return ML_ERROR_DIMENSION_MISMATCH;
    }
    
    // Verify dimensions match
    if (input_len != cols) {
        return ML_ERROR_DIMENSION_MISMATCH;
    }
    
    if (weights_len != static_cast<uint64_t>(rows) * cols) {
        return ML_ERROR_INVALID_WEIGHTS;
    }
    
    if (bias_len != rows) {
        return ML_ERROR_DIMENSION_MISMATCH;
    }
    
    // Perform matrix multiplication
    matrix::dense_forward(output, input, weights, bias, rows, cols);
    
    *output_len = rows;
    return ML_SUCCESS;
}

uint64_t sol_ml_activation(
    int32_t* data,
    uint64_t len,
    uint8_t activation_type)
{
    // Validate pointer
    if (!data) {
        return ML_ERROR_NULL_POINTER;
    }
    
    // Validate length
    if (len == 0 || len > ML_MAX_NEURONS_PER_LAYER) {
        return ML_ERROR_INVALID_INPUT_LENGTH;
    }
    
    // Validate activation type
    if (activation_type > static_cast<uint8_t>(ActivationType::LEAKY_RELU)) {
        return ML_ERROR_INVALID_ACTIVATION;
    }
    
    // Apply activation
    activation::apply(data, static_cast<size_t>(len), 
                      static_cast<ActivationType>(activation_type));
    
    return ML_SUCCESS;
}

uint64_t sol_ml_decision_tree(
    const int32_t* features,
    uint64_t features_len,
    const DecisionTreeNode* tree_nodes,
    uint64_t num_nodes,
    uint32_t max_depth,
    int32_t* result)
{
    // Validate pointers
    if (!features || !tree_nodes || !result) {
        return ML_ERROR_NULL_POINTER;
    }
    
    // Validate dimensions
    if (features_len == 0 || features_len > ML_MAX_INPUT_SIZE) {
        return ML_ERROR_INVALID_INPUT_LENGTH;
    }
    
    if (num_nodes == 0 || num_nodes > ML_MAX_TREE_NODES) {
        return ML_ERROR_INVALID_MODEL;
    }
    
    if (max_depth > ML_MAX_TREE_DEPTH) {
        max_depth = ML_MAX_TREE_DEPTH;
    }
    
    // Traverse the tree with bounded depth
    int16_t current_node = 0;
    
    for (uint32_t depth = 0; depth < max_depth; depth++) {
        if (current_node < 0 || static_cast<uint64_t>(current_node) >= num_nodes) {
            return ML_ERROR_INVALID_MODEL;
        }
        
        const DecisionTreeNode& node = tree_nodes[current_node];
        
        // Check if leaf node
        if (node.is_leaf()) {
            *result = node.leaf_value;
            return ML_SUCCESS;
        }
        
        // Validate feature index
        if (node.feature_index < 0 || 
            static_cast<uint64_t>(node.feature_index) >= features_len) {
            return ML_ERROR_INVALID_MODEL;
        }
        
        // Compare feature with threshold and traverse
        if (features[node.feature_index] <= node.threshold) {
            current_node = node.left_child;
        } else {
            current_node = node.right_child;
        }
        
        // Check for leaf indicator
        if (current_node < 0) {
            // Negative index indicates we've reached a result
            *result = tree_nodes[static_cast<size_t>(-current_node - 1)].leaf_value;
            return ML_SUCCESS;
        }
    }
    
    // Max depth exceeded without reaching leaf
    return ML_ERROR_TREE_DEPTH_EXCEEDED;
}

uint64_t sol_ml_forward(
    const int32_t* input,
    uint64_t input_len,
    const uint8_t* model_data,
    uint64_t model_len,
    int32_t* output,
    uint64_t* output_len)
{
    // Validate pointers
    if (!input || !model_data || !output || !output_len) {
        return ML_ERROR_NULL_POINTER;
    }
    
    // Validate input length
    if (input_len == 0 || input_len > ML_MAX_INPUT_SIZE) {
        return ML_ERROR_INVALID_INPUT_LENGTH;
    }
    
    // Deserialize model
    MLPModel model;
    if (!model_io::deserialize_mlp(model_data, model_len, model)) {
        return ML_ERROR_INVALID_MODEL;
    }
    
    // Validate model
    if (!model.is_valid()) {
        return ML_ERROR_INVALID_MODEL;
    }
    
    if (input_len != model.input_size) {
        return ML_ERROR_DIMENSION_MISMATCH;
    }
    
    // Allocate temporary buffers (stack-based for eBPF compatibility)
    int32_t buffer1[ML_MAX_NEURONS_PER_LAYER];
    int32_t buffer2[ML_MAX_NEURONS_PER_LAYER];
    
    // Copy input to first buffer
    for (size_t i = 0; i < input_len; i++) {
        buffer1[i] = input[i];
    }
    
    // Pointers to ping-pong between buffers
    int32_t* current_input = buffer1;
    int32_t* current_output = buffer2;
    size_t current_size = input_len;
    
    // Forward pass through each layer
    for (size_t layer_idx = 0; layer_idx < model.num_layers; layer_idx++) {
        const DenseLayerWeights& layer = model.layers[layer_idx];
        
        if (current_size != layer.input_size) {
            return ML_ERROR_DIMENSION_MISMATCH;
        }
        
        // Compute dense layer output
        matrix::dense_forward(
            current_output,
            current_input,
            layer.weights.data(),
            layer.biases.data(),
            layer.output_size,
            layer.input_size
        );
        
        // Apply activation
        activation::apply(current_output, layer.output_size, layer.activation);
        
        // Swap buffers for next layer
        std::swap(current_input, current_output);
        current_size = layer.output_size;
    }
    
    // Copy final output (now in current_input due to last swap)
    *output_len = current_size;
    for (size_t i = 0; i < current_size && i < ML_MAX_OUTPUT_SIZE; i++) {
        output[i] = current_input[i];
    }
    
    return ML_SUCCESS;
}

// ============================================================================
// Model Serialization Implementation
// ============================================================================

namespace model_io {

std::vector<uint8_t> serialize_mlp(const MLPModel& model) {
    std::vector<uint8_t> data;
    
    // Reserve approximate size
    data.reserve(sizeof(MLPModel) + model.get_memory_size());
    
    // Write header
    auto write_u32 = [&data](uint32_t val) {
        data.push_back(val & 0xFF);
        data.push_back((val >> 8) & 0xFF);
        data.push_back((val >> 16) & 0xFF);
        data.push_back((val >> 24) & 0xFF);
    };
    
    auto write_u64 = [&data](uint64_t val) {
        for (int i = 0; i < 8; i++) {
            data.push_back((val >> (i * 8)) & 0xFF);
        }
    };
    
    auto write_u16 = [&data](uint16_t val) {
        data.push_back(val & 0xFF);
        data.push_back((val >> 8) & 0xFF);
    };
    
    auto write_i16 = [&data](int16_t val) {
        data.push_back(val & 0xFF);
        data.push_back((val >> 8) & 0xFF);
    };
    
    auto write_i32 = [&data](int32_t val) {
        data.push_back(val & 0xFF);
        data.push_back((val >> 8) & 0xFF);
        data.push_back((val >> 16) & 0xFF);
        data.push_back((val >> 24) & 0xFF);
    };
    
    // Magic number for MLP model
    write_u32(0x4D4C5031);  // "MLP1"
    
    write_u32(model.version);
    write_u32(model.input_size);
    write_u32(model.output_size);
    data.push_back(model.num_layers);
    data.push_back(model.quantization_bits);
    write_u16(model.padding);
    write_u64(model.timestamp);
    
    // Write each layer
    for (const auto& layer : model.layers) {
        write_u16(layer.input_size);
        write_u16(layer.output_size);
        data.push_back(static_cast<uint8_t>(layer.activation));
        data.push_back(0);  // padding
        data.push_back(0);
        data.push_back(0);
        
        // Write weights
        for (int16_t w : layer.weights) {
            write_i16(w);
        }
        
        // Write biases
        for (int32_t b : layer.biases) {
            write_i32(b);
        }
    }
    
    return data;
}

bool deserialize_mlp(const uint8_t* data, size_t len, MLPModel& model) {
    if (len < 32) return false;  // Minimum header size
    
    size_t offset = 0;
    
    auto read_u32 = [&data, &offset, len]() -> uint32_t {
        if (offset + 4 > len) return 0;
        uint32_t val = data[offset] |
                      (static_cast<uint32_t>(data[offset + 1]) << 8) |
                      (static_cast<uint32_t>(data[offset + 2]) << 16) |
                      (static_cast<uint32_t>(data[offset + 3]) << 24);
        offset += 4;
        return val;
    };
    
    auto read_u64 = [&data, &offset, len]() -> uint64_t {
        if (offset + 8 > len) return 0;
        uint64_t val = 0;
        for (int i = 0; i < 8; i++) {
            val |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
        }
        offset += 8;
        return val;
    };
    
    auto read_u16 = [&data, &offset, len]() -> uint16_t {
        if (offset + 2 > len) return 0;
        uint16_t val = data[offset] | (static_cast<uint16_t>(data[offset + 1]) << 8);
        offset += 2;
        return val;
    };
    
    auto read_i16 = [&read_u16]() -> int16_t {
        return static_cast<int16_t>(read_u16());
    };
    
    auto read_i32 = [&read_u32]() -> int32_t {
        return static_cast<int32_t>(read_u32());
    };
    
    // Check magic number
    uint32_t magic = read_u32();
    if (magic != 0x4D4C5031) {  // "MLP1"
        return false;
    }
    
    model.version = read_u32();
    model.input_size = read_u32();
    model.output_size = read_u32();
    model.num_layers = data[offset++];
    model.quantization_bits = data[offset++];
    model.padding = read_u16();
    model.timestamp = read_u64();
    
    // Validate header
    if (model.num_layers > ML_MAX_LAYERS ||
        model.input_size > ML_MAX_INPUT_SIZE ||
        model.output_size > ML_MAX_OUTPUT_SIZE) {
        return false;
    }
    
    // Read layers
    model.layers.clear();
    model.layers.reserve(model.num_layers);
    
    for (uint8_t i = 0; i < model.num_layers; i++) {
        if (offset + 8 > len) return false;
        
        DenseLayerWeights layer;
        layer.input_size = read_u16();
        layer.output_size = read_u16();
        layer.activation = static_cast<ActivationType>(data[offset++]);
        offset += 3;  // padding
        
        // Validate layer dimensions
        if (layer.input_size > ML_MAX_INPUT_SIZE ||
            layer.output_size > ML_MAX_NEURONS_PER_LAYER) {
            return false;
        }
        
        size_t weight_count = static_cast<size_t>(layer.input_size) * layer.output_size;
        size_t weights_bytes = weight_count * 2;  // int16
        size_t biases_bytes = layer.output_size * 4;  // int32
        
        if (offset + weights_bytes + biases_bytes > len) return false;
        
        // Read weights
        layer.weights.resize(weight_count);
        for (size_t j = 0; j < weight_count; j++) {
            layer.weights[j] = read_i16();
        }
        
        // Read biases
        layer.biases.resize(layer.output_size);
        for (size_t j = 0; j < layer.output_size; j++) {
            layer.biases[j] = read_i32();
        }
        
        model.layers.push_back(std::move(layer));
    }
    
    return true;
}

std::vector<uint8_t> serialize_decision_tree(const DecisionTreeModel& model) {
    std::vector<uint8_t> data;
    
    auto write_u32 = [&data](uint32_t val) {
        data.push_back(val & 0xFF);
        data.push_back((val >> 8) & 0xFF);
        data.push_back((val >> 16) & 0xFF);
        data.push_back((val >> 24) & 0xFF);
    };
    
    auto write_u64 = [&data](uint64_t val) {
        for (int i = 0; i < 8; i++) {
            data.push_back((val >> (i * 8)) & 0xFF);
        }
    };
    
    auto write_u16 = [&data](uint16_t val) {
        data.push_back(val & 0xFF);
        data.push_back((val >> 8) & 0xFF);
    };
    
    auto write_i16 = [&data](int16_t val) {
        data.push_back(val & 0xFF);
        data.push_back((val >> 8) & 0xFF);
    };
    
    auto write_i32 = [&data](int32_t val) {
        data.push_back(val & 0xFF);
        data.push_back((val >> 8) & 0xFF);
        data.push_back((val >> 16) & 0xFF);
        data.push_back((val >> 24) & 0xFF);
    };
    
    // Magic number for decision tree
    write_u32(0x44543031);  // "DT01"
    
    write_u32(model.version);
    write_u16(model.num_nodes);
    write_u16(model.max_depth);
    write_u32(model.num_features);
    write_u32(model.num_classes);
    write_u64(model.timestamp);
    
    // Write nodes
    for (const auto& node : model.nodes) {
        write_i16(node.feature_index);
        write_i16(node.left_child);
        write_i16(node.right_child);
        write_i16(node.padding);
        write_i32(node.threshold);
        write_i32(node.leaf_value);
    }
    
    return data;
}

bool deserialize_decision_tree(const uint8_t* data, size_t len, DecisionTreeModel& model) {
    if (len < 28) return false;  // Minimum header size
    
    size_t offset = 0;
    
    auto read_u32 = [&data, &offset, len]() -> uint32_t {
        if (offset + 4 > len) return 0;
        uint32_t val = data[offset] |
                      (static_cast<uint32_t>(data[offset + 1]) << 8) |
                      (static_cast<uint32_t>(data[offset + 2]) << 16) |
                      (static_cast<uint32_t>(data[offset + 3]) << 24);
        offset += 4;
        return val;
    };
    
    auto read_u64 = [&data, &offset, len]() -> uint64_t {
        if (offset + 8 > len) return 0;
        uint64_t val = 0;
        for (int i = 0; i < 8; i++) {
            val |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
        }
        offset += 8;
        return val;
    };
    
    auto read_u16 = [&data, &offset, len]() -> uint16_t {
        if (offset + 2 > len) return 0;
        uint16_t val = data[offset] | (static_cast<uint16_t>(data[offset + 1]) << 8);
        offset += 2;
        return val;
    };
    
    auto read_i16 = [&read_u16]() -> int16_t {
        return static_cast<int16_t>(read_u16());
    };
    
    auto read_i32 = [&read_u32]() -> int32_t {
        return static_cast<int32_t>(read_u32());
    };
    
    // Check magic number
    uint32_t magic = read_u32();
    if (magic != 0x44543031) {  // "DT01"
        return false;
    }
    
    model.version = read_u32();
    model.num_nodes = read_u16();
    model.max_depth = read_u16();
    model.num_features = read_u32();
    model.num_classes = read_u32();
    model.timestamp = read_u64();
    
    // Validate header
    if (model.num_nodes > ML_MAX_TREE_NODES ||
        model.max_depth > ML_MAX_TREE_DEPTH ||
        model.num_features > ML_MAX_INPUT_SIZE) {
        return false;
    }
    
    // Check we have enough data for nodes
    size_t node_bytes = model.num_nodes * 16;  // Each node is 16 bytes
    if (offset + node_bytes > len) return false;
    
    // Read nodes
    model.nodes.clear();
    model.nodes.reserve(model.num_nodes);
    
    for (uint16_t i = 0; i < model.num_nodes; i++) {
        DecisionTreeNode node;
        node.feature_index = read_i16();
        node.left_child = read_i16();
        node.right_child = read_i16();
        node.padding = read_i16();
        node.threshold = read_i32();
        node.leaf_value = read_i32();
        
        model.nodes.push_back(node);
    }
    
    return true;
}

} // namespace model_io

} // namespace svm
} // namespace slonana

// ============================================================================
// Extern "C" Wrappers for C Linkage
// ============================================================================

extern "C" {

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
    uint32_t cols)
{
    return slonana::svm::sol_ml_matmul(input, input_len, weights, weights_len,
                                        bias, bias_len, output, output_len, rows, cols);
}

uint64_t sol_ml_activation(
    int32_t* data,
    uint64_t len,
    uint8_t activation)
{
    return slonana::svm::sol_ml_activation(data, len, activation);
}

uint64_t sol_ml_decision_tree(
    const int32_t* features,
    uint64_t features_len,
    const slonana::svm::DecisionTreeNode* tree_nodes,
    uint64_t num_nodes,
    uint32_t max_depth,
    int32_t* result)
{
    return slonana::svm::sol_ml_decision_tree(features, features_len, tree_nodes,
                                               num_nodes, max_depth, result);
}

uint64_t sol_ml_forward(
    const int32_t* input,
    uint64_t input_len,
    const uint8_t* model,
    uint64_t model_len,
    int32_t* output,
    uint64_t* output_len)
{
    return slonana::svm::sol_ml_forward(input, input_len, model, model_len, 
                                         output, output_len);
}

} // extern "C"
