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

# Find and add tests
file(GLOB TEST_SOURCES "${CMAKE_SOURCE_DIR}/tests/*.cpp")
if(TEST_SOURCES)
    add_executable(slonana_tests ${TEST_SOURCES})
    target_link_libraries(slonana_tests slonana_core)
    add_test(NAME validator_tests COMMAND slonana_tests)
endif()

# Install targets
install(TARGETS slonana_validator DESTINATION bin)