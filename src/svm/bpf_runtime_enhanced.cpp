#include "svm/bpf_runtime_enhanced.h"
#include <algorithm>
#include <stdexcept>

namespace slonana {
namespace svm {

// ============================================================================
// Enhanced BPF Runtime Implementation
// ============================================================================

void EnhancedBpfRuntime::add_memory_region(const MemoryRegion& region) {
    // Validate region
    if (region.size == 0) {
        throw std::invalid_argument("Memory region size must be > 0");
    }
    
    // Check for overlaps with existing regions
    for (const auto& existing : memory_regions_) {
        // Optimized: Use clearer overlap detection
        bool overlaps = !(region.start >= existing.start + existing.size ||
                         existing.start >= region.start + region.size);
        if (overlaps) {
            throw std::invalid_argument(
                "Memory region overlaps with existing region: " + existing.name);
        }
    }
    
    memory_regions_.push_back(region);
    
    // Keep regions sorted by start address for better lookup performance
    std::sort(memory_regions_.begin(), memory_regions_.end(),
              [](const MemoryRegion& a, const MemoryRegion& b) {
                  return a.start < b.start;
              });
}

void EnhancedBpfRuntime::remove_memory_region(uintptr_t start) {
    auto it = std::find_if(memory_regions_.begin(), memory_regions_.end(),
                          [start](const MemoryRegion& r) { 
                              return r.start == start; 
                          });
    
    if (it != memory_regions_.end()) {
        memory_regions_.erase(it);
    }
}

bool EnhancedBpfRuntime::validate_memory_access(
    uintptr_t addr, size_t size, MemoryPermission required_perms) const {
    
    // Find the region containing this address
    const MemoryRegion* region = get_region(addr);
    
    if (!region) {
        // Address not in any region
        return false;
    }
    
    // Check if entire range is within region
    if (!region->contains_range(addr, size)) {
        return false;
    }
    
    // Check permissions
    if (!has_permission(region->permissions, required_perms)) {
        return false;
    }
    
    return true;
}

const MemoryRegion* EnhancedBpfRuntime::get_region(uintptr_t addr) const {
    // Optimized: Use binary search since regions are sorted
    // For small number of regions (< 10 typically), linear search is fine
    // But this is more scalable
    auto it = std::lower_bound(
        memory_regions_.begin(), 
        memory_regions_.end(),
        addr,
        [](const MemoryRegion& region, uintptr_t value) {
            return region.start + region.size <= value;
        });
    
    if (it != memory_regions_.end() && it->contains(addr)) {
        return &(*it);
    }
    
    return nullptr;
}

void EnhancedBpfRuntime::clear_regions() {
    memory_regions_.clear();
}

uint64_t EnhancedBpfRuntime::get_instruction_cost(uint8_t opcode) {
    const uint8_t op_class = opcode & 0x07;
    const uint8_t op_code = opcode & 0xF0;
    
    // Classify instruction and return cost using lookup table approach
    switch (op_class) {
    case 0x0: // LD
    case 0x1: // LDX
        return compute_costs::LOAD;
    case 0x2: // ST
    case 0x3: // STX
        return compute_costs::STORE;
    case 0x4: // ALU
    case 0x7: // ALU64
        switch (op_code) {
        case 0x30: // DIV
        case 0x90: // MOD
            return compute_costs::ALU_DIV;
        case 0x20: // MUL
            return compute_costs::ALU_MUL;
        default:
            return compute_costs::ALU_ADD;
        }
    case 0x5: // JMP
        switch (opcode) {
        case 0x85:
            return compute_costs::CALL;
        case 0x95:
            return compute_costs::EXIT;
        default:
            return compute_costs::JUMP;
        }
    default:
        return compute_costs::DEFAULT;
    }
}

// ============================================================================
// Stack Frame Manager Implementation
// ============================================================================

bool StackFrameManager::push_frame(uintptr_t return_addr, 
                                   uint64_t frame_pointer,
                                   uint64_t compute_units) {
    if (is_max_depth_exceeded()) {
        return false;
    }
    
    frames_.emplace_back(return_addr, frame_pointer, compute_units);
    return true;
}

bool StackFrameManager::pop_frame(StackFrame& frame) {
    if (frames_.empty()) {
        return false;
    }
    
    frame = frames_.back();
    frames_.pop_back();
    return true;
}

std::optional<StackFrame> StackFrameManager::pop_frame() {
    if (frames_.empty()) {
        return std::nullopt;
    }
    
    StackFrame frame = frames_.back();
    frames_.pop_back();
    return frame;
}

} // namespace svm
} // namespace slonana
