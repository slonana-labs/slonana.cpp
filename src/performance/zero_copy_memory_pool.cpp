#include "performance/zero_copy_memory_pool.h"
#include <algorithm>
#include <cpuid.h>
#include <cstring>
#include <numa.h>
#include <sys/mman.h>

namespace slonana {
namespace performance {

ZeroCopyMemoryPool::ZeroCopyMemoryPool() {
  // Initialize all chunks as available
  for (auto &chunk : pool_) {
    chunk.in_use.store(false, std::memory_order_relaxed);
    chunk.allocation_id = 0;
  }
}

ZeroCopyMemoryPool::~ZeroCopyMemoryPool() {
  // All chunks should be released by their handles
  // No explicit cleanup needed due to RAII design
}

ZeroCopyMemoryPool::ZeroCopyHandle ZeroCopyMemoryPool::allocate_chunk() {
  const size_t start_index = next_chunk_index_.load(std::memory_order_relaxed);

  // Try to find an available chunk starting from next_chunk_index_
  for (size_t i = 0; i < POOL_SIZE; ++i) {
    const size_t index = (start_index + i) % POOL_SIZE;
    MemoryChunk &chunk = pool_[index];

    // Try to acquire chunk atomically
    bool expected = false;
    if (chunk.in_use.compare_exchange_weak(expected, true,
                                           std::memory_order_acquire)) {
      // Successfully acquired chunk
      chunk.allocation_id =
          allocation_counter_.fetch_add(1, std::memory_order_relaxed);

      // Update next_chunk_index_ for faster future allocations
      next_chunk_index_.store((index + 1) % POOL_SIZE,
                              std::memory_order_relaxed);

      return ZeroCopyHandle(&chunk, this);
    }
  }

  // No available chunks
  failed_allocation_counter_.fetch_add(1, std::memory_order_relaxed);
  return ZeroCopyHandle(nullptr, this);
}

void ZeroCopyMemoryPool::release_chunk(MemoryChunk *chunk) {
  if (chunk) {
    chunk->in_use.store(false, std::memory_order_release);
  }
}

ZeroCopyMemoryPool::PoolStats ZeroCopyMemoryPool::get_stats() const {
  PoolStats stats{};
  stats.total_chunks = POOL_SIZE;
  stats.allocated_chunks = 0;

  // Count allocated chunks
  for (const auto &chunk : pool_) {
    if (chunk.in_use.load(std::memory_order_acquire)) {
      stats.allocated_chunks++;
    }
  }

  stats.available_chunks = stats.total_chunks - stats.allocated_chunks;
  stats.utilization_percentage =
      (static_cast<double>(stats.allocated_chunks) / stats.total_chunks) *
      100.0;
  stats.total_allocations = allocation_counter_.load(std::memory_order_relaxed);
  stats.failed_allocations =
      failed_allocation_counter_.load(std::memory_order_relaxed);

  return stats;
}

void ZeroCopyMemoryPool::enable_huge_pages() {
  // Advise kernel to use huge pages for better TLB performance
  if (madvise(pool_.data(), sizeof(pool_), MADV_HUGEPAGE) == -1) {
    // Non-fatal error, continue without huge pages
    return;
  }
}

void ZeroCopyMemoryPool::set_numa_node(int node_id) {
  if (numa_available() == -1) {
    return; // NUMA not available
  }

  // Bind memory allocation to specific NUMA node
  numa_set_preferred(node_id);
}

// SIMD Transaction Processor Implementation

size_t SIMDTransactionProcessor::process_batch_simd(
    const std::array<SIMDTransactionData, SIMD_BATCH_SIZE> &batch,
    std::array<bool, SIMD_BATCH_SIZE> &results) {
  if (!check_avx2_support()) {
    // Fallback to scalar processing if AVX2 not available
    size_t success_count = 0;
    for (size_t i = 0; i < SIMD_BATCH_SIZE; ++i) {
      // Simplified verification for demonstration
      results[i] = (batch[i].amount > 0 && batch[i].fee > 0);
      if (results[i]) {
        success_count++;
      }
    }
    return success_count;
  }

  // Extract signature and message hashes for vectorized verification
  std::array<__m256i, SIMD_BATCH_SIZE> signatures;
  std::array<__m256i, SIMD_BATCH_SIZE> messages;

  for (size_t i = 0; i < SIMD_BATCH_SIZE; ++i) {
    signatures[i] = batch[i].signature_hash;
    messages[i] = batch[i].message_hash;
  }

  // Perform vectorized signature verification
  verify_signatures_simd(signatures, messages, results);

  // Count successful verifications
  size_t success_count = 0;
  for (bool result : results) {
    if (result) {
      success_count++;
    }
  }

  return success_count;
}

void SIMDTransactionProcessor::verify_signatures_simd(
    const std::array<__m256i, SIMD_BATCH_SIZE> &signatures,
    const std::array<__m256i, SIMD_BATCH_SIZE> &messages,
    std::array<bool, SIMD_BATCH_SIZE> &results) {
  // Vectorized signature verification using AVX2
  // This is a simplified implementation - real ed25519 verification would be
  // more complex

  for (size_t i = 0; i < SIMD_BATCH_SIZE; ++i) {
    // XOR signature with message hash as a simple verification check
    __m256i result_vec = _mm256_xor_si256(signatures[i], messages[i]);

    // Check if result is non-zero (simplified verification)
    __m256i zero = _mm256_setzero_si256();
    __m256i cmp = _mm256_cmpeq_epi8(result_vec, zero);

    // Extract comparison mask
    int mask = _mm256_movemask_epi8(cmp);

    // If mask is not all 1s, signature verification passes
    results[i] = (mask != -1);
  }
}

bool SIMDTransactionProcessor::check_avx2_support() {
  unsigned int eax, ebx, ecx, edx;

  // Check for AVX2 support (CPUID leaf 7, subleaf 0, EBX bit 5)
  if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
    return (ebx & (1 << 5)) != 0;
  }

  return false;
}

bool SIMDTransactionProcessor::check_avx512_support() {
  unsigned int eax, ebx, ecx, edx;

  // Check for AVX-512F support (CPUID leaf 7, subleaf 0, EBX bit 16)
  if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
    return (ebx & (1 << 16)) != 0;
  }

  return false;
}

} // namespace performance
} // namespace slonana