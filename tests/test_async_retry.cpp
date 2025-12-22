/**
 * Unit tests for Async Retry abstraction layer
 * 
 * Tests the new async retry infrastructure including:
 * - AsyncRetryPolicy configuration
 * - BackoffCalculator algorithms
 * - AsyncRetryExecutor worker pool
 * - CancellationToken mechanics
 * - Integration with FaultTolerance
 */

#include "common/async_retry.h"
#include "common/fault_tolerance.h"
#include "common/types.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>

using namespace slonana::common;

// ============================================================================
// Test Fixtures
// ============================================================================

class AsyncRetryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset global executor state if needed
    }
    
    void TearDown() override {
        // Cleanup
    }
};

// ============================================================================
// AsyncRetryPolicy Tests
// ============================================================================

TEST_F(AsyncRetryTest, PolicyConversionFromSync) {
    RetryPolicy sync_policy;
    sync_policy.max_attempts = 5;
    sync_policy.initial_delay = std::chrono::milliseconds(200);
    sync_policy.max_delay = std::chrono::milliseconds(10000);
    sync_policy.backoff_multiplier = 3.0;
    sync_policy.jitter_factor = 0.2;
    
    auto async_policy = AsyncRetryPolicy::from_sync_policy(sync_policy);
    
    EXPECT_EQ(async_policy.max_attempts, 5);
    EXPECT_EQ(async_policy.initial_delay, std::chrono::milliseconds(200));
    EXPECT_EQ(async_policy.max_delay, std::chrono::milliseconds(10000));
    EXPECT_DOUBLE_EQ(async_policy.backoff_multiplier, 3.0);
    EXPECT_DOUBLE_EQ(async_policy.jitter_factor, 0.2);
    EXPECT_EQ(async_policy.strategy, RetryStrategy::EXPONENTIAL_BACKOFF);
}

TEST_F(AsyncRetryTest, PolicyFactoryMethods) {
    auto rpc_policy = AsyncRetryPolicy::create_rpc_policy();
    EXPECT_EQ(rpc_policy.max_attempts, 3);
    EXPECT_GT(rpc_policy.operation_timeout.count(), 0);
    
    auto network_policy = AsyncRetryPolicy::create_network_policy();
    EXPECT_EQ(network_policy.max_attempts, 5);
    EXPECT_GT(network_policy.total_timeout.count(), 0);
    
    auto storage_policy = AsyncRetryPolicy::create_storage_policy();
    EXPECT_EQ(storage_policy.max_attempts, 4);
    EXPECT_FALSE(storage_policy.retry_on_timeout);
}

TEST_F(AsyncRetryTest, FaultToleranceIntegration) {
    RetryPolicy sync_policy = FaultTolerance::create_network_retry_policy();
    auto async_policy = FaultTolerance::to_async_policy(sync_policy);
    
    EXPECT_EQ(async_policy.max_attempts, sync_policy.max_attempts);
    EXPECT_EQ(async_policy.initial_delay, sync_policy.initial_delay);
}

// ============================================================================
// CancellationToken Tests
// ============================================================================

TEST_F(AsyncRetryTest, CancellationTokenBasicOperations) {
    CancellationToken token;
    
    EXPECT_FALSE(token.is_cancelled());
    
    token.cancel();
    EXPECT_TRUE(token.is_cancelled());
    
    token.reset();
    EXPECT_FALSE(token.is_cancelled());
}

