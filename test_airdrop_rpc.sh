#!/bin/bash

# Test script to verify airdrop RPC functionality
# This will help debug the airdrop issue

echo "=== Testing Slonana Validator Airdrop RPC ==="

# Start validator in background
echo "Starting validator..."
cd /home/runner/work/slonana.cpp/slonana.cpp
./build/slonana_validator --ledger-path /tmp/test_ledger --rpc-bind-address 127.0.0.1:8899 > /tmp/validator.log 2>&1 &
VALIDATOR_PID=$!

# Wait for startup
echo "Waiting for validator to start..."
sleep 10

# Test basic RPC health
echo "Testing RPC health..."
curl -s "http://localhost:8899/health" || echo "Health endpoint not available"

# Test getVersion method
echo "Testing getVersion RPC method..."
curl -s -X POST "http://localhost:8899" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"getVersion"}' \
  | jq . || echo "getVersion failed"

# Test requestAirdrop method directly
echo "Testing requestAirdrop RPC method..."
curl -s -X POST "http://localhost:8899" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"requestAirdrop","params":["11111111111111111111111111111112", 1000000000]}' \
  | jq . || echo "requestAirdrop failed"

# Test getBalance method 
echo "Testing getBalance RPC method..."
curl -s -X POST "http://localhost:8899" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"getBalance","params":["11111111111111111111111111111112"]}' \
  | jq . || echo "getBalance failed"

# Generate a keypair and test with Solana CLI
echo "Testing with Solana CLI..."
export PATH="$HOME/.local/share/solana/install/active_release/bin:$PATH"
solana-keygen new --no-bip39-passphrase --silent --outfile /tmp/test-keypair.json
solana config set --url "http://localhost:8899"

echo "Attempting airdrop via Solana CLI..."
solana airdrop 1 --keypair /tmp/test-keypair.json || echo "Solana CLI airdrop failed"

echo "Checking balance via Solana CLI..."
solana balance --keypair /tmp/test-keypair.json || echo "Balance check failed"

# Cleanup
echo "Cleaning up..."
kill $VALIDATOR_PID 2>/dev/null
wait $VALIDATOR_PID 2>/dev/null

echo "Test complete. Check output above for issues."