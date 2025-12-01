# Makefile for AI Trading Agent
#
# This builds both the sBPF program and the test client.
# 
# Prerequisites:
#   - LLVM/Clang with BPF target support (for sBPF compilation)
#   - slonana built (for test client linking)

# Paths
SLONANA_ROOT := ../..
BUILD_DIR := $(SLONANA_ROOT)/build
INCLUDE_DIR := $(SLONANA_ROOT)/include

# Compilers
CXX := g++
CLANG := clang++
LLC := llc
LLVM_OBJCOPY := llvm-objcopy

# Flags
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
INCLUDES := -I$(INCLUDE_DIR)

# sBPF compilation flags
BPF_CFLAGS := -target bpf -O2 -g -Wall
BPF_CFLAGS += -D__bpf__
BPF_CFLAGS += -Wno-unused-function
BPF_CFLAGS += -fno-stack-protector

# Libraries
LIBS := -L$(BUILD_DIR) -pthread

# Targets
.PHONY: all clean test client program

all: program client

# ============================================================================
# sBPF Program
# ============================================================================

# Compile sBPF program (would need actual BPF toolchain)
# For now, we create a mock .so file
program: ai_trading_agent.so

# Note: Full BPF compilation requires:
# 1. Clang with BPF target
# 2. BPF linker
# 3. llvm-objcopy for binary extraction
#
# The actual compilation would be:
#   $(CLANG) $(BPF_CFLAGS) -c ai_trading_agent.cpp -o ai_trading_agent.o
#   $(LLVM_OBJCOPY) -O binary ai_trading_agent.o ai_trading_agent.so
#
# For this example, we create a placeholder

ai_trading_agent.so: ai_trading_agent.cpp
	@echo "========================================"
	@echo "Building sBPF Program"
	@echo "========================================"
	@echo ""
	@echo "Note: Full sBPF compilation requires LLVM with BPF target."
	@echo "This creates a placeholder .so file for demonstration."
	@echo ""
	@echo "To compile for real sBPF deployment:"
	@echo "  1. Install LLVM with BPF target support"
	@echo "  2. Run: clang++ -target bpf -O2 -c ai_trading_agent.cpp -o ai_trading_agent.o"
	@echo "  3. Run: llvm-objcopy -O binary ai_trading_agent.o ai_trading_agent.so"
	@echo ""
	@# Create a placeholder file
	touch ai_trading_agent.so
	@echo "✓ Created placeholder ai_trading_agent.so"

# ============================================================================
# Test Client
# ============================================================================

client: test_client

test_client: test_client.cpp
	@echo "========================================"
	@echo "Building Test Client"
	@echo "========================================"
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_client.cpp -o test_client $(LIBS)
	@echo "✓ Built test_client"

# ============================================================================
# Test
# ============================================================================

test: client
	@echo "========================================"
	@echo "Running Test Client"
	@echo "========================================"
	./test_client trigger
	@echo ""
	./test_client benchmark

# ============================================================================
# Clean
# ============================================================================

clean:
	rm -f ai_trading_agent.o ai_trading_agent.so test_client
	@echo "✓ Cleaned build artifacts"

# ============================================================================
# Help
# ============================================================================

help:
	@echo "AI Trading Agent Build System"
	@echo "=============================="
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build both program and client (default)"
	@echo "  program  - Build sBPF program (requires BPF toolchain)"
	@echo "  client   - Build test client"
	@echo "  test     - Run tests"
	@echo "  clean    - Remove build artifacts"
	@echo "  help     - Show this help"
	@echo ""
	@echo "Requirements:"
	@echo "  - GCC/G++ 9+ for test client"
	@echo "  - LLVM/Clang with BPF target for sBPF program"
	@echo "  - slonana built in $(BUILD_DIR)"
