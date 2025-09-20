#include "common/fault_tolerance.h"
#include "common/recovery.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace slonana::common;

class TestFailureSimulator {
private:
    int call_count_ = 0;
    int fail_until_ = 2;

public:
    Result<bool> unreliable_operation() {
        call_count_++;
        if (call_count_ <= fail_until_) {
            return Result<bool>("Simulated transient failure");
        }
        return Result<bool>(true);
    }
    
    Result<bool> always_fail_operation() {
        return Result<bool>("Permanent failure");
    }
    
    int get_call_count() const { return call_count_; }
    void reset() { call_count_ = 0; }
};

bool test_retry_mechanism() {
    std::cout << "\n=== Testing Retry Mechanism ===" << std::endl;
    
    TestFailureSimulator simulator;
    RetryPolicy policy = FaultTolerance::create_rpc_retry_policy();
    
    // Test successful retry after failures
    auto result = FaultTolerance::retry_with_backoff([&simulator]() {
        return simulator.unreliable_operation();
    }, policy);
    
    if (!result.is_ok()) {
        std::cout << "❌ Retry test failed: " << result.error() << std::endl;
        return false;
    }
    
    if (simulator.get_call_count() != 3) {
        std::cout << "❌ Expected 3 calls, got " << simulator.get_call_count() << std::endl;
        return false;
    }
    
    std::cout << "✅ Retry mechanism working - succeeded after " << simulator.get_call_count() << " attempts" << std::endl;
    
    // Test max retry limit
    simulator.reset();
    auto fail_result = FaultTolerance::retry_with_backoff([&simulator]() {
        return simulator.always_fail_operation();
    }, policy);
    
    if (fail_result.is_ok()) {
        std::cout << "❌ Expected failure after max retries" << std::endl;
        return false;
    }
    
    std::cout << "✅ Max retry limit respected" << std::endl;
    return true;
}

bool test_circuit_breaker() {
    std::cout << "\n=== Testing Circuit Breaker ===" << std::endl;
    
    CircuitBreakerConfig config;
    config.failure_threshold = 3;
    config.timeout = std::chrono::milliseconds(100);
    
    CircuitBreaker breaker(config);
    TestFailureSimulator simulator;
    
    // Trigger failures to open circuit
    for (int i = 0; i < 3; ++i) {
        auto result = breaker.execute([&simulator]() {
            return simulator.always_fail_operation();
        });
        if (result.is_ok()) {
            std::cout << "❌ Expected failure but got success" << std::endl;
            return false;
        }
    }
    
    if (breaker.get_state() != CircuitState::OPEN) {
        std::cout << "❌ Circuit breaker should be OPEN" << std::endl;
        return false;
    }
    
    std::cout << "✅ Circuit breaker opened after " << config.failure_threshold << " failures" << std::endl;
    
    // Test that circuit stays open
    auto blocked_result = breaker.execute([&simulator]() {
        return simulator.unreliable_operation();
    });
    
    if (blocked_result.is_ok()) {
        std::cout << "❌ Circuit breaker should block calls when OPEN" << std::endl;
        return false;
    }
    
    std::cout << "✅ Circuit breaker blocks calls when OPEN" << std::endl;
    return true;
}

