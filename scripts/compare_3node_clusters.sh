#!/usr/bin/env bash
set -e

# 3-Node Cluster Comparison Script
# Runs both Agave and Slonana 3-node clusters and compares results

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Configuration
WORKSPACE_DIR="/tmp/3node_comparison"
TEST_DURATION=300  # 5 minutes

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_header() { echo -e "${CYAN}=== $1 ===${NC}"; }

show_help() {
    cat << EOF
3-Node Cluster Comparison Script

USAGE:
    $0 [OPTIONS]

OPTIONS:
    --duration SECONDS  Test duration for each benchmark (default: 300)
    --skip-agave        Skip Agave benchmark
    --skip-slonana      Skip Slonana benchmark
    --help              Show this help message

EXAMPLES:
    # Run both benchmarks with default 5-minute duration
    $0

    # Run with custom duration
    $0 --duration 600

    # Run only Slonana benchmark
    $0 --skip-agave
EOF
}

# Parse arguments
SKIP_AGAVE=false
SKIP_SLONANA=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --duration)
            TEST_DURATION="$2"
            shift 2
            ;;
        --skip-agave)
            SKIP_AGAVE=true
            shift
            ;;
        --skip-slonana)
            SKIP_SLONANA=true
            shift
            ;;
        --help)
            show_help
            exit 0
            ;;
        *)
            log_error "Unknown argument: $1"
            show_help
            exit 1
            ;;
    esac
done

# Ensure at least one benchmark will run
if [[ "$SKIP_AGAVE" == "true" && "$SKIP_SLONANA" == "true" ]]; then
    log_error "Both --skip-agave and --skip-slonana were provided; no benchmarks to run."
    log_error "Please run without at least one of these flags. Use --help for usage."
    exit 1
fi
# Setup workspace
log_info "Setting up comparison workspace..."
rm -rf "$WORKSPACE_DIR"
mkdir -p "$WORKSPACE_DIR"/{agave,slonana}

# Run Agave benchmark
if [[ "$SKIP_AGAVE" == "false" ]]; then
    log_header "Running Agave 3-Node Cluster Benchmark"
    
    if command -v agave-validator &> /dev/null; then
        "$SCRIPT_DIR/benchmark_agave_3node.sh" \
            --cluster "$WORKSPACE_DIR/agave/cluster" \
            --results "$WORKSPACE_DIR/agave/results" \
            --test-duration "$TEST_DURATION" \
            --verbose
        
        log_success "Agave benchmark completed"
    else
        log_warning "Agave validator not found, skipping Agave benchmark"
        SKIP_AGAVE=true
    fi
    
    echo ""
fi

# Run Slonana benchmark
if [[ "$SKIP_SLONANA" == "false" ]]; then
    log_header "Running Slonana 3-Node Cluster Benchmark"
    
    "$SCRIPT_DIR/benchmark_slonana_3node.sh" \
        --cluster "$WORKSPACE_DIR/slonana/cluster" \
        --results "$WORKSPACE_DIR/slonana/results" \
        --test-duration "$TEST_DURATION" \
        --verbose
    
    log_success "Slonana benchmark completed"
    echo ""
fi

# Compare results
log_header "Benchmark Comparison Results"

if [[ "$SKIP_AGAVE" == "false" ]] && [[ -f "$WORKSPACE_DIR/agave/results/benchmark_results.json" ]]; then
    echo ""
    echo "Agave 3-Node Cluster Results:"
    echo "─────────────────────────────"
    
    agave_tps=$(jq -r '.effective_tps' "$WORKSPACE_DIR/agave/results/benchmark_results.json")
    agave_latency=$(jq -r '.rpc_latency_ms' "$WORKSPACE_DIR/agave/results/benchmark_results.json")
    agave_tx=$(jq -r '.successful_transactions' "$WORKSPACE_DIR/agave/results/benchmark_results.json")
    
    echo "  Effective TPS: $agave_tps"
    echo "  RPC Latency: ${agave_latency}ms"
    echo "  Successful Transactions: $agave_tx"
    echo "  Load Distribution:"
    jq -r '.load_distribution | "    Node 1: \(.node1_transactions) tx\n    Node 2: \(.node2_transactions) tx\n    Node 3: \(.node3_transactions) tx"' \
        "$WORKSPACE_DIR/agave/results/benchmark_results.json"
