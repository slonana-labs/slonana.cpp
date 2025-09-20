# Slonana.cpp Code Style Guide

This document establishes coding and documentation standards for the Slonana C++ validator to ensure consistent, maintainable, and self-documenting code.

## Table of Contents
- [Documentation Standards](#documentation-standards)
- [Modern C++ Guidelines](#modern-c-guidelines)
- [Code Organization](#code-organization)
- [Naming Conventions](#naming-conventions)
- [Error Handling](#error-handling)
- [Performance Guidelines](#performance-guidelines)

## Documentation Standards

### Doxygen Documentation

All public APIs (classes, functions, enums) **must** be documented using Doxygen-compatible comments.

#### Function Documentation Template

```cpp
/**
 * @brief Brief description of what the function does
 * 
 * Detailed description explaining the purpose, algorithm, or important
 * implementation details. Explain the "why" not just the "what".
 * 
 * @param param_name Description of the parameter, including valid ranges/values
 * @param another_param Description with constraints or special behavior
 * @return Description of return value, including error conditions
 * @throws ExceptionType When this exception might be thrown
 * @note Important implementation notes or caveats
 * @warning Performance implications or thread safety concerns
 * 
 * Example usage:
 * @code
 * auto result = function_name(42, "example");
 * if (result.has_value()) {
 *     // Handle success case
 * }
 * @endcode
 */
common::Result<bool> function_name(int param_name, const std::string& another_param) noexcept;
```

#### Class Documentation Template

```cpp
/**
 * @brief Brief description of the class purpose
 * 
 * Detailed description of the class responsibility, design patterns used,
 * thread safety guarantees, and lifecycle management.
 * 
 * @note Thread safety: [Safe/Unsafe/Conditionally safe with details]
 * @note Exception safety: [No-throw/Basic/Strong guarantee]
 * 
 * Example usage:
 * @code
 * auto validator = std::make_unique<ValidatorCore>(ledger, identity);
 * validator->start();
 * @endcode
 */
class ValidatorCore {
public:
    /**
     * @brief Constructor that initializes the validator with dependencies
     * 
     * @param ledger Shared pointer to ledger manager (must not be null)
     * @param identity Validator's public key for network identification
     * @throws std::invalid_argument If ledger is null or identity is invalid
     */
    explicit ValidatorCore(std::shared_ptr<ledger::LedgerManager> ledger,
                          const PublicKey& identity);
    
    /// Default destructor - automatically stops validator if running
    ~ValidatorCore() = default;
    
    // ... rest of class
};
```

#### Inline Comments

Use inline comments sparingly and focus on explaining **why** something is done, not **what** is being done:

```cpp
// Good: Explains why
// Use atomic flag to ensure thread-safe shutdown across validator threads
std::atomic<bool> shutdown_requested_{false};

// Avoid: Explains what (obvious from code)
// Set shutdown_requested to false
std::atomic<bool> shutdown_requested_{false};
```

## Modern C++ Guidelines

### Memory Management

**Always prefer smart pointers over raw pointers:**

```cpp
// Good: RAII and automatic cleanup
auto manager = std::make_unique<LedgerManager>(config);
std::shared_ptr<ValidatorCore> validator = create_validator();

// Avoid: Manual memory management
LedgerManager* manager = new LedgerManager(config);  // Don't do this
```

### Type Safety and Clarity

**Use `auto` judiciously for type deduction:**

```cpp
// Good: Clear intent, complex template types
auto result = complicated_template_function<T, U, V>();
auto it = container.find(key);

// Good: Explicit types for simple cases
bool is_valid = validate_block(block);
size_t count = transactions.size();

// Avoid: Obscure intent
auto x = 42;  // Use 'int x = 42;' instead
```

**Prefer modern casting over C-style casts:**

```cpp
// Good: Explicit intent
auto derived = static_cast<DerivedClass*>(base_ptr);
auto bytes = reinterpret_cast<const uint8_t*>(data.data());

// Avoid: C-style casts
auto derived = (DerivedClass*)base_ptr;  // Don't do this
```

### Function Specifications

**Use `noexcept` for functions that won't throw:**

```cpp
// Good: Clear exception contract
bool is_valid_slot(Slot slot) noexcept;
Hash calculate_hash(const std::vector<uint8_t>& data) noexcept;

// Functions that may throw should not use noexcept
Result<bool> validate_transaction(const Transaction& tx);  // May throw
```

**Use `constexpr` for compile-time computable functions:**

```cpp
// Good: Compile-time constants
constexpr uint32_t DEFAULT_RPC_PORT = 8899;
constexpr size_t MAX_TRANSACTION_SIZE = 1232;

// Good: Compile-time functions
constexpr bool is_power_of_two(uint64_t n) noexcept {
    return n > 0 && (n & (n - 1)) == 0;
}
```

**Use `override` and `final` for virtual functions:**

```cpp
class DerivedValidator : public ValidatorBase {
public:
    // Good: Explicit override
    bool validate_block(const Block& block) override;
    
    // Good: Prevent further inheritance
    void shutdown() final;
};
```

### Container and Algorithm Usage

**Prefer range-based loops:**

```cpp
// Good: Range-based for
for (const auto& transaction : block.transactions) {
    process_transaction(transaction);
}

// Avoid: Index-based loops when not needed
for (size_t i = 0; i < block.transactions.size(); ++i) {
    process_transaction(block.transactions[i]);  // Unnecessary indexing
}
```

**Use standard algorithms:**

```cpp
// Good: Expressive algorithms
auto it = std::find_if(validators.begin(), validators.end(),
                      [&](const auto& v) { return v.identity == target_id; });

// Good: Parallel algorithms for performance-critical paths
std::transform(std::execution::par_unseq,
               transactions.begin(), transactions.end(),
               results.begin(), process_transaction);
```

## Code Organization

### Header Dependencies

**Minimize header dependencies:**

```cpp
// Good: Forward declarations in headers
namespace slonana {
namespace ledger {
    class LedgerManager;  // Forward declaration
}
}

// Include implementations in .cpp files
#include "ledger/manager.h"  // Only in .cpp
```

**Use include guards consistently:**

```cpp
#pragma once  // Preferred over traditional include guards

// Alternative (if #pragma once not supported):
#ifndef SLONANA_VALIDATOR_CORE_H
#define SLONANA_VALIDATOR_CORE_H
// ... content ...
#endif
```

### Namespace Usage

**Use namespace hierarchy consistently:**

```cpp
namespace slonana {
namespace validator {

class ValidatorCore {
    // Implementation
};

}  // namespace validator
}  // namespace slonana
```

## Naming Conventions

### Consistent Naming Patterns

```cpp
// Classes: PascalCase
class ValidatorCore;
class LedgerManager;

// Functions and variables: snake_case
void process_transaction();
bool is_valid_block;
size_t transaction_count;

// Constants: SCREAMING_SNAKE_CASE
constexpr uint32_t MAX_BLOCK_SIZE = 65536;
constexpr int64_t DEFAULT_TIMEOUT_MS = 5000;

// Private members: trailing underscore
class ValidatorCore {
private:
    std::atomic<bool> running_;
    std::unique_ptr<Impl> impl_;
};

// Enums: PascalCase with descriptive prefix
enum class ValidationResult {
    SUCCESS,
    INVALID_SIGNATURE,
    INSUFFICIENT_FUNDS
};
```

## Error Handling

### Result Type Pattern

**Use `Result<T>` for recoverable errors:**

```cpp
/**
 * @brief Validates a transaction for execution
 * 
 * @param transaction Transaction to validate
 * @return Result containing validation success or error details
 * @note This function never throws exceptions
 */
common::Result<bool> validate_transaction(const Transaction& transaction) noexcept;
```

**Reserve exceptions for exceptional conditions:**

```cpp
// Good: Use for programming errors or unrecoverable failures
if (ledger == nullptr) {
    throw std::invalid_argument("Ledger cannot be null");
}

// Good: Use Result<T> for expected error conditions
auto result = parse_transaction(raw_data);
if (!result.has_value()) {
    log_warning("Failed to parse transaction: {}", result.error());
    return;
}
```

## Performance Guidelines

### Hot Path Optimization

**Avoid allocations in critical paths:**

```cpp
// Good: Pre-allocate and reuse
class TransactionProcessor {
private:
    std::vector<Transaction> transaction_buffer_;  // Reused across calls
    
public:
    void process_transactions(const std::vector<Transaction>& txs) {
        transaction_buffer_.clear();
        transaction_buffer_.reserve(txs.size());  // Avoid reallocations
        // ... processing logic
    }
};
```

**Use appropriate container types:**

```cpp
// Good: Choose containers for access patterns
std::unordered_map<Hash, Transaction> tx_cache_;  // O(1) lookup
std::vector<Transaction> pending_txs_;            // Sequential access
std::array<uint8_t, 32> hash_buffer_;            // Fixed size, stack allocated
```

### Thread Safety Documentation

**Always document thread safety guarantees:**

```cpp
/**
 * @brief Thread-safe transaction pool for pending transactions
 * 
 * All public methods are thread-safe and can be called concurrently
 * from multiple threads without external synchronization.
 * 
 * @note Internal synchronization uses RWLock for optimal read performance
 * @warning Do not hold references to returned objects across method calls
 */
class ThreadSafeTransactionPool {
    // Implementation
};
```

## Examples

### Before: Inconsistent Style

```cpp
// Poor documentation and style
class validator {
public:
    bool start();  // No documentation
    void Stop();   // Inconsistent naming
    
private:
    bool running;  // No trailing underscore
    LedgerManager* ledger;  // Raw pointer
};

bool validator::start() {
    if (ledger == NULL) return false;  // C-style null check
    running = true;
    return true;
}
```

### After: Consistent Style

```cpp
/**
 * @brief Core validator implementation handling consensus and block production
 * 
 * This class coordinates between ledger management, network communication,
 * and consensus mechanisms to operate as a Solana validator node.
 * 
 * @note Thread safety: All public methods are thread-safe
 * @note Exception safety: Strong guarantee for all operations
 */
class ValidatorCore {
public:
    /**
     * @brief Starts the validator and begins participating in consensus
     * 
     * Initializes all subsystems and starts background threads for
     * block production, transaction processing, and network communication.
     * 
     * @return Result indicating success or detailed error information
     * @note Must be called before any other validator operations
     */
    common::Result<bool> start() noexcept;
    
    /**
     * @brief Gracefully stops the validator and cleans up resources
     * 
     * Signals all background threads to stop and waits for clean shutdown.
     * Safe to call multiple times.
     */
    void stop() noexcept;
    
private:
    std::atomic<bool> running_{false};
    std::shared_ptr<ledger::LedgerManager> ledger_;
};

common::Result<bool> ValidatorCore::start() noexcept {
    if (ledger_ == nullptr) {
        return common::Result<bool>("Ledger manager not initialized");
    }
    
    running_.store(true, std::memory_order_release);
    return common::Result<bool>(true);
}
```

---

## Integration with Development Workflow

### Pre-commit Checks

These standards are enforced through:
- `make format` - Applies clang-format rules
- `make lint` - Runs static analysis (cppcheck)
- `make ci-fast` - Validates formatting and basic tests

### Code Review Focus

During code reviews, prioritize:
1. **Documentation completeness** - All public APIs documented
2. **Exception safety** - Clear error handling strategy
3. **Performance implications** - Allocation patterns in hot paths
4. **Thread safety** - Synchronization requirements documented
5. **Modern C++ usage** - Smart pointers, RAII, constexpr where appropriate

---

This style guide evolves with the codebase. Propose improvements through the standard PR process.