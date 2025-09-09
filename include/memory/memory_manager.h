#pragma once

#include "common/types.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace slonana {
namespace memory {

using namespace slonana::common;

enum class AllocationStrategy {
  FIRST_FIT,      // First available block that fits
  BEST_FIT,       // Smallest block that fits
  WORST_FIT,      // Largest available block
  BUDDY_SYSTEM,   // Buddy system allocation
  SLAB_ALLOCATOR, // Slab allocation for fixed sizes
  POOL_ALLOCATOR  // Pre-allocated pools
};

enum class MemoryRegion {
  STACK,        // Stack memory for execution
  HEAP,         // Heap memory for programs
  ACCOUNT_DATA, // Account data storage
  INSTRUCTION,  // Instruction cache
  METADATA,     // Metadata and headers
  TEMPORARY     // Temporary/scratch space
};

struct MemoryBlock {
  void *address;
  size_t size;
  size_t alignment;
  bool is_free;
  MemoryRegion region;
  std::chrono::steady_clock::time_point last_accessed;
  uint64_t access_count;
  std::string owner_id;

  // Fragmentation tracking
  MemoryBlock *prev;
  MemoryBlock *next;
  bool is_merged;
};

struct MemoryStats {
  size_t total_allocated = 0;
  size_t total_freed = 0;
  size_t current_usage = 0;
  size_t peak_usage = 0;
  size_t fragmentation_bytes = 0;
  double fragmentation_ratio = 0.0;

  // Performance metrics
  uint64_t allocation_count = 0;
  uint64_t deallocation_count = 0;
  uint64_t reallocation_count = 0;
  uint64_t total_allocation_time_ns = 0;
  uint64_t avg_allocation_time_ns = 0;

  // Cache metrics
  uint64_t cache_hits = 0;
  uint64_t cache_misses = 0;
  double cache_hit_ratio = 0.0;

  // Garbage collection metrics
  uint64_t gc_cycles = 0;
  uint64_t gc_time_ns = 0;
  size_t gc_bytes_collected = 0;
};

struct PoolConfig {
  AllocationStrategy strategy = AllocationStrategy::BUDDY_SYSTEM;
  size_t initial_size_mb = 64;
  size_t max_size_mb = 512;
  size_t block_size = 4096;
  size_t alignment = 8;
  bool enable_garbage_collection = true;
  bool enable_compression = false;
  bool enable_encryption = false;
  std::chrono::milliseconds gc_interval{5000};
  double gc_threshold = 0.8; // Trigger GC at 80% usage
};

// Base memory allocator interface
class IMemoryAllocator {
public:
  virtual ~IMemoryAllocator() = default;
  virtual void *allocate(size_t size, size_t alignment = 8) = 0;
  virtual void deallocate(void *ptr) = 0;
  virtual void *reallocate(void *ptr, size_t new_size) = 0;
  virtual size_t get_allocated_size(void *ptr) = 0;
  virtual MemoryStats get_stats() const = 0;
  virtual void defragment() = 0;
  virtual void garbage_collect() = 0;
};

// Memory manager that coordinates all allocators
class MemoryManager {
private:
  std::unique_ptr<IMemoryAllocator> primary_allocator_;

  PoolConfig config_;
  mutable std::mutex manager_mutex_;

  // Global statistics
  MemoryStats global_stats_;
  std::atomic<bool> monitoring_enabled_{false};

public:
  MemoryManager(const PoolConfig &config);
  ~MemoryManager();

  bool initialize();
  void shutdown();

  // Primary allocation interface
  void *allocate(size_t size, size_t alignment = 8);
  void *allocate_in_region(MemoryRegion region, size_t size,
                           size_t alignment = 8);
  void deallocate(void *ptr);
  void *reallocate(void *ptr, size_t new_size);

  // Specialized allocation
  void *allocate_stack_memory(size_t size);
  void *allocate_heap_memory(size_t size);
  void *allocate_account_memory(size_t size);
  void *allocate_temporary_memory(size_t size);

  // Memory operations
  void copy_memory(void *dest, const void *src, size_t size);
  void move_memory(void *dest, const void *src, size_t size);
  void zero_memory(void *ptr, size_t size);
  bool compare_memory(const void *ptr1, const void *ptr2, size_t size);

  // Management operations
  void defragment_all();
  void garbage_collect();
  void optimize_allocation_strategy();
  void clear_unused_memory();

  // Statistics and monitoring
  MemoryStats get_global_stats() const;
  MemoryStats get_allocator_stats(const std::string &allocator_name) const;
  void reset_statistics();
  void enable_monitoring(bool enable);

  // Configuration
  void update_config(const PoolConfig &new_config);
  PoolConfig get_config() const { return config_; }

private:
  IMemoryAllocator *select_allocator(size_t size);
  void update_global_statistics();
  void log_memory_usage();
  void detect_memory_leaks();

  size_t calculate_optimal_allocation_size(size_t requested_size);
  AllocationStrategy select_optimal_strategy(size_t size, MemoryRegion region);
};

// Memory optimization utilities
namespace memory_utils {
size_t align_size(size_t size, size_t alignment);
bool is_power_of_2(size_t value);
size_t next_power_of_2(size_t value);
size_t calculate_fragmentation(const std::vector<MemoryBlock> &blocks);

// Performance analysis
double calculate_allocation_efficiency(const MemoryStats &stats);
std::vector<std::string> identify_memory_bottlenecks(const MemoryStats &stats);
size_t estimate_optimal_pool_size(const MemoryStats &historical_stats);

// Memory patterns
bool detect_memory_leak(const MemoryStats &current,
                        const MemoryStats &previous);
std::vector<size_t>
analyze_allocation_patterns(const std::vector<size_t> &allocation_sizes);
AllocationStrategy
recommend_strategy(const std::vector<size_t> &allocation_patterns);

// System integration
size_t get_system_page_size();
size_t get_available_memory();
void configure_virtual_memory();
void enable_memory_protection();

// Debugging and profiling
void dump_memory_layout(const MemoryManager &manager,
                        const std::string &filename);
void profile_allocation_performance(MemoryManager &manager, size_t iterations);
void validate_heap_integrity(const MemoryManager &manager);
} // namespace memory_utils

} // namespace memory
} // namespace slonana