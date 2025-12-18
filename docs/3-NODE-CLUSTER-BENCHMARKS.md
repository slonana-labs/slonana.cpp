# 3-Node Cluster Benchmarking

This document describes the 3-node cluster benchmarking capabilities for both Agave and Slonana validators.

## Overview

The 3-node cluster benchmarking system provides comprehensive performance testing with:

- **Full cluster setup**: 3 validators with distinct keypairs
- **Vote accounts**: Proper vote account creation and management
- **Stake delegation**: Automated stake delegation to validators
- **Gossip networking**: Inter-validator communication via gossip protocol
- **Transaction load balancing**: Round-robin distribution across nodes
- **Cluster health monitoring**: Per-node and aggregate health checks
- **Extended testing**: 5-minute test duration for thorough analysis

## Architecture

### Port Configuration

Each validator node uses dedicated ports to avoid conflicts:

#### Node 1 (Bootstrap)
- RPC Port: 8899
- Gossip Port: 8001
- TPU Port Range: 8003-8023

#### Node 2
- RPC Port: 8999
- Gossip Port: 8002
- TPU Port Range: 8005-8025

#### Node 3
- RPC Port: 9099
- Gossip Port: 8010
- TPU Port Range: 8012-8032

### Keypair Structure

Each validator requires three keypairs:
- **Identity keypair**: Validator's identity on the network
- **Vote account keypair**: For recording votes
- **Stake account keypair**: For delegating stake

## Scripts

### benchmark_agave_3node.sh

Runs a 3-node Agave validator cluster with full configuration.

**Usage:**
```bash
./scripts/benchmark_agave_3node.sh \
    --cluster /tmp/agave_cluster \
    --results /tmp/agave_results \
    --test-duration 300
```

**Features:**
- Automatic genesis creation with 3 bootstrap validators
- Vote account setup and stake delegation
- Gossip entrypoint configuration
- Transaction load balancing
- Per-node health monitoring
- Comprehensive results in JSON format

### benchmark_slonana_3node.sh

Runs a 3-node Slonana validator cluster with matching configuration.

**Usage:**
```bash
./scripts/benchmark_slonana_3node.sh \
    --cluster /tmp/slonana_cluster \
    --results /tmp/slonana_results \
    --test-duration 300
```

**Features:**
- Same architecture as Agave benchmark for fair comparison
- Automatic binary detection from build directory
- CI environment optimization
- Load-balanced transaction distribution
- Cluster health verification

### compare_3node_clusters.sh

Runs both Agave and Slonana benchmarks and generates comparison report.

**Usage:**
```bash
./scripts/compare_3node_clusters.sh --duration 300
```

**Options:**
- `--duration SECONDS`: Test duration (default: 300)
- `--skip-agave`: Skip Agave benchmark
- `--skip-slonana`: Skip Slonana benchmark

**Output:**
- Side-by-side performance comparison
- TPS and latency differences
- Load distribution analysis
- Combined JSON report for further analysis

## Benchmark Results

### JSON Output Format

Each benchmark produces a JSON results file with the following structure:

```json
{
  "validator_type": "agave-3node-cluster|slonana-3node-cluster",
  "timestamp": "2025-12-16T18:33:09Z",
  "test_duration_seconds": 300,
  "cluster_config": {
    "num_validators": 3,
    "vote_accounts": true,
    "stake_delegation": true
  },
  "rpc_latency_ms": 25,
  "effective_tps": 1250,
  "submitted_requests": 375000,
  "successful_transactions": 375000,
  "load_distribution": {
    "node1_transactions": 125000,
    "node2_transactions": 125000,
    "node3_transactions": 125000
  },
  "system_info": {
    "cores": 8,
    "total_memory_mb": 32768
  }
}
```

### Metrics Explained

- **effective_tps**: Successful transactions per second
- **rpc_latency_ms**: Average RPC response time across all nodes
- **load_distribution**: Number of transactions sent to each node
- **successful_transactions**: Total transactions that completed successfully

## Prerequisites

### Agave Benchmark
- `agave-validator` binary
- Solana CLI tools (`solana`, `solana-keygen`, `solana-genesis`)
- System utilities: `curl`, `jq`, `awk`

### Slonana Benchmark
- Built `slonana_validator` binary in `build/` directory
- Solana CLI tools (same as Agave)
- System utilities: `curl`, `jq`, `awk`

