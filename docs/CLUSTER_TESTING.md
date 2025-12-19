# Multi-Node Cluster Testing Guide

This guide covers setting up and testing multi-node Slonana clusters with ML inference capabilities.

## Table of Contents
1. [Local 3-Node Cluster](#local-3-node-cluster)
2. [10-Node Performance Cluster](#10-node-performance-cluster)
3. [Consensus Testing](#consensus-testing)
4. [Network Testing](#network-testing)
5. [Load Testing](#load-testing)
6. [Monitoring](#monitoring)

## Local 3-Node Cluster

### Prerequisites

```bash
# Install dependencies
sudo apt-get install -y python3 tmux jq

# Build validator
cd /home/runner/work/slonana.cpp/slonana.cpp
make release
```

### Setup Script

Create `scripts/setup_local_cluster.sh`:

```bash
#!/bin/bash
set -e

NUM_NODES=${1:-3}
BASE_PORT=8899
GOSSIP_BASE=8001

echo "Setting up $NUM_NODES node local cluster..."

# Create directory structure
mkdir -p cluster/node{1..$NUM_NODES}/{ledger,config}

# Generate keypairs
for i in $(seq 1 $NUM_NODES); do
    ./slonana_keygen new -o cluster/node$i/config/validator-keypair.json
    echo "Generated keypair for node $i"
done

# Create configuration files
for i in $(seq 1 $NUM_NODES); do
    RPC_PORT=$((BASE_PORT + i - 1))
    GOSSIP_PORT=$((GOSSIP_BASE + i - 1))
    
    cat > cluster/node$i/config/validator.toml <<EOF
[validator]
identity_keypair = "cluster/node$i/config/validator-keypair.json"
ledger_path = "cluster/node$i/ledger"

[rpc]
bind_address = "127.0.0.1:$RPC_PORT"
enable = true

[gossip]
bind_address = "127.0.0.1:$GOSSIP_PORT"
entrypoint = "127.0.0.1:$GOSSIP_BASE"

[consensus]
expected_shred_version = 1
enable_voting = true

[ml_inference]
enable = true
max_agents = 1000
EOF

    echo "Created config for node $i (RPC: $RPC_PORT, Gossip: $GOSSIP_PORT)"
done

echo "Cluster setup complete!"
```

### Start Cluster

Create `scripts/start_cluster.sh`:

```bash
#!/bin/bash
set -e

NUM_NODES=${1:-3}

echo "Starting $NUM_NODES node cluster..."

# Start nodes in tmux sessions
for i in $(seq 1 $NUM_NODES); do
    SESSION="slonana-node$i"
    
    tmux new-session -d -s $SESSION
    tmux send-keys -t $SESSION "./slonana_validator --config cluster/node$i/config/validator.toml" C-m
    
    echo "Started node $i in tmux session: $SESSION"
    sleep 2
done

echo "All nodes started. Use 'tmux attach -t slonana-node1' to view node 1"
echo "Monitor all nodes with: ./scripts/monitor_cluster.sh"
```

### Test Cluster

Create `scripts/test_cluster.sh`:

```bash
#!/bin/bash
set -e

echo "Testing cluster health..."

# Check each node
for port in 8899 8900 8901; do
    echo -n "Node RPC $port: "
    
    if curl -s -X POST -H "Content-Type: application/json" \
        -d '{"jsonrpc":"2.0","id":1,"method":"getHealth"}' \
        http://127.0.0.1:$port > /dev/null 2>&1; then
        echo "✓ Healthy"
    else
        echo "✗ Unhealthy"
    fi
done

# Check cluster size
CLUSTER_SIZE=$(curl -s -X POST -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","id":1,"method":"getClusterNodes"}' \
    http://127.0.0.1:8899 | jq '.result | length')

echo "Cluster size: $CLUSTER_SIZE nodes"

# Deploy test program
echo "Deploying ML test program..."
./slonana deploy examples/async_agent/build/async_agent_sbpf.so \
    --url http://127.0.0.1:8899

echo "Cluster test complete!"
```

## 10-Node Performance Cluster

### Multi-Machine Setup

For a distributed cluster across multiple machines:

```bash
# On each machine, configure with unique IP
cat > /opt/slonana/config/validator.toml <<EOF
[validator]
identity_keypair = "/opt/slonana/keys/validator-keypair.json"
ledger_path = "/mnt/ledger"

[rpc]
bind_address = "0.0.0.0:8899"
enable = true

[gossip]
bind_address = "0.0.0.0:8001"
entrypoint = "10.0.1.1:8001"  # Bootstrap node IP

[consensus]
expected_shred_version = 1
enable_voting = true

[ml_inference]
enable = true
max_agents = 10000
EOF
```

### Network Topology

```
Bootstrap Node (10.0.1.1)
├── Validator 1 (10.0.1.2)
├── Validator 2 (10.0.1.3)
├── Validator 3 (10.0.1.4)
├── Validator 4 (10.0.1.5)
├── Validator 5 (10.0.1.6)
├── Validator 6 (10.0.1.7)
├── Validator 7 (10.0.1.8)
├── Validator 8 (10.0.1.9)
└── Validator 9 (10.0.1.10)
```

## Consensus Testing

### Fork Resolution Test

```bash
# scripts/test_fork_resolution.sh
#!/bin/bash

echo "Testing fork resolution..."

# Create network partition
sudo iptables -A INPUT -s 10.0.1.2 -j DROP
sudo iptables -A INPUT -s 10.0.1.3 -j DROP

echo "Network partition created (nodes 1-2 isolated)"
sleep 30

# Heal partition
sudo iptables -D INPUT -s 10.0.1.2 -j DROP
sudo iptables -D INPUT -s 10.0.1.3 -j DROP

echo "Network partition healed"
sleep 60

# Check if cluster converged
./scripts/check_consensus.sh
```

### Vote Aggregation Test

```bash
# Monitor vote activity
for i in {1..10}; do
    VOTES=$(curl -s http://10.0.1.$i:8899/api/v1/metrics | grep "vote_count" | cut -d' ' -f2)
    echo "Node $i: $VOTES votes"
done
```

### Leader Rotation Test

```bash
# Monitor leader schedule
watch -n 1 'curl -s -X POST -H "Content-Type: application/json" \
    -d "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getLeaderSchedule\"}" \
    http://127.0.0.1:8899 | jq ".result"'
```

## Network Testing

### Latency Test

```bash
# scripts/test_network_latency.sh
#!/bin/bash

echo "Testing network latency between nodes..."

for i in {1..10}; do
    for j in {1..10}; do
        if [ $i -ne $j ]; then
            LATENCY=$(ping -c 5 10.0.1.$j | tail -1 | awk '{print $4}' | cut -d '/' -f 2)
            echo "Node $i -> Node $j: ${LATENCY}ms"
        fi
    done
done
```

### Bandwidth Test

```bash
# Test bandwidth between nodes
iperf3 -s -p 5201 &  # Server on node 1
iperf3 -c 10.0.1.1 -p 5201 -P 10 -t 60  # Client from node 2
```

### Packet Loss Test

```bash
# Simulate packet loss
sudo tc qdisc add dev eth0 root netem loss 1%

# Test transaction success rate
./scripts/send_transactions.sh 1000

# Remove packet loss
sudo tc qdisc del dev eth0 root
```

## Load Testing

### Transaction Generation

Create `scripts/generate_load.py`:

```python
#!/usr/bin/env python3
import json
import requests
import time
from concurrent.futures import ThreadPoolExecutor

RPC_URL = "http://127.0.0.1:8899"
TARGET_TPS = 10000
DURATION_SECONDS = 300

def send_transaction():
    """Send a single transaction"""
    payload = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "sendTransaction",
        "params": [{
            "instructions": [{
                "program_id": "MLInferenceProgramId",
                "accounts": [],
                "data": "base58_encoded_data"
            }],
            "signatures": []
        }]
    }
    
    try:
        response = requests.post(RPC_URL, json=payload, timeout=5)
        return response.status_code == 200
    except:
        return False

def load_test():
    """Run load test"""
    print(f"Starting load test: {TARGET_TPS} TPS for {DURATION_SECONDS}s")
    
    start_time = time.time()
    total_sent = 0
    total_success = 0
    
    with ThreadPoolExecutor(max_workers=100) as executor:
        while time.time() - start_time < DURATION_SECONDS:
            batch_start = time.time()
            
            # Send batch of transactions
            futures = [executor.submit(send_transaction) for _ in range(TARGET_TPS)]
            results = [f.result() for f in futures]
            
            total_sent += len(results)
            total_success += sum(results)
            
            # Sleep to maintain target TPS
            batch_duration = time.time() - batch_start
            if batch_duration < 1.0:
                time.sleep(1.0 - batch_duration)
    
    duration = time.time() - start_time
    actual_tps = total_sent / duration
    success_rate = (total_success / total_sent) * 100
    
    print(f"Load test complete:")
    print(f"  Duration: {duration:.1f}s")
    print(f"  Transactions sent: {total_sent}")
    print(f"  Transactions successful: {total_success}")
    print(f"  Actual TPS: {actual_tps:.1f}")
    print(f"  Success rate: {success_rate:.1f}%")

if __name__ == "__main__":
    load_test()
```

### Ramp-Up Test

```bash
# scripts/ramp_up_test.sh
#!/bin/bash

echo "Starting ramp-up test..."

for tps in 100 500 1000 2000 5000 10000; do
    echo "Testing at ${tps} TPS..."
    python3 scripts/generate_load.py --tps $tps --duration 60
    sleep 30  # Cool down between tests
done
```

## Monitoring

### Prometheus Configuration

Create `monitoring/prometheus.yml`:

```yaml
global:
  scrape_interval: 15s

scrape_configs:
  - job_name: 'slonana_validator'
    static_configs:
      - targets:
          - '10.0.1.1:9090'
          - '10.0.1.2:9090'
          - '10.0.1.3:9090'
          - '10.0.1.4:9090'
          - '10.0.1.5:9090'
          - '10.0.1.6:9090'
          - '10.0.1.7:9090'
          - '10.0.1.8:9090'
          - '10.0.1.9:9090'
          - '10.0.1.10:9090'
```

### Grafana Dashboards

Create dashboard JSON for key metrics:

- **Validator Health**
  - Node uptime
  - RPC request rate
  - Transaction throughput
  - Block height

- **ML Inference Metrics**
  - Inference count
  - Inference latency (P50, P95, P99)
  - Model types used
  - Agent count

- **Consensus Metrics**
  - Vote rate
  - Leader schedule
  - Fork count
  - Confirmation time

- **Network Metrics**
  - Gossip message rate
  - Peer count
  - Network bandwidth
  - Packet loss

### Monitoring Script

Create `scripts/monitor_cluster.sh`:

```bash
#!/bin/bash

while true; do
    clear
    echo "=== Slonana Cluster Status ==="
    echo
    
    for i in {1..3}; do
        PORT=$((8898 + i))
        
        echo "Node $i (RPC: $PORT):"
        
        # Get slot height
        SLOT=$(curl -s -X POST -H "Content-Type: application/json" \
            -d '{"jsonrpc":"2.0","id":1,"method":"getSlot"}' \
            http://127.0.0.1:$PORT | jq -r '.result')
        echo "  Slot: $SLOT"
        
        # Get transaction count
        TX_COUNT=$(curl -s -X POST -H "Content-Type: application/json" \
            -d '{"jsonrpc":"2.0","id":1,"method":"getTransactionCount"}' \
            http://127.0.0.1:$PORT | jq -r '.result')
        echo "  Transactions: $TX_COUNT"
        
        # Get peer count
        PEERS=$(curl -s -X POST -H "Content-Type: application/json" \
            -d '{"jsonrpc":"2.0","id":1,"method":"getClusterNodes"}' \
            http://127.0.0.1:$PORT | jq -r '.result | length')
        echo "  Peers: $PEERS"
        
        echo
    done
    
    sleep 5
done
```

## Testing Checklist

- [ ] Set up local 3-node cluster
- [ ] Verify gossip between nodes
- [ ] Test consensus with fork resolution
- [ ] Deploy ML inference programs
- [ ] Run load test at target TPS
- [ ] Test network partition recovery
- [ ] Monitor metrics with Prometheus/Grafana
- [ ] Test leader rotation
- [ ] Verify state synchronization
- [ ] Document test results

## Performance Targets

| Metric | Target | Pass Criteria |
|--------|--------|---------------|
| Throughput | 10,000 TPS | >8,000 TPS |
| Confirmation Time | <1s (P99) | <2s (P99) |
| Fork Resolution | <60s | <120s |
| Peer Discovery | <30s | <60s |
| Network Latency | <100ms | <200ms |
| ML Inference | <1ms | <5ms |

## Troubleshooting

### Cluster Won't Form

```bash
# Check gossip connectivity
nc -zv 127.0.0.1 8001

# Check logs
tail -f cluster/node1/ledger/validator.log

# Verify keypairs
./slonana_keygen verify cluster/node1/config/validator-keypair.json
```

### High Latency

```bash
# Check network
ping -c 10 10.0.1.2

# Check CPU usage
top -p $(pidof slonana_validator)

# Check disk I/O
iostat -x 1
```

### Fork Not Resolving

```bash
# Check vote activity
grep "vote" cluster/node*/ledger/validator.log

# Check Tower BFT state
curl http://127.0.0.1:8899/api/v1/tower_state
```

## References

- [Solana Cluster Testing](https://docs.solana.com/cluster/overview)
- [Network Partition Testing](https://jepsen.io/)
- [Distributed Systems Testing](https://github.com/aphyr/jepsen)
