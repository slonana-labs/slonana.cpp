#!/bin/bash

# Comprehensive TPS Load Testing Suite
# Determines maximum TPS capacity at different success rates (100%, 97%, 95%)

cd /home/runner/work/slonana.cpp/slonana.cpp

echo "=== SLONANA VALIDATOR TPS LOAD TESTING SUITE ==="
echo "Finding maximum sustainable TPS at different success rate thresholds"
echo ""

# Configuration
VALIDATOR_STARTUP_TIME=20
TEST_DURATION=30
COOLDOWN_TIME=10
MAX_TPS_TO_TEST=50
MIN_SUCCESS_RATE_100=100
MIN_SUCCESS_RATE_97=97
MIN_SUCCESS_RATE_95=95

# Results storage
declare -A results_100
declare -A results_97  
declare -A results_95
declare -A success_rates

# Build validator if needed
if [[ ! -f build/slonana_validator ]]; then
    echo "Building validator..."
    make -j$(nproc) > /dev/null 2>&1
    if [[ $? -ne 0 ]]; then
        echo "❌ Build failed"
        exit 1
    fi
fi

# Function to start validator
start_validator() {
    local ledger_dir="$1"
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
    
    echo $!
}

# Function to wait for validator readiness
wait_for_validator() {
    local timeout=30
    local count=0
    
    while [[ $count -lt $timeout ]]; do
        if curl -s "http://localhost:8899" -X POST -H "Content-Type: application/json" \
           -d '{"jsonrpc":"2.0","id":1,"method":"getHealth"}' | grep -q "ok"; then
            return 0
        fi
        sleep 1
        count=$((count + 1))
    done
    return 1
}

# Function to test TPS at specific rate
test_tps_rate() {
    local target_tps="$1"
    local test_name="$2"
    
    echo "  Testing TPS: $target_tps"
    
    local submitted=0
    local successful=0
    local failed=0
    local start_time=$(date +%s)
    local end_time=$((start_time + TEST_DURATION))
    
    # Calculate delay between transactions to achieve target TPS
    local delay_ms=$(echo "scale=3; 1000 / $target_tps" | bc -l)
    local delay_sec=$(echo "scale=3; $delay_ms / 1000" | bc -l)
    
    # Send transactions at target rate
    while [[ $(date +%s) -lt $end_time ]]; do
        submitted=$((submitted + 1))
        
        # Create unique transaction data
        local tx_id=$(printf "%08d" $submitted)
        local response=$(curl -s -X POST "http://localhost:8899" -H "Content-Type: application/json" \
            -d "{\"jsonrpc\":\"2.0\",\"id\":$submitted,\"method\":\"sendTransaction\",\"params\":[\"AQABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9AQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpbXF1eX2BhYmNkZWZnaGlqa2xtbm9wcXJzdHV2d3h5ent8fX5/gIGCg4SFhoeIiYqLjI2Oj5CRkpOUlZaXmJmam5ydnp+goaKjpKWmp6ipqqusra6vsLGys7S1tre4ubq7vL2+v8DBwsPExcbHyMnKy8zNzs/Q0dLT1NXW19jZ2tvc3d7f4OHi4+Tl5ufo6err7O3u7/Dx8vP09fb3+Pn6+/z9/v${tx_id}=\"]}")
        
        if echo "$response" | grep -q '"result"'; then
            successful=$((successful + 1))
        else
            failed=$((failed + 1))
        fi
        
        # Precise timing control
        sleep "$delay_sec"
    done
    
    # Calculate metrics
    local actual_duration=$(($(date +%s) - start_time))
    local success_rate=$(echo "scale=2; $successful * 100 / $submitted" | bc -l)
    local actual_tps=$(echo "scale=2; $successful / $actual_duration" | bc -l)
    
    echo "    Submitted: $submitted, Successful: $successful, Failed: $failed"
    echo "    Success Rate: ${success_rate}%, Actual TPS: $actual_tps"
    
    # Store results
    success_rates[$target_tps]=$success_rate
    
    # Return success rate as integer for comparison
    echo "$success_rate" | cut -d. -f1
}

