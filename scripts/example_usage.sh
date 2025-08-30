#!/usr/bin/env bash
# Example usage script for the standalone benchmark system
# Demonstrates how to use the new benchmark scripts locally

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}Slonana Benchmark System - Usage Examples${NC}"
echo "========================================"
echo ""

echo -e "${GREEN}1. Basic benchmark comparison (requires Solana CLI tools):${NC}"
echo "   # Run Agave benchmark"
echo "   ./scripts/benchmark_agave.sh --ledger /tmp/agave_ledger --results /tmp/agave_results"
echo ""
echo "   # Run Slonana benchmark" 
echo "   ./scripts/benchmark_slonana.sh --ledger /tmp/slonana_ledger --results /tmp/slonana_results"
echo ""

echo -e "${GREEN}2. Custom test duration and ports:${NC}"
echo "   ./scripts/benchmark_agave.sh \\"
echo "     --ledger /tmp/ledger --results /tmp/results \\"
echo "     --test-duration 120 --rpc-port 9899 --gossip-port 9001"
echo ""

echo -e "${GREEN}3. Bootstrap-only mode (setup testing):${NC}"
echo "   ./scripts/benchmark_slonana.sh \\"
echo "     --ledger /tmp/ledger --results /tmp/results \\"
echo "     --bootstrap-only"
echo ""

echo -e "${GREEN}4. Placeholder mode (when binary not available):${NC}"
echo "   ./scripts/benchmark_slonana.sh \\"
echo "     --ledger /tmp/ledger --results /tmp/results \\"
echo "     --use-placeholder"
echo ""

echo -e "${GREEN}5. Custom binary paths:${NC}"
echo "   ./scripts/benchmark_agave.sh \\"
echo "     --ledger /tmp/ledger --results /tmp/results \\"
echo "     --validator-bin /usr/local/bin/agave-validator"
echo ""

echo -e "${GREEN}6. Verbose logging:${NC}"
echo "   ./scripts/benchmark_slonana.sh \\"
echo "     --ledger /tmp/ledger --results /tmp/results \\"
echo "     --verbose"
echo ""

echo -e "${YELLOW}Pre-commit Testing:${NC}"
echo "The system includes pre-commit hooks that validate scripts before commit:"
echo "   # Manual pre-commit test"
echo "   .git/hooks/pre-commit"
echo ""
echo "   # Install pre-commit framework (optional)"
echo "   pip install pre-commit"
echo "   pre-commit install"
echo ""

echo -e "${YELLOW}GitHub Actions Integration:${NC}"
echo "The workflow now uses these standalone scripts:"
echo "   # View workflow"
echo "   cat .github/workflows/benchmark-comparison.yml"
echo ""

echo -e "${YELLOW}Example: Run a quick local comparison${NC}"
echo "if you have Solana CLI tools installed:"
echo ""

read -p "Would you like to run a quick demo? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo -e "${BLUE}Running quick benchmark demo...${NC}"
    
    TEMP_BASE="/tmp/slonana_benchmark_demo_$(date +%s)"
    mkdir -p "$TEMP_BASE"
    
    echo "Testing Slonana script help:"
    if ./scripts/benchmark_slonana.sh --help | head -5; then
        echo -e "${GREEN}✅ Slonana script help works${NC}"
    else
        echo -e "${YELLOW}⚠️  Slonana script help failed${NC}"
    fi
    
    echo ""
    echo "Testing placeholder mode:"
    if ./scripts/benchmark_slonana.sh \
        --ledger "$TEMP_BASE/slonana_ledger" \
        --results "$TEMP_BASE/slonana_results" \
        --use-placeholder \
        --test-duration 10; then
        echo -e "${GREEN}✅ Placeholder mode works${NC}"
        echo "Results:"
        cat "$TEMP_BASE/slonana_results/benchmark_results.json" | jq .
    else
        echo -e "${YELLOW}⚠️  Placeholder mode failed${NC}"
    fi
    
    # Cleanup
    rm -rf "$TEMP_BASE"
    
    echo -e "${GREEN}Demo completed!${NC}"
else
    echo "Demo skipped. You can run the commands above manually."
fi

echo ""
echo -e "${BLUE}Script locations:${NC}"
echo "  Agave benchmark: $PROJECT_ROOT/scripts/benchmark_agave.sh"
echo "  Slonana benchmark: $PROJECT_ROOT/scripts/benchmark_slonana.sh"
echo "  Pre-commit hook: $PROJECT_ROOT/.git/hooks/pre-commit"
echo "  Pre-commit config: $PROJECT_ROOT/.pre-commit-config.yaml"
echo "  GitHub workflow: $PROJECT_ROOT/.github/workflows/benchmark-comparison.yml"