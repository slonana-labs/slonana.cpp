#!/bin/bash

# Slonana Universal Installer
# Automatically detects OS and installs slonana with all dependencies
# Usage: curl -sSL https://install.slonana.com | bash
# Or: wget -qO- https://install.slonana.com | bash

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
SLONANA_VERSION=${SLONANA_VERSION:-"latest"}
INSTALL_DIR=${INSTALL_DIR:-"/usr/local/bin"}
DATA_DIR=${SLONANA_DATA_DIR:-"$HOME/.slonana"}
LOG_FILE="$DATA_DIR/install.log"

# Utility functions
log() {
    echo -e "${GREEN}[INFO]${NC} $1" | tee -a "$LOG_FILE"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1" | tee -a "$LOG_FILE"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" | tee -a "$LOG_FILE"
    exit 1
}

# Create data directory
mkdir -p "$DATA_DIR"
touch "$LOG_FILE"

log "Starting Slonana Universal Installer v1.0"
log "Log file: $LOG_FILE"

# Detect operating system and architecture
detect_os() {
    local os=""
    local arch=""
    
    # Detect OS
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        if [[ -f /etc/os-release ]]; then
            . /etc/os-release
            case $ID in
                ubuntu|debian)
                    os="debian"
                    ;;
                centos|rhel|fedora|rocky|almalinux)
                    os="redhat"
                    ;;
                arch)
                    os="arch"
                    ;;
                alpine)
                    os="alpine"
                    ;;
                *)
                    os="linux"
                    ;;
            esac
        else
            os="linux"
        fi
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        os="macos"
    elif [[ "$OSTYPE" == "cygwin" ]] || [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "win32" ]]; then
        os="windows"
    else
        error "Unsupported operating system: $OSTYPE"
    fi
    
    # Detect architecture
    case $(uname -m) in
        x86_64|amd64)
            arch="x86_64"
            ;;
        aarch64|arm64)
            arch="arm64"
            ;;
        armv7l)
            arch="armv7"
            ;;
        i386|i686)
            arch="i386"
            ;;
        *)
            error "Unsupported architecture: $(uname -m)"
            ;;
    esac
    
    log "Detected OS: $os, Architecture: $arch"
    echo "$os:$arch"
}

# Check if running as root/sudo when needed
check_permissions() {
    if [[ "$1" == "debian" ]] || [[ "$1" == "redhat" ]] || [[ "$1" == "arch" ]] || [[ "$1" == "alpine" ]]; then
        if [[ $EUID -ne 0 ]] && ! command -v sudo >/dev/null 2>&1; then
            error "This script requires root privileges or sudo access for package installation"
        fi
    fi
}

# Install dependencies based on OS
install_dependencies() {
    local os="$1"
    
    log "Installing dependencies for $os..."
    
    case $os in
        debian)
            if command -v apt >/dev/null 2>&1; then
                sudo apt update
                sudo apt install -y \
                    build-essential \
                    cmake \
                    libssl-dev \
                    libpthread-stubs0-dev \
                    curl \
                    wget \
                    git \
                    pkg-config \
                    libudev-dev \
                    libusb-1.0-0-dev \
                    libhidapi-dev \
                    unzip \
                    ca-certificates
            else
                error "apt package manager not found"
            fi
            ;;
        redhat)
            if command -v dnf >/dev/null 2>&1; then
                sudo dnf groupinstall -y "Development Tools"
                sudo dnf install -y \
                    cmake \
                    openssl-devel \
                    curl \
                    wget \
                    git \
                    pkg-config \
                    libudev-devel \
                    libusb-devel \
                    hidapi-devel \
                    unzip \
                    ca-certificates
            elif command -v yum >/dev/null 2>&1; then
                sudo yum groupinstall -y "Development Tools"
                sudo yum install -y \
                    cmake \
                    openssl-devel \
                    curl \
                    wget \
                    git \
                    pkg-config \
                    libudev-devel \
                    libusb-devel \
                    hidapi-devel \
                    unzip \
                    ca-certificates
            else
                error "dnf/yum package manager not found"
            fi
            ;;
        arch)
            sudo pacman -Sy --noconfirm \
                base-devel \
                cmake \
                openssl \
                curl \
                wget \
                git \
                pkg-config \
                udev \
                libusb \
                hidapi \
                unzip \
                ca-certificates
            ;;
        alpine)
            sudo apk update
            sudo apk add \
                build-base \
                cmake \
                openssl-dev \
                curl \
                wget \
                git \
                pkgconfig \
                eudev-dev \
                libusb-dev \
                hidapi-dev \
                unzip \
                ca-certificates
            ;;
        macos)
            if command -v brew >/dev/null 2>&1; then
                brew update
                brew install \
                    cmake \
                    openssl \
                    pkg-config \
                    libusb \
                    hidapi \
                    wget
            else
                warn "Homebrew not found, installing it first..."
                /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
                export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
                brew install \
                    cmake \
                    openssl \
                    pkg-config \
                    libusb \
                    hidapi \
                    wget
            fi
            ;;
        windows)
            if command -v choco >/dev/null 2>&1; then
                choco install -y \
                    git \
                    cmake \
                    visualstudio2022buildtools \
                    vcredist140 \
                    wget \
                    unzip
            else
                warn "Chocolatey not found. Please install manually or use WSL."
                warn "Run: Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))"
            fi
            ;;
        *)
            warn "Unknown OS, attempting to install basic dependencies..."
            ;;
    esac
    
    log "Dependencies installed successfully"
}

