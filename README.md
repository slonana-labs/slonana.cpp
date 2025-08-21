# slonana.cpp
C++ implementation of SVM/Solana validator

## Overview
This project implements a native C++ Solana validator with integrated SVM (Solana Virtual Machine) logic for high-performance blockchain operations. The validator can participate in consensus, block validation, and staking operations.

## Features

### Core Components
- **Network Layer**: Gossip protocol for peer discovery and RPC server for external interaction
- **Ledger Management**: Persistent block and transaction storage with chain validation
- **Validator Core**: Block validation, voting logic, and fork choice algorithm
- **Staking & Rewards**: Stake account management and reward distribution
- **SVM Execution Engine**: Program loading, execution, and state transitions

### Architecture
The validator is built with a modular architecture:

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Network       │    │   Validator     │    │   SVM           │
│   - Gossip      │◄──►│   - Core        │◄──►│   - Engine      │
│   - RPC         │    │   - Fork Choice │    │   - Accounts    │
└─────────────────┘    └─────────────────┘    └─────────────────┘
         │                       │                       │
         ▼                       ▼                       ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Ledger        │    │   Staking       │    │   Common        │
│   - Blocks      │    │   - Accounts    │    │   - Types       │
│   - Transactions│    │   - Rewards     │    │   - Utils       │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

## Building

### Prerequisites
- CMake 3.16 or higher
- C++20 compatible compiler (GCC 13.3+ recommended)
- Make

### Build Instructions
```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

### Testing
```bash
# Run the test suite
./slonana_tests

# Run comprehensive tests
./slonana_comprehensive_tests

# Run performance benchmarks
./slonana_benchmarks

# Or use the benchmark script
cd ..
./run_benchmarks.sh
```

### Benchmarking
Comprehensive performance benchmarks comparing with Anza/Agave reference implementation:

```bash
# Quick benchmark run
./build/slonana_benchmarks

# Full benchmark with build and reporting
./run_benchmarks.sh
```

**Benchmark Categories**:
- 🔧 Core Operations (hashing, serialization, parsing)
- 🔐 Cryptographic Operations (signatures, hash chains, merkle trees)
- 📊 Data Structures (account lookup, transaction queues, vote tracking)
- 🌐 Network Simulation (message handling, gossip propagation)
- 🧠 Memory Operations (allocation patterns, cache efficiency)
- 📄 JSON Processing (RPC parsing, response generation)

See [BENCHMARKING.md](BENCHMARKING.md) for detailed performance analysis and comparison with Anza/Agave.

## Usage

### Basic Validator
```bash
# Start validator with default settings
./slonana_validator

# Specify custom paths and addresses
./slonana_validator \
  --ledger-path /path/to/ledger \
  --identity /path/to/validator-keypair.json \
  --rpc-bind-address 127.0.0.1:8899 \
  --gossip-bind-address 127.0.0.1:8001
```

### Command Line Options
- `--ledger-path PATH`: Path to ledger data directory (default: ./ledger)
- `--identity KEYPAIR`: Path to validator identity keypair (default: ./validator-keypair.json)
- `--rpc-bind-address ADDR`: RPC server bind address (default: 127.0.0.1:8899)
- `--gossip-bind-address ADDR`: Gossip network bind address (default: 127.0.0.1:8001)
- `--no-rpc`: Disable RPC server
- `--no-gossip`: Disable gossip protocol
- `--help`: Show help message

## Implementation Status

✅ **Completed**:
- [x] Basic project structure and build system
- [x] Core type definitions and common utilities
- [x] Network layer (Gossip protocol and RPC server)
- [x] Ledger management (Block storage and transaction handling)
- [x] Validator core (Block validation and fork choice)
- [x] Staking and rewards system
- [x] SVM execution engine with basic program support
- [x] Main validator application with CLI
- [x] Comprehensive test suite

🚧 **Stub Implementation**:
- Cryptographic signature verification
- Actual network protocol implementation
- Real SVM program execution
- Persistent storage backends
- Advanced consensus features

## Testing

The project includes comprehensive unit tests covering all major components:

```bash
cd build
./slonana_tests
```

Expected output:
```
=== Slonana C++ Validator Test Suite ===
Running test: Common Types... PASSED
Running test: Validator Config... PASSED
Running test: Ledger Block Operations... PASSED
Running test: Ledger Manager... PASSED
Running test: Gossip Protocol... PASSED
Running test: Validator Core... PASSED
Running test: Staking Manager... PASSED
Running test: SVM Execution... PASSED
Running test: Full Validator... PASSED

=== Test Summary ===
Tests run: 9
Tests passed: 9
Tests failed: 0
All tests PASSED!
```

## Architecture Details

### Network Layer
- **GossipProtocol**: Handles peer discovery, cluster communication, and message broadcasting
- **RpcServer**: Provides JSON-RPC endpoints for external interaction

### Ledger Management
- **Block**: Contains transactions, metadata, and cryptographic proofs
- **Transaction**: Signed instructions with verification logic
- **LedgerManager**: Persistent storage with chain validation

### Validator Core
- **ForkChoice**: Implements fork selection algorithm based on stake weight
- **BlockValidator**: Validates block structure, signatures, and chain continuity
- **ValidatorCore**: Orchestrates consensus participation and vote production

### Staking System
- **StakeAccount**: Represents delegated stake to validators
- **ValidatorStakeInfo**: Tracks validator performance and stake amounts
- **RewardsCalculator**: Computes epoch rewards based on inflation and performance

### SVM Engine
- **ExecutionEngine**: Executes transactions and manages compute budgets
- **ProgramAccount**: Represents executable programs and their state
- **AccountManager**: Handles account lifecycle and rent collection

## License

This project is released into the public domain under the Unlicense. See LICENSE for details.
