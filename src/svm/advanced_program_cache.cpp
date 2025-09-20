#include "svm/advanced_program_cache.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

namespace slonana {
namespace svm {

AdvancedProgramCache::AdvancedProgramCache(const Configuration &config)
    : config_(config) {
  stats_.last_gc_run = std::chrono::steady_clock::now();

  // Start background threads
  gc_thread_ = std::thread(&AdvancedProgramCache::gc_worker_loop, this);

  if (config_.enable_precompilation) {
    precompilation_thread_ =
        std::thread(&AdvancedProgramCache::precompilation_worker_loop, this);
  }

  std::cout << "AdvancedProgramCache initialized with max size: "
            << config_.max_cache_size
            << ", max memory: " << config_.max_memory_usage_mb << "MB"
            << std::endl;
}

AdvancedProgramCache::~AdvancedProgramCache() {
  should_stop_ = true;

  if (gc_thread_.joinable()) {
    gc_thread_.join();
  }

  if (precompilation_thread_.joinable()) {
    precompilation_thread_.join();
  }
}

std::shared_ptr<CompiledProgram>
AdvancedProgramCache::get_program(const PublicKey &program_id) {
  std::shared_lock<std::shared_mutex> lock(cache_mutex_);

  auto it = cache_map_.find(program_id);
  if (it != cache_map_.end()) {
    // Cache hit
    stats_.cache_hits++;

    // Update access time and move to front
    auto &program = it->second.program;
    program->last_access_time = std::chrono::steady_clock::now();
    program->execution_count++;

    // Move to front of LRU list (requires upgrading to unique lock)
    lock.unlock();
    std::unique_lock<std::shared_mutex> write_lock(cache_mutex_);
    move_to_front(program_id);

    return program;
  } else {
    // Cache miss
    stats_.cache_misses++;
    return nullptr;
  }
}

bool AdvancedProgramCache::cache_program(const PublicKey &program_id,
                                         const std::vector<uint8_t> &bytecode) {
  std::unique_lock<std::shared_mutex> lock(cache_mutex_);

  // Check if already cached
  auto it = cache_map_.find(program_id);
  if (it != cache_map_.end()) {
    move_to_front(program_id);
    return true;
  }

  // Create new compiled program
  auto program = std::make_shared<CompiledProgram>(program_id, bytecode);

  // Compile the program
  lock.unlock();
  bool compilation_success = compile_program(*program);
  lock.lock();

  if (!compilation_success) {
    return false;
  }

  // Check if we need to evict before adding
  if (should_evict()) {
    size_t evict_count = std::max(1UL, cache_map_.size() / 10);
    evict_least_recently_used(evict_count);
  }

  // Add to cache
  lru_list_.push_front(program_id);
  cache_map_.emplace(program_id, CacheEntry(program));
  cache_map_[program_id].lru_it = lru_list_.begin();

  // Update statistics
  stats_.total_programs++;
  stats_.total_memory_usage += program->memory_usage;

  std::cout << "Cached program: " << program_id.size()
            << " bytes, compilation time: " << program->compilation_cost_us
            << "μs" << std::endl;

  return true;
}

bool AdvancedProgramCache::precompile_program(
    const PublicKey &program_id, const std::vector<uint8_t> &bytecode) {
  if (!config_.enable_precompilation) {
    return false;
  }

  auto program = std::make_shared<CompiledProgram>(program_id, bytecode);

  if (!compile_program(*program)) {
    return false;
  }

  program->is_precompiled = true;

  std::unique_lock<std::shared_mutex> lock(cache_mutex_);

  // Add to cache
  lru_list_.push_front(program_id);
  cache_map_.emplace(program_id, CacheEntry(program));
  cache_map_[program_id].lru_it = lru_list_.begin();

  stats_.precompiled_programs++;
  stats_.total_programs++;
  stats_.total_memory_usage += program->memory_usage;

  std::cout << "Precompiled program: " << program_id.size() << " bytes"
            << std::endl;

  return true;
}

void AdvancedProgramCache::invalidate_program(const PublicKey &program_id) {
  std::unique_lock<std::shared_mutex> lock(cache_mutex_);

  auto it = cache_map_.find(program_id);
  if (it != cache_map_.end()) {
    stats_.total_memory_usage -= it->second.program->memory_usage;
    lru_list_.erase(it->second.lru_it);
    cache_map_.erase(it);
    stats_.total_programs--;
  }
}

void AdvancedProgramCache::clear_cache() {
  std::unique_lock<std::shared_mutex> lock(cache_mutex_);

  cache_map_.clear();
  lru_list_.clear();
  stats_.total_programs = 0;
  stats_.total_memory_usage = 0;

  std::cout << "Program cache cleared" << std::endl;
}

bool AdvancedProgramCache::compile_program(CompiledProgram &program) {
  auto start_time = std::chrono::high_resolution_clock::now();

  // Simple JIT compilation simulation
  bool success = jit_compile_bytecode(program.bytecode, program.compiled_code,
                                      program.compilation_cost_us);

  if (success) {
    auto end_time = std::chrono::high_resolution_clock::now();
    program.compilation_cost_us =
        std::chrono::duration_cast<std::chrono::microseconds>(end_time -
                                                              start_time)
            .count();
    program.memory_usage =
        program.bytecode.size() + program.compiled_code.size();

    stats_.compilations++;
    stats_.total_compilation_time_us += program.compilation_cost_us;
  }

  return success;
}

bool AdvancedProgramCache::jit_compile_bytecode(
    const std::vector<uint8_t> &bytecode, std::vector<uint8_t> &compiled_code,
    uint64_t &compilation_time_us) {
  auto start_time = std::chrono::high_resolution_clock::now();

  try {
    // Simulate JIT compilation - in production this would use LLVM or similar
    compiled_code.clear();
    compiled_code.reserve(bytecode.size() * 2); // Estimated size

    // Simple transformation: expand each bytecode instruction
    for (uint8_t byte : bytecode) {
      // Simulate instruction expansion
      compiled_code.push_back(0x48); // REX prefix
      compiled_code.push_back(byte);

      // Add some complexity to simulate real compilation
      if (byte > 0x80) {
        compiled_code.push_back(0x00);
        compiled_code.push_back(0x01);
      }
    }

    // Simulate compilation overhead
    std::this_thread::sleep_for(
        std::chrono::microseconds(bytecode.size() / 10));

    auto end_time = std::chrono::high_resolution_clock::now();
    compilation_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                              end_time - start_time)
                              .count();

    return true;
  } catch (const std::exception &e) {
    std::cerr << "JIT compilation error: " << e.what() << std::endl;
    return false;
  }
}

void AdvancedProgramCache::move_to_front(const PublicKey &program_id) {
  auto cache_it = cache_map_.find(program_id);
  if (cache_it != cache_map_.end()) {
    // Remove from current position
    lru_list_.erase(cache_it->second.lru_it);

    // Add to front
    lru_list_.push_front(program_id);
    cache_it->second.lru_it = lru_list_.begin();
  }
}

bool AdvancedProgramCache::should_evict() const {
  return cache_map_.size() >=
             config_.max_cache_size * config_.eviction_threshold ||
         is_memory_pressure();
}

bool AdvancedProgramCache::is_memory_pressure() const {
  uint64_t current_memory_mb = stats_.total_memory_usage.load() / (1024 * 1024);
  return current_memory_mb >= config_.max_memory_usage_mb;
}

void AdvancedProgramCache::evict_least_recently_used(size_t count) {
  size_t evicted = 0;

  while (evicted < count && !lru_list_.empty()) {
    // Remove from back (least recently used)
    PublicKey lru_program = lru_list_.back();

    auto cache_it = cache_map_.find(lru_program);
    if (cache_it != cache_map_.end()) {
      stats_.total_memory_usage -= cache_it->second.program->memory_usage;
      cache_map_.erase(cache_it);
      stats_.evictions++;
    }

    lru_list_.pop_back();
    evicted++;
  }

  stats_.total_programs -= evicted;

  if (evicted > 0) {
    std::cout << "Evicted " << evicted << " programs from cache" << std::endl;
  }
}

void AdvancedProgramCache::garbage_collect() {
  std::unique_lock<std::shared_mutex> lock(cache_mutex_);

  auto now = std::chrono::steady_clock::now();
  auto cutoff_time =
      now - std::chrono::hours(1); // Remove programs not accessed in 1 hour

  std::vector<PublicKey> to_remove;

  for (const auto &[program_id, entry] : cache_map_) {
    if (entry.program->last_access_time < cutoff_time &&
        !entry.program->is_precompiled) {
      to_remove.push_back(program_id);
    }
  }

  for (const auto &program_id : to_remove) {
    auto it = cache_map_.find(program_id);
    if (it != cache_map_.end()) {
      stats_.total_memory_usage -= it->second.program->memory_usage;
      lru_list_.erase(it->second.lru_it);
      cache_map_.erase(it);
      stats_.evictions++;
    }
  }

  stats_.total_programs -= to_remove.size();
  stats_.last_gc_run = now;

  if (!to_remove.empty()) {
    std::cout << "GC: Removed " << to_remove.size() << " unused programs"
              << std::endl;
  }
}

void AdvancedProgramCache::gc_worker_loop() {
  while (!should_stop_) {
    std::this_thread::sleep_for(
        std::chrono::seconds(config_.gc_frequency_seconds));

    if (!should_stop_) {
      garbage_collect();
    }
  }
}

void AdvancedProgramCache::precompilation_worker_loop() {
  while (!should_stop_) {
    std::this_thread::sleep_for(
        std::chrono::seconds(30)); // Check every 30 seconds

    if (!should_stop_) {
      background_precompilation();
    }
  }
}

void AdvancedProgramCache::background_precompilation() {
  // This would analyze usage patterns and precompile frequently used programs
  // For now, it's a placeholder
  if (config_.enable_precompilation) {
    std::cout << "Background precompilation check..." << std::endl;
  }
}

CacheStatistics AdvancedProgramCache::get_statistics() const {
  std::shared_lock<std::shared_mutex> lock(cache_mutex_);

  CacheStatistics current_stats = stats_;
  current_stats.total_programs = cache_map_.size();

  return current_stats;
}

void AdvancedProgramCache::print_cache_statistics() const {
  auto stats = get_statistics();

  std::cout << "\n=== Program Cache Statistics ===" << std::endl;
  std::cout << "Total programs: " << stats.total_programs.load() << std::endl;
  std::cout << "Cache hits: " << stats.cache_hits.load() << std::endl;
  std::cout << "Cache misses: " << stats.cache_misses.load() << std::endl;
  std::cout << "Hit rate: " << (stats.get_hit_rate() * 100) << "%" << std::endl;
  std::cout << "Compilations: " << stats.compilations.load() << std::endl;
  std::cout << "Evictions: " << stats.evictions.load() << std::endl;
  std::cout << "Precompiled: " << stats.precompiled_programs.load()
            << std::endl;
  std::cout << "Memory usage: " << (stats.total_memory_usage.load() / 1024)
            << " KB" << std::endl;
  std::cout << "Avg compilation time: " << stats.get_avg_compilation_time_us()
            << " μs" << std::endl;
  std::cout << "===============================" << std::endl;
}

// ProgramCacheManager implementation
ProgramCacheManager::ProgramCacheManager(
    const AdvancedProgramCache::Configuration &config) {
  cache_ = std::make_unique<AdvancedProgramCache>(config);
}

bool ProgramCacheManager::load_program(const PublicKey &program_id,
                                       const std::vector<uint8_t> &bytecode) {
  return cache_->cache_program(program_id, bytecode);
}

std::shared_ptr<CompiledProgram>
ProgramCacheManager::execute_program(const PublicKey &program_id) {
  return cache_->get_program(program_id);
}

void ProgramCacheManager::warm_up_cache(
    const std::vector<PublicKey> &program_ids) {
  std::cout << "Warming up cache with " << program_ids.size() << " programs..."
            << std::endl;

  for (const auto &program_id : program_ids) {
    // In production, this would load bytecode from storage
    std::vector<uint8_t> dummy_bytecode(100, 0x42);
    cache_->precompile_program(program_id, dummy_bytecode);
  }
}

CacheStatistics ProgramCacheManager::get_performance_stats() const {
  return cache_->get_statistics();
}

void ProgramCacheManager::start_performance_monitoring() {
  monitoring_enabled_ = true;
  monitoring_thread_ =
      std::thread(&ProgramCacheManager::monitoring_worker_loop, this);
}

void ProgramCacheManager::stop_performance_monitoring() {
  monitoring_enabled_ = false;
  if (monitoring_thread_.joinable()) {
    monitoring_thread_.join();
  }
}

void ProgramCacheManager::monitoring_worker_loop() {
  while (monitoring_enabled_) {
    std::this_thread::sleep_for(std::chrono::seconds(10));

    if (monitoring_enabled_) {
      log_performance_metrics();
    }
  }
}

void ProgramCacheManager::log_performance_metrics() {
  auto stats = get_performance_stats();

  std::cout << "[CACHE MONITOR] Hit rate: " << (stats.get_hit_rate() * 100)
            << "%, Programs: " << stats.total_programs.load()
            << ", Memory: " << (stats.total_memory_usage.load() / 1024) << "KB"
            << std::endl;
}

} // namespace svm
} // namespace slonana