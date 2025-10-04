/**
 * @file fault_tolerance.h
 * @brief Defines utilities and patterns for building resilient systems.
 *
 * This file includes mechanisms for handling transient faults and preventing
 * cascading failures. It provides implementations for retry logic with
- * exponential backoff, circuit breakers, state checkpointing, and graceful
+ * exponential backoff, circuit breakers, state checkpointing, and graceful
 * degradation of services.
 */
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
 * @brief Configuration for retry policies in fault-tolerant operations.
 * @details Defines parameters for how retry logic should behave, including
 * the number of attempts, delay strategies, and conditions for retrying.
 */
struct RetryPolicy {
  /// @brief Maximum number of times to retry the operation.
  uint32_t max_attempts = 3;
  /// @brief The initial delay before the first retry.
  std::chrono::milliseconds initial_delay{100};
  /// @brief The maximum delay between retries.
  std::chrono::milliseconds max_delay{5000};
  /// @brief The multiplier for exponential backoff.
  double backoff_multiplier = 2.0;
  /// @brief Factor for adding randomness to delays to prevent thundering herd.
  double jitter_factor = 0.1;

  /// @brief Whether to retry on a timeout error.
  bool retry_on_timeout = true;
  /// @brief Whether to retry on a network connection error.
  bool retry_on_connection_error = true;
  /// @brief Whether to retry on a generic transient error.
  bool retry_on_transient_error = true;
};

/**
 * @brief Represents the state of a CircuitBreaker.
 */
enum class CircuitState {
  /// @brief The circuit is closed and operations are executed normally.
  CLOSED,
  /// @brief The circuit is open; operations fail fast without being executed.
  OPEN,
  /// @brief The circuit is partially open, allowing a limited number of trial
  /// operations to see if the system has recovered.
  HALF_OPEN
};

/**
 * @brief Configuration for a CircuitBreaker.
 * @details Defines thresholds and timeouts for controlling the state
 * transitions of a circuit breaker.
 */
struct CircuitBreakerConfig {
  /// @brief Number of failures required to trip the circuit to the OPEN state.
  uint32_t failure_threshold = 5;
  /// @brief Time to wait in the OPEN state before transitioning to HALF_OPEN.
  std::chrono::milliseconds timeout{30000};
  /// @brief Number of successful operations required in HALF_OPEN state to
  /// transition back to CLOSED.
  uint32_t success_threshold = 2;
};

/**
 * @brief Provides static utility methods for fault tolerance patterns.
 */
class FaultTolerance {
public:
  /**
   * @brief Executes an operation with retry logic and exponential backoff.
   * @tparam F The type of the operation to execute.
   * @tparam R The return type of the operation, typically a Result<T>.
   * @param operation The operation to execute. It should be a callable that
   * returns a Result-like object with an `is_ok()` method.
   * @param policy The retry policy to apply.
   * @return The result of the operation after all retries. If the operation
   * never succeeds, returns the result of the last attempt or a max retries
   * exceeded error.
   * @warning This method blocks the calling thread during retry delays.
   * Avoid using it in performance-critical paths or thread pools where
   * thread starvation could occur.
   */
  template<typename F, typename R = std::invoke_result_t<F>>
  static R retry_with_backoff(F&& operation, const RetryPolicy& policy = {}) {
    std::random_device rd;
    std::mt19937 gen(rd());
    auto delay = policy.initial_delay;

    for (uint32_t attempt = 1; attempt <= policy.max_attempts; ++attempt) {
      auto result = operation();

      if (result.is_ok() || attempt == policy.max_attempts) {
        return result;
      }

      auto delay_count = delay.count();
      auto max_jitter = static_cast<long>(delay_count * policy.jitter_factor);
      if (max_jitter > 1000) max_jitter = 1000;
      if (max_jitter < 1) max_jitter = 1;

      std::uniform_int_distribution<long> jitter_dist(-max_jitter, max_jitter);
      auto jittered_delay = delay + std::chrono::milliseconds(jitter_dist(gen));

      std::this_thread::sleep_for(jittered_delay);

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
   * @brief Checks if an error message indicates a retryable condition.
   * @param error The error message string to inspect.
   * @return True if the error is deemed retryable, false otherwise.
   */
  static bool is_retryable_error(const std::string& error);

  /**
   * @brief Creates a default retry policy for RPC operations.
   * @return A pre-configured RetryPolicy suitable for RPC calls.
   */
  static RetryPolicy create_rpc_retry_policy();

  /**
   * @brief Creates a default retry policy for network operations.
   * @return A pre-configured RetryPolicy suitable for network interactions.
   */
  static RetryPolicy create_network_retry_policy();

  /**
   * @brief Creates a default retry policy for storage operations.
   * @return A pre-configured RetryPolicy suitable for storage access.
   */
  static RetryPolicy create_storage_retry_policy();
};

/**
 * @brief Implements the Circuit Breaker pattern to prevent cascading failures.
 * @details Wraps operations that call external dependencies and monitors them
 * for failures. If the failure rate exceeds a threshold, it "trips" the
 * circuit, preventing further calls and allowing the dependency to recover.
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
  /**
   * @brief Constructs a new CircuitBreaker.
   * @param config The configuration for this circuit breaker.
   */
  explicit CircuitBreaker(const CircuitBreakerConfig& config = {});

  /**
   * @brief Executes an operation through the circuit breaker.
   * @tparam F The type of the operation to execute.
   * @tparam R The return type of the operation, typically a Result<T>.
   * @param operation The operation to execute.
   * @return The result of the operation, or an error if the circuit is open.
   */
  template<typename F, typename R = std::invoke_result_t<F>>
  R execute(F&& operation) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (state_ == CircuitState::OPEN) {
        auto now = std::chrono::steady_clock::now();
        if (now - last_failure_time_ < config_.timeout) {
          return R("Circuit breaker is OPEN");
        }
        state_ = CircuitState::HALF_OPEN;
        success_count_ = 0;
      }
    }

