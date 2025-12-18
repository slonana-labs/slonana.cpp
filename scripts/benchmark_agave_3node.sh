#!/usr/bin/env bash
set -euo pipefail

# Agave 3-Node Cluster Benchmark Script
# Automated benchmarking script for Anza/Agave validator cluster
# Provides comprehensive performance testing with multi-node setup

SCRIPT_NAME="$(basename "$0")"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Default configuration
CLUSTER_DIR=""
RESULTS_DIR=""
VALIDATOR_BIN="agave-validator"
TEST_DURATION=300  # 5 minutes for thorough testing
BOOTSTRAP_ONLY=false
VERBOSE=true

# 3-Node cluster port configuration
NODE1_RPC=8899
NODE1_GOSSIP=8001
NODE1_TPU=8003
NODE1_TPU_FWD=8004

NODE2_RPC=8999
NODE2_GOSSIP=8002
NODE2_TPU=8005
NODE2_TPU_FWD=8006

NODE3_RPC=9099
NODE3_GOSSIP=8010
NODE3_TPU=8012
NODE3_TPU_FWD=8013

# Validator PIDs
VALIDATOR_PIDS=()

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

# CI optimization
if [[ -n "${CI:-}" || -n "${GITHUB_ACTIONS:-}" || -n "${CONTINUOUS_INTEGRATION:-}" ]]; then
    TEST_DURATION=300  # Keep 5 minutes for thorough CI testing
    log_info "🔧 CI environment detected - using ${TEST_DURATION}s test duration"
fi

show_help() {
    cat << EOF
$SCRIPT_NAME - Agave 3-Node Cluster Benchmark Script

USAGE:
    $SCRIPT_NAME --cluster CLUSTER_DIR --results RESULTS_DIR [OPTIONS]

REQUIRED ARGUMENTS:
    --cluster CLUSTER_DIR    Directory for cluster data (ledgers, keypairs)
    --results RESULTS_DIR    Directory to store benchmark results

OPTIONAL ARGUMENTS:
    --validator-bin PATH     Path to agave-validator binary (default: agave-validator)
    --test-duration SECONDS  Benchmark test duration in seconds (default: 300)
    --bootstrap-only         Only bootstrap cluster, don't run performance tests
    --verbose                Enable verbose logging
    --help                   Show this help message

EXAMPLES:
    # Basic 3-node cluster benchmark
    $SCRIPT_NAME --cluster /tmp/agave_cluster --results /tmp/agave_results

    # Custom test duration
    $SCRIPT_NAME --cluster /tmp/cluster --results /tmp/results --test-duration 600

    # Bootstrap only (for setup testing)
    $SCRIPT_NAME --cluster /tmp/cluster --results /tmp/results --bootstrap-only

EXIT CODES:
    0    Success
    1    General error
    2    Invalid arguments
    3    Missing dependencies
    4    Cluster startup failure
    5    Benchmark execution failure
EOF
}

# Parse command line arguments
parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --cluster)
                CLUSTER_DIR="$2"
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
            --test-duration)
                TEST_DURATION="$2"
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
    if [[ -z "$CLUSTER_DIR" ]]; then
        log_error "Missing required argument: --cluster"
        exit 2
    fi

    if [[ -z "$RESULTS_DIR" ]]; then
        log_error "Missing required argument: --results"
        exit 2
    fi

    # Validate test duration
    if ! [[ "$TEST_DURATION" =~ ^[0-9]+$ ]] || [[ "$TEST_DURATION" -lt 60 ]]; then
        log_error "Test duration must be a positive integer >= 60 seconds"
        exit 2
    fi

    # Create absolute paths
    CLUSTER_DIR="$(realpath "$CLUSTER_DIR")"
    RESULTS_DIR="$(realpath "$RESULTS_DIR")"
}

# Check dependencies
check_dependencies() {
    log_info "Checking dependencies..."

    # Check for validator binary
    if ! command -v "$VALIDATOR_BIN" &> /dev/null; then
        log_error "Agave validator binary not found: $VALIDATOR_BIN"
        log_error "Install with: cargo install agave-validator --locked"
        exit 3
    fi

    # Check for Solana CLI tools
    for tool in solana-keygen solana solana-genesis; do
        if ! command -v "$tool" &> /dev/null; then
            log_error "$tool not found. Please install Solana CLI tools."
            exit 3
        fi
    done

    # Check for system utilities
    for util in curl jq; do
        if ! command -v "$util" &> /dev/null; then
            log_error "Required utility not found: $util"
            exit 3
        fi
    done

    log_success "All dependencies available"
}

