# User Manual for Slonana.cpp Validator

## Table of Contents
- [Introduction](#introduction)
- [Getting Started](#getting-started)
- [Basic Operations](#basic-operations)
- [RPC API Usage](#rpc-api-usage)
- [Command Line Interface](#command-line-interface)
- [Configuration](#configuration)
- [Monitoring](#monitoring)
- [Common Use Cases](#common-use-cases)
- [FAQ](#faq)

## Introduction

Welcome to Slonana.cpp, a high-performance C++ implementation of a Solana-compatible blockchain validator. This manual will guide you through installing, configuring, and operating your validator.

### What is Slonana.cpp?

Slonana.cpp is a native C++ validator that implements:
- **Complete Solana RPC API** - 35+ JSON-RPC methods
- **High-Performance Execution** - Native C++ for maximum speed  
- **Full Validator Features** - Consensus, staking, SVM execution
- **Cross-Platform Support** - Linux, macOS, and Windows

### Key Benefits

- ⚡ **541x faster** than reference implementations in benchmarks
- 🔒 **Production-ready** with comprehensive security features
- 🌐 **Full Compatibility** with existing Solana ecosystem
- 📊 **Advanced Monitoring** with metrics and health checks
- 🐳 **Docker Ready** with orchestration support

## Getting Started

### Quick Installation

Choose your preferred installation method:

**Using Package Managers (Recommended):**

```bash
# macOS
brew install slonana-validator

# Ubuntu/Debian  
sudo apt install slonana-validator

# CentOS/RHEL/Fedora
sudo dnf install slonana-validator

# Windows
choco install slonana-validator
```

**Using Docker:**

```bash
# Pull and run latest image
docker run -p 8899:8899 slonana/validator:latest
```

**Download Binary:**

Visit [GitHub Releases](https://github.com/slonana-labs/slonana.cpp/releases) and download the appropriate binary for your platform.

### First Run

After installation, start your validator:

```bash
# Create data directory
mkdir -p ./validator-data

# Start validator (development mode)
slonana-validator \
  --ledger-path ./validator-data/ledger \
  --rpc-bind-address 127.0.0.1:8899

# Verify it's running
curl http://localhost:8899/health
```

You should see output similar to:
```
[2024-01-01T12:00:00Z] INFO  Initializing Solana validator...
[2024-01-01T12:00:01Z] INFO  RPC server listening on 127.0.0.1:8899
[2024-01-01T12:00:01Z] INFO  Gossip protocol listening on 127.0.0.1:8001
[2024-01-01T12:00:02Z] INFO  Validator ready and accepting connections
```

## Basic Operations

### Starting the Validator

**Minimum Configuration:**
```bash
slonana-validator --ledger-path ./ledger
```

**Production Configuration:**
```bash
slonana-validator \
  --ledger-path /var/lib/slonana/ledger \
  --identity /etc/slonana/validator-keypair.json \
  --rpc-bind-address 0.0.0.0:8899 \
  --gossip-bind-address 0.0.0.0:8001 \
  --log-level info
```

**Using Configuration File:**
```bash
slonana-validator --config /etc/slonana/validator.toml
```

### Stopping the Validator

```bash
# Graceful shutdown (if running in foreground)
Ctrl+C

# Using systemd (if installed as service)
sudo systemctl stop slonana-validator

# Using Docker
docker stop slonana-validator
```

### Checking Status

```bash
# Health check
curl http://localhost:8899/health

# Detailed status via RPC
curl -X POST http://localhost:8899 \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "getClusterNodes"
  }'

# Version information
slonana-validator --version
```

## RPC API Usage

### Basic RPC Calls

All RPC calls follow JSON-RPC 2.0 format:

```bash
curl -X POST http://localhost:8899 \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "METHOD_NAME",
    "params": [PARAMETERS]
  }'
```

### Common RPC Methods

#### Get Account Information

```bash
curl -X POST http://localhost:8899 \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "getAccountInfo",
    "params": ["4fYNw3dojWmQ4dXtSGE9epjRGy9fJsqZDAdqNTgDEDVX"]
  }'
```

#### Get Account Balance

```bash
curl -X POST http://localhost:8899 \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "getBalance",
    "params": ["4fYNw3dojWmQ4dXtSGE9epjRGy9fJsqZDAdqNTgDEDVX"]
  }'
```

#### Get Current Slot

```bash
curl -X POST http://localhost:8899 \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "getSlot"
  }'
```

#### Send Transaction

```bash
curl -X POST http://localhost:8899 \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "sendTransaction",
    "params": ["BASE64_ENCODED_TRANSACTION"]
  }'
```

### Available RPC Methods

**Account Methods:**
- `getAccountInfo` - Get account details
- `getBalance` - Get account balance
- `getProgramAccounts` - Get accounts owned by program
- `getMultipleAccounts` - Get multiple account info

**Block Methods:**
- `getSlot` - Get current slot
- `getBlock` - Get block information
- `getBlockHeight` - Get current block height
- `getBlocks` - Get list of blocks

**Transaction Methods:**
- `sendTransaction` - Submit transaction
- `getTransaction` - Get transaction details
- `simulateTransaction` - Simulate transaction
- `getRecentBlockhash` - Get recent blockhash

**Network Methods:**
- `getClusterNodes` - Get cluster node information
- `getVersion` - Get validator version
- `getHealth` - Get health status

**For complete API documentation, see [API.md](API.md)**

## Command Line Interface

### Core Options

```bash
slonana-validator [OPTIONS]

Core Options:
  --config PATH                Configuration file path
  --ledger-path PATH          Ledger data directory
  --identity PATH             Validator identity keypair
  --rpc-bind-address ADDR     RPC server bind address
  --gossip-bind-address ADDR  Gossip protocol bind address
  --log-level LEVEL           Logging level (debug|info|warn|error)

Network Options:
  --known-validator ADDR      Add known validator address
  --bootstrap-peers PEERS     Comma-separated bootstrap peers
  --max-connections NUM       Maximum network connections

Performance Options:
  --worker-threads NUM        Number of worker threads (0=auto)
  --compute-units-limit NUM   Maximum compute units per transaction

Security Options:
  --enable-rpc-auth          Enable RPC authentication
  --rpc-auth-token TOKEN     RPC authentication token
  --disable-unsafe-rpc       Disable unsafe RPC methods

Debugging Options:
  --version                  Show version information
  --help                     Show help message
  --dry-run                  Validate configuration without starting
```

### Examples

**Development Setup:**
```bash
slonana-validator \
  --ledger-path ./dev-ledger \
  --log-level debug \
  --rpc-bind-address 127.0.0.1:8899
```

**Production Setup:**
```bash
slonana-validator \
  --config /etc/slonana/validator.toml \
  --ledger-path /var/lib/slonana/ledger \
  --identity /etc/slonana/validator-keypair.json \
  --rpc-bind-address 0.0.0.0:8899 \
  --gossip-bind-address 0.0.0.0:8001 \
  --known-validator mainnet-beta.solana.com:8001 \
  --log-level info
```

**Cluster Setup:**
```bash
slonana-validator \
  --ledger-path ./cluster-node-1 \
  --rpc-bind-address 127.0.0.1:18899 \
  --gossip-bind-address 127.0.0.1:18001 \
  --bootstrap-peers 127.0.0.1:28001,127.0.0.1:38001
```

## Configuration

### Configuration File Format

The validator uses TOML format for configuration:

```toml
# /etc/slonana/validator.toml
[network]
rpc_bind_address = "0.0.0.0:8899"
gossip_bind_address = "0.0.0.0:8001"
max_connections = 1000

[storage]
ledger_path = "/var/lib/slonana/ledger"
compression_enabled = true

[rpc]
enable_rpc = true
rate_limit_requests_per_second = 100

[logging]
level = "info"
target = "file"
file_path = "/var/log/slonana/validator.log"

[security]
identity_keypair_path = "/etc/slonana/validator-keypair.json"
```

### Environment Variables

All configuration options can be set via environment variables:

```bash
# Core settings
export SLONANA_LEDGER_PATH=/var/lib/slonana/ledger
export SLONANA_IDENTITY_KEYPAIR=/etc/slonana/validator-keypair.json
export SLONANA_RPC_BIND_ADDRESS=0.0.0.0:8899
export SLONANA_LOG_LEVEL=info

# Performance settings
export SLONANA_WORKER_THREADS=8
export SLONANA_MAX_CONNECTIONS=1000

# Security settings
export SLONANA_ENABLE_RPC_AUTH=false
export SLONANA_RPC_AUTH_TOKEN=""
```

### Identity Management

Generate a new validator identity:

```bash
# Generate new keypair
slonana-validator generate-keypair validator-keypair.json

# View public key
slonana-validator show-public-key validator-keypair.json

# Validate keypair
slonana-validator validate-keypair validator-keypair.json
```

## Monitoring

### Health Checks

**Basic Health Check:**
```bash
curl http://localhost:8899/health
# Response: "ok" (200 status) or error details
```

**Detailed Health Check:**
```bash
curl -X POST http://localhost:8899 \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "getHealth"
  }'
```

### Metrics Collection

**Prometheus Metrics:**
```bash
# If metrics enabled (port 9090 by default)
curl http://localhost:9090/metrics
```

**Key Metrics to Monitor:**
- `slonana_validator_slot_height` - Current validator slot
- `slonana_rpc_requests_total` - Total RPC requests processed
- `slonana_network_peers_connected` - Number of connected peers
- `slonana_validator_vote_distance` - Vote lag from cluster
- `slonana_memory_usage_bytes` - Memory usage

### Log Analysis

**View Logs (systemd):**
```bash
# Follow logs
sudo journalctl -u slonana-validator -f

# View recent logs
sudo journalctl -u slonana-validator -n 100

# Search logs
sudo journalctl -u slonana-validator | grep ERROR
```

**Log Levels:**
- `DEBUG` - Detailed debugging information
- `INFO` - General operational messages
- `WARN` - Warning conditions
- `ERROR` - Error conditions requiring attention

### Performance Monitoring

**System Resources:**
```bash
# CPU and memory usage
top -p $(pgrep slonana-validator)

# Network connections
ss -tuln | grep -E ":(8899|8001|8003)"

# Disk I/O
iostat -x 1

# Ledger size
du -sh /var/lib/slonana/ledger
```

## Common Use Cases

### Development and Testing

**Local Development Node:**
```bash
# Start a local validator for development
slonana-validator \
  --ledger-path ./dev-ledger \
  --log-level debug \
  --enable-test-rpc-methods \
  --rpc-bind-address 127.0.0.1:8899
```

**Multi-Node Test Cluster:**
```bash
# Start 3 nodes for testing
# Node 1
slonana-validator --ledger-path ./node1 --rpc-bind-address 127.0.0.1:18899 --gossip-bind-address 127.0.0.1:18001 &

# Node 2  
slonana-validator --ledger-path ./node2 --rpc-bind-address 127.0.0.1:28899 --gossip-bind-address 127.0.0.1:28001 --bootstrap-peers 127.0.0.1:18001 &

# Node 3
slonana-validator --ledger-path ./node3 --rpc-bind-address 127.0.0.1:38899 --gossip-bind-address 127.0.0.1:38001 --bootstrap-peers 127.0.0.1:18001,127.0.0.1:28001 &
```

### RPC Service Provider

**High-Performance RPC Node:**
```bash
slonana-validator \
  --config /etc/slonana/rpc-node.toml \
  --ledger-path /var/lib/slonana/ledger \
  --rpc-bind-address 0.0.0.0:8899 \
  --worker-threads 16 \
  --max-connections 2000 \
  --rate-limit-requests-per-second 1000 \
  --enable-rpc-auth \
  --log-level info
```

### Mainnet Validator

**Production Mainnet Validator:**
```bash
slonana-validator \
  --config /etc/slonana/mainnet-validator.toml \
  --ledger-path /var/lib/slonana/ledger \
  --identity /etc/slonana/mainnet-keypair.json \
  --rpc-bind-address 0.0.0.0:8899 \
  --gossip-bind-address 0.0.0.0:8001 \
  --known-validator mainnet-beta.solana.com:8001 \
  --expected-genesis-hash 5eykt4UsFv8P8NJdTREpY1vzqKqZKvdpKuc147dw2N9d \
  --log-level info
```

## FAQ

### General Questions

**Q: What's the difference between Slonana.cpp and the official Solana validator?**

A: Slonana.cpp is a native C++ implementation that offers:
- Significantly better performance (541x faster in benchmarks)
- Lower resource usage
- Complete API compatibility
- Enhanced monitoring and observability features

**Q: Can I use Slonana.cpp as a drop-in replacement for the Solana validator?**

A: Yes! Slonana.cpp implements the complete Solana RPC API and network protocols, making it fully compatible with existing Solana infrastructure and applications.

**Q: Is Slonana.cpp production-ready?**

A: Yes, Slonana.cpp includes comprehensive testing, security features, and monitoring capabilities suitable for production deployment.

### Technical Questions

**Q: How much storage space do I need?**

A: Storage requirements depend on your use case:
- Development: 1-10 GB
- RPC node: 100-500 GB
- Full validator: 1+ TB (and growing)

**Q: What are the minimum system requirements?**

A: Minimum: 4 CPU cores, 8 GB RAM, 100 GB SSD
Recommended: 16+ CPU cores, 32+ GB RAM, 1+ TB NVMe SSD

**Q: How do I migrate from the official Solana validator?**

A: 1. Stop your current validator
2. Install Slonana.cpp
3. Use the same ledger directory and identity keypair
4. Start Slonana.cpp with equivalent configuration

**Q: Does Slonana.cpp support clustering?**

A: Yes, you can run multiple Slonana.cpp validators in a cluster configuration using the bootstrap peers functionality.

### Troubleshooting

**Q: The validator won't start. What should I check?**

A: Common issues:
1. Port conflicts - check if ports 8899/8001 are already in use
2. Permission issues - ensure the validator user can write to data directories
3. Missing identity keypair - generate one if needed
4. Insufficient disk space - check available storage

**Q: RPC requests are timing out. How can I fix this?**

A: Try:
1. Increase worker threads: `--worker-threads 16`
2. Increase connection limits: `--max-connections 2000`
3. Check network connectivity and firewall rules
4. Monitor system resources for bottlenecks

**Q: How do I enable debug logging?**

A: Use `--log-level debug` or set `SLONANA_LOG_LEVEL=debug` environment variable.

### Support

For additional support:
- **Documentation**: https://github.com/slonana-labs/slonana.cpp/tree/main/docs
- **GitHub Issues**: https://github.com/slonana-labs/slonana.cpp/issues
- **Community Discord**: https://discord.gg/slonana

---

This manual covers the essential operations for running Slonana.cpp. For advanced configuration and deployment scenarios, see the [Deployment Guide](DEPLOYMENT.md) and [Architecture Guide](ARCHITECTURE.md).