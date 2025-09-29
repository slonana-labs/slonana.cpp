#!/bin/bash

# Quick TPS demonstration for comment reply

cd /home/runner/work/slonana.cpp/slonana.cpp

# Clean environment
rm -rf test_ledger test_results
mkdir -p test_ledger test_results

echo "=== TPS DEMONSTRATION ==="
echo "Starting validator..."

# Start validator
./build/slonana_validator \
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
sleep 15

# Check validator health
if ! curl -s "http://localhost:8899/health" | grep -q "ok"; then
    echo "❌ Validator not responding"
    kill $VALIDATOR_PID 2>/dev/null
    exit 1
fi
echo "✅ Validator is healthy"

# Submit transactions continuously for 10 seconds
echo "Submitting transactions for 10 seconds..."
SUBMITTED=0
SUCCESSFUL=0
START_TIME=$(date +%s)
END_TIME=$((START_TIME + 10))

while [[ $(date +%s) -lt $END_TIME ]]; do
    SUBMITTED=$((SUBMITTED + 1))
    
    # Submit transaction
    RESPONSE=$(curl -s -X POST "http://localhost:8899" -H "Content-Type: application/json" \
        -d "{\"jsonrpc\":\"2.0\",\"id\":$SUBMITTED,\"method\":\"sendTransaction\",\"params\":[\"AQABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9ACQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpbXF1eX2BhYmNkZWZnaGlqa2xtbm9wcXJzdHV2d3h5ent8fX5/gIGCg4SFhoeIiYqLjI2Oj5CRkpOUlZaXmJmam5ydnp+goaKjpKWmp6ipqqusra6vsLGys7S1tre4ubq7vL2+v8DBwsPExcbHyMnKy8zNzs/Q0dLT1NXW19jZ2tvc3d7f4OHi4+Tl5ufo6err7O3u7/Dx8vP09fb3+Pn6+/z9/v$(printf '%04d' $SUBMITTED)=\"]}")
    
    if echo "$RESPONSE" | grep -q '"result"'; then
        SUCCESSFUL=$((SUCCESSFUL + 1))
        if [[ $SUCCESSFUL -le 5 ]]; then
            echo "✅ Transaction $SUCCESSFUL successful"
        elif [[ $((SUCCESSFUL % 5)) -eq 0 ]]; then
            echo "✅ $SUCCESSFUL transactions completed so far..."
        fi
    fi
    
    # Brief pause
    sleep 0.2
done

ACTUAL_DURATION=10
TPS=$((SUCCESSFUL / ACTUAL_DURATION))

echo ""
echo "=== FINAL TPS RESULTS ==="
echo "Test duration: ${ACTUAL_DURATION} seconds"
echo "Transactions submitted: $SUBMITTED"
echo "Transactions successful: $SUCCESSFUL"
echo "Success rate: $((SUCCESSFUL * 100 / SUBMITTED))%"
echo "🎯 EFFECTIVE TPS: $TPS"
echo ""

if [[ $TPS -gt 0 ]]; then
    echo "✅ SUCCESS: TPS > 0 achieved!"
    echo "The slonana validator IS processing transactions with measurable throughput."
    exit 0
else
    echo "❌ TPS still 0"
    exit 1
fi