#!/bin/bash
# Slonana C++ Validator - Dependency Update Helper
# This script helps with safely updating dependencies

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

usage() {
    cat << EOF
Usage: $0 [command]

Commands:
    check       Check current dependency versions and security status
    update      Check for available updates (requires manual review)
    audit       Run comprehensive security audit
    report      Generate dependency report
    help        Show this help message

Examples:
    $0 check           # Check current versions
    $0 audit           # Run security audit
    $0 report          # Generate dependency report
EOF
}

check_dependencies() {
    log "Checking current dependency versions..."
    echo
    
    # Check OpenSSL
    if command -v openssl >/dev/null 2>&1; then
        OPENSSL_VERSION=$(openssl version | cut -d' ' -f2)
        echo "OpenSSL: $OPENSSL_VERSION"
        
        # Basic security check
        case "$OPENSSL_VERSION" in
            3.0.1[0-9]|3.0.1[3-9]|3.1.*|3.2.*)
                success "✅ OpenSSL version is current and secure"
                ;;
            3.0.[0-9])
                warn "⚠️  OpenSSL version may have security updates available"
                ;;
            *)
                error "❌ OpenSSL version may be vulnerable"
                ;;
        esac
    else
        error "OpenSSL not found"
    fi
    echo
    
    # Check CMake
    if command -v cmake >/dev/null 2>&1; then
        CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
        echo "CMake: $CMAKE_VERSION"
        
        CMAKE_MAJOR=$(echo $CMAKE_VERSION | cut -d. -f1)
        CMAKE_MINOR=$(echo $CMAKE_VERSION | cut -d. -f2)
        if [ "$CMAKE_MAJOR" -gt 3 ] || ([ "$CMAKE_MAJOR" -eq 3 ] && [ "$CMAKE_MINOR" -ge 16 ]); then
            success "✅ CMake version meets requirements"
        else
            error "❌ CMake version too old (minimum: 3.16)"
        fi
    else
        error "CMake not found"
    fi
    echo
    
    # Check GCC
    if command -v gcc >/dev/null 2>&1; then
        GCC_VERSION=$(gcc --version | head -n1 | cut -d' ' -f4)
        echo "GCC: $GCC_VERSION"
        
        GCC_MAJOR=$(echo $GCC_VERSION | cut -d. -f1)
        if [ "$GCC_MAJOR" -ge 13 ]; then
            success "✅ GCC version meets requirements"
        else
            warn "⚠️  GCC version may be too old (recommended: 13+)"
        fi
    else
        warn "GCC not found"
    fi
    echo
    
    # Check Clang (alternative)
    if command -v clang >/dev/null 2>&1; then
        CLANG_VERSION=$(clang --version | head -n1 | grep -o '[0-9]\+\.[0-9]\+' | head -n1)
        echo "Clang: $CLANG_VERSION"
        
        if [ -n "$CLANG_VERSION" ]; then
            CLANG_MAJOR=$(echo $CLANG_VERSION | cut -d. -f1)
            if [ "$CLANG_MAJOR" -ge 15 ]; then
                success "✅ Clang version meets requirements"
            else
                warn "⚠️  Clang version may be too old (recommended: 15+)"
            fi
        else
            warn "⚠️  Could not determine Clang version"
        fi
    fi
    echo
    
    # Check Make
    if command -v make >/dev/null 2>&1; then
        MAKE_VERSION=$(make --version | head -n1 | cut -d' ' -f3)
        echo "GNU Make: $MAKE_VERSION"
        success "✅ Make found"
    else
        error "Make not found"
    fi
}

