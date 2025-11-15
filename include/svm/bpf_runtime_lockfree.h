#pragma once

#include "bpf_runtime_enhanced.h"
#include <atomic>
#include <array>
#include <immintrin.h> // For SIMD intrinsics

namespace slonana {
namespace svm {

/**
 * Lock-Free BPF Runtime with Advanced Optimizations
 * 
 * Features:
 * - Lock-free stack frame management using atomic operations
 * - SIMD-optimized permission checking for multiple regions
 * - Cache-aligned data structures for hot paths
 * - Branch prediction hints
 * - Prefetching for memory regions
 */

// Cache line size for alignment
constexpr size_t CACHE_LINE_SIZE = 64;

/**
 * Cache-aligned memory region for optimal performance
 */
struct alignas(CACHE_LINE_SIZE) CacheAlignedMemoryRegion {
    uintptr_t start;
    size_t size;
    uint32_t permissions;  // Use plain uint32_t for atomic operations
    uintptr_t end;         // Precomputed end address for faster checks
    
    // Hot-path frequently accessed fields together
    uint32_t access_count; // For LRU tracking
    uint32_t padding[9];   // Pad to cache line size
    
    CacheAlignedMemoryRegion() 
        : start(0), size(0), permissions(0), end(0), access_count(0) {
        std::fill(std::begin(padding), std::end(padding), 0);
    }
    
    CacheAlignedMemoryRegion(uintptr_t s, size_t sz, uint32_t perms)
        : start(s), size(sz), permissions(perms), end(s + sz), access_count(0) {
        std::fill(std::begin(padding), std::end(padding), 0);
    }
    
    // Hot path: inline and branch prediction hints
    [[nodiscard]] __attribute__((always_inline, hot))
    inline bool fast_contains(uintptr_t addr) const noexcept {
        // Use single comparison with precomputed end
        return __builtin_expect(addr >= start && addr < end, 1);
    }
    
    [[nodiscard]] __attribute__((always_inline, hot))
    inline bool fast_contains_range(uintptr_t addr, size_t len) const noexcept {
        // Overflow check with likely hint
        if (__builtin_expect(addr + len < addr, 0)) return false;
        return addr >= start && addr + len <= end;
    }
};

/**
 * Lock-free stack frame with atomic operations
 */
struct alignas(16) LockFreeStackFrame {
    std::atomic<uintptr_t> return_address;
    std::atomic<uint64_t> frame_pointer;
    std::atomic<uint64_t> compute_units;
    
    LockFreeStackFrame() 
        : return_address(0), frame_pointer(0), compute_units(0) {}
    
    LockFreeStackFrame(uintptr_t ret, uint64_t fp, uint64_t cu)
        : return_address(ret), frame_pointer(fp), compute_units(cu) {}
};

/**
 * Lock-free stack frame manager using atomic operations
 */
class LockFreeStackFrameManager {
public:
    static constexpr size_t MAX_DEPTH = 64;
    
    LockFreeStackFrameManager() : depth_(0), max_depth_(MAX_DEPTH) {}
    
    /**
     * Lock-free push operation
     */
    [[nodiscard]] __attribute__((hot))
    bool push_frame(uintptr_t return_addr, uint64_t frame_pointer, uint64_t compute_units) noexcept {
        size_t current_depth = depth_.load(std::memory_order_acquire);
        
        // Fast path: check depth without blocking
        if (__builtin_expect(current_depth >= max_depth_, 0)) {
            return false;
        }
        
        // Try to increment depth atomically
        size_t expected = current_depth;
        while (!depth_.compare_exchange_weak(expected, current_depth + 1,
                                            std::memory_order_release,
                                            std::memory_order_acquire)) {
            current_depth = expected;
            if (current_depth >= max_depth_) {
                return false;
            }
        }
        
        // Store frame data (depth already incremented)
        frames_[current_depth].return_address.store(return_addr, std::memory_order_release);
        frames_[current_depth].frame_pointer.store(frame_pointer, std::memory_order_release);
        frames_[current_depth].compute_units.store(compute_units, std::memory_order_release);
        
        return true;
    }
    
