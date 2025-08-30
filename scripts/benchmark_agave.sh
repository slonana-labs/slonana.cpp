#!/usr/bin/env bash
set -euo pipefail

# Agave Validator Benchmark Script
# Automated benchmarking script for Anza/Agave validator
# Provides comprehensive performance testing with real transaction processing

SCRIPT_NAME="$(basename "$0")"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Default configuration
LEDGER_DIR=""
RESULTS_DIR=""
VALIDATOR_BIN="agave-validator"
LEDGER_TOOL_BIN="agave-ledger-tool"
TEST_DURATION=60
RPC_PORT=8899
GOSSIP_PORT=8001
IDENTITY_FILE=""
BOOTSTRAP_ONLY=false
VERBOSE=false

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1" >&2
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1" >&2
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1" >&2
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

log_verbose() {
    if [[ "$VERBOSE" == true ]]; then
        echo -e "${BLUE}[VERBOSE]${NC} $1" >&2
    fi
}

show_help() {
    cat << EOF
$SCRIPT_NAME - Agave Validator Benchmark Script

USAGE:
    $SCRIPT_NAME --ledger LEDGER_DIR --results RESULTS_DIR [OPTIONS]

REQUIRED ARGUMENTS:
    --ledger LEDGER_DIR      Directory for validator ledger data
    --results RESULTS_DIR    Directory to store benchmark results

OPTIONAL ARGUMENTS:
    --validator-bin PATH     Path to agave-validator binary (default: agave-validator)
    --ledger-tool PATH       Path to agave-ledger-tool binary (default: agave-ledger-tool)
    --identity KEYFILE       Validator identity keypair file (auto-generated if not provided)
    --test-duration SECONDS  Benchmark test duration in seconds (default: 60)
    --rpc-port PORT          RPC port for validator (default: 8899)
    --gossip-port PORT       Gossip port for validator (default: 8001)
    --bootstrap-only         Only bootstrap validator, don't run performance tests
    --verbose                Enable verbose logging
    --help                   Show this help message

EXAMPLES:
    # Basic benchmark
    $SCRIPT_NAME --ledger /tmp/agave_ledger --results /tmp/agave_results

    # Custom test duration and ports
    $SCRIPT_NAME --ledger /tmp/ledger --results /tmp/results --test-duration 120 --rpc-port 9899

    # Bootstrap only (for setup testing)
    $SCRIPT_NAME --ledger /tmp/ledger --results /tmp/results --bootstrap-only

    # Use custom binary paths
    $SCRIPT_NAME --ledger /tmp/ledger --results /tmp/results \\
        --validator-bin /usr/local/bin/agave-validator \\
        --ledger-tool /usr/local/bin/agave-ledger-tool

EXIT CODES:
    0    Success
    1    General error
    2    Invalid arguments
    3    Missing dependencies
    4    Validator startup failure
    5    Benchmark execution failure
EOF
}

# Parse command line arguments
parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --ledger)
                LEDGER_DIR="$2"
                shift 2
                ;;
            --results)
                RESULTS_DIR="$2"
                shift 2
                ;;
            --validator-bin)
                VALIDATOR_BIN="$2"
                shift 2
                ;;
            --ledger-tool)
                LEDGER_TOOL_BIN="$2"
                shift 2
                ;;
            --identity)
                IDENTITY_FILE="$2"
                shift 2
                ;;
            --test-duration)
                TEST_DURATION="$2"
                shift 2
                ;;
            --rpc-port)
                RPC_PORT="$2"
                shift 2
                ;;
            --gossip-port)
                GOSSIP_PORT="$2"
                shift 2
                ;;
            --bootstrap-only)
                BOOTSTRAP_ONLY=true
                shift
                ;;
            --verbose)
                VERBOSE=true
                shift
                ;;
            --help)
                show_help
                exit 0
                ;;
            *)
                log_error "Unknown argument: $1"
                show_help
                exit 2
                ;;
        esac
    done

    # Validate required arguments
    if [[ -z "$LEDGER_DIR" ]]; then
        log_error "Missing required argument: --ledger"
        exit 2
    fi

    if [[ -z "$RESULTS_DIR" ]]; then
        log_error "Missing required argument: --results"
        exit 2
    fi

    # Validate test duration
    if ! [[ "$TEST_DURATION" =~ ^[0-9]+$ ]] || [[ "$TEST_DURATION" -lt 10 ]]; then
        log_error "Test duration must be a positive integer >= 10 seconds"
        exit 2
    fi

    # Create absolute paths
    LEDGER_DIR="$(realpath "$LEDGER_DIR")"
    RESULTS_DIR="$(realpath "$RESULTS_DIR")"
}

