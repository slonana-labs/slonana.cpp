# Slonana.cpp Development Guide

## Table of Contents
- [Getting Started](#getting-started)
- [Development Environment](#development-environment)
- [Code Organization](#code-organization)
- [Building and Testing](#building-and-testing)
- [Contributing Guidelines](#contributing-guidelines)
- [Debugging and Profiling](#debugging-and-profiling)
- [Best Practices](#best-practices)

## Getting Started

### Prerequisites

**Required:**
- C++20 compatible compiler (GCC 13.3+, Clang 15+, or MSVC 19.29+)
- CMake 3.16 or higher
- Git for version control

**Development Philosophy:**
Slonana.cpp is built with a "zero-mock" philosophy - all code uses real implementations, not mock objects or test stubs. This ensures that development and testing environments closely mirror production behavior.

**Platform-Specific Setup:**

**Universal Installation (Recommended):**
```bash
# Automatically installs all development dependencies
curl -sSL https://install.slonana.com | bash
```

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    git \
    clang-format \
    clang-tidy \
    cppcheck \
    valgrind \
    gdb
```

**macOS:**
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install Homebrew
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install cmake git llvm
```

**Windows:**
```powershell
# Install Visual Studio 2022 with C++ workload
# Install vcpkg for package management
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg integrate install
```

### Quick Start

```bash
# Clone the repository
git clone https://github.com/slonana-labs/slonana.cpp.git
cd slonana.cpp

# Create build directory
mkdir build
cd build

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build the project
make -j$(nproc)  # Linux/macOS
# or
cmake --build . --config Release  # Windows

# Run tests
./slonana_tests
./slonana_comprehensive_tests

# Run the validator
./slonana_validator --help
```

## Development Environment

### Recommended IDE Setup

**Visual Studio Code:**
```json
// .vscode/settings.json
{
    "C_Cpp.default.cppStandard": "c++20",
    "C_Cpp.default.compilerPath": "/usr/bin/g++",
    "C_Cpp.default.includePath": [
        "${workspaceFolder}/include",
        "${workspaceFolder}/build/_deps/**"
    ],
    "files.associations": {
        "*.h": "cpp",
        "*.cpp": "cpp"
    },
    "editor.formatOnSave": true,
    "C_Cpp.clang_format_style": "file"
}
```

**CLion:**
- Import CMake project
- Enable ClangFormat integration
- Configure code inspection profiles
- Set up remote development for Linux targets

### Code Formatting

The project uses `.clang-format` for consistent code style:

```yaml
# .clang-format
BasedOnStyle: Google
IndentWidth: 4
TabWidth: 4
UseTab: Never
ColumnLimit: 100
BreakBeforeBraces: Attach
AllowShortFunctionsOnASingleLine: Empty
AlwaysBreakTemplateDeclarations: Yes
Standard: c++20
```

**Format code:**
```bash
# Format all source files
find src include tests -name "*.cpp" -o -name "*.h" | xargs clang-format -i

# Check formatting (CI)
find src include tests -name "*.cpp" -o -name "*.h" | xargs clang-format --dry-run --Werror
```

## Code Organization

### Directory Structure

```
slonana.cpp/
├── include/                 # Public headers
│   ├── common/             # Common types and utilities
│   ├── network/            # Network layer interfaces  
│   ├── ledger/             # Ledger management
│   ├── validator/          # Validator core logic
│   ├── staking/            # Staking system
│   └── svm/                # SVM execution engine
├── src/                    # Implementation files
│   ├── common/             # Common utilities implementation
│   ├── network/            # Network layer implementation
│   ├── ledger/             # Ledger management implementation
│   ├── validator/          # Validator core implementation
│   ├── staking/            # Staking system implementation
│   ├── svm/                # SVM engine implementation
│   ├── main.cpp            # Main validator executable
│   └── slonana_validator.cpp # Validator integration
├── tests/                  # Test suites
│   ├── test_common.cpp     # Common types tests
│   ├── test_network.cpp    # Network layer tests
│   ├── test_ledger.cpp     # Ledger tests
│   ├── test_validator.cpp  # Validator tests
│   ├── test_consensus.cpp  # Consensus tests
│   ├── test_rpc_comprehensive.cpp # RPC API tests
│   └── test_integration.cpp # Integration tests
├── docs/                   # Documentation
├── scripts/                # Build and utility scripts
└── benchmarks/             # Performance benchmarks
```

### Namespace Organization

```cpp
namespace slonana {
    namespace common {      // Basic types, utilities, crypto
        class PublicKey;
        class Hash;
        class Signature;
        template<typename T> class Result;
    }
    
    namespace network {     // Network protocols and RPC
        class GossipProtocol;
        class RpcServer;
        class P2pManager;
    }
    
    namespace ledger {      // Block storage and transaction handling
        class Block;
        class Transaction;
        class LedgerManager;
    }
    
    namespace validator {   // Consensus and validation logic
        class ValidatorCore;
        class ForkChoice;
        class VoteProcessor;
    }
    
    namespace staking {     // Stake accounts and rewards
        class StakeAccount;
        class StakingManager;
        class RewardsCalculator;
    }
    
    namespace svm {         // Virtual machine execution
        class ExecutionEngine;
        class AccountManager;
        class ProgramLoader;
    }
}
```

### Header Dependencies

Follow these guidelines for header dependencies:

1. **Forward Declarations**: Use forward declarations in headers when possible
2. **Include Order**: System headers first, then third-party, then project headers
3. **Header Guards**: Use `#pragma once` for header guards
4. **Minimal Includes**: Only include what you directly use

```cpp
// Good: forward declaration in header
// validator_core.h
namespace slonana::ledger {
    class Block;  // Forward declaration
}

namespace slonana::validator {
    class ValidatorCore {
        bool process_block(const ledger::Block& block);
    };
}

// Implementation includes full definition
// validator_core.cpp
#include "validator/validator_core.h"
#include "ledger/block.h"  // Full include for implementation
```

## Building and Testing

### CMake Configuration

**Build Types:**
```bash
# Debug build (default)
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Release build
cmake .. -DCMAKE_BUILD_TYPE=Release

# Release with debug info
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Minimum size release
cmake .. -DCMAKE_BUILD_TYPE=MinSizeRel
```

**Advanced Options:**
```bash
# Enable AddressSanitizer
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON

# Enable ThreadSanitizer
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_TSAN=ON

# Enable static analysis
cmake .. -DENABLE_STATIC_ANALYSIS=ON

# Cross-compilation for ARM64
cmake .. -DCMAKE_TOOLCHAIN_FILE=cmake/arm64-toolchain.cmake
```

### Testing Framework

The project uses a custom lightweight testing framework optimized for performance testing:

```cpp
// Basic test structure
TEST(TestSuiteName, TestName) {
    // Arrange
    auto validator = create_test_validator();
    
    // Act
    auto result = validator.process_transaction(test_tx);
    
    // Assert
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(expected_balance, get_account_balance(test_account));
}

// Benchmark test
BENCHMARK(BenchmarkSuiteName, BenchmarkName) {
    auto start = std::chrono::high_resolution_clock::now();
    
    // Code to benchmark
    for (int i = 0; i < 1000; ++i) {
        hash_function(test_data);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    
    REPORT_METRIC("hash_ops_per_second", 1000.0 * 1e9 / duration.count());
}
```

### Running Tests

**Unit Tests:**
```bash
# Run all tests
./slonana_tests

# Run specific test suite
./slonana_tests --filter="Common*"

# Run with verbose output
./slonana_tests --verbose

# Run comprehensive tests
./slonana_comprehensive_tests
```

**Performance Tests:**
```bash
# Run benchmarks
./slonana_benchmarks

# Run with custom iterations
./slonana_benchmarks --iterations=10000

# Generate detailed report
./slonana_benchmarks --output=json > benchmark_results.json
```

**Integration Tests:**
```bash
# Full validator integration
./slonana_comprehensive_tests --integration

# Network integration with multiple nodes
./run_network_tests.sh

# RPC API conformance
./test_rpc_conformance.sh
```

## Contributing Guidelines

### Git Workflow

1. **Fork and Clone:**
```bash
git clone https://github.com/yourusername/slonana.cpp.git
cd slonana.cpp
git remote add upstream https://github.com/slonana-labs/slonana.cpp.git
```

2. **Create Feature Branch:**
```bash
git checkout -b feature/amazing-feature
```

3. **Make Changes and Commit:**
```bash
# Make your changes
git add .
git commit -m "feat: add amazing feature

- Implements XYZ functionality
- Improves performance by 20%
- Adds comprehensive tests

Closes #123"
```

4. **Push and Create PR:**
```bash
git push origin feature/amazing-feature
# Create pull request on GitHub
```

### Commit Message Format

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>[optional scope]: <description>

[optional body]

[optional footer(s)]
```

**Types:**
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation changes
- `style`: Code style changes (formatting, etc.)
- `refactor`: Code refactoring
- `perf`: Performance improvements
- `test`: Adding or modifying tests
- `chore`: Maintenance tasks

### Code Review Process

1. **Automated Checks:** All PRs must pass CI/CD pipeline
2. **Code Review:** At least one maintainer review required
3. **Testing:** New features must include comprehensive tests
4. **Documentation:** Update docs for user-facing changes
5. **Performance:** Benchmark critical path changes

### Pull Request Template

```markdown
## Description
Brief description of changes made.

## Type of Change
- [ ] Bug fix (non-breaking change that fixes an issue)
- [ ] New feature (non-breaking change that adds functionality)
- [ ] Breaking change (fix or feature that would cause existing functionality to not work as expected)
- [ ] Performance improvement
- [ ] Documentation update

## Testing
- [ ] Unit tests added/updated
- [ ] Integration tests added/updated
- [ ] Benchmarks run and performance maintained/improved
- [ ] Manual testing completed

## Checklist
- [ ] Code follows project style guidelines
- [ ] Self-review completed
- [ ] Code is commented, particularly in hard-to-understand areas
- [ ] Documentation updated
- [ ] No new warnings introduced
```

## Debugging and Profiling

### Debugging Tools

**GDB (Linux/macOS):**
```bash
# Build with debug symbols
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Run with GDB
gdb ./slonana_validator
(gdb) run --ledger-path /tmp/test_ledger
(gdb) bt  # Backtrace on crash
(gdb) info locals  # Local variables
(gdb) print variable_name  # Inspect variables
```

**AddressSanitizer:**
```bash
# Build with ASan
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
make

# Run tests
./slonana_tests
# ASan will report memory errors automatically
```

**Valgrind:**
```bash
# Check for memory leaks
valgrind --tool=memcheck --leak-check=full ./slonana_tests

# Profile cache performance
valgrind --tool=cachegrind ./slonana_validator

# Check for race conditions
valgrind --tool=helgrind ./slonana_tests
```

### Performance Profiling

**perf (Linux):**
```bash
# CPU profiling
perf record -g ./slonana_benchmarks
perf report

# Memory access patterns
perf record -e cache-misses,cache-references ./slonana_validator
perf report

# System-wide profiling
sudo perf record -a -g -- sleep 10
```

**Instruments (macOS):**
```bash
# Launch with Instruments
instruments -t "Time Profiler" ./slonana_validator

# Memory profiling
instruments -t "Allocations" ./slonana_validator
```

**Custom Profiling:**
```cpp
// Built-in profiling macros
#include "common/profiler.h"

void expensive_function() {
    PROFILE_SCOPE("expensive_function");
    
    {
        PROFILE_SCOPE("cpu_intensive_part");
        // CPU-intensive code
    }
    
    {
        PROFILE_SCOPE("memory_intensive_part");
        // Memory-intensive code
    }
}

// Generate profiling report
PROFILE_DUMP("profile_report.json");
```

## Best Practices

### Performance Guidelines

1. **Memory Management:**
   - Use RAII for resource management
   - Prefer stack allocation over heap when possible
   - Use move semantics for expensive-to-copy objects
   - Implement custom allocators for hot paths

2. **Concurrency:**
   - Use lock-free algorithms where possible
   - Minimize lock contention with fine-grained locking
   - Use thread-local storage for per-thread data
   - Prefer atomic operations over mutexes for simple operations

3. **CPU Optimization:**
   - Use const and constexpr liberally
   - Enable compiler optimizations (-O3, LTO)
   - Use SIMD intrinsics for vectorizable operations
   - Profile before optimizing

4. **Modern C++ Standards:**
   - Follow [CODE_STYLE.md](CODE_STYLE.md) for modern C++ patterns
   - Use smart pointers instead of raw pointers
   - Apply `noexcept` for non-throwing functions
   - Use `constexpr` for compile-time computation
   - Prefer `auto` for complex template types, explicit types for clarity
   - Use `override` and `final` specifiers for virtual functions
   - Use standard algorithms over manual loops where appropriate

### Code Quality and Style

**📖 Documentation Standards:**
All code must follow the comprehensive documentation standards defined in [CODE_STYLE.md](CODE_STYLE.md), including:
- Doxygen-compatible comments for all public APIs
- Detailed function documentation with parameters, return values, and examples  
- Class-level documentation explaining purpose and usage patterns
- Thread safety and exception safety guarantees
- Performance implications and complexity notes

1. **Error Handling:**
   - Use Result<T> type for recoverable errors
   - Use exceptions only for exceptional conditions
   - Validate input parameters
   - Provide meaningful error messages

2. **Testing:**
   - Write tests before implementing features (TDD)
   - Aim for >90% code coverage
   - Include edge cases and error conditions
   - Use property-based testing for complex algorithms

3. **Documentation:**
   - Follow Doxygen standards as defined in [CODE_STYLE.md](CODE_STYLE.md)
   - Document all public APIs with comprehensive comments
   - Include parameter descriptions, return values, and usage examples
   - Explain complex algorithms and data structures
   - Keep documentation in sync with code changes
   - Use inline comments sparingly but effectively to explain "why" not "what"
   - Document thread safety guarantees and exception safety levels
   - Provide performance notes for critical methods

### Security Considerations

1. **Input Validation:**
   - Validate all external inputs
   - Use safe string handling functions
   - Avoid buffer overflows with bounds checking
   - Sanitize data before processing

2. **Cryptography:**
   - Use proven cryptographic libraries
   - Never implement crypto primitives yourself
   - Securely handle private keys and sensitive data
   - Use constant-time operations for crypto

3. **Network Security:**
   - Validate all network messages
   - Implement rate limiting and DDoS protection
   - Use secure communication protocols
   - Handle network failures gracefully

### Deployment

1. **Configuration:**
   - Use environment variables for deployment settings
   - Provide sensible defaults
   - Validate configuration on startup
   - Support configuration reloading

2. **Monitoring:**
   - Log important events and errors
   - Expose metrics for monitoring
   - Implement health checks
   - Use structured logging

3. **Scalability:**
   - Design for horizontal scaling
   - Minimize shared state
   - Use asynchronous processing where possible
   - Plan for graceful degradation

This development guide provides comprehensive information for contributors to build, test, and enhance the Slonana.cpp validator effectively.