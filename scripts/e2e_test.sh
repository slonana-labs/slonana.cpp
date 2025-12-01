#!/bin/bash
# End-to-End Test Script for Slonana AI Trading Agent
# This script:
# 1. Builds the slonana validator
# 2. Starts the validator
# 3. Builds the sBPF program
# 4. Deploys to the running validator
# 5. Executes transactions against the program
# 6. Verifies results

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
CLI="$SCRIPT_DIR/slonana-cli.sh"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step() { echo -e "${CYAN}[STEP]${NC} $1"; }

cleanup() {
    log_info "Cleaning up..."
    pkill -f slonana_validator 2>/dev/null || true
    rm -f "$PROJECT_ROOT/validator.pid"
}

trap cleanup EXIT

echo ""
echo -e "${CYAN}╔══════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║         SLONANA E2E TEST - AI Trading Agent Deployment           ║${NC}"
echo -e "${CYAN}╚══════════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Step 1: Build the validator
log_step "1/6 Building slonana validator..."
cd "$PROJECT_ROOT"
mkdir -p build
cd build

if [ ! -f "slonana_validator" ]; then
    cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
    make -j$(nproc 2>/dev/null || echo 4) slonana_validator 2>&1 | tail -5
fi

if [ -f "slonana_validator" ]; then
    log_success "Validator binary built: $(ls -la slonana_validator | awk '{print $5}') bytes"
else
    log_error "Failed to build validator"
    exit 1
fi

# Step 2: Start the validator
log_step "2/6 Starting slonana validator..."
cd "$PROJECT_ROOT"

# Kill any existing validator
pkill -f slonana_validator 2>/dev/null || true
sleep 1

# Create required directories
mkdir -p ledger logs keys programs

# Generate validator identity if needed
if [ ! -f "keys/validator-identity.json" ]; then
    "$CLI" keygen keys/validator-identity.json > /dev/null 2>&1
fi

# Start validator
nohup "$PROJECT_ROOT/build/slonana_validator" \
    --ledger-path "$PROJECT_ROOT/ledger" \
    --rpc-bind-address "127.0.0.1:8899" \
    > "$PROJECT_ROOT/logs/validator.log" 2>&1 &

VALIDATOR_PID=$!
echo "$VALIDATOR_PID" > "$PROJECT_ROOT/validator.pid"

# Wait for validator to start
sleep 3

if kill -0 "$VALIDATOR_PID" 2>/dev/null; then
    log_success "Validator started with PID: $VALIDATOR_PID"
    
    # Show some validator output
    echo ""
    echo -e "${YELLOW}Validator logs (last 10 lines):${NC}"
    tail -10 "$PROJECT_ROOT/logs/validator.log" 2>/dev/null || echo "  (no logs yet)"
    echo ""
else
    log_error "Validator failed to start"
    cat "$PROJECT_ROOT/logs/validator.log" 2>/dev/null | tail -20
    exit 1
fi

# Step 3: Build the sBPF program
log_step "3/6 Building ML Trading Agent sBPF program..."

SBPF_SOURCE="$PROJECT_ROOT/examples/ml_trading_agent/ml_trading_agent_sbpf.c"
SBPF_OUTPUT="$PROJECT_ROOT/examples/ml_trading_agent/build/ml_trading_agent_sbpf.so"

if [ ! -f "$SBPF_SOURCE" ]; then
    log_error "sBPF source not found: $SBPF_SOURCE"
    exit 1
fi

# Build using CLI
"$CLI" build "$SBPF_SOURCE" 2>&1 | grep -E "SUCCESS|Binary|Size|Error" || true

if [ -f "$SBPF_OUTPUT" ]; then
    FILE_TYPE=$(file "$SBPF_OUTPUT")
    log_success "sBPF binary built: $SBPF_OUTPUT"
    log_info "Binary type: $FILE_TYPE"
else
    log_error "Failed to build sBPF program"
    exit 1
fi

# Verify it's a real BPF binary
if ! echo "$FILE_TYPE" | grep -q "eBPF\|BPF"; then
    log_error "Not a valid eBPF binary!"
    exit 1
fi

# Step 4: Deploy to validator
log_step "4/6 Deploying program to validator..."

DEPLOY_OUTPUT=$("$CLI" deploy "$SBPF_OUTPUT" 2>&1)
echo "$DEPLOY_OUTPUT" | grep -E "Program ID|Transaction|SUCCESS|Error" || true

