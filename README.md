# Slonana.cpp

[![Build Status](https://github.com/slonana-labs/slonana.cpp/workflows/CI%2FCD%20Pipeline/badge.svg)](https://github.com/slonana-labs/slonana.cpp/actions)
[![Docker Pulls](https://img.shields.io/docker/pulls/slonana/validator)](https://hub.docker.com/r/slonana/validator)
[![GitHub Release](https://img.shields.io/github/v/release/slonana-labs/slonana.cpp)](https://github.com/slonana-labs/slonana.cpp/releases)
[![License](https://img.shields.io/badge/license-Unlicense-blue.svg)](LICENSE)

**Production-ready high-performance C++ implementation of a Solana-compatible blockchain validator**

Slonana.cpp is a battle-tested native C++ validator that delivers exceptional performance while maintaining full compatibility with the Solana ecosystem. Built from the ground up for speed, security, and scalability, with all critical bugs eliminated and comprehensive real implementations throughout.

## 🚀 Key Features

- **⚡ Extreme Performance**: Optimized for high throughput and low latency using modern C++ features.
- **🌐 Complete Solana RPC API**: Fully compatible with the Solana JSON-RPC 2.0 specification, supporting over 35 methods.
- **🔒 Production Ready**: Hardened with robust fault tolerance, security, and monitoring features.
- **🐳 Docker Native**: Multi-architecture container support with comprehensive deployment scenarios using Docker Compose.
- **🔧 Cross-Platform**: Supports Linux, macOS, and Windows with a universal installer.
- **✅ Comprehensive Documentation**: Full source code documentation, providing clarity for every module, class, and function.
- **🛡️ Battle-Tested**: All critical bugs have been eliminated, ensuring stable and reliable operation.
- **📊 Hardware Integration**: Supports Ledger and Trezor hardware wallets for secure key management.

## 📚 Codebase Documentation

The entire codebase is thoroughly documented using Doxygen-style comments. This includes detailed explanations for every module, class, function, and parameter.

To generate the documentation locally, you will need to have Doxygen installed. Then, you can run the following command from the root of the repository:

```bash
doxygen Doxyfile
```

This will generate a `docs/html` directory containing the full browsable documentation.

For a high-level overview of the system, please refer to the following guides:

| Document | Description |
|----------|-------------|
| **[User Manual](docs/USER_MANUAL.md)** | Complete guide for operators and users. |
| **[API Documentation](docs/API.md)** | Comprehensive RPC API reference. |
| **[Architecture Guide](docs/ARCHITECTURE.md)** | Deep dive into the system design and components. |
| **[Development Guide](docs/DEVELOPMENT.md)** | Instructions for contributing and development workflows. |
| **[Contributing Guide](CONTRIBUTING.md)** | Required development workflow and performance standards. |
| **[Deployment Guide](docs/DEPLOYMENT.md)** | Production deployment and configuration guides. |
| **[Testing Guide](TESTING.md)** | Information on the testing framework and procedures. |
| **[Benchmarking Guide](BENCHMARKING.md)** | Performance analysis and comparisons. |

## 🚀 Quick Start

### One-Line Installation

**Universal Installer (Recommended):**
```bash
curl -sSL https://install.slonana.com | bash
```

**Or download and run locally:**
```bash
wget https://raw.githubusercontent.com/slonana-labs/slonana.cpp/main/install.sh
chmod +x install.sh && ./install.sh
```

This universal installer automatically:
- ✅ Detects your operating system (Linux, macOS, Windows/WSL).
- ✅ Installs all required dependencies for your platform.
- ✅ Downloads and configures the latest stable release.
- ✅ Sets up monitoring and logging.
- ✅ Verifies the installation with health checks.

### Manual Installation

Choose your preferred method:

**From Source:**
```bash
# Clone and build
git clone https://github.com/slonana-labs/slonana.cpp.git
cd slonana.cpp
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

**Docker:**
```bash
docker run -p 8899:8899 slonana/validator:latest
```

**Package Managers:**
```bash
# macOS
brew install slonana-validator

# Ubuntu/Debian  
sudo apt update && sudo apt install slonana-validator

# CentOS/RHEL/Fedora
sudo dnf install slonana-validator

# Windows (Chocolatey)
choco install slonana-validator
```

## 🏗️ Architecture

Slonana.cpp features a modular, high-performance architecture designed for clarity, maintainability, and performance.

```
┌─────────────────────────────────────────────────────────────────┐
│                    Slonana.cpp Validator                        │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────────┐     │
│  │   Network   │    │  Validator  │    │      SVM        │     │
│  │   Layer     │◄──►│    Core     │◄──►│    Engine       │     │
│  │             │    │             │    │                 │     │
│  │ • Gossip    │    │ • Consensus │    │ • Execution     │     │
│  │ • RPC       │    │ • Voting    │    │ • Programs      │     │
│  │ • P2P       │    │ • ForkChoice│    │ • Accounts      │     │
│  └─────────────┘    └─────────────┘    └─────────────────┘     │
│         │                   │                   │              │
│         ▼                   ▼                   ▼              │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────────┐     │
│  │   Ledger    │    │   Staking   │    │     Common      │     │
│  │  Management │    │   System    │    │    Types        │     │
│  │             │    │             │    │                 │     │
│  │ • Blocks    │    │ • Accounts  │    │ • Crypto        │     │
│  │ • Txns      │    │ • Rewards   │    │ • Serialization │     │
│  │ • Storage   │    │ • Slashing  │    │ • Utilities     │     │
│  └─────────────┘    └─────────────┘    └─────────────────┘     │
└─────────────────────────────────────────────────────────────────┘
```

### Core Modules

*   **`common`**: Contains foundational data structures, types, and utilities used across the entire codebase. This includes logging, fault tolerance mechanisms, and recovery tools.
*   **`network`**: Manages all peer-to-peer communication. It includes the `GossipProtocol` for cluster-wide message dissemination and the `SolanaRpcServer` for handling external JSON-RPC requests.
*   **`ledger`**: Responsible for the persistent storage of the blockchain. It manages the block and transaction database, ensuring data integrity and providing access to historical data.
*   **`consensus`**: Implements the Tower BFT consensus algorithm. This module is responsible for vote lockouts, fork choice, and ensuring the safety of the chain.
*   **`banking`**: A high-throughput, multi-stage pipeline for processing transactions. It handles validation, execution, and commitment of transactions to the ledger.
*   **`svm`**: The Solana Virtual Machine, responsible for executing on-chain programs (smart contracts). It manages account state and program execution.
*   **`security`**: Provides a robust security layer for inter-node communication, including TLS and message-level signing and encryption.
*   **`validator`**: The core orchestrator of the validator node. It initializes and manages all other modules, bringing them together to form a cohesive system.

**Key Design Principles:**
- 🔄 **Zero-Copy Design**: Minimizes memory allocations and data copies for maximum efficiency.
- 🔒 **Lock-Free Algorithms**: Employs lock-free data structures in performance-critical paths to maximize concurrency.
- ⚡ **NUMA Awareness**: Optimized for modern multi-socket hardware architectures.
- 📊 **Cache Efficiency**: Utilizes data structures and algorithms that are friendly to modern CPU caches.

For detailed architecture information, see the **[Architecture Guide](docs/ARCHITECTURE.md)**.

## 🔧 Building from Source

### Prerequisites
- CMake 3.16+
- C++20 compatible compiler (GCC 13.3+, Clang 15+)
- OpenSSL development libraries

### Build Instructions

```bash
# Clone repository
git clone https://github.com/slonana-labs/slonana.cpp.git
cd slonana.cpp

# Install dependencies (Ubuntu/Debian)
sudo apt update
sudo apt install build-essential cmake libssl-dev

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run tests
./slonana_tests
./slonana_comprehensive_tests

# Install
sudo make install
```

For detailed development setup, see the **[Development Guide](docs/DEVELOPMENT.md)**.

## 🧪 Testing

A comprehensive testing framework with 70+ tests covers all components of the validator.

```bash
# Run all tests
cd build
./slonana_comprehensive_tests

# Run specific test suites
./slonana_tests          # Basic tests
./test_rpc_comprehensive # RPC API tests
./slonana_benchmarks     # Performance benchmarks

# Run with Docker
docker run slonana/validator:dev test-all
```

**Test Coverage:**
- ✅ **Unit tests** for all core components.
- ✅ **Integration tests** with multi-node scenarios.
- ✅ **RPC API conformance tests** for over 35 methods.
- ✅ **Performance benchmarks** with realistic load testing.
- ✅ **Security and edge case validation**.
- ✅ **Hardware wallet integration tests**.

See the **[Testing Guide](TESTING.md)** for detailed testing procedures.

## 🤝 Contributing

We welcome contributions! Please see our **[Development Guide](docs/DEVELOPMENT.md)** for:

- Development environment setup
- Code style guidelines
- Testing requirements
- Pull request process

### Quick Contribution Guide

```bash
# Fork and clone
git clone https://github.com/yourusername/slonana.cpp.git
cd slonana.cpp

# Create feature branch
git checkout -b feature/amazing-feature

# Make changes and test
make test

# Format code
make format

# Submit PR
git push origin feature/amazing-feature
```

## 📄 License

This project is released into the public domain under the [Unlicense](LICENSE).