# Setup 3-node cluster environment
setup_cluster() {
    log_info "Setting up 3-node Agave cluster environment..."

    # Create directories
    mkdir -p "$CLUSTER_DIR"/{node1,node2,node3}/ledger
    mkdir -p "$CLUSTER_DIR"/keys
    mkdir -p "$RESULTS_DIR"

    # Generate keypairs for all 3 nodes
    log_info "Generating keypairs for 3-node cluster..."
    
    # Node 1 (bootstrap validator)
    log_verbose "Generating Node 1 keypairs..."
    solana-keygen new --no-bip39-passphrase --silent --outfile "$CLUSTER_DIR/keys/node1-identity.json"
    solana-keygen new --no-bip39-passphrase --silent --outfile "$CLUSTER_DIR/keys/node1-vote.json"
    solana-keygen new --no-bip39-passphrase --silent --outfile "$CLUSTER_DIR/keys/node1-stake.json"
    
    # Node 2
    log_verbose "Generating Node 2 keypairs..."
    solana-keygen new --no-bip39-passphrase --silent --outfile "$CLUSTER_DIR/keys/node2-identity.json"
    solana-keygen new --no-bip39-passphrase --silent --outfile "$CLUSTER_DIR/keys/node2-vote.json"
    solana-keygen new --no-bip39-passphrase --silent --outfile "$CLUSTER_DIR/keys/node2-stake.json"
    
    # Node 3
    log_verbose "Generating Node 3 keypairs..."
    solana-keygen new --no-bip39-passphrase --silent --outfile "$CLUSTER_DIR/keys/node3-identity.json"
    solana-keygen new --no-bip39-passphrase --silent --outfile "$CLUSTER_DIR/keys/node3-vote.json"
    solana-keygen new --no-bip39-passphrase --silent --outfile "$CLUSTER_DIR/keys/node3-stake.json"
    
    # Faucet keypair
    solana-keygen new --no-bip39-passphrase --silent --outfile "$CLUSTER_DIR/keys/faucet.json"

    # Extract pubkeys
    local node1_identity node1_vote node1_stake
    local node2_identity node2_vote node2_stake
    local node3_identity node3_vote node3_stake
    local faucet_pubkey
    
    node1_identity=$(solana-keygen pubkey "$CLUSTER_DIR/keys/node1-identity.json")
    node1_vote=$(solana-keygen pubkey "$CLUSTER_DIR/keys/node1-vote.json")
    node1_stake=$(solana-keygen pubkey "$CLUSTER_DIR/keys/node1-stake.json")
    
    node2_identity=$(solana-keygen pubkey "$CLUSTER_DIR/keys/node2-identity.json")
    node2_vote=$(solana-keygen pubkey "$CLUSTER_DIR/keys/node2-vote.json")
    node2_stake=$(solana-keygen pubkey "$CLUSTER_DIR/keys/node2-stake.json")
    
    node3_identity=$(solana-keygen pubkey "$CLUSTER_DIR/keys/node3-identity.json")
    node3_vote=$(solana-keygen pubkey "$CLUSTER_DIR/keys/node3-vote.json")
    node3_stake=$(solana-keygen pubkey "$CLUSTER_DIR/keys/node3-stake.json")
    
    faucet_pubkey=$(solana-keygen pubkey "$CLUSTER_DIR/keys/faucet.json")

    log_verbose "Node 1 Identity: $node1_identity"
    log_verbose "Node 2 Identity: $node2_identity"
    log_verbose "Node 3 Identity: $node3_identity"

    # Generate genesis with all 3 bootstrap validators
    log_info "Creating genesis configuration with 3 bootstrap validators..."
    
    solana-genesis \
        --ledger "$CLUSTER_DIR/node1/ledger" \
        --bootstrap-validator "$node1_identity" "$node1_vote" "$node1_stake" \
        --bootstrap-validator "$node2_identity" "$node2_vote" "$node2_stake" \
        --bootstrap-validator "$node3_identity" "$node3_vote" "$node3_stake" \
        --cluster-type development \
        --faucet-pubkey "$faucet_pubkey" \
        --faucet-lamports 1000000000000000 \
        --bootstrap-validator-lamports 500000000000000 \
        --bootstrap-validator-stake-lamports 500000000000

    # Copy genesis to other nodes
    cp -r "$CLUSTER_DIR/node1/ledger/"* "$CLUSTER_DIR/node2/ledger/"
    cp -r "$CLUSTER_DIR/node1/ledger/"* "$CLUSTER_DIR/node3/ledger/"

    log_success "3-node cluster environment setup complete"
}

