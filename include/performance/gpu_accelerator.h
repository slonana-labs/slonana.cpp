#pragma once

#include <vector>
#include <memory>
#include <future>
#include <atomic>
#include <cstdint>

#ifdef CUDA_AVAILABLE
#include <cuda_runtime.h>
#include <cublas_v2.h>
#endif

namespace slonana {
namespace performance {

/**
 * GPU Accelerator for Ultra-High Throughput Transaction Processing
 * 
 * Leverages GPU parallel processing capabilities for massive throughput
 * improvements in signature verification and cryptographic operations.
 * 
 * Key Features:
 * - Parallel signature verification (10,000+ concurrent operations)
 * - GPU memory streaming for optimal bandwidth utilization
 * - Asynchronous processing with CPU-GPU overlap
 * - Support for multiple GPU configurations
 */
class GPUAccelerator {
public:
    /**
     * GPU Configuration Options
     */
    struct GPUConfig {
        int device_id = 0;                    // GPU device to use
        size_t memory_pool_size = 1024 * 1024 * 1024; // 1GB GPU memory pool
        size_t max_batch_size = 10000;        // Maximum transactions per batch
        bool enable_streams = true;           // Use CUDA streams for overlap
        bool enable_unified_memory = false;   // Use CUDA unified memory
        int num_streams = 8;                  // Number of CUDA streams
    };
    
    /**
     * Transaction data optimized for GPU processing
     */
    struct GPUTransactionBatch {
        std::vector<uint8_t> signatures;      // Packed signature data
        std::vector<uint8_t> messages;        // Packed message data  
        std::vector<uint64_t> amounts;        // Transaction amounts
        std::vector<uint32_t> fees;           // Transaction fees
        size_t batch_size;                    // Number of transactions
        
        GPUTransactionBatch() = default;
        GPUTransactionBatch(size_t size) : batch_size(size) {
            signatures.reserve(size * 64);   // 64 bytes per signature
            messages.reserve(size * 32);     // 32 bytes per message hash
            amounts.reserve(size);
            fees.reserve(size);
        }
    };
    
    /**
     * GPU Processing Results
     */
    struct GPUProcessingResult {
        std::vector<bool> verification_results; // Per-transaction verification results
        size_t successful_verifications;        // Count of successful verifications
        double processing_time_ms;              // GPU processing time in milliseconds
        double memory_transfer_time_ms;         // Memory transfer time
        double total_time_ms;                   // Total processing time
        size_t gpu_memory_used;                 // GPU memory used in bytes
    };
    
    explicit GPUAccelerator(const GPUConfig& config = GPUConfig{});
    ~GPUAccelerator();
    
    /**
     * Initialize GPU accelerator
     * @return true if initialization successful, false otherwise
     */
    bool initialize();
    
    /**
     * Check if GPU acceleration is available
     */
    static bool is_gpu_available();
    
    /**
     * Get available GPU devices
     */
    static std::vector<int> get_available_devices();
    
    /**
     * Process transaction batch on GPU
     * @param batch Transaction batch to process
     * @return Processing results with verification outcomes
     */
    std::future<GPUProcessingResult> process_batch_async(const GPUTransactionBatch& batch);
    
    /**
     * Process transaction batch synchronously
     * @param batch Transaction batch to process
     * @return Processing results with verification outcomes
     */
    GPUProcessingResult process_batch_sync(const GPUTransactionBatch& batch);
    
    /**
     * Verify signatures on GPU in parallel
     * @param signatures Vector of signature data (64 bytes each)
     * @param messages Vector of message hashes (32 bytes each)
     * @return Vector of verification results (true = valid, false = invalid)
     */
    std::vector<bool> verify_signatures_gpu(
        const std::vector<uint8_t>& signatures,
        const std::vector<uint8_t>& messages
    );
    
    /**
     * Get GPU performance statistics
     */
    struct GPUStats {
        size_t total_batches_processed;
        size_t total_transactions_processed;
        double average_batch_processing_time_ms;
        double peak_throughput_tps;
        size_t gpu_memory_peak_usage;
        double gpu_utilization_percentage;
    };
    
