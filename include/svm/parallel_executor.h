#pragma once

#include "svm/engine.h"
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace slonana {
namespace svm {

// Forward declarations
class ParallelExecutor;
class DependencyAnalyzer;
class MemoryPool;
class ThreadPool;

enum class ExecutionStrategy {
    SEQUENTIAL,        // Traditional sequential execution
    PARALLEL_BASIC,    // Basic parallel execution with simple dependency checking
    PARALLEL_OPTIMIZED,// Optimized parallel execution with advanced scheduling
    SPECULATIVE,       // Speculative execution with rollback capability
    HYBRID            // Hybrid approach combining strategies
};

struct ExecutionTask {
    std::string task_id;
    std::string program_id;
    std::vector<uint8_t> bytecode;
    std::vector<AccountInfo> accounts;
    std::vector<uint8_t> instruction_data;
    std::vector<std::string> read_accounts;
    std::vector<std::string> write_accounts;
    uint64_t priority = 0;
    std::chrono::steady_clock::time_point submission_time;
    std::function<void(ExecutionResult)> completion_callback;
    
    // Dependency tracking
    std::vector<std::string> dependencies;
    std::atomic<bool> ready_to_execute{false};
    std::atomic<bool> is_executing{false};
    std::atomic<bool> is_completed{false};
};

struct ParallelExecutionStats {
    uint64_t total_tasks_executed = 0;
    uint64_t parallel_tasks_executed = 0;
    uint64_t sequential_tasks_executed = 0;
    uint64_t dependency_conflicts = 0;
    uint64_t speculative_rollbacks = 0;
    double average_parallelism_factor = 0.0;
    uint64_t total_execution_time_us = 0;
    uint64_t parallel_execution_time_us = 0;
    double speedup_ratio = 1.0;
    size_t max_concurrent_tasks = 0;
    size_t current_active_tasks = 0;
};

// Dependency analysis for parallel execution
class DependencyAnalyzer {
private:
    std::unordered_map<std::string, std::unordered_set<std::string>> account_dependencies_;
    std::mutex dependency_mutex_;
    
public:
    DependencyAnalyzer();
    
    // Analyze task dependencies
    std::vector<std::string> analyze_dependencies(const ExecutionTask& task);
    bool has_conflict(const ExecutionTask& task1, const ExecutionTask& task2);
    bool can_execute_parallel(const std::vector<ExecutionTask*>& tasks);
    
    // Account access tracking
    void register_account_access(const std::string& task_id, const std::vector<std::string>& read_accounts, 
                                const std::vector<std::string>& write_accounts);
    void unregister_task(const std::string& task_id);
    
    // Conflict detection
    enum class ConflictType {
        NONE,
        READ_WRITE_CONFLICT,
        WRITE_WRITE_CONFLICT,
        PROGRAM_CONFLICT
    };
    
    ConflictType detect_conflict(const ExecutionTask& task1, const ExecutionTask& task2);
    std::vector<std::string> find_conflicting_tasks(const ExecutionTask& task);
    
    // Graph analysis
    std::vector<std::vector<ExecutionTask*>> build_execution_groups(std::vector<ExecutionTask*>& tasks);
    bool has_cyclic_dependency(const std::vector<ExecutionTask*>& tasks);
    
private:
    bool is_read_write_conflict(const std::string& account, const ExecutionTask& reader, const ExecutionTask& writer);
    bool is_write_write_conflict(const std::string& account, const ExecutionTask& writer1, const ExecutionTask& writer2);
    void extract_account_accesses(const ExecutionTask& task, std::vector<std::string>& reads, std::vector<std::string>& writes);
};

// Thread pool for parallel execution
class ThreadPool {
private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_{false};
    size_t num_threads_;
    
    // Load balancing
    std::vector<std::atomic<size_t>> thread_task_counts_;
    std::atomic<size_t> total_tasks_queued_{0};
    
public:
    ThreadPool(size_t num_threads);
    ~ThreadPool();
    
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;
    
    size_t get_thread_count() const { return num_threads_; }
    size_t get_queue_size() const;
    size_t get_active_tasks() const;
    void resize(size_t new_size);
    
private:
    void worker_loop(size_t thread_id);
    size_t get_least_busy_thread() const;
};

// Memory pool for efficient allocation
class MemoryPool {
private:
    struct MemoryBlock {
        void* data;
        size_t size;
        bool in_use;
        std::chrono::steady_clock::time_point last_used;
        size_t alignment;
    };
    
    std::vector<MemoryBlock> blocks_;
    std::mutex pool_mutex_;
    size_t total_allocated_ = 0;
    size_t total_requested_ = 0;
    size_t max_pool_size_;
    size_t block_size_;
    
    // Statistics
    std::atomic<uint64_t> allocations_{0};
    std::atomic<uint64_t> deallocations_{0};
    std::atomic<uint64_t> pool_hits_{0};
    std::atomic<uint64_t> pool_misses_{0};
    
public:
    MemoryPool(size_t max_size_mb = 256, size_t default_block_size = 4096);
    ~MemoryPool();
    
    void* allocate(size_t size, size_t alignment = 8);
    void deallocate(void* ptr);
    void* reallocate(void* ptr, size_t new_size);
    
    // Pool management
    void clear_unused_blocks();
    void defragment();
    size_t get_total_allocated() const { return total_allocated_; }
    size_t get_total_requested() const { return total_requested_; }
    double get_pool_efficiency() const;
    
    // Statistics
    uint64_t get_allocation_count() const { return allocations_.load(); }
    uint64_t get_deallocation_count() const { return deallocations_.load(); }
    uint64_t get_pool_hits() const { return pool_hits_.load(); }
    uint64_t get_pool_misses() const { return pool_misses_.load(); }
    double get_hit_ratio() const;
    
private:
    MemoryBlock* find_free_block(size_t size, size_t alignment);
    MemoryBlock* allocate_new_block(size_t size, size_t alignment);
    void merge_free_blocks();
    size_t align_size(size_t size, size_t alignment) const;
};

// Speculative execution manager
class SpeculativeExecutor {
private:
    struct SpeculativeState {
        std::string task_id;
        std::unordered_map<std::string, std::vector<uint8_t>> account_snapshots;
        ExecutionResult result;
        bool is_valid = true;
        std::chrono::steady_clock::time_point start_time;
    };
    
    std::unordered_map<std::string, SpeculativeState> speculative_states_;
    std::mutex speculation_mutex_;
    
public:
    SpeculativeExecutor();
    
    bool begin_speculation(const std::string& task_id, const std::vector<AccountInfo>& accounts);
    ExecutionResult execute_speculatively(const ExecutionTask& task);
    bool validate_speculation(const std::string& task_id, const std::vector<AccountInfo>& current_accounts);
    void commit_speculation(const std::string& task_id);
    void rollback_speculation(const std::string& task_id);
    
    size_t get_active_speculations() const;
    void clear_expired_speculations(std::chrono::milliseconds max_age);
    
private:
    std::vector<uint8_t> create_account_snapshot(const AccountInfo& account);
    bool compare_account_state(const AccountInfo& account, const std::vector<uint8_t>& snapshot);
};

// Main parallel execution engine
class ParallelExecutor {
private:
    ExecutionStrategy strategy_;
    std::unique_ptr<ThreadPool> thread_pool_;
    std::unique_ptr<DependencyAnalyzer> dependency_analyzer_;
    std::unique_ptr<MemoryPool> memory_pool_;
    std::unique_ptr<SpeculativeExecutor> speculative_executor_;
    
    // Task management
    std::queue<std::unique_ptr<ExecutionTask>> pending_tasks_;
    std::vector<std::unique_ptr<ExecutionTask>> active_tasks_;
    std::mutex task_mutex_;
    std::condition_variable task_cv_;
    
    // Execution control
    std::atomic<bool> running_{false};
    std::thread scheduler_thread_;
    std::thread monitor_thread_;
    
    // Configuration
    size_t max_parallel_tasks_ = 16;
    size_t dependency_lookahead_ = 100;
    bool enable_speculation_ = false;
    std::chrono::milliseconds speculation_timeout_{1000};
    
    // Statistics
    ParallelExecutionStats stats_;
    mutable std::mutex stats_mutex_;
    
public:
    ParallelExecutor(ExecutionStrategy strategy = ExecutionStrategy::PARALLEL_OPTIMIZED,
                    size_t num_threads = std::thread::hardware_concurrency());
    ~ParallelExecutor();
    
    bool initialize();
    void shutdown();
    
    // Configuration
    void set_strategy(ExecutionStrategy strategy) { strategy_ = strategy; }
    void set_max_parallel_tasks(size_t max_tasks) { max_parallel_tasks_ = max_tasks; }
    void enable_speculation(bool enable) { enable_speculation_ = enable; }
    void set_thread_count(size_t count);
    
    // Task submission
    std::string submit_task(
        const std::string& program_id,
        const std::vector<uint8_t>& bytecode,
        const std::vector<AccountInfo>& accounts,
        const std::vector<uint8_t>& instruction_data,
        uint64_t priority = 0
    );
    
    std::future<ExecutionResult> submit_task_async(
        const std::string& program_id,
        const std::vector<uint8_t>& bytecode,
        const std::vector<AccountInfo>& accounts,
        const std::vector<uint8_t>& instruction_data,
        uint64_t priority = 0
    );
    
    // Batch execution
    std::vector<ExecutionResult> execute_batch(
        const std::vector<ExecutionTask>& tasks,
        bool preserve_order = true
    );
    
    std::vector<std::future<ExecutionResult>> execute_batch_async(
        const std::vector<ExecutionTask>& tasks
    );
    
    // Status and monitoring
    ParallelExecutionStats get_stats() const;
    size_t get_pending_task_count() const;
    size_t get_active_task_count() const;
    void reset_stats();
    
    // Resource management
    size_t get_memory_usage() const;
    double get_cpu_utilization() const;
    void optimize_thread_allocation();
    void clear_caches();
    
private:
    void scheduler_loop();
    void monitor_loop();
    
    std::vector<ExecutionTask*> select_ready_tasks();
    bool can_execute_task(const ExecutionTask& task);
    void execute_task_group(std::vector<ExecutionTask*>& tasks);
    void execute_single_task(ExecutionTask* task);
    
    void update_dependencies();
    void rebalance_threads();
    void collect_statistics();
    
    ExecutionResult execute_sequential(const ExecutionTask& task);
    ExecutionResult execute_parallel(const ExecutionTask& task);
    ExecutionResult execute_speculative(const ExecutionTask& task);
    
    std::string generate_task_id();
    void complete_task(ExecutionTask* task, const ExecutionResult& result);
};

// Configuration for parallel execution
struct ParallelExecutionConfig {
    ExecutionStrategy strategy = ExecutionStrategy::PARALLEL_OPTIMIZED;
    size_t num_threads = std::thread::hardware_concurrency();
    size_t max_parallel_tasks = 16;
    size_t dependency_lookahead = 100;
    bool enable_speculation = false;
    std::chrono::milliseconds speculation_timeout{1000};
    
    // Memory pool configuration
    size_t memory_pool_size_mb = 256;
    size_t memory_block_size = 4096;
    
    // Performance tuning
    bool enable_numa_optimization = false;
    bool enable_cpu_affinity = false;
    bool enable_dynamic_scheduling = true;
    bool enable_load_balancing = true;
    
    // Monitoring
    bool enable_detailed_stats = false;
    std::chrono::milliseconds stats_collection_interval{1000};
};

// Factory for creating parallel executors
class ParallelExecutorFactory {
public:
    static std::unique_ptr<ParallelExecutor> create_optimized_executor(
        const ParallelExecutionConfig& config
    );
    
    static std::unique_ptr<ParallelExecutor> create_sequential_executor();
    
    static std::unique_ptr<ParallelExecutor> create_speculative_executor(
        const ParallelExecutionConfig& config
    );
    
    static ParallelExecutionConfig get_optimal_config();
    static void benchmark_configurations(std::vector<ParallelExecutionConfig>& configs);
};

// Utility functions for parallel execution
namespace parallel_utils {
    size_t get_optimal_thread_count();
    size_t get_optimal_memory_pool_size();
    std::vector<std::string> detect_account_conflicts(const std::vector<ExecutionTask>& tasks);
    
    // Performance analysis
    double calculate_parallelization_efficiency(const ParallelExecutionStats& stats);
    double calculate_speedup_factor(uint64_t sequential_time, uint64_t parallel_time);
    std::vector<std::string> identify_bottlenecks(const ParallelExecutionStats& stats);
    
    // System optimization
    void configure_numa_policy();
    void set_cpu_affinity(const std::vector<size_t>& cpu_cores);
    void optimize_scheduler_parameters();
    void enable_performance_monitoring();
    
    // Task optimization
    std::vector<ExecutionTask> optimize_task_order(std::vector<ExecutionTask> tasks);
    std::vector<std::vector<ExecutionTask>> partition_tasks_by_dependency(
        const std::vector<ExecutionTask>& tasks
    );
    
    uint64_t estimate_task_execution_time(const ExecutionTask& task);
    uint64_t calculate_critical_path_length(const std::vector<ExecutionTask>& tasks);
}

}} // namespace slonana::svm