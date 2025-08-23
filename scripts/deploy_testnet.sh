#!/bin/bash

# Slonana C++ Validator Testnet Deployment Script
# This script automates the deployment of a slonana.cpp validator to testnet

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
CONFIG_DIR="$PROJECT_ROOT/config/testnet"
KEYS_DIR="$PROJECT_ROOT/keys"
LOGS_DIR="$PROJECT_ROOT/logs"

# Default configuration
TESTNET_RPC_URL="https://api.testnet.solana.com"
TESTNET_WS_URL="wss://api.testnet.solana.com"
GOSSIP_PORT=8001
RPC_PORT=8899
VALIDATOR_PORT=8003
METRICS_PORT=9090

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Help function
show_help() {
    echo "Slonana C++ Validator Testnet Deployment Script"
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -h, --help           Show this help message"
    echo "  -b, --build          Build the validator before deployment"
    echo "  -c, --clean          Clean build before deployment"
    echo "  -k, --generate-keys  Generate new validator identity keys"
    echo "  -s, --start          Start the validator after deployment"
    echo "  -t, --test           Run tests before deployment"
    echo "  --rpc-port PORT      Set custom RPC port (default: 8899)"
    echo "  --gossip-port PORT   Set custom gossip port (default: 8001)"
    echo "  --metrics-port PORT  Set custom metrics port (default: 9090)"
    echo ""
    echo "Examples:"
    echo "  $0 --build --generate-keys --start"
    echo "  $0 --clean --test --start"
    echo "  $0 --rpc-port 8900 --gossip-port 8002"
}

# Parse command line arguments
BUILD=false
CLEAN=false
GENERATE_KEYS=false
START_VALIDATOR=false
RUN_TESTS=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -b|--build)
            BUILD=true
            shift
            ;;
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -k|--generate-keys)
            GENERATE_KEYS=true
            shift
            ;;
        -s|--start)
            START_VALIDATOR=true
            shift
            ;;
        -t|--test)
            RUN_TESTS=true
            shift
            ;;
        --rpc-port)
            RPC_PORT="$2"
            shift 2
            ;;
        --gossip-port)
            GOSSIP_PORT="$2"
            shift 2
            ;;
        --metrics-port)
            METRICS_PORT="$2"
            shift 2
            ;;
        *)
            log_error "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."
    
    # Check if running on supported platform
    if [[ "$OSTYPE" != "linux-gnu"* ]] && [[ "$OSTYPE" != "darwin"* ]]; then
        log_warning "This script is designed for Linux and macOS. Other platforms may not work correctly."
    fi
    
    # Check required tools
    local required_tools=("cmake" "make" "g++" "openssl")
    for tool in "${required_tools[@]}"; do
        if ! command -v "$tool" &> /dev/null; then
            log_error "Required tool '$tool' is not installed"
            exit 1
        fi
    done
    
    # Check network connectivity
    if ! curl -s --connect-timeout 5 "$TESTNET_RPC_URL" > /dev/null; then
        log_warning "Cannot connect to Solana testnet. Network connectivity may be limited."
    fi
    
    log_success "Prerequisites check completed"
}

# Create necessary directories
create_directories() {
    log_info "Creating necessary directories..."
    
    mkdir -p "$CONFIG_DIR"
    mkdir -p "$KEYS_DIR"
    mkdir -p "$LOGS_DIR"
    mkdir -p "$BUILD_DIR"
    
    log_success "Directories created"
}

# Clean build directory
clean_build() {
    if [[ "$CLEAN" == true ]]; then
        log_info "Cleaning build directory..."
        rm -rf "$BUILD_DIR"
        mkdir -p "$BUILD_DIR"
        log_success "Build directory cleaned"
    fi
}

# Build the validator
build_validator() {
    if [[ "$BUILD" == true ]]; then
        log_info "Building slonana validator..."
        
        cd "$PROJECT_ROOT"
        
        # Configure build
        cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
        
        # Build
        make -C "$BUILD_DIR" -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
        
        if [[ ! -f "$BUILD_DIR/slonana_validator" ]]; then
            log_error "Build failed - validator executable not found"
            exit 1
        fi
        
        log_success "Validator built successfully"
    else
        # Check if validator exists
        if [[ ! -f "$BUILD_DIR/slonana_validator" ]]; then
            log_error "Validator executable not found. Run with --build to compile."
            exit 1
        fi
    fi
}

