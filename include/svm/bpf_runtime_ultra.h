#pragma once

#include "bpf_runtime_lockfree.h"
#include <memory>
#include <bit>

namespace slonana {
namespace svm {

/**
 * Ultra-Optimized BPF Runtime with Advanced Techniques
 * 
 * Additional optimizations beyond lock-free + SIMD:
 * - Memory pooling for zero-allocation hot paths
 * - Bit-packed permissions for faster checks
 * - Speculative execution hints
 * - Profile-guided optimization markers
 * - Hardware prefetch streaming
 * - Branchless algorithms
 */

/**
 * Memory pool for stack frames (zero-allocation hot path)
 */
template<size_t Size, size_t Alignment = 64>
class alignas(Alignment) MemoryPool {
public:
    MemoryPool() : allocated_count_(0) {
        // Pre-allocate memory
        pool_ = static_cast<uint8_t*>(
            aligned_alloc(Alignment, Size * sizeof(LockFreeStackFrame)));
    }
    
    ~MemoryPool() {
        if (pool_) {
            free(pool_);
        }
    }
    
    // Non-copyable
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    
    [[nodiscard]] __attribute__((hot, always_inline))
    inline void* allocate() noexcept {
        size_t idx = allocated_count_.fetch_add(1, std::memory_order_relaxed);
        if (__builtin_expect(idx < Size, 1)) {
            return pool_ + (idx * sizeof(LockFreeStackFrame));
        }
        return nullptr;
    }
    
    __attribute__((hot, always_inline))
    inline void deallocate(void* ptr) noexcept {
        // Simple pool: just decrement counter
        allocated_count_.fetch_sub(1, std::memory_order_relaxed);
    }

private:
    uint8_t* pool_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> allocated_count_;
};

/**
 * Bit-packed permission flags for ultra-fast checking
 */
class PackedPermissions {
public:
    static constexpr uint8_t READ = 0x01;
    static constexpr uint8_t WRITE = 0x02;
    static constexpr uint8_t EXECUTE = 0x04;
    static constexpr uint8_t READ_WRITE = READ | WRITE;
    static constexpr uint8_t ALL = READ | WRITE | EXECUTE;
    
    // Branchless permission check using bitwise operations
    [[nodiscard]] __attribute__((always_inline, const, hot))
    static inline bool has_permission(uint8_t current, uint8_t required) noexcept {
        // Branchless: return true if all required bits are set
        return ((current & required) == required);
    }
    
    // Vectorized permission check for multiple regions (AVX2)
#ifdef __AVX2__
    [[nodiscard]] __attribute__((hot))
    static inline __m256i check_permissions_simd(__m256i current_perms, uint8_t required) noexcept {
        __m256i required_vec = _mm256_set1_epi8(required);
        __m256i masked = _mm256_and_si256(current_perms, required_vec);
        return _mm256_cmpeq_epi8(masked, required_vec);
    }
#endif
};

/**
 * Branchless range check using arithmetic
 */
class BranchlessRangeCheck {
public:
    [[nodiscard]] __attribute__((always_inline, const, hot))
    static inline bool in_range(uintptr_t addr, uintptr_t start, uintptr_t end) noexcept {
        // Branchless using unsigned arithmetic underflow
        return (addr - start) < (end - start);
    }
    
    [[nodiscard]] __attribute__((always_inline, const, hot))
    static inline bool range_in_range(uintptr_t addr, size_t len, 
                                     uintptr_t start, uintptr_t end) noexcept {
        // Check both bounds branchlessly
        uintptr_t addr_end = addr + len;
        // Overflow check: addr_end < addr means overflow
        bool no_overflow = (addr_end >= addr);
        bool in_bounds = (addr - start) < (end - start) && 
                         (addr_end - start) <= (end - start);
        return no_overflow & in_bounds;
    }
};

/**
 * Hardware prefetch utilities for streaming access patterns
 */
class HardwarePrefetch {
public:
    // Prefetch levels: L1, L2, L3
    enum class Level : int {
        L1 = 3,  // Temporal locality, all levels
        L2 = 2,  // Moderate temporal locality, L2/L3
        L3 = 1,  // Low temporal locality, L3 only
        NTA = 0  // Non-temporal (streaming, bypass cache)
    };
    