TEST_F(AsyncRetryTest, CancellationTokenThreadSafety) {
    auto token = std::make_shared<CancellationToken>();
    std::atomic<int> cancel_count{0};
    
    // Start multiple threads that cancel the token
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([token, &cancel_count]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            token->cancel();
            cancel_count.fetch_add(1, std::memory_order_relaxed);
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_TRUE(token->is_cancelled());
    EXPECT_EQ(cancel_count.load(), 10);
}

// ============================================================================
// BackoffCalculator Tests
// ============================================================================

TEST_F(AsyncRetryTest, ExponentialBackoffCalculation) {
    AsyncRetryPolicy policy;
    policy.initial_delay = std::chrono::milliseconds(100);
    policy.backoff_multiplier = 2.0;
    policy.max_delay = std::chrono::milliseconds(10000);
    policy.jitter_factor = 0.0; // No jitter for predictable testing
    policy.strategy = RetryStrategy::EXPONENTIAL_BACKOFF;
    
    BackoffCalculator calculator(policy);
    
    // First attempt: 100ms
    auto delay1 = calculator.calculate_delay(1);
    EXPECT_GE(delay1.count(), 90);
    EXPECT_LE(delay1.count(), 110);
    
    // Second attempt: 200ms
    auto delay2 = calculator.calculate_delay(2);
    EXPECT_GE(delay2.count(), 190);
    EXPECT_LE(delay2.count(), 210);
    
    // Third attempt: 400ms
    auto delay3 = calculator.calculate_delay(3);
    EXPECT_GE(delay3.count(), 390);
    EXPECT_LE(delay3.count(), 410);
}

TEST_F(AsyncRetryTest, LinearBackoffCalculation) {
    AsyncRetryPolicy policy;
    policy.initial_delay = std::chrono::milliseconds(100);
    policy.max_delay = std::chrono::milliseconds(10000);
    policy.jitter_factor = 0.0;
    policy.strategy = RetryStrategy::LINEAR_BACKOFF;
    
    BackoffCalculator calculator(policy);
    
    auto delay1 = calculator.calculate_delay(1);
    EXPECT_EQ(delay1.count(), 100);
    
    auto delay2 = calculator.calculate_delay(2);
    EXPECT_EQ(delay2.count(), 200);
    
    auto delay3 = calculator.calculate_delay(3);
    EXPECT_EQ(delay3.count(), 300);
}

TEST_F(AsyncRetryTest, FixedDelayCalculation) {
    AsyncRetryPolicy policy;
    policy.initial_delay = std::chrono::milliseconds(500);
    policy.jitter_factor = 0.0;
    policy.strategy = RetryStrategy::FIXED_DELAY;
    
    BackoffCalculator calculator(policy);
    
    EXPECT_EQ(calculator.calculate_delay(1).count(), 500);
    EXPECT_EQ(calculator.calculate_delay(2).count(), 500);
    EXPECT_EQ(calculator.calculate_delay(5).count(), 500);
}

TEST_F(AsyncRetryTest, MaxDelayClamp) {
    AsyncRetryPolicy policy;
    policy.initial_delay = std::chrono::milliseconds(100);
    policy.backoff_multiplier = 10.0; // Aggressive multiplier
    policy.max_delay = std::chrono::milliseconds(1000);
    policy.jitter_factor = 0.0;
    policy.strategy = RetryStrategy::EXPONENTIAL_BACKOFF;
    
    BackoffCalculator calculator(policy);
    
    // After a few attempts, should hit max_delay
    auto delay5 = calculator.calculate_delay(5);
    EXPECT_LE(delay5.count(), 1000);
}

// ============================================================================
// AsyncRetryExecutor Tests
// ============================================================================

TEST_F(AsyncRetryTest, ExecutorInitializationShutdown) {
    AsyncRetryExecutor executor(2);
    
    EXPECT_FALSE(executor.is_running());
    
    EXPECT_TRUE(executor.initialize());
    EXPECT_TRUE(executor.is_running());
    
    executor.shutdown();
    EXPECT_FALSE(executor.is_running());
}

TEST_F(AsyncRetryTest, SuccessfulOperationNoRetry) {
    AsyncRetryExecutor executor(2);
    ASSERT_TRUE(executor.initialize());
    
    std::atomic<int> call_count{0};
    
    auto operation = [&call_count]() -> Result<int> {
        call_count.fetch_add(1, std::memory_order_relaxed);
        return Result<int>::ok(42);
    };
    
    AsyncRetryPolicy policy;
    policy.max_attempts = 3;
    
    auto future = executor.execute_with_retry(operation, policy);
    auto result = future.get();
    
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(result.unwrap(), 42);
    EXPECT_EQ(result.attempts_made, 1);
    EXPECT_EQ(call_count.load(), 1);
    
    executor.shutdown();
}

TEST_F(AsyncRetryTest, RetryWithEventualSuccess) {
    AsyncRetryExecutor executor(2);
    ASSERT_TRUE(executor.initialize());
    
    std::atomic<int> call_count{0};
    
    auto operation = [&call_count]() -> Result<std::string> {
        int count = call_count.fetch_add(1, std::memory_order_relaxed);
        if (count < 2) {
            return Result<std::string>::err("Transient error");
        }
        return Result<std::string>::ok("Success");
    };
    
    AsyncRetryPolicy policy;
    policy.max_attempts = 5;
    policy.initial_delay = std::chrono::milliseconds(10);
    
    auto future = executor.execute_with_retry(operation, policy);
    auto result = future.get();
    
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(result.unwrap(), "Success");
    EXPECT_EQ(result.attempts_made, 3);
    EXPECT_EQ(call_count.load(), 3);
    
    executor.shutdown();
}

TEST_F(AsyncRetryTest, RetryExhaustion) {
    AsyncRetryExecutor executor(2);
    ASSERT_TRUE(executor.initialize());
    
    std::atomic<int> call_count{0};
    
    auto operation = [&call_count]() -> Result<int> {
        call_count.fetch_add(1, std::memory_order_relaxed);
        return Result<int>::err("Permanent error");
    };
    
    AsyncRetryPolicy policy;
    policy.max_attempts = 3;
    policy.initial_delay = std::chrono::milliseconds(10);
    
    auto future = executor.execute_with_retry(operation, policy);
    auto result = future.get();
    
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.attempts_made, 3);
    EXPECT_EQ(call_count.load(), 3);
    EXPECT_EQ(result.error_history.size(), 3);
    
    executor.shutdown();
}

