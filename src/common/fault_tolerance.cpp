/**
 * @file fault_tolerance.cpp
 * @brief Implements utilities and patterns for building resilient systems.
 *
 * This file provides the concrete implementations for the fault tolerance
 * mechanisms defined in `fault_tolerance.h`, including retry logic,
 * circuit breakers, and graceful degradation management.
 */
#include "common/fault_tolerance.h"
#include <algorithm>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>

namespace slonana {
namespace common {

/**
 * @brief Determines if an error is retryable based on its message.
 * @param error The error message string.
 * @return True if the error message contains keywords indicating a transient
 * issue (e.g., "timeout", "connection", "unavailable").
 */
bool FaultTolerance::is_retryable_error(const std::string &error) {
  static const std::unordered_set<std::string> retryable_patterns = {
      "timeout",     "connection", "network",    "temporary",  "transient",
      "unavailable", "busy",       "overloaded", "rate limit", "throttle"};

  std::string lower_error = error;
  std::transform(lower_error.begin(), lower_error.end(), lower_error.begin(),
                 ::tolower);

  for (const auto &pattern : retryable_patterns) {
    if (lower_error.find(pattern) != std::string::npos) {
      return true;
    }
  }

  return false;
}

/**
 * @brief Creates a default retry policy optimized for RPC calls.
 * @return A RetryPolicy with moderate attempts and short delays.
 */
RetryPolicy FaultTolerance::create_rpc_retry_policy() {
  RetryPolicy policy;
  policy.max_attempts = 3;
  policy.initial_delay = std::chrono::milliseconds(50);
  policy.max_delay = std::chrono::milliseconds(2000);
  policy.backoff_multiplier = 2.0;
  policy.jitter_factor = 0.1;
  return policy;
}

/**
 * @brief Creates a default retry policy optimized for network operations.
 * @return A RetryPolicy with more attempts and longer delays to handle
 * network instability.
 */
RetryPolicy FaultTolerance::create_network_retry_policy() {
  RetryPolicy policy;
  policy.max_attempts = 5;
  policy.initial_delay = std::chrono::milliseconds(100);
  policy.max_delay = std::chrono::milliseconds(5000);
  policy.backoff_multiplier = 1.5;
  policy.jitter_factor = 0.2;
  return policy;
}

/**
 * @brief Creates a default retry policy for storage-related operations.
 * @return A RetryPolicy with parameters suited for I/O operations.
 */
RetryPolicy FaultTolerance::create_storage_retry_policy() {
  RetryPolicy policy;
  policy.max_attempts = 3;
  policy.initial_delay = std::chrono::milliseconds(200);
  policy.max_delay = std::chrono::milliseconds(3000);
  policy.backoff_multiplier = 2.0;
  policy.jitter_factor = 0.05;
  return policy;
}

/**
 * @brief Constructs a CircuitBreaker with the given configuration.
 * @param config The configuration settings for the circuit breaker.
 */
CircuitBreaker::CircuitBreaker(const CircuitBreakerConfig &config)
    : config_(config), state_(CircuitState::CLOSED), failure_count_(0),
      success_count_(0), last_failure_time_(std::chrono::steady_clock::now()) {}

/**
 * @brief Gets the current state of the circuit breaker in a thread-safe manner.
 * @return The current CircuitState.
 */
CircuitState CircuitBreaker::get_state() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

/**
 * @brief Gets the current number of consecutive failures in a thread-safe manner.
 * @return The current failure count.
 */
uint32_t CircuitBreaker::get_failure_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return failure_count_;
}

/**
 * @brief Records a successful operation, acquiring a lock.
 */
void CircuitBreaker::on_success() {
  std::lock_guard<std::mutex> lock(mutex_);
  on_success_unlocked();
}

/**
 * @brief Records a failed operation, acquiring a lock.
 */
void CircuitBreaker::on_failure() {
  std::lock_guard<std::mutex> lock(mutex_);
  on_failure_unlocked();
}

/**
 * @brief Handles the logic for a successful operation. Assumes a lock is already held.
 * @details If in HALF_OPEN state, increments success count and potentially
 * transitions to CLOSED. If in CLOSED state, resets failure count.
 */
void CircuitBreaker::on_success_unlocked() {
  if (state_ == CircuitState::HALF_OPEN) {
    success_count_++;
    if (success_count_ >= config_.success_threshold) {
      state_ = CircuitState::CLOSED;
      failure_count_ = 0;
      success_count_ = 0;
#ifdef DEBUG_FAULT_TOLERANCE
      std::cout << "Circuit breaker state changed to CLOSED" << std::endl;
#endif
    }
  } else if (state_ == CircuitState::CLOSED) {
    failure_count_ = 0;
  }
}

/**
 * @brief Handles the logic for a failed operation. Assumes a lock is already held.
 * @details Increments failure count and potentially transitions to OPEN state if
 * the threshold is reached. If already in HALF_OPEN, transitions back to OPEN.
 */
void CircuitBreaker::on_failure_unlocked() {
  failure_count_++;
  last_failure_time_ = std::chrono::steady_clock::now();

  if (state_ == CircuitState::CLOSED &&
      failure_count_ >= config_.failure_threshold) {
    state_ = CircuitState::OPEN;
#ifdef DEBUG_FAULT_TOLERANCE
    std::cout << "Circuit breaker state changed to OPEN after "
              << failure_count_ << " failures" << std::endl;
#endif
  } else if (state_ == CircuitState::HALF_OPEN) {
    state_ = CircuitState::OPEN;
    success_count_ = 0;
#ifdef DEBUG_FAULT_TOLERANCE
    std::cout << "Circuit breaker state changed back to OPEN" << std::endl;
#endif
  }
}

/**
 * @brief Sets the degradation mode for a component in a thread-safe manner.
 * @param component The name of the component.
 * @param mode The new degradation mode.
 */
void DegradationManager::set_component_mode(const std::string &component,
                                            DegradationMode mode) {
  std::unique_lock<std::shared_mutex> lock(modes_mutex_);
  auto old_mode = component_modes_[component];
  component_modes_[component] = mode;

#ifdef DEBUG_FAULT_TOLERANCE
  if (old_mode != mode) {
    std::cout << "Component '" << component
              << "' degradation mode changed from "
              << static_cast<int>(old_mode) << " to " << static_cast<int>(mode)
              << std::endl;
  }
#endif
}

/**
 * @brief Retrieves the current degradation mode for a component.
 * @param component The name of the component.
 * @return The current degradation mode, or NORMAL if not set.
 */
DegradationMode
DegradationManager::get_component_mode(const std::string &component) const {
  std::shared_lock<std::shared_mutex> lock(modes_mutex_);
  auto it = component_modes_.find(component);
  return (it != component_modes_.end()) ? it->second : DegradationMode::NORMAL;
}

/**
 * @brief (Deprecated) Checks if an operation is allowed based on string matching.
 * @param component The name of the component.
 * @param operation The name of the operation.
 * @return True if the operation is permitted in the component's current mode.
 */
bool DegradationManager::is_operation_allowed(
    const std::string &component, const std::string &operation) const {
  DegradationMode mode = get_component_mode(component);

  switch (mode) {
  case DegradationMode::NORMAL:
    return true;

  case DegradationMode::READ_ONLY:
    if (operation == "read" || operation == "get" || operation == "query") {
      return true;
    }
    if (operation.find("write") != std::string::npos ||
        operation.find("update") != std::string::npos ||
        operation.find("modify") != std::string::npos ||
        operation.find("create") != std::string::npos ||
        operation.find("delete") != std::string::npos ||
        operation.find("insert") != std::string::npos ||
        operation.find("remove") != std::string::npos) {
      return false;
    }
    if ((operation.length() >= 5 && operation.substr(0, 5) == "read_") ||
        (operation.length() >= 4 && operation.substr(0, 4) == "get_") ||
        (operation.length() >= 6 && operation.substr(0, 6) == "query_")) {
      return true;
    }
    if (operation.find("_read") != std::string::npos ||
        operation.find("_get") != std::string::npos) {
      return true;
    }
    return false;

  case DegradationMode::ESSENTIAL_ONLY:
    if (operation == "essential" || operation == "critical" ||
        operation == "health") {
      return true;
    }
    if ((operation.length() >= 10 && operation.substr(0, 10) == "essential_") ||
        (operation.length() >= 9 && operation.substr(0, 9) == "critical_") ||
        (operation.length() >= 7 && operation.substr(0, 7) == "health_")) {
      return true;
    }
    if ((operation.find("_essential") != std::string::npos ||
         operation.find("_critical") != std::string::npos ||
         operation.find("_health") != std::string::npos) &&
        operation.find("non") == std::string::npos) {
      return true;
    }
    return false;

  case DegradationMode::OFFLINE:
    return false;

  default:
    return false;
  }
}

/**
 * @brief Retrieves the status of all components in a thread-safe manner.
 * @return A map of component names to their current degradation modes.
 */
std::unordered_map<std::string, DegradationMode>
DegradationManager::get_system_status() const {
  std::shared_lock<std::shared_mutex> lock(modes_mutex_);
  return component_modes_;
}

/**
 * @brief Parses a string to determine its corresponding OperationType.
 * @param operation The string name of the operation.
 * @return The best-matching OperationType. Defaults to READ for safety.
 */
OperationType parse_operation_type(const std::string &operation) {
  std::string lower_op = operation;
  std::transform(lower_op.begin(), lower_op.end(), lower_op.begin(), ::tolower);

  if (lower_op == "read" || lower_op.find("read") == 0) return OperationType::READ;
  if (lower_op == "get" || lower_op.find("get") == 0) return OperationType::GET;
  if (lower_op == "query" || lower_op.find("query") == 0) return OperationType::QUERY;
  if (lower_op == "list" || lower_op.find("list") == 0) return OperationType::LIST;
  if (lower_op == "fetch" || lower_op.find("fetch") == 0) return OperationType::FETCH;
  if (lower_op == "write" || lower_op.find("write") != std::string::npos) return OperationType::WRITE;
  if (lower_op == "update" || lower_op.find("update") != std::string::npos) return OperationType::UPDATE;
  if (lower_op == "create" || lower_op.find("create") != std::string::npos) return OperationType::CREATE;
  if (lower_op == "delete" || lower_op.find("delete") != std::string::npos) return OperationType::DELETE;
  if (lower_op == "insert" || lower_op.find("insert") != std::string::npos) return OperationType::INSERT;
  if (lower_op == "modify" || lower_op.find("modify") != std::string::npos) return OperationType::MODIFY;
  if (lower_op.find("health") != std::string::npos) return OperationType::HEALTH_CHECK;
  if (lower_op.find("heartbeat") != std::string::npos) return OperationType::HEARTBEAT;
  if (lower_op.find("status") != std::string::npos) return OperationType::STATUS;

  return OperationType::READ;
}

/**
 * @brief Checks if an operation type is allowed in a given degradation mode.
 * @param op_type The type of operation.
 * @param mode The current degradation mode.
 * @return True if the operation is permitted, false otherwise.
 */
bool is_operation_type_allowed(OperationType op_type, DegradationMode mode) {
  switch (mode) {
  case DegradationMode::NORMAL:
    return true;

  case DegradationMode::READ_ONLY:
    return op_type == OperationType::READ || op_type == OperationType::GET ||
           op_type == OperationType::QUERY || op_type == OperationType::LIST ||
           op_type == OperationType::FETCH ||
           op_type == OperationType::HEALTH_CHECK ||
           op_type == OperationType::HEARTBEAT ||
           op_type == OperationType::STATUS;

  case DegradationMode::ESSENTIAL_ONLY:
    return op_type == OperationType::HEALTH_CHECK ||
           op_type == OperationType::HEARTBEAT ||
           op_type == OperationType::STATUS;

  case DegradationMode::OFFLINE:
    return false;

  default:
    return false;
  }
}

/**
 * @brief Checks if an operation is allowed for a component using its type.
 * @param component The name of the component.
 * @param op_type The type of the operation.
 * @return True if the operation is permitted in the component's current mode.
 */
bool DegradationManager::is_operation_type_allowed(
    const std::string &component, OperationType op_type) const {
  DegradationMode mode = get_component_mode(component);
  return ::slonana::common::is_operation_type_allowed(op_type, mode);
}

} // namespace common
} // namespace slonana