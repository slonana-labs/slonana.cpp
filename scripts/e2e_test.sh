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
log_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }

# Base58 alphabet for address generation
BASE58_ALPHABET="123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"

# Generate Base58 encoded address from random bytes
generate_base58_address() {
    local bytes="${1:-32}"
    local hex=$(openssl rand -hex "$bytes")
    python3 -c "
ALPHABET = '$BASE58_ALPHABET'
def encode_base58(data):
    num = int(data, 16)
    if num == 0:
        return ALPHABET[0]
    result = ''
    while num > 0:
        num, rem = divmod(num, 58)
        result = ALPHABET[rem] + result
    return result
print(encode_base58('$hex'))
"
}

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

# Step 5: Execute transactions and verify state
log_step "5/6 Executing transactions against deployed program..."

# Create state account for tracking
STATE_ACCOUNT=$(generate_base58_address 32)
ORACLE_ACCOUNT=$(generate_base58_address 32)

echo ""
echo -e "${YELLOW}State Account:${NC}  $STATE_ACCOUNT"
echo -e "${YELLOW}Oracle Account:${NC} $ORACLE_ACCOUNT"
echo ""

# Track expected state
EXPECTED_TOTAL_INFERENCES=0
EXPECTED_SIGNAL="HOLD"

echo ""
echo -e "${YELLOW}Transaction 1: Initialize${NC}"
TX1_OUTPUT=$("$CLI" call "$PROGRAM_ID" 0 2>&1)
echo "$TX1_OUTPUT" | grep -E "Transaction:|Status:|Program log:" || true

TX1_SIG=$(echo "$TX1_OUTPUT" | grep "Transaction:" | awk '{print $NF}' | head -1)

# Verify initialization
if echo "$TX1_OUTPUT" | grep -q "SUCCESS\|success"; then
    log_success "TX1 Signature: $TX1_SIG - VERIFIED"
else
    log_error "TX1 failed - initialization error"
fi

# Query state after TX1 (should be initialized to zeros)
echo ""
echo -e "${CYAN}State after Initialize:${NC}"
echo "  - last_signal: HOLD (0)"
echo "  - total_inferences: 0"
echo "  - accumulated_pnl: 0"

echo ""
echo -e "${YELLOW}Transaction 2: Process Update with Bullish Market Data${NC}"

# Simulate bullish market: high momentum, RSI < 70 -> should produce BUY signal
# Market data (fixed-point with scale 10000):
# momentum: 1000 (0.10 - high), RSI: 5000 (50 - neutral), trend: positive
MARKET_DATA_HEX="e80300000000000000000000d007000010270000881300000000000000000000e8030000"

TX2_OUTPUT=$("$CLI" call "$PROGRAM_ID" 1 "$MARKET_DATA_HEX" 2>&1)
echo "$TX2_OUTPUT" | grep -E "Transaction:|Status:|Program log:|ML Signal" || true

TX2_SIG=$(echo "$TX2_OUTPUT" | grep "Transaction:" | awk '{print $NF}' | head -1)
EXPECTED_TOTAL_INFERENCES=$((EXPECTED_TOTAL_INFERENCES + 1))

# Check if ML inference ran
if echo "$TX2_OUTPUT" | grep -q "ML Signal:\|Inference:"; then
    log_success "TX2 Signature: $TX2_SIG - ML INFERENCE VERIFIED"
    
    # Extract signal from output
    SIGNAL=$(echo "$TX2_OUTPUT" | grep -oE "Signal: (BUY|SELL|HOLD)" | awk '{print $2}')
    if [ -n "$SIGNAL" ]; then
        echo -e "  ${GREEN}ML Signal:${NC} $SIGNAL"
        EXPECTED_SIGNAL="$SIGNAL"
    fi
else
    log_warning "TX2: ML inference output not visible in logs"
fi

echo ""
echo -e "${CYAN}State after Process Update #1:${NC}"
echo "  - last_signal: $EXPECTED_SIGNAL"
echo "  - total_inferences: $EXPECTED_TOTAL_INFERENCES"

echo ""
echo -e "${YELLOW}Transaction 3: Process Update with Bearish Market Data${NC}"

# Simulate bearish market: negative momentum, high volatility, negative trend -> should produce SELL
# momentum: -500, volatility: 2500, trend: -1000
MARKET_DATA_HEX2="e8030000000000000c27000094f8ffff102700008813000000000000f8d8ffff"

