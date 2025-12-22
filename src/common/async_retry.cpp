#include "common/async_retry.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>

namespace slonana {
namespace common {

// ============================================================================
// AsyncRetryPolicy Implementation
// ============================================================================

AsyncRetryPolicy AsyncRetryPolicy::from_sync_policy(const RetryPolicy& policy) {
    AsyncRetryPolicy async_policy;
    async_policy.max_attempts = policy.max_attempts;
    async_policy.initial_delay = policy.initial_delay;
    async_policy.max_delay = policy.max_delay;
    async_policy.backoff_multiplier = policy.backoff_multiplier;
    async_policy.jitter_factor = policy.jitter_factor;
    async_policy.retry_on_timeout = policy.retry_on_timeout;
    async_policy.retry_on_connection_error = policy.retry_on_connection_error;
    async_policy.retry_on_transient_error = policy.retry_on_transient_error;
    async_policy.strategy = RetryStrategy::EXPONENTIAL_BACKOFF;
    return async_policy;
}

AsyncRetryPolicy AsyncRetryPolicy::create_rpc_policy() {
    AsyncRetryPolicy policy;
    policy.max_attempts = 3;
    policy.initial_delay = std::chrono::milliseconds(100);
    policy.max_delay = std::chrono::milliseconds(2000);
    policy.backoff_multiplier = 2.0;
    policy.jitter_factor = 0.1;
    policy.retry_on_timeout = true;
    policy.retry_on_connection_error = true;
    policy.retry_on_transient_error = true;
    policy.strategy = RetryStrategy::EXPONENTIAL_BACKOFF;
    policy.operation_timeout = std::chrono::milliseconds(10000);
    policy.total_timeout = std::chrono::milliseconds(30000);
    return policy;
}

AsyncRetryPolicy AsyncRetryPolicy::create_network_policy() {
    AsyncRetryPolicy policy;
    policy.max_attempts = 5;
    policy.initial_delay = std::chrono::milliseconds(200);
    policy.max_delay = std::chrono::milliseconds(5000);
    policy.backoff_multiplier = 2.5;
    policy.jitter_factor = 0.15;
    policy.retry_on_timeout = true;
    policy.retry_on_connection_error = true;
    policy.retry_on_transient_error = true;
    policy.strategy = RetryStrategy::EXPONENTIAL_BACKOFF;
    policy.operation_timeout = std::chrono::milliseconds(15000);
    policy.total_timeout = std::chrono::milliseconds(60000);
    return policy;
}

AsyncRetryPolicy AsyncRetryPolicy::create_storage_policy() {
    AsyncRetryPolicy policy;
    policy.max_attempts = 4;
    policy.initial_delay = std::chrono::milliseconds(150);
    policy.max_delay = std::chrono::milliseconds(3000);
    policy.backoff_multiplier = 2.0;
    policy.jitter_factor = 0.1;
    policy.retry_on_timeout = false; // Storage ops should be fast
    policy.retry_on_connection_error = true;
    policy.retry_on_transient_error = true;
    policy.strategy = RetryStrategy::EXPONENTIAL_BACKOFF;
    policy.operation_timeout = std::chrono::milliseconds(5000);
    policy.total_timeout = std::chrono::milliseconds(20000);
    return policy;
}

// ============================================================================
// BackoffCalculator Implementation
// ============================================================================

BackoffCalculator::BackoffCalculator(const AsyncRetryPolicy& policy)
    : policy_(policy) {
    std::random_device rd;
    rng_.seed(rd());
}

std::chrono::milliseconds BackoffCalculator::calculate_delay(uint32_t attempt) {
    std::chrono::milliseconds delay;
    
    switch (policy_.strategy) {
        case RetryStrategy::EXPONENTIAL_BACKOFF:
            delay = exponential_backoff(attempt);
            break;
        case RetryStrategy::LINEAR_BACKOFF:
            delay = linear_backoff(attempt);
            break;
        case RetryStrategy::FIXED_DELAY:
            delay = fixed_delay(attempt);
            break;
        case RetryStrategy::FIBONACCI_BACKOFF:
            delay = fibonacci_backoff(attempt);
            break;
        case RetryStrategy::CUSTOM:
            if (policy_.custom_backoff) {
                delay = policy_.custom_backoff(attempt);
            } else {
                delay = exponential_backoff(attempt); // Fallback
            }
            break;
        default:
            delay = exponential_backoff(attempt);
    }
    
    delay = apply_jitter(delay);
    delay = clamp_delay(delay);
    
    return delay;
}

std::chrono::milliseconds BackoffCalculator::exponential_backoff(uint32_t attempt) {
    // delay = initial_delay * (backoff_multiplier ^ (attempt - 1))
    double multiplier = std::pow(policy_.backoff_multiplier, attempt - 1);
    auto delay_ms = static_cast<long>(policy_.initial_delay.count() * multiplier);
    return std::chrono::milliseconds(delay_ms);
}

std::chrono::milliseconds BackoffCalculator::linear_backoff(uint32_t attempt) {
    // delay = initial_delay * attempt
    auto delay_ms = policy_.initial_delay.count() * attempt;
    return std::chrono::milliseconds(delay_ms);
}

std::chrono::milliseconds BackoffCalculator::fixed_delay(uint32_t /* attempt */) {
    return policy_.initial_delay;
}

std::chrono::milliseconds BackoffCalculator::fibonacci_backoff(uint32_t attempt) {
    // Calculate Fibonacci number for attempt
    uint64_t fib_prev = 0;
    uint64_t fib_curr = 1;
    
    for (uint32_t i = 2; i <= attempt; ++i) {
        uint64_t fib_next = fib_prev + fib_curr;
        fib_prev = fib_curr;
        fib_curr = fib_next;
    }
    
    auto delay_ms = policy_.initial_delay.count() * fib_curr;
    return std::chrono::milliseconds(delay_ms);
}

std::chrono::milliseconds BackoffCalculator::apply_jitter(std::chrono::milliseconds delay) {
    if (policy_.jitter_factor <= 0.0) {
        return delay;
    }
    
    auto delay_count = delay.count();
    auto max_jitter = static_cast<long>(delay_count * policy_.jitter_factor);
    
    // Prevent overflow and ensure reasonable bounds
    if (max_jitter > 1000) max_jitter = 1000; // Cap jitter at 1 second
    if (max_jitter < 1) max_jitter = 1;       // Minimum jitter of 1ms
    
    std::uniform_int_distribution<long> jitter_dist(-max_jitter, max_jitter);
    auto jittered_delay = delay + std::chrono::milliseconds(jitter_dist(rng_));
    
    // Ensure non-negative delay
    if (jittered_delay.count() < 0) {
        jittered_delay = std::chrono::milliseconds(0);
    }
    
    return jittered_delay;
}

std::chrono::milliseconds BackoffCalculator::clamp_delay(std::chrono::milliseconds delay) {
    return std::min(delay, policy_.max_delay);
}

// ============================================================================
// AsyncRetryExecutor Implementation
// ============================================================================

AsyncRetryExecutor::AsyncRetryExecutor(size_t num_workers)
    : num_workers_(num_workers > 0 ? num_workers : std::thread::hardware_concurrency() * 2),
      running_(false) {
    
    // Ensure at least 2 workers
    if (num_workers_ < 2) {
        num_workers_ = 2;
    }
}

AsyncRetryExecutor::~AsyncRetryExecutor() {
    if (running_.load(std::memory_order_acquire)) {
        shutdown();
    }
}

bool AsyncRetryExecutor::initialize() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        LOG_WARN("AsyncRetryExecutor already initialized");
        return false;
    }
    
