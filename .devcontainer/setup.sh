#!/bin/bash
# Post-create setup script for Slonana C++ development environment
# This script is automatically executed after the dev container is created

set -e

echo "🚀 Setting up Slonana C++ development environment..."

# Install system dependencies
echo "📦 Installing system dependencies..."
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    curl \
    pkg-config \
    libssl-dev \
    libudev-dev \
    llvm-dev \
    libclang-dev \
    clang \
    bc \
    jq \
    netcat-openbsd

# Install Solana CLI as requested
echo "🔧 Installing Solana CLI..."
curl --proto '=https' --tlsv1.2 -sSfL https://solana-install.solana.workers.dev | bash

# Add Solana CLI to PATH for both bash and zsh
echo "⚙️ Configuring PATH for Solana CLI..."
echo 'export PATH="$HOME/.local/share/solana/install/active_release/bin:$PATH"' >> ~/.bashrc
echo 'export PATH="$HOME/.local/share/solana/install/active_release/bin:$PATH"' >> ~/.zshrc

# Source the updated PATH
export PATH="$HOME/.local/share/solana/install/active_release/bin:$PATH"

# Verify Solana CLI installation
echo "✅ Verifying Solana CLI installation..."
if command -v solana &> /dev/null; then
    echo "   • Solana CLI version: $(solana --version)"
    echo "   • Solana keygen available: $(command -v solana-keygen && echo "✅" || echo "❌")"
else
    echo "   • ⚠️ Solana CLI not found in PATH, may need to restart terminal"
fi

# Set up Git configuration for development
echo "🔧 Configuring Git..."
git config --global init.defaultBranch main
git config --global pull.rebase false

# Create workspace directories
echo "📁 Creating workspace directories..."
mkdir -p ~/workspace/slonana-dev
mkdir -p ~/workspace/benchmark-results

# Set up bash aliases for common Slonana tasks
echo "⚙️ Setting up development aliases..."
cat >> ~/.bashrc << 'EOF'

# Slonana Development Aliases
alias slonana-build='cd ~/workspace && make -j$(nproc)'
alias slonana-test='cd ~/workspace && make test'
alias slonana-benchmark='cd ~/workspace && ./scripts/benchmark_slonana.sh --verbose'
alias slonana-clean='cd ~/workspace && make clean'

EOF

# Copy aliases to zsh as well
cat >> ~/.zshrc << 'EOF'

# Slonana Development Aliases
alias slonana-build='cd ~/workspace && make -j$(nproc)'
alias slonana-test='cd ~/workspace && make test'
alias slonana-benchmark='cd ~/workspace && ./scripts/benchmark_slonana.sh --verbose'
alias slonana-clean='cd ~/workspace && make clean'

EOF

echo "✅ Slonana C++ development environment setup complete!"
echo ""
echo "🎯 Ready for development with:"
echo "   • Solana CLI pre-installed and configured"
echo "   • C++ build tools and CMake ready"
echo "   • Development aliases available"
echo "   • Ports forwarded for validator operation (8899, 8001, 9900)"
echo ""
echo "💡 Restart your terminal or run 'source ~/.bashrc' to use Solana CLI commands"
echo "🚀 Happy coding!"