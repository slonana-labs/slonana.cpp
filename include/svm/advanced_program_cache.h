#pragma once

#include "common/types.h"
#include <atomic>
#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace slonana {
namespace svm {

using namespace slonana::common;

/**
 * Compiled program metadata
 */
struct CompiledProgram {
  PublicKey program_id;
  std::vector<uint8_t> bytecode;
  std::vector<uint8_t> compiled_code;  // JIT compiled machine code
  std::chrono::steady_clock::time_point compilation_time;
  std::chrono::steady_clock::time_point last_access_time;
  uint64_t execution_count;
  uint64_t compilation_cost_us;  // Microseconds to compile
  bool is_precompiled;
  size_t memory_usage;
  
  CompiledProgram(const PublicKey& id, const std::vector<uint8_t>& code)
      : program_id(id), bytecode(code), compilation_time(std::chrono::steady_clock::now()),
        last_access_time(std::chrono::steady_clock::now()), execution_count(0),
        compilation_cost_us(0), is_precompiled(false), memory_usage(code.size()) {}
};

/**
 * Cache statistics for monitoring
 */
struct CacheStatistics {
  std::atomic<size_t> total_programs{0};
  std::atomic<size_t> cache_hits{0};
  std::atomic<size_t> cache_misses{0};
  std::atomic<size_t> compilations{0};
  std::atomic<size_t> evictions{0};
  std::atomic<size_t> precompiled_programs{0};
  std::atomic<uint64_t> total_compilation_time_us{0};
  std::atomic<uint64_t> total_memory_usage{0};
  std::chrono::steady_clock::time_point last_gc_run;
  
  // Copy constructor
  CacheStatistics(const CacheStatistics& other) 
      : total_programs(other.total_programs.load())
      , cache_hits(other.cache_hits.load())
      , cache_misses(other.cache_misses.load())
      , compilations(other.compilations.load())
      , evictions(other.evictions.load())
      , precompiled_programs(other.precompiled_programs.load())
      , total_compilation_time_us(other.total_compilation_time_us.load())
      , total_memory_usage(other.total_memory_usage.load())
      , last_gc_run(other.last_gc_run) {}
  
  // Assignment operator
  CacheStatistics& operator=(const CacheStatistics& other) {
    if (this != &other) {
      total_programs.store(other.total_programs.load());
      cache_hits.store(other.cache_hits.load());
      cache_misses.store(other.cache_misses.load());
      compilations.store(other.compilations.load());
      evictions.store(other.evictions.load());
      precompiled_programs.store(other.precompiled_programs.load());
      total_compilation_time_us.store(other.total_compilation_time_us.load());
      total_memory_usage.store(other.total_memory_usage.load());
      last_gc_run = other.last_gc_run;
    }
    return *this;
  }
  
  // Default constructor
  CacheStatistics() : last_gc_run(std::chrono::steady_clock::now()) {}
  
  // Calculate hit rate
  double get_hit_rate() const {
    uint64_t hits = cache_hits.load();
    uint64_t misses = cache_misses.load();
    uint64_t total = hits + misses;
    return total > 0 ? (double)hits / total : 0.0;
  }
  
  // Calculate average compilation time
  double get_avg_compilation_time_us() const {
    uint64_t compilations_count = compilations.load();
    uint64_t total_time = total_compilation_time_us.load();
    return compilations_count > 0 ? (double)total_time / compilations_count : 0.0;
  }
};

/**
 * Advanced Program Cache with JIT compilation and smart eviction
 */
class AdvancedProgramCache {
public:
  struct Configuration {
    size_t max_cache_size;
    size_t max_memory_usage_mb;
    uint32_t gc_frequency_seconds;
    uint32_t max_compilation_time_ms;
    bool enable_precompilation;
    bool enable_aggressive_gc;
    bool enable_compilation_metrics;
    double eviction_threshold;  // Evict when cache > threshold * max_size
    
    Configuration()
        : max_cache_size(1000)
        , max_memory_usage_mb(512)
        , gc_frequency_seconds(60)
        , max_compilation_time_ms(100)
        , enable_precompilation(true)
        , enable_aggressive_gc(false)
        , enable_compilation_metrics(true)
        , eviction_threshold(0.8) {}
  };
  
