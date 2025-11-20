cmake_minimum_required(VERSION 3.16)
project(slonana_validator VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags for performance and debugging
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Configuration options
option(ENABLE_E2E_TESTING "Enable end-to-end testing features" OFF)
option(ENABLE_PERFORMANCE_MONITORING "Enable performance monitoring features" OFF)
option(ENABLE_COMPREHENSIVE_TESTING "Enable comprehensive testing features" OFF)
option(ENABLE_CHAOS_TESTING "Enable chaos testing features" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer for race detection" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer for memory error detection" OFF)

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
    "${SRC_DIR}/banking/*.cpp"
    "${SRC_DIR}/storage/*.cpp"
    "${SRC_DIR}/slonana_validator.cpp"
    "${SRC_DIR}/enhanced_validator_identity.cpp"
)

# Create core library
add_library(slonana_core STATIC ${CORE_SOURCES})
target_include_directories(slonana_core PUBLIC ${INCLUDE_DIR})

# Check for nlohmann_json early and add system includes if needed
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    # Check if system headers are available
    if(EXISTS "/usr/include/nlohmann/json.hpp")
        message(STATUS "Using system nlohmann_json headers from /usr/include")
        target_include_directories(slonana_core SYSTEM PUBLIC /usr/include)
    endif()
endif()

# Sanitizer configuration
if(ENABLE_TSAN)
    target_compile_options(slonana_core PUBLIC -fsanitize=thread -fno-omit-frame-pointer -g)
    target_link_options(slonana_core PUBLIC -fsanitize=thread)
    message(STATUS "ThreadSanitizer enabled for slonana_core")
endif()

if(ENABLE_ASAN)
    target_compile_options(slonana_core PUBLIC -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(slonana_core PUBLIC -fsanitize=address)
    message(STATUS "AddressSanitizer enabled for slonana_core")
endif()
# Apply configuration-based compile definitions
if(ENABLE_E2E_TESTING)
    target_compile_definitions(slonana_core PUBLIC ENABLE_E2E_TESTING)
    message(STATUS "E2E testing features enabled")
endif()

if(ENABLE_PERFORMANCE_MONITORING)
    target_compile_definitions(slonana_core PUBLIC ENABLE_PERFORMANCE_MONITORING)
    message(STATUS "Performance monitoring features enabled")
endif()

if(ENABLE_COMPREHENSIVE_TESTING)
    target_compile_definitions(slonana_core PUBLIC ENABLE_COMPREHENSIVE_TESTING)
    message(STATUS "Comprehensive testing features enabled")
endif()

if(ENABLE_CHAOS_TESTING)
    target_compile_definitions(slonana_core PUBLIC ENABLE_CHAOS_TESTING)
    message(STATUS "Chaos testing features enabled")
endif()

# Main executable
add_executable(slonana_validator ${SRC_DIR}/main.cpp)
target_link_libraries(slonana_validator slonana_core)

# Enable testing
enable_testing()

# Fetch GoogleTest for unit testing
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip
)
# Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

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
    "${CMAKE_SOURCE_DIR}/tests/test_transaction_processing.cpp"
    "${CMAKE_SOURCE_DIR}/tests/test_enhanced_consensus.cpp"
    "${CMAKE_SOURCE_DIR}/tests/test_performance_stress.cpp"
    "${CMAKE_SOURCE_DIR}/tests/test_bpf_runtime.cpp"
    "${CMAKE_SOURCE_DIR}/tests/test_consensus_mechanism.cpp"
    # New comprehensive test suites
    "${CMAKE_SOURCE_DIR}/tests/test_bpf_runtime_comprehensive.cpp"
    "${CMAKE_SOURCE_DIR}/tests/test_consensus_mechanism_comprehensive.cpp"
    # SVM compatibility tests
    "${CMAKE_SOURCE_DIR}/tests/test_svm_compatibility.cpp"
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

# Concurrency stress test
add_executable(slonana_concurrency_stress_test 
    "${CMAKE_SOURCE_DIR}/tests/concurrency_stress_test.cpp"
)
target_link_libraries(slonana_concurrency_stress_test slonana_core)

