#pragma once

/**
 * Async Retry Abstraction Layer
 * 
 * Provides non-blocking retry mechanisms with futures/promises for high-throughput
 * async operations. Addresses technical debt from blocking retry patterns while
 * maintaining backward compatibility.
 * 
 * Design Goals:
 * - Non-blocking: Return std::future immediately
 * - Composable: Support future chaining and combinators  
 * - Cancellable: Allow early termination of retry sequences
 * - Observable: Progress callbacks for retry attempts
 * - Testable: Clean dependency injection for mocking
 * 
 * Integration with existing infrastructure:
 * - Compatible with common/fault_tolerance.h retry policies
 * - Uses worker pool pattern from svm/async_bpf_execution.h
 * - Integrates with circuit breaker for failure protection
 */

#include "common/types.h"
#include "common/fault_tolerance.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <thread>
#include <vector>

namespace slonana {
namespace common {

// ============================================================================
// Async Retry Policy Configuration
// ============================================================================

/**
 * Retry strategy types
 */
enum class RetryStrategy {
    EXPONENTIAL_BACKOFF,  // Exponential delay with jitter (default)
    LINEAR_BACKOFF,       // Linear delay increase
    FIXED_DELAY,          // Constant delay between attempts
    FIBONACCI_BACKOFF,    // Fibonacci sequence delays
    CUSTOM                // User-defined backoff function
};

/**
 * Progress notification for retry attempts
 */
struct RetryProgress {
    uint32_t attempt_number;
    uint32_t max_attempts;
    std::chrono::milliseconds next_delay;
    std::string last_error;
    std::chrono::steady_clock::time_point attempt_time;
};

/**
 * Async-friendly retry policy configuration
 * 
 * Extends synchronous RetryPolicy with async-specific features:
 * - Progress callbacks for observability
 * - Cancellation token support
 * - Custom backoff strategies
 */
struct AsyncRetryPolicy {
    // Core retry parameters (compatible with RetryPolicy)
    uint32_t max_attempts = 3;
    std::chrono::milliseconds initial_delay{100};
    std::chrono::milliseconds max_delay{5000};
    double backoff_multiplier = 2.0;
    double jitter_factor = 0.1;
    
    // Retry conditions
    bool retry_on_timeout = true;
    bool retry_on_connection_error = true;
    bool retry_on_transient_error = true;
    
    // Async-specific features
    RetryStrategy strategy = RetryStrategy::EXPONENTIAL_BACKOFF;
    std::function<bool(const std::string&)> error_classifier = nullptr;
    std::function<void(const RetryProgress&)> progress_callback = nullptr;
    std::function<std::chrono::milliseconds(uint32_t)> custom_backoff = nullptr;
    
    // Timeout configuration
    std::chrono::milliseconds operation_timeout{30000};
    std::chrono::milliseconds total_timeout{0}; // 0 = no total timeout
    
    // Circuit breaker integration
    bool enable_circuit_breaker = false;
    std::string circuit_breaker_id;
    
    /**
     * Convert from synchronous RetryPolicy
     */
    static AsyncRetryPolicy from_sync_policy(const RetryPolicy& policy);
    
    /**
     * Create policy for specific operation types
     */
    static AsyncRetryPolicy create_rpc_policy();
    static AsyncRetryPolicy create_network_policy();
    static AsyncRetryPolicy create_storage_policy();
};

// ============================================================================
// Cancellation Token
// ============================================================================

/**
 * Cancellation token for aborting async retry operations
 * 
 * Thread-safe cancellation mechanism allowing early termination of
 * ongoing retry sequences from any thread.
 */
class CancellationToken {
public:
    CancellationToken() : cancelled_(false) {}
    
    /**
     * Request cancellation of the operation
     */
    void cancel() {
        cancelled_.store(true, std::memory_order_release);
    }
    
    /**
     * Check if cancellation was requested
     */
    bool is_cancelled() const {
        return cancelled_.load(std::memory_order_acquire);
    }
    
