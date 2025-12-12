#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$REPO_ROOT"

echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║     E2E TEST WITH REAL VALIDATOR - NO MOCKS                      ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo

# Step 1: Build validator if needed
echo "[STEP 1/5] Building slonana validator..."
if [ ! -f "build/slonana_validator" ] && [ ! -f "slonana_validator" ]; then
    echo "Building validator binary..."
    mkdir -p build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make slonana_validator -j$(nproc)
    cd ..
fi

if [ -f "build/slonana_validator" ]; then
    VALIDATOR_BIN="build/slonana_validator"
elif [ -f "slonana_validator" ]; then
    VALIDATOR_BIN="slonana_validator"
else
    echo "ERROR: Validator binary not found"
    exit 1
fi

echo "✓ Validator binary: $VALIDATOR_BIN"
echo

# Step 2: Build async agent sBPF program
echo "[STEP 2/5] Building async agent sBPF program..."
cd examples/async_agent
mkdir -p build

if ! command -v clang &> /dev/null; then
    echo "ERROR: clang not found. Install with: apt-get install clang"
    exit 1
fi

clang -target bpfel -O2 -fno-builtin -ffreestanding -nostdlib \
    -c async_agent_sbpf.c -o build/async_agent_sbpf.o

if [ ! -f build/async_agent_sbpf.o ]; then
    echo "ERROR: Failed to compile sBPF program"
    exit 1
fi

# Link to create .so
clang -target bpfel -nostdlib -shared \
    build/async_agent_sbpf.o -o build/async_agent_sbpf.so

if [ ! -f build/async_agent_sbpf.so ]; then
    echo "ERROR: Failed to link sBPF program"
    exit 1
fi

SBPF_SIZE=$(stat -f%z build/async_agent_sbpf.so 2>/dev/null || stat -c%s build/async_agent_sbpf.so)
echo "✓ sBPF binary: build/async_agent_sbpf.so ($SBPF_SIZE bytes)"

# Verify it's ELF eBPF
file build/async_agent_sbpf.so | grep -q "eBPF"
if [ $? -eq 0 ]; then
    echo "✓ Verified: ELF 64-bit eBPF binary"
else
    echo "WARNING: Binary type verification failed"
fi

cd "$REPO_ROOT"
echo

# Step 3: Build test executable
echo "[STEP 3/5] Building test executable..."
mkdir -p build
cd build

if [ ! -f CMakeCache.txt ]; then
    cmake .. -DCMAKE_BUILD_TYPE=Release
fi

make test_real_validator_deployment -j$(nproc)

if [ ! -f test_real_validator_deployment ]; then
    echo "ERROR: Test executable not found"
    exit 1
fi

echo "✓ Test executable built"
cd "$REPO_ROOT"
echo

# Step 4: Make sure slonana-cli.sh is executable
echo "[STEP 4/5] Preparing CLI tools..."
chmod +x scripts/slonana-cli.sh
echo "✓ CLI tools ready"
echo

# Step 5: Run the test
echo "[STEP 5/5] Running real validator test..."
echo "────────────────────────────────────────────────────────────────────"
echo

cd build
./test_real_validator_deployment
TEST_RESULT=$?

cd "$REPO_ROOT"

echo
echo "════════════════════════════════════════════════════════════════════"
if [ $TEST_RESULT -eq 0 ]; then
    echo
    echo "  ✓ ALL E2E TESTS PASSED WITH REAL VALIDATOR"
    echo
    echo "  This test:"
    echo "  • Started actual slonana_validator process"
    echo "  • Waited for RPC endpoint to be ready"
    echo "  • Deployed real sBPF binary via RPC/CLI"
    echo "  • Executed transactions via RPC/CLI"
    echo "  • Verified program execution"
    echo "  • Shut down validator cleanly"
    echo
    echo "  NO MOCKS, NO STUBS, NO SIMULATION"
    echo
else
    echo
    echo "  ✗ E2E TEST FAILED"
    echo
    echo "  Check logs above for details"
    echo
fi
echo "════════════════════════════════════════════════════════════════════"

exit $TEST_RESULT