# Check dependencies
check_dependencies() {
    log_info "Checking dependencies..."

    # Check for required binaries
    if ! command -v "$VALIDATOR_BIN" &> /dev/null; then
        log_error "Agave validator binary not found: $VALIDATOR_BIN"
        log_error "Install with: curl --proto '=https' --tlsv1.2 -sSfL https://solana-install.solana.workers.dev | bash"
        exit 3
    fi

    if ! command -v solana-keygen &> /dev/null; then
        log_error "solana-keygen not found. Please install Solana CLI tools."
        exit 3
    fi

    if ! command -v solana &> /dev/null; then
        log_error "solana CLI not found. Please install Solana CLI tools."
        exit 3
    fi

    # Check for system utilities
    for util in curl jq bc; do
        if ! command -v "$util" &> /dev/null; then
            log_error "Required utility not found: $util"
            exit 3
        fi
    done

    log_success "All dependencies available"
}

# Setup validator environment
setup_validator() {
    log_info "Setting up Agave validator environment..."

    # Create directories
    mkdir -p "$LEDGER_DIR"
    mkdir -p "$RESULTS_DIR"

    # Generate identity keypair if not provided
    if [[ -z "$IDENTITY_FILE" ]]; then
        IDENTITY_FILE="$RESULTS_DIR/validator-keypair.json"
        log_verbose "Generating validator identity keypair: $IDENTITY_FILE"
        solana-keygen new --no-bip39-passphrase --silent --outfile "$IDENTITY_FILE"
    fi

    # Generate bootstrap validator genesis
    log_verbose "Creating genesis configuration..."
    solana-genesis --bootstrap-validator "$IDENTITY_FILE" "$LEDGER_DIR" 2>/dev/null

    log_success "Validator environment setup complete"
}

# Start validator
start_validator() {
    log_info "Starting Agave validator..."

    # Dynamic port range
    local port_range=$((GOSSIP_PORT + 1))-$((GOSSIP_PORT + 20))

    log_verbose "Validator configuration:"
    log_verbose "  Identity: $IDENTITY_FILE"
    log_verbose "  Ledger: $LEDGER_DIR"
    log_verbose "  RPC Port: $RPC_PORT"
    log_verbose "  Gossip Port: $GOSSIP_PORT"
    log_verbose "  Port Range: $port_range"

    # Start validator in background
    "$VALIDATOR_BIN" \
        --identity "$IDENTITY_FILE" \
        --ledger "$LEDGER_DIR" \
        --rpc-port "$RPC_PORT" \
        --gossip-port "$GOSSIP_PORT" \
        --dynamic-port-range "$port_range" \
        --enable-rpc-transaction-history \
        --log "$RESULTS_DIR/agave_validator.log" &

    VALIDATOR_PID=$!
    log_verbose "Validator started with PID: $VALIDATOR_PID"

    # Save PID for cleanup
    echo "$VALIDATOR_PID" > "$RESULTS_DIR/validator.pid"

    # Wait for validator to start
    log_info "Waiting for validator to become ready..."
    local timeout=120
    local wait_time=0

    while [[ $wait_time -lt $timeout ]]; do
        if curl -s "http://localhost:$RPC_PORT/health" > /dev/null 2>&1; then
            log_success "Validator is ready!"
            return 0
        fi
        sleep 5
        wait_time=$((wait_time + 5))
        log_verbose "Waiting... ($wait_time/${timeout}s)"
    done

    log_error "Validator failed to start within ${timeout}s timeout"
    cleanup_validator
    exit 4
}