    /**
     * Reset cancellation state
     */
    void reset() {
        cancelled_.store(false, std::memory_order_release);
    }
    
private:
    std::atomic<bool> cancelled_;
};

using CancellationTokenPtr = std::shared_ptr<CancellationToken>;

// ============================================================================
// Type Traits for Result<T> Extraction
// ============================================================================

/**
 * Helper to extract T from Result<T>
 */
template<typename T>
struct result_value_type;

template<typename T>
struct result_value_type<Result<T>> {
    using type = T;
};

template<typename T>
using result_value_type_t = typename result_value_type<T>::type;

// ============================================================================
// Async Retry Result
// ============================================================================

/**
 * Result of an async retry operation
 * 
 * Wraps the operation result with retry metadata for observability
 */
template<typename T>
struct AsyncRetryResult {
    Result<T> result;
    uint32_t attempts_made;
    std::chrono::milliseconds total_duration;
    bool was_cancelled;
    std::vector<std::string> error_history;
    
    AsyncRetryResult() 
        : result("Not executed"), 
          attempts_made(0),
          total_duration(0),
          was_cancelled(false) {}
    
    explicit AsyncRetryResult(Result<T> r)
        : result(std::move(r)),
          attempts_made(1),
          total_duration(0),
          was_cancelled(false) {}
    
    bool is_ok() const { return result.is_ok(); }
    bool is_err() const { return result.is_err(); }
    
    const T& unwrap() const { return result.value(); }
    
    const std::string& error() const { return result.error(); }
};

// ============================================================================
// Backoff Calculator
// ============================================================================

/**
 * Calculates retry delays based on configured strategy
 * 
 * Implements various backoff algorithms with jitter to prevent
 * thundering herd problems.
 */
class BackoffCalculator {
public:
    explicit BackoffCalculator(const AsyncRetryPolicy& policy);
    
    /**
     * Calculate delay for given attempt number
     * @param attempt Attempt number (1-based)
     * @return Delay duration with jitter applied
     */
    std::chrono::milliseconds calculate_delay(uint32_t attempt);
    
private:
    AsyncRetryPolicy policy_;
    std::mt19937 rng_;
    
    std::chrono::milliseconds exponential_backoff(uint32_t attempt);
    std::chrono::milliseconds linear_backoff(uint32_t attempt);
    std::chrono::milliseconds fixed_delay(uint32_t attempt);
    std::chrono::milliseconds fibonacci_backoff(uint32_t attempt);
    std::chrono::milliseconds apply_jitter(std::chrono::milliseconds delay);
    std::chrono::milliseconds clamp_delay(std::chrono::milliseconds delay);
};

// ============================================================================
// Async Retry Executor
// ============================================================================

/**
 * Non-blocking retry executor using worker thread pool
 * 
 * Core async retry engine that executes operations with configurable
 * retry policies without blocking the calling thread. Returns futures
 * for async result retrieval.
 * 
 * Thread-Safety: All public methods are thread-safe
 * Lifecycle: Must call initialize() before use, shutdown() for cleanup
 */
class AsyncRetryExecutor {
public:
    /**
     * Create executor with specified worker count
     * @param num_workers Number of worker threads (0 = auto-detect)
     */
    explicit AsyncRetryExecutor(size_t num_workers = 0);
    ~AsyncRetryExecutor();
    
    // Disable copy/move
    AsyncRetryExecutor(const AsyncRetryExecutor&) = delete;
    AsyncRetryExecutor& operator=(const AsyncRetryExecutor&) = delete;
    
    /**
     * Initialize the executor and start worker threads
     */
    bool initialize();
    
    /**
     * Shutdown the executor and wait for pending operations
     * @param timeout Maximum time to wait for operations to complete
     */
    void shutdown(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));
    
    /**
     * Check if executor is running
     */
    bool is_running() const { return running_.load(std::memory_order_acquire); }
    
    /**
     * Execute operation with retry policy
     * 
     * @param operation Function to execute, should return Result<T>
     * @param policy Retry policy configuration
     * @param cancellation_token Optional cancellation token
     * @return Future for AsyncRetryResult<T>
     * 
     * Example:
     *   auto future = executor.execute_with_retry(
     *       []() { return fetch_data(); },
     *       AsyncRetryPolicy::create_network_policy()
     *   );
     *   auto result = future.get(); // Blocks until complete
     */
    template<typename F>
    auto execute_with_retry(
        F&& operation,
        const AsyncRetryPolicy& policy = {},
        CancellationTokenPtr cancellation_token = nullptr
    ) -> std::future<AsyncRetryResult<result_value_type_t<std::invoke_result_t<F>>>>;
    
