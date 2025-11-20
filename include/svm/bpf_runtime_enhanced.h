#pragma once

#include "bpf_runtime.h"
#include <map>
#include <optional>
#include <vector>

namespace slonana {
namespace svm {

/**
 * Enhanced BPF Runtime Features for Agave Compatibility
 * 
 * Adds latest BPF features:
 * - Enhanced memory region management with fine-grained permissions
 * - Updated compute unit costs matching Agave
 * - Improved stack frame management
 */

// Memory region permissions
enum class MemoryPermission : uint32_t {
    NONE = 0,
    READ = 1 << 0,
    WRITE = 1 << 1,
    EXECUTE = 1 << 2,
    READ_WRITE = READ | WRITE,
    READ_EXECUTE = READ | EXECUTE,
    ALL = READ | WRITE | EXECUTE
};

inline MemoryPermission operator|(MemoryPermission a, MemoryPermission b) {
    return static_cast<MemoryPermission>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool has_permission(MemoryPermission current, MemoryPermission required) {
    return (static_cast<uint32_t>(current) & static_cast<uint32_t>(required)) ==
           static_cast<uint32_t>(required);
}

/**
 * Memory region with permissions
 */
struct MemoryRegion {
    uintptr_t start;
    size_t size;
    MemoryPermission permissions;
    std::string name; // For debugging
    
    MemoryRegion() 
        : start(0), size(0), permissions(MemoryPermission::NONE) {}
    
    MemoryRegion(uintptr_t start_, size_t size_, 
                 MemoryPermission perms, const std::string& name_ = "")
        : start(start_), size(size_), permissions(perms), name(name_) {}
    
    bool contains(uintptr_t addr) const {
        return addr >= start && addr < start + size;
    }
    
    bool contains_range(uintptr_t addr, size_t len) const {
        return addr >= start && 
               addr + len <= start + size &&
               addr + len > addr; // Check for overflow
    }
};

/**
 * Enhanced BPF Runtime with memory region management
 */
class EnhancedBpfRuntime : public BpfRuntime {
public:
    EnhancedBpfRuntime() = default;
    ~EnhancedBpfRuntime() = default;
    
    /**
     * Add a memory region with specific permissions
     */
    void add_memory_region(const MemoryRegion& region);
    
    /**
     * Remove a memory region
     */
    void remove_memory_region(uintptr_t start);
    
    /**
     * Validate memory access against regions
     */
    bool validate_memory_access(uintptr_t addr, size_t size, 
                                MemoryPermission required_perms) const;
    
    /**
     * Get memory region containing address
     */
    const MemoryRegion* get_region(uintptr_t addr) const;
    
    /**
     * Get memory region containing address (alias for get_region)
     */
    const MemoryRegion* get_memory_region(uintptr_t addr) const {
        return get_region(addr);
    }
    
    /**
     * Clear all memory regions
     */
    void clear_regions();
    
    /**
     * Get compute unit cost for instruction
     */
    static uint64_t get_instruction_cost(uint8_t opcode);
    
private:
    std::vector<MemoryRegion> memory_regions_;
};

/**
 * Updated Compute Unit Costs (Agave 2024-2025)
 * 
 * These costs match the latest Agave implementation for accurate
 * compute budget tracking.
 */
namespace compute_costs {
    // Base instruction costs
    constexpr uint64_t DEFAULT = 1;
    constexpr uint64_t NOP = 0;
    
    // ALU operations
    constexpr uint64_t ALU_ADD = 1;
    constexpr uint64_t ALU_SUB = 1;
    constexpr uint64_t ALU_MUL = 1;
    constexpr uint64_t ALU_DIV = 4;
    constexpr uint64_t ALU_MOD = 4;
    constexpr uint64_t ALU_SHIFT = 1;
    constexpr uint64_t ALU_BITWISE = 1;
    
    // Memory operations
    constexpr uint64_t LOAD = 1;
    constexpr uint64_t STORE = 1;
    constexpr uint64_t LOAD64 = 2;
    constexpr uint64_t STORE64 = 2;
    
    // Jump operations
    constexpr uint64_t JUMP = 1;
    constexpr uint64_t JUMP_CONDITIONAL = 1;
    constexpr uint64_t CALL = 100;
    constexpr uint64_t EXIT = 0;
    
    // Stack operations
    constexpr uint64_t PUSH = 1;
    constexpr uint64_t POP = 1;
    constexpr uint64_t STACK_FRAME_SETUP = 10;
    constexpr uint64_t STACK_FRAME_TEARDOWN = 5;
    
    // Memory access costs (per byte)
    constexpr uint64_t MEMORY_READ_PER_BYTE = 0;
    constexpr uint64_t MEMORY_WRITE_PER_BYTE = 0;
    
    // Extended operations
    constexpr uint64_t SYSCALL_BASE = 100;
    constexpr uint64_t LOAD_IMM64 = 1; // Two instruction words
} // namespace compute_costs

/**
 * Stack frame tracking for improved call depth management
 */
struct StackFrame {
    uintptr_t return_address;
    uint64_t frame_pointer;
    uint64_t compute_units_at_entry;
    uint64_t compute_units_used;  // Add this field for test compatibility
    
    StackFrame(uintptr_t ret, uint64_t fp, uint64_t cu)
        : return_address(ret), frame_pointer(fp), 
          compute_units_at_entry(cu), compute_units_used(cu) {}
};

/**
 * Stack frame manager
 */
class StackFrameManager {
public:
    StackFrameManager() : max_depth_(64) {}
    
    /**
     * Push a new stack frame
     */
    bool push_frame(uintptr_t return_addr, uint64_t frame_pointer, 
                    uint64_t compute_units);
    
    /**
     * Pop the top stack frame
     */
    bool pop_frame(StackFrame& frame);
    
    /**
     * Pop the top stack frame (returns optional)
     */
    std::optional<StackFrame> pop_frame();
    
    /**
     * Get current call depth
     */
    size_t get_depth() const { return frames_.size(); }
    
    /**
     * Alias for get_depth (for backward compatibility with tests)
     */
    size_t get_current_depth() const { return get_depth(); }
    
    /**
     * Check if max depth exceeded
     */
    bool is_max_depth_exceeded() const { 
        return frames_.size() >= max_depth_; 
    }
    
    /**
     * Set maximum call depth
     */
    void set_max_depth(size_t depth) { max_depth_ = depth; }
    
    /**
     * Get maximum call depth
     */
    size_t get_max_depth() const { return max_depth_; }
    
    /**
     * Clear all frames
     */
    void clear() { frames_.clear(); }
    
private:
    std::vector<StackFrame> frames_;
    size_t max_depth_;
};

} // namespace svm
} // namespace slonana