# Run performance benchmarks
run_benchmarks() {
    if [[ "$BOOTSTRAP_ONLY" == true ]]; then
        log_info "Bootstrap-only mode, skipping performance tests"
        return 0
    fi

    log_info "Running Agave validator performance benchmarks..."

    # Record initial system state
    record_system_state "start"

    # Test RPC response times
    test_rpc_performance

    # Test transaction throughput
    test_transaction_throughput

    # Record final system state
    record_system_state "end"

    # Generate results summary
    generate_results_summary
}

# Record system state
record_system_state() {
    local state="$1"
    local memory_mb=$(free -m | awk '/^Mem:/{print $3}')
    local cpu_percent=$(top -bn1 | grep "Cpu(s)" | awk '{print $2}' | sed 's/%us,//' | cut -d'%' -f1)

    cat > "$RESULTS_DIR/system_${state}.json" << EOF
{
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "memory_mb": $memory_mb,
  "cpu_percent": ${cpu_percent:-0},
  "validator_pid": ${VALIDATOR_PID:-0}
}
EOF

    log_verbose "System state recorded: $state"
}

# Test RPC performance
test_rpc_performance() {
    log_info "Testing RPC response times..."

    local start_time=$(date +%s%N)
    local rpc_calls=100

    for ((i=1; i<=rpc_calls; i++)); do
        curl -s -X POST "http://localhost:$RPC_PORT" \
            -H "Content-Type: application/json" \
            -d '{"jsonrpc":"2.0","id":1,"method":"getVersion"}' \
            > /dev/null
    done

    local end_time=$(date +%s%N)
    local duration_ns=$((end_time - start_time))
    local avg_latency_ms=$((duration_ns / 1000000 / rpc_calls))

    echo "$avg_latency_ms" > "$RESULTS_DIR/rpc_latency_ms.txt"
    log_success "RPC latency test completed: ${avg_latency_ms}ms average"
}

# Test transaction throughput
test_transaction_throughput() {
    log_info "Testing transaction throughput..."

    # Configure Solana CLI
    export PATH="$HOME/.local/share/solana/install/active_release/bin:$PATH"
    solana config set --url "http://localhost:$RPC_PORT" > /dev/null

    # Generate keypairs
    local sender_keypair="$RESULTS_DIR/sender-keypair.json"
    local recipient_keypair="$RESULTS_DIR/recipient-keypair.json"

    solana-keygen new --no-bip39-passphrase --silent --outfile "$sender_keypair"
    solana-keygen new --no-bip39-passphrase --silent --outfile "$recipient_keypair"

    # Airdrop SOL to sender
    log_verbose "Requesting airdrop..."
    local airdrop_attempts=0
    while [[ $airdrop_attempts -lt 5 ]]; do
        if solana airdrop 100 --keypair "$sender_keypair" > /dev/null 2>&1; then
            break
        fi
        ((airdrop_attempts++))
        sleep 2
    done

    # Verify balance
    local balance
    balance=$(solana balance --keypair "$sender_keypair" | awk '{print $1}')
    
    if (( $(echo "$balance < 10" | bc -l) )); then
        log_warning "Insufficient balance for throughput test: ${balance} SOL"
        echo "0" > "$RESULTS_DIR/effective_tps.txt"
        echo "0" > "$RESULTS_DIR/successful_transactions.txt"
        return 0
    fi

    log_verbose "Starting transaction throughput test for ${TEST_DURATION}s..."

    # Run transaction test
    local txn_count=0
    local success_count=0
    local start_time=$(date +%s)
    local recipient_pubkey
    recipient_pubkey=$(solana-keygen pubkey "$recipient_keypair")

    while [[ $(($(date +%s) - start_time)) -lt $TEST_DURATION ]]; do
        for ((i=1; i<=5; i++)); do
            if solana transfer "$recipient_pubkey" 0.001 \
                --keypair "$sender_keypair" \
                --allow-unfunded-recipient \
                --fee-payer "$sender_keypair" \
                --no-wait > /dev/null 2>&1; then
                ((success_count++))
            fi
            ((txn_count++))
        done
        sleep 0.2
    done

    local actual_duration=$(($(date +%s) - start_time))
    actual_duration=${actual_duration:-1}  # Prevent division by zero
    local effective_tps=$((success_count / actual_duration))

    # Save results
    echo "$effective_tps" > "$RESULTS_DIR/effective_tps.txt"
    echo "$success_count" > "$RESULTS_DIR/successful_transactions.txt"
    echo "$txn_count" > "$RESULTS_DIR/submitted_requests.txt"

    log_success "Transaction throughput test completed"
    log_info "Effective TPS: $effective_tps"
    log_info "Successful transactions: $success_count"
}

