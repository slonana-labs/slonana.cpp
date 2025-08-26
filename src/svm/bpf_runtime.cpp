#include "svm/bpf_runtime.h"
#include <iostream>
#include <cassert>

namespace slonana {
namespace svm {

BpfExecutionResult BpfRuntime::execute(const BpfProgram& program, const BpfExecutionContext& context) {
    return execute_internal(program, context, false);
}

BpfExecutionResult BpfRuntime::execute_interpreter(const BpfProgram& program, const BpfExecutionContext& context) {
    return execute_internal(program, context, false);
}

BpfExecutionResult BpfRuntime::execute_jit(const BpfProgram& program, const BpfExecutionContext& context) {
    return execute_internal(program, context, true);
}

void BpfRuntime::set_max_compute_units(uint64_t max_units) {
    max_compute_units_ = max_units;
}

void BpfRuntime::set_max_memory_size(size_t max_memory) {
    max_memory_size_ = max_memory;
}

BpfExecutionResult BpfRuntime::execute_internal(const BpfProgram& program, const BpfExecutionContext& context, bool use_jit) {
    BpfExecutionResult result;
    
    // Basic validation
    if (program.code.empty()) {
        result.error_message = "Empty program";
        return result;
    }
    
    if (program.compute_units > max_compute_units_) {
        result.error_message = "Compute units exceed maximum";
        return result;
    }
    
    // Simulate program execution
    // In a real implementation, this would parse and execute BPF instructions
    result.success = true;
    result.return_value = 42; // Mock return value
    result.compute_units_consumed = std::min(program.compute_units, static_cast<uint64_t>(100));
    
    return result;
}

} // namespace svm
} // namespace slonana