### Installation

Install Solana CLI tools:
```bash
curl --proto '=https' --tlsv1.2 -sSfL https://solana-install.solana.workers.dev | bash
```

Install Agave validator (optional, for comparison):
```bash
cargo install agave-validator --locked
```

Build Slonana:
```bash
cd slonana.cpp
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make slonana_validator
```

## Examples

### Run Agave Benchmark Only
```bash
./scripts/benchmark_agave_3node.sh \
    --cluster /tmp/agave_test \
    --results /tmp/agave_results \
    --verbose
```

### Run Slonana Benchmark Only
```bash
./scripts/benchmark_slonana_3node.sh \
    --cluster /tmp/slonana_test \
    --results /tmp/slonana_results \
    --verbose
```

### Run Full Comparison
```bash
./scripts/compare_3node_clusters.sh
```

### Bootstrap Only (No Tests)
```bash
./scripts/benchmark_slonana_3node.sh \
    --cluster /tmp/cluster \
    --results /tmp/results \
    --bootstrap-only
```

## Troubleshooting

### Port Conflicts

If you see port binding errors:
```bash
# Check for processes using the ports
lsof -i :8899
lsof -i :8001

# Kill conflicting processes
kill -9 <PID>
```

### Validator Startup Failures

Check the validator logs:
```bash
tail -f /tmp/<results_dir>/node1.log
tail -f /tmp/<results_dir>/node2.log
tail -f /tmp/<results_dir>/node3.log
```

### Insufficient File Descriptors

Increase file descriptor limits:
```bash
ulimit -n 1000000
```

Or edit `/etc/security/limits.conf`:
```
* soft nofile 1000000
* hard nofile 1000000
```

### Genesis Configuration Issues

Remove existing ledger data and regenerate:
```bash
rm -rf /tmp/<cluster_dir>
# Re-run the benchmark script
```

## Performance Optimization

### For Higher TPS

1. **Reduce sleep time** in transaction loop (edit scripts)
2. **Increase transaction batch size**
3. **Use faster storage** for ledger data (SSD/NVMe)
4. **Allocate more resources** (CPU cores, RAM)

### For Lower Latency

1. **Use localhost** for all connections
2. **Disable network throttling**
3. **Optimize RPC configuration**
4. **Run on dedicated hardware**

## CI/CD Integration

The scripts automatically detect CI environments and optimize accordingly:

```bash
export CI=true
export GITHUB_ACTIONS=true
./scripts/compare_3node_clusters.sh
```

CI optimizations include:
- Appropriate test duration
- Resource-aware configuration
- Automatic cleanup on failure
- JSON results for automated analysis

## Advanced Usage

### Custom Validator Arguments

Edit the `start_validator_node()` function in either script to add custom arguments:

```bash
# In benchmark_slonana_3node.sh
cmd+=(--custom-arg value)
cmd+=(--another-arg)
```

### Different Port Ranges

Modify the port configuration variables at the top of the scripts:

```bash
NODE1_RPC=18899
NODE1_GOSSIP=18001
# etc.
```

### Custom Genesis Configuration

Edit the `setup_cluster()` function to customize genesis parameters:

```bash
solana-genesis \
    --faucet-lamports 2000000000000000 \
    --bootstrap-validator-lamports 1000000000000000 \
    # etc.
```

## Known Limitations

1. **Local testing only**: Scripts are designed for local development/testing
2. **Resource intensive**: Running 3 validators requires significant resources
3. **No network simulation**: Does not simulate real network conditions
4. **Limited to 3 nodes**: Architecture is fixed at 3 validators

## Future Enhancements

Potential improvements for future versions:

- [ ] Configurable number of validators (N-node clusters)
- [ ] Network condition simulation (latency, packet loss)
- [ ] Byzantine fault testing
- [ ] Automatic performance regression detection
- [ ] Grafana dashboard integration
- [ ] Docker/Kubernetes deployment options
- [ ] Remote cluster support
- [ ] Historical performance tracking

## References

- [Solana Validator Documentation](https://docs.solana.com/running-validator)
- [Agave Validator Guide](https://github.com/anza-xyz/agave)
- [Slonana Architecture](../README.md)

## Support

For issues or questions:
1. Check the troubleshooting section above
2. Review validator logs
3. Open an issue on GitHub
4. Contact the development team

---

**Last Updated**: December 2025
**Version**: 1.0.0
