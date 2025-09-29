# Slonana C++ Development Container

This directory contains the development container configuration for the Slonana C++ project, optimized for GitHub Codespaces and VS Code Dev Containers.

## Features

### Pre-installed Tools
- **Solana CLI**: Fully installed and configured with PATH setup
- **C++ Build Tools**: GCC, Clang, CMake, Make
- **Development Tools**: Git, GitHub CLI, common utilities
- **VS Code Extensions**: C++, CMake, GitHub Copilot

### Port Forwarding
- **8899**: Slonana RPC endpoint
- **8001**: Slonana Gossip port  
- **9900**: Slonana Faucet port

### Development Aliases
- `slonana-build`: Build the project with parallel jobs
- `slonana-test`: Run the test suite
- `slonana-benchmark`: Execute benchmark with verbose output
- `slonana-clean`: Clean build artifacts

## Quick Start

### Using GitHub Codespaces
1. Click "Code" → "Codespaces" → "Create codespace"
2. Wait for container setup (Solana CLI will be automatically installed)
3. Open terminal and verify: `solana --version`

### Using VS Code Dev Containers
1. Install the "Dev Containers" extension
2. Open project in VS Code
3. Command Palette → "Dev Containers: Reopen in Container"
4. Wait for setup completion

## Solana CLI Integration

The dev container automatically installs Solana CLI using:
```bash
curl --proto '=https' --tlsv1.2 -sSfL https://solana-install.solana.workers.dev | bash
```

The CLI is added to PATH for both bash and zsh shells, enabling immediate use of:
- `solana` - Main CLI tool
- `solana-keygen` - Key generation
- `solana-test-validator` - Local validator

## Solving the sendTransaction Issue

This dev container configuration specifically addresses the issue where the benchmark script's `sendTransaction` functionality was failing due to missing Solana CLI. With the pre-installed CLI, the complete transaction processing pipeline will work:

```
Benchmark Script → Solana CLI → sendTransaction RPC → Banking Stage → Transaction Processing
```

## Post-Setup Verification

After container creation, verify the setup:

```bash
# Check Solana CLI
solana --version
solana-keygen --version

# Build the project
slonana-build

# Run benchmark (should now work with CLI)
slonana-benchmark
```

## Configuration Details

The devcontainer.json includes:
- Ubuntu 22.04 base image with C++ tools
- Automatic Solana CLI installation via postCreateCommand
- VS Code C++ and Copilot extensions
- Port forwarding for validator services
- Optimized settings for C++ development

This ensures a consistent development environment where the sendTransaction RPC method works properly through the complete transaction processing pipeline.