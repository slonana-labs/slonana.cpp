cmake_minimum_required(VERSION 3.16)
project(slonana_validator VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags for performance and debugging
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source directories
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

# Core library sources
file(GLOB_RECURSE CORE_SOURCES
    "${SRC_DIR}/network/*.cpp"
    "${SRC_DIR}/ledger/*.cpp"
    "${SRC_DIR}/validator/*.cpp"
    "${SRC_DIR}/consensus/*.cpp"
    "${SRC_DIR}/staking/*.cpp"
    "${SRC_DIR}/svm/*.cpp"
    "${SRC_DIR}/common/*.cpp"
    "${SRC_DIR}/wallet/*.cpp"
    "${SRC_DIR}/monitoring/*.cpp"
    "${SRC_DIR}/security/*.cpp"
    "${SRC_DIR}/cluster/*.cpp"
    "${SRC_DIR}/genesis/*.cpp"
    "${SRC_DIR}/slonana_validator.cpp"
)

# Create core library
add_library(slonana_core STATIC ${CORE_SOURCES})
target_include_directories(slonana_core PUBLIC ${INCLUDE_DIR})

# Main executable
add_executable(slonana_validator ${SRC_DIR}/main.cpp)
target_link_libraries(slonana_validator slonana_core)

# Enable testing
enable_testing()

# Comprehensive test suite
set(COMPREHENSIVE_TEST_SOURCES
    "${CMAKE_SOURCE_DIR}/tests/test_comprehensive.cpp"
    "${CMAKE_SOURCE_DIR}/tests/test_common.cpp"
    "${CMAKE_SOURCE_DIR}/tests/test_ledger.cpp"
    "${CMAKE_SOURCE_DIR}/tests/test_network.cpp"
    "${CMAKE_SOURCE_DIR}/tests/test_consensus.cpp"
    "${CMAKE_SOURCE_DIR}/tests/test_rpc_comprehensive.cpp"
    "${CMAKE_SOURCE_DIR}/tests/test_integration.cpp"
    "${CMAKE_SOURCE_DIR}/tests/test_wallet.cpp"
    "${CMAKE_SOURCE_DIR}/tests/test_monitoring.cpp"
)

# Original simple test suite (for backwards compatibility)
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_validator.cpp")
    add_executable(slonana_tests "${CMAKE_SOURCE_DIR}/tests/test_validator.cpp")
    target_link_libraries(slonana_tests slonana_core)
    add_test(NAME validator_basic_tests COMMAND slonana_tests)
endif()

# Comprehensive test suite
add_executable(slonana_comprehensive_tests ${COMPREHENSIVE_TEST_SOURCES})
target_link_libraries(slonana_comprehensive_tests slonana_core)
target_include_directories(slonana_comprehensive_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_comprehensive_tests PRIVATE COMPREHENSIVE_TESTS)
add_test(NAME validator_comprehensive_tests COMMAND slonana_comprehensive_tests)

# Individual test modules (for granular testing)
add_executable(slonana_common_tests 
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_common.cpp"
)
target_link_libraries(slonana_common_tests slonana_core)
target_include_directories(slonana_common_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")

add_executable(slonana_rpc_tests 
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_rpc_comprehensive.cpp"
)
target_link_libraries(slonana_rpc_tests slonana_core)
target_include_directories(slonana_rpc_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")

# Phase 2 component tests
add_executable(slonana_wallet_tests 
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_wallet.cpp"
)
target_link_libraries(slonana_wallet_tests slonana_core)
target_include_directories(slonana_wallet_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_wallet_tests PRIVATE STANDALONE_WALLET_TESTS)

add_executable(slonana_monitoring_tests 
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_monitoring.cpp"
)
target_link_libraries(slonana_monitoring_tests slonana_core)
target_include_directories(slonana_monitoring_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_monitoring_tests PRIVATE STANDALONE_MONITORING_TESTS)

add_executable(slonana_consensus_timing_tests 
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_consensus_timing.cpp"
)
target_link_libraries(slonana_consensus_timing_tests slonana_core)
target_include_directories(slonana_consensus_timing_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_consensus_timing_tests PRIVATE STANDALONE_CONSENSUS_TIMING_TESTS)

add_executable(slonana_proof_of_history_tests 
    "${CMAKE_SOURCE_DIR}/tests/test_proof_of_history.cpp"
)
target_link_libraries(slonana_proof_of_history_tests slonana_core)
target_include_directories(slonana_proof_of_history_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")

# Find OpenSSL for PoH cryptographic operations
find_package(OpenSSL REQUIRED)
target_link_libraries(slonana_core OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(slonana_proof_of_history_tests OpenSSL::SSL OpenSSL::Crypto)

# Add individual test targets
add_test(NAME common_tests COMMAND slonana_common_tests)
add_test(NAME rpc_comprehensive_tests COMMAND slonana_rpc_tests)
add_test(NAME wallet_tests COMMAND slonana_wallet_tests)
add_test(NAME monitoring_tests COMMAND slonana_monitoring_tests)
add_test(NAME consensus_timing_tests COMMAND slonana_consensus_timing_tests)
add_test(NAME proof_of_history_tests COMMAND slonana_proof_of_history_tests)

# Genesis and mainnet tests
add_executable(slonana_genesis_tests 
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_genesis.cpp"
)
target_link_libraries(slonana_genesis_tests slonana_core OpenSSL::SSL OpenSSL::Crypto)
target_include_directories(slonana_genesis_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_genesis_tests PRIVATE STANDALONE_GENESIS_TESTS)
add_test(NAME genesis_tests COMMAND slonana_genesis_tests)

# Comprehensive benchmarking suite
add_executable(slonana_benchmarks 
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_benchmarks_simple.cpp"
)
target_link_libraries(slonana_benchmarks slonana_core)
target_include_directories(slonana_benchmarks PRIVATE "${CMAKE_SOURCE_DIR}/tests")

# Add benchmark test (optional - can be run separately)
add_test(NAME benchmark_suite COMMAND slonana_benchmarks)

# Enhanced SVM Engine test suite
add_executable(slonana_enhanced_svm_tests 
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_svm_enhanced.cpp"
)
target_link_libraries(slonana_enhanced_svm_tests slonana_core)
target_include_directories(slonana_enhanced_svm_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_enhanced_svm_tests PRIVATE STANDALONE_SVM_ENHANCED_TESTS)
add_test(NAME enhanced_svm_tests COMMAND slonana_enhanced_svm_tests)

# Genesis CLI tool
add_executable(slonana-genesis "${SRC_DIR}/genesis/cli_tool.cpp")
target_link_libraries(slonana-genesis slonana_core OpenSSL::SSL OpenSSL::Crypto)

# Install targets
install(TARGETS slonana_validator DESTINATION bin)
install(TARGETS slonana_benchmarks DESTINATION bin)
install(TARGETS slonana-genesis DESTINATION bin)