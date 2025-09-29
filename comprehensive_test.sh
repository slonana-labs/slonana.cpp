#!/bin/bash

# Comprehensive test to understand why TPS is still 0

cd /home/runner/work/slonana.cpp/slonana.cpp

echo "=== COMPREHENSIVE TPS INVESTIGATION ==="
echo "1. Testing direct RPC sendTransaction"
echo "2. Testing CLI transaction flow"
echo "3. Analyzing banking stage callbacks"
echo ""

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
echo "=== TEST 1: Direct RPC sendTransaction ==="

# Test direct RPC sendTransaction
RESPONSE=$(curl -s -X POST "http://localhost:8899" -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","id":1,"method":"sendTransaction","params":["AQABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9AQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpbXF1eX2BhYmNkZWZnaGlqa2xtbm9wcXJzdHV2d3h5ent8fX5/gIGCg4SFhoeIiYqLjI2Oj5CRkpOUlZaXmJmam5ydnp+goaKjpKWmp6ipqqusra6vsLGys7S1tre4ubq7vL2+v8DBwsPExcbHyMnKy8zNzs/Q0dLT1NXW19jZ2tvc3d7f4OHi4+Tl5ufo6err7O3u7/Dx8vP09fb3+Pn6+/z9/v0001="]}')

echo "Direct RPC sendTransaction response: $RESPONSE"

if echo "$RESPONSE" | grep -q '"result"'; then
    echo "✅ Direct RPC sendTransaction: SUCCESS"
else
    echo "❌ Direct RPC sendTransaction: FAILED"
fi

sleep 3

echo ""
echo "=== TEST 2: Install Solana CLI and test transaction flow ==="

# Install Solana CLI for proper testing
export PATH="$HOME/.local/share/solana/install/active_release/bin:$PATH"
if ! command -v solana >/dev/null 2>&1; then
    echo "Installing Solana CLI..."
    curl --proto '=https' --tlsv1.2 -sSfL https://solana-install.solana.workers.dev | bash -s -- --no-modify-path
    export PATH="$HOME/.local/share/solana/install/active_release/bin:$PATH"
fi

if command -v solana >/dev/null 2>&1; then
    echo "✅ Solana CLI available: $(which solana)"
    
    # Configure CLI
    solana config set --url http://localhost:8899 >/dev/null 2>&1
    
    # Generate keypair
    solana-keygen new --no-bip39-passphrase --silent --outfile test_results/test_keypair.json >/dev/null 2>&1
    
    # Try airdrop
    echo "Testing airdrop..."
    AIRDROP_RESULT=$(solana airdrop 1 test_results/test_keypair.json 2>&1)
    echo "Airdrop result: $AIRDROP_RESULT"
    
    # Check balance
    BALANCE=$(solana balance test_results/test_keypair.json 2>&1)
    echo "Balance: $BALANCE"
    
    # Generate recipient
    solana-keygen new --no-bip39-passphrase --silent --outfile test_results/recipient.json >/dev/null 2>&1
    RECIPIENT=$(solana-keygen pubkey test_results/recipient.json)
    
    # Try transfer with detailed logging
    echo "Attempting transfer..."
    echo "Command: solana transfer $RECIPIENT 0.001 --keypair test_results/test_keypair.json --allow-unfunded-recipient --fee-payer test_results/test_keypair.json --verbose"
    
    TRANSFER_RESULT=$(timeout 30s solana transfer "$RECIPIENT" 0.001 --keypair test_results/test_keypair.json --allow-unfunded-recipient --fee-payer test_results/test_keypair.json --verbose 2>&1)
    echo "Transfer result: $TRANSFER_RESULT"
    
    if echo "$TRANSFER_RESULT" | grep -q "Signature:"; then
        echo "✅ CLI Transfer: SUCCESS"
    else
        echo "❌ CLI Transfer: FAILED"
    fi
else
    echo "❌ Solana CLI not available, skipping CLI tests"
fi

sleep 5

echo ""
echo "=== TEST 3: Banking Stage Analysis ==="

# Stop validator gracefully
echo "Stopping validator..."
kill -TERM $VALIDATOR_PID 2>/dev/null
sleep 5

echo ""
echo "=== RESULTS ANALYSIS ==="

# Check callback setup
if grep -q "Block notification callback connected successfully" test_results/validator.log; then
    echo "✅ Callback Setup: SUCCESS"
else
    echo "❌ Callback Setup: FAILED"
fi

# Check sendTransaction calls
SENDTX_COUNT=$(grep -c "sendTransaction called" test_results/validator.log 2>/dev/null || echo "0")
echo "📊 sendTransaction calls: $SENDTX_COUNT"

# Check banking stage submissions
BANKING_COUNT=$(grep -c "Transaction submitted to banking stage" test_results/validator.log 2>/dev/null || echo "0")
echo "📊 Banking stage submissions: $BANKING_COUNT"

# Check block commitments
COMMIT_COUNT=$(grep -c "Successfully committed.*transactions" test_results/validator.log 2>/dev/null || echo "0")
echo "📊 Block commitments: $COMMIT_COUNT"

# Check callback triggers
CALLBACK_COUNT=$(grep -c "Processed block at slot.*with.*transactions" test_results/validator.log 2>/dev/null || echo "0")
echo "📊 Callback triggers: $CALLBACK_COUNT"

# Show final statistics
echo ""
echo "=== FINAL VALIDATOR STATISTICS ==="
grep -A 8 "Validator Statistics" test_results/validator.log | tail -8

echo ""
echo "=== DIAGNOSIS ==="
if [[ $SENDTX_COUNT -eq 0 ]]; then
    echo "🔍 Root Issue: sendTransaction RPC method never called"
    echo "   - CLI is failing before reaching transaction submission"
    echo "   - Check airdrop/funding, account balances, or CLI configuration"
elif [[ $BANKING_COUNT -eq 0 ]]; then
    echo "🔍 Root Issue: Transactions not reaching banking stage"
    echo "   - sendTransaction called but banking stage not receiving"
    echo "   - Check RPC server transaction processing logic"
elif [[ $COMMIT_COUNT -eq 0 ]]; then
    echo "🔍 Root Issue: Banking stage not committing transactions"
    echo "   - Transactions reach banking stage but fail processing"
    echo "   - Check banking stage transaction validation/processing"
elif [[ $CALLBACK_COUNT -eq 0 ]]; then
    echo "🔍 Root Issue: Block notification callback not triggered"
    echo "   - Transactions processed but callback not called"
    echo "   - Check callback setup and trigger logic"
else
    echo "🎉 All components working - need to check statistics update logic"
fi

echo ""
echo "Detailed logs available in test_results/validator.log"