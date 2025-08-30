#!/bin/bash

# Benchmark Results Display Script
# Shows the latest benchmark comparison results

set -e

COMPARISON_FILE="benchmark_comparison.json"

if [ ! -f "$COMPARISON_FILE" ]; then
    echo "❌ No benchmark comparison results found."
    echo "Run the benchmark-comparison GitHub Action first."
    exit 1
fi

echo "📊 Latest Benchmark Comparison Results"
echo "======================================"
echo ""

# Parse the JSON results
BENCHMARK_DATE=$(jq -r '.benchmark_date' $COMPARISON_FILE)
AGAVE_TPS=$(jq -r '.results.agave.tps' $COMPARISON_FILE)
AGAVE_LATENCY=$(jq -r '.results.agave.rpc_latency_ms' $COMPARISON_FILE)
AGAVE_MEMORY=$(jq -r '.results.agave.memory_usage_mb' $COMPARISON_FILE)
AGAVE_CPU=$(jq -r '.results.agave.cpu_usage_percent' $COMPARISON_FILE)

SLONANA_TPS=$(jq -r '.results.slonana.tps' $COMPARISON_FILE)
SLONANA_LATENCY=$(jq -r '.results.slonana.rpc_latency_ms' $COMPARISON_FILE)
SLONANA_MEMORY=$(jq -r '.results.slonana.memory_usage_mb' $COMPARISON_FILE)
SLONANA_CPU=$(jq -r '.results.slonana.cpu_usage_percent' $COMPARISON_FILE)

TPS_IMPROVEMENT=$(jq -r '.results.improvements.tps_percent' $COMPARISON_FILE)
LATENCY_IMPROVEMENT=$(jq -r '.results.improvements.latency_percent' $COMPARISON_FILE)
MEMORY_IMPROVEMENT=$(jq -r '.results.improvements.memory_percent' $COMPARISON_FILE)

CORES=$(jq -r '.environment.cores' $COMPARISON_FILE)
MEMORY_GB=$(jq -r '.environment.memory_gb' $COMPARISON_FILE)

echo "🕐 Benchmark Date: $BENCHMARK_DATE"
echo "💻 Test Environment: $CORES cores, ${MEMORY_GB}GB RAM"
echo ""

echo "📈 Performance Comparison:"
echo "┌─────────────────────┬──────────────┬──────────────┬─────────────────┐"
echo "│ Metric              │ Agave        │ Slonana      │ Improvement     │"
echo "├─────────────────────┼──────────────┼──────────────┼─────────────────┤"
printf "│ Transaction TPS     │ %12s │ %12s │ %15s │\n" "$AGAVE_TPS" "$SLONANA_TPS" "${TPS_IMPROVEMENT}%"
printf "│ RPC Latency (ms)    │ %12s │ %12s │ %15s │\n" "$AGAVE_LATENCY" "$SLONANA_LATENCY" "${LATENCY_IMPROVEMENT}%"
printf "│ Memory Usage (MB)   │ %12s │ %12s │ %15s │\n" "$AGAVE_MEMORY" "$SLONANA_MEMORY" "${MEMORY_IMPROVEMENT}%"
printf "│ CPU Usage (%%)       │ %12s │ %12s │ %15s │\n" "$AGAVE_CPU" "$SLONANA_CPU" "N/A"
echo "└─────────────────────┴──────────────┴──────────────┴─────────────────┘"
echo ""

# Determine winner
if (( $(echo "$TPS_IMPROVEMENT > 0" | bc -l) )); then
    echo "🏆 Winner: Slonana.cpp with ${TPS_IMPROVEMENT}% better throughput"
else
    echo "🏆 Winner: Agave with $(echo "scale=1; -1 * $TPS_IMPROVEMENT" | bc -l)% better throughput"
fi

echo ""
echo "📁 Full results available in: $COMPARISON_FILE"
echo "🔄 To refresh results, re-run the GitHub Action workflow."