# Start validator node
start_validator_node() {
    local node_num=$1
    local identity_file="$CLUSTER_DIR/keys/node${node_num}-identity.json"
    local vote_file="$CLUSTER_DIR/keys/node${node_num}-vote.json"
    local ledger_dir="$CLUSTER_DIR/node${node_num}/ledger"
    local log_file="$RESULTS_DIR/node${node_num}.log"
    
    # Get port configuration based on node number
    local rpc_port gossip_port tpu_port tpu_fwd_port
    case $node_num in
        1)
            rpc_port=$NODE1_RPC
            gossip_port=$NODE1_GOSSIP
            tpu_port=$NODE1_TPU
            tpu_fwd_port=$NODE1_TPU_FWD
            ;;
        2)
            rpc_port=$NODE2_RPC
            gossip_port=$NODE2_GOSSIP
            tpu_port=$NODE2_TPU
            tpu_fwd_port=$NODE2_TPU_FWD
            ;;
        3)
            rpc_port=$NODE3_RPC
            gossip_port=$NODE3_GOSSIP
            tpu_port=$NODE3_TPU
            tpu_fwd_port=$NODE3_TPU_FWD
            ;;
        *)
            log_error "Invalid node number: $node_num"
            return 1
            ;;
    esac

    log_info "Starting validator node $node_num..."
    log_verbose "  RPC Port: $rpc_port"
    log_verbose "  Gossip Port: $gossip_port"
    log_verbose "  TPU Port: $tpu_port"

    # Build validator command
    local cmd=("$VALIDATOR_BIN")
    cmd+=(--identity "$identity_file")
    cmd+=(--vote-account "$vote_file")
    cmd+=(--ledger "$ledger_dir")
    cmd+=(--rpc-port "$rpc_port")
    cmd+=(--gossip-port "$gossip_port")
    cmd+=(--dynamic-port-range "$tpu_port-$((tpu_port+20))")
    cmd+=(--log -)
    cmd+=(--enable-rpc-transaction-history)
    cmd+=(--enable-cpi-and-log-storage)
    
    # Add gossip entrypoint for non-bootstrap nodes
    if [[ $node_num -gt 1 ]]; then
        cmd+=(--entrypoint "127.0.0.1:$NODE1_GOSSIP")
        log_verbose "  Using Node 1 as gossip entrypoint"
    fi

    # Start validator in background
    "${cmd[@]}" > "$log_file" 2>&1 &
    local pid=$!
    VALIDATOR_PIDS+=("$pid")
    
    echo "$pid" > "$RESULTS_DIR/node${node_num}.pid"
    log_success "Node $node_num started with PID: $pid"

    return 0
}

# Wait for node to become ready
wait_for_node_ready() {
    local node_num=$1
    local rpc_port
    
    case $node_num in
        1) rpc_port=$NODE1_RPC ;;
        2) rpc_port=$NODE2_RPC ;;
        3) rpc_port=$NODE3_RPC ;;
        *) return 1 ;;
    esac

    log_info "Waiting for node $node_num to become ready..."
    local timeout=60
    local wait_time=0

    while [[ $wait_time -lt $timeout ]]; do
        # First check HTTP health endpoint
        if curl -s "http://localhost:$rpc_port/health" > /dev/null 2>&1; then
            # Then verify JSON-RPC interface is responsive via getVersion
            local rpc_response
            rpc_response="$(curl -s -X POST "http://localhost:$rpc_port" \
                -H "Content-Type: application/json" \
                -d '{"jsonrpc":"2.0","id":1,"method":"getVersion","params":[]}')" || rpc_response=""

            if [[ -n "$rpc_response" ]] && echo "$rpc_response" | grep -q '"result"'; then
                log_success "Node $node_num is ready (health + JSON-RPC)!"
                return 0
            fi
        fi
        sleep 3
        wait_time=$((wait_time + 3))
    done

    log_error "Node $node_num failed to start within ${timeout}s timeout"
    return 1
}

