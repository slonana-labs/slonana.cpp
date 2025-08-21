#!/bin/bash
set -euo pipefail

# Slonana.cpp Validator Entrypoint Script
# Handles initialization, configuration, and startup

SLONANA_HOME=${SLONANA_HOME:-/opt/slonana}
SLONANA_LEDGER_PATH=${SLONANA_LEDGER_PATH:-$SLONANA_HOME/data/ledger}
SLONANA_CONFIG_PATH=${SLONANA_CONFIG_PATH:-$SLONANA_HOME/config/validator.toml}
SLONANA_LOG_LEVEL=${SLONANA_LOG_LEVEL:-info}
SLONANA_RPC_BIND_ADDRESS=${SLONANA_RPC_BIND_ADDRESS:-0.0.0.0:8899}
SLONANA_GOSSIP_BIND_ADDRESS=${SLONANA_GOSSIP_BIND_ADDRESS:-0.0.0.0:8001}
SLONANA_IDENTITY_KEYPAIR=${SLONANA_IDENTITY_KEYPAIR:-$SLONANA_HOME/config/validator-keypair.json}

# Logging function
log() {
    echo "[$(date -Iseconds)] $*" >&2
}

# Error handling
error_exit() {
    log "ERROR: $1"
    exit 1
}

# Initialize data directories
init_directories() {
    log "Initializing data directories..."
    
    mkdir -p "$(dirname "$SLONANA_LEDGER_PATH")" || error_exit "Failed to create ledger directory"
    mkdir -p "$SLONANA_HOME/logs" || error_exit "Failed to create logs directory"
    
    # Set proper permissions
    chmod 750 "$SLONANA_HOME/data" "$SLONANA_HOME/logs" 2>/dev/null || true
}

# Generate validator identity if it doesn't exist
generate_identity() {
    if [[ ! -f "$SLONANA_IDENTITY_KEYPAIR" ]]; then
        log "Generating validator identity keypair..."
        
        # Create a simple keypair file (in production, use proper key generation)
        mkdir -p "$(dirname "$SLONANA_IDENTITY_KEYPAIR")"
        cat > "$SLONANA_IDENTITY_KEYPAIR" << 'EOF'
{
    "privateKey": "5KjvvwAJ3C7GNJbLNtMbG1FzTXLJEHMZHFHHKOPCSFPjHMNFJ2wQsYV2pKHCEX1Z5tMPCj5wJJ8fFTCxoVFpQpDKt",
    "publicKey": "4fYNw3dojWmQ4dXtSGE9epjRGy9fJsqZDAdqNTgDEDVX"
}
EOF
        chmod 600 "$SLONANA_IDENTITY_KEYPAIR"
        log "Generated validator identity: $(dirname "$SLONANA_IDENTITY_KEYPAIR")"
    else
        log "Using existing validator identity: $SLONANA_IDENTITY_KEYPAIR"
    fi
}

# Wait for dependencies (if any)
wait_for_dependencies() {
    # Check if bootstrap peers are specified and wait for them
    if [[ -n "${SLONANA_BOOTSTRAP_PEERS:-}" ]]; then
        log "Waiting for bootstrap peers: $SLONANA_BOOTSTRAP_PEERS"
        
        IFS=',' read -ra PEERS <<< "$SLONANA_BOOTSTRAP_PEERS"
        for peer in "${PEERS[@]}"; do
            IFS=':' read -ra PEER_PARTS <<< "$peer"
            host="${PEER_PARTS[0]}"
            port="${PEER_PARTS[1]:-8001}"
            
            log "Waiting for peer $host:$port..."
            timeout=60
            while ! nc -z "$host" "$port" 2>/dev/null && [[ $timeout -gt 0 ]]; do
                sleep 1
                ((timeout--))
            done
            
            if [[ $timeout -le 0 ]]; then
                log "WARNING: Timeout waiting for peer $host:$port"
            else
                log "Peer $host:$port is ready"
            fi
        done
    fi
}