    auto result = operation();

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (result.is_ok()) {
        on_success_unlocked();
      } else {
        on_failure_unlocked();
      }
    }

    return result;
  }

  /**
   * @brief Gets the current state of the circuit breaker.
   * @return The current CircuitState.
   */
  CircuitState get_state() const;

  /**
   * @brief Gets the current consecutive failure count.
   * @return The number of recent failures.
   */
  uint32_t get_failure_count() const;

private:
  void on_success();
  void on_failure();
  void on_success_unlocked();
  void on_failure_unlocked();
};

/**
 * @brief Interface for state checkpointing and recovery.
 * @details Defines a contract for components that need to save and restore
 * their state to/from persistent storage for recovery purposes.
 */
class Checkpoint {
public:
  virtual ~Checkpoint() = default;

  /**
   * @brief Saves the current state to a checkpoint in persistent storage.
   * @param checkpoint_id A unique identifier for this checkpoint.
   * @return A Result indicating success or failure.
   */
  virtual Result<bool> save_checkpoint(const std::string& checkpoint_id) = 0;

  /**
   * @brief Restores the component's state from a specified checkpoint.
   * @param checkpoint_id The identifier of the checkpoint to restore from.
   * @return A Result indicating success or failure.
   */
  virtual Result<bool> restore_checkpoint(const std::string& checkpoint_id) = 0;

  /**
   * @brief Lists all available checkpoints.
   * @return A Result containing a list of checkpoint IDs, or an error.
   */
  virtual Result<std::vector<std::string>> list_checkpoints() = 0;

  /**
   * @brief Verifies the integrity of a checkpoint.
   * @param checkpoint_id The identifier of the checkpoint to verify.
   * @return A Result indicating if the checkpoint is valid.
   */
  virtual Result<bool> verify_checkpoint(const std::string& checkpoint_id) = 0;
};

/**
 * @brief Defines categories of operations for the DegradationManager.
 */
enum class OperationType {
  READ, GET, QUERY, LIST, FETCH,
  WRITE, UPDATE, CREATE, DELETE, INSERT, MODIFY,
  HEALTH_CHECK, HEARTBEAT, STATUS,
  SHUTDOWN, RESTART, CONFIG_UPDATE
};

/**
 * @brief Defines the graceful degradation modes for system components.
 */
enum class DegradationMode {
  /// @brief Full functionality is available.
  NORMAL,
  /// @brief Only read operations are permitted.
  READ_ONLY,
  /// @brief Only essential operations (like health checks) are permitted.
  ESSENTIAL_ONLY,
  /// @brief The component is completely unavailable.
  OFFLINE
};

/**
 * @brief Parses a string to determine its corresponding OperationType.
 * @param operation The string name of the operation (e.g., "READ").
 * @return The matching OperationType enum value.
 */
OperationType parse_operation_type(const std::string& operation);

/**
 * @brief Checks if an operation type is allowed in a given degradation mode.
 * @param op_type The type of the operation.
 * @param mode The current degradation mode.
 * @return True if the operation is allowed, false otherwise.
 */
bool is_operation_type_allowed(OperationType op_type, DegradationMode mode);

/**
 * @brief Manages the graceful degradation of system components.
 * @details Allows different parts of the system to be placed into various
 * degradation modes (e.g., read-only) to handle partial failures gracefully
 * without a full system shutdown.
 */
class DegradationManager {
private:
  std::unordered_map<std::string, DegradationMode> component_modes_;
  mutable std::shared_mutex modes_mutex_;

public:
  /**
   * @brief Sets the degradation mode for a specific component.
   * @param component The name of the component (e.g., "RPCService").
   * @param mode The degradation mode to set.
   */
  void set_component_mode(const std::string& component, DegradationMode mode);

  /**
   * @brief Gets the current degradation mode for a component.
   * @param component The name of the component.
   * @return The current DegradationMode for the component.
   */
  DegradationMode get_component_mode(const std::string& component) const;

  /**
   * @brief Checks if an operation is allowed for a component in its current mode.
   * @param component The name of the component.
   * @param operation The name of the operation.
   * @return True if the operation is allowed, false otherwise.
   * @deprecated Use the overload with OperationType for better type safety.
   */
  bool is_operation_allowed(const std::string& component, const std::string& operation) const;

  /**
   * @brief Checks if an operation type is allowed for a component in its current mode.
   * @param component The name of the component.
   * @param op_type The type of the operation.
   * @return True if the operation is allowed, false otherwise.
   */
  bool is_operation_type_allowed(const std::string& component, OperationType op_type) const;

  /**
   * @brief Gets the health status of all managed components.
   * @return A map from component name to its current DegradationMode.
   */
  std::unordered_map<std::string, DegradationMode> get_system_status() const;
};

} // namespace common
} // namespace slonana