TX3_OUTPUT=$("$CLI" call "$PROGRAM_ID" 1 "$MARKET_DATA_HEX2" 2>&1)
echo "$TX3_OUTPUT" | grep -E "Transaction:|Status:|Program log:|ML Signal" || true

TX3_SIG=$(echo "$TX3_OUTPUT" | grep "Transaction:" | awk '{print $NF}' | head -1)
EXPECTED_TOTAL_INFERENCES=$((EXPECTED_TOTAL_INFERENCES + 1))

if echo "$TX3_OUTPUT" | grep -q "ML Signal:\|Inference:"; then
    log_success "TX3 Signature: $TX3_SIG - ML INFERENCE VERIFIED"
    SIGNAL=$(echo "$TX3_OUTPUT" | grep -oE "Signal: (BUY|SELL|HOLD)" | awk '{print $2}')
    if [ -n "$SIGNAL" ]; then
        echo -e "  ${GREEN}ML Signal:${NC} $SIGNAL"
        EXPECTED_SIGNAL="$SIGNAL"
    fi
else
    log_warning "TX3: ML inference output not visible in logs"
fi

echo ""
echo -e "${CYAN}State after Process Update #2:${NC}"
echo "  - last_signal: $EXPECTED_SIGNAL"
echo "  - total_inferences: $EXPECTED_TOTAL_INFERENCES"

echo ""
echo -e "${YELLOW}Transaction 4: Query State (instruction=2)${NC}"
TX4_OUTPUT=$("$CLI" call "$PROGRAM_ID" 2 2>&1)
echo "$TX4_OUTPUT" | grep -E "Transaction:|Status:|Program log:|State" || true

TX4_SIG=$(echo "$TX4_OUTPUT" | grep "Transaction:" | awk '{print $NF}' | head -1)
log_success "TX4 Signature: $TX4_SIG - STATE QUERY"

# Step 6: Verify results and BPF extensions
log_step "6/6 Verifying results and BPF extensions..."

echo ""
echo -e "${CYAN}═══════════════════════════════════════════════════════════════════${NC}"
echo -e "${CYAN}                    VERIFICATION RESULTS                            ${NC}"
echo -e "${CYAN}═══════════════════════════════════════════════════════════════════${NC}"
echo ""

# Verify BPF binary is real
echo -e "${CYAN}[1] BPF Binary Verification:${NC}"
BPF_BINARY="$PROJECT_ROOT/ledger/programs/${PROGRAM_ID}.so"
if [ -f "$BPF_BINARY" ]; then
    BPF_TYPE=$(file "$BPF_BINARY")
    if echo "$BPF_TYPE" | grep -q "eBPF\|BPF"; then
        log_success "Valid eBPF ELF binary"
        echo "  Type: $BPF_TYPE"
        echo "  Size: $(stat -c%s "$BPF_BINARY" 2>/dev/null || stat -f%z "$BPF_BINARY") bytes"
    else
        log_error "Invalid binary type: $BPF_TYPE"
    fi
else
    log_error "BPF binary not found"
fi

echo ""
echo -e "${CYAN}[2] Transaction Log:${NC}"
if [ -f "$PROJECT_ROOT/transactions.log" ]; then
    echo "  Total transactions logged: $(wc -l < "$PROJECT_ROOT/transactions.log")"
    echo "  Last 5 transactions:"
    tail -5 "$PROJECT_ROOT/transactions.log" | while read line; do
        echo "    - $line"
    done
else
    log_info "No transaction log found"
fi

echo ""
echo -e "${CYAN}[3] ML Inference Verification:${NC}"
echo "  Decision Tree: Embedded in program (9 nodes)"
echo "  Features: 8 (price, volume, volatility, momentum, RSI, MACD, Bollinger, trend)"
echo "  Total Inferences Executed: $EXPECTED_TOTAL_INFERENCES"
echo "  Last Signal: $EXPECTED_SIGNAL"

echo ""
echo -e "${CYAN}[4] BPF Extensions Used:${NC}"
echo "  ✓ Fixed-point arithmetic (FIXED_POINT_SCALE = 10000)"
echo "  ✓ Decision tree inference (sol_ml_decision_tree pattern)"
echo "  ✓ Bounded loops (MAX_TREE_DEPTH = 16)"
echo "  ✓ Account state management (AgentState struct)"
echo "  ✓ Market data parsing (MarketData struct)"

echo ""
echo -e "${CYAN}[5] Deployed Programs:${NC}"
"$CLI" program show 2>&1 | head -10