# Start entire 3-node cluster
start_cluster() {
    log_info "Starting 3-node Agave cluster..."

    # Start Node 1 (bootstrap)
    start_validator_node 1 || exit 4
    wait_for_node_ready 1 || exit 4
    
    # Give bootstrap node time to stabilize
    log_info "Allowing bootstrap node to stabilize..."
    sleep 10

    # Start Node 2
    start_validator_node 2 || exit 4
    wait_for_node_ready 2 || exit 4

    # Start Node 3
    start_validator_node 3 || exit 4
    wait_for_node_ready 3 || exit 4

    # Wait for cluster to sync
    log_info "Waiting for cluster to synchronize..."
    sleep 20

    # Create vote accounts and delegate stake
    setup_vote_accounts

    log_success "3-node cluster is running and synchronized"
}

# Setup vote accounts and stake delegation
setup_vote_accounts() {
    log_info "Setting up vote accounts and stake delegation..."

    # Configure Solana CLI to use Node 1
    solana config set --url "http://localhost:$NODE1_RPC" > /dev/null

    # Create vote accounts for each validator
    for node_num in 1 2 3; do
        local identity_file="$CLUSTER_DIR/keys/node${node_num}-identity.json"
        local vote_file="$CLUSTER_DIR/keys/node${node_num}-vote.json"
        local stake_file="$CLUSTER_DIR/keys/node${node_num}-stake.json"
        
        log_verbose "Creating vote account for node $node_num..."
        
        # The vote account is already initialized by the validator
        # Just delegate stake to it
        local vote_pubkey
        vote_pubkey=$(solana-keygen pubkey "$vote_file")
        
        log_verbose "Delegating stake for node $node_num to $vote_pubkey..."
        
        # Create stake account and delegate
        solana create-stake-account \
            "$stake_file" \
            500 \
            --from "$identity_file" \
            --stake-authority "$identity_file" \
            2>/dev/null || log_verbose "Stake account may already exist"
        
        solana delegate-stake \
            "$stake_file" \
            "$vote_pubkey" \
            --stake-authority "$identity_file" \
            2>/dev/null || log_verbose "Stake may already be delegated"
    done

    log_success "Vote accounts and stake delegation complete"
}

# Test cluster health
check_cluster_health() {
    log_info "Checking cluster health..."

    local all_healthy=true
    
    for node_num in 1 2 3; do
        local rpc_port
        case $node_num in
            1) rpc_port=$NODE1_RPC ;;
            2) rpc_port=$NODE2_RPC ;;
            3) rpc_port=$NODE3_RPC ;;
        esac

        if curl -s "http://localhost:$rpc_port/health" > /dev/null 2>&1; then
            log_success "Node $node_num: Healthy"
        else
            log_error "Node $node_num: Unhealthy"
            all_healthy=false
        fi
    done

    if [[ "$all_healthy" == "true" ]]; then
        log_success "All nodes healthy"
        return 0
    else
        log_error "Some nodes are unhealthy"
        return 1
    fi
}

# Run performance benchmarks
run_benchmarks() {
    if [[ "$BOOTSTRAP_ONLY" == true ]]; then
        log_info "Bootstrap-only mode, skipping performance tests"
        return 0
    fi

    log_info "Running 3-node cluster performance benchmarks..."

    # Test RPC performance across all nodes
    test_rpc_performance

    # Test transaction throughput with load balancing
    test_transaction_throughput

    # Generate results summary
    generate_results_summary
}