bool test_degradation_manager() {
    std::cout << "\n=== Testing Degradation Manager ===" << std::endl;
    
    DegradationManager manager;
    
    // Test normal mode
    if (manager.get_component_mode("rpc") != DegradationMode::NORMAL) {
        std::cout << "❌ Default mode should be NORMAL" << std::endl;
        return false;
    }
    
    if (!manager.is_operation_allowed("rpc", "read_account")) {
        std::cout << "❌ Read operations should be allowed in NORMAL mode" << std::endl;
        return false;
    }
    
    // Test read-only mode
    manager.set_component_mode("rpc", DegradationMode::READ_ONLY);
    
    if (!manager.is_operation_allowed("rpc", "read_account")) {
        std::cout << "❌ Read operations should be allowed in READ_ONLY mode" << std::endl;
        return false;
    }
    
    if (manager.is_operation_allowed("rpc", "write_account")) {
        std::cout << "❌ Write operations should be blocked in READ_ONLY mode" << std::endl;
        return false;
    }
    
    // Test offline mode
    manager.set_component_mode("rpc", DegradationMode::OFFLINE);
    
    if (manager.is_operation_allowed("rpc", "read_account")) {
        std::cout << "❌ All operations should be blocked in OFFLINE mode" << std::endl;
        return false;
    }
    
    std::cout << "✅ Degradation manager working correctly" << std::endl;
    return true;
}

bool test_checkpoint_basic() {
    std::cout << "\n=== Testing Basic Checkpoint Operations ===" << std::endl;
    
    std::string test_dir = "/tmp/slonana_checkpoint_test";
    FileCheckpoint checkpoint(test_dir);
    
    // Test basic checkpoint creation
    auto save_result = checkpoint.save_checkpoint("test_checkpoint_1");
    if (!save_result.is_ok()) {
        std::cout << "❌ Failed to save checkpoint: " << save_result.error() << std::endl;
        return false;
    }
    
    // Test checkpoint verification (simplified version)
    auto verify_result = checkpoint.verify_checkpoint("test_checkpoint_1");
    if (!verify_result.is_ok()) {
        std::cout << "❌ Failed to verify checkpoint: " << verify_result.error() << std::endl;
        return false;
    }
    
    // Test data save/load
    std::vector<uint8_t> test_data = {1, 2, 3, 4, 5};
    auto data_save_result = checkpoint.save_data("test_data_1", test_data);
    if (!data_save_result.is_ok()) {
        std::cout << "❌ Failed to save data: " << data_save_result.error() << std::endl;
        return false;
    }
    
    auto data_load_result = checkpoint.load_data("test_data_1");
    if (!data_load_result.is_ok()) {
        std::cout << "❌ Failed to load data: " << data_load_result.error() << std::endl;
        return false;
    }
    
    if (data_load_result.value() != test_data) {
        std::cout << "❌ Loaded data doesn't match saved data" << std::endl;
        return false;
    }
    
    std::cout << "✅ Basic checkpoint operations working" << std::endl;
    return true;
}

bool test_retryable_error_detection() {
    std::cout << "\n=== Testing Retryable Error Detection ===" << std::endl;
    
    // Test retryable errors
    std::vector<std::string> retryable_errors = {
        "Connection timeout",
        "Network unavailable", 
        "Service temporarily busy",
        "Rate limit exceeded"
    };
    
    for (const auto& error : retryable_errors) {
        if (!FaultTolerance::is_retryable_error(error)) {
            std::cout << "❌ Error should be retryable: " << error << std::endl;
            return false;
        }
    }
    
    // Test non-retryable errors
    std::vector<std::string> non_retryable_errors = {
        "Invalid credentials",
        "Permission denied",
        "Resource not found",
        "Malformed request"
    };
    
    for (const auto& error : non_retryable_errors) {
        if (FaultTolerance::is_retryable_error(error)) {
            std::cout << "❌ Error should NOT be retryable: " << error << std::endl;
            return false;
        }
    }
    
    std::cout << "✅ Retryable error detection working correctly" << std::endl;
    return true;
}

int main() {
    std::cout << "🔧 Running Fault Tolerance Tests" << std::endl;
    
    bool all_passed = true;
    
    all_passed &= test_retryable_error_detection();
    all_passed &= test_retry_mechanism();
    all_passed &= test_circuit_breaker();
    all_passed &= test_degradation_manager();
    all_passed &= test_checkpoint_basic();
    
    if (all_passed) {
        std::cout << "\n✅ All fault tolerance tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ Some fault tolerance tests failed!" << std::endl;
        return 1;
    }
}