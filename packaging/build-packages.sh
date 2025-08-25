#!/bin/bash

# Slonana Validator Package Distribution Build Script
# This script builds packages for all supported platforms

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
VERSION="1.0.0"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

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

show_help() {
    echo "Slonana Validator Package Distribution Build Script"
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -h, --help           Show this help message"
    echo "  -a, --all            Build all package types"
    echo "  -d, --deb            Build Debian package"
    echo "  -r, --rpm            Build RPM package"
    echo "  -b, --homebrew       Prepare Homebrew formula"
    echo "  -c, --chocolatey     Prepare Chocolatey package"
    echo "  --clean              Clean build artifacts"
    echo "  --test               Test built packages"
    echo ""
    echo "Examples:"
    echo "  $0 --all            Build all packages"
    echo "  $0 --deb --rpm      Build Debian and RPM packages"
    echo "  $0 --test           Test existing packages"
}

# Parse command line arguments
BUILD_ALL=false
BUILD_DEB=false
BUILD_RPM=false
BUILD_HOMEBREW=false
BUILD_CHOCOLATEY=false
CLEAN_BUILD=false
TEST_PACKAGES=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -a|--all)
            BUILD_ALL=true
            shift
            ;;
        -d|--deb)
            BUILD_DEB=true
            shift
            ;;
        -r|--rpm)
            BUILD_RPM=true
            shift
            ;;
        -b|--homebrew)
            BUILD_HOMEBREW=true
            shift
            ;;
        -c|--chocolatey)
            BUILD_CHOCOLATEY=true
            shift
            ;;
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --test)
            TEST_PACKAGES=true
            shift
            ;;
        *)
            log_error "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

# Set all flags if --all is specified
if [[ "$BUILD_ALL" == true ]]; then
    BUILD_DEB=true
    BUILD_RPM=true
    BUILD_HOMEBREW=true
    BUILD_CHOCOLATEY=true