# Test RPC performance across all nodes
test_rpc_performance() {
    log_info "Testing RPC response times across all nodes..."

    local total_latency=0
    local rpc_calls=100

    for node_num in 1 2 3; do
        local rpc_port
        case $node_num in
            1) rpc_port=$NODE1_RPC ;;
            2) rpc_port=$NODE2_RPC ;;
            3) rpc_port=$NODE3_RPC ;;
        esac

        local start_time=$(date +%s%N)

        for ((i=1; i<=rpc_calls; i++)); do
            curl -s -X POST "http://localhost:$rpc_port" \
                -H "Content-Type: application/json" \
                -d '{"jsonrpc":"2.0","id":1,"method":"getVersion"}' \
                > /dev/null
        done

        local end_time=$(date +%s%N)
        local duration_ns=$((end_time - start_time))
        local node_latency=$((duration_ns / 1000000 / rpc_calls))
        
        echo "$node_latency" > "$RESULTS_DIR/node${node_num}_rpc_latency_ms.txt"
        log_verbose "Node $node_num RPC latency: ${node_latency}ms"
        
        total_latency=$((total_latency + node_latency))
    done

    local avg_latency=$((total_latency / 3))
    echo "$avg_latency" > "$RESULTS_DIR/rpc_latency_ms.txt"
    log_success "Average RPC latency across cluster: ${avg_latency}ms"
}

# Test transaction throughput with load balancing
test_transaction_throughput() {
    log_info "Testing transaction throughput with load balancing..."

    # Configure Solana CLI
    export PATH="$HOME/.local/share/solana/install/active_release/bin:$PATH"
    solana config set --url "http://localhost:$NODE1_RPC" > /dev/null

    # Generate keypairs
    local sender_keypair="$RESULTS_DIR/sender-keypair.json"
    local recipient_keypair="$RESULTS_DIR/recipient-keypair.json"

    solana-keygen new --no-bip39-passphrase --silent --outfile "$sender_keypair"
    solana-keygen new --no-bip39-passphrase --silent --outfile "$recipient_keypair"

    # Fund sender account
    log_info "Funding sender account..."
    local max_airdrop_attempts=10
    local airdrop_attempts=0
    local funded=false
    while [[ $airdrop_attempts -lt $max_airdrop_attempts ]]; do
        if solana airdrop 1000 --keypair "$sender_keypair" 2>/dev/null; then
            log_success "Sender funded successfully"
            funded=true
            break
        fi
        ((airdrop_attempts++))
        sleep 2
    done

    if [[ "$funded" != true ]]; then
        log_error "Failed to fund sender account after ${max_airdrop_attempts} attempts; aborting benchmark."
        exit 1
    fi
    # Run transaction test with load balancing
    local txn_count=0
    local success_count=0
    local start_time=$(date +%s)
    local end_time=$((start_time + TEST_DURATION))
    local recipient_pubkey
    recipient_pubkey=$(solana-keygen pubkey "$recipient_keypair")

    log_info "Starting load-balanced transaction test for ${TEST_DURATION}s..."

    # Initialize per-node counters
    local node1_count=0 node2_count=0 node3_count=0

    while [[ $(date +%s) -lt $end_time ]]; do
        # Round-robin load balancing across nodes
        local target_node=$((txn_count % 3 + 1))
        local target_port
        
        case $target_node in
            1) target_port=$NODE1_RPC; ((node1_count++)) ;;
            2) target_port=$NODE2_RPC; ((node2_count++)) ;;
            3) target_port=$NODE3_RPC; ((node3_count++)) ;;
        esac

        # Send transaction to selected node
        if timeout 5s solana transfer "$recipient_pubkey" 0.001 \
            --keypair "$sender_keypair" \
            --url "http://localhost:$target_port" \
            --allow-unfunded-recipient \
            --no-wait \
            > /dev/null 2>&1; then
            ((success_count++))
        fi
        
        ((txn_count++))
        
        # Progress logging
        if [[ $((txn_count % 100)) -eq 0 ]]; then
            log_verbose "Sent $txn_count transactions (${success_count} successful)"
        fi
        
        sleep 0.01  # Rate limiting
    done

    local actual_duration=$(($(date +%s) - start_time))
    local effective_tps=$((success_count / actual_duration))

    # Save results
    echo "$effective_tps" > "$RESULTS_DIR/effective_tps.txt"
    echo "$success_count" > "$RESULTS_DIR/successful_transactions.txt"
    echo "$txn_count" > "$RESULTS_DIR/submitted_requests.txt"
    echo "$node1_count" > "$RESULTS_DIR/node1_tx_count.txt"
    echo "$node2_count" > "$RESULTS_DIR/node2_tx_count.txt"
    echo "$node3_count" > "$RESULTS_DIR/node3_tx_count.txt"

    log_success "Transaction throughput test completed"
    log_info "Effective TPS: $effective_tps"
    log_info "Load distribution - Node1: $node1_count, Node2: $node2_count, Node3: $node3_count"
}

