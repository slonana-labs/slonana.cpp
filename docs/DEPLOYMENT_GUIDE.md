# Slonana Validator Deployment Guide

Complete guide for deploying Slonana validators in production with ML inference capabilities.

## Table of Contents
1. [System Requirements](#system-requirements)
2. [Installation](#installation)
3. [Configuration](#configuration)
4. [Security Hardening](#security-hardening)
5. [Monitoring](#monitoring)
6. [Backup and Recovery](#backup-and-recovery)
7. [Troubleshooting](#troubleshooting)

## System Requirements

### Hardware Requirements

**Minimum (Development):**
- CPU: 8 cores (Intel Xeon or AMD EPYC)
- RAM: 32GB
- Storage: 500GB NVMe SSD
- Network: 1Gbps

**Recommended (Production):**
- CPU: 16+ cores (Intel Xeon Platinum or AMD EPYC 7xx3)
- RAM: 128GB+ ECC memory
- Storage: 2TB+ NVMe SSD (RAID 1 recommended)
- Network: 10Gbps+ with low latency (<5ms to peers)

**Optimal (High-Performance):**
- CPU: 32+ cores with AVX-512 support
- RAM: 256GB+ ECC memory
- Storage: 4TB+ NVMe SSD RAID 1
- Network: 25Gbps+ dedicated link

### Software Requirements

- **Operating System:** Ubuntu 22.04 LTS or later
- **Kernel:** Linux 5.15+ (for eBPF support)
- **Compiler:** GCC 11+ or Clang 14+
- **CMake:** 3.20+
- **Dependencies:** See installation section

## Installation

### Step 1: System Setup

```bash
# Update system
sudo apt-get update && sudo apt-get upgrade -y

# Install build dependencies
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    clang \
    llvm \
    libssl-dev \
    pkg-config \
    libclang-dev \
    protobuf-compiler \
    libprotobuf-dev

# Install LLVM/Clang for eBPF
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 14
```

### Step 2: Clone and Build

```bash
# Clone repository
git clone https://github.com/slonana-labs/slonana.cpp.git
cd slonana.cpp

# Build in release mode
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Install to system
sudo make install

# Verify installation
slonana_validator --version
```

### Step 3: Create System User

```bash
# Create dedicated user
sudo useradd -r -s /bin/bash -m -d /opt/slonana slonana

# Create directories
sudo mkdir -p /opt/slonana/{keys,ledger,config,logs}
sudo chown -R slonana:slonana /opt/slonana
```

## Configuration

### Generate Identity

```bash
# Switch to slonana user
sudo su - slonana

# Generate validator identity keypair
slonana_keygen new -o /opt/slonana/keys/validator-keypair.json

# Generate vote account keypair
slonana_keygen new -o /opt/slonana/keys/vote-keypair.json

# Show public keys
slonana_keygen pubkey /opt/slonana/keys/validator-keypair.json
slonana_keygen pubkey /opt/slonana/keys/vote-keypair.json
```

### Configuration File

Create `/opt/slonana/config/validator.toml`:

```toml
[validator]
# Identity and ledger
identity_keypair = "/opt/slonana/keys/validator-keypair.json"
vote_keypair = "/opt/slonana/keys/vote-keypair.json"
ledger_path = "/opt/slonana/ledger"

# Network endpoints
entrypoint = "mainnet.slonana.io:8001"
expected_shred_version = 1

[rpc]
# RPC server configuration
bind_address = "0.0.0.0:8899"
enable = true
max_connections = 1000
rate_limit_rps = 100

[gossip]
# Gossip configuration
bind_address = "0.0.0.0:8001"
advertise_address = "YOUR_PUBLIC_IP:8001"

[consensus]
# Consensus parameters
enable_voting = true
snapshot_interval_slots = 1000
maximum_snapshots_to_retain = 5

[banking]
# Transaction processing
max_tx_per_batch = 1000
tx_queue_size = 10000

[ml_inference]
# ML inference settings
enable = true
max_agents = 10000
inference_threads = 8
max_model_size_bytes = 10485760  # 10MB

[logging]
# Logging configuration
level = "info"
file = "/opt/slonana/logs/validator.log"
max_size_mb = 100
max_backups = 10
```

### Systemd Service

Create `/etc/systemd/system/slonana-validator.service`:

```ini
[Unit]
Description=Slonana Validator
After=network.target
StartLimitIntervalSec=0

[Service]
Type=simple
User=slonana
Group=slonana
WorkingDirectory=/opt/slonana
ExecStart=/usr/local/bin/slonana_validator --config /opt/slonana/config/validator.toml
Restart=always
RestartSec=10
LimitNOFILE=1000000
LimitNPROC=1000000

# Performance tuning
CPUSchedulingPolicy=fifo
CPUSchedulingPriority=99
IOSchedulingClass=realtime
IOSchedulingPriority=0

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable slonana-validator
sudo systemctl start slonana-validator
sudo systemctl status slonana-validator
```

## Security Hardening

### Firewall Configuration

```bash
# Allow SSH
sudo ufw allow 22/tcp

# Allow RPC (restrict to trusted IPs in production)
sudo ufw allow 8899/tcp

# Allow Gossip
sudo ufw allow 8001/tcp
sudo ufw allow 8001/udp

# Allow QUIC
sudo ufw allow 8003/udp

# Enable firewall
sudo ufw enable
```

### SSH Hardening

Edit `/etc/ssh/sshd_config`:

```
# Disable root login
PermitRootLogin no

# Use SSH keys only
PasswordAuthentication no
PubkeyAuthentication yes

# Limit users
AllowUsers slonana youradmin

# Use strong ciphers
Ciphers chacha20-poly1305@openssh.com,aes256-gcm@openssh.com
MACs hmac-sha2-512-etm@openssh.com,hmac-sha2-256-etm@openssh.com
```

Restart SSH:

```bash
sudo systemctl restart sshd
```

### AppArmor Profile

Create `/etc/apparmor.d/opt.slonana.validator`:

```
#include <tunables/global>

/usr/local/bin/slonana_validator {
  #include <abstractions/base>
  #include <abstractions/nameservice>

  capability net_bind_service,
  capability sys_resource,
  capability ipc_lock,

  /opt/slonana/** rw,
  /opt/slonana/keys/** r,
  /proc/sys/kernel/random/uuid r,
  
  network inet stream,
  network inet dgram,
  network inet6 stream,
  network inet6 dgram,
}
```

Load profile:

```bash
sudo apparmor_parser -r /etc/apparmor.d/opt.slonana.validator
```

### Key Management

```bash
# Secure key permissions
chmod 400 /opt/slonana/keys/*.json
chown slonana:slonana /opt/slonana/keys/*.json

# Encrypt keys at rest (optional)
sudo apt-get install -y ecryptfs-utils
sudo mount -t ecryptfs /opt/slonana/keys /opt/slonana/keys
```

## Monitoring

### Prometheus Metrics

The validator exposes Prometheus metrics on port 9090. Configure Prometheus:

```yaml
# /etc/prometheus/prometheus.yml
scrape_configs:
  - job_name: 'slonana_validator'
    static_configs:
      - targets: ['localhost:9090']
    scrape_interval: 15s
```

### Grafana Dashboards

Install Grafana and import dashboards for:

**Validator Metrics:**
- Node uptime and health
- Block height and slot
- Transaction throughput
- Vote success rate
- Skip rate

**ML Inference Metrics:**
- Inference count
- Inference latency (P50, P95, P99)
- Active agents
- Model types distribution

**System Metrics:**
- CPU usage and load
- Memory usage
- Disk I/O and usage
- Network bandwidth

### Log Monitoring

```bash
# View live logs
sudo journalctl -u slonana-validator -f

# Search for errors
sudo journalctl -u slonana-validator | grep ERROR

# Check last 100 lines
sudo journalctl -u slonana-validator -n 100
```

### Alerting

Configure alerts in Prometheus:

```yaml
# /etc/prometheus/alert.rules.yml
groups:
  - name: validator_alerts
    rules:
      - alert: ValidatorDown
        expr: up{job="slonana_validator"} == 0
        for: 5m
        labels:
          severity: critical
        annotations:
          summary: "Validator is down"
          
      - alert: HighSkipRate
        expr: skip_rate > 0.1
        for: 10m
        labels:
          severity: warning
        annotations:
          summary: "Skip rate above 10%"
          
      - alert: HighMLInferenceLatency
        expr: ml_inference_latency_p99 > 10000000  # 10ms
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "ML inference P99 latency above 10ms"
```

## Backup and Recovery

### Automated Backups

Create `/opt/slonana/scripts/backup.sh`:

```bash
#!/bin/bash
set -e

BACKUP_DIR="/mnt/backup/slonana"
DATE=$(date +%Y%m%d_%H%M%S)

# Create backup directory
mkdir -p $BACKUP_DIR

# Backup keypairs
tar -czf $BACKUP_DIR/keys_$DATE.tar.gz -C /opt/slonana keys/

# Backup configuration
cp /opt/slonana/config/validator.toml $BACKUP_DIR/validator_$DATE.toml

# Backup recent ledger snapshot
cp /opt/slonana/ledger/snapshot-*.tar.zst $BACKUP_DIR/ 2>/dev/null || true

# Delete old backups (keep last 7 days)
find $BACKUP_DIR -mtime +7 -delete

echo "Backup completed: $DATE"
```

Add to crontab:

```bash
# Daily backup at 2 AM
0 2 * * * /opt/slonana/scripts/backup.sh
```

### Disaster Recovery

**Recovery Steps:**

1. **Install fresh system** following installation steps

2. **Restore keypairs:**
```bash
tar -xzf keys_backup.tar.gz -C /opt/slonana/
chmod 400 /opt/slonana/keys/*.json
```

3. **Restore configuration:**
```bash
cp validator_backup.toml /opt/slonana/config/validator.toml
```

4. **Bootstrap from snapshot:**
```bash
# Download latest snapshot
wget https://snapshots.slonana.io/latest.tar.zst

# Extract to ledger
tar -xf latest.tar.zst -C /opt/slonana/ledger/
```

5. **Start validator:**
```bash
sudo systemctl start slonana-validator
```

## Troubleshooting

### Validator Won't Start

```bash
# Check logs
sudo journalctl -u slonana-validator -n 100

# Check config file
slonana_validator --config /opt/slonana/config/validator.toml --validate-config

# Check file permissions
ls -la /opt/slonana/keys/
ls -la /opt/slonana/ledger/
```

### Low Transaction Throughput

```bash
# Check system resources
top
iostat -x 1
iftop

# Check network latency to peers
./scripts/check_peer_latency.sh

# Verify banking configuration
grep "banking" /opt/slonana/config/validator.toml
```

### High Skip Rate

Possible causes:
- Network latency to leader
- Insufficient CPU resources
- Disk I/O bottleneck

```bash
# Check CPU usage
mpstat -P ALL 1

# Check disk latency
iostat -x 1

# Check network
ping -c 100 mainnet.slonana.io
```

### ML Inference Errors

```bash
# Check ML inference logs
grep "ml_inference" /opt/slonana/logs/validator.log

# Verify sBPF programs
slonana program list

# Check inference metrics
curl http://localhost:9090/metrics | grep ml_inference
```

### Memory Issues

```bash
# Check memory usage
free -h

# Check for memory leaks
valgrind --leak-check=full ./slonana_validator

# Increase swap (temporary solution)
sudo fallocate -l 32G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
```

## Performance Tuning

### Kernel Parameters

Add to `/etc/sysctl.conf`:

```
# Network tuning
net.core.rmem_max = 134217728
net.core.wmem_max = 134217728
net.ipv4.tcp_rmem = 4096 87380 67108864
net.ipv4.tcp_wmem = 4096 65536 67108864
net.core.netdev_max_backlog = 5000
net.ipv4.tcp_congestion_control = bbr

# File descriptors
fs.file-max = 2097152

# VM tuning
vm.swappiness = 10
vm.dirty_ratio = 15
vm.dirty_background_ratio = 5
```

Apply:

```bash
sudo sysctl -p
```

### CPU Governor

```bash
# Set to performance mode
sudo cpupower frequency-set -g performance

# Verify
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
```

## Maintenance

### Updates

```bash
# Stop validator
sudo systemctl stop slonana-validator

# Backup
/opt/slonana/scripts/backup.sh

# Update code
cd /path/to/slonana.cpp
git pull origin main
cd build
make -j$(nproc)
sudo make install

# Restart validator
sudo systemctl start slonana-validator
```

### Cleanup

```bash
# Remove old logs
find /opt/slonana/logs -name "*.log.*" -mtime +30 -delete

# Clean old snapshots
find /opt/slonana/ledger -name "snapshot-*" -mtime +7 -delete
```

## Support

- **Documentation:** https://docs.slonana.io
- **Discord:** https://discord.gg/slonana
- **GitHub Issues:** https://github.com/slonana-labs/slonana.cpp/issues

## References

- [Linux Performance Tuning](https://www.kernel.org/doc/Documentation/sysctl/)
- [Security Best Practices](https://www.nsa.gov/Cybersecurity/)
- [Monitoring with Prometheus](https://prometheus.io/docs/)
