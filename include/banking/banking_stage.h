#pragma once

#include "common/types.h"
#include "ledger/manager.h"
#include <memory>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <chrono>
#include <unordered_map>
#include <future>

namespace slonana {
namespace banking {

/**
 * Transaction Batch represents a group of transactions processed together
 */
class TransactionBatch {
public:
    using TransactionPtr = std::shared_ptr<ledger::Transaction>;
    
    TransactionBatch();
    explicit TransactionBatch(std::vector<TransactionPtr> transactions);
    ~TransactionBatch();
    
    // Batch operations
    void add_transaction(TransactionPtr transaction);
    const std::vector<TransactionPtr>& get_transactions() const { return transactions_; }
    size_t size() const { return transactions_.size(); }
    bool empty() const { return transactions_.empty(); }
    
    // Batch properties
    uint64_t get_batch_id() const { return batch_id_; }
    std::chrono::steady_clock::time_point get_creation_time() const { return creation_time_; }
    
    // Execution state
    enum class State {
        PENDING,
        PROCESSING,
        COMPLETED,
        FAILED
    };
    
    State get_state() const { return state_; }
    void set_state(State state) { state_ = state; }
    
    // Results
    void set_results(std::vector<bool> results) { results_ = std::move(results); }
    const std::vector<bool>& get_results() const { return results_; }
    
private:
    uint64_t batch_id_;
    std::vector<TransactionPtr> transactions_;
    std::chrono::steady_clock::time_point creation_time_;
    State state_;
    std::vector<bool> results_;
    
    static std::atomic<uint64_t> next_batch_id_;
};

/**
 * Transaction Pipeline Stage represents a processing stage in the banking pipeline
 */
class PipelineStage {
public:
    using ProcessFunction = std::function<bool(std::shared_ptr<TransactionBatch>)>;
    
    PipelineStage(const std::string& name, ProcessFunction process_fn);
    ~PipelineStage();
    
    // Stage lifecycle
    bool start();
    bool stop();
    bool is_running() const { return running_; }
    
    // Processing
    void submit_batch(std::shared_ptr<TransactionBatch> batch);
    void set_next_stage(std::shared_ptr<PipelineStage> next_stage) { next_stage_ = next_stage; }
    
    // Configuration
    void set_batch_timeout(std::chrono::milliseconds timeout) { batch_timeout_ = timeout; }
    void set_max_parallel_batches(size_t max_batches) { max_parallel_batches_ = max_batches; }
    
    // Statistics
    size_t get_processed_batches() const { return processed_batches_; }
    size_t get_failed_batches() const { return failed_batches_; }
    size_t get_pending_batches() const;
    double get_average_processing_time_ms() const;
    
    const std::string& get_name() const { return name_; }
    
private:
    std::string name_;
    ProcessFunction process_fn_;
    std::shared_ptr<PipelineStage> next_stage_;
    
    bool running_;
    std::chrono::milliseconds batch_timeout_;
    size_t max_parallel_batches_;
    
    // Processing queue
    std::queue<std::shared_ptr<TransactionBatch>> batch_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // Worker threads
    std::vector<std::thread> workers_;
    bool should_stop_;
    
    // Statistics
    std::atomic<size_t> processed_batches_;
    std::atomic<size_t> failed_batches_;
    std::atomic<uint64_t> total_processing_time_ms_;
    
    void worker_loop();
    void process_batch(std::shared_ptr<TransactionBatch> batch);
};

/**
 * Resource Monitor tracks CPU and memory usage for optimal performance
 */
class ResourceMonitor {
public:
    ResourceMonitor();
    ~ResourceMonitor();
    
    // Monitoring lifecycle
    bool start();
    bool stop();
    
    // Resource metrics
    double get_cpu_usage() const { return cpu_usage_; }
    size_t get_memory_usage_mb() const { return memory_usage_mb_; }
    size_t get_peak_memory_usage_mb() const { return peak_memory_usage_mb_; }
    
    // Thresholds
    void set_cpu_threshold(double threshold) { cpu_threshold_ = threshold; }
    void set_memory_threshold_mb(size_t threshold) { memory_threshold_mb_ = threshold; }
    
    // Alerts
    bool is_cpu_overloaded() const { return cpu_usage_ > cpu_threshold_; }
    bool is_memory_overloaded() const { return memory_usage_mb_ > memory_threshold_mb_; }
    
private:
    bool monitoring_;
    std::thread monitor_thread_;
    
    std::atomic<double> cpu_usage_;
    std::atomic<size_t> memory_usage_mb_;
    std::atomic<size_t> peak_memory_usage_mb_;
    
    double cpu_threshold_;
    size_t memory_threshold_mb_;
    
