# Async Retry Migration Guide

**Version:** 1.0  
**Date:** 2025-12-22  
**Status:** Production Ready

---

## Overview

This guide helps you migrate from blocking retry patterns to the new asynchronous retry infrastructure in slonana.cpp. The async retry layer provides non-blocking retry semantics while maintaining full backward compatibility with existing code.

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Migration Patterns](#migration-patterns)
3. [API Reference](#api-reference)
4. [Performance Tuning](#performance-tuning)
5. [Best Practices](#best-practices)
6. [Troubleshooting](#troubleshooting)
7. [Examples](#examples)

---

## Quick Start

### Basic Usage

```cpp
#include "common/async_retry.h"

using namespace slonana::common;

// Define your operation
auto fetch_data = []() -> Result<std::string> {
    // Your network/RPC/storage operation here
    return Result<std::string>::ok("data");
};

// Execute with async retry (non-blocking)
auto future = async_retry(fetch_data, AsyncRetryPolicy::create_network_policy());

// Get result when ready (blocks until complete)
auto result = future.get();

if (result.is_ok()) {
    std::cout << "Success after " << result.attempts_made << " attempts\n";
    std::cout << "Data: " << result.unwrap() << "\n";
} else {
    std::cerr << "Failed: " << result.error() << "\n";
}
```

### Synchronous Compatibility Wrapper

For gradual migration, use `sync_retry()` which provides the same blocking behavior:

```cpp
// Drop-in replacement for blocking retry
auto result = sync_retry(fetch_data, AsyncRetryPolicy::create_network_policy());
```

---

## Migration Patterns

### Pattern 1: Simple Blocking Retry → Async Retry

**Before (Blocking):**
```cpp
#include "common/fault_tolerance.h"

auto operation = []() -> Result<int> {
    return fetch_from_network();
};

auto result = FaultTolerance::retry_with_backoff(
    operation,
    FaultTolerance::create_network_retry_policy()
);
```

**After (Async):**
```cpp
#include "common/async_retry.h"

auto operation = []() -> Result<int> {
    return fetch_from_network();
};

// Convert existing policy
auto sync_policy = FaultTolerance::create_network_retry_policy();
auto async_policy = FaultTolerance::to_async_policy(sync_policy);

// Or use async-optimized policy
auto async_policy = AsyncRetryPolicy::create_network_policy();

auto future = async_retry(operation, async_policy);
auto result = future.get(); // AsyncRetryResult<int>

if (result.is_ok()) {
    int value = result.unwrap();
}
```

### Pattern 2: RPC Server Retry Integration

**Before:**
```cpp
// In RpcServer class
template<typename F>
auto retry_operation(F&& operation) {
    return FaultTolerance::retry_with_backoff(
        std::forward<F>(operation),
        rpc_retry_policy_
    );
}
```

**After:**
```cpp
// Add async variant
template<typename F>
auto async_retry_operation(F&& operation) {
    auto async_policy = FaultTolerance::to_async_policy(rpc_retry_policy_);
    return async_retry(std::forward<F>(operation), async_policy);
}

// Keep sync version for backward compatibility
template<typename F>
auto retry_operation(F&& operation) {
    auto async_policy = FaultTolerance::to_async_policy(rpc_retry_policy_);
    return sync_retry(std::forward<F>(operation), async_policy);
}
```

### Pattern 3: Custom Error Classification

**Before:**
```cpp
// Relied on default error classification
auto result = FaultTolerance::retry_with_backoff(operation, policy);
```

**After:**
```cpp
AsyncRetryPolicy policy = AsyncRetryPolicy::create_network_policy();

// Custom error classifier
policy.error_classifier = [](const std::string& error) {
    // Retry only specific errors
    return error.find("RETRY_ME") != std::string::npos ||
           error.find("TRANSIENT") != std::string::npos;
};

auto future = async_retry(operation, policy);
```

### Pattern 4: Progress Monitoring

**New capability with async retry:**

```cpp
AsyncRetryPolicy policy = AsyncRetryPolicy::create_network_policy();

// Add progress callback
policy.progress_callback = [](const RetryProgress& progress) {
    std::cout << "Retry attempt " << progress.attempt_number 
              << "/" << progress.max_attempts
              << ", next delay: " << progress.next_delay.count() << "ms\n";
    std::cout << "Last error: " << progress.last_error << "\n";
};

auto future = async_retry(operation, policy);
```

### Pattern 5: Cancellable Operations

**New capability with async retry:**

```cpp
auto cancellation_token = std::make_shared<CancellationToken>();

// Start async retry
auto future = async_retry(operation, policy, cancellation_token);

// Cancel from another thread if needed
std::thread canceller([cancellation_token]() {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    cancellation_token->cancel();
});

auto result = future.get();
if (result.was_cancelled) {
    std::cout << "Operation was cancelled\n";
}

canceller.join();
```

---

## API Reference

### AsyncRetryPolicy

```cpp
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
    std::function<bool(const std::string&)> error_classifier;
    std::function<void(const RetryProgress&)> progress_callback;
    std::function<std::chrono::milliseconds(uint32_t)> custom_backoff;
    
    // Timeout configuration
    std::chrono::milliseconds operation_timeout{30000};
    std::chrono::milliseconds total_timeout{0}; // 0 = no limit
    
    // Circuit breaker integration
    bool enable_circuit_breaker = false;
    std::string circuit_breaker_id;
    
    // Factory methods
    static AsyncRetryPolicy from_sync_policy(const RetryPolicy& policy);
    static AsyncRetryPolicy create_rpc_policy();
    static AsyncRetryPolicy create_network_policy();
    static AsyncRetryPolicy create_storage_policy();
};
```

### AsyncRetryExecutor

```cpp
class AsyncRetryExecutor {
public:
    explicit AsyncRetryExecutor(size_t num_workers = 0); // 0 = auto
    
    bool initialize();
    void shutdown(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));
    bool is_running() const;
    
    // Execute single operation
    template<typename F>
    auto execute_with_retry(
        F&& operation,
        const AsyncRetryPolicy& policy = {},
        CancellationTokenPtr cancellation_token = nullptr
    ) -> std::future<AsyncRetryResult<T>>;
    
    // Execute multiple operations concurrently
    template<typename F>
    auto execute_batch_with_retry(
        std::vector<F> operations,
        const AsyncRetryPolicy& policy = {}
    ) -> std::vector<std::future<AsyncRetryResult<T>>>;
    
    Stats get_stats() const;
    void reset_stats();
};
```

### Convenience Functions

```cpp
// Non-blocking retry (returns future)
template<typename F>
auto async_retry(
    F&& operation,
    const AsyncRetryPolicy& policy = {},
    CancellationTokenPtr cancellation_token = nullptr
) -> std::future<AsyncRetryResult<T>>;

// Blocking retry (backward compatibility)
template<typename F>
auto sync_retry(
    F&& operation,
    const AsyncRetryPolicy& policy = {}
) -> AsyncRetryResult<T>;
```

### CancellationToken

```cpp
class CancellationToken {
public:
    void cancel();
    bool is_cancelled() const;
    void reset();
};
```

---

## Performance Tuning

### Worker Pool Sizing

Default sizing (CPU cores × 2) works well for I/O-bound operations:

```cpp
// Use default (recommended for most cases)
AsyncRetryExecutor executor; // Auto-detect optimal worker count

// Custom worker count
AsyncRetryExecutor executor(8); // Explicit worker count
```

**Guidelines:**
- **I/O-bound operations**: CPU cores × 2 to 4
- **CPU-bound operations**: CPU cores × 1 to 2
- **Mixed workloads**: CPU cores × 2 (default)

### Retry Strategy Selection

```cpp
AsyncRetryPolicy policy;

// Exponential backoff (default, recommended for most cases)
policy.strategy = RetryStrategy::EXPONENTIAL_BACKOFF;
policy.backoff_multiplier = 2.0;

// Linear backoff (predictable timing)
policy.strategy = RetryStrategy::LINEAR_BACKOFF;

// Fixed delay (simplest, for rate limiting)
policy.strategy = RetryStrategy::FIXED_DELAY;

// Fibonacci backoff (gradual increase)
policy.strategy = RetryStrategy::FIBONACCI_BACKOFF;

// Custom backoff
policy.strategy = RetryStrategy::CUSTOM;
policy.custom_backoff = [](uint32_t attempt) {
    return std::chrono::milliseconds(attempt * attempt * 100);
};
```

### Memory Optimization

```cpp
// For high-volume scenarios, create dedicated executor
class HighVolumeService {
private:
    AsyncRetryExecutor retry_executor_{16}; // Larger worker pool
    
public:
    void initialize() {
        retry_executor_.initialize();
    }
    
    auto process_request(const Request& req) {
        return retry_executor_.execute_with_retry(
            [req]() { return handle_request(req); },
            AsyncRetryPolicy::create_rpc_policy()
        );
    }
};
```

---

## Best Practices

### 1. Choose Appropriate Retry Policies

```cpp
// ✅ Good: Use factory methods for common scenarios
auto policy = AsyncRetryPolicy::create_network_policy(); // For network I/O
auto policy = AsyncRetryPolicy::create_rpc_policy();     // For RPC calls
auto policy = AsyncRetryPolicy::create_storage_policy(); // For storage ops

// ❌ Bad: Over-aggressive retries
AsyncRetryPolicy policy;
policy.max_attempts = 100; // Too many!
policy.initial_delay = std::chrono::milliseconds(10); // Too fast!
```

### 2. Set Appropriate Timeouts

```cpp
// ✅ Good: Set both operation and total timeouts
AsyncRetryPolicy policy;
policy.operation_timeout = std::chrono::milliseconds(5000);  // Per-attempt
policy.total_timeout = std::chrono::milliseconds(30000);     // Total

// ❌ Bad: No timeout limits
AsyncRetryPolicy policy;
policy.total_timeout = std::chrono::milliseconds(0); // Infinite!
```

### 3. Use Progress Callbacks Wisely

```cpp
// ✅ Good: Lightweight logging
policy.progress_callback = [](const RetryProgress& progress) {
    LOG_DEBUG("Retry " << progress.attempt_number << "/" << progress.max_attempts);
};

// ❌ Bad: Heavy processing in callback
policy.progress_callback = [](const RetryProgress& progress) {
    expensive_database_write(progress); // Don't do this!
    complex_calculation();              // Or this!
};
```

### 4. Handle Cancellation Properly

```cpp
// ✅ Good: Clean cancellation handling
auto token = std::make_shared<CancellationToken>();
auto future = async_retry(operation, policy, token);

// Cancel on user request
if (user_requested_cancel) {
    token->cancel();
}

auto result = future.get();
if (result.was_cancelled) {
    cleanup_resources();
    return Result<T>::err("Cancelled by user");
}

// ❌ Bad: Ignoring cancellation
auto result = future.get();
// No check for was_cancelled!
```

### 5. Test Retry Logic

```cpp
// ✅ Good: Test with controlled failure injection
#include <gtest/gtest.h>

TEST(RetryTest, EventualSuccess) {
    std::atomic<int> attempt{0};
    
    auto flaky_op = [&attempt]() -> Result<int> {
        if (attempt.fetch_add(1) < 2) {
            return Result<int>::err("Transient");
        }
        return Result<int>::ok(42);
    };
    
    auto result = sync_retry(flaky_op, AsyncRetryPolicy::create_rpc_policy());
    
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(result.attempts_made, 3);
}
```

---

## Troubleshooting

### Issue: High memory usage

**Symptom:** Memory grows with number of pending operations

**Solution:**
```cpp
// Monitor executor statistics
auto stats = global_async_retry_executor().get_stats();
if (stats.pending_operations > 1000) {
    LOG_WARN("High pending operation count: " << stats.pending_operations);
    // Implement backpressure
}
```

### Issue: Slow retry performance

**Symptom:** Retries taking longer than expected

**Solution:**
```cpp
// Check worker utilization
auto stats = executor.get_stats();
double utilization = static_cast<double>(stats.pending_operations) / num_workers;

if (utilization > 10.0) {
    // Increase worker count
    AsyncRetryExecutor new_executor(num_workers * 2);
}
```

### Issue: Retries not happening

**Symptom:** Operations fail immediately without retry

**Solution:**
```cpp
// Check error classification
AsyncRetryPolicy policy;
policy.error_classifier = [](const std::string& error) {
    std::cout << "Checking error: " << error << "\n";
    return FaultTolerance::is_retryable_error(error);
};

// Or use progress callback to debug
policy.progress_callback = [](const RetryProgress& progress) {
    std::cout << "Retry " << progress.attempt_number 
              << ", error: " << progress.last_error << "\n";
};
```

### Issue: Tests hanging

**Symptom:** Unit tests hang waiting for futures

**Solution:**
```cpp
// Always set timeouts in tests
TEST(AsyncRetryTest, MyTest) {
    AsyncRetryPolicy policy;
    policy.max_attempts = 3;
    policy.initial_delay = std::chrono::milliseconds(10);
    policy.total_timeout = std::chrono::milliseconds(1000); // Limit test time
    
    auto future = async_retry(operation, policy);
    
    // Use future wait_for instead of get
    auto status = future.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);
    
    auto result = future.get();
}
```

---

## Examples

### Example 1: HTTP Client with Retry

```cpp
#include "common/async_retry.h"
#include "network/http_client.h"

class AsyncHttpClient {
private:
    HttpClient sync_client_;
    AsyncRetryPolicy default_policy_;
    
public:
    AsyncHttpClient() {
        default_policy_ = AsyncRetryPolicy::create_network_policy();
        default_policy_.progress_callback = [](const RetryProgress& p) {
            LOG_DEBUG("HTTP retry " << p.attempt_number << "/" << p.max_attempts);
        };
    }
    
    std::future<AsyncRetryResult<HttpResponse>> get_async(const std::string& url) {
        auto operation = [this, url]() -> Result<HttpResponse> {
            auto response = sync_client_.get(url);
            if (response.success) {
                return Result<HttpResponse>::ok(response);
            }
            return Result<HttpResponse>::err(response.error_message);
        };
        
        return async_retry(operation, default_policy_);
    }
};
```

### Example 2: Database Connection with Circuit Breaker

```cpp
#include "common/async_retry.h"
#include "common/fault_tolerance.h"

class DatabaseClient {
private:
    AsyncRetryPolicy retry_policy_;
    CircuitBreaker circuit_breaker_;
    
public:
    DatabaseClient() {
        retry_policy_ = AsyncRetryPolicy::create_storage_policy();
        retry_policy_.enable_circuit_breaker = true;
        retry_policy_.circuit_breaker_id = "database";
        
        CircuitBreakerConfig cb_config;
        cb_config.failure_threshold = 5;
        cb_config.timeout = std::chrono::milliseconds(10000);
        circuit_breaker_ = CircuitBreaker(cb_config);
    }
    
    std::future<AsyncRetryResult<QueryResult>> query_async(const std::string& sql) {
        auto operation = [this, sql]() -> Result<QueryResult> {
            return circuit_breaker_.execute([this, &sql]() {
                return execute_query(sql);
            });
        };
        
        return async_retry(operation, retry_policy_);
    }
};
```

### Example 3: Batch Processing

```cpp
std::vector<std::future<AsyncRetryResult<ProcessedData>>> process_batch(
    const std::vector<RawData>& batch
) {
    AsyncRetryPolicy policy = AsyncRetryPolicy::create_rpc_policy();
    
    std::vector<std::function<Result<ProcessedData>()>> operations;
    for (const auto& data : batch) {
        operations.push_back([data]() {
            return process_data(data);
        });
    }
    
    return global_async_retry_executor().execute_batch_with_retry(
        operations,
        policy
    );
}

// Usage
auto futures = process_batch(raw_data);
std::vector<ProcessedData> results;

for (auto& future : futures) {
    auto result = future.get();
    if (result.is_ok()) {
        results.push_back(result.unwrap());
    } else {
        LOG_ERROR("Processing failed: " << result.error());
    }
}
```

---

## Migration Checklist

- [ ] Identify blocking retry operations in hot paths
- [ ] Replace with async variants using `async_retry()`
- [ ] Add cancellation tokens for long-running operations
- [ ] Set appropriate timeouts (operation and total)
- [ ] Add progress callbacks for observability
- [ ] Test with failure injection
- [ ] Monitor worker pool utilization
- [ ] Benchmark performance improvements
- [ ] Update documentation
- [ ] Train team on new patterns

---

## Support

For questions or issues with async retry:
- GitHub Issues: https://github.com/slonana-labs/slonana.cpp/issues
- Documentation: See `MODULAR_INTERFACES_AUDIT.md`
- Code examples: `tests/test_async_retry.cpp`

---

*Last updated: 2025-12-22*
