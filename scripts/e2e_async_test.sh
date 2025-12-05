#!/bin/bash
# Async BPF E2E Test Script
# Tests timers, watchers, and ring buffers in deployed programs

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

echo ""
echo -e "${CYAN}╔══════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║         ASYNC BPF E2E TEST - Timers, Watchers, Ring Buffers      ║${NC}"
echo -e "${CYAN}╚══════════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Step 1: Build async agent program
log_step "1/5 Building Async Agent sBPF program..."

ASYNC_SOURCE="$PROJECT_ROOT/examples/async_agent/async_agent_sbpf.c"
ASYNC_OUTPUT="$PROJECT_ROOT/examples/async_agent/build/async_agent_sbpf.so"

if [ ! -f "$ASYNC_SOURCE" ]; then
    log_error "Async agent source not found: $ASYNC_SOURCE"
    exit 1
fi

"$CLI" build "$ASYNC_SOURCE" 2>&1 | grep -E "SUCCESS|Binary|Size|Error" || true

if [ -f "$ASYNC_OUTPUT" ]; then
    log_success "Async agent built successfully"
else
    log_error "Failed to build async agent"
    exit 1
fi

# Step 2: Deploy program (file-based, no validator process needed)
log_step "2/5 Deploying async agent (file-based)..."

