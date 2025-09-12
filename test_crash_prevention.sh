#!/bin/bash

echo "=== Transaction Crash Prevention Test ==="

# Create test directories
mkdir -p /tmp/test_ledger
mkdir -p /tmp/test_results

echo "Starting slonana validator with crash prevention..."

# Start validator in background with output logging
cd /home/runner/work/slonana.cpp/slonana.cpp/build
timeout 60 ./slonana_validator \
  --ledger-path /tmp/test_ledger \
  --rpc-bind-address 127.0.0.1:18899 \
  --gossip-bind-address 127.0.0.1:18001 \
  --log-level info > /tmp/test_results/validator.log 2>&1 &

VALIDATOR_PID=$!
echo "Validator started with PID: $VALIDATOR_PID"

# Wait for validator to start
echo "Waiting for validator to initialize..."
sleep 15

# Check if validator is still running
if ! kill -0 $VALIDATOR_PID 2>/dev/null; then
    echo "❌ FAILED: Validator process died during startup"
    cat /tmp/test_results/validator.log
    exit 1
fi

echo "✅ Validator started successfully and is running"

# Test with simple RPC calls to check if validator responds without crashing
echo "Testing basic RPC responsiveness..."

# Test getHealth method
timeout 5 curl -s -X POST http://127.0.0.1:18899 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"getHealth"}' > /tmp/test_results/health_response.json 2>/dev/null

if [ $? -eq 0 ]; then
    echo "✅ getHealth RPC call successful"
else
    echo "⚠️  getHealth RPC call failed (but validator might still be running)"
fi

# Test problematic transaction that previously caused crashes
echo "Testing sendTransaction with potentially problematic data..."

# Test 1: Short transaction
timeout 5 curl -s -X POST http://127.0.0.1:18899 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"sendTransaction","params":["SHORT"]}' > /tmp/test_results/tx1_response.json 2>/dev/null

# Test 2: Empty transaction
timeout 5 curl -s -X POST http://127.0.0.1:18899 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":3,"method":"sendTransaction","params":[""]}' > /tmp/test_results/tx2_response.json 2>/dev/null

# Test 3: Invalid transaction format
timeout 5 curl -s -X POST http://127.0.0.1:18899 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":4,"method":"sendTransaction","params":["INVALID_DATA!@#$"]}' > /tmp/test_results/tx3_response.json 2>/dev/null

# Test 4: Very long transaction
LONG_TX=$(printf 'A%.0s' {1..1000})
timeout 5 curl -s -X POST http://127.0.0.1:18899 \
  -H "Content-Type: application/json" \
  -d "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"sendTransaction\",\"params\":[\"$LONG_TX\"]}" > /tmp/test_results/tx4_response.json 2>/dev/null

echo "Completed transaction tests"

# Check if validator is still running after all the problematic transactions
sleep 2

if kill -0 $VALIDATOR_PID 2>/dev/null; then
    echo "✅ SUCCESS: Validator is still running after all transaction tests"
    echo "✅ Transaction crash prevention is working!"
    
    # Test final responsiveness
    timeout 5 curl -s -X POST http://127.0.0.1:18899 \
      -H "Content-Type: application/json" \
      -d '{"jsonrpc":"2.0","id":99,"method":"getVersion"}' > /tmp/test_results/final_test.json 2>/dev/null
    
    if [ $? -eq 0 ]; then
        echo "✅ Validator is responsive after transaction stress tests"
    else
        echo "⚠️  Validator may be unresponsive but is still running"
    fi
    
    TEST_RESULT=0
else
    echo "❌ FAILED: Validator process died after transaction tests"
    echo "❌ Transaction crash prevention did not work"
    TEST_RESULT=1
fi

# Show validator log output for debugging
echo ""
echo "=== Validator Log Output ==="
tail -50 /tmp/test_results/validator.log

# Cleanup
echo ""
echo "Stopping validator..."
kill -TERM $VALIDATOR_PID 2>/dev/null || true
sleep 2
kill -KILL $VALIDATOR_PID 2>/dev/null || true

echo ""
echo "=== Test Summary ==="
if [ $TEST_RESULT -eq 0 ]; then
    echo "🎉 TRANSACTION CRASH PREVENTION TEST PASSED!"
    echo "The validator successfully handled problematic transactions without crashing."
else
    echo "❌ TRANSACTION CRASH PREVENTION TEST FAILED!"
    echo "The validator crashed when processing problematic transactions."
fi

exit $TEST_RESULT