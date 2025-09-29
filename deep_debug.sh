#!/bin/bash

# Deep investigation script to trace why callbacks aren't working

cd /home/runner/work/slonana.cpp/slonana.cpp

echo "=== DEEP INVESTIGATION: Why callbacks aren't triggering ==="

# Start validator with detailed logging
echo "Starting validator with banking stage debugging..."
timeout 60s ./build/slonana_validator \
    --ledger-path test_ledger \
    --rpc-bind-address 127.0.0.1:8899 \
    --gossip-bind-address 127.0.0.1:8001 \
    --log-level info \
    --network-id devnet \
    --allow-stale-rpc \
    --faucet-port 9900 \
    --rpc-faucet-address 127.0.0.1:9900 > test_results/deep_debug.log 2>&1 &

VALIDATOR_PID=$!
echo "Validator PID: $VALIDATOR_PID"

# Wait for startup
sleep 15

# Submit transaction and trace what happens
echo "Submitting transaction and tracing banking stage..."
RESPONSE=$(curl -s -X POST "http://localhost:8899" -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","id":1,"method":"sendTransaction","params":["AQABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9ARQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpbXF1eX2BhYmNkZWZnaGlqa2xtbm9wcXJzdHV2d3h5ent8fX5/gIGCg4SFhoeIiYqLjI2Oj5CRkpOUlZaXmJmam5ydnp+goaKjpKWmp6ipqqusra6vsLGys7S1tre4ubq7vL2+v8DBwsPExcbHyMnKy8zNzs/Q0dLT1NXW19jZ2tvc3d7f4OHi4+Tl5ufo6err7O3u7/Dx8vP09fb3+Pn6+/z9/v0001="]}' 2>/dev/null)

echo "Transaction response: $RESPONSE"

# Wait for processing
sleep 10

# Stop validator gracefully
echo "Stopping validator..."
kill -TERM $VALIDATOR_PID 2>/dev/null
sleep 5

echo ""
echo "=== CALLBACK ANALYSIS ==="
echo ""

# Check if callback was set up
if grep -q "Block notification callback connected successfully" test_results/deep_debug.log; then
    echo "✅ Callback setup: CONFIRMED"
else
    echo "❌ Callback setup: MISSING"
fi

# Check if sendTransaction was called
if grep -q "sendTransaction" test_results/deep_debug.log; then
    echo "✅ sendTransaction RPC: CALLED"
    grep "sendTransaction" test_results/deep_debug.log | head -3
else
    echo "❌ sendTransaction RPC: NOT CALLED"
fi

# Check if banking stage processed anything
echo ""
echo "=== BANKING STAGE ACTIVITY ==="
if grep -q "Banking:" test_results/deep_debug.log; then
    echo "✅ Banking stage activity detected:"
    grep "Banking:" test_results/deep_debug.log | tail -10
else
    echo "❌ No banking stage activity found"
fi

# Check if blocks were committed
echo ""
echo "=== BLOCK COMMITMENT ==="
if grep -q "Successfully committed.*transactions" test_results/deep_debug.log; then
    echo "✅ Block commitment detected:"
    grep "Successfully committed.*transactions" test_results/deep_debug.log
else
    echo "❌ No block commitments found"
fi

# Check if callback was actually triggered
echo ""
echo "=== CALLBACK EXECUTION ==="
if grep -q "Processed block at slot.*with.*transactions" test_results/deep_debug.log; then
    echo "✅ Block callback triggered:"
    grep "Processed block at slot.*with.*transactions" test_results/deep_debug.log
else
    echo "❌ Block callback never triggered"
fi

# Show final statistics
echo ""
echo "=== FINAL STATISTICS ==="
grep -A 10 "Validator Statistics" test_results/deep_debug.log | tail -10

echo ""
echo "=== DIAGNOSTIC SUMMARY ==="
echo "The issue is likely:"
echo "1. Transactions not reaching banking stage commitment"
echo "2. Banking stage not calling block notification callback"
echo "3. Callback being set but not properly linked"
echo ""
echo "Full log saved to test_results/deep_debug.log"