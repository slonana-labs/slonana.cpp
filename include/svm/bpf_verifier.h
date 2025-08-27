#pragma once

#include "bpf_runtime.h"
#include <vector>
#include <string>

namespace slonana {
namespace svm {

/**
 * BPF Program Verifier - validates BPF programs for safety and correctness
 */
class BpfVerifier {
public:
    BpfVerifier() = default;
    ~BpfVerifier() = default;
    
    /**
     * Verify a BPF program for safety and correctness
     * @param program The BPF program to verify
     * @return true if the program is valid and safe to execute
     */
    bool verify(const BpfProgram& program);
    
    /**
     * Get the last verification error message
     */
    const std::string& get_last_error() const { return last_error_; }
    
    /**
     * Set verification options
     */
    void set_max_instructions(size_t max_instructions);
    void set_allow_infinite_loops(bool allow);
    void set_max_stack_depth(size_t max_depth);
    
private:
    std::string last_error_;
    size_t max_instructions_ = 4096;
    bool allow_infinite_loops_ = false;
    size_t max_stack_depth_ = 512;
    
    bool verify_instruction_bounds(const BpfProgram& program);
    bool verify_jump_targets(const BpfProgram& program);
    bool verify_memory_access(const BpfProgram& program);
    bool verify_no_infinite_loops(const BpfProgram& program);
    bool verify_stack_usage(const BpfProgram& program);
};

} // namespace svm
} // namespace slonana