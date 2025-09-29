#!/bin/bash

# Debug why CLI transactions are failing at sendTransaction step

cd /home/runner/work/slonana.cpp/slonana.cpp

echo "=== DEBUGGING CLI TRANSACTION FAILURE ==="

# Clean environment
rm -rf test_ledger test_results
mkdir -p test_ledger test_results

# Start validator
echo "Starting validator..."
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
echo "Validator PID: $VALIDATOR_PID"

# Wait for startup
sleep 15

echo ""
echo "=== COMPARISON TESTS ==="

# Test 1: Direct RPC sendTransaction (this should work based on previous tests)
echo "1. Testing direct RPC sendTransaction..."
DIRECT_RESPONSE=$(curl -s -X POST "http://localhost:8899" -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","id":1,"method":"sendTransaction","params":["AQABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9AQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpbXF1eX2BhYmNkZWZnaGlqa2xtbm9wcXJzdHV2d3h5ent8fX5/gIGCg4SFhoeIiYqLjI2Oj5CRkpOUlZaXmJmam5ydnp+goaKjpKWmp6ipqqusra6vsLGys7S1tre4ubq7vL2+v8DBwsPExcbHyMnKy8zNzs/Q0dLT1NXW19jZ2tvc3d7f4OHi4+Tl5ufo6err7O3u7/Dx8vP09fb3+Pn6+/z9/v0001="]}')

if echo "$DIRECT_RESPONSE" | grep -q '"result"'; then
    echo "   ✅ Direct RPC: SUCCESS"
    echo "   Response: $DIRECT_RESPONSE"
else
    echo "   ❌ Direct RPC: FAILED"
    echo "   Response: $DIRECT_RESPONSE"
fi

sleep 2

# Test 2: Try Solana CLI install and test
echo ""
echo "2. Testing Solana CLI transaction (abbreviated)..."

# Check if Solana CLI is available
export PATH="$HOME/.local/share/solana/install/active_release/bin:$PATH"
if command -v solana >/dev/null 2>&1; then
    echo "   ✅ Solana CLI available"
    
    # Configure CLI
    solana config set --url http://localhost:8899 >/dev/null 2>&1
    
    # Generate test keypair
    solana-keygen new --no-bip39-passphrase --silent --outfile test_results/test_sender.json 2>/dev/null
    solana-keygen new --no-bip39-passphrase --silent --outfile test_results/test_recipient.json 2>/dev/null
    
    RECIPIENT=$(solana-keygen pubkey test_results/test_recipient.json 2>/dev/null)
    
    # Try airdrop
    echo "   Testing airdrop..."
    AIRDROP_RESULT=$(timeout 10s solana airdrop 1 test_results/test_sender.json 2>&1)
    echo "   Airdrop result: $AIRDROP_RESULT"
    
    # Check balance
    BALANCE=$(timeout 5s solana balance test_results/test_sender.json 2>&1)
    echo "   Balance: $BALANCE"
    
    # Try transfer with more debugging
    echo "   Attempting transfer with debugging..."
    timeout 15s strace -f -e trace=network,write solana transfer "$RECIPIENT" 0.001 \
        --keypair test_results/test_sender.json \
        --allow-unfunded-recipient \
        --fee-payer test_results/test_sender.json \
        --verbose 2>&1 | head -20
        
else
    echo "   ❌ Solana CLI not available"
fi

# Wait for transaction processing
sleep 5

# Stop validator
echo ""
echo "Stopping validator..."
kill -TERM $VALIDATOR_PID 2>/dev/null
sleep 3

echo ""
echo "=== ANALYSIS ==="

# Check if direct RPC worked
if grep -q "sendTransaction called" test_results/validator.log; then
    echo "✅ Direct RPC sendTransaction reached validator"
    SEND_COUNT=$(grep -c "sendTransaction called" test_results/validator.log)
    echo "   sendTransaction calls: $SEND_COUNT"
else
    echo "❌ No sendTransaction calls detected"
fi

# Check banking stage activity
if grep -q "Banking.*Successfully committed" test_results/validator.log; then
    echo "✅ Banking stage committed transactions"
    grep "Banking.*Successfully committed" test_results/validator.log
else
    echo "❌ No banking stage commits found"
fi

# Check validator statistics
echo ""
echo "Final validator statistics:"
grep -A 8 "Validator Statistics" test_results/validator.log | tail -8

echo ""
echo "=== ROOT CAUSE ANALYSIS ==="
echo "The issue is clear: Solana CLI makes RPC calls (getLatestBlockhash, getAccountInfo, getFeeForMessage)"
echo "but never calls sendTransaction. This suggests the CLI is failing during transaction construction"
echo "or signing, possibly due to:"
echo "1. Blockhash validation issues"
echo "2. Account balance insufficient for fees"
echo "3. Transaction construction/signing errors"
echo "4. RPC response format incompatibility"

echo ""
echo "Next steps: Need to investigate RPC response compatibility or use direct RPC approach"