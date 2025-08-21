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
    "${SRC_DIR}/staking/*.cpp"
    "${SRC_DIR}/svm/*.cpp"
    "${SRC_DIR}/common/*.cpp"
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

# Add individual test targets
add_test(NAME common_tests COMMAND slonana_common_tests)
add_test(NAME rpc_comprehensive_tests COMMAND slonana_rpc_tests)

# Comprehensive benchmarking suite
add_executable(slonana_benchmarks 
    "${CMAKE_SOURCE_DIR}/tests/test_framework.h"
    "${CMAKE_SOURCE_DIR}/tests/test_benchmarks_simple.cpp"
)
target_link_libraries(slonana_benchmarks slonana_core)
target_include_directories(slonana_benchmarks PRIVATE "${CMAKE_SOURCE_DIR}/tests")

# Add benchmark test (optional - can be run separately)
add_test(NAME benchmark_suite COMMAND slonana_benchmarks)

# Install targets
install(TARGETS slonana_validator DESTINATION bin)
install(TARGETS slonana_benchmarks DESTINATION bin)