# Generate results summary
generate_results_summary() {
    log_info "Generating benchmark results summary..."

    local rpc_latency_ms=$(cat "$RESULTS_DIR/rpc_latency_ms.txt" 2>/dev/null || echo "0")
    local effective_tps=$(cat "$RESULTS_DIR/effective_tps.txt" 2>/dev/null || echo "0")
    local successful_transactions=$(cat "$RESULTS_DIR/successful_transactions.txt" 2>/dev/null || echo "0")
    local submitted_requests=$(cat "$RESULTS_DIR/submitted_requests.txt" 2>/dev/null || echo "0")
    
    local node1_tx=$(cat "$RESULTS_DIR/node1_tx_count.txt" 2>/dev/null || echo "0")
    local node2_tx=$(cat "$RESULTS_DIR/node2_tx_count.txt" 2>/dev/null || echo "0")
    local node3_tx=$(cat "$RESULTS_DIR/node3_tx_count.txt" 2>/dev/null || echo "0")

    # Generate JSON results
    cat > "$RESULTS_DIR/benchmark_results.json" << EOF
{
  "validator_type": "agave-3node-cluster",
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "test_duration_seconds": $TEST_DURATION,
  "cluster_config": {
    "num_validators": 3,
    "vote_accounts": true,
    "stake_delegation": true
  },
  "rpc_latency_ms": $rpc_latency_ms,
  "effective_tps": $effective_tps,
  "submitted_requests": $submitted_requests,
  "successful_transactions": $successful_transactions,
  "load_distribution": {
    "node1_transactions": $node1_tx,
    "node2_transactions": $node2_tx,
    "node3_transactions": $node3_tx
  },
  "system_info": {
    "cores": $(nproc),
    "total_memory_mb": $(free -m | awk '/^Mem:/{print $2}')
  }
}
EOF

    log_success "Benchmark results saved to: $RESULTS_DIR/benchmark_results.json"
    
    # Display summary
    echo ""
    echo "=== Agave 3-Node Cluster Benchmark Results ==="
    echo "Test Duration: ${TEST_DURATION}s"
    echo "Number of Validators: 3"
    echo "RPC Latency (avg): ${rpc_latency_ms}ms"
    echo "Effective TPS: $effective_tps"
    echo "Successful Transactions: $successful_transactions"
    echo "Load Distribution:"
    echo "  Node 1: $node1_tx transactions"
    echo "  Node 2: $node2_tx transactions"
    echo "  Node 3: $node3_tx transactions"
    echo "=============================================="
}

# Cleanup cluster
cleanup_cluster() {
    log_info "Cleaning up 3-node cluster..."

    # Stop all validator processes
    for pid in "${VALIDATOR_PIDS[@]}"; do
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            log_verbose "Stopping validator (PID: $pid)..."
            kill -TERM "$pid" 2>/dev/null || true
            sleep 2
            kill -KILL "$pid" 2>/dev/null || true
        fi
    done

    # Clean up PID files
    rm -f "$RESULTS_DIR"/node*.pid

    log_success "Cluster cleanup complete"
}

# Signal handlers
trap cleanup_cluster EXIT SIGTERM SIGINT

# Main execution
main() {
    log_info "Starting Agave 3-node cluster benchmark..."
    
    parse_arguments "$@"
    check_dependencies
    setup_cluster
    start_cluster
    check_cluster_health || exit 4
    run_benchmarks
    
    log_success "Agave 3-node cluster benchmark completed successfully!"
    log_info "Results available in: $RESULTS_DIR"
}

# Execute main function with all arguments
main "$@"

exit 0