fi

if [[ "$SKIP_SLONANA" == "false" ]] && [[ -f "$WORKSPACE_DIR/slonana/results/benchmark_results.json" ]]; then
    echo ""
    echo "Slonana 3-Node Cluster Results:"
    echo "───────────────────────────────"
    
    slonana_tps=$(jq -r '.effective_tps' "$WORKSPACE_DIR/slonana/results/benchmark_results.json")
    slonana_latency=$(jq -r '.rpc_latency_ms' "$WORKSPACE_DIR/slonana/results/benchmark_results.json")
    slonana_tx=$(jq -r '.successful_transactions' "$WORKSPACE_DIR/slonana/results/benchmark_results.json")
    
    echo "  Effective TPS: $slonana_tps"
    echo "  RPC Latency: ${slonana_latency}ms"
    echo "  Successful Transactions: $slonana_tx"
    echo "  Load Distribution:"
    jq -r '.load_distribution | "    Node 1: \(.node1_transactions) tx\n    Node 2: \(.node2_transactions) tx\n    Node 3: \(.node3_transactions) tx"' \
        "$WORKSPACE_DIR/slonana/results/benchmark_results.json"
fi

# Generate comparison if both ran
if [[ "$SKIP_AGAVE" == "false" ]] && [[ "$SKIP_SLONANA" == "false" ]] && \
   [[ -f "$WORKSPACE_DIR/agave/results/benchmark_results.json" ]] && \
   [[ -f "$WORKSPACE_DIR/slonana/results/benchmark_results.json" ]]; then
    
    echo ""
    echo "Performance Comparison:"
    echo "──────────────────────"
    
    # TPS comparison (using awk for portability)
    if awk "BEGIN {exit !($slonana_tps > $agave_tps)}"; then
        tps_diff=$(awk "BEGIN {printf \"%.2f\", (($slonana_tps - $agave_tps) / $agave_tps) * 100}")
        echo -e "  ${GREEN}✓${NC} Slonana TPS is ${tps_diff}% higher than Agave"
    elif awk "BEGIN {exit !($agave_tps > $slonana_tps)}"; then
        tps_diff=$(awk "BEGIN {printf \"%.2f\", (($agave_tps - $slonana_tps) / $slonana_tps) * 100}")
        echo -e "  ${YELLOW}○${NC} Agave TPS is ${tps_diff}% higher than Slonana"
    else
        echo "  ≈ TPS is equal"
    fi
    
    # Latency comparison (using awk for portability)
    if awk "BEGIN {exit !($agave_latency < $slonana_latency)}"; then
        latency_diff=$(awk "BEGIN {printf \"%.2f\", (($slonana_latency - $agave_latency) / $agave_latency) * 100}")
        echo -e "  ${YELLOW}○${NC} Agave latency is ${latency_diff}% lower than Slonana"
    elif awk "BEGIN {exit !($slonana_latency < $agave_latency)}"; then
        latency_diff=$(awk "BEGIN {printf \"%.2f\", (($agave_latency - $slonana_latency) / $slonana_latency) * 100}")
        echo -e "  ${GREEN}✓${NC} Slonana latency is ${latency_diff}% lower than Agave"
    else
        echo "  ≈ Latency is equal"
    fi
    
    # Generate combined JSON report
    jq -n \
        --slurpfile agave "$WORKSPACE_DIR/agave/results/benchmark_results.json" \
        --slurpfile slonana "$WORKSPACE_DIR/slonana/results/benchmark_results.json" \
        '{
            "comparison_timestamp": (now | todate),
            "test_duration_seconds": '$TEST_DURATION',
            "agave": $agave[0],
            "slonana": $slonana[0]
        }' > "$WORKSPACE_DIR/comparison_results.json"
    
    log_success "Comparison results saved to: $WORKSPACE_DIR/comparison_results.json"
fi

echo ""
log_success "3-node cluster comparison complete!"
log_info "Results directory: $WORKSPACE_DIR"