  explicit AdvancedProgramCache(const Configuration& config = Configuration{});
  ~AdvancedProgramCache();
  
  // Core cache operations
  std::shared_ptr<CompiledProgram> get_program(const PublicKey& program_id);
  bool cache_program(const PublicKey& program_id, const std::vector<uint8_t>& bytecode);
  bool precompile_program(const PublicKey& program_id, const std::vector<uint8_t>& bytecode);
  void invalidate_program(const PublicKey& program_id);
  void clear_cache();
  
  // Compilation management
  bool compile_program(CompiledProgram& program);
  std::vector<PublicKey> get_precompilation_candidates() const;
  void background_precompilation();
  
  // Cache management
  void garbage_collect();
  void evict_least_recently_used(size_t count);
  void evict_by_memory_pressure();
  
  // Performance optimization
  void optimize_cache_layout();
  void prefetch_related_programs(const PublicKey& program_id);
  
  // Statistics and monitoring
  CacheStatistics get_statistics() const;
  void reset_statistics();
  std::vector<std::pair<PublicKey, uint64_t>> get_most_used_programs(size_t count = 10) const;
  void print_cache_statistics() const;
  
  // Configuration
  Configuration get_configuration() const { return config_; }
  void update_configuration(const Configuration& new_config);
  
  // Debugging and analysis
  void print_cache_contents() const;
  bool verify_cache_consistency() const;
  std::string get_cache_report() const;
  
private:
  Configuration config_;
  mutable CacheStatistics stats_;
  
  // LRU Cache implementation
  struct CacheEntry {
    std::shared_ptr<CompiledProgram> program;
    std::list<PublicKey>::iterator lru_it;
    
    CacheEntry() = default;
    CacheEntry(std::shared_ptr<CompiledProgram> p) : program(p) {}
  };
  
  mutable std::unordered_map<PublicKey, CacheEntry> cache_map_;
  mutable std::list<PublicKey> lru_list_;
  mutable std::shared_mutex cache_mutex_;
  
  // Background threads
  std::thread gc_thread_;
  std::thread precompilation_thread_;
  std::atomic<bool> should_stop_{false};
  
  // Compilation engine
  bool jit_compile_bytecode(const std::vector<uint8_t>& bytecode, 
                           std::vector<uint8_t>& compiled_code,
                           uint64_t& compilation_time_us);
  
  // Cache management internals
  void move_to_front(const PublicKey& program_id);
  bool should_evict() const;
  size_t calculate_memory_usage() const;
  void update_access_pattern(const PublicKey& program_id);
  
  // Background workers
  void gc_worker_loop();
  void precompilation_worker_loop();
  
  // Smart eviction strategies
  std::vector<PublicKey> select_eviction_candidates(size_t count) const;
  double calculate_eviction_score(const CompiledProgram& program) const;
  
  // Memory management
  void compact_memory();
  bool is_memory_pressure() const;
  
  // Performance optimization helpers
  void analyze_usage_patterns();
  void optimize_compilation_parameters();
};

/**
 * Program Cache Manager - High-level interface
 */
class ProgramCacheManager {
public:
  explicit ProgramCacheManager(const AdvancedProgramCache::Configuration& config = {});
  ~ProgramCacheManager() = default;
  
  // High-level operations
  bool load_program(const PublicKey& program_id, const std::vector<uint8_t>& bytecode);
  std::shared_ptr<CompiledProgram> execute_program(const PublicKey& program_id);
  void warm_up_cache(const std::vector<PublicKey>& program_ids);
  
  // Performance monitoring
  void start_performance_monitoring();
  void stop_performance_monitoring();
  AdvancedProgramCache::Configuration optimize_configuration();
  
  // Statistics
  CacheStatistics get_performance_stats() const;
  void generate_performance_report(const std::string& filename) const;
  
private:
  std::unique_ptr<AdvancedProgramCache> cache_;
  std::atomic<bool> monitoring_enabled_{false};
  std::thread monitoring_thread_;
  
  void monitoring_worker_loop();
  void log_performance_metrics();
};

} // namespace svm
} // namespace slonana