    /**
     * Execute multiple operations concurrently with retry
     * 
     * @param operations Vector of operations to execute
     * @param policy Retry policy for all operations
     * @return Vector of futures for results
     */
    template<typename F>
    auto execute_batch_with_retry(
        std::vector<F> operations,
        const AsyncRetryPolicy& policy = {}
    ) -> std::vector<std::future<AsyncRetryResult<result_value_type_t<std::invoke_result_t<F>>>>>;
    
    /**
     * Get executor statistics
     */
    struct Stats {
        uint64_t total_operations;
        uint64_t successful_operations;
        uint64_t failed_operations;
        uint64_t cancelled_operations;
        uint64_t total_retry_attempts;
        uint64_t pending_operations;
        double average_attempts_per_operation;
        std::chrono::milliseconds average_duration;
    };
    
    Stats get_stats() const;
    
    /**
     * Reset statistics
     */
    void reset_stats();
    
private:
    size_t num_workers_;
    std::vector<std::thread> workers_;
    std::atomic<bool> running_;
    
    // Task queue
    struct Task {
        std::function<void()> work;
        std::chrono::steady_clock::time_point enqueue_time;
    };
    
    std::queue<Task> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // Statistics (atomic for lock-free reads)
    std::atomic<uint64_t> total_operations_{0};
    std::atomic<uint64_t> successful_operations_{0};
    std::atomic<uint64_t> failed_operations_{0};
    std::atomic<uint64_t> cancelled_operations_{0};
    std::atomic<uint64_t> total_retry_attempts_{0};
    std::atomic<uint64_t> pending_operations_{0};
    
    // Worker thread function
    void worker_loop();
    
    // Internal retry execution logic
    template<typename F, typename T>
    AsyncRetryResult<T> execute_with_retry_internal(
        F&& operation,
        const AsyncRetryPolicy& policy,
        CancellationTokenPtr cancellation_token
    );
    
