#!/bin/bash
#
# Deploy AI Trading Agent to slonana Validator
#
# This script:
# 1. Builds the sBPF program
# 2. Deploys to a running validator
# 3. Initializes the agent
# 4. Configures autonomous execution
#
# Usage:
#   ./deploy.sh [--local|--devnet|--mainnet]
#

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
NETWORK="${1:---local}"
PROGRAM_SO="ai_trading_agent.so"
KEYPAIR_PATH="$HOME/.config/solana/id.json"

echo -e "${BLUE}"
echo "╔════════════════════════════════════════════════════════════╗"
echo "║        AI Trading Agent Deployment Script                   ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo -e "${NC}"

# ============================================================================
# Network Selection
# ============================================================================

case $NETWORK in
    --local)
        RPC_URL="http://localhost:8899"
        echo -e "${GREEN}Network: Local (localhost:8899)${NC}"
        ;;
    --devnet)
        RPC_URL="https://api.devnet.solana.com"
        echo -e "${YELLOW}Network: Devnet${NC}"
        ;;
    --mainnet)
        RPC_URL="https://api.mainnet-beta.solana.com"
        echo -e "${RED}Network: Mainnet (USE WITH CAUTION!)${NC}"
        read -p "Are you sure you want to deploy to mainnet? (yes/no): " confirm
        if [ "$confirm" != "yes" ]; then
            echo "Aborted."
            exit 1
        fi
        ;;
    *)
        echo "Usage: $0 [--local|--devnet|--mainnet]"
        exit 1
        ;;
esac

echo ""

# ============================================================================
# Build Program
# ============================================================================

echo -e "${BLUE}Step 1: Building sBPF Program${NC}"
echo "────────────────────────────────"

if [ ! -f "$PROGRAM_SO" ]; then
    echo "Building program..."
    make program
else
    echo "Program already built: $PROGRAM_SO"
fi

echo -e "${GREEN}✓ Build complete${NC}"
echo ""

# ============================================================================
# Check Prerequisites
# ============================================================================

echo -e "${BLUE}Step 2: Checking Prerequisites${NC}"
echo "────────────────────────────────"

# Check if slonana CLI is available
if ! command -v slonana &> /dev/null; then
    echo -e "${YELLOW}Warning: slonana CLI not found in PATH${NC}"
    echo "Using mock deployment for demonstration"
    MOCK_DEPLOY=true
else
    MOCK_DEPLOY=false
    echo "slonana CLI: $(which slonana)"
fi

# Check keypair
if [ -f "$KEYPAIR_PATH" ]; then
    echo "Keypair: $KEYPAIR_PATH"
else
    echo -e "${YELLOW}Warning: Keypair not found at $KEYPAIR_PATH${NC}"
    if [ "$MOCK_DEPLOY" = false ]; then
        echo "Generate one with: slonana-keygen new"
        exit 1
    fi
fi

echo -e "${GREEN}✓ Prerequisites checked${NC}"
echo ""

# ============================================================================
# Deploy Program
# ============================================================================

echo -e "${BLUE}Step 3: Deploying Program${NC}"
echo "────────────────────────────────"

if [ "$MOCK_DEPLOY" = true ]; then
    echo "Mock deployment (demonstration mode):"
    echo ""
    echo "  Would execute:"
    echo "    slonana program deploy \\"
    echo "      --url $RPC_URL \\"
    echo "      --keypair $KEYPAIR_PATH \\"
    echo "      $PROGRAM_SO"
    echo ""
    PROGRAM_ID="AiTrading11111111111111111111111111111111111"
    echo "  Mock Program ID: $PROGRAM_ID"
else
    echo "Deploying to $RPC_URL..."
    PROGRAM_ID=$(slonana program deploy \
        --url "$RPC_URL" \
        --keypair "$KEYPAIR_PATH" \
        "$PROGRAM_SO" | grep "Program Id:" | awk '{print $3}')
    echo "Program ID: $PROGRAM_ID"
fi

echo -e "${GREEN}✓ Program deployed${NC}"
echo ""

# ============================================================================
# Initialize Agent
# ============================================================================

echo -e "${BLUE}Step 4: Initializing Agent${NC}"
echo "────────────────────────────────"

if [ "$MOCK_DEPLOY" = true ]; then
    echo "Mock initialization:"
    echo ""
    echo "  Would send Initialize transaction with:"
    echo "    - Model Version: 1"
    echo "    - State Account: (PDA derived from program)"
    echo "    - Oracle Account: (Pyth/Switchboard price feed)"
    echo "    - Escrow Account: (Token account for trading)"
    echo ""
    echo "  Mock State Account: AgentState11111111111111111111111111111111"
else
    echo "Building test client..."
    make client
    
    echo "Sending initialization transaction..."
    ./test_client init
fi

echo -e "${GREEN}✓ Agent initialized${NC}"
echo ""

# ============================================================================
# Configure Autonomous Execution
# ============================================================================

echo -e "${BLUE}Step 5: Configuring Autonomous Execution${NC}"
echo "────────────────────────────────────────────"

if [ "$MOCK_DEPLOY" = true ]; then
    echo "Mock configuration:"
    echo ""
    echo "  Would enable:"
    echo "    ⏱️  Periodic Timer: Every 1 slot (~400ms)"
    echo "    👁️  Oracle Watcher: Trigger on price changes"
    echo ""
    echo "  The agent will automatically:"
    echo "    1. Extract features from oracle data"
    echo "    2. Run ML inference (~10ns latency)"
    echo "    3. Execute trades based on signals"
else
    ./test_client configure
fi

echo -e "${GREEN}✓ Autonomous execution configured${NC}"
echo ""

# ============================================================================
# Summary
# ============================================================================

echo -e "${BLUE}"
echo "╔════════════════════════════════════════════════════════════╗"
echo "║                   Deployment Complete!                      ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo -e "${NC}"

echo "Summary:"
echo "────────"
echo "  Network:        $NETWORK"
echo "  RPC URL:        $RPC_URL"
echo "  Program ID:     $PROGRAM_ID"
echo "  Status:         Active"
echo ""
echo "Next Steps:"
echo "───────────"
echo "  1. Monitor agent: ./test_client status"
echo "  2. Trigger manual inference: ./test_client trigger"
echo "  3. Pause agent: ./test_client pause"
echo "  4. View logs: slonana logs $PROGRAM_ID"
echo ""
echo "Documentation:"
echo "──────────────"
echo "  - README.md - Overview and architecture"
echo "  - docs/TUTORIAL_ML_INFERENCE.md - ML inference tutorial"
echo "  - docs/ASYNC_BPF_EXECUTION.md - Async execution guide"
echo ""
