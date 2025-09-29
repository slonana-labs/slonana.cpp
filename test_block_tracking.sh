#!/bin/bash

# Quick test to verify block tracking is working

cd /home/runner/work/slonana.cpp/slonana.cpp

echo "=== TESTING BLOCK TRACKING FIX ==="
echo "Starting validator..."

# Start validator in background with shorter timeout
timeout 45s ./build/slonana_validator \
    --ledger-path test_ledger \
    --rpc-bind-address 127.0.0.1:8899 \
    --gossip-bind-address 127.0.0.1:8001 \
    --log-level info \
    --network-id devnet \
    --allow-stale-rpc \
    --faucet-port 9900 \
    --rpc-faucet-address 127.0.0.1:9900 > test_results/validator.log 2>&1 &

VALIDATOR_PID=$!
echo "Validator started with PID: $VALIDATOR_PID"

# Wait for startup
sleep 20

# Check if validator is responsive
if curl -s "http://localhost:8899/health" | grep -q "ok"; then
    echo "✅ Validator is responsive"
else
    echo "❌ Validator not responding, checking logs..."
    tail -20 test_results/validator.log
    kill $VALIDATOR_PID 2>/dev/null
    exit 1
fi

# Submit a few transactions to trigger block creation
echo "Submitting test transactions..."
for i in {1..3}; do
    RESPONSE=$(curl -s -X POST "http://localhost:8899" -H "Content-Type: application/json" \
        -d "{\"jsonrpc\":\"2.0\",\"id\":$i,\"method\":\"sendTransaction\",\"params\":[\"AQABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9AQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpbXF1eX2BhYmNkZWZnaGlqa2xtbm9wcXJzdHV2d3h5ent8fX5/gIGCg4SFhoeIiYqLjI2Oj5CRkpOUlZaXmJmam5ydnp+goaKjpKWmp6ipqqusra6vsLGys7S1tre4ubq7vL2+v8DBwsPExcbHyMnKy8zNzs/Q0dLT1NXW19jZ2tvc3d7f4OHi4+Tl5ufo6err7O3u7/Dx8vP09fb3+Pn6+/z9/v$(printf '%04d' $i)=\"]}")
    
    if echo "$RESPONSE" | grep -q '"result"'; then
        echo "✅ Transaction $i submitted successfully"
    else
        echo "❌ Transaction $i failed: $RESPONSE"
    fi
    sleep 1
done

# Wait a bit for block processing
echo "Waiting for block processing..."
sleep 5

# Send termination signal and wait for graceful shutdown
echo "Shutting down validator..."
kill -TERM $VALIDATOR_PID 2>/dev/null
sleep 3

# Check the final validator output for block statistics
echo ""
echo "=== VALIDATOR FINAL STATISTICS ==="
grep -A 10 -B 5 "Validator Statistics\|Blocks Processed\|block notification callback\|Successfully committed.*transactions" test_results/validator.log | tail -20

echo ""
echo "=== CHECKING FOR BLOCK TRACKING INDICATORS ==="
if grep -q "✅ Block notification callback connected successfully" test_results/validator.log; then
    echo "✅ Block notification callback was set up correctly"
else
    echo "❌ Block notification callback setup not found"
fi

if grep -q "Processed block at slot.*with.*transactions" test_results/validator.log; then
    echo "✅ Block processing callbacks are working"
else
    echo "❌ No block processing callback messages found"
fi

if grep -q "Banking: Successfully committed.*transactions" test_results/validator.log; then
    echo "✅ Banking stage is committing transactions"
else
    echo "❌ No transaction commitments found"
fi

# Check if final statistics show non-zero values
if grep "Blocks Processed:" test_results/validator.log | grep -v "Blocks Processed: 0"; then
    echo "✅ SUCCESS: Blocks are being tracked (non-zero count)"
    exit 0
else
    echo "❌ ISSUE: Blocks Processed is still 0"
    echo "Last 10 lines of validator log:"
    tail -10 test_results/validator.log
    exit 1
fi