TEST_F(AsyncRetryTest, CancellationDuringRetry) {
    AsyncRetryExecutor executor(2);
    ASSERT_TRUE(executor.initialize());
    
    std::atomic<int> call_count{0};
    auto cancellation_token = std::make_shared<CancellationToken>();
    
    auto operation = [&call_count, cancellation_token]() -> Result<int> {
        int count = call_count.fetch_add(1, std::memory_order_relaxed);
        if (count == 1) {
            // Cancel on second attempt
            cancellation_token->cancel();
        }
        return Result<int>::err("Transient error");
    };
    
    AsyncRetryPolicy policy;
    policy.max_attempts = 10;
    policy.initial_delay = std::chrono::milliseconds(50);
    
    auto future = executor.execute_with_retry(operation, policy, cancellation_token);
    auto result = future.get();
    
    EXPECT_FALSE(result.is_ok());
    EXPECT_TRUE(result.was_cancelled);
    EXPECT_LT(result.attempts_made, 10); // Should not complete all attempts
    
    executor.shutdown();
}

TEST_F(AsyncRetryTest, ProgressCallback) {
    AsyncRetryExecutor executor(2);
    ASSERT_TRUE(executor.initialize());
    
    std::atomic<int> progress_count{0};
    std::vector<RetryProgress> progress_history;
    std::mutex progress_mutex;
    
    auto operation = []() -> Result<int> {
        return Result<int>::err("Transient error");
    };
    
    AsyncRetryPolicy policy;
    policy.max_attempts = 3;
    policy.initial_delay = std::chrono::milliseconds(10);
    policy.progress_callback = [&](const RetryProgress& progress) {
        std::lock_guard<std::mutex> lock(progress_mutex);
        progress_count.fetch_add(1, std::memory_order_relaxed);
        progress_history.push_back(progress);
    };
    
    auto future = executor.execute_with_retry(operation, policy);
    auto result = future.get();
    
    EXPECT_FALSE(result.is_ok());
    EXPECT_GT(progress_count.load(), 0);
    EXPECT_EQ(progress_history.size(), 2); // Callbacks between attempts
    
    executor.shutdown();
}

TEST_F(AsyncRetryTest, ConcurrentOperations) {
    AsyncRetryExecutor executor(4);
    ASSERT_TRUE(executor.initialize());
    
    std::atomic<int> total_operations{0};
    
    std::vector<std::future<AsyncRetryResult<int>>> futures;
    for (int i = 0; i < 20; ++i) {
        auto operation = [&total_operations, i]() -> Result<int> {
            total_operations.fetch_add(1, std::memory_order_relaxed);
            return Result<int>::ok(i);
        };
        
        futures.push_back(executor.execute_with_retry(operation, AsyncRetryPolicy()));
    }
    
    // Wait for all operations
    int success_count = 0;
    for (auto& future : futures) {
        auto result = future.get();
        if (result.is_ok()) {
            success_count++;
        }
    }
    
    EXPECT_EQ(success_count, 20);
    EXPECT_EQ(total_operations.load(), 20);
    
    executor.shutdown();
}

TEST_F(AsyncRetryTest, ExecutorStatistics) {
    AsyncRetryExecutor executor(2);
    ASSERT_TRUE(executor.initialize());
    
    // Run some successful operations
    for (int i = 0; i < 5; ++i) {
        auto op = []() -> Result<int> { return Result<int>::ok(1); };
        auto future = executor.execute_with_retry(op, AsyncRetryPolicy());
        future.get();
    }
    
    // Run some failed operations
    for (int i = 0; i < 3; ++i) {
        auto op = []() -> Result<int> { return Result<int>::err("Error"); };
        AsyncRetryPolicy policy;
        policy.max_attempts = 2;
        policy.initial_delay = std::chrono::milliseconds(5);
        auto future = executor.execute_with_retry(op, policy);
        future.get();
    }
    
    auto stats = executor.get_stats();
    
    EXPECT_EQ(stats.total_operations, 8);
    EXPECT_EQ(stats.successful_operations, 5);
    EXPECT_EQ(stats.failed_operations, 3);
    EXPECT_GT(stats.total_retry_attempts, 0);
    
    executor.shutdown();
}