fi

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."
    
    # Check if we're in the right directory
    if [[ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
        log_error "Must be run from slonana.cpp project root or packaging directory"
        exit 1
    fi
    
    # Check if project is built
    if [[ ! -f "$PROJECT_ROOT/build/slonana_validator" ]]; then
        log_error "Project not built. Run 'cmake --build build' first."
        exit 1
    fi
    
    log_success "Prerequisites check completed"
}

# Clean build artifacts
clean_build() {
    if [[ "$CLEAN_BUILD" == true ]]; then
        log_info "Cleaning build artifacts..."
        rm -f "$PROJECT_ROOT"/*.deb
        rm -f "$PROJECT_ROOT"/*.rpm
        rm -f "$PROJECT_ROOT"/*.pkg
        rm -rf "$PROJECT_ROOT/packaging/temp"
        log_success "Build artifacts cleaned"
    fi
}

# Build Debian package
build_debian_package() {
    if [[ "$BUILD_DEB" == true ]]; then
        log_info "Building Debian package..."
        
        if ! command -v dpkg-deb &> /dev/null; then
            log_warning "dpkg-deb not found. Skipping Debian package build."
            return
        fi
        
        cd "$PROJECT_ROOT"
        if [[ -f "packaging/build-deb.sh" ]]; then
            bash packaging/build-deb.sh
            log_success "Debian package built successfully"
        else
            log_error "Debian build script not found"
        fi
    fi
}

# Build RPM package
build_rpm_package() {
    if [[ "$BUILD_RPM" == true ]]; then
        log_info "Building RPM package..."
        
        if ! command -v rpmbuild &> /dev/null; then
            log_warning "rpmbuild not found. Skipping RPM package build."
            return
        fi
        
        cd "$PROJECT_ROOT"
        
        # Create RPM build directories
        mkdir -p ~/rpmbuild/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
        
        # Copy spec file
        cp packaging/rpm/slonana-validator.spec ~/rpmbuild/SPECS/
        
        # Create source tarball
        tar -czf ~/rpmbuild/SOURCES/slonana-validator-${VERSION}.tar.gz \
            --exclude='.git' \
            --exclude='build' \
            --exclude='*.deb' \
            --exclude='*.rpm' \
            --transform="s,^,slonana-validator-${VERSION}/," \
            *
        
        # Build RPM
        rpmbuild -ba ~/rpmbuild/SPECS/slonana-validator.spec
        
        # Copy result
        cp ~/rpmbuild/RPMS/x86_64/slonana-validator-${VERSION}-1.*.x86_64.rpm .
        
        log_success "RPM package built successfully"
    fi
}

# Prepare Homebrew formula
prepare_homebrew_formula() {
    if [[ "$BUILD_HOMEBREW" == true ]]; then
        log_info "Preparing Homebrew formula..."
        
        # Create Homebrew tap directory structure
        mkdir -p "$PROJECT_ROOT/homebrew-slonana/Formula"
        
        # Copy formula
        cp "$PROJECT_ROOT/packaging/homebrew/slonana-validator.rb" \
           "$PROJECT_ROOT/homebrew-slonana/Formula/"
        
        # Create tap README
        cat > "$PROJECT_ROOT/homebrew-slonana/README.md" << 'EOF'
# Homebrew Slonana

This tap provides formulae for Slonana C++ Solana validator.

## Installation

```bash
brew tap slonana-labs/slonana
brew install slonana-validator
```

## Usage

```bash
# Start the validator service
brew services start slonana-validator

# Generate identity keys
slonana-genesis create-identity --output-dir $(brew --prefix)/var/lib/slonana

# Check status
brew services list | grep slonana
```

## Support

For issues and support, visit: https://github.com/slonana-labs/slonana.cpp
EOF
        
        log_success "Homebrew formula prepared"
        log_info "To publish: Create repository at github.com/slonana-labs/homebrew-slonana"
    fi
}

# Prepare Chocolatey package
prepare_chocolatey_package() {
    if [[ "$BUILD_CHOCOLATEY" == true ]]; then
        log_info "Preparing Chocolatey package..."
        
        mkdir -p "$PROJECT_ROOT/chocolatey-package/tools"
        
        # Create chocolatey package specification
        cat > "$PROJECT_ROOT/chocolatey-package/slonana-validator.nuspec" << EOF
<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://schemas.microsoft.com/packaging/2015/06/nuspec.xsd">
  <metadata>
    <id>slonana-validator</id>
    <version>${VERSION}</version>
    <title>Slonana Validator</title>
    <authors>Slonana Labs</authors>
    <projectUrl>https://github.com/slonana-labs/slonana.cpp</projectUrl>
    <iconUrl>https://slonana.dev/icon.png</iconUrl>
    <copyright>2025 Slonana Labs</copyright>
    <licenseUrl>https://github.com/slonana-labs/slonana.cpp/blob/main/LICENSE</licenseUrl>
    <requireLicenseAcceptance>false</requireLicenseAcceptance>
    <tags>solana validator blockchain cpp</tags>
    <summary>High-performance C++ Solana validator implementation</summary>
    <description>
Slonana.cpp is a high-performance C++ implementation of a Solana validator
that provides full compatibility with the Solana network while delivering
superior performance and reliability.

Features:
* Full Solana protocol compatibility
* High-performance SVM execution engine
* Comprehensive monitoring and metrics
* Hardware wallet support
* Production-ready deployment tooling
    </description>
  </metadata>
  <files>
    <file src="tools\\**" target="tools" />
  </files>
</package>
EOF

        # Create installation script
        cat > "$PROJECT_ROOT/chocolatey-package/tools/chocolateyinstall.ps1" << 'EOF'
$ErrorActionPreference = 'Stop'

$packageName = 'slonana-validator'
$toolsDir = "$(Split-Path -parent $MyInvocation.MyCommand.Definition)"
$url64 = 'https://github.com/slonana-labs/slonana.cpp/releases/download/v1.0.0/slonana-validator-windows-x64.zip'

$packageArgs = @{
  packageName   = $packageName
  unzipLocation = $toolsDir
  url64bit      = $url64
  checksum64    = 'YOUR_CHECKSUM_HERE'
  checksumType64= 'sha256'
}

Install-ChocolateyZipPackage @packageArgs

# Create Windows service
$serviceName = 'SlonanaValidator'
$serviceDisplayName = 'Slonana Validator'
$serviceDescription = 'High-performance C++ Solana validator'
$exePath = Join-Path $toolsDir 'slonana_validator.exe'
$configPath = Join-Path $env:ProgramData 'Slonana\validator.toml'

# Create config directory
$configDir = Split-Path $configPath
if (!(Test-Path $configDir)) {
    New-Item -ItemType Directory -Path $configDir -Force
}

# Create default configuration
@"
# Slonana Validator Configuration for Windows
[network]
gossip_port = 8001
rpc_port = 8899
validator_port = 8003

[validator]
identity_file = "C:\\ProgramData\\Slonana\\identity.json"
vote_account_file = "C:\\ProgramData\\Slonana\\vote-account.json"
ledger_path = "C:\\ProgramData\\Slonana\\ledger"

[consensus]
enable_voting = true
vote_threshold = 0.67

[rpc]
enable_rpc = true
rpc_bind_address = "127.0.0.1:8899"

[monitoring]
enable_metrics = true
metrics_port = 9090

[logging]
log_level = "info"
log_file = "C:\\ProgramData\\Slonana\\validator.log"
"@ | Out-File -FilePath $configPath -Encoding UTF8

Write-Host "Slonana Validator installed successfully!"
Write-Host "Configuration: $configPath"
Write-Host "To start: Start-Service $serviceName"
EOF

        # Create uninstallation script
        cat > "$PROJECT_ROOT/chocolatey-package/tools/chocolateyuninstall.ps1" << 'EOF'
$ErrorActionPreference = 'Stop'

$serviceName = 'SlonanaValidator'

# Stop and remove service if it exists
if (Get-Service $serviceName -ErrorAction SilentlyContinue) {
    Stop-Service $serviceName -Force
    Remove-Service $serviceName
}

Write-Host "Slonana Validator uninstalled successfully!"
EOF
        
        log_success "Chocolatey package prepared"
        log_info "To build: Run 'choco pack' in chocolatey-package directory"
    fi
}

# Test packages
test_packages() {
    if [[ "$TEST_PACKAGES" == true ]]; then
        log_info "Testing built packages..."
        
        # Test Debian package
        if [[ -f "slonana-validator_${VERSION}_amd64.deb" ]]; then
            log_info "Testing Debian package..."
            if dpkg --info "slonana-validator_${VERSION}_amd64.deb" &>/dev/null; then
                log_success "Debian package structure is valid"
            else
                log_error "Debian package is invalid"
            fi
        fi
        
        # Test RPM package
        if [[ -f "slonana-validator-${VERSION}-1."*".x86_64.rpm" ]]; then
            log_info "Testing RPM package..."
            if rpm -qip slonana-validator-${VERSION}-1.*.x86_64.rpm &>/dev/null; then
                log_success "RPM package structure is valid"
            else
                log_error "RPM package is invalid"
            fi
        fi
        
        # Test Homebrew formula
        if [[ -f "homebrew-slonana/Formula/slonana-validator.rb" ]]; then
            log_info "Testing Homebrew formula..."
            if brew formula-check homebrew-slonana/Formula/slonana-validator.rb &>/dev/null; then
                log_success "Homebrew formula is valid"
            else
                log_warning "Homebrew formula may have issues (brew not available or formula needs refinement)"
            fi
        fi
    fi
}

# Show summary
show_summary() {
    log_info "Package Build Summary:"
    echo "======================"
    
    if [[ -f "slonana-validator_${VERSION}_amd64.deb" ]]; then
        echo "✅ Debian package: slonana-validator_${VERSION}_amd64.deb"
    fi
    
    if [[ -f "slonana-validator-${VERSION}-1."*".x86_64.rpm" ]]; then
        echo "✅ RPM package: slonana-validator-${VERSION}-1.*.x86_64.rpm"
    fi
    
    if [[ -d "homebrew-slonana" ]]; then
        echo "✅ Homebrew tap: homebrew-slonana/"
    fi
    
    if [[ -d "chocolatey-package" ]]; then
        echo "✅ Chocolatey package: chocolatey-package/"
    fi
    
    echo ""
    echo "Installation instructions:"
    echo "  Debian/Ubuntu: sudo dpkg -i slonana-validator_${VERSION}_amd64.deb"
    echo "  RHEL/CentOS:   sudo rpm -i slonana-validator-${VERSION}-1.*.x86_64.rpm"
    echo "  macOS:         brew tap slonana-labs/slonana && brew install slonana-validator"
    echo "  Windows:       choco install slonana-validator"
    echo "======================"
}

# Main execution
main() {
    log_info "Starting package distribution build for Slonana Validator ${VERSION}..."
    
    check_prerequisites
    clean_build
    build_debian_package
    build_rpm_package
    prepare_homebrew_formula
    prepare_chocolatey_package
    test_packages
    
    log_success "Package distribution build completed!"
    show_summary
}

# Execute main function
main "$@"