# Timing race condition fix validation test
add_executable(slonana_timing_race_test 
    "${CMAKE_SOURCE_DIR}/tests/test_timing_race_fix.cpp"
)
target_link_libraries(slonana_timing_race_test slonana_core)

# Shutdown race condition test
add_executable(slonana_shutdown_race_test 
    "${CMAKE_SOURCE_DIR}/tests/test_shutdown_race.cpp"
)
target_link_libraries(slonana_shutdown_race_test slonana_core)

# Find OpenSSL for PoH cryptographic operations
find_package(OpenSSL REQUIRED)
target_link_libraries(slonana_core OpenSSL::SSL OpenSSL::Crypto sodium)

# Link nlohmann_json if found as package
find_package(nlohmann_json 3.2.0 QUIET)
if(nlohmann_json_FOUND)
    target_link_libraries(slonana_core nlohmann_json::nlohmann_json)
endif()

target_link_libraries(slonana_proof_of_history_tests OpenSSL::SSL OpenSSL::Crypto)

# Add individual test targets
add_test(NAME common_tests COMMAND slonana_common_tests)
add_test(NAME rpc_comprehensive_tests COMMAND slonana_rpc_tests)
add_test(NAME wallet_tests COMMAND slonana_wallet_tests)
add_test(NAME monitoring_tests COMMAND slonana_monitoring_tests)
add_test(NAME consensus_timing_tests COMMAND slonana_consensus_timing_tests)
add_test(NAME proof_of_history_tests COMMAND slonana_proof_of_history_tests)

# Agave Phase 1 test suite
add_executable(slonana_agave_phase1_tests 
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_agave_phase1_tasks.cpp"
)
target_link_libraries(slonana_agave_phase1_tests slonana_core OpenSSL::SSL OpenSSL::Crypto)
target_include_directories(slonana_agave_phase1_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_agave_phase1_tests PRIVATE STANDALONE_AGAVE_TESTS)
add_test(NAME agave_phase1_tests COMMAND slonana_agave_phase1_tests)

# Tower BFT test suite (Agave Phase 1)
add_executable(slonana_tower_bft_tests
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_tower_bft.cpp"
)
target_link_libraries(slonana_tower_bft_tests slonana_core)
target_include_directories(slonana_tower_bft_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_tower_bft_tests PRIVATE STANDALONE_TOWER_BFT_TESTS)
add_test(NAME tower_bft_tests COMMAND slonana_tower_bft_tests)

# Turbine Protocol test suite (Agave Phase 1)
add_executable(slonana_turbine_protocol_tests
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_turbine_protocol.cpp"
)
target_link_libraries(slonana_turbine_protocol_tests slonana_core)
target_include_directories(slonana_turbine_protocol_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_turbine_protocol_tests PRIVATE STANDALONE_TURBINE_PROTOCOL_TESTS)
add_test(NAME turbine_protocol_tests COMMAND slonana_turbine_protocol_tests)