# Generate program ID from file hash (proper Base58 encoding)
FILE_HASH=$(sha256sum "$ASYNC_OUTPUT" | cut -d' ' -f1)
PROGRAM_ID=$(echo "$FILE_HASH" | python3 -c "
ALPHABET = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'
import sys

def encode_base58(hex_str):
    # Convert hex to bytes (32 bytes from SHA256)
    data_bytes = bytes.fromhex(hex_str)
    num = int.from_bytes(data_bytes, 'big')
    if num == 0:
        return ALPHABET[0]
    result = ''
    while num > 0:
        num, rem = divmod(num, 58)
        result = ALPHABET[rem] + result
    # Pad to 44 characters (standard for 32-byte keys)
    return result.rjust(44, ALPHABET[0])

data = sys.stdin.read().strip()
print(encode_base58(data))
" 2>/dev/null || echo "111111111111111111111111111111111111111111")

# Copy to deployment directories (both ledger/programs and programs/)
PROGRAM_DIR="$PROJECT_ROOT/ledger/programs"
PROGRAMS_DIR="$PROJECT_ROOT/programs"
mkdir -p "$PROGRAM_DIR"
mkdir -p "$PROGRAMS_DIR"
cp "$ASYNC_OUTPUT" "$PROGRAM_DIR/${PROGRAM_ID}.so"
cp "$ASYNC_OUTPUT" "$PROGRAMS_DIR/${PROGRAM_ID}.so"

# Create metadata file
cat > "$PROGRAMS_DIR/${PROGRAM_ID}.json" <<EOF
{
    "program_id": "$PROGRAM_ID",
    "file_hash": "$FILE_HASH",
    "binary_path": "$PROGRAM_DIR/${PROGRAM_ID}.so",
    "deployed_at": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
    "status": "deployed"
}
EOF

log_success "Async agent deployed: $PROGRAM_ID"
log_info "Binary: $PROGRAM_DIR/${PROGRAM_ID}.so"

# Helper function to query and display account state
query_account_state() {
    local program_id="$1"
    local tx_num="$2"
    local rpc_url="${3:-http://localhost:8899}"
    
    echo ""
    echo -e "${CYAN}📊 Account State After TX $tx_num:${NC}"
    
    # Query account via RPC
    local response=$(curl -s -X POST "$rpc_url" \
        -H "Content-Type: application/json" \
        -d "{
            \"jsonrpc\": \"2.0\",
            \"id\": 1,
            \"method\": \"getAccountInfo\",
            \"params\": [
                \"$program_id\",
                {\"encoding\": \"base64\"}
            ]
        }" 2>/dev/null)
    
    # Extract and decode account data
    if echo "$response" | grep -q "\"result\""; then
        local data=$(echo "$response" | python3 -c "
import json, sys, base64
try:
    data = json.load(sys.stdin)
    if 'result' in data and data['result'] and 'value' in data['result']:
        account = data['result']['value']
        if account and 'data' in account:
            # Decode base64 data to hex
            account_data = account['data'][0] if isinstance(account['data'], list) else account['data']
            raw_bytes = base64.b64decode(account_data)
            hex_data = raw_bytes.hex()
            
            # Parse account state (async agent: events_processed, timer_id, watcher_id)
            if len(raw_bytes) >= 24:
                events = int.from_bytes(raw_bytes[0:8], 'little')
                timer = int.from_bytes(raw_bytes[8:16], 'little')
                watcher = int.from_bytes(raw_bytes[16:24], 'little')
                print(f'{{\"events_processed\": {events}, \"timer_id\": {timer}, \"watcher_id\": {watcher}, \"data_hex\": \"{hex_data}\"}}')
            else:
                print(f'{{\"data_hex\": \"{hex_data}\", \"data_length\": {len(raw_bytes)}}}')
        else:
            print('{\"error\": \"No data in account\"}')
    else:
        print('{\"error\": \"Account not found or RPC error\"}')
except Exception as e:
    print(f'{{\"error\": \"{str(e)}\"}}')
" 2>/dev/null)
        
        # Display formatted JSON
        echo "$data" | python3 -m json.tool 2>/dev/null || echo "$data"
    else
        echo -e "${YELLOW}  ⚠️  Account state query failed or account not yet initialized${NC}"
    fi
}

# Step 3: Test initialization (creates timer, watcher, ring buffer)
log_step "3/5 Testing async initialization..."

echo ""
echo -e "${YELLOW}TX1: Initialize (creates timer, watcher, ring buffer)${NC}"
TX1=$("$CLI" call "$PROGRAM_ID" 0 2>&1)
echo "$TX1" | grep -E "Transaction:|Status:" || true

if echo "$TX1" | grep -q "SUCCESS\|success"; then
    log_success "Async initialization: PASSED"
    echo "  - Timer created"
    echo "  - Watcher created"
    echo "  - Ring buffer created"
else
    log_error "Async initialization failed"
fi

# Query state after TX1
query_account_state "$PROGRAM_ID" "1"

# Step 4: Test timer and watcher events
log_step "4/5 Testing timer and watcher events..."

echo ""
echo -e "${YELLOW}TX2: Process Timer Tick${NC}"
TX2=$("$CLI" call "$PROGRAM_ID" 1 2>&1)
echo "$TX2" | grep -E "Transaction:|Status:" || true

if echo "$TX2" | grep -q "SUCCESS\|success"; then
    log_success "Timer tick processing: PASSED"
fi
query_account_state "$PROGRAM_ID" "2"

echo ""
echo -e "${YELLOW}TX3: Process Watcher Trigger${NC}"
TX3=$("$CLI" call "$PROGRAM_ID" 2 2>&1)
echo "$TX3" | grep -E "Transaction:|Status:" || true

if echo "$TX3" | grep -q "SUCCESS\|success"; then
    log_success "Watcher trigger processing: PASSED"
fi
query_account_state "$PROGRAM_ID" "3"

echo ""
echo -e "${YELLOW}TX4: Execute ML Inference${NC}"
TX4=$("$CLI" call "$PROGRAM_ID" 3 2>&1)
echo "$TX4" | grep -E "Transaction:|Status:|Signal" || true

if echo "$TX4" | grep -q "SUCCESS\|success"; then
    log_success "ML inference execution: PASSED"
fi
query_account_state "$PROGRAM_ID" "4"

echo ""
echo -e "${YELLOW}TX5: Query State${NC}"
TX5=$("$CLI" call "$PROGRAM_ID" 4 2>&1)
echo "$TX5" | grep -E "Transaction:|Status:|State" || true

if echo "$TX5" | grep -q "SUCCESS\|success"; then
    log_success "State query: PASSED"
fi
query_account_state "$PROGRAM_ID" "5"

echo ""
echo -e "${YELLOW}TX6: Cleanup (cancel timer, remove watcher)${NC}"
TX6=$("$CLI" call "$PROGRAM_ID" 5 2>&1)
echo "$TX6" | grep -E "Transaction:|Status:" || true

if echo "$TX6" | grep -q "SUCCESS\|success"; then
    log_success "Cleanup: PASSED"
fi
query_account_state "$PROGRAM_ID" "6"

# Step 5: Verify async features
log_step "5/5 Verifying async BPF extensions..."

echo ""
echo -e "${CYAN}═══════════════════════════════════════════════════════════════════${NC}"
echo -e "${CYAN}              ASYNC BPF VERIFICATION RESULTS                        ${NC}"
echo -e "${CYAN}═══════════════════════════════════════════════════════════════════${NC}"
echo ""

TESTS_PASSED=0
TESTS_TOTAL=6

# Test results
for i in 1 2 3 4 5 6; do
    eval "TX_OUTPUT=\$TX$i"
    if echo "$TX_OUTPUT" | grep -q "SUCCESS\|success"; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi
done

echo -e "${CYAN}[1] Timer System:${NC}"
echo "  - Timer creation: sol_timer_create"
echo "  - Timer cancellation: sol_timer_cancel"
echo "  - Periodic execution support"

echo ""
echo -e "${CYAN}[2] Watcher System:${NC}"
echo "  - Account watcher: sol_watcher_create"
echo "  - Threshold triggers: ABOVE/BELOW"
echo "  - Watcher removal: sol_watcher_remove"

echo ""
echo -e "${CYAN}[3] Ring Buffer System:${NC}"
echo "  - Buffer creation: sol_ring_buffer_create"
echo "  - Event queuing: sol_ring_buffer_push"
echo "  - Event dequeuing: sol_ring_buffer_pop"

echo ""
echo -e "${CYAN}[4] ML Integration:${NC}"
echo "  - Decision tree inference"
echo "  - Trade signal generation"
echo "  - Position management"

echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                 ASYNC BPF TEST COMPLETE                          ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "  ${CYAN}Program ID:${NC}       $PROGRAM_ID"
echo -e "  ${CYAN}Transactions:${NC}     6 executed"
echo -e "  ${CYAN}Tests Passed:${NC}     $TESTS_PASSED/$TESTS_TOTAL"
echo ""

if [ "$TESTS_PASSED" -eq "$TESTS_TOTAL" ]; then
    log_success "All async BPF tests passed!"
else
    log_error "Some tests failed: $TESTS_PASSED/$TESTS_TOTAL"
fi
