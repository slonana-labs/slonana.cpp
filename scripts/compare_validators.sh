#!/usr/bin/env bash
set -euo pipefail

# Validator Comparison Script
# Runs both Agave and Slonana validators and compares their performance metrics
# Only compares metrics that both validators can provide

SCRIPT_NAME="$(basename "$0")"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Default configuration
TEST_DURATION=60
BASE_RESULTS_DIR="$PROJECT_ROOT/validator_comparison"
AGAVE_RESULTS_DIR="$BASE_RESULTS_DIR/agave"
SLONANA_RESULTS_DIR="$BASE_RESULTS_DIR/slonana"
AGAVE_LEDGER_DIR="$PROJECT_ROOT/test_ledgers/agave"
SLONANA_LEDGER_DIR="$PROJECT_ROOT/test_ledgers/slonana"
RPC_PORT_AGAVE=8899
RPC_PORT_SLONANA=8900
GOSSIP_PORT_AGAVE=8001
GOSSIP_PORT_SLONANA=8002
VERBOSE=false
CLEANUP_AFTER=true

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
PURPLE='\033[0;35m'
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

log_comparison() {
    echo -e "${CYAN}[COMPARISON]${NC} $1" >&2
}

log_agave() {
    echo -e "${PURPLE}[AGAVE]${NC} $1" >&2
}

log_slonana() {
    echo -e "${GREEN}[SLONANA]${NC} $1" >&2
}

show_help() {
    cat << EOF
$SCRIPT_NAME - Validator Performance Comparison Tool

USAGE:
    $SCRIPT_NAME [OPTIONS]

DESCRIPTION:
    Runs both Agave and Slonana validators with identical test conditions
    and compares their performance on common metrics that both can provide.

OPTIONS:
    --test-duration SECONDS  Duration for each validator test (default: 60)
    --results-dir PATH       Base directory for results (default: PROJECT_ROOT/validator_comparison)
    --rpc-port-agave PORT    RPC port for Agave validator (default: 8899)
    --rpc-port-slonana PORT  RPC port for Slonana validator (default: 8900)
    --keep-temp-files        Don't cleanup temporary files after comparison
    --verbose                Enable verbose logging
    --help                   Show this help message

COMMON METRICS COMPARED:
    • RPC Response Latency (milliseconds)
    • Transaction Processing Throughput (TPS)
    • Memory Usage (MB)
    • CPU Utilization (%)
    • Startup Time (seconds)
    • Slot Processing Rate (slots/second)

EXAMPLES:
    # Basic comparison with default settings
    $SCRIPT_NAME

    # Extended test with verbose output
    $SCRIPT_NAME --test-duration 120 --verbose

    # Custom ports and keep temp files
    $SCRIPT_NAME --rpc-port-agave 9899 --rpc-port-slonana 9900 --keep-temp-files

EXIT CODES:
    0    Success - comparison completed
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
            --test-duration)
                TEST_DURATION="$2"
                shift 2
                ;;
            --results-dir)
                BASE_RESULTS_DIR="$2"
                AGAVE_RESULTS_DIR="$BASE_RESULTS_DIR/agave"
                SLONANA_RESULTS_DIR="$BASE_RESULTS_DIR/slonana"
                shift 2
                ;;
            --rpc-port-agave)
                RPC_PORT_AGAVE="$2"
                shift 2
                ;;
            --rpc-port-slonana)
                RPC_PORT_SLONANA="$2"
                shift 2
                ;;
            --keep-temp-files)
                CLEANUP_AFTER=false
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

    # Validate test duration
    if ! [[ "$TEST_DURATION" =~ ^[0-9]+$ ]] || [[ "$TEST_DURATION" -lt 30 ]]; then
        log_error "Test duration must be a positive integer >= 30 seconds"
        exit 2
    fi

    # Create absolute paths
    BASE_RESULTS_DIR="$(mkdir -p "$BASE_RESULTS_DIR" && realpath "$BASE_RESULTS_DIR")"
    AGAVE_RESULTS_DIR="$BASE_RESULTS_DIR/agave"
    SLONANA_RESULTS_DIR="$BASE_RESULTS_DIR/slonana"
    
    # Create ledger directories before getting absolute paths
    mkdir -p "$AGAVE_LEDGER_DIR" "$SLONANA_LEDGER_DIR"
    AGAVE_LEDGER_DIR="$(realpath "$AGAVE_LEDGER_DIR")"
    SLONANA_LEDGER_DIR="$(realpath "$SLONANA_LEDGER_DIR")"
}