    /**
     * Lock-free pop operation
     */
    [[nodiscard]] __attribute__((hot))
    bool pop_frame(uintptr_t& return_addr, uint64_t& frame_pointer, uint64_t& compute_units) noexcept {
        size_t current_depth = depth_.load(std::memory_order_acquire);
        
        // Fast path: empty check
        if (__builtin_expect(current_depth == 0, 0)) {
            return false;
        }
        
        // Try to decrement depth atomically
        size_t expected = current_depth;
        while (!depth_.compare_exchange_weak(expected, current_depth - 1,
                                            std::memory_order_release,
                                            std::memory_order_acquire)) {
            current_depth = expected;
            if (current_depth == 0) {
                return false;
            }
        }
        
        // Load frame data (depth already decremented)
        size_t frame_idx = current_depth - 1;
        return_addr = frames_[frame_idx].return_address.load(std::memory_order_acquire);
        frame_pointer = frames_[frame_idx].frame_pointer.load(std::memory_order_acquire);
        compute_units = frames_[frame_idx].compute_units.load(std::memory_order_acquire);
        
        return true;
    }
    
    /**
     * Get current depth (lock-free)
     */
    [[nodiscard]] __attribute__((always_inline))
    inline size_t get_depth() const noexcept {
        return depth_.load(std::memory_order_acquire);
    }
    
    /**
     * Check if max depth exceeded (lock-free, hot path)
     */
    [[nodiscard]] __attribute__((always_inline, hot))
    inline bool is_max_depth_exceeded() const noexcept {
        return __builtin_expect(depth_.load(std::memory_order_relaxed) >= max_depth_, 0);
    }
    
    void set_max_depth(size_t depth) noexcept {
        max_depth_ = (depth > MAX_DEPTH) ? MAX_DEPTH : depth;
    }
    
    void clear() noexcept {
        depth_.store(0, std::memory_order_release);
    }

private:
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> depth_;
    size_t max_depth_;
    std::array<LockFreeStackFrame, MAX_DEPTH> frames_;
};

/**
 * Ultra-fast instruction cost lookup using compile-time generated lookup table
 */
class FastInstructionCostLookup {
public:
    FastInstructionCostLookup() {
        // Initialize lookup table at construction
        init_lookup_table();
    }
    
    /**
     * Hot path: Direct array lookup, no branches
     */
    [[nodiscard]] __attribute__((always_inline, hot, const))
    inline uint64_t get_cost(uint8_t opcode) const noexcept {
        return cost_table_[opcode];
    }

private:
    void init_lookup_table() {
        // Initialize all to default cost
        std::fill(std::begin(cost_table_), std::end(cost_table_), 
                 compute_costs::DEFAULT);
        
        // Set specific costs
        for (int i = 0; i < 256; i++) {
            uint8_t op_class = i & 0x07;
            uint8_t op_code = i & 0xF0;
            
            switch (op_class) {
            case 0x0: case 0x1:
                cost_table_[i] = compute_costs::LOAD;
                break;
            case 0x2: case 0x3:
                cost_table_[i] = compute_costs::STORE;
                break;
            case 0x4: case 0x7:
                if (op_code == 0x30 || op_code == 0x90) {
                    cost_table_[i] = compute_costs::ALU_DIV;
                } else if (op_code == 0x20) {
                    cost_table_[i] = compute_costs::ALU_MUL;
                } else {
                    cost_table_[i] = compute_costs::ALU_ADD;
                }
                break;
            case 0x5:
                if (i == 0x85) {
                    cost_table_[i] = compute_costs::CALL;
                } else if (i == 0x95) {
                    cost_table_[i] = compute_costs::EXIT;
                } else {
                    cost_table_[i] = compute_costs::JUMP;
                }
                break;
            }
        }
    }
    
    alignas(CACHE_LINE_SIZE) std::array<uint64_t, 256> cost_table_;
};

/**
 * Lock-Free BPF Runtime with extreme optimizations
 */
class LockFreeBpfRuntime {
public:
    static constexpr size_t MAX_REGIONS = 32;
    static constexpr size_t CACHE_LINES = 4; // Track recently accessed regions
    
    LockFreeBpfRuntime() : region_count_(0) {
        // Initialize cache with invalid entries
        for (size_t i = 0; i < CACHE_LINES; i++) {
            region_cache_[i].store(nullptr, std::memory_order_relaxed);
        }
    }
    
