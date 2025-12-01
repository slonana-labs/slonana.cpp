cmake_minimum_required(VERSION 3.16)
project(ml_trading_agent_example VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to slonana root
set(SLONANA_ROOT "${CMAKE_SOURCE_DIR}/../..")

# Include directories
include_directories(${SLONANA_ROOT}/include)

# Link to slonana core library
# Assumes slonana has been built in ${SLONANA_ROOT}/build
link_directories(${SLONANA_ROOT}/build)

# ============================================================================
# Native test binary for ml_trading_agent
# ============================================================================
add_executable(ml_trading_agent_test
    ml_trading_agent.cpp
)

# Don't define __BPF__ for native compilation
target_compile_definitions(ml_trading_agent_test PRIVATE)

# ============================================================================
# Deploy and invoke client
# ============================================================================
add_executable(deploy_and_invoke
    deploy_and_invoke.cpp
)

target_link_libraries(deploy_and_invoke
    slonana_core
    pthread
)

# ============================================================================
# sBPF compilation (requires clang with BPF target)
# ============================================================================

# Check if clang supports BPF target
find_program(CLANG_EXECUTABLE clang)

if(CLANG_EXECUTABLE)
    # Custom target for sBPF compilation
    add_custom_target(ml_trading_agent_sbpf
        COMMAND ${CLANG_EXECUTABLE}
            -target bpf
            -O2
            -D__BPF__
            -I${SLONANA_ROOT}/include
            -c ${CMAKE_SOURCE_DIR}/ml_trading_agent.cpp
            -o ml_trading_agent.o
        COMMAND llvm-objcopy
            -O binary
            ml_trading_agent.o
            ml_trading_agent.so
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Compiling ML Trading Agent to sBPF"
        SOURCES ml_trading_agent.cpp
    )
    
    message(STATUS "sBPF compilation enabled (use: make ml_trading_agent_sbpf)")
else()
    message(WARNING "clang not found - sBPF compilation disabled")
endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS deploy_and_invoke ml_trading_agent_test
    RUNTIME DESTINATION bin
)

install(FILES
    README.md
    train_model.py
    DESTINATION share/ml_trading_agent
)
