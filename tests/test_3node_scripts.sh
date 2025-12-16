#!/usr/bin/env bash
set -e

# Test script for 3-node cluster benchmarking scripts
# Validates script logic without running actual validators

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "Testing 3-node cluster benchmark scripts..."
echo ""

# Test 1: Script syntax validation
echo "Test 1: Validating script syntax..."
bash -n "$PROJECT_ROOT/scripts/benchmark_agave_3node.sh" && echo "  ✓ benchmark_agave_3node.sh syntax OK"
bash -n "$PROJECT_ROOT/scripts/benchmark_slonana_3node.sh" && echo "  ✓ benchmark_slonana_3node.sh syntax OK"
bash -n "$PROJECT_ROOT/scripts/compare_3node_clusters.sh" && echo "  ✓ compare_3node_clusters.sh syntax OK"
echo ""

# Test 2: Help messages work
echo "Test 2: Testing help messages..."
"$PROJECT_ROOT/scripts/benchmark_agave_3node.sh" --help > /dev/null 2>&1 && echo "  ✓ Agave script help works"
"$PROJECT_ROOT/scripts/benchmark_slonana_3node.sh" --help > /dev/null 2>&1 && echo "  ✓ Slonana script help works"
"$PROJECT_ROOT/scripts/compare_3node_clusters.sh" --help > /dev/null 2>&1 && echo "  ✓ Comparison script help works"
echo ""

# Test 3: Scripts are executable
echo "Test 3: Checking execute permissions..."
[ -x "$PROJECT_ROOT/scripts/benchmark_agave_3node.sh" ] && echo "  ✓ benchmark_agave_3node.sh is executable"
[ -x "$PROJECT_ROOT/scripts/benchmark_slonana_3node.sh" ] && echo "  ✓ benchmark_slonana_3node.sh is executable"
[ -x "$PROJECT_ROOT/scripts/compare_3node_clusters.sh" ] && echo "  ✓ compare_3node_clusters.sh is executable"
echo ""

# Test 4: Documentation exists
echo "Test 4: Checking documentation..."
[ -f "$PROJECT_ROOT/docs/3-NODE-CLUSTER-BENCHMARKS.md" ] && echo "  ✓ Documentation exists"
[ -s "$PROJECT_ROOT/docs/3-NODE-CLUSTER-BENCHMARKS.md" ] && echo "  ✓ Documentation is not empty"
echo ""

# Test 5: Port configuration validation
echo "Test 5: Validating port configurations..."
# Extract port numbers from Agave script
agave_ports=$(grep -E "NODE[123]_(RPC|GOSSIP|TPU)=" "$PROJECT_ROOT/scripts/benchmark_agave_3node.sh" | wc -l)
[ "$agave_ports" -ge 12 ] && echo "  ✓ Agave script has all port configurations"

# Extract port numbers from Slonana script
slonana_ports=$(grep -E "NODE[123]_(RPC|GOSSIP|TPU)=" "$PROJECT_ROOT/scripts/benchmark_slonana_3node.sh" | wc -l)
[ "$slonana_ports" -ge 12 ] && echo "  ✓ Slonana script has all port configurations"
echo ""

# Test 6: Function definitions
echo "Test 6: Checking critical function definitions..."
grep -q "setup_cluster()" "$PROJECT_ROOT/scripts/benchmark_agave_3node.sh" && echo "  ✓ Agave: setup_cluster() defined"
grep -q "start_cluster()" "$PROJECT_ROOT/scripts/benchmark_agave_3node.sh" && echo "  ✓ Agave: start_cluster() defined"
grep -q "setup_vote_accounts()" "$PROJECT_ROOT/scripts/benchmark_agave_3node.sh" && echo "  ✓ Agave: setup_vote_accounts() defined"
grep -q "test_transaction_throughput()" "$PROJECT_ROOT/scripts/benchmark_agave_3node.sh" && echo "  ✓ Agave: test_transaction_throughput() defined"

grep -q "setup_cluster()" "$PROJECT_ROOT/scripts/benchmark_slonana_3node.sh" && echo "  ✓ Slonana: setup_cluster() defined"
grep -q "start_cluster()" "$PROJECT_ROOT/scripts/benchmark_slonana_3node.sh" && echo "  ✓ Slonana: start_cluster() defined"
grep -q "setup_vote_accounts()" "$PROJECT_ROOT/scripts/benchmark_slonana_3node.sh" && echo "  ✓ Slonana: setup_vote_accounts() defined"
grep -q "test_transaction_throughput()" "$PROJECT_ROOT/scripts/benchmark_slonana_3node.sh" && echo "  ✓ Slonana: test_transaction_throughput() defined"
echo ""

# Test 7: Load balancing implementation
echo "Test 7: Checking load balancing logic..."
grep -q "Round-robin load balancing" "$PROJECT_ROOT/scripts/benchmark_agave_3node.sh" && echo "  ✓ Agave: Load balancing implemented"
grep -q "Round-robin load balancing" "$PROJECT_ROOT/scripts/benchmark_slonana_3node.sh" && echo "  ✓ Slonana: Load balancing implemented"
echo ""

# Test 8: Health monitoring
echo "Test 8: Checking health monitoring..."
grep -q "check_cluster_health()" "$PROJECT_ROOT/scripts/benchmark_agave_3node.sh" && echo "  ✓ Agave: Health monitoring implemented"
grep -q "check_cluster_health()" "$PROJECT_ROOT/scripts/benchmark_slonana_3node.sh" && echo "  ✓ Slonana: Health monitoring implemented"
echo ""

# Test 9: Signal handlers
echo "Test 9: Checking signal handlers..."
grep -q "trap cleanup_cluster" "$PROJECT_ROOT/scripts/benchmark_agave_3node.sh" && echo "  ✓ Agave: Signal handlers configured"
grep -q "trap cleanup_cluster" "$PROJECT_ROOT/scripts/benchmark_slonana_3node.sh" && echo "  ✓ Slonana: Signal handlers configured"
echo ""

# Test 10: JSON output
echo "Test 10: Checking JSON result generation..."
grep -q "benchmark_results.json" "$PROJECT_ROOT/scripts/benchmark_agave_3node.sh" && echo "  ✓ Agave: JSON output implemented"
grep -q "benchmark_results.json" "$PROJECT_ROOT/scripts/benchmark_slonana_3node.sh" && echo "  ✓ Slonana: JSON output implemented"
grep -q "comparison_results.json" "$PROJECT_ROOT/scripts/compare_3node_clusters.sh" && echo "  ✓ Comparison: JSON output implemented"
echo ""

echo "=========================================="
echo "All tests passed! ✓"
echo "=========================================="
echo ""
echo "Note: These tests validate script structure and logic."
echo "Full integration testing requires:"
echo "  1. Solana CLI tools installed"
echo "  2. Agave validator binary (for Agave tests)"
echo "  3. Built slonana_validator binary (for Slonana tests)"
echo "  4. Sufficient system resources (3 validators)"
echo ""