    GPUStats get_performance_stats() const;
    
    /**
     * Reset performance statistics
     */
    void reset_stats();
    
    /**
     * Enable/disable GPU processing
     */
    void set_enabled(bool enabled);
    bool is_enabled() const;

private:
    GPUConfig config_;
    bool initialized_;
    bool enabled_;
    
    // Performance tracking
    std::atomic<size_t> total_batches_processed_{0};
    std::atomic<size_t> total_transactions_processed_{0};
    std::atomic<uint64_t> total_processing_time_us_{0};
    std::atomic<size_t> peak_memory_usage_{0};
    
#ifdef CUDA_AVAILABLE
    cudaDeviceProp device_properties_;
    std::vector<cudaStream_t> cuda_streams_;
    
    // GPU memory management
    uint8_t* gpu_signatures_buffer_;
    uint8_t* gpu_messages_buffer_;
    bool* gpu_results_buffer_;
    size_t gpu_buffer_size_;
    
    // CUDA context management
    bool setup_cuda_context();
    void cleanup_cuda_resources();
    
    // GPU kernel operations
    bool launch_signature_verification_kernel(
        const uint8_t* signatures,
        const uint8_t* messages,
        bool* results,
        size_t batch_size,
        cudaStream_t stream
    );
#endif
    
    // Fallback CPU implementation when GPU not available
    GPUProcessingResult process_batch_cpu_fallback(const GPUTransactionBatch& batch);
};

/**
 * GPU Memory Pool for Efficient GPU Memory Management
 * 
 * Pre-allocates GPU memory to avoid allocation overhead during processing.
 */
class GPUMemoryPool {
public:
    struct GPUMemoryBlock {
        void* ptr;
        size_t size;
        bool in_use;
    };
    
    explicit GPUMemoryPool(size_t total_size = 1024 * 1024 * 1024); // 1GB default
    ~GPUMemoryPool();
    
    /**
     * Allocate GPU memory block
     * @param size Size in bytes
     * @return Pointer to allocated memory, or nullptr if allocation failed
     */
    void* allocate(size_t size);
    
    /**
     * Release GPU memory block
     * @param ptr Pointer to memory block
     */
    void deallocate(void* ptr);
    
    /**
     * Get memory pool statistics
     */
    struct PoolStats {
        size_t total_size;
        size_t allocated_size;
        size_t available_size;
        size_t num_allocated_blocks;
        double utilization_percentage;
    };
    
    PoolStats get_stats() const;

private:
    std::vector<GPUMemoryBlock> memory_blocks_;
    void* base_memory_ptr_;
    size_t total_size_;
    std::mutex pool_mutex_;
    
    void initialize_memory_pool();
};

/**
 * GPU Signature Verification Algorithms
 * 
 * Optimized implementations of cryptographic signature verification
 * algorithms for GPU parallel execution.
 */
namespace gpu_crypto {

/**
 * Ed25519 signature verification on GPU
 * @param signatures Batch of signatures to verify
 * @param messages Batch of message hashes
 * @param public_keys Batch of public keys
 * @param results Output array for verification results
 * @param batch_size Number of signatures to verify
 * @return true if GPU kernel executed successfully
 */
bool verify_ed25519_batch_gpu(
    const uint8_t* signatures,
    const uint8_t* messages,
    const uint8_t* public_keys,
    bool* results,
    size_t batch_size
);

/**
 * ECDSA signature verification on GPU
 * @param signatures Batch of signatures to verify  
 * @param messages Batch of message hashes
 * @param public_keys Batch of public keys
 * @param results Output array for verification results
 * @param batch_size Number of signatures to verify
 * @return true if GPU kernel executed successfully
 */
bool verify_ecdsa_batch_gpu(
    const uint8_t* signatures,
    const uint8_t* messages,
    const uint8_t* public_keys,
    bool* results,
    size_t batch_size
);

} // namespace gpu_crypto

} // namespace performance
} // namespace slonana