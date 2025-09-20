#include "common/fault_tolerance.h"
#include <algorithm>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>

namespace slonana {
namespace common {

// FaultTolerance static methods implementation
bool FaultTolerance::is_retryable_error(const std::string& error) {
  static const std::unordered_set<std::string> retryable_patterns = {
    "timeout", "connection", "network", "temporary", "transient",
    "unavailable", "busy", "overloaded", "rate limit", "throttle"
  };
  
  std::string lower_error = error;
  std::transform(lower_error.begin(), lower_error.end(), lower_error.begin(), ::tolower);
  
  for (const auto& pattern : retryable_patterns) {
    if (lower_error.find(pattern) != std::string::npos) {
      return true;
    }
  }
  
  return false;
}

RetryPolicy FaultTolerance::create_rpc_retry_policy() {
  RetryPolicy policy;
  policy.max_attempts = 3;
  policy.initial_delay = std::chrono::milliseconds(50);
  policy.max_delay = std::chrono::milliseconds(2000);
  policy.backoff_multiplier = 2.0;
  policy.jitter_factor = 0.1;
  return policy;
}

RetryPolicy FaultTolerance::create_network_retry_policy() {
  RetryPolicy policy;
  policy.max_attempts = 5;
  policy.initial_delay = std::chrono::milliseconds(100);
  policy.max_delay = std::chrono::milliseconds(5000);
  policy.backoff_multiplier = 1.5;
  policy.jitter_factor = 0.2;
  return policy;
}

RetryPolicy FaultTolerance::create_storage_retry_policy() {
  RetryPolicy policy;
  policy.max_attempts = 3;
  policy.initial_delay = std::chrono::milliseconds(200);
  policy.max_delay = std::chrono::milliseconds(3000);
  policy.backoff_multiplier = 2.0;
  policy.jitter_factor = 0.05;
  return policy;
}

// CircuitBreaker implementation
CircuitBreaker::CircuitBreaker(const CircuitBreakerConfig& config)
    : config_(config), state_(CircuitState::CLOSED), failure_count_(0), 
      success_count_(0), last_failure_time_(std::chrono::steady_clock::now()) {}

CircuitState CircuitBreaker::get_state() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

uint32_t CircuitBreaker::get_failure_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return failure_count_;
}

void CircuitBreaker::on_success() {
  std::lock_guard<std::mutex> lock(mutex_);
  on_success_unlocked();
}

void CircuitBreaker::on_failure() {
  std::lock_guard<std::mutex> lock(mutex_);
  on_failure_unlocked();
}

void CircuitBreaker::on_success_unlocked() {
  if (state_ == CircuitState::HALF_OPEN) {
    success_count_++;
    if (success_count_ >= config_.success_threshold) {
      state_ = CircuitState::CLOSED;
      failure_count_ = 0;
      success_count_ = 0;
      // Only log state changes in debug builds or when explicitly enabled
      #ifdef DEBUG_FAULT_TOLERANCE
      std::cout << "Circuit breaker state changed to CLOSED" << std::endl;
      #endif
    }
  } else if (state_ == CircuitState::CLOSED) {
    failure_count_ = 0; // Reset failure count on success
  }
}

void CircuitBreaker::on_failure_unlocked() {
  failure_count_++;
  last_failure_time_ = std::chrono::steady_clock::now();
  
  if (state_ == CircuitState::CLOSED && failure_count_ >= config_.failure_threshold) {
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

// DegradationManager implementation
void DegradationManager::set_component_mode(const std::string& component, DegradationMode mode) {
  std::unique_lock<std::shared_mutex> lock(modes_mutex_);
  auto old_mode = component_modes_[component];
  component_modes_[component] = mode;
  
  #ifdef DEBUG_FAULT_TOLERANCE
  if (old_mode != mode) {
    std::cout << "Component '" << component << "' degradation mode changed from " 
              << static_cast<int>(old_mode) << " to " << static_cast<int>(mode) << std::endl;
  }
  #endif
}

DegradationMode DegradationManager::get_component_mode(const std::string& component) const {
  std::shared_lock<std::shared_mutex> lock(modes_mutex_);
  auto it = component_modes_.find(component);
  return (it != component_modes_.end()) ? it->second : DegradationMode::NORMAL;
}

bool DegradationManager::is_operation_allowed(const std::string& component, const std::string& operation) const {
  DegradationMode mode = get_component_mode(component);
  
  switch (mode) {
    case DegradationMode::NORMAL:
      return true;
      
    case DegradationMode::READ_ONLY:
      // Use strict matching to prevent false positives
      // Only allow operations that are explicitly read-only
      if (operation == "read" || operation == "get" || operation == "query") {
        return true;
      }
      
      // For compound operations, ensure they don't contain write-like keywords
      if (operation.find("write") != std::string::npos ||
          operation.find("update") != std::string::npos ||
          operation.find("modify") != std::string::npos ||
          operation.find("create") != std::string::npos ||
          operation.find("delete") != std::string::npos ||
          operation.find("insert") != std::string::npos ||
          operation.find("remove") != std::string::npos) {
        return false;
      }
      
      // Allow operations that start with read_, get_, query_ (but we already checked for write keywords above)
      if ((operation.length() >= 5 && operation.substr(0, 5) == "read_") ||
          (operation.length() >= 4 && operation.substr(0, 4) == "get_") ||
          (operation.length() >= 6 && operation.substr(0, 6) == "query_")) {
        return true;
      }
      
      // Allow operations that end with _read, _get
      if (operation.find("_read") != std::string::npos || operation.find("_get") != std::string::npos) {
        return true;
      }
      
      return false;
             
    case DegradationMode::ESSENTIAL_ONLY:
      // Only allow explicitly marked essential operations
      if (operation == "essential" || operation == "critical" || operation == "health") {
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
          operation.find("non") == std::string::npos) { // Exclude "non_essential"
        return true;
      }
      return false;
             
    case DegradationMode::OFFLINE:
      return false;
      
    default:
      return false;
  }
}

std::unordered_map<std::string, DegradationMode> DegradationManager::get_system_status() const {
  std::shared_lock<std::shared_mutex> lock(modes_mutex_);
  return component_modes_;
}

// Helper functions for enum-based operation checking
OperationType parse_operation_type(const std::string& operation) {
  // Convert to lowercase for case-insensitive matching
  std::string lower_op = operation;
  std::transform(lower_op.begin(), lower_op.end(), lower_op.begin(), ::tolower);
  
  // Read operations
  if (lower_op == "read" || lower_op.find("read") == 0) return OperationType::READ;
  if (lower_op == "get" || lower_op.find("get") == 0) return OperationType::GET;
  if (lower_op == "query" || lower_op.find("query") == 0) return OperationType::QUERY;
  if (lower_op == "list" || lower_op.find("list") == 0) return OperationType::LIST;
  if (lower_op == "fetch" || lower_op.find("fetch") == 0) return OperationType::FETCH;
  
  // Write operations
  if (lower_op == "write" || lower_op.find("write") != std::string::npos) return OperationType::WRITE;
  if (lower_op == "update" || lower_op.find("update") != std::string::npos) return OperationType::UPDATE;
  if (lower_op == "create" || lower_op.find("create") != std::string::npos) return OperationType::CREATE;
  if (lower_op == "delete" || lower_op.find("delete") != std::string::npos) return OperationType::DELETE;
  if (lower_op == "insert" || lower_op.find("insert") != std::string::npos) return OperationType::INSERT;
  if (lower_op == "modify" || lower_op.find("modify") != std::string::npos) return OperationType::MODIFY;
  
  // Essential operations
  if (lower_op.find("health") != std::string::npos) return OperationType::HEALTH_CHECK;
  if (lower_op.find("heartbeat") != std::string::npos) return OperationType::HEARTBEAT;
  if (lower_op.find("status") != std::string::npos) return OperationType::STATUS;
  
  // Default to read for unknown operations to be safe
  return OperationType::READ;
}

bool is_operation_type_allowed(OperationType op_type, DegradationMode mode) {
  switch (mode) {
    case DegradationMode::NORMAL:
      return true;
      
    case DegradationMode::READ_ONLY:
      return op_type == OperationType::READ ||
             op_type == OperationType::GET ||
             op_type == OperationType::QUERY ||
             op_type == OperationType::LIST ||
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

bool DegradationManager::is_operation_type_allowed(const std::string& component, OperationType op_type) const {
  DegradationMode mode = get_component_mode(component);
  return ::slonana::common::is_operation_type_allowed(op_type, mode);
}

} // namespace common
} // namespace slonana