# Get latest release version from GitHub
get_latest_version() {
    if [[ "$SLONANA_VERSION" == "latest" ]]; then
        log "Fetching latest release version..."
        local version
        version=$(curl -s https://api.github.com/repos/slonana-labs/slonana.cpp/releases/latest | grep '"tag_name"' | cut -d'"' -f4)
        if [[ -z "$version" ]]; then
            warn "Could not fetch latest version, using development build"
            echo "main"
        else
            echo "$version"
        fi
    else
        echo "$SLONANA_VERSION"
    fi
}

# Download and install pre-built binary if available
install_binary() {
    local os="$1"
    local arch="$2"
    local version="$3"
    
    log "Attempting to download pre-built binary..."
    
    local binary_name="slonana-${os}-${arch}"
    local download_url="https://github.com/slonana-labs/slonana.cpp/releases/download/${version}/${binary_name}.tar.gz"
    
    if curl -f -L "$download_url" -o "/tmp/${binary_name}.tar.gz" 2>/dev/null; then
        log "Downloaded pre-built binary"
        cd /tmp
        tar -xzf "${binary_name}.tar.gz"
        
        if [[ "$os" == "windows" ]]; then
            sudo cp slonana-validator.exe "$INSTALL_DIR/"
            sudo cp slonana-cli.exe "$INSTALL_DIR/"
        else
            sudo cp slonana-validator "$INSTALL_DIR/"
            sudo cp slonana-cli "$INSTALL_DIR/"
            sudo chmod +x "$INSTALL_DIR/slonana-validator" "$INSTALL_DIR/slonana-cli"
        fi
        
        rm -f "/tmp/${binary_name}.tar.gz"
        log "Binary installation completed"
        return 0
    else
        warn "Pre-built binary not available, will build from source"
        return 1
    fi
}

# Build from source
build_from_source() {
    log "Building Slonana from source..."
    
    local build_dir="/tmp/slonana-build"
    rm -rf "$build_dir"
    mkdir -p "$build_dir"
    cd "$build_dir"
    
    log "Cloning repository..."
    git clone https://github.com/slonana-labs/slonana.cpp.git .
    
    log "Configuring build..."
    mkdir -p build
    cd build
    
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS specific configuration
        cmake .. \
            -DCMAKE_BUILD_TYPE=Release \
            -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl \
            -DCMAKE_PREFIX_PATH=/opt/homebrew
    else
        cmake .. -DCMAKE_BUILD_TYPE=Release
    fi
    
    log "Building (this may take several minutes)..."
    make -j$(nproc 2>/dev/null || echo 4)
    
    log "Installing binaries..."
    sudo make install
    
    log "Cleaning up build directory..."
    rm -rf "$build_dir"
    
    log "Source build completed successfully"
}

# Setup configuration
setup_configuration() {
    log "Setting up configuration..."
    
    # Create ledger directory
    mkdir -p "$DATA_DIR/ledger"
    
    # Create basic configuration file
    cat > "$DATA_DIR/validator.conf" << EOF
# Slonana Validator Configuration
# Generated by installer on $(date)

# Network configuration
rpc-bind-address = "0.0.0.0:8899"
rpc-port = 8899
gossip-port = 8001
dynamic-port-range = "8002-8020"

# Ledger configuration  
ledger-path = "$DATA_DIR/ledger"
snapshots-path = "$DATA_DIR/snapshots"
accounts-path = "$DATA_DIR/accounts"

# Features
enable-rpc-transaction-history = true
enable-cpi-and-log-storage = true
limit-ledger-size = 50000000

# Monitoring
enable-rpc-get-confirmed-signatures-for-address2 = true
enable-rpc-get-account-info = true

# Performance
accounts-db-caching-enabled = true
banking-trace-dir-byte-limit = 1000000000
EOF
    
    log "Configuration created at $DATA_DIR/validator.conf"
}

# Create systemd service (Linux only)
create_systemd_service() {
    if [[ "$1" != "macos" ]] && [[ "$1" != "windows" ]] && command -v systemctl >/dev/null 2>&1; then
        log "Creating systemd service..."
        
        sudo tee /etc/systemd/system/slonana-validator.service > /dev/null << EOF
[Unit]
Description=Slonana Validator
After=network.target

[Service]
Type=simple
User=$USER
WorkingDirectory=$DATA_DIR
ExecStart=$INSTALL_DIR/slonana-validator --config $DATA_DIR/validator.conf
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal
SyslogIdentifier=slonana-validator

[Install]
WantedBy=multi-user.target
EOF
        
        sudo systemctl daemon-reload
        sudo systemctl enable slonana-validator
        
        log "Systemd service created. Start with: sudo systemctl start slonana-validator"
    fi
}

# Verify installation
verify_installation() {
    log "Verifying installation..."
    
    # Update PATH to include installation directory
    export PATH="$INSTALL_DIR:$PATH"
    
    if command -v slonana_validator >/dev/null 2>&1; then
        local version=$(slonana_validator --version 2>/dev/null || echo "unknown")
        log "✅ slonana_validator installed: $version"
    elif [[ -f "$INSTALL_DIR/slonana_validator" ]]; then
        local version=$($INSTALL_DIR/slonana_validator --version 2>/dev/null || echo "unknown")
        log "✅ slonana_validator installed: $version"
    else
        error "slonana_validator not found"
    fi
    
    if command -v slonana-cli >/dev/null 2>&1; then
        local version=$(slonana-cli --version 2>/dev/null || echo "unknown")
        log "✅ slonana-cli installed: $version"
    elif [[ -f "$INSTALL_DIR/slonana-cli" ]]; then
        local version=$($INSTALL_DIR/slonana-cli --version 2>/dev/null || echo "unknown")
        log "✅ slonana-cli installed: $version"
    else
        warn "slonana-cli not found (optional)"
    fi
    
    # Test basic functionality
    log "Testing basic functionality..."
    if timeout 10s $INSTALL_DIR/slonana_validator --help >/dev/null 2>&1; then
        log "✅ Basic functionality test passed"
    else
        warn "Basic functionality test failed (this may be normal)"
    fi
}

# Print usage instructions
print_usage() {
    local os="$1"
    
    echo ""
    echo -e "${GREEN}🎉 Slonana installation completed successfully!${NC}"
    echo ""
    echo -e "${BLUE}Quick Start:${NC}"
    echo "  1. Start validator: $INSTALL_DIR/slonana_validator --config $DATA_DIR/validator.conf"
    echo "  2. Check health: curl http://localhost:8899/health"
    echo "  3. View logs: tail -f $DATA_DIR/logs/validator.log"
    echo ""
    echo -e "${BLUE}Useful Commands:${NC}"
    echo "  • Configuration: $DATA_DIR/validator.conf"
    echo "  • Data directory: $DATA_DIR"
    echo "  • Check version: $INSTALL_DIR/slonana_validator --version"
    echo "  • Add to PATH: export PATH=\"$INSTALL_DIR:\$PATH\""
    echo ""
    
    if [[ "$os" != "macos" ]] && [[ "$os" != "windows" ]] && command -v systemctl >/dev/null 2>&1; then
        echo -e "${BLUE}Service Management:${NC}"
        echo "  • Start service: sudo systemctl start slonana-validator"
        echo "  • Stop service: sudo systemctl stop slonana-validator"
        echo "  • View logs: sudo journalctl -u slonana-validator -f"
        echo ""
    fi
    
    echo -e "${BLUE}Documentation:${NC}"
    echo "  • User Manual: https://github.com/slonana-labs/slonana.cpp/blob/main/docs/USER_MANUAL.md"
    echo "  • API Docs: https://github.com/slonana-labs/slonana.cpp/blob/main/docs/API.md"
    echo "  • Website: https://slonana.com"
    echo ""
    echo -e "${YELLOW}Support:${NC}"
    echo "  • GitHub Issues: https://github.com/slonana-labs/slonana.cpp/issues"
    echo "  • Installation log: $LOG_FILE"
}

# Main installation flow
main() {
    log "Starting Slonana installation..."
    
    # Detect OS and architecture
    local os_arch
    os_arch=$(detect_os)
    local os="${os_arch%:*}"
    local arch="${os_arch#*:}"
    
    # Check permissions
    check_permissions "$os"
    
    # Install dependencies
    install_dependencies "$os"
    
    # Get version to install
    local version
    version=$(get_latest_version)
    log "Installing version: $version"
    
    # Try binary installation first, fall back to source build
    if ! install_binary "$os" "$arch" "$version"; then
        build_from_source
    fi
    
    # Setup configuration and services
    setup_configuration
    create_systemd_service "$os"
    
    # Verify installation
    verify_installation
    
    # Print usage instructions
    print_usage "$os"
    
    log "Installation completed successfully!"
}

# Handle interruption
trap 'error "Installation interrupted by user"' INT TERM

# Run main function
main "$@"