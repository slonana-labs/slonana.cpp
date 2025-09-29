#!/bin/bash

# Final test to verify TPS fix

cd /home/runner/work/slonana.cpp/slonana.cpp

echo "=== FINAL TPS FIX VALIDATION ==="

# Start validator
echo "Starting validator..."
timeout 90s ./build/slonana_validator \
    --ledger-path test_ledger \
    --rpc-bind-address 127.0.0.1:8899 \
    --gossip-bind-address 127.0.0.1:8001 \
    --log-level info \
    --network-id devnet \
    --allow-stale-rpc \
    --faucet-port 9900 \
    --rpc-faucet-address 127.0.0.1:9900 > test_results/final_test.log 2>&1 &

VALIDATOR_PID=$!
echo "Validator PID: $VALIDATOR_PID"

# Wait for startup
sleep 15

echo "Submitting test transaction..."
RESPONSE=$(curl -s -X POST "http://localhost:8899" -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","id":1,"method":"sendTransaction","params":["AQABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9AREREGR0hJSlNNTU5PUFFRU1RVVldYWVpbXF1eX2BhYmNkZWZnaGlqa2xtbm9wcXJzdHV2d3h5ent8fX5/gIGCg4SFhoeIiYqLjI2Oj5CRkpOUlZaXmJmam5ydnp+goaKjpKWmp6ipqqusra6vsLGys7S1tre4ubq7vL2+v8DBwsPExcbHyMnKy8zNzs/Q0dLT1NXW19jZ2tvc3d7f4OHi4+Tl5ufo6err7O3u7/Dx8vP09fb3+Pn6+/z9/v0001="]}')

echo "Transaction response: $RESPONSE"

# Wait for processing
sleep 10

# Stop validator gracefully
echo "Stopping validator..."
kill -TERM $VALIDATOR_PID 2>/dev/null
sleep 5

echo ""
echo "=== ANALYSIS ==="

# Check key indicators
echo "1. Callback Setup:"
if grep -q "Block notification callback connected successfully" test_results/final_test.log; then
    echo "   ✅ SUCCESS"
else
    echo "   ❌ FAILED"
fi

echo "2. Genesis Block Handling:"
if grep -q "Creating genesis block" test_results/final_test.log; then
    echo "   ✅ Genesis block created"
else
    echo "   ⚠️  No genesis block message (may be normal)"
fi

echo "3. Transaction Processing:"
if grep -q "Process function completed.*validation.*SUCCESS" test_results/final_test.log && \
   grep -q "Process function completed.*execution.*SUCCESS" test_results/final_test.log && \
   grep -q "Process function completed.*commitment.*SUCCESS" test_results/final_test.log; then
    echo "   ✅ All stages completed successfully"
else
    echo "   ❌ Stage processing failed"
    grep "Process function completed.*result:" test_results/final_test.log | tail -3
fi

echo "4. Block Commitment:"
if grep -q "Successfully committed.*transactions to ledger" test_results/final_test.log; then
    echo "   ✅ Block committed to ledger"
    grep "Successfully committed.*transactions to ledger" test_results/final_test.log
else
    echo "   ❌ No block commitment found"
fi

echo "5. Block Notification Callback:"
if grep -q "Notified validator core about block" test_results/final_test.log; then
    echo "   ✅ Callback triggered successfully"
    grep "Notified validator core about block" test_results/final_test.log
else
    echo "   ❌ Callback not triggered"
fi

echo "6. Validator Statistics Update:"
if grep -q "Processed block at slot.*with.*transactions" test_results/final_test.log; then
    echo "   ✅ Statistics callback working"
    grep "Processed block at slot.*with.*transactions" test_results/final_test.log
else
    echo "   ❌ Statistics callback not working"
fi

echo "7. Final Statistics:"
FINAL_STATS=$(grep -A 8 "Validator Statistics" test_results/final_test.log | tail -8)
echo "$FINAL_STATS"

if echo "$FINAL_STATS" | grep -q "Blocks Processed: [1-9]"; then
    echo "   🎉 SUCCESS: Blocks Processed > 0!"
else
    echo "   ❌ STILL ISSUE: Blocks Processed = 0"
fi

if echo "$FINAL_STATS" | grep -q "Transactions Processed: [1-9]"; then
    echo "   🎉 SUCCESS: Transactions Processed > 0!"
else
    echo "   ❌ STILL ISSUE: Transactions Processed = 0"
fi

echo ""
echo "=== FINAL VERDICT ==="
if grep -q "Successfully committed.*transactions to ledger" test_results/final_test.log && \
   grep -q "Notified validator core about block" test_results/final_test.log; then
    echo "🎉 TPS ISSUE RESOLVED! Transaction pipeline working end-to-end"
    echo "✅ Banking stage commits transactions"
    echo "✅ Block notification callback triggers"
    echo "✅ Validator statistics should be updated"
else
    echo "❌ TPS issue persists - additional investigation needed"
fi

echo ""
echo "Full logs in test_results/final_test.log"