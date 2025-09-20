#pragma once

#include "common/types.h"
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

namespace slonana {
namespace common {

/**
 * Retry policy configuration for fault tolerance operations
 */
struct RetryPolicy {
  uint32_t max_attempts = 3;
  std::chrono::milliseconds initial_delay{100};
  std::chrono::milliseconds max_delay{5000};
  double backoff_multiplier = 2.0;
  double jitter_factor = 0.1; // Add randomness to prevent thundering herd
  
  // Specific retry conditions
  bool retry_on_timeout = true;
  bool retry_on_connection_error = true;
  bool retry_on_transient_error = true;
};

/**
 * Circuit breaker states for preventing cascading failures
 */
enum class CircuitState {
  CLOSED,    // Normal operation
  OPEN,      // Failing fast
  HALF_OPEN  // Testing recovery
};

/**
 * Circuit breaker configuration
 */
struct CircuitBreakerConfig {
  uint32_t failure_threshold = 5;
  std::chrono::milliseconds timeout{30000}; // Time to wait before trying again
  uint32_t success_threshold = 2; // Successes needed to close circuit
};

/**
 * Fault tolerance utilities and retry mechanisms
 */
class FaultTolerance {
public:
  /**
   * Execute an operation with retry logic and exponential backoff
   */
  template<typename F, typename R = std::invoke_result_t<F>>
  static R retry_with_backoff(F&& operation, const RetryPolicy& policy = {}) {
    // Note: For simplicity, we assume operation returns Result<T> type
    // In production, this would have more sophisticated type checking
    
    std::random_device rd;
    std::mt19937 gen(rd());
    auto delay = policy.initial_delay;
    
    for (uint32_t attempt = 1; attempt <= policy.max_attempts; ++attempt) {
      auto result = operation();
      
      if (result.is_ok() || attempt == policy.max_attempts) {
        return result;
      }
      
      // Calculate jittered delay
      auto jitter_range = static_cast<long>(delay.count() * policy.jitter_factor);
      std::uniform_int_distribution<long> jitter_dist(-jitter_range, jitter_range);
      auto jittered_delay = delay + std::chrono::milliseconds(jitter_dist(gen));
      
      std::this_thread::sleep_for(jittered_delay);
      
      // Exponential backoff with max cap
      delay = std::min(
        std::chrono::duration_cast<std::chrono::milliseconds>(
          delay * policy.backoff_multiplier
        ),
        policy.max_delay
      );
    }
    
    return R("Max retry attempts exceeded");
  }
  
  /**
   * Check if an error is retryable based on the error message
   */
  static bool is_retryable_error(const std::string& error);
  
  /**
   * Create a default retry policy for different operation types
   */
  static RetryPolicy create_rpc_retry_policy();
  static RetryPolicy create_network_retry_policy();
  static RetryPolicy create_storage_retry_policy();
};

/**
 * Circuit breaker implementation for external dependencies
 */
class CircuitBreaker {
private:
  CircuitBreakerConfig config_;
  CircuitState state_;
  uint32_t failure_count_;
  uint32_t success_count_;
  std::chrono::steady_clock::time_point last_failure_time_;
  mutable std::mutex mutex_;

public:
  explicit CircuitBreaker(const CircuitBreakerConfig& config = {});
  
  /**
   * Execute operation through circuit breaker
   */
  template<typename F, typename R = std::invoke_result_t<F>>
  R execute(F&& operation) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ == CircuitState::OPEN) {
      auto now = std::chrono::steady_clock::now();
      if (now - last_failure_time_ < config_.timeout) {
        return R("Circuit breaker is OPEN");
      }
      state_ = CircuitState::HALF_OPEN;
      success_count_ = 0;
    }
    
    auto result = operation();
    
    if (result.is_ok()) {
      on_success();
    } else {
      on_failure();
    }
    
    return result;
  }
  
  CircuitState get_state() const;
  uint32_t get_failure_count() const;

private:
  void on_success();
  void on_failure();
};

/**
 * State checkpoint interface for recovery mechanisms
 */
class Checkpoint {
public:
  virtual ~Checkpoint() = default;
  
  /**
   * Save current state to persistent storage
   */
  virtual Result<bool> save_checkpoint(const std::string& checkpoint_id) = 0;
  
  /**
   * Restore state from checkpoint
   */
  virtual Result<bool> restore_checkpoint(const std::string& checkpoint_id) = 0;
  
  /**
   * List available checkpoints
   */
  virtual Result<std::vector<std::string>> list_checkpoints() = 0;
  
  /**
   * Verify checkpoint integrity
   */
  virtual Result<bool> verify_checkpoint(const std::string& checkpoint_id) = 0;
};

/**
 * Graceful degradation modes for different components
 */
enum class DegradationMode {
  NORMAL,           // Full functionality
  READ_ONLY,        // Limited to read operations
  ESSENTIAL_ONLY,   // Only critical operations
  OFFLINE           // Component unavailable
};

/**
 * Degradation manager for handling partial failures
 */
class DegradationManager {
private:
  std::unordered_map<std::string, DegradationMode> component_modes_;
  mutable std::shared_mutex modes_mutex_;

public:
  /**
   * Set degradation mode for a component
   */
  void set_component_mode(const std::string& component, DegradationMode mode);
  
  /**
   * Get current degradation mode for a component
   */
  DegradationMode get_component_mode(const std::string& component) const;
  
  /**
   * Check if operation is allowed in current degradation mode
   */
  bool is_operation_allowed(const std::string& component, const std::string& operation) const;
  
  /**
   * Get overall system health status
   */
  std::unordered_map<std::string, DegradationMode> get_system_status() const;
};

} // namespace common
} // namespace slonana