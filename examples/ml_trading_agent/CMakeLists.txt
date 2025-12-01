cmake_minimum_required(VERSION 3.16)
project(ml_trading_agent_example VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to slonana root
set(SLONANA_ROOT "${CMAKE_SOURCE_DIR}/../..")

# Include directories
include_directories(${SLONANA_ROOT}/include)

# Link to slonana build directory if available
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
# Deploy and invoke client requires full slonana build
# Build from repo root: cmake -B build && make -C build
# Then build this example
# ============================================================================

# Check if slonana build exists
if(EXISTS "${SLONANA_ROOT}/build/libslonana_svm.a" OR EXISTS "${SLONANA_ROOT}/build/CMakeCache.txt")
    add_executable(deploy_and_invoke
        deploy_and_invoke.cpp
    )
    
    # Try to link against slonana libraries
    target_link_libraries(deploy_and_invoke
        slonana_svm
        slonana_common
        ssl
        crypto
        pthread
    )
    
    message(STATUS "deploy_and_invoke target enabled (slonana build found)")
else()
    message(WARNING "Slonana not built - deploy_and_invoke will be skipped. Build slonana first: cmake -B build && make -C build")
endif()

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

install(TARGETS ml_trading_agent_test
    RUNTIME DESTINATION bin
)

install(FILES
    README.md
    train_model.py
    DESTINATION share/ml_trading_agent
)
