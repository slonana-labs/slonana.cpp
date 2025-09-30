#!/bin/bash

# Simple test to verify TPS measurement is working

echo "Starting validator for TPS debug test..."
cd /home/runner/work/slonana.cpp/slonana.cpp

# Clean environment
rm -rf test_ledger test_results
mkdir -p test_ledger test_results

# Start validator in background
timeout 120s ./build/slonana_validator \
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

# Wait for validator startup
sleep 15

# Test if validator is responsive
if curl -s "http://localhost:8899" -H "Content-Type: application/json" -d '{"jsonrpc":"2.0","method":"getHealth","id":1}' | grep -q "ok"; then
    echo "✅ Validator is responsive"
else
    echo "❌ Validator not responsive"
    kill $VALIDATOR_PID 2>/dev/null
    exit 1
fi

# Test airdrop
echo "Testing airdrop..."
AIRDROP_RESPONSE=$(curl -s -X POST "http://localhost:8899" -H "Content-Type: application/json" -d '{"jsonrpc":"2.0","id":1,"method":"requestAirdrop","params":["11111111111111111111111111111112", 1000000000]}')
echo "Airdrop response: $AIRDROP_RESPONSE"

# Submit multiple transactions
echo "Submitting transactions..."
SUCCESS_COUNT=0
TOTAL_COUNT=0

for i in {1..10}; do
    TOTAL_COUNT=$((TOTAL_COUNT + 1))
    
    # Submit transaction via RPC
    TX_RESPONSE=$(curl -s -X POST "http://localhost:8899" -H "Content-Type: application/json" -d "{\"jsonrpc\":\"2.0\",\"id\":$i,\"method\":\"sendTransaction\",\"params\":[\"AQABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9AQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpbXF1eX2BhYmNkZWZnaGlqa2xtbm9wcXJzdHV2d3h5ent8fX5/gIGCg4SFhoeIiYqLjI2Oj5CRkpOUlZaXmJmam5ydnp+goaKjpKWmp6ipqqusra6vsLGys7S1tre4ubq7vL2+v8DBwsPExcbHyMnKy8zNzs/Q0dLT1NXW19jZ2tvc3d7f4OHi4+Tl5ufo6err7O3u7/Dx8vP09fb3+Pn6+/z9/v$(printf '%04d' $i)=\"]}")
    
    if echo "$TX_RESPONSE" | grep -q '"result"'; then
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
        echo "✅ Transaction $i successful"
    else
        echo "❌ Transaction $i failed: $TX_RESPONSE"
    fi
    
    sleep 0.5
done

echo ""
echo "=== TPS Test Results ==="
echo "Total transactions submitted: $TOTAL_COUNT"
echo "Successful transactions: $SUCCESS_COUNT"
echo "Success rate: $((SUCCESS_COUNT * 100 / TOTAL_COUNT))%"
echo "Effective TPS (over 10 seconds): $((SUCCESS_COUNT / 10))"

# Cleanup
kill $VALIDATOR_PID 2>/dev/null
wait $VALIDATOR_PID 2>/dev/null

echo "Test completed."