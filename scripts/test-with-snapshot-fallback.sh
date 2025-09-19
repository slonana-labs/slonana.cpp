#!/bin/bash

# Test script that mimics CI snapshot handling behavior
# Usage: ./scripts/test-with-snapshot-fallback.sh

set -e

echo "=== Slonana Test Runner with Snapshot Fallback ==="

# Create build directory if it doesn't exist
if [ ! -d "build" ]; then
    echo "Creating build directory..."
    mkdir -p build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc)
    cd ..
fi

# Create snapshots directory if it doesn't exist  
mkdir -p build/snapshots

# Check snapshot integrity
SNAPSHOT="build/snapshots/latest.tar.zst"
if [ ! -s "$SNAPSHOT" ] || [ $(stat -c%s "$SNAPSHOT" 2>/dev/null || echo 0) -lt 10000 ]; then
    echo "WARN: Snapshot invalid or missing (size: $(stat -c%s "$SNAPSHOT" 2>/dev/null || echo 0) bytes). Using genesis fallback."
    # Copy genesis file as fallback
    cp config/mainnet/genesis.json build/snapshots/ 2>/dev/null || echo "Genesis file available as fallback"
    export SNAPSHOT_MODE=genesis
else
    echo "Snapshot is valid (size: $(stat -c%s "$SNAPSHOT") bytes)"
    export SNAPSHOT_MODE=snapshot
fi

echo "Running tests with snapshot mode: $SNAPSHOT_MODE"

cd build

# Run tests with graceful error handling
EXIT_CODE=0

echo "=== Running Basic Tests ==="
if ! ./slonana_tests; then
    echo "WARNING: Some basic tests failed"
    EXIT_CODE=1
fi

echo "=== Running Comprehensive Tests ==="  
if ! ./slonana_comprehensive_tests; then
    echo "WARNING: Some comprehensive tests failed"
    EXIT_CODE=1
fi

if [ $EXIT_CODE -eq 1 ]; then
    echo "Some test failures detected, check output above"
    exit 1
else
    echo "All tests completed successfully"
fi