    void monitor_loop();
    double calculate_cpu_usage();
    size_t calculate_memory_usage();
};

/**
 * Enhanced Banking Stage provides multi-stage transaction processing pipeline
 * Compatible with Agave's banking stage for high-throughput transaction processing
 */
class BankingStage {
public:
    using TransactionPtr = std::shared_ptr<ledger::Transaction>;
    using CompletionCallback = std::function<void(std::shared_ptr<TransactionBatch>)>;
    
    BankingStage();
    ~BankingStage();
    
    // Banking stage lifecycle
    bool initialize();
    bool start();
    bool stop();
    bool shutdown();
    bool is_running() const { return running_; }
    
    // Transaction processing
    void submit_transaction(TransactionPtr transaction);
    void submit_transactions(std::vector<TransactionPtr> transactions);
    std::future<bool> process_transaction_async(TransactionPtr transaction);
    
    // Batch processing
    void submit_batch(std::shared_ptr<TransactionBatch> batch);
    void set_batch_size(size_t batch_size) { batch_size_ = batch_size; }
    void set_batch_timeout(std::chrono::milliseconds timeout) { batch_timeout_ = timeout; }
    
    // Callback registration
    void set_completion_callback(CompletionCallback callback) { completion_callback_ = callback; }
    
    // Pipeline configuration
    void set_parallel_stages(size_t stages) { parallel_stages_ = stages; }
    void set_max_concurrent_batches(size_t max_batches) { max_concurrent_batches_ = max_batches; }
    void enable_adaptive_batching(bool enabled) { adaptive_batching_enabled_ = enabled; }
    
    // Performance tuning
    void set_worker_thread_count(size_t count) { worker_thread_count_ = count; }
    void enable_resource_monitoring(bool enabled) { resource_monitoring_enabled_ = enabled; }
    
    // Statistics and monitoring
    struct Statistics {
        size_t total_transactions_processed;
        size_t total_batches_processed;
        size_t failed_transactions;
        size_t pending_transactions;
        double average_batch_processing_time_ms;
        double transactions_per_second;
        double cpu_usage;
        size_t memory_usage_mb;
        std::chrono::milliseconds uptime;
    };
    
    Statistics get_statistics() const;
    size_t get_pending_transaction_count() const;
    double get_throughput_tps() const;
    
    // Advanced features
    void enable_priority_processing(bool enabled) { priority_processing_enabled_ = enabled; }
    void set_transaction_priority(TransactionPtr transaction, int priority);
    
private:
    bool initialized_;
    bool running_;
    
    // Pipeline stages
    std::vector<std::shared_ptr<PipelineStage>> pipeline_stages_;
    std::shared_ptr<PipelineStage> validation_stage_;
    std::shared_ptr<PipelineStage> execution_stage_;
    std::shared_ptr<PipelineStage> commitment_stage_;
    
    // Configuration
    size_t batch_size_;
    std::chrono::milliseconds batch_timeout_;
    size_t parallel_stages_;
    size_t max_concurrent_batches_;
    size_t worker_thread_count_;
    bool adaptive_batching_enabled_;
    bool resource_monitoring_enabled_;
    bool priority_processing_enabled_;
    
    // Transaction queue
    std::queue<TransactionPtr> transaction_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // Batching
    std::shared_ptr<TransactionBatch> current_batch_;
    std::mutex batch_mutex_;
    std::thread batch_processor_;
    bool should_stop_;
    
    // Callbacks
    CompletionCallback completion_callback_;
    
    // Resource monitoring
    std::unique_ptr<ResourceMonitor> resource_monitor_;
    
    // Statistics
    std::atomic<size_t> total_transactions_processed_;
    std::atomic<size_t> total_batches_processed_;
    std::atomic<size_t> failed_transactions_;
    std::atomic<uint64_t> total_processing_time_ms_;
    std::chrono::steady_clock::time_point start_time_;
    
    // Priority queue for priority processing
    std::priority_queue<std::pair<int, TransactionPtr>> priority_queue_;
    std::unordered_map<TransactionPtr, int> transaction_priorities_;
    mutable std::mutex priority_mutex_;
    
    // Internal methods
    void initialize_pipeline();
    void process_batches();
    void create_batch_if_needed();
    void process_transaction_queue();
    
    // Pipeline stage functions
    bool validate_batch(std::shared_ptr<TransactionBatch> batch);
    bool execute_batch(std::shared_ptr<TransactionBatch> batch);
    bool commit_batch(std::shared_ptr<TransactionBatch> batch);
    
    // Adaptive batching
    void adjust_batch_size_if_needed();
    size_t calculate_optimal_batch_size();
    
    // Resource management
    bool should_throttle_processing() const;
    void handle_resource_pressure();
};

} // namespace banking
} // namespace slonana