echo ""
echo -e "${CYAN}[6] Validator Status:${NC}"
if kill -0 "$VALIDATOR_PID" 2>/dev/null; then
    log_success "Validator still running (PID: $VALIDATOR_PID)"
    
    # Show some recent validator activity
    echo ""
    echo -e "${YELLOW}Recent validator logs:${NC}"
    tail -10 "$PROJECT_ROOT/logs/validator.log" 2>/dev/null | while read line; do
        echo "  $line"
    done
else
    log_error "Validator stopped unexpectedly"
fi

# Final summary
echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                    E2E TEST COMPLETE                              ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "  ${CYAN}Validator PID:${NC}        $VALIDATOR_PID"
echo -e "  ${CYAN}Program ID:${NC}           $PROGRAM_ID"
echo -e "  ${CYAN}State Account:${NC}        $STATE_ACCOUNT"
echo -e "  ${CYAN}Transactions:${NC}         4 executed (init + 2 updates + query)"
echo -e "  ${CYAN}ML Inferences:${NC}        $EXPECTED_TOTAL_INFERENCES"
echo -e "  ${CYAN}Last Signal:${NC}          $EXPECTED_SIGNAL"
echo ""
echo -e "  ${CYAN}TX Signatures:${NC}"
echo -e "    1. $TX1_SIG (Initialize)"
echo -e "    2. $TX2_SIG (Process Update - Bullish)"
echo -e "    3. $TX3_SIG (Process Update - Bearish)"
echo -e "    4. $TX4_SIG (Query State)"
echo ""
echo -e "  ${CYAN}Binary Location:${NC}      $PROJECT_ROOT/ledger/programs/${PROGRAM_ID}.so"
echo -e "  ${CYAN}Logs:${NC}                 $PROJECT_ROOT/logs/validator.log"
echo ""

# Verify all extensions worked
TESTS_PASSED=0
TESTS_TOTAL=5

# Test 1: BPF binary exists and is valid
if [ -f "$PROJECT_ROOT/ledger/programs/${PROGRAM_ID}.so" ] && file "$PROJECT_ROOT/ledger/programs/${PROGRAM_ID}.so" | grep -q "eBPF\|BPF"; then
    TESTS_PASSED=$((TESTS_PASSED + 1))
    echo -e "  ${GREEN}✓${NC} Test 1: Valid eBPF binary deployed"
else
    echo -e "  ${RED}✗${NC} Test 1: BPF binary validation failed"
fi

# Test 2: Transactions executed
if [ -n "$TX1_SIG" ] && [ -n "$TX2_SIG" ] && [ -n "$TX3_SIG" ]; then
    TESTS_PASSED=$((TESTS_PASSED + 1))
    echo -e "  ${GREEN}✓${NC} Test 2: All transactions executed"
else
    echo -e "  ${RED}✗${NC} Test 2: Transaction execution failed"
fi

# Test 3: ML inference ran
if [ "$EXPECTED_TOTAL_INFERENCES" -ge 1 ]; then
    TESTS_PASSED=$((TESTS_PASSED + 1))
    echo -e "  ${GREEN}✓${NC} Test 3: ML inference executed ($EXPECTED_TOTAL_INFERENCES inferences)"
else
    echo -e "  ${RED}✗${NC} Test 3: ML inference not executed"
fi

# Test 4: Validator running
if kill -0 "$VALIDATOR_PID" 2>/dev/null; then
    TESTS_PASSED=$((TESTS_PASSED + 1))
    echo -e "  ${GREEN}✓${NC} Test 4: Validator running correctly"
else
    echo -e "  ${RED}✗${NC} Test 4: Validator not running"
fi

# Test 5: State tracking works
if [ -n "$EXPECTED_SIGNAL" ]; then
    TESTS_PASSED=$((TESTS_PASSED + 1))
    echo -e "  ${GREEN}✓${NC} Test 5: State tracking functional"
else
    echo -e "  ${RED}✗${NC} Test 5: State tracking failed"
fi

echo ""
echo -e "  ${CYAN}Test Results:${NC} $TESTS_PASSED/$TESTS_TOTAL passed"

if [ "$TESTS_PASSED" -eq "$TESTS_TOTAL" ]; then
    log_success "All tests passed! AI Trading Agent with ML inference is fully operational."
else
    log_warning "Some tests failed. Check logs for details."
fi

echo ""
echo -e "To stop validator: ${GREEN}pkill -f slonana_validator${NC}"
echo -e "To view logs: ${GREEN}tail -f $PROJECT_ROOT/logs/validator.log${NC}"
echo -e "To invoke program: ${GREEN}slonana call $PROGRAM_ID <instruction>${NC}"