    __attribute__((always_inline))
    static inline void prefetch_read(const void* addr, Level level = Level::L1) noexcept {
        __builtin_prefetch(addr, 0, static_cast<int>(level));
    }
    
    __attribute__((always_inline))
    static inline void prefetch_write(void* addr, Level level = Level::L1) noexcept {
        __builtin_prefetch(addr, 1, static_cast<int>(level));
    }
    
    // Stream prefetch: prefetch multiple cache lines ahead
    template<size_t N>
    __attribute__((always_inline))
    static inline void prefetch_stream(const void* addr) noexcept {
        for (size_t i = 0; i < N; i++) {
            __builtin_prefetch(static_cast<const char*>(addr) + i * CACHE_LINE_SIZE, 
                              0, static_cast<int>(Level::NTA));
        }
    }
};

/**
 * Ultra-optimized memory region with all techniques
 */
struct alignas(CACHE_LINE_SIZE) UltraMemoryRegion {
    uintptr_t start;
    uintptr_t end;           // Precomputed
    uint8_t permissions;     // Bit-packed
    uint8_t padding1[7];     // Align to 8-byte boundary
    
    // Statistics for profiling (separate cache line to avoid false sharing)
    alignas(CACHE_LINE_SIZE) mutable std::atomic<uint64_t> access_count;
    mutable std::atomic<uint64_t> hit_count;
    
    UltraMemoryRegion() 
        : start(0), end(0), permissions(0), access_count(0), hit_count(0) {
        std::fill(std::begin(padding1), std::end(padding1), 0);
    }
    
    UltraMemoryRegion(uintptr_t s, uintptr_t e, uint8_t perms)
        : start(s), end(e), permissions(perms), access_count(0), hit_count(0) {
        std::fill(std::begin(padding1), std::end(padding1), 0);
    }
    
    // Ultra-fast branchless contains check
    [[nodiscard]] __attribute__((always_inline, hot))
    inline bool contains_branchless(uintptr_t addr) const noexcept {
        return BranchlessRangeCheck::in_range(addr, start, end);
    }
    
    // Profile-guided: mark this as a hot path with high probability
    [[nodiscard]] __attribute__((hot))
    inline bool validate_fast(uintptr_t addr, size_t len, uint8_t req_perms) noexcept {
        // Increment access count for profiling (mutable atomics)
        access_count.fetch_add(1, std::memory_order_relaxed);
        
        // Fast permission check (branchless)
        bool perm_ok = PackedPermissions::has_permission(permissions, req_perms);
        
        // Fast range check (branchless)
        bool range_ok = BranchlessRangeCheck::range_in_range(addr, len, start, end);
        
        // Combine results
        bool result = perm_ok & range_ok;
        
        // Track hits
        if (__builtin_expect(result, 1)) {
            hit_count.fetch_add(1, std::memory_order_relaxed);
        }
        
        return result;
    }
};

/**
 * Ultra-optimized runtime with all advanced techniques
 */
class UltraOptimizedBpfRuntime {
public:
    static constexpr size_t MAX_REGIONS = 32;
    static constexpr size_t CACHE_SIZE = 8;  // Larger cache for better hit rate
    
    UltraOptimizedBpfRuntime() : region_count_(0), cache_index_(0) {
        for (size_t i = 0; i < CACHE_SIZE; i++) {
            region_cache_[i].store(nullptr, std::memory_order_relaxed);
        }
    }
    
    bool add_region(uintptr_t start, size_t size, uint8_t permissions) {
        size_t count = region_count_.load(std::memory_order_acquire);
        if (count >= MAX_REGIONS) {
            return false;
        }
        
        // Initialize region in-place
        new (&regions_[count]) UltraMemoryRegion(start, start + size, permissions);
        region_count_.store(count + 1, std::memory_order_release);
        
        return true;
    }
    
