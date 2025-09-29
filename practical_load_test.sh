#!/bin/bash

# Practical TPS Load Testing - Focused on realistic CI environment testing
# Tests multiple TPS rates to find capacity limits at different success rate thresholds

cd /home/runner/work/slonana.cpp/slonana.cpp

echo "=== PRACTICAL TPS LOAD TESTING ==="
echo "Testing validator TPS capacity at different success rate thresholds"
echo ""

# Configuration for CI environment
TEST_DURATION=20
STARTUP_TIME=15
RATES_TO_TEST=(1 2 3 5 8 10 15 20)

# Results tracking
declare -A test_results
declare -A success_rates

# Ensure validator is built
if [[ ! -f build/slonana_validator ]]; then
    echo "❌ Validator binary not found. Run 'make' first."
    exit 1
fi

# Function to test specific TPS rate
test_tps_rate() {
    local target_tps="$1"
    local test_id="$2"
    
    echo "🧪 Testing TPS Rate: $target_tps"
    echo "  Target: $target_tps transactions/second for ${TEST_DURATION}s"
    
    # Calculate timing
    local delay_ms=$((1000 / target_tps))
    local delay_sec=$(echo "scale=3; $delay_ms / 1000" | bc -l)
    
    # Start fresh validator
    local ledger_dir="load_test_${test_id}_${target_tps}"
    rm -rf "$ledger_dir"
    mkdir -p "$ledger_dir"
    
    ./build/slonana_validator \
        --ledger-path "$ledger_dir" \
        --rpc-bind-address 127.0.0.1:8899 \
        --gossip-bind-address 127.0.0.1:8001 \
        --log-level error \
        --network-id devnet \
        --allow-stale-rpc \
        --faucet-port 9900 \
        --rpc-faucet-address 127.0.0.1:9900 > "$ledger_dir/validator.log" 2>&1 &
    
    local validator_pid=$!
    echo "  Validator started (PID: $validator_pid), waiting ${STARTUP_TIME}s..."
    sleep $STARTUP_TIME
    
    # Check if validator is responsive
    local health_check=$(curl -s "http://localhost:8899" -X POST -H "Content-Type: application/json" \
        -d '{"jsonrpc":"2.0","id":1,"method":"getHealth"}' 2>/dev/null)
    
    if ! echo "$health_check" | grep -q "ok"; then
        echo "  ❌ Validator not responsive"
        kill -TERM $validator_pid 2>/dev/null
        return 1
    fi
    
    echo "  ✅ Validator ready, starting load test..."
    
    # Load test execution
    local submitted=0
    local successful=0
    local failed=0
    local start_time=$(date +%s)
    local end_time=$((start_time + TEST_DURATION))
    
    while [[ $(date +%s) -lt $end_time ]]; do
        submitted=$((submitted + 1))
        
        # Create unique transaction
        local tx_suffix=$(printf "%04d" $((submitted % 10000)))
        local response=$(curl -s -X POST "http://localhost:8899" -H "Content-Type: application/json" \
            -d "{\"jsonrpc\":\"2.0\",\"id\":$submitted,\"method\":\"sendTransaction\",\"params\":[\"AQABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9REQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpbXF1eX2BhYmNkZWZnaGlqa2xtbm9wcXJzdHV2d3h5ent8fX5/gIGCg4SFhoeIiYqLjI2Oj5CRkpOUlZaXmJmam5ydnp+goaKjpKWmp6ipqqusra6vsLGys7S1tre4ubq7vL2+v8DBwsPExcbHyMnKy8zNzs/Q0dLT1NXW19jZ2tvc3d7f4OHi4+Tl5ufo6err7O3u7/Dx8vP09fb3+Pn6+/z9/v$tx_suffix=\"]}")
        
        if echo "$response" | grep -q '"result"'; then
            successful=$((successful + 1))
        else
            failed=$((failed + 1))
        fi
        
        # Rate limiting
        sleep "$delay_sec"
    done
    
    # Calculate results
    local actual_duration=$(($(date +%s) - start_time))
    local success_rate=$(echo "scale=1; $successful * 100 / $submitted" | bc -l)
    local actual_tps=$(echo "scale=1; $successful / $actual_duration" | bc -l)
    
    echo "  📊 Results:"
    echo "    Submitted: $submitted transactions"
    echo "    Successful: $successful transactions" 
    echo "    Failed: $failed transactions"
    echo "    Success Rate: ${success_rate}%"
    echo "    Actual TPS: $actual_tps"
    
    # Store results
    test_results[$target_tps]="$submitted,$successful,$failed,$success_rate,$actual_tps"
    success_rates[$target_tps]=$success_rate
    
    # Graceful shutdown
    echo "  Stopping validator..."
    kill -TERM $validator_pid 2>/dev/null
    sleep 3
    
    echo ""
    return 0
}

# Main execution
echo "Starting practical load testing with rates: ${RATES_TO_TEST[*]}"
echo ""

# Test each rate
test_count=0
for rate in "${RATES_TO_TEST[@]}"; do
    test_count=$((test_count + 1))
    echo "=== Test $test_count/${#RATES_TO_TEST[@]} ==="
    test_tps_rate $rate $test_count
done

# Analyze results and find thresholds
echo "=== LOAD TEST ANALYSIS ==="
echo ""

max_100_percent=0
max_97_percent=0
max_95_percent=0

echo "📈 Performance Summary:"
printf "%-8s %-12s %-12s %-12s %-15s %-10s\n" "TPS" "Submitted" "Successful" "Failed" "Success Rate" "Actual TPS"
echo "------------------------------------------------------------------------"

for rate in "${RATES_TO_TEST[@]}"; do
    if [[ -n "${test_results[$rate]}" ]]; then
        IFS=',' read -r submitted successful failed success_rate actual_tps <<< "${test_results[$rate]}"
        printf "%-8s %-12s %-12s %-12s %-15s %-10s\n" "$rate" "$submitted" "$successful" "$failed" "${success_rate}%" "$actual_tps"
        
        # Find maximum TPS for each threshold
        success_int=$(echo "$success_rate" | cut -d. -f1)
        if [[ $success_int -eq 100 && $rate -gt $max_100_percent ]]; then
            max_100_percent=$rate
        fi
        if [[ $success_int -ge 97 && $rate -gt $max_97_percent ]]; then
            max_97_percent=$rate
        fi
        if [[ $success_int -ge 95 && $rate -gt $max_95_percent ]]; then
            max_95_percent=$rate
        fi
    fi
done

echo ""
echo "🎯 Maximum Sustainable TPS by Success Rate Threshold:"
echo "  • 100% Success Rate: $max_100_percent TPS (Perfect reliability)"
echo "  • ≥97% Success Rate: $max_97_percent TPS (High reliability)"
echo "  • ≥95% Success Rate: $max_95_percent TPS (Production ready)"
echo ""

# Performance recommendations
echo "💡 Performance Recommendations:"
if [[ $max_100_percent -gt 0 ]]; then
    echo "  • For mission-critical applications requiring 100% reliability: $max_100_percent TPS"
fi
if [[ $max_97_percent -gt $max_100_percent ]]; then
    echo "  • For high-throughput with occasional failures acceptable: $max_97_percent TPS"
fi
if [[ $max_95_percent -gt $max_97_percent ]]; then
    echo "  • For maximum throughput with 5% failure tolerance: $max_95_percent TPS"
fi

echo ""
echo "📝 Notes:"
echo "  • Tests conducted in CI environment with limited resources"
echo "  • Production deployment may achieve higher TPS with dedicated hardware"
echo "  • Results based on ${TEST_DURATION}-second test windows"

# Save results
cat > load_test_results.json << EOF
{
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "test_environment": "CI",
  "test_duration_seconds": $TEST_DURATION,
  "maximum_sustainable_tps": {
    "100_percent_success": $max_100_percent,
    "97_percent_success": $max_97_percent,
    "95_percent_success": $max_95_percent
  },
  "detailed_results": {
EOF

first=true
for rate in "${RATES_TO_TEST[@]}"; do
    if [[ -n "${test_results[$rate]}" ]]; then
        IFS=',' read -r submitted successful failed success_rate actual_tps <<< "${test_results[$rate]}"
        if [[ $first == false ]]; then
            echo "," >> load_test_results.json
        fi
        cat >> load_test_results.json << EOF
    "${rate}_tps": {
      "submitted": $submitted,
      "successful": $successful,
      "failed": $failed,
      "success_rate_percent": $success_rate,
      "actual_tps": $actual_tps
    }EOF
        first=false
    fi
done

cat >> load_test_results.json << EOF

  }
}
EOF

echo ""
echo "✅ Load testing completed successfully!"
echo "📄 Detailed results saved to load_test_results.json"