    bool should_retry_error(const std::string& error, const AsyncRetryPolicy& policy);
    void notify_progress(const AsyncRetryPolicy& policy, const RetryProgress& progress);
};

// ============================================================================
// Global Executor Instance (Singleton Pattern)
// ============================================================================

/**
 * Get global async retry executor instance
 * 
 * Lazy-initialized singleton executor for convenient access.
 * Automatically initialized with optimal worker count.
 */
AsyncRetryExecutor& global_async_retry_executor();

// ============================================================================
// Convenience Functions
// ============================================================================

/**
 * Execute operation with retry using global executor
 * 
 * Convenience wrapper around global_async_retry_executor().execute_with_retry()
 * 
 * @param operation Function to execute
 * @param policy Retry policy
 * @param cancellation_token Optional cancellation token
 * @return Future for result
 */
template<typename F>
auto async_retry(
    F&& operation,
    const AsyncRetryPolicy& policy = {},
    CancellationTokenPtr cancellation_token = nullptr
) -> std::future<AsyncRetryResult<result_value_type_t<std::invoke_result_t<F>>>> {
    return global_async_retry_executor().execute_with_retry(
        std::forward<F>(operation),
        policy,
        cancellation_token
    );
}

/**
 * Execute operation with retry and block for result (backward compatibility helper)
 * 
 * Provides synchronous interface by blocking on future.get()
 * Useful for gradual migration from blocking to async code.
 * 
 * @param operation Function to execute
 * @param policy Retry policy
 * @return Result after retries complete
 */
template<typename F>
auto sync_retry(
    F&& operation,
    const AsyncRetryPolicy& policy = {}
) -> AsyncRetryResult<result_value_type_t<std::invoke_result_t<F>>> {
    auto future = async_retry(std::forward<F>(operation), policy);
    return future.get();
}

// ============================================================================
// Template Implementation (Header-Only for Type Deduction)
// ============================================================================

template<typename F>
auto AsyncRetryExecutor::execute_with_retry(
    F&& operation,
    const AsyncRetryPolicy& policy,
    CancellationTokenPtr cancellation_token
) -> std::future<AsyncRetryResult<result_value_type_t<std::invoke_result_t<F>>>> {
    
    using ResultType = result_value_type_t<std::invoke_result_t<F>>;
    using RetryResultType = AsyncRetryResult<ResultType>;
    
    // Create promise/future pair
    auto promise = std::make_shared<std::promise<RetryResultType>>();
    auto future = promise->get_future();
    
    // Create cancellation token if not provided
    if (!cancellation_token) {
        cancellation_token = std::make_shared<CancellationToken>();
    }
    
    // Package work into task
    Task task;
    task.enqueue_time = std::chrono::steady_clock::now();
    task.work = [this, 
                  op = std::forward<F>(operation),
                  policy,
                  cancellation_token,
                  promise]() mutable {
        try {
            auto result = execute_with_retry_internal<decltype(op), ResultType>(
                std::move(op),
                policy,
                cancellation_token
            );
            promise->set_value(std::move(result));
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };
    
    // Enqueue task
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(std::move(task));
        pending_operations_.fetch_add(1, std::memory_order_relaxed);
    }
    queue_cv_.notify_one();
    
    total_operations_.fetch_add(1, std::memory_order_relaxed);
    
    return future;
}

template<typename F>
auto AsyncRetryExecutor::execute_batch_with_retry(
    std::vector<F> operations,
    const AsyncRetryPolicy& policy
) -> std::vector<std::future<AsyncRetryResult<result_value_type_t<std::invoke_result_t<F>>>>> {
    
    using ResultType = result_value_type_t<std::invoke_result_t<F>>;
    std::vector<std::future<AsyncRetryResult<ResultType>>> futures;
    futures.reserve(operations.size());
    
    for (auto& op : operations) {
        futures.push_back(execute_with_retry(std::move(op), policy));
    }
    
    return futures;
}

template<typename F, typename T>
AsyncRetryResult<T> AsyncRetryExecutor::execute_with_retry_internal(
    F&& operation,
    const AsyncRetryPolicy& policy,
    CancellationTokenPtr cancellation_token
) {
    AsyncRetryResult<T> retry_result;
    retry_result.attempts_made = 0;
    
    auto start_time = std::chrono::steady_clock::now();
    BackoffCalculator backoff(policy);
    
    for (uint32_t attempt = 1; attempt <= policy.max_attempts; ++attempt) {
        // Check cancellation
        if (cancellation_token && cancellation_token->is_cancelled()) {
            retry_result.was_cancelled = true;
            retry_result.result = Result<T>("Operation cancelled");
            cancelled_operations_.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        
        // Check total timeout
        if (policy.total_timeout.count() > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time
            );
            if (elapsed >= policy.total_timeout) {
                retry_result.result = Result<T>("Total timeout exceeded");
                retry_result.error_history.push_back("Total timeout exceeded");
                failed_operations_.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }
        
        retry_result.attempts_made = attempt;
        total_retry_attempts_.fetch_add(1, std::memory_order_relaxed);
        
        // Execute operation
        auto result = operation();
        
        // Success case
        if (result.is_ok()) {
            retry_result.result = std::move(result);
            successful_operations_.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        
        // Failure case
        std::string error = result.error();
        retry_result.error_history.push_back(error);
        
        // Check if we should retry this error
        if (!should_retry_error(error, policy)) {
            retry_result.result = std::move(result);
            failed_operations_.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        
        // Last attempt - don't delay
        if (attempt == policy.max_attempts) {
            retry_result.result = std::move(result);
            failed_operations_.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        
        // Calculate delay and notify progress
        auto delay = backoff.calculate_delay(attempt);
        
        if (policy.progress_callback) {
            RetryProgress progress;
            progress.attempt_number = attempt;
            progress.max_attempts = policy.max_attempts;
            progress.next_delay = delay;
            progress.last_error = error;
            progress.attempt_time = std::chrono::steady_clock::now();
            notify_progress(policy, progress);
        }
        
        // Sleep with cancellation check
        auto sleep_end = std::chrono::steady_clock::now() + delay;
        while (std::chrono::steady_clock::now() < sleep_end) {
            if (cancellation_token && cancellation_token->is_cancelled()) {
                retry_result.was_cancelled = true;
                retry_result.result = Result<T>("Operation cancelled during backoff");
                cancelled_operations_.fetch_add(1, std::memory_order_relaxed);
                goto cleanup;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
cleanup:
    auto end_time = std::chrono::steady_clock::now();
    retry_result.total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time
    );
    
    pending_operations_.fetch_sub(1, std::memory_order_relaxed);
    
    return retry_result;
}

} // namespace common
} // namespace slonana