check_for_updates() {
    log "Checking for available dependency updates..."
    echo
    
    warn "Manual checks required:"
    echo "1. OpenSSL: Check https://www.openssl.org/news/ for latest releases"
    echo "2. CMake: Check https://cmake.org/download/ for latest version"
    echo "3. GCC: Check your system package manager"
    echo "4. System packages: Run 'apt list --upgradable' or equivalent"
    echo
    
    log "Package manager update commands:"
    echo "Ubuntu/Debian:"
    echo "  sudo apt update && sudo apt upgrade"
    echo "  sudo apt install cmake build-essential libssl-dev"
    echo
    echo "CentOS/RHEL/Fedora:"
    echo "  sudo dnf update"
    echo "  sudo dnf install cmake gcc-c++ openssl-devel"
    echo
    echo "macOS:"
    echo "  brew update && brew upgrade"
    echo "  brew install cmake openssl@3"
}

run_security_audit() {
    log "Running comprehensive security audit..."
    echo
    
    # Check if we're in the project directory
    if [ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]; then
        error "Not in a slonana.cpp project directory"
        exit 1
    fi
    
    # Run dependency checks
    check_dependencies
    echo
    
    # Check for secrets in source code
    log "Scanning for potential secrets in source code..."
    if command -v grep >/dev/null 2>&1; then
        SECRET_PATTERNS="password|secret|private_key|api_key|token"
        if grep -r -i -E "$SECRET_PATTERNS" --include="*.cpp" --include="*.h" --include="*.hpp" src/ include/ 2>/dev/null | grep -v "SecretKey\|PublicKey\|test\|example"; then
            warn "⚠️  Potential secrets found in source code (review manually)"
        else
            success "✅ No obvious secrets detected in source code"
        fi
    fi
    echo
    
    # Check for vulnerable file permissions
    log "Checking file permissions..."
    if find . -name "*.pem" -o -name "*.key" -o -name "*_key" | grep -v test | head -5; then
        warn "⚠️  Private key files found - ensure they are properly secured"
    else
        success "✅ No obvious private key files found"
    fi
    echo
    
    # Check git configuration
    log "Checking Git security configuration..."
    if git config --get-regexp "url\." | grep -q "https://"; then
        success "✅ Using HTTPS for Git remotes"
    else
        warn "⚠️  Consider using HTTPS for Git remotes"
    fi
}

generate_report() {
    log "Generating dependency report..."
    
    REPORT_FILE="dependency_report_$(date +%Y%m%d_%H%M%S).txt"
    
    cat > "$REPORT_FILE" << EOF
# Slonana C++ Validator - Dependency Report
Generated: $(date)

## System Information
OS: $(uname -a)
Architecture: $(uname -m)

## Dependency Versions
EOF
    
    # Add OpenSSL info
    if command -v openssl >/dev/null 2>&1; then
        echo "OpenSSL: $(openssl version)" >> "$REPORT_FILE"
    fi
    
    # Add CMake info
    if command -v cmake >/dev/null 2>&1; then
        echo "CMake: $(cmake --version | head -n1)" >> "$REPORT_FILE"
    fi
    
    # Add GCC info
    if command -v gcc >/dev/null 2>&1; then
        echo "GCC: $(gcc --version | head -n1)" >> "$REPORT_FILE"
    fi
    
    # Add Make info
    if command -v make >/dev/null 2>&1; then
        echo "Make: $(make --version | head -n1)" >> "$REPORT_FILE"
    fi
    
    # Add package info (if on Debian/Ubuntu)
    if command -v dpkg >/dev/null 2>&1; then
        echo "" >> "$REPORT_FILE"
        echo "## Installed Security-Critical Packages" >> "$REPORT_FILE"
        dpkg -l | grep -E "(openssl|libssl|cmake|gcc|make)" >> "$REPORT_FILE"
    fi
    
    success "Report generated: $REPORT_FILE"
}

# Main script logic
case "${1:-help}" in
    check)
        check_dependencies
        ;;
    update)
        check_for_updates
        ;;
    audit)
        run_security_audit
        ;;
    report)
        generate_report
        ;;
    help|--help|-h)
        usage
        ;;
    *)
        error "Unknown command: $1"
        echo
        usage
        exit 1
        ;;
esac