    try {
        // Start worker threads
        workers_.reserve(num_workers_);
        for (size_t i = 0; i < num_workers_; ++i) {
            workers_.emplace_back(&AsyncRetryExecutor::worker_loop, this);
        }
        
        LOG_INFO("AsyncRetryExecutor initialized with " + std::to_string(num_workers_) + " workers");
        return true;
        
    } catch (const std::exception& e) {
        running_.store(false, std::memory_order_release);
        LOG_ERROR("Failed to initialize AsyncRetryExecutor: " + std::string(e.what()));
        return false;
    }
}

void AsyncRetryExecutor::shutdown(std::chrono::milliseconds timeout) {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        LOG_WARN("AsyncRetryExecutor already shut down");
        return;
    }
    
    LOG_INFO("Shutting down AsyncRetryExecutor...");
    
    // Wake up all workers
    queue_cv_.notify_all();
    
    // Wait for workers to finish with timeout
    auto deadline = std::chrono::steady_clock::now() + timeout;
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            auto remaining = deadline - std::chrono::steady_clock::now();
            if (remaining > std::chrono::milliseconds(0)) {
                // Note: std::thread doesn't support timed join, so we just join
                worker.join();
            } else {
                // Timeout exceeded, detach remaining threads
                LOG_WARN("Worker thread did not finish within timeout, detaching");
                worker.detach();
            }
        }
    }
    
    workers_.clear();
    
    // Clear remaining tasks
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!task_queue_.empty()) {
            task_queue_.pop();
        }
        pending_operations_.store(0, std::memory_order_release);
    }
    
    LOG_INFO("AsyncRetryExecutor shut down complete");
}