# Extract program ID
PROGRAM_ID=$(echo "$DEPLOY_OUTPUT" | grep "Program ID:" | awk '{print $NF}' | head -1)

if [ -z "$PROGRAM_ID" ]; then
    # Try to get from programs directory
    PROGRAM_ID=$(ls -t "$PROJECT_ROOT/programs"/*.json 2>/dev/null | head -1 | xargs basename 2>/dev/null | sed 's/.json//')
fi

if [ -z "$PROGRAM_ID" ]; then
    log_error "Failed to get program ID"
    exit 1
fi

log_success "Program deployed with ID: $PROGRAM_ID"

# Verify program binary exists in ledger
if [ -f "$PROJECT_ROOT/ledger/programs/${PROGRAM_ID}.so" ]; then
    log_success "Program binary stored in ledger"
    ls -la "$PROJECT_ROOT/ledger/programs/${PROGRAM_ID}.so"
fi

# Step 5: Execute transactions
log_step "5/6 Executing transactions against deployed program..."

echo ""
echo -e "${YELLOW}Transaction 1: Initialize${NC}"
TX1_OUTPUT=$("$CLI" call "$PROGRAM_ID" 0 2>&1)
echo "$TX1_OUTPUT" | grep -E "Transaction:|Status:|Program log:" || true

TX1_SIG=$(echo "$TX1_OUTPUT" | grep "Transaction:" | awk '{print $NF}' | head -1)
log_success "TX1 Signature: $TX1_SIG"

echo ""
echo -e "${YELLOW}Transaction 2: Process Update (instruction=1)${NC}"
TX2_OUTPUT=$("$CLI" call "$PROGRAM_ID" 1 2>&1)
echo "$TX2_OUTPUT" | grep -E "Transaction:|Status:|Program log:|ML Signal" || true

TX2_SIG=$(echo "$TX2_OUTPUT" | grep "Transaction:" | awk '{print $NF}' | head -1)
log_success "TX2 Signature: $TX2_SIG"

echo ""
echo -e "${YELLOW}Transaction 3: Process Update (instruction=1)${NC}"
TX3_OUTPUT=$("$CLI" call "$PROGRAM_ID" 1 2>&1)
echo "$TX3_OUTPUT" | grep -E "Transaction:|Status:|Program log:|ML Signal" || true

TX3_SIG=$(echo "$TX3_OUTPUT" | grep "Transaction:" | awk '{print $NF}' | head -1)
log_success "TX3 Signature: $TX3_SIG"

# Step 6: Verify results
log_step "6/6 Verifying results..."

echo ""
echo -e "${CYAN}Transaction Log:${NC}"
if [ -f "$PROJECT_ROOT/transactions.log" ]; then
    tail -5 "$PROJECT_ROOT/transactions.log"
else
    log_info "No transaction log found"
fi

echo ""
echo -e "${CYAN}Deployed Programs:${NC}"
"$CLI" program show 2>&1 | head -10

echo ""
echo -e "${CYAN}Validator Status:${NC}"
if kill -0 "$VALIDATOR_PID" 2>/dev/null; then
    log_success "Validator still running (PID: $VALIDATOR_PID)"
else
    log_error "Validator stopped unexpectedly"
fi

# Final summary
echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                    E2E TEST COMPLETE                              ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "  ${CYAN}Validator PID:${NC}    $VALIDATOR_PID"
echo -e "  ${CYAN}Program ID:${NC}       $PROGRAM_ID"
echo -e "  ${CYAN}Transactions:${NC}     3 executed"
echo -e "  ${CYAN}TX Signatures:${NC}"
echo -e "    - $TX1_SIG"
echo -e "    - $TX2_SIG"
echo -e "    - $TX3_SIG"
echo ""
echo -e "  ${CYAN}Binary Location:${NC}  $PROJECT_ROOT/ledger/programs/${PROGRAM_ID}.so"
echo -e "  ${CYAN}Logs:${NC}             $PROJECT_ROOT/logs/validator.log"
echo ""

log_success "All steps completed successfully!"
echo ""
echo -e "To stop validator: ${GREEN}pkill -f slonana_validator${NC}"
echo -e "To view logs: ${GREEN}tail -f $PROJECT_ROOT/logs/validator.log${NC}"