// ============================================================================
// Global Executor Tests
// ============================================================================

TEST_F(AsyncRetryTest, GlobalExecutorAccess) {
    auto& executor = global_async_retry_executor();
    EXPECT_TRUE(executor.is_running());
}

TEST_F(AsyncRetryTest, AsyncRetryConvenienceFunction) {
    auto operation = []() -> Result<std::string> {
        return Result<std::string>::ok("Test");
    };
    
    auto future = async_retry(operation);
    auto result = future.get();
    
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(result.unwrap(), "Test");
}

TEST_F(AsyncRetryTest, SyncRetryConvenienceFunction) {
    std::atomic<int> call_count{0};
    
    auto operation = [&call_count]() -> Result<int> {
        int count = call_count.fetch_add(1, std::memory_order_relaxed);
        if (count < 1) {
            return Result<int>::err("Retry me");
        }
        return Result<int>::ok(100);
    };
    
    AsyncRetryPolicy policy;
    policy.max_attempts = 3;
    policy.initial_delay = std::chrono::milliseconds(10);
    
    auto result = sync_retry(operation, policy);
    
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(result.unwrap(), 100);
    EXPECT_EQ(result.attempts_made, 2);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(AsyncRetryTest, NetworkRetryScenario) {
    AsyncRetryExecutor executor(2);
    ASSERT_TRUE(executor.initialize());
    
    std::atomic<int> attempt_num{0};
    
    // Simulate network operation that fails twice then succeeds
    auto network_op = [&attempt_num]() -> Result<std::string> {
        int attempt = attempt_num.fetch_add(1, std::memory_order_relaxed);
        
        if (attempt == 0) {
            return Result<std::string>::err("Connection timeout");
        } else if (attempt == 1) {
            return Result<std::string>::err("Network unreachable");
        } else {
            return Result<std::string>::ok("Data fetched");
        }
    };
    
    auto policy = AsyncRetryPolicy::create_network_policy();
    policy.initial_delay = std::chrono::milliseconds(10);
    
    auto future = executor.execute_with_retry(network_op, policy);
    auto result = future.get();
    
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(result.unwrap(), "Data fetched");
    EXPECT_EQ(result.attempts_made, 3);
    EXPECT_EQ(result.error_history.size(), 2);
    
    executor.shutdown();
}

TEST_F(AsyncRetryTest, RpcRetryWithTimeout) {
    AsyncRetryExecutor executor(2);
    ASSERT_TRUE(executor.initialize());
    
    auto rpc_call = []() -> Result<int> {
        // Simulate slow RPC call
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return Result<int>::ok(123);
    };
    
    auto policy = AsyncRetryPolicy::create_rpc_policy();
    policy.operation_timeout = std::chrono::milliseconds(100);
    
    auto future = executor.execute_with_retry(rpc_call, policy);
    auto result = future.get();
    
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(result.unwrap(), 123);
    
    executor.shutdown();
}

// ============================================================================
// Error Classification Tests
// ============================================================================

TEST_F(AsyncRetryTest, CustomErrorClassifier) {
    AsyncRetryExecutor executor(2);
    ASSERT_TRUE(executor.initialize());
    
    std::atomic<int> call_count{0};
    
    auto operation = [&call_count]() -> Result<int> {
        int count = call_count.fetch_add(1, std::memory_order_relaxed);
        if (count == 0) {
            return Result<int>::err("CUSTOM_RETRYABLE");
        } else if (count == 1) {
            return Result<int>::err("CUSTOM_PERMANENT");
        }
        return Result<int>::ok(1);
    };
    
    AsyncRetryPolicy policy;
    policy.max_attempts = 5;
    policy.initial_delay = std::chrono::milliseconds(10);
    policy.error_classifier = [](const std::string& error) {
        return error.find("RETRYABLE") != std::string::npos;
    };
    
    auto future = executor.execute_with_retry(operation, policy);
    auto result = future.get();
    
    EXPECT_FALSE(result.is_ok()); // Should stop at CUSTOM_PERMANENT
    EXPECT_EQ(result.attempts_made, 2);
    
    executor.shutdown();
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