# Health check endpoint
health_check() {
    cat << 'EOF' > "$SLONANA_HOME/health_check.sh"
#!/bin/bash
# Simple health check - verify the validator is responding
curl -s -f "http://localhost:8899" -H "Content-Type: application/json" -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "getHealth"
}' | grep -q '"result"'
EOF
    chmod +x "$SLONANA_HOME/health_check.sh"
}

# Start the validator
start_validator() {
    log "Starting Slonana validator..."
    log "  Ledger path: $SLONANA_LEDGER_PATH"
    log "  Identity: $SLONANA_IDENTITY_KEYPAIR"
    log "  RPC bind: $SLONANA_RPC_BIND_ADDRESS" 
    log "  Gossip bind: $SLONANA_GOSSIP_BIND_ADDRESS"
    log "  Log level: $SLONANA_LOG_LEVEL"
    
    # Build command line arguments
    ARGS=(
        --ledger-path "$SLONANA_LEDGER_PATH"
        --identity "$SLONANA_IDENTITY_KEYPAIR"
        --rpc-bind-address "$SLONANA_RPC_BIND_ADDRESS"
        --gossip-bind-address "$SLONANA_GOSSIP_BIND_ADDRESS"
    )
    
    # Add cluster configuration if specified
    if [[ -n "${SLONANA_CLUSTER_CONFIG:-}" ]]; then
        ARGS+=(--config "$SLONANA_CLUSTER_CONFIG")
    fi
    
    # Add bootstrap peers if specified
    if [[ -n "${SLONANA_BOOTSTRAP_PEERS:-}" ]]; then
        IFS=',' read -ra PEERS <<< "$SLONANA_BOOTSTRAP_PEERS"
        for peer in "${PEERS[@]}"; do
            ARGS+=(--known-validator "$peer")
        done
    fi
    
    # Execute the validator
    exec "$SLONANA_HOME/bin/slonana_validator" "${ARGS[@]}"
}

# Handle special commands
case "${1:-validator}" in
    validator)
        init_directories
        generate_identity
        health_check
        wait_for_dependencies
        start_validator
        ;;
    
    init)
        log "Initializing validator..."
        init_directories
        generate_identity
        log "Initialization complete"
        ;;
    
    health)
        # Run health check
        if [[ -f "$SLONANA_HOME/health_check.sh" ]]; then
            exec "$SLONANA_HOME/health_check.sh"
        else
            error_exit "Health check script not found"
        fi
        ;;
    
    version)
        exec "$SLONANA_HOME/bin/slonana_validator" --version
        ;;
        
    help|--help|-h)
        cat << 'EOF'
Slonana.cpp Validator Docker Entrypoint

Usage: entrypoint.sh [COMMAND]

Commands:
  validator    Start the validator (default)
  init         Initialize data directories and generate keys
  health       Run health check
  version      Show validator version
  help         Show this help message

Environment Variables:
  SLONANA_HOME                 Base directory (/opt/slonana)
  SLONANA_LEDGER_PATH         Ledger data path 
  SLONANA_CONFIG_PATH         Configuration file path
  SLONANA_LOG_LEVEL           Logging level (debug|info|warn|error)
  SLONANA_RPC_BIND_ADDRESS    RPC server bind address
  SLONANA_GOSSIP_BIND_ADDRESS Gossip protocol bind address
  SLONANA_IDENTITY_KEYPAIR    Validator identity keypair file
  SLONANA_BOOTSTRAP_PEERS     Comma-separated list of bootstrap peers
  SLONANA_CLUSTER_CONFIG      Cluster configuration file

Examples:
  # Start validator with custom settings
  docker run -e SLONANA_LOG_LEVEL=debug slonana/validator

  # Initialize only
  docker run slonana/validator init

  # Run health check
  docker run slonana/validator health
EOF
        ;;
    
    *)
        # Pass through any other command to the validator binary
        exec "$SLONANA_HOME/bin/slonana_validator" "$@"
        ;;
esac