# Enhanced test suites (if comprehensive testing is enabled)
if(ENABLE_COMPREHENSIVE_TESTING)
    message(STATUS "Building comprehensive test suites...")
    
    # Transaction Processing Test Suite
    add_executable(slonana_transaction_processing_tests 
        "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
        "${CMAKE_SOURCE_DIR}/tests/test_transaction_processing.cpp"
    )
    target_link_libraries(slonana_transaction_processing_tests slonana_core)
    target_include_directories(slonana_transaction_processing_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
    target_compile_definitions(slonana_transaction_processing_tests PRIVATE STANDALONE_TRANSACTION_TESTS)
    
    # Enhanced Consensus Test Suite
    add_executable(slonana_enhanced_consensus_tests 
        "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
        "${CMAKE_SOURCE_DIR}/tests/test_enhanced_consensus.cpp"
    )
    target_link_libraries(slonana_enhanced_consensus_tests slonana_core)
    target_include_directories(slonana_enhanced_consensus_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
    target_compile_definitions(slonana_enhanced_consensus_tests PRIVATE STANDALONE_CONSENSUS_TESTS)
    
    # Performance and Stress Test Suite
    add_executable(slonana_performance_stress_tests 
        "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
        "${CMAKE_SOURCE_DIR}/tests/test_performance_stress.cpp"
    )
    target_link_libraries(slonana_performance_stress_tests slonana_core)
    target_include_directories(slonana_performance_stress_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
    target_compile_definitions(slonana_performance_stress_tests PRIVATE STANDALONE_PERFORMANCE_TESTS)
    
    # BPF Runtime Test Suite
    add_executable(slonana_bpf_runtime_tests 
        "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
        "${CMAKE_SOURCE_DIR}/tests/test_bpf_runtime.cpp"
    )
    target_link_libraries(slonana_bpf_runtime_tests slonana_core)
    target_include_directories(slonana_bpf_runtime_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
    target_compile_definitions(slonana_bpf_runtime_tests PRIVATE STANDALONE_BPF_TESTS)
    
    # Consensus Mechanism Test Suite
    add_executable(slonana_consensus_mechanism_tests 
        "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
        "${CMAKE_SOURCE_DIR}/tests/test_consensus_mechanism.cpp"
    )
    target_link_libraries(slonana_consensus_mechanism_tests slonana_core)
    target_include_directories(slonana_consensus_mechanism_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
    target_compile_definitions(slonana_consensus_mechanism_tests PRIVATE STANDALONE_CONSENSUS_TESTS)
    
    # Add comprehensive test targets
    add_test(NAME transaction_processing_tests COMMAND slonana_transaction_processing_tests)
    add_test(NAME enhanced_consensus_tests COMMAND slonana_enhanced_consensus_tests)
    add_test(NAME performance_stress_tests COMMAND slonana_performance_stress_tests)
    add_test(NAME bpf_runtime_tests COMMAND slonana_bpf_runtime_tests)
    add_test(NAME consensus_mechanism_tests COMMAND slonana_consensus_mechanism_tests)
    
    message(STATUS "Comprehensive test suites configured")
endif()

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

# Extended SPL program test suite
add_executable(slonana_spl_extended_tests 
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_spl_extended.cpp"
)
target_link_libraries(slonana_spl_extended_tests slonana_core)
target_include_directories(slonana_spl_extended_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_spl_extended_tests PRIVATE STANDALONE_SPL_EXTENDED_TESTS)
add_test(NAME spl_extended_tests COMMAND slonana_spl_extended_tests)

# WebSocket test suite
add_executable(slonana_websocket_tests
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_websocket.cpp"
)
target_link_libraries(slonana_websocket_tests slonana_core)
target_include_directories(slonana_websocket_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_websocket_tests PRIVATE STANDALONE_WEBSOCKET_TESTS)
add_test(NAME websocket_tests COMMAND slonana_websocket_tests)

# Cluster connection test suite
add_executable(slonana_cluster_tests
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_cluster_connection.cpp"
)
target_link_libraries(slonana_cluster_tests slonana_core)
target_include_directories(slonana_cluster_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_cluster_tests PRIVATE STANDALONE_CLUSTER_TESTS)
add_test(NAME cluster_tests COMMAND slonana_cluster_tests)

# Snapshot system test suite
add_executable(slonana_snapshot_tests
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_snapshot.cpp"
    "${CMAKE_SOURCE_DIR}/tests/test_snapshot_bootstrap.cpp"
)
target_link_libraries(slonana_snapshot_tests slonana_core)
target_include_directories(slonana_snapshot_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_snapshot_tests PRIVATE STANDALONE_SNAPSHOT_TESTS)
add_test(NAME snapshot_tests COMMAND slonana_snapshot_tests)

# Hardware wallet test suite
add_executable(slonana_hardware_wallet_tests
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_hardware_wallet.cpp"
)
target_link_libraries(slonana_hardware_wallet_tests slonana_core)
target_include_directories(slonana_hardware_wallet_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_hardware_wallet_tests PRIVATE STANDALONE_HARDWARE_WALLET_TESTS)
add_test(NAME hardware_wallet_tests COMMAND slonana_hardware_wallet_tests)

# Key management test suite
add_executable(slonana_key_management_tests
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_key_management.cpp"
)
target_link_libraries(slonana_key_management_tests slonana_core OpenSSL::SSL OpenSSL::Crypto)
target_include_directories(slonana_key_management_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_key_management_tests PRIVATE STANDALONE_KEY_MANAGEMENT_TESTS)
add_test(NAME key_management_tests COMMAND slonana_key_management_tests)

# Fault tolerance test suite
add_executable(slonana_fault_tolerance_tests
    "${CMAKE_SOURCE_DIR}/tests/test_fault_tolerance.cpp"
)
target_link_libraries(slonana_fault_tolerance_tests slonana_core)
target_include_directories(slonana_fault_tolerance_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_fault_tolerance_tests PRIVATE STANDALONE_FAULT_TOLERANCE_TESTS)
add_test(NAME fault_tolerance_tests COMMAND slonana_fault_tolerance_tests)

# Fault tolerance integration test suite
add_executable(slonana_fault_tolerance_integration_tests
    "${CMAKE_SOURCE_DIR}/tests/test_fault_tolerance_integration.cpp"
)
target_link_libraries(slonana_fault_tolerance_integration_tests slonana_core)
target_include_directories(slonana_fault_tolerance_integration_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_fault_tolerance_integration_tests PRIVATE STANDALONE_FAULT_TOLERANCE_INTEGRATION_TESTS)
add_test(NAME fault_tolerance_integration_tests COMMAND slonana_fault_tolerance_integration_tests)

# Bug fix validation test suite
add_executable(slonana_bug_fix_tests
    "${CMAKE_SOURCE_DIR}/tests/test_bug_fixes.cpp"
)
target_link_libraries(slonana_bug_fix_tests slonana_core)
target_include_directories(slonana_bug_fix_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_bug_fix_tests PRIVATE STANDALONE_BUG_FIX_TESTS)
add_test(NAME bug_fix_validation_tests COMMAND slonana_bug_fix_tests)

# Logging and alerting test suite
add_executable(slonana_logging_alerting_tests
    "${CMAKE_SOURCE_DIR}/tests/test_logging_alerting.cpp"
)
target_link_libraries(slonana_logging_alerting_tests slonana_core)
target_include_directories(slonana_logging_alerting_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_logging_alerting_tests PRIVATE STANDALONE_LOGGING_ALERTING_TESTS)
add_test(NAME logging_alerting_tests COMMAND slonana_logging_alerting_tests)

# Secure messaging tests
add_executable(slonana_secure_messaging_tests 
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_secure_messaging.cpp"
)
target_link_libraries(slonana_secure_messaging_tests slonana_core OpenSSL::SSL OpenSSL::Crypto)
target_include_directories(slonana_secure_messaging_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_secure_messaging_tests PRIVATE STANDALONE_SECURE_MESSAGING_TESTS)
add_test(NAME secure_messaging_tests COMMAND slonana_secure_messaging_tests)

# Secure networking integration tests
add_executable(slonana_secure_networking_integration_tests 
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_secure_networking_integration.cpp"
)
target_link_libraries(slonana_secure_networking_integration_tests slonana_core OpenSSL::SSL OpenSSL::Crypto)
target_include_directories(slonana_secure_networking_integration_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(slonana_secure_networking_integration_tests PRIVATE STANDALONE_SECURE_NETWORKING_TESTS)
add_test(NAME secure_networking_integration_tests COMMAND slonana_secure_networking_integration_tests)

# Gossip protocol tests
add_executable(slonana_gossip_protocol_tests
    "${CMAKE_SOURCE_DIR}/tests/test_gossip_protocol.cpp"
)
target_link_libraries(slonana_gossip_protocol_tests slonana_core)
target_include_directories(slonana_gossip_protocol_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
add_test(NAME gossip_protocol_tests COMMAND slonana_gossip_protocol_tests)

# Fee market tests
add_executable(slonana_fee_market_tests
    "${CMAKE_SOURCE_DIR}/tests/test_fee_market.cpp"
)
target_link_libraries(slonana_fee_market_tests slonana_core)
target_include_directories(slonana_fee_market_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
add_test(NAME fee_market_tests COMMAND slonana_fee_market_tests)

# MEV protection tests
add_executable(slonana_mev_protection_tests
    "${CMAKE_SOURCE_DIR}/tests/test_mev_protection.cpp"
)
target_link_libraries(slonana_mev_protection_tests slonana_core)
target_include_directories(slonana_mev_protection_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
add_test(NAME mev_protection_tests COMMAND slonana_mev_protection_tests)

# CPI depth tracking tests
add_executable(slonana_cpi_depth_tests
    "${CMAKE_SOURCE_DIR}/tests/test_cpi_depth.cpp"
)
target_link_libraries(slonana_cpi_depth_tests slonana_core)
target_include_directories(slonana_cpi_depth_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
add_test(NAME cpi_depth_tests COMMAND slonana_cpi_depth_tests)

# Banking stage integration tests
add_executable(slonana_banking_integration_tests
    "${CMAKE_SOURCE_DIR}/tests/test_banking_integration.cpp"
)
target_link_libraries(slonana_banking_integration_tests slonana_core)
target_include_directories(slonana_banking_integration_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
add_test(NAME banking_integration_tests COMMAND slonana_banking_integration_tests)

# Networking enhancements tests
add_executable(slonana_networking_enhancements_tests
    "${CMAKE_SOURCE_DIR}/tests/test_networking_enhancements.cpp"
)
target_link_libraries(slonana_networking_enhancements_tests slonana_core)
target_include_directories(slonana_networking_enhancements_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
add_test(NAME networking_enhancements_tests COMMAND slonana_networking_enhancements_tests)

# Networking performance benchmarks
add_executable(benchmark_networking
    "${CMAKE_SOURCE_DIR}/tests/benchmark_networking.cpp"
)
target_link_libraries(benchmark_networking slonana_core)
target_include_directories(benchmark_networking PRIVATE "${CMAKE_SOURCE_DIR}/tests")

# BPF Runtime performance benchmarks
add_executable(benchmark_bpf_runtime
    "${CMAKE_SOURCE_DIR}/tests/benchmark_bpf_runtime.cpp"
)
target_link_libraries(benchmark_bpf_runtime slonana_core)
target_include_directories(benchmark_bpf_runtime PRIVATE "${CMAKE_SOURCE_DIR}/tests")

# Lock-free BPF Runtime benchmarks with SIMD optimizations
add_executable(benchmark_lockfree_bpf
    "${CMAKE_SOURCE_DIR}/tests/benchmark_lockfree_bpf.cpp"
)
target_link_libraries(benchmark_lockfree_bpf slonana_core pthread)
target_include_directories(benchmark_lockfree_bpf PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_options(benchmark_lockfree_bpf PRIVATE -mavx2 -march=native)

# Ultra-optimized BPF Runtime benchmarks (branchless, multi-level cache, prefetch)
add_executable(benchmark_ultra_bpf
    "${CMAKE_SOURCE_DIR}/tests/benchmark_ultra_bpf.cpp"
)
target_link_libraries(benchmark_ultra_bpf slonana_core pthread)
target_include_directories(benchmark_ultra_bpf PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_options(benchmark_ultra_bpf PRIVATE -O3 -mavx2 -march=native -fno-strict-aliasing)

# Assembly hand-tuned BPF Runtime benchmarks (ultimate performance)
add_executable(benchmark_asm_bpf
    "${CMAKE_SOURCE_DIR}/tests/benchmark_asm_bpf.cpp"
)
target_link_libraries(benchmark_asm_bpf slonana_core pthread)
target_include_directories(benchmark_asm_bpf PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_compile_options(benchmark_asm_bpf PRIVATE -O3 -mavx2 -march=native -fno-strict-aliasing)

# SVM Syscalls Tests - Cryptographic Operations
add_executable(slonana_syscalls_crypto_tests
    "${CMAKE_SOURCE_DIR}/tests/test_syscalls_crypto.cpp"
)
target_link_libraries(slonana_syscalls_crypto_tests slonana_core)
target_include_directories(slonana_syscalls_crypto_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
add_test(NAME syscalls_crypto_tests COMMAND slonana_syscalls_crypto_tests)

# SVM Syscalls Tests - Sysvar Access
add_executable(slonana_syscalls_sysvar_tests
    "${CMAKE_SOURCE_DIR}/tests/test_syscalls_sysvar.cpp"
)
target_link_libraries(slonana_syscalls_sysvar_tests slonana_core)
target_include_directories(slonana_syscalls_sysvar_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
add_test(NAME syscalls_sysvar_tests COMMAND slonana_syscalls_sysvar_tests)

# Enhanced BPF Runtime Tests
add_executable(slonana_bpf_enhanced_tests
    "${CMAKE_SOURCE_DIR}/tests/test_bpf_runtime_enhanced.cpp"
)
target_link_libraries(slonana_bpf_enhanced_tests slonana_core)
target_include_directories(slonana_bpf_enhanced_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
add_test(NAME bpf_enhanced_tests COMMAND slonana_bpf_enhanced_tests)

# Extended Test Suites for Improved Coverage

# SVM Syscalls Extended Tests - Cryptographic Operations (Edge Cases)
add_executable(slonana_syscalls_crypto_extended_tests
    "${CMAKE_SOURCE_DIR}/tests/test_syscalls_crypto_extended.cpp"
)
target_link_libraries(slonana_syscalls_crypto_extended_tests slonana_core)
target_include_directories(slonana_syscalls_crypto_extended_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
add_test(NAME syscalls_crypto_extended_tests COMMAND slonana_syscalls_crypto_extended_tests)

# SVM Syscalls Extended Tests - Sysvar Access (Edge Cases)
add_executable(slonana_syscalls_sysvar_extended_tests
    "${CMAKE_SOURCE_DIR}/tests/test_syscalls_sysvar_extended.cpp"
)
target_link_libraries(slonana_syscalls_sysvar_extended_tests slonana_core gtest_main)
target_include_directories(slonana_syscalls_sysvar_extended_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
add_test(NAME syscalls_sysvar_extended_tests COMMAND slonana_syscalls_sysvar_extended_tests)

# Enhanced BPF Runtime Extended Tests (Stress, Concurrency, Integration)
add_executable(slonana_bpf_runtime_extended_tests
    "${CMAKE_SOURCE_DIR}/tests/test_bpf_runtime_extended.cpp"
)
target_link_libraries(slonana_bpf_runtime_extended_tests slonana_core gtest_main)
target_include_directories(slonana_bpf_runtime_extended_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
add_test(NAME bpf_runtime_extended_tests COMMAND slonana_bpf_runtime_extended_tests)

# Adversarial Syscall Tests (Bug Discovery)
add_executable(slonana_adversarial_syscalls_tests
    "${CMAKE_SOURCE_DIR}/tests/test_adversarial_syscalls.cpp"
)
target_link_libraries(slonana_adversarial_syscalls_tests slonana_core gtest_main)
target_include_directories(slonana_adversarial_syscalls_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
add_test(NAME adversarial_syscalls_tests COMMAND slonana_adversarial_syscalls_tests)

# Fuzzing BPF Runtime Tests (Bug Discovery)
add_executable(slonana_fuzzing_bpf_runtime_tests
    "${CMAKE_SOURCE_DIR}/tests/test_fuzzing_bpf_runtime.cpp"
)
target_link_libraries(slonana_fuzzing_bpf_runtime_tests slonana_core gtest_main)
target_include_directories(slonana_fuzzing_bpf_runtime_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
add_test(NAME fuzzing_bpf_runtime_tests COMMAND slonana_fuzzing_bpf_runtime_tests)


# Genesis CLI tool
add_executable(slonana-genesis "${SRC_DIR}/genesis/cli_tool.cpp")
target_link_libraries(slonana-genesis slonana_core OpenSSL::SSL OpenSSL::Crypto)

# Key management CLI tool
add_executable(slonana-keygen "${SRC_DIR}/tools/slonana_keygen.cpp")
target_link_libraries(slonana-keygen slonana_core OpenSSL::SSL OpenSSL::Crypto)

# Install targets
install(TARGETS slonana_validator DESTINATION bin)
install(TARGETS slonana_benchmarks DESTINATION bin)
install(TARGETS slonana-genesis DESTINATION bin)
install(TARGETS slonana-keygen DESTINATION bin)