# Generate comprehensive results summary
generate_results_summary() {
    log_info "Generating benchmark results summary..."

    # Read metrics
    local rpc_latency_ms=$(cat "$RESULTS_DIR/rpc_latency_ms.txt" 2>/dev/null || echo "0")
    local effective_tps=$(cat "$RESULTS_DIR/effective_tps.txt" 2>/dev/null || echo "0")
    local successful_transactions=$(cat "$RESULTS_DIR/successful_transactions.txt" 2>/dev/null || echo "0")
    local submitted_requests=$(cat "$RESULTS_DIR/submitted_requests.txt" 2>/dev/null || echo "0")

    # Get resource usage
    local memory_usage_mb="0"
    local cpu_usage="0"
    
    if [[ -n "${VALIDATOR_PID:-}" ]] && kill -0 "$VALIDATOR_PID" 2>/dev/null; then
        memory_usage_mb=$(ps -p "$VALIDATOR_PID" -o rss= 2>/dev/null | awk '{print $1/1024}' || echo "0")
        cpu_usage=$(ps -p "$VALIDATOR_PID" -o %cpu= 2>/dev/null | awk '{print $1}' || echo "0")
    fi

    # Generate JSON results
    cat > "$RESULTS_DIR/benchmark_results.json" << EOF
{
  "validator_type": "agave",
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "test_duration_seconds": $TEST_DURATION,
  "rpc_latency_ms": $rpc_latency_ms,
  "effective_tps": $effective_tps,
  "submitted_requests": $submitted_requests,
  "successful_transactions": $successful_transactions,
  "memory_usage_mb": $memory_usage_mb,
  "cpu_usage_percent": $cpu_usage,
  "system_info": {
    "cores": $(nproc),
    "total_memory_mb": $(free -m | awk '/^Mem:/{print $2}')
  }
}
EOF

    log_success "Benchmark results saved to: $RESULTS_DIR/benchmark_results.json"
    
    # Display summary
    echo ""
    echo "=== Agave Validator Benchmark Results ==="
    echo "RPC Latency: ${rpc_latency_ms}ms"
    echo "Effective TPS: $effective_tps"
    echo "Successful Transactions: $successful_transactions"
    echo "Memory Usage: ${memory_usage_mb}MB"
    echo "CPU Usage: ${cpu_usage}%"
    echo "========================================"
}

# Cleanup validator process
cleanup_validator() {
    if [[ -f "$RESULTS_DIR/validator.pid" ]]; then
        local pid
        pid=$(cat "$RESULTS_DIR/validator.pid")
        
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            log_info "Stopping validator (PID: $pid)..."
            kill "$pid" 2>/dev/null || true
            sleep 5
            
            # Force kill if still running
            if kill -0 "$pid" 2>/dev/null; then
                log_warning "Force killing validator..."
                kill -9 "$pid" 2>/dev/null || true
            fi
        fi
        
        rm -f "$RESULTS_DIR/validator.pid"
    fi
}

# Trap cleanup on exit
trap cleanup_validator EXIT

# Main execution
main() {
    log_info "Starting Agave validator benchmark..."
    
    parse_arguments "$@"
    check_dependencies
    setup_validator
    start_validator
    run_benchmarks
    
    log_success "Agave validator benchmark completed successfully!"
    log_info "Results available in: $RESULTS_DIR"
}

# Execute main function with all arguments
main "$@"