    /**
     * Ultra-fast validation with multi-level caching and streaming prefetch
     */
    [[nodiscard]] __attribute__((hot))
    bool validate_ultra_fast(uintptr_t addr, size_t size, uint8_t required_perms) noexcept {
        // Level 1: Check most recent (cache_index_ points to last accessed)
        size_t recent_idx = cache_index_.load(std::memory_order_relaxed);
        const UltraMemoryRegion* recent = region_cache_[recent_idx % CACHE_SIZE].load(std::memory_order_relaxed);
        
        if (__builtin_expect(recent != nullptr, 1)) {
            if (recent->contains_branchless(addr)) {
                return const_cast<UltraMemoryRegion*>(recent)->validate_fast(addr, size, required_perms);
            }
        }
        
        // Level 2: Check entire cache (unrolled for better performance)
        for (size_t i = 0; i < CACHE_SIZE; i++) {
            const UltraMemoryRegion* cached = region_cache_[i].load(std::memory_order_relaxed);
            if (cached != nullptr && cached->contains_branchless(addr)) {
                // Update most recent
                cache_index_.store(i, std::memory_order_relaxed);
                return const_cast<UltraMemoryRegion*>(cached)->validate_fast(addr, size, required_perms);
            }
        }
        
        // Level 3: Search all regions with streaming prefetch
        const size_t count = region_count_.load(std::memory_order_acquire);
        
        // Prefetch first few regions
        if (count > 2) {
            HardwarePrefetch::prefetch_read(&regions_[1], HardwarePrefetch::Level::L1);
            HardwarePrefetch::prefetch_read(&regions_[2], HardwarePrefetch::Level::L2);
        }
        
        for (size_t i = 0; i < count; i++) {
            // Prefetch next region while processing current
            if (i + 3 < count) {
                HardwarePrefetch::prefetch_read(&regions_[i + 3], HardwarePrefetch::Level::L2);
            }
            
            if (regions_[i].contains_branchless(addr)) {
                // Update cache (round-robin)
                size_t cache_slot = (recent_idx + 1) % CACHE_SIZE;
                region_cache_[cache_slot].store(&regions_[i], std::memory_order_relaxed);
                cache_index_.store(cache_slot, std::memory_order_relaxed);
                
                return const_cast<UltraMemoryRegion&>(regions_[i]).validate_fast(addr, size, required_perms);
            }
        }
        
        return false;
    }
    
    /**
     * Get profiling statistics
     */
    struct ProfileStats {
        uint64_t total_accesses;
        uint64_t total_hits;
        double hit_rate;
    };
    
    ProfileStats get_profile_stats() const {
        ProfileStats stats = {0, 0, 0.0};
        const size_t count = region_count_.load(std::memory_order_acquire);
        
        for (size_t i = 0; i < count; i++) {
            stats.total_accesses += regions_[i].access_count.load(std::memory_order_relaxed);
            stats.total_hits += regions_[i].hit_count.load(std::memory_order_relaxed);
        }
        
        if (stats.total_accesses > 0) {
            stats.hit_rate = static_cast<double>(stats.total_hits) / stats.total_accesses;
        }
        
        return stats;
    }
    
    void clear() noexcept {
        region_count_.store(0, std::memory_order_release);
        cache_index_.store(0, std::memory_order_relaxed);
        for (size_t i = 0; i < CACHE_SIZE; i++) {
            region_cache_[i].store(nullptr, std::memory_order_relaxed);
        }
    }

private:
    alignas(CACHE_LINE_SIZE) std::array<UltraMemoryRegion, MAX_REGIONS> regions_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> region_count_;
    alignas(CACHE_LINE_SIZE) mutable std::atomic<size_t> cache_index_;
    
    // Multi-level cache
    alignas(CACHE_LINE_SIZE) mutable std::array<std::atomic<const UltraMemoryRegion*>, CACHE_SIZE> region_cache_;
};

/**
 * Compile-time region for zero-overhead validation
 */
template<uintptr_t Start, size_t Size, uint8_t Permissions>
class StaticRegion {
public:
    static constexpr uintptr_t start = Start;
    static constexpr uintptr_t end = Start + Size;
    static constexpr uint8_t permissions = Permissions;
    
    [[nodiscard]] __attribute__((always_inline, const))
    static constexpr bool contains(uintptr_t addr) noexcept {
        return addr >= start && addr < end;
    }
    
    [[nodiscard]] __attribute__((always_inline, const))
    static constexpr bool validate(uintptr_t addr, size_t len, uint8_t req_perms) noexcept {
        return contains(addr) && 
               (addr + len <= end) &&
               ((permissions & req_perms) == req_perms);
    }
};

} // namespace svm
} // namespace slonana