    /**
     * Add region (not lock-free, called during setup)
     */
    bool add_region(uintptr_t start, size_t size, uint32_t permissions) {
        size_t count = region_count_.load(std::memory_order_acquire);
        if (count >= MAX_REGIONS) {
            return false;
        }
        
        regions_[count] = CacheAlignedMemoryRegion(start, size, permissions);
        region_count_.store(count + 1, std::memory_order_release);
        
        return true;
    }
    
    /**
     * Hot path: Validate memory access with cache and prefetching
     */
    [[nodiscard]] __attribute__((hot))
    bool validate_access_fast(uintptr_t addr, size_t size, uint32_t required_perms) const noexcept {
        // Check cache first (most likely hit)
        const CacheAlignedMemoryRegion* cached = region_cache_[0].load(std::memory_order_relaxed);
        if (__builtin_expect(cached != nullptr, 1)) {
            if (cached->fast_contains_range(addr, size)) {
                // Fast permission check (bitwise AND)
                return (cached->permissions & required_perms) == required_perms;
            }
        }
        
        // Cache miss: search all regions with prefetching
        const size_t count = region_count_.load(std::memory_order_acquire);
        
        for (size_t i = 0; i < count; i++) {
            // Prefetch next region for better cache performance
            if (i + 1 < count) {
                __builtin_prefetch(&regions_[i + 1], 0, 3);
            }
            
            if (regions_[i].fast_contains_range(addr, size)) {
                // Update cache (write-through, relaxed ordering for performance)
                region_cache_[0].store(&regions_[i], std::memory_order_relaxed);
                
                // Check permissions
                return (regions_[i].permissions & required_perms) == required_perms;
            }
        }
        
        return false;
    }
    
    /**
     * SIMD-optimized batch validation for multiple addresses
     */
    [[nodiscard]] __attribute__((hot))
    bool validate_batch_simd(const uintptr_t* addrs, size_t count, 
                            size_t access_size, uint32_t required_perms) const noexcept {
#if defined(__AVX2__)
        // Use AVX2 for parallel checking of up to 4 addresses
        if (count >= 4) {
            __m256i addr_vec = _mm256_loadu_si256((__m256i*)addrs);
            
            const size_t region_count = region_count_.load(std::memory_order_acquire);
            for (size_t i = 0; i < region_count; i++) {
                __m256i start_vec = _mm256_set1_epi64x(regions_[i].start);
                __m256i end_vec = _mm256_set1_epi64x(regions_[i].end);
                
                // Check if all addresses are in range
                __m256i ge_start = _mm256_cmpgt_epi64(addr_vec, start_vec);
                __m256i lt_end = _mm256_cmpgt_epi64(end_vec, addr_vec);
                __m256i in_range = _mm256_and_si256(ge_start, lt_end);
                
                // If all addresses are in this region
                if (_mm256_movemask_epi8(in_range) == -1) {
                    return (regions_[i].permissions & required_perms) == required_perms;
                }
            }
            return false;
        }
#endif
        // Fallback for non-SIMD or smaller batches
        for (size_t i = 0; i < count; i++) {
            if (!validate_access_fast(addrs[i], access_size, required_perms)) {
                return false;
            }
        }
        return true;
    }
    
    /**
     * Get instruction cost (hot path, inlined)
     */
    [[nodiscard]] __attribute__((always_inline, hot))
    inline uint64_t get_instruction_cost(uint8_t opcode) const noexcept {
        return cost_lookup_.get_cost(opcode);
    }
    
    /**
     * Clear all regions
     */
    void clear() noexcept {
        region_count_.store(0, std::memory_order_release);
        for (size_t i = 0; i < CACHE_LINES; i++) {
            region_cache_[i].store(nullptr, std::memory_order_relaxed);
        }
    }

private:
    // Cache-aligned for optimal performance
    alignas(CACHE_LINE_SIZE) std::array<CacheAlignedMemoryRegion, MAX_REGIONS> regions_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> region_count_;
    
    // Region cache for hot path optimization (last accessed region)
    alignas(CACHE_LINE_SIZE) mutable std::array<std::atomic<const CacheAlignedMemoryRegion*>, CACHE_LINES> region_cache_;
    
    // Instruction cost lookup table (const, no synchronization needed)
    FastInstructionCostLookup cost_lookup_;
};

} // namespace svm
} // namespace slonana