# Check dependencies
check_dependencies() {
    log_info "Checking dependencies for validator comparison..."

    # Check if benchmark scripts exist
    if [[ ! -f "$SCRIPT_DIR/benchmark_agave.sh" ]]; then
        log_error "Agave benchmark script not found: $SCRIPT_DIR/benchmark_agave.sh"
        exit 3
    fi

    if [[ ! -f "$SCRIPT_DIR/benchmark_slonana.sh" ]]; then
        log_error "Slonana benchmark script not found: $SCRIPT_DIR/benchmark_slonana.sh"
        exit 3
    fi

    # Make scripts executable
    chmod +x "$SCRIPT_DIR/benchmark_agave.sh"
    chmod +x "$SCRIPT_DIR/benchmark_slonana.sh"

    # Check for required utilities
    for util in jq bc curl; do
        if ! command -v "$util" &> /dev/null; then
            log_error "Required utility not found: $util"
            exit 3
        fi
    done

    log_success "All dependencies available"
}

# Setup comparison environment
setup_environment() {
    log_info "Setting up comparison environment..."

    # Create directories
    mkdir -p "$BASE_RESULTS_DIR"
    mkdir -p "$AGAVE_RESULTS_DIR"
    mkdir -p "$SLONANA_RESULTS_DIR"
    mkdir -p "$AGAVE_LEDGER_DIR"
    mkdir -p "$SLONANA_LEDGER_DIR"

    # Clean any previous results
    rm -f "$AGAVE_RESULTS_DIR"/*.json "$AGAVE_RESULTS_DIR"/*.txt "$AGAVE_RESULTS_DIR"/*.log
    rm -f "$SLONANA_RESULTS_DIR"/*.json "$SLONANA_RESULTS_DIR"/*.txt "$SLONANA_RESULTS_DIR"/*.log

    log_success "Environment setup complete"
}

# Run Agave validator benchmark
run_agave_benchmark() {
    log_agave "Starting Agave validator benchmark..."
    
    local start_time=$(date +%s.%N)
    
    # Run Agave benchmark script
    if "$SCRIPT_DIR/benchmark_agave.sh" \
        --ledger "$AGAVE_LEDGER_DIR" \
        --results "$AGAVE_RESULTS_DIR" \
        --test-duration "$TEST_DURATION" \
        --rpc-port "$RPC_PORT_AGAVE" \
        --gossip-port "$GOSSIP_PORT_AGAVE" \
        $(if [[ "$VERBOSE" == true ]]; then echo "--verbose"; fi); then
        
        local end_time=$(date +%s.%N)
        local duration=$(echo "$end_time - $start_time" | bc -l)
        echo "$duration" > "$AGAVE_RESULTS_DIR/benchmark_duration.txt"
        
        log_agave "Benchmark completed successfully in ${duration}s"
        return 0
    else
        log_error "Agave benchmark failed"
        return 1
    fi
}

# Run Slonana validator benchmark
run_slonana_benchmark() {
    log_slonana "Starting Slonana validator benchmark..."
    
    local start_time=$(date +%s.%N)
    
    # Run Slonana benchmark script
    if "$SCRIPT_DIR/benchmark_slonana.sh" \
        --ledger "$SLONANA_LEDGER_DIR" \
        --results "$SLONANA_RESULTS_DIR" \
        --test-duration "$TEST_DURATION" \
        --rpc-port "$RPC_PORT_SLONANA" \
        --gossip-port "$GOSSIP_PORT_SLONANA" \
        $(if [[ "$VERBOSE" == true ]]; then echo "--verbose"; fi); then
        
        local end_time=$(date +%s.%N)
        local duration=$(echo "$end_time - $start_time" | bc -l)
        echo "$duration" > "$SLONANA_RESULTS_DIR/benchmark_duration.txt"
        
        log_slonana "Benchmark completed successfully in ${duration}s"
        return 0
    else
        log_error "Slonana benchmark failed"
        return 1
    fi
}

# Extract common metrics from benchmark results
extract_common_metrics() {
    local validator_name="$1"
    local results_dir="$2"
    local output_file="$3"
    
    log_verbose "Extracting common metrics for $validator_name..."
    
    # Read benchmark results
    local benchmark_file="$results_dir/benchmark_results.json"
    local duration_file="$results_dir/benchmark_duration.txt"
    
    if [[ ! -f "$benchmark_file" ]]; then
        log_warning "Benchmark results not found for $validator_name: $benchmark_file"
        return 1
    fi
    
    # Extract metrics with fallback values
    local rpc_latency=$(jq -r '.rpc_latency_ms // 0' "$benchmark_file")
    local effective_tps=$(jq -r '.effective_tps // 0' "$benchmark_file")
    local memory_usage=$(jq -r '.memory_usage_mb // 0' "$benchmark_file")
    local cpu_usage=$(jq -r '.cpu_usage_percent // 0' "$benchmark_file")
    local successful_transactions=$(jq -r '.successful_transactions // 0' "$benchmark_file")
    local submitted_requests=$(jq -r '.submitted_requests // 0' "$benchmark_file")
    local test_duration=$(jq -r '.test_duration_seconds // 0' "$benchmark_file")
    
    # Calculate startup time (total benchmark duration - test duration)
    local benchmark_duration="0"
    if [[ -f "$duration_file" ]]; then
        benchmark_duration=$(cat "$duration_file")
    fi
    local startup_time=$(echo "$benchmark_duration - $test_duration" | bc -l)
    startup_time=${startup_time:-0}
    
    # Calculate slot processing rate (approximate based on transaction processing)
    local slot_rate="0"
    if [[ "$test_duration" -gt 0 && "$successful_transactions" -gt 0 ]]; then
        # Assume ~64 transactions per slot (Solana default)
        slot_rate=$(echo "scale=2; $successful_transactions / 64 / $test_duration" | bc -l)
    fi
    
    # Create standardized metrics JSON
    cat > "$output_file" << EOF
{
  "validator": "$validator_name",
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "common_metrics": {
    "rpc_latency_ms": $rpc_latency,
    "transaction_throughput_tps": $effective_tps,
    "memory_usage_mb": $memory_usage,
    "cpu_usage_percent": $cpu_usage,
    "startup_time_seconds": $startup_time,
    "slot_processing_rate": $slot_rate,
    "test_duration_seconds": $test_duration,
    "successful_transactions": $successful_transactions,
    "submitted_requests": $submitted_requests,
    "benchmark_duration_seconds": $benchmark_duration
  }
}
EOF
    
    log_verbose "Metrics extracted for $validator_name"
    return 0
}

# Generate comparison report
generate_comparison_report() {
    log_comparison "Generating validator comparison report..."
    
    # Extract metrics for both validators
    local agave_metrics="$BASE_RESULTS_DIR/agave_common_metrics.json"
    local slonana_metrics="$BASE_RESULTS_DIR/slonana_common_metrics.json"
    
    extract_common_metrics "agave" "$AGAVE_RESULTS_DIR" "$agave_metrics"
    extract_common_metrics "slonana" "$SLONANA_RESULTS_DIR" "$slonana_metrics"
    
    # Check if both metric files exist
    if [[ ! -f "$agave_metrics" ]] || [[ ! -f "$slonana_metrics" ]]; then
        log_error "Failed to extract metrics from one or both validators"
        return 1
    fi
    
    # Read metrics
    local agave_rpc_latency=$(jq -r '.common_metrics.rpc_latency_ms' "$agave_metrics")
    local agave_tps=$(jq -r '.common_metrics.transaction_throughput_tps' "$agave_metrics")
    local agave_memory=$(jq -r '.common_metrics.memory_usage_mb' "$agave_metrics")
    local agave_cpu=$(jq -r '.common_metrics.cpu_usage_percent' "$agave_metrics")
    local agave_startup=$(jq -r '.common_metrics.startup_time_seconds' "$agave_metrics")
    local agave_slot_rate=$(jq -r '.common_metrics.slot_processing_rate' "$agave_metrics")
    
    local slonana_rpc_latency=$(jq -r '.common_metrics.rpc_latency_ms' "$slonana_metrics")
    local slonana_tps=$(jq -r '.common_metrics.transaction_throughput_tps' "$slonana_metrics")
    local slonana_memory=$(jq -r '.common_metrics.memory_usage_mb' "$slonana_metrics")
    local slonana_cpu=$(jq -r '.common_metrics.cpu_usage_percent' "$slonana_metrics")
    local slonana_startup=$(jq -r '.common_metrics.startup_time_seconds' "$slonana_metrics")
    local slonana_slot_rate=$(jq -r '.common_metrics.slot_processing_rate' "$slonana_metrics")
    
    # Calculate performance ratios (Slonana vs Agave)
    local rpc_ratio="N/A"
    local tps_ratio="N/A"
    local memory_ratio="N/A"
    local cpu_ratio="N/A"
    local startup_ratio="N/A"
    local slot_ratio="N/A"
    
    if [[ "$agave_rpc_latency" != "0" && "$agave_rpc_latency" != "null" ]]; then
        rpc_ratio=$(echo "scale=2; $agave_rpc_latency / $slonana_rpc_latency" | bc -l 2>/dev/null || echo "N/A")
    fi
    
    if [[ "$agave_tps" != "0" && "$agave_tps" != "null" ]]; then
        tps_ratio=$(echo "scale=2; $slonana_tps / $agave_tps" | bc -l 2>/dev/null || echo "N/A")
    fi
    
    if [[ "$agave_memory" != "0" && "$agave_memory" != "null" ]]; then
        memory_ratio=$(echo "scale=2; $agave_memory / $slonana_memory" | bc -l 2>/dev/null || echo "N/A")
    fi
    
    if [[ "$agave_cpu" != "0" && "$agave_cpu" != "null" ]]; then
        cpu_ratio=$(echo "scale=2; $agave_cpu / $slonana_cpu" | bc -l 2>/dev/null || echo "N/A")
    fi
    
    if [[ "$agave_startup" != "0" && "$agave_startup" != "null" ]]; then
        startup_ratio=$(echo "scale=2; $agave_startup / $slonana_startup" | bc -l 2>/dev/null || echo "N/A")
    fi
    
    if [[ "$agave_slot_rate" != "0" && "$agave_slot_rate" != "null" ]]; then
        slot_ratio=$(echo "scale=2; $slonana_slot_rate / $agave_slot_rate" | bc -l 2>/dev/null || echo "N/A")
    fi
    
    # Generate comprehensive comparison report
    local report_file="$BASE_RESULTS_DIR/validator_comparison_report.txt"
    
    cat > "$report_file" << EOF
Validator Performance Comparison Report
=======================================
Generated: $(date)
Test Duration: ${TEST_DURATION} seconds
Environment: $(uname -a)

Common Metrics Comparison
========================

RPC Response Latency:
  • Agave:   ${agave_rpc_latency}ms
  • Slonana: ${slonana_rpc_latency}ms
  • Ratio:   ${rpc_ratio}x (Agave latency / Slonana latency)

Transaction Throughput:
  • Agave:   ${agave_tps} TPS
  • Slonana: ${slonana_tps} TPS
  • Ratio:   ${tps_ratio}x (Slonana / Agave)

Memory Usage:
  • Agave:   ${agave_memory}MB
  • Slonana: ${slonana_memory}MB
  • Ratio:   ${memory_ratio}x (Agave / Slonana)

CPU Utilization:
  • Agave:   ${agave_cpu}%
  • Slonana: ${slonana_cpu}%
  • Ratio:   ${cpu_ratio}x (Agave / Slonana)

Startup Time:
  • Agave:   ${agave_startup}s
  • Slonana: ${slonana_startup}s
  • Ratio:   ${startup_ratio}x (Agave / Slonana)

Slot Processing Rate:
  • Agave:   ${agave_slot_rate} slots/sec
  • Slonana: ${slonana_slot_rate} slots/sec
  • Ratio:   ${slot_ratio}x (Slonana / Agave)

Performance Summary
==================
$(if [[ "$tps_ratio" != "N/A" ]] && (( $(echo "$tps_ratio > 1" | bc -l) )); then echo "✅ Slonana shows ${tps_ratio}x higher transaction throughput"; else echo "⚠️  Transaction throughput comparison: $tps_ratio"; fi)
$(if [[ "$rpc_ratio" != "N/A" ]] && (( $(echo "$rpc_ratio > 1" | bc -l) )); then echo "✅ Slonana shows ${rpc_ratio}x lower RPC latency"; else echo "⚠️  RPC latency comparison: $rpc_ratio"; fi)
$(if [[ "$memory_ratio" != "N/A" ]] && (( $(echo "$memory_ratio > 1" | bc -l) )); then echo "✅ Slonana uses ${memory_ratio}x less memory"; else echo "⚠️  Memory usage comparison: $memory_ratio"; fi)
$(if [[ "$slot_ratio" != "N/A" ]] && (( $(echo "$slot_ratio > 1" | bc -l) )); then echo "✅ Slonana shows ${slot_ratio}x higher slot processing rate"; else echo "⚠️  Slot processing comparison: $slot_ratio"; fi)

Notes:
• All metrics compared under identical test conditions
• Ratios > 1.0 indicate Slonana advantage for throughput metrics
• Ratios > 1.0 indicate Agave advantage for latency/resource metrics
• N/A indicates insufficient data for comparison

Detailed Results:
• Agave results: $AGAVE_RESULTS_DIR/
• Slonana results: $SLONANA_RESULTS_DIR/
• Raw metrics: $BASE_RESULTS_DIR/*_common_metrics.json
EOF
    
    # Generate JSON comparison report
    local json_report="$BASE_RESULTS_DIR/validator_comparison.json"
    cat > "$json_report" << EOF
{
  "comparison_report": {
    "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
    "test_duration_seconds": $TEST_DURATION,
    "validators": {
      "agave": {
        "rpc_latency_ms": $agave_rpc_latency,
        "transaction_throughput_tps": $agave_tps,
        "memory_usage_mb": $agave_memory,
        "cpu_usage_percent": $agave_cpu,
        "startup_time_seconds": $agave_startup,
        "slot_processing_rate": $agave_slot_rate
      },
      "slonana": {
        "rpc_latency_ms": $slonana_rpc_latency,
        "transaction_throughput_tps": $slonana_tps,
        "memory_usage_mb": $slonana_memory,
        "cpu_usage_percent": $slonana_cpu,
        "startup_time_seconds": $slonana_startup,
        "slot_processing_rate": $slonana_slot_rate
      }
    },
    "performance_ratios": {
      "rpc_latency_improvement": "$rpc_ratio",
      "throughput_improvement": "$tps_ratio",
      "memory_efficiency": "$memory_ratio",
      "cpu_efficiency": "$cpu_ratio",
      "startup_improvement": "$startup_ratio",
      "slot_rate_improvement": "$slot_ratio"
    }
  }
}
EOF
    
    log_success "Comparison report generated:"
    log_info "  Text report: $report_file"
    log_info "  JSON report: $json_report"
    
    # Display summary
    echo ""
    echo "=========================================="
    echo "VALIDATOR PERFORMANCE COMPARISON SUMMARY"
    echo "=========================================="
    echo ""
    printf "%-25s | %-15s | %-15s | %-10s\n" "Metric" "Agave" "Slonana" "Ratio"
    printf "%-25s | %-15s | %-15s | %-10s\n" "=========================" "===============" "===============" "=========="
    printf "%-25s | %-15s | %-15s | %-10s\n" "RPC Latency (ms)" "$agave_rpc_latency" "$slonana_rpc_latency" "$rpc_ratio"
    printf "%-25s | %-15s | %-15s | %-10s\n" "Throughput (TPS)" "$agave_tps" "$slonana_tps" "$tps_ratio"
    printf "%-25s | %-15s | %-15s | %-10s\n" "Memory Usage (MB)" "$agave_memory" "$slonana_memory" "$memory_ratio"
    printf "%-25s | %-15s | %-15s | %-10s\n" "CPU Usage (%)" "$agave_cpu" "$slonana_cpu" "$cpu_ratio"
    printf "%-25s | %-15s | %-15s | %-10s\n" "Startup Time (s)" "$agave_startup" "$slonana_startup" "$startup_ratio"
    printf "%-25s | %-15s | %-15s | %-10s\n" "Slot Rate (slots/s)" "$agave_slot_rate" "$slonana_slot_rate" "$slot_ratio"
    echo ""
    echo "✅ Higher ratios indicate Slonana advantage for throughput metrics"
    echo "✅ Higher ratios indicate Agave advantage for resource usage metrics"
    echo "=========================================="
}

# Cleanup temporary files
cleanup_files() {
    if [[ "$CLEANUP_AFTER" == true ]]; then
        log_info "Cleaning up temporary files..."
        
        # Keep results but remove temporary ledger data
        rm -rf "$AGAVE_LEDGER_DIR" "$SLONANA_LEDGER_DIR" 2>/dev/null || true
        
        # Remove individual metric files (keep main results)
        rm -f "$BASE_RESULTS_DIR"/*_common_metrics.json 2>/dev/null || true
        
        log_verbose "Temporary files cleaned up"
    else
        log_info "Keeping all temporary files as requested"
    fi
}

# Main execution
main() {
    echo "=================================================="
    echo "VALIDATOR PERFORMANCE COMPARISON TOOL"
    echo "=================================================="
    log_info "Starting comprehensive validator comparison..."
    
    parse_arguments "$@"
    check_dependencies
    setup_environment
    
    local overall_start=$(date +%s.%N)
    
    # Run benchmarks sequentially to avoid port conflicts
    log_info "Running Agave validator benchmark (this may take a while)..."
    if ! run_agave_benchmark; then
        log_error "Agave benchmark failed, continuing with Slonana only"
    fi
    
    log_info "Running Slonana validator benchmark (this may take a while)..."
    if ! run_slonana_benchmark; then
        log_error "Slonana benchmark failed"
        exit 5
    fi
    
    # Generate comparison report
    generate_comparison_report
    
    local overall_end=$(date +%s.%N)
    local total_time=$(echo "$overall_end - $overall_start" | bc -l)
    
    # Cleanup
    cleanup_files
    
    log_success "Validator comparison completed in ${total_time}s"
    log_info "Results available in: $BASE_RESULTS_DIR"
}

# Execute main function with all arguments
main "$@"

# Ensure successful completion returns exit code 0
exit 0