AsyncRetryExecutor::Stats AsyncRetryExecutor::get_stats() const {
    Stats stats;
    stats.total_operations = total_operations_.load(std::memory_order_relaxed);
    stats.successful_operations = successful_operations_.load(std::memory_order_relaxed);
    stats.failed_operations = failed_operations_.load(std::memory_order_relaxed);
    stats.cancelled_operations = cancelled_operations_.load(std::memory_order_relaxed);
    stats.total_retry_attempts = total_retry_attempts_.load(std::memory_order_relaxed);
    stats.pending_operations = pending_operations_.load(std::memory_order_relaxed);
    
    if (stats.total_operations > 0) {
        stats.average_attempts_per_operation = 
            static_cast<double>(stats.total_retry_attempts) / stats.total_operations;
    } else {
        stats.average_attempts_per_operation = 0.0;
    }
    
    // Average duration would require tracking, simplified for now
    stats.average_duration = std::chrono::milliseconds(0);
    
    return stats;
}

void AsyncRetryExecutor::reset_stats() {
    total_operations_.store(0, std::memory_order_relaxed);
    successful_operations_.store(0, std::memory_order_relaxed);
    failed_operations_.store(0, std::memory_order_relaxed);
    cancelled_operations_.store(0, std::memory_order_relaxed);
    total_retry_attempts_.store(0, std::memory_order_relaxed);
}

void AsyncRetryExecutor::worker_loop() {
    LOG_DEBUG("AsyncRetryExecutor worker thread started");
    
    while (running_.load(std::memory_order_acquire)) {
        Task task;
        
        // Wait for task or shutdown
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !task_queue_.empty() || !running_.load(std::memory_order_acquire);
            });
            
            if (!running_.load(std::memory_order_acquire) && task_queue_.empty()) {
                break;
            }
            
            if (!task_queue_.empty()) {
                task = std::move(task_queue_.front());
                task_queue_.pop();
            } else {
                continue;
            }
        }
        
        // Execute task
        try {
            task.work();
        } catch (const std::exception& e) {
            LOG_ERROR("Exception in worker thread: " + std::string(e.what()));
        } catch (...) {
            LOG_ERROR("Unknown exception in worker thread");
        }
    }
    
    LOG_DEBUG("AsyncRetryExecutor worker thread exiting");
}

bool AsyncRetryExecutor::should_retry_error(const std::string& error, const AsyncRetryPolicy& policy) {
    // Use custom error classifier if provided
    if (policy.error_classifier) {
        return policy.error_classifier(error);
    }
    
    // Default error classification
    std::string error_lower = error;
    std::transform(error_lower.begin(), error_lower.end(), error_lower.begin(), ::tolower);
    
    // Check for timeout errors
    if (policy.retry_on_timeout) {
        if (error_lower.find("timeout") != std::string::npos ||
            error_lower.find("timed out") != std::string::npos) {
            return true;
        }
    }
    
    // Check for connection errors
    if (policy.retry_on_connection_error) {
        if (error_lower.find("connection") != std::string::npos ||
            error_lower.find("connect") != std::string::npos ||
            error_lower.find("network") != std::string::npos ||
            error_lower.find("unreachable") != std::string::npos) {
            return true;
        }
    }
    
    // Check for transient errors
    if (policy.retry_on_transient_error) {
        if (error_lower.find("transient") != std::string::npos ||
            error_lower.find("temporary") != std::string::npos ||
            error_lower.find("503") != std::string::npos ||
            error_lower.find("429") != std::string::npos || // Rate limit
            error_lower.find("502") != std::string::npos || // Bad gateway
            error_lower.find("504") != std::string::npos) { // Gateway timeout
            return true;
        }
    }
    
    // Use FaultTolerance utility if available
    return FaultTolerance::is_retryable_error(error);
}

void AsyncRetryExecutor::notify_progress(const AsyncRetryPolicy& policy, const RetryProgress& progress) {
    if (policy.progress_callback) {
        try {
            policy.progress_callback(progress);
        } catch (const std::exception& e) {
            LOG_ERROR("Exception in progress callback: " + std::string(e.what()));
        } catch (...) {
            LOG_ERROR("Unknown exception in progress callback");
        }
    }
}

// ============================================================================
// Global Executor Instance
// ============================================================================

AsyncRetryExecutor& global_async_retry_executor() {
    static AsyncRetryExecutor executor;
    static std::once_flag init_flag;
    
    std::call_once(init_flag, [&]() {
        if (!executor.initialize()) {
            LOG_ERROR("Failed to initialize global async retry executor");
        }
    });
    
    return executor;
}

} // namespace common
} // namespace slonana
