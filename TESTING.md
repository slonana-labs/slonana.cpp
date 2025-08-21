# Slonana C++ Validator - Testing Guide

## Overview

This document describes the comprehensive testing framework for the Slonana C++ Validator implementation.

## Test Structure

The test suite is organized into multiple layers:

### 1. Unit Tests
- **Common Types** (`test_common.cpp`): Tests for basic types, configurations, and utilities
- **Ledger Operations** (`test_ledger.cpp`): Block operations, serialization, and ledger management
- **Network Components** (`test_network.cpp`): RPC server and gossip protocol functionality
- **Consensus Components** (`test_consensus.cpp`): Staking manager and SVM execution engine

### 2. RPC API Tests
- **Comprehensive RPC** (`test_rpc_comprehensive.cpp`): Tests all 35+ Solana RPC methods
- **Error Handling**: JSON-RPC 2.0 compliance and error scenarios
- **Performance**: Batch processing and concurrent request handling

### 3. Integration Tests
- **Full Validator Lifecycle** (`test_integration.cpp`): End-to-end validator operations
- **Component Integration**: Cross-component interactions and data flow
- **Error Recovery**: Fault tolerance and graceful error handling
- **Performance Stress**: High-load scenarios and resource utilization

## Running Tests

### Prerequisites
```bash
# Install dependencies
sudo apt-get install cmake build-essential

# Build the project
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Test Execution

#### Basic Test Suite (Original)
```bash
./slonana_tests
```

#### Comprehensive Test Suite (All Components)
```bash
./slonana_comprehensive_tests
```

#### Individual Test Modules
```bash
# Common types and utilities
./slonana_common_tests

# RPC API comprehensive testing
./slonana_rpc_tests
```

#### CTest Integration
```bash
# Run all tests through CTest
ctest --output-on-failure --verbose

# Run specific test categories
ctest -R "validator_comprehensive"
ctest -R "common_tests"
ctest -R "rpc_comprehensive"
```

## Test Categories

### Unit Tests (51 tests)
- ✅ Common Types: 10 tests
- ✅ Ledger Operations: 9 tests  
- ✅ Network Components: 17 tests
- ✅ Consensus Components: 15 tests

### RPC API Tests (11 tests covering 35+ methods)
- ✅ Account Methods: getAccountInfo, getBalance, getProgramAccounts, etc.
- ✅ Block Methods: getBlock, getSlot, getBlockHeight, getGenesisHash, etc.
- ✅ Transaction Methods: getTransaction, sendTransaction, simulateTransaction, etc.
- ✅ Network Methods: getVersion, getClusterNodes, getIdentity, getHealth
- ✅ Validator Methods: getVoteAccounts, getEpochInfo, getLeaderSchedule, etc.
- ✅ Staking Methods: getStakeActivation, getInflationGovernor, etc.
- ✅ Utility Methods: getRecentBlockhash, getFeeForMessage, etc.

### Integration Tests (8 tests)
- ✅ Full Validator Lifecycle
- ✅ RPC Integration
- ✅ Block Processing Pipeline
- ✅ Staking Integration
- ✅ SVM Integration
- ✅ Multi-Component Interaction
- ✅ Error Recovery
- ✅ Performance Stress Testing

## Performance Benchmarks

The test suite includes performance benchmarks for:

- **Block Processing**: 50 blocks processed in <100ms
- **RPC Throughput**: 100 requests processed in <50ms
- **Account Operations**: 100 account creations in <200ms
- **Memory Usage**: Tested with Valgrind for leak detection

## Test Framework Features

### Enhanced Assertions
```cpp
ASSERT_TRUE(condition)
ASSERT_FALSE(condition)
ASSERT_EQ(expected, actual)
ASSERT_NE(expected, actual)
ASSERT_LT(a, b)
ASSERT_LE(a, b)
ASSERT_GT(a, b)
ASSERT_GE(a, b)
ASSERT_CONTAINS(container, item)
ASSERT_NOT_EMPTY(container)
ASSERT_THROWS(statement, exception_type)
EXPECT_NO_THROW(statement)
```

### Benchmarking Support
```cpp
runner.run_benchmark("Operation Name", benchmark_function, iterations);
```

### Timing and Statistics
- Individual test execution times
- Pass/fail rates
- Detailed failure reporting
- Performance metrics

## Continuous Integration

### GitHub Actions Workflow

The CI pipeline includes:

1. **Multi-Platform Builds**
   - Ubuntu 20.04 and latest
   - GCC and Clang compilers
   - Debug and Release builds

2. **Quality Checks**
   - Code formatting (clang-format)
   - Static analysis (cppcheck, clang-tidy)
   - Memory leak detection (Valgrind)

3. **Security Scanning**
   - Vulnerability scanning (Trivy)
   - SARIF report generation

4. **Performance Testing**
   - Load testing
   - Resource utilization monitoring
   - Performance regression detection

5. **Coverage Analysis**
   - Code coverage reporting
   - Codecov integration

## Test Data Management

Tests use temporary directories for:
- Ledger storage: `/tmp/test_*_ledger`
- Identity files: `/tmp/test_*_identity.json`
- Configuration: Test-specific configs

Cleanup is automated after each test completion.

## Adding New Tests

### Unit Test Example
```cpp
void test_new_feature() {
    // Setup
    auto component = std::make_unique<NewComponent>();
    
    // Execute
    auto result = component->perform_operation();
    
    // Verify
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(expected_value, result.value());
}

// Register in module
runner.run_test("New Feature Test", test_new_feature);
```

### Integration Test Example
```cpp
void test_new_integration() {
    // Setup full validator
    slonana::common::ValidatorConfig config;
    config.ledger_path = "/tmp/test_new_integration";
    
    auto validator = std::make_unique<slonana::SolanaValidator>(config);
    validator->initialize();
    validator->start();
    
    // Test interaction between components
    // ...
    
    validator->stop();
}
```

## Best Practices

1. **Isolation**: Each test should be independent and not affect others
2. **Cleanup**: Always clean up temporary files and resources
3. **Deterministic**: Tests should produce consistent results
4. **Fast**: Unit tests should complete quickly (<1s each)
5. **Descriptive**: Test names should clearly indicate what is being tested
6. **Coverage**: Aim for high code coverage while maintaining test quality

## Troubleshooting

### Common Issues

1. **Port Conflicts**: Tests use specific ports (18899, 18001) - ensure they're available
2. **Temporary Files**: Clean up `/tmp/test_*` directories if tests fail unexpectedly  
3. **Build Issues**: Ensure all dependencies are installed and C++20 is supported
4. **Memory Issues**: Run with Valgrind to detect leaks in Debug builds

### Debug Mode
```bash
# Build with debug symbols
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

# Run with memory checking
valgrind --tool=memcheck --leak-check=full ./slonana_comprehensive_tests
```

## Test Metrics

Current test coverage:
- **70+ individual tests** across all components
- **35+ RPC methods** comprehensively tested
- **8 integration scenarios** validated
- **Performance benchmarks** for critical paths
- **100% pass rate** in CI/CD pipeline

The comprehensive test suite ensures the Solana validator implementation is robust, performant, and production-ready.