# Run tests
run_tests() {
    if [[ "$RUN_TESTS" == true ]]; then
        log_info "Running validator tests..."
        
        cd "$PROJECT_ROOT"
        
        # Run comprehensive tests
        if [[ -f "$BUILD_DIR/slonana_comprehensive_tests" ]]; then
            if ! "$BUILD_DIR/slonana_comprehensive_tests"; then
                log_error "Tests failed. Aborting deployment."
                exit 1
            fi
        fi
        
        # Run enhanced SVM tests
        if [[ -f "$BUILD_DIR/slonana_enhanced_svm_tests" ]]; then
            if ! "$BUILD_DIR/slonana_enhanced_svm_tests"; then
                log_error "Enhanced SVM tests failed. Aborting deployment."
                exit 1
            fi
        fi
        
        log_success "All tests passed"
    fi
}

# Generate validator identity keys
generate_keys() {
    if [[ "$GENERATE_KEYS" == true ]]; then
        log_info "Generating validator identity keys..."
        
        # Generate validator identity keypair
        IDENTITY_FILE="$KEYS_DIR/validator-identity.json"
        VOTE_ACCOUNT_FILE="$KEYS_DIR/vote-account.json"
        
        # Use OpenSSL to generate keys (simplified for demo)
        openssl rand -hex 32 > "$IDENTITY_FILE.seed"
        openssl rand -hex 32 > "$VOTE_ACCOUNT_FILE.seed"
        
        # Create JSON key files (simplified format)
        cat > "$IDENTITY_FILE" << EOF
{
  "seed": "$(cat "$IDENTITY_FILE.seed")",
  "public_key": "$(openssl rand -hex 32)",
  "created": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF
        
        cat > "$VOTE_ACCOUNT_FILE" << EOF
{
  "seed": "$(cat "$VOTE_ACCOUNT_FILE.seed")",
  "public_key": "$(openssl rand -hex 32)",
  "created": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF
        
        # Clean up seed files
        rm "$IDENTITY_FILE.seed" "$VOTE_ACCOUNT_FILE.seed"
        
        # Set appropriate permissions
        chmod 600 "$IDENTITY_FILE" "$VOTE_ACCOUNT_FILE"
        
        log_success "Validator keys generated"
        log_info "Identity file: $IDENTITY_FILE"
        log_info "Vote account file: $VOTE_ACCOUNT_FILE"
    fi
}

# Create validator configuration
create_config() {
    log_info "Creating validator configuration..."
    
    CONFIG_FILE="$CONFIG_DIR/validator.toml"
    
    cat > "$CONFIG_FILE" << EOF
# Slonana C++ Validator Configuration for Testnet

[network]
testnet_rpc_url = "$TESTNET_RPC_URL"
testnet_ws_url = "$TESTNET_WS_URL"
gossip_port = $GOSSIP_PORT
rpc_port = $RPC_PORT
validator_port = $VALIDATOR_PORT

[validator]
identity_file = "$KEYS_DIR/validator-identity.json"
vote_account_file = "$KEYS_DIR/vote-account.json"
ledger_path = "$PROJECT_ROOT/ledger"
log_level = "info"

[consensus]
enable_voting = true
vote_threshold = 0.67
max_skip_rate = 0.15

[rpc]
enable_rpc = true
rpc_bind_address = "0.0.0.0:$RPC_PORT"
enable_rpc_transaction_history = true
rpc_max_connections = 1000

[monitoring]
enable_metrics = true
metrics_port = $METRICS_PORT
prometheus_bind_address = "0.0.0.0:$METRICS_PORT"

[performance]
banking_threads = 4
transaction_threads = 8
ledger_verification_threads = 4
enable_parallel_execution = true

[security]
enable_tls = false
max_connections_per_ip = 100
connection_timeout_seconds = 30

EOF

    log_success "Configuration created: $CONFIG_FILE"
}

# Start the validator
start_validator() {
    if [[ "$START_VALIDATOR" == true ]]; then
        log_info "Starting slonana validator..."
        
        # Check if keys exist
        if [[ ! -f "$KEYS_DIR/validator-identity.json" ]]; then
            log_error "Validator identity keys not found. Run with --generate-keys first."
            exit 1
        fi
        
        # Create startup script
        STARTUP_SCRIPT="$PROJECT_ROOT/start_validator.sh"
        cat > "$STARTUP_SCRIPT" << EOF
#!/bin/bash
cd "$PROJECT_ROOT"

# Set environment variables
export SLONANA_CONFIG_FILE="$CONFIG_DIR/validator.toml"
export SLONANA_LOG_FILE="$LOGS_DIR/validator.log"

# Start validator with logging
exec "$BUILD_DIR/slonana_validator" \\
    --config "\$SLONANA_CONFIG_FILE" \\
    --identity "$KEYS_DIR/validator-identity.json" \\
    --vote-account "$KEYS_DIR/vote-account.json" \\
    --ledger "$PROJECT_ROOT/ledger" \\
    --rpc-port $RPC_PORT \\
    --gossip-port $GOSSIP_PORT \\
    --testnet \\
    --log-level info \\
    2>&1 | tee "\$SLONANA_LOG_FILE"
EOF
        
        chmod +x "$STARTUP_SCRIPT"
        
        log_success "Validator startup script created: $STARTUP_SCRIPT"
        log_info "Starting validator in background..."
        
        # Start validator in background
        nohup "$STARTUP_SCRIPT" > "$LOGS_DIR/startup.log" 2>&1 &
        VALIDATOR_PID=$!
        
        # Save PID for later management
        echo $VALIDATOR_PID > "$PROJECT_ROOT/validator.pid"
        
        # Wait a moment and check if process is still running
        sleep 3
        if kill -0 $VALIDATOR_PID 2>/dev/null; then
            log_success "Validator started successfully (PID: $VALIDATOR_PID)"
            log_info "RPC endpoint: http://localhost:$RPC_PORT"
            log_info "Metrics endpoint: http://localhost:$METRICS_PORT/metrics"
            log_info "Logs: $LOGS_DIR/validator.log"
            log_info "To stop: kill $VALIDATOR_PID"
        else
            log_error "Validator failed to start. Check logs in $LOGS_DIR/"
            exit 1
        fi
    fi
}

# Display deployment summary
show_summary() {
    log_info "Deployment Summary:"
    echo "===================="
    echo "Project Root: $PROJECT_ROOT"
    echo "Build Directory: $BUILD_DIR"
    echo "Configuration: $CONFIG_DIR/validator.toml"
    echo "Keys Directory: $KEYS_DIR"
    echo "Logs Directory: $LOGS_DIR"
    echo ""
    echo "Network Configuration:"
    echo "  RPC Port: $RPC_PORT"
    echo "  Gossip Port: $GOSSIP_PORT"
    echo "  Metrics Port: $METRICS_PORT"
    echo ""
    if [[ "$START_VALIDATOR" == true ]]; then
        echo "Validator Status: RUNNING"
        echo "  RPC Endpoint: http://localhost:$RPC_PORT"
        echo "  Metrics: http://localhost:$METRICS_PORT/metrics"
        if [[ -f "$PROJECT_ROOT/validator.pid" ]]; then
            echo "  PID: $(cat "$PROJECT_ROOT/validator.pid")"
        fi
    else
        echo "Validator Status: CONFIGURED (not started)"
        echo "  To start: $PROJECT_ROOT/start_validator.sh"
    fi
    echo "===================="
}

# Main execution
main() {
    log_info "Starting slonana validator testnet deployment..."
    
    check_prerequisites
    create_directories
    clean_build
    build_validator
    run_tests
    generate_keys
    create_config
    start_validator
    
    log_success "Deployment completed successfully!"
    show_summary
}

# Execute main function
main "$@"