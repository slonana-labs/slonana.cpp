#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include <immintrin.h>

namespace slonana {
namespace performance {

/**
 * Zero-Copy Memory Pool for Ultra-High Performance
 * 
 * Revolutionary memory management system that eliminates malloc/free overhead
 * by pre-allocating aligned memory chunks for optimal SIMD operations.
 * 
 * Key Features:
 * - 32-byte alignment for AVX2/AVX-512 operations
 * - Lock-free allocation using atomic operations
 * - Zero-copy semantics with direct memory access
 * - NUMA-aware allocation for multi-socket systems
 */
class ZeroCopyMemoryPool {
public:
    static constexpr size_t CHUNK_SIZE = 64 * 1024; // 64KB chunks
    static constexpr size_t POOL_SIZE = 512; // 32MB total pool
    static constexpr size_t ALIGNMENT = 32; // AVX2 alignment
    
    struct alignas(ALIGNMENT) MemoryChunk {
        uint8_t data[CHUNK_SIZE];
        std::atomic<bool> in_use{false};
        uint64_t allocation_id{0};
        
        MemoryChunk() = default;
        MemoryChunk(const MemoryChunk&) = delete;
        MemoryChunk& operator=(const MemoryChunk&) = delete;
    };
    
    /**
     * Zero-Copy Memory Handle
     * RAII wrapper for automatic memory management
     */
    class ZeroCopyHandle {
    private:
        MemoryChunk* chunk_;
        ZeroCopyMemoryPool* pool_;
        
    public:
        ZeroCopyHandle(MemoryChunk* chunk, ZeroCopyMemoryPool* pool)
            : chunk_(chunk), pool_(pool) {}
            
        ~ZeroCopyHandle() {
            if (chunk_ && pool_) {
                pool_->release_chunk(chunk_);
            }
        }
        
        // Move semantics only
        ZeroCopyHandle(ZeroCopyHandle&& other) noexcept
            : chunk_(other.chunk_), pool_(other.pool_) {
            other.chunk_ = nullptr;
            other.pool_ = nullptr;
        }
        
        ZeroCopyHandle& operator=(ZeroCopyHandle&& other) noexcept {
            if (this != &other) {
                if (chunk_ && pool_) {
                    pool_->release_chunk(chunk_);
                }
                chunk_ = other.chunk_;
                pool_ = other.pool_;
                other.chunk_ = nullptr;
                other.pool_ = nullptr;
            }
            return *this;
        }
        
        // No copying allowed
        ZeroCopyHandle(const ZeroCopyHandle&) = delete;
        ZeroCopyHandle& operator=(const ZeroCopyHandle&) = delete;
        
        uint8_t* data() { return chunk_ ? chunk_->data : nullptr; }
        const uint8_t* data() const { return chunk_ ? chunk_->data : nullptr; }
        size_t size() const { return CHUNK_SIZE; }
        bool valid() const { return chunk_ != nullptr; }
    };
    
    ZeroCopyMemoryPool();
    ~ZeroCopyMemoryPool();
    
    /**
     * Allocate a zero-copy memory chunk
     * @return Handle to allocated memory, or invalid handle if pool exhausted
     */
    ZeroCopyHandle allocate_chunk();
    
    /**
     * Get pool statistics for monitoring
     */
    struct PoolStats {
        size_t total_chunks;
        size_t available_chunks;
        size_t allocated_chunks;
        double utilization_percentage;
        uint64_t total_allocations;
        uint64_t failed_allocations;
    };
    
    PoolStats get_stats() const;
    
    /**
     * Enable huge pages for better TLB performance
     */
    void enable_huge_pages();
    
    /**
     * Set NUMA node preference for allocation
     */
    void set_numa_node(int node_id);

private:
    alignas(64) std::array<MemoryChunk, POOL_SIZE> pool_;
    std::atomic<size_t> next_chunk_index_{0};
    std::atomic<uint64_t> allocation_counter_{0};
    std::atomic<uint64_t> failed_allocation_counter_{0};
    
    void release_chunk(MemoryChunk* chunk);
    
    // Cache line padding to prevent false sharing
    alignas(64) uint8_t padding_[64];
};

/**
 * SIMD-Optimized Transaction Batch Processor
 * 
 * Processes multiple transactions simultaneously using vectorized operations
 * for maximum throughput on modern CPUs.
 */
class SIMDTransactionProcessor {
public:
    static constexpr size_t SIMD_BATCH_SIZE = 8; // AVX2 can handle 8x 32-byte operations
    
    /**
     * Transaction data optimized for SIMD operations
     */
    struct alignas(32) SIMDTransactionData {
        __m256i signature_hash;
        __m256i message_hash;
        uint64_t amount;
        uint32_t fee;
        uint32_t padding; // Ensure 32-byte alignment
    };
    
    /**
     * Process a batch of transactions using SIMD vectorization
     * @param batch Array of transactions (must be SIMD_BATCH_SIZE)
     * @param results Output array for verification results
     * @return Number of successfully processed transactions
     */
    static size_t process_batch_simd(
        const std::array<SIMDTransactionData, SIMD_BATCH_SIZE>& batch,
        std::array<bool, SIMD_BATCH_SIZE>& results
    );
    
    /**
     * Vectorized signature verification using AVX2
     */
    static void verify_signatures_simd(
        const std::array<__m256i, SIMD_BATCH_SIZE>& signatures,
        const std::array<__m256i, SIMD_BATCH_SIZE>& messages,
        std::array<bool, SIMD_BATCH_SIZE>& results
    );
    
    /**
     * Check CPU capabilities for SIMD optimization
     */
    static bool check_avx2_support();
    static bool check_avx512_support();
};

/**
 * Lock-Free Ring Buffer for Ultra-Low Latency Communication
 * 
 * Single Producer Single Consumer (SPSC) ring buffer optimized for
 * cache performance and minimal latency.
 */
template<typename T, size_t Size>
class LockFreeRingBuffer {
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
    
private:
    struct alignas(64) CacheLine {
        std::atomic<size_t> value{0};
        uint8_t padding[64 - sizeof(std::atomic<size_t>)];
    };
    
    alignas(64) std::array<T, Size> buffer_;
    CacheLine head_; // Producer writes here
    CacheLine tail_; // Consumer reads from here
    
public:
    LockFreeRingBuffer() = default;
    
    /**
     * Producer: Push element to ring buffer
     * @param item Item to push
     * @return true if successful, false if buffer full
     */
    bool push(const T& item) {
        const size_t head = head_.value.load(std::memory_order_relaxed);
        const size_t next_head = (head + 1) & (Size - 1);
        
        if (next_head == tail_.value.load(std::memory_order_acquire)) {
            return false; // Buffer full
        }
        
        buffer_[head] = item;
        head_.value.store(next_head, std::memory_order_release);
        return true;
    }
    
    /**
     * Consumer: Pop element from ring buffer
     * @param item Output parameter for popped item
     * @return true if successful, false if buffer empty
     */
    bool pop(T& item) {
        const size_t tail = tail_.value.load(std::memory_order_relaxed);
        
        if (tail == head_.value.load(std::memory_order_acquire)) {
            return false; // Buffer empty
        }
        
        item = buffer_[tail];
        tail_.value.store((tail + 1) & (Size - 1), std::memory_order_release);
        return true;
    }
    
    /**
     * Get current buffer occupancy
     */
    size_t size() const {
        const size_t head = head_.value.load(std::memory_order_acquire);
        const size_t tail = tail_.value.load(std::memory_order_acquire);
        return (head - tail) & (Size - 1);
    }
    
    bool empty() const { return size() == 0; }
    bool full() const { return size() == Size - 1; }
};

} // namespace performance
} // namespace slonana