# Function to find maximum TPS for success rate threshold
find_max_tps_for_threshold() {
    local threshold="$1"
    local threshold_name="$2"
    
    echo "🎯 Finding maximum TPS for ${threshold}% success rate threshold"
    
    local max_tps=0
    local low=1
    local high=$MAX_TPS_TO_TEST
    
    # Binary search approach for efficiency
    while [[ $low -le $high ]]; do
        local mid=$(( (low + high) / 2 ))
        
        echo "  Testing TPS range: $low-$high, testing: $mid"
        
        # Start fresh validator for each test
        local ledger_dir="load_test_${threshold}_${mid}"
        local validator_pid=$(start_validator "$ledger_dir")
        
        echo "  Starting validator (PID: $validator_pid)..."
        sleep $VALIDATOR_STARTUP_TIME
        
        if ! wait_for_validator; then
            echo "  ⚠️  Validator startup failed, skipping..."
            kill -TERM $validator_pid 2>/dev/null
            high=$((mid - 1))
            continue
        fi
        
        # Test this TPS rate
        local achieved_success_rate=$(test_tps_rate $mid "threshold_${threshold}")
        
        # Stop validator
        kill -TERM $validator_pid 2>/dev/null
        sleep $COOLDOWN_TIME
        
        if [[ $achieved_success_rate -ge $threshold ]]; then
            max_tps=$mid
            low=$((mid + 1))
            echo "  ✅ TPS $mid achieves ${achieved_success_rate}% (>= ${threshold}%)"
        else
            high=$((mid - 1))
            echo "  ❌ TPS $mid only achieves ${achieved_success_rate}% (< ${threshold}%)"
        fi
    done
    
    echo "  🏆 Maximum TPS for ${threshold}% success rate: $max_tps"
    
    case $threshold in
        100) results_100[$threshold_name]=$max_tps ;;
        97)  results_97[$threshold_name]=$max_tps ;;
        95)  results_95[$threshold_name]=$max_tps ;;
    esac
}

# Function to run detailed characterization at specific rates
characterize_tps_performance() {
    echo ""
    echo "🔬 Detailed TPS Performance Characterization"
    echo "Testing specific TPS rates to understand performance curve"
    
    local test_rates=(1 2 5 10 15 20 25 30)
    
    for rate in "${test_rates[@]}"; do
        echo ""
        echo "Characterizing TPS: $rate"
        
        # Start validator
        local ledger_dir="characterize_${rate}"
        local validator_pid=$(start_validator "$ledger_dir")
        
        sleep $VALIDATOR_STARTUP_TIME
        
        if wait_for_validator; then
            test_tps_rate $rate "characterization"
        else
            echo "  ⚠️  Validator startup failed"
        fi
        
        # Stop validator
        kill -TERM $validator_pid 2>/dev/null
        sleep $COOLDOWN_TIME
    done
}

# Main execution
echo "Starting comprehensive TPS load testing..."
echo ""

# Find maximum TPS for each threshold
find_max_tps_for_threshold 100 "perfect"
echo ""
find_max_tps_for_threshold 97 "high_reliability"
echo ""  
find_max_tps_for_threshold 95 "production_ready"

# Run detailed characterization
characterize_tps_performance

# Generate comprehensive report
echo ""
echo "=== COMPREHENSIVE TPS LOAD TEST RESULTS ==="
echo ""
echo "📊 Maximum Sustainable TPS by Success Rate Threshold:"
echo "  • 100% Success Rate: ${results_100[perfect]:-0} TPS (Perfect reliability)"
echo "  • 97% Success Rate:  ${results_97[high_reliability]:-0} TPS (High reliability)"  
echo "  • 95% Success Rate:  ${results_95[production_ready]:-0} TPS (Production ready)"
echo ""

echo "📈 Performance Characterization Curve:"
for rate in 1 2 5 10 15 20 25 30; do
    if [[ -n "${success_rates[$rate]}" ]]; then
        printf "  • %2d TPS: %6.1f%% success rate\n" $rate ${success_rates[$rate]}
    fi
done

echo ""
echo "🎯 Recommendations:"

if [[ ${results_100[perfect]:-0} -gt 0 ]]; then
    echo "  • For mission-critical applications: ${results_100[perfect]} TPS (100% reliability)"
fi

if [[ ${results_97[high_reliability]:-0} -gt 0 ]]; then
    echo "  • For high-throughput applications: ${results_97[high_reliability]} TPS (97% reliability)"
fi

if [[ ${results_95[production_ready]:-0} -gt 0 ]]; then
    echo "  • For maximum throughput: ${results_95[production_ready]} TPS (95% reliability)"
fi

echo ""
echo "Load testing completed successfully!"

# Save results to file
cat > load_test_results.json << EOF
{
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "test_duration_seconds": $TEST_DURATION,
  "maximum_tps": {
    "100_percent_success": ${results_100[perfect]:-0},
    "97_percent_success": ${results_97[high_reliability]:-0}, 
    "95_percent_success": ${results_95[production_ready]:-0}
  },
  "performance_curve": {
EOF

first=true
for rate in 1 2 5 10 15 20 25 30; do
    if [[ -n "${success_rates[$rate]}" ]]; then
        if [[ $first == false ]]; then
            echo "," >> load_test_results.json
        fi
        printf "    \"%d_tps\": %.1f" $rate ${success_rates[$rate]} >> load_test_results.json
        first=false
    fi
done

cat >> load_test_results.json << EOF

  },
  "test_methodology": {
    "binary_search_range": "1-$MAX_TPS_TO_TEST TPS",
    "test_duration_per_rate": "$TEST_DURATION seconds",
    "validator_startup_time": "$VALIDATOR_STARTUP_TIME seconds",
    "cooldown_between_tests": "$COOLDOWN_TIME seconds"
  }
}
EOF

echo "Results saved to load_test_results.json"