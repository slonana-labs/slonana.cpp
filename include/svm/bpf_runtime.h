#pragma once

#include "common/types.h"
#include <vector>
#include <cstdint>
#include <memory>

namespace slonana {
namespace svm {

using namespace slonana::common;

/**
 * BPF Program representation
 */
struct BpfProgram {
    std::vector<uint8_t> code;
    uint64_t compute_units;
    
    BpfProgram() : compute_units(0) {}
};

/**
 * BPF Execution Context
 */
struct BpfExecutionContext {
    std::vector<uint8_t> input_data;
    std::vector<uint8_t> stack_memory;
    std::vector<uint8_t> heap_memory;
    uint64_t registers[11] = {0}; // BPF has 11 registers (r0-r10)
    
    BpfExecutionContext() = default;
};

/**
 * BPF Execution Result
 */
struct BpfExecutionResult {
    bool success;
    uint64_t return_value;
    uint64_t compute_units_consumed;
    std::string error_message;
    
    BpfExecutionResult() : success(false), return_value(0), compute_units_consumed(0) {}
    
    bool is_success() const { return success; }
};

/**
 * BPF Runtime for executing Berkeley Packet Filter programs
 */
class BpfRuntime {
public:
    BpfRuntime() = default;
    ~BpfRuntime() = default;
    
    /**
     * Execute a BPF program
     */
    BpfExecutionResult execute(const BpfProgram& program, const BpfExecutionContext& context);
    
    /**
     * Execute using interpreter mode
     */
    BpfExecutionResult execute_interpreter(const BpfProgram& program, const BpfExecutionContext& context);
    
    /**
     * Execute using JIT compilation
     */
    BpfExecutionResult execute_jit(const BpfProgram& program, const BpfExecutionContext& context);
    
    /**
     * Configure runtime parameters
     */
    void set_max_compute_units(uint64_t max_units);
    void set_max_memory_size(size_t max_memory);
    
private:
    uint64_t max_compute_units_ = 1000000;
    size_t max_memory_size_ = 1024 * 1024; // 1MB default
    
    BpfExecutionResult execute_internal(const BpfProgram& program, const BpfExecutionContext& context, bool use_jit);
};

} // namespace svm
} // namespace slonana