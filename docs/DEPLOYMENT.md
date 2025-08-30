# Slonana.cpp Production Deployment Guide

## Table of Contents
- [Quick Start](#quick-start)
- [Universal Installation](#universal-installation)
- [Installation Methods](#installation-methods)
- [Docker Deployment](#docker-deployment)
- [Production Deployment](#production-deployment)
- [Configuration](#configuration)
- [Monitoring and Maintenance](#monitoring-and-maintenance)
- [Troubleshooting](#troubleshooting)
- [Security Best Practices](#security-best-practices)

## Quick Start

### System Requirements

**Minimum Requirements:**
- CPU: 4 cores, 2.4 GHz
- RAM: 8 GB
- Storage: 100 GB SSD
- Network: 100 Mbps bandwidth

**Recommended Requirements (Production):**
- CPU: 16+ cores, 3.0 GHz with AVX2 support
- RAM: 32+ GB  
- Storage: 2+ TB NVMe SSD (RAID recommended)
- Network: 1 Gbps bandwidth with low latency

**Supported Operating Systems:**
- Ubuntu 18.04+ LTS, 20.04+ LTS, 22.04+ LTS
- Debian 10+, 11+, 12+
- CentOS 7+, 8+, Rocky Linux 8+, 9+
- RHEL 7+, 8+, 9+, AlmaLinux 8+, 9+
- Fedora 35+, 36+, 37+, 38+
- macOS 11+ (Big Sur), 12+ (Monterey), 13+ (Ventura), 14+ (Sonoma)
- Windows 10+, Windows 11 (via WSL2)
- Alpine Linux 3.15+, 3.16+, 3.17+
- Arch Linux (rolling)

## Universal Installation

### One-Line Installation (Recommended)

The universal installer automatically detects your OS and handles all dependencies:

```bash
# Install on any supported system
curl -sSL https://install.slonana.com | bash

# Or download and inspect first
wget https://raw.githubusercontent.com/slonana-labs/slonana.cpp/main/install.sh
chmod +x install.sh && ./install.sh
```

**What the Universal Installer Does:**
- ✅ Detects your operating system and architecture
- ✅ Installs build tools and dependencies (cmake, gcc/clang, openssl, etc.)
- ✅ Downloads pre-built binaries or builds from source as needed
- ✅ Creates configuration files with sensible defaults
- ✅ Sets up systemd services (Linux) for automatic startup
- ✅ Verifies installation with health checks
- ✅ Provides usage instructions and next steps

**Supported Platforms:**
- Linux: x86_64, ARM64, ARMv7
- macOS: Intel x86_64, Apple Silicon ARM64
- Windows: x86_64 (via WSL2)
- CentOS/RHEL 8+
- macOS 12+
- Windows 10/Server 2019+

### 30-Second Start

```bash
# Download and run (Linux/macOS)
curl -sSL https://github.com/slonana-labs/slonana.cpp/releases/latest/download/install.sh | bash
slonana-validator --ledger-path ./validator-ledger

# Docker one-liner
docker run -p 8899:8899 -v $(pwd)/ledger:/opt/slonana/data slonana/validator:latest
```

## Installation Methods

### Package Managers (Recommended)

#### macOS - Homebrew
```bash
# Add Slonana tap
brew tap slonana-labs/tap https://github.com/slonana-labs/homebrew-tap

# Install validator
brew install slonana-validator

# Start validator
slonana-validator --help
```

#### Ubuntu/Debian - APT
```bash
# Add repository
curl -fsSL https://packages.slonana.org/gpg | sudo gpg --dearmor -o /usr/share/keyrings/slonana.gpg
echo "deb [signed-by=/usr/share/keyrings/slonana.gpg] https://packages.slonana.org/apt stable main" | sudo tee /etc/apt/sources.list.d/slonana.list

# Install
sudo apt update
sudo apt install slonana-validator

# Start service
sudo systemctl enable slonana-validator
sudo systemctl start slonana-validator
```

#### CentOS/RHEL/Fedora - RPM
```bash
# Add repository
sudo tee /etc/yum.repos.d/slonana.repo << 'EOF'
[slonana]
name=Slonana Repository
baseurl=https://packages.slonana.org/rpm/
enabled=1
gpgcheck=1
gpgkey=https://packages.slonana.org/gpg
EOF

# Install
sudo dnf install slonana-validator  # Fedora/CentOS 8+
# or
sudo yum install slonana-validator  # CentOS 7

# Start service
sudo systemctl enable slonana-validator
sudo systemctl start slonana-validator
```

#### Windows - Chocolatey
```powershell
# Install Chocolatey if not already installed
Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

# Install validator
choco install slonana-validator

# Start validator
slonana-validator --help
```

### Binary Installation

#### Linux
```bash
# Download latest release
VERSION=$(curl -s https://api.github.com/repos/slonana-labs/slonana.cpp/releases/latest | grep tag_name | cut -d '"' -f 4)
wget "https://github.com/slonana-labs/slonana.cpp/releases/download/${VERSION}/slonana-validator-linux-x64.tar.gz"

# Extract and install
tar -xzf slonana-validator-linux-x64.tar.gz
sudo mv slonana_validator /usr/local/bin/slonana-validator
sudo chmod +x /usr/local/bin/slonana-validator

# Verify installation
slonana-validator --version
```

#### macOS
```bash
# Download and install
VERSION=$(curl -s https://api.github.com/repos/slonana-labs/slonana.cpp/releases/latest | grep tag_name | cut -d '"' -f 4)
curl -L "https://github.com/slonana-labs/slonana.cpp/releases/download/${VERSION}/slonana-validator-macos-x64.tar.gz" | tar -xz
sudo mv slonana_validator /usr/local/bin/slonana-validator

# Verify installation  
slonana-validator --version
```

#### Windows
```powershell
# Download latest release
$version = (Invoke-RestMethod https://api.github.com/repos/slonana-labs/slonana.cpp/releases/latest).tag_name
$url = "https://github.com/slonana-labs/slonana.cpp/releases/download/$version/slonana-validator-windows-x64.zip"
Invoke-WebRequest -Uri $url -OutFile "slonana-validator.zip"

# Extract
Expand-Archive -Path "slonana-validator.zip" -DestinationPath "C:\Program Files\Slonana"

# Add to PATH (requires admin)
$env:PATH += ";C:\Program Files\Slonana"
[Environment]::SetEnvironmentVariable("PATH", $env:PATH, [EnvironmentVariableTarget]::Machine)

# Verify installation
slonana-validator --version
```

### Build from Source

```bash
# Clone repository
git clone https://github.com/slonana-labs/slonana.cpp.git
cd slonana.cpp

# Install dependencies (Ubuntu/Debian)
sudo apt install build-essential cmake libssl-dev

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Install
sudo make install
```

## Docker Deployment

### Single Node

```bash
# Pull latest image
docker pull slonana/validator:latest

# Run with persistent data
docker run -d \
  --name slonana-validator \
  -p 8899:8899 \
  -p 8001:8001 \
  -v $(pwd)/validator-data:/opt/slonana/data \
  -v $(pwd)/validator-config:/opt/slonana/config \
  slonana/validator:latest
```

### Docker Compose - Single Node

```yaml
# docker-compose.yml
version: '3.8'

services:
  validator:
    image: slonana/validator:latest
    container_name: slonana-validator
    ports:
      - "8899:8899"  # RPC
      - "8001:8001"  # Gossip
    volumes:
      - ./data:/opt/slonana/data
      - ./config:/opt/slonana/config:ro
    environment:
      - SLONANA_LOG_LEVEL=info
      - SLONANA_RPC_BIND_ADDRESS=0.0.0.0:8899
    restart: unless-stopped
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:8899/health"]
      interval: 30s
      timeout: 10s
      retries: 3
```

```bash
# Start
docker-compose up -d

# View logs
docker-compose logs -f

# Stop
docker-compose down
```

### Multi-Node Cluster

```bash
# Use cluster profile
docker-compose --profile cluster up -d

# This starts 3 validator nodes:
# - Node 1: RPC on 18899, Gossip on 18001
# - Node 2: RPC on 28899, Gossip on 28001  
# - Node 3: RPC on 38899, Gossip on 38001
```

## Production Deployment

### Systemd Service (Linux)

```bash
# Create service file
sudo tee /etc/systemd/system/slonana-validator.service << 'EOF'
[Unit]
Description=Slonana C++ Validator
After=network.target
Wants=network-online.target

[Service]
Type=simple
User=slonana
Group=slonana
ExecStart=/usr/local/bin/slonana-validator \
  --ledger-path /var/lib/slonana/ledger \
  --identity /etc/slonana/validator-keypair.json \
  --rpc-bind-address 0.0.0.0:8899 \
  --gossip-bind-address 0.0.0.0:8001

# Security settings
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/lib/slonana
ProtectKernelTunables=true
ProtectKernelModules=true
ProtectControlGroups=true

# Resource limits
LimitNOFILE=65536
LimitNPROC=4096

# Restart settings
Restart=always
RestartSec=10
TimeoutStartSec=300
TimeoutStopSec=30

# Logging
StandardOutput=journal
StandardError=journal
SyslogIdentifier=slonana-validator

[Install]
WantedBy=multi-user.target
EOF

# Create user and directories
sudo useradd -r -s /bin/false slonana
sudo mkdir -p /var/lib/slonana /etc/slonana
sudo chown slonana:slonana /var/lib/slonana

# Generate identity keypair
sudo -u slonana slonana-validator generate-keypair /etc/slonana/validator-keypair.json

# Enable and start service
sudo systemctl daemon-reload
sudo systemctl enable slonana-validator
sudo systemctl start slonana-validator

# Check status
sudo systemctl status slonana-validator
```

### Kubernetes Deployment

```yaml
# slonana-validator.yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: slonana-validator
  namespace: slonana
spec:
  replicas: 1
  selector:
    matchLabels:
      app: slonana-validator
  template:
    metadata:
      labels:
        app: slonana-validator
    spec:
      securityContext:
        runAsUser: 1000
        runAsGroup: 1000
        fsGroup: 1000
      containers:
      - name: validator
        image: slonana/validator:latest
        ports:
        - containerPort: 8899
          name: rpc
        - containerPort: 8001
          name: gossip
        env:
        - name: SLONANA_LOG_LEVEL
          value: "info"
        - name: SLONANA_RPC_BIND_ADDRESS
          value: "0.0.0.0:8899"
        - name: SLONANA_GOSSIP_BIND_ADDRESS
          value: "0.0.0.0:8001"
        volumeMounts:
        - name: ledger-data
          mountPath: /opt/slonana/data
        - name: config
          mountPath: /opt/slonana/config
          readOnly: true
        resources:
          requests:
            cpu: 2000m
            memory: 8Gi
          limits:
            cpu: 8000m
            memory: 32Gi
        livenessProbe:
          httpGet:
            path: /health
            port: 8899
          initialDelaySeconds: 60
          periodSeconds: 30
        readinessProbe:
          httpGet:
            path: /health
            port: 8899
          initialDelaySeconds: 30
          periodSeconds: 10
      volumes:
      - name: ledger-data
        persistentVolumeClaim:
          claimName: slonana-ledger-pvc
      - name: config
        configMap:
          name: slonana-config

---
apiVersion: v1
kind: Service
metadata:
  name: slonana-validator-service
  namespace: slonana
spec:
  selector:
    app: slonana-validator
  ports:
  - name: rpc
    port: 8899
    targetPort: 8899
  - name: gossip
    port: 8001
    targetPort: 8001
  type: LoadBalancer

---
apiVersion: v1
kind: PersistentVolumeClaim
metadata:
  name: slonana-ledger-pvc
  namespace: slonana
spec:
  accessModes:
    - ReadWriteOnce
  resources:
    requests:
      storage: 1Ti
  storageClassName: fast-ssd
```

```bash
# Deploy to Kubernetes
kubectl create namespace slonana
kubectl apply -f slonana-validator.yaml

# Check status
kubectl get pods -n slonana
kubectl logs -f deployment/slonana-validator -n slonana
```

## Configuration

### Environment Variables

```bash
# Core settings
export SLONANA_LEDGER_PATH=/var/lib/slonana/ledger
export SLONANA_IDENTITY_KEYPAIR=/etc/slonana/validator-keypair.json
export SLONANA_RPC_BIND_ADDRESS=0.0.0.0:8899
export SLONANA_GOSSIP_BIND_ADDRESS=0.0.0.0:8001

# Network settings
export SLONANA_BOOTSTRAP_PEERS=node1.cluster.slonana.org:8001,node2.cluster.slonana.org:8001
export SLONANA_CLUSTER_TYPE=mainnet  # mainnet, testnet, devnet

# Performance tuning
export SLONANA_LOG_LEVEL=info
export SLONANA_MAX_CONNECTIONS=1000
export SLONANA_COMPUTE_UNITS_LIMIT=1400000

# Security
export SLONANA_RPC_ALLOWED_ORIGINS=https://app.slonana.org,https://explorer.slonana.org
export SLONANA_ENABLE_RPC_AUTHENTICATION=true
```

### Configuration File

```toml
# /etc/slonana/validator.toml
[network]
rpc_bind_address = "0.0.0.0:8899"
gossip_bind_address = "0.0.0.0:8001"
bootstrap_peers = [
    "node1.cluster.slonana.org:8001",
    "node2.cluster.slonana.org:8001",
    "node3.cluster.slonana.org:8001"
]
max_connections = 1000

[consensus]
vote_threshold = 0.67
confirmation_blocks = 32
max_lockout_slots = 8192

[storage]
ledger_path = "/var/lib/slonana/ledger"
accounts_path = "/var/lib/slonana/accounts"
snapshot_interval_slots = 10000
compression_enabled = true

[rpc]
enable_rpc = true
allowed_origins = ["*"]
rate_limit_requests_per_second = 100
max_request_size = 1048576  # 1MB

[logging]
level = "info"
target = "file"
file_path = "/var/log/slonana/validator.log"
max_file_size = "100MB"
max_files = 10

[performance]
worker_threads = 0  # auto-detect
compute_units_limit = 1400000
account_cache_size = 10000
transaction_pool_size = 10000

[security]
identity_keypair_path = "/etc/slonana/validator-keypair.json"
enable_rpc_authentication = false
rpc_auth_token = ""
```

### Command Line Options

```bash
# Core options
slonana-validator \
  --config /etc/slonana/validator.toml \
  --ledger-path /var/lib/slonana/ledger \
  --identity /etc/slonana/validator-keypair.json \
  --rpc-bind-address 0.0.0.0:8899 \
  --gossip-bind-address 0.0.0.0:8001 \
  --log-level info \
  --enable-rpc \
  --rpc-port 8899 \
  --gossip-port 8001 \
  --known-validator node1.cluster.slonana.org:8001 \
  --known-validator node2.cluster.slonana.org:8001
```

## Monitoring and Maintenance

### Health Checks

```bash
# Basic health check
curl -s http://localhost:8899/health

# RPC health check
curl -X POST http://localhost:8899 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"getHealth"}'

# Detailed status
curl -X POST http://localhost:8899 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"getClusterNodes"}'
```

### Metrics and Monitoring

```bash
# Prometheus metrics endpoint
curl http://localhost:9090/metrics

# Key metrics to monitor:
# - slonana_validator_slot_height
# - slonana_validator_vote_distance
# - slonana_validator_transaction_count
# - slonana_rpc_requests_total
# - slonana_network_peers_connected
```

### Log Management

```bash
# View logs (systemd)
sudo journalctl -u slonana-validator -f

# View logs (Docker)
docker logs -f slonana-validator

# Log rotation configuration
sudo tee /etc/logrotate.d/slonana-validator << 'EOF'
/var/log/slonana/*.log {
    daily
    rotate 30
    compress
    delaycompress
    missingok
    notifempty
    create 0644 slonana slonana
    postrotate
        systemctl reload slonana-validator
    endscript
}
EOF
```

### Performance Monitoring

```bash
# CPU and memory usage
top -p $(pgrep slonana-validator)

# Network connections
ss -tuln | grep :8899
ss -tuln | grep :8001

# Disk I/O
iostat -x 1

# Ledger size
du -sh /var/lib/slonana/ledger
```

### Backup and Recovery

```bash
# Backup ledger data
sudo systemctl stop slonana-validator
sudo tar -czf validator-backup-$(date +%Y%m%d).tar.gz \
  /var/lib/slonana/ledger \
  /etc/slonana/validator-keypair.json \
  /etc/slonana/validator.toml

# Upload to backup storage
aws s3 cp validator-backup-$(date +%Y%m%d).tar.gz s3://slonana-backups/

# Restore from backup
sudo systemctl stop slonana-validator
sudo rm -rf /var/lib/slonana/ledger
sudo tar -xzf validator-backup-20240101.tar.gz -C /
sudo chown -R slonana:slonana /var/lib/slonana
sudo systemctl start slonana-validator
```

## Troubleshooting

### Common Issues

#### Validator Won't Start

```bash
# Check logs
sudo journalctl -u slonana-validator -n 50

# Common causes:
# 1. Port already in use
sudo netstat -tulpn | grep :8899

# 2. Permission issues
sudo chown -R slonana:slonana /var/lib/slonana

# 3. Missing identity keypair
sudo -u slonana slonana-validator generate-keypair /etc/slonana/validator-keypair.json

# 4. Insufficient disk space
df -h /var/lib/slonana
```

#### Network Connectivity Issues

```bash
# Test gossip connectivity
telnet node1.cluster.slonana.org 8001

# Check firewall rules
sudo ufw status
sudo iptables -L

# Test RPC endpoint
curl -X POST http://localhost:8899 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"getVersion"}'
```

#### Performance Issues

```bash
# Check system resources
htop
iotop
nethogs

# Optimize disk I/O
echo deadline | sudo tee /sys/block/sda/queue/scheduler

# Increase file descriptor limits
echo "slonana soft nofile 65536" | sudo tee -a /etc/security/limits.conf
echo "slonana hard nofile 65536" | sudo tee -a /etc/security/limits.conf
```

### Debug Mode

```bash
# Start in debug mode
slonana-validator --log-level debug --config /etc/slonana/validator.toml

# Enable debug logging for specific modules
export RUST_LOG=slonana_validator=debug,slonana_rpc=info
```

### Support and Community

- **GitHub Issues**: https://github.com/slonana-labs/slonana.cpp/issues
- **Documentation**: https://docs.slonana.org
- **Discord**: https://discord.gg/slonana
- **Telegram**: https://t.me/slonana

## Security Best Practices

### Network Security

```bash
# Firewall configuration (ufw)
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow 8899/tcp  # RPC
sudo ufw allow 8001/udp  # Gossip
sudo ufw enable

# Restrict RPC access
# Only allow trusted IPs for RPC access
sudo ufw allow from 10.0.0.0/8 to any port 8899
sudo ufw allow from 172.16.0.0/12 to any port 8899
sudo ufw allow from 192.168.0.0/16 to any port 8899
```

### Key Management

```bash
# Secure key storage
sudo chmod 600 /etc/slonana/validator-keypair.json
sudo chown slonana:slonana /etc/slonana/validator-keypair.json

# Hardware security module (HSM) integration
# Use environment variable for key path if using HSM
export SLONANA_IDENTITY_KEYPAIR=/dev/hsm/validator-key

# Key rotation (when needed)
slonana-validator generate-keypair /etc/slonana/validator-keypair-new.json
# Update configuration and restart validator
```

### System Hardening

```bash
# Disable unnecessary services
sudo systemctl disable apache2 nginx mysql

# Update system regularly
sudo apt update && sudo apt upgrade -y

# Enable automatic security updates
sudo apt install unattended-upgrades
sudo dpkg-reconfigure -plow unattended-upgrades

# Configure SSH security
# Edit /etc/ssh/sshd_config:
# PermitRootLogin no
# PasswordAuthentication no
# AllowUsers slonana-admin
sudo systemctl restart ssh
```

### Monitoring and Alerting

```bash
# Set up fail2ban for SSH protection
sudo apt install fail2ban
sudo systemctl enable fail2ban

# Monitor validator health with alerts
# Example monitoring script
cat > /usr/local/bin/monitor-validator.sh << 'EOF'
#!/bin/bash
if ! curl -s http://localhost:8899/health >/dev/null; then
    echo "Validator health check failed" | mail -s "Validator Alert" admin@slonana.org
fi
EOF

# Add to crontab
echo "*/5 * * * * /usr/local/bin/monitor-validator.sh" | crontab -
```

This comprehensive deployment guide covers all aspects of installing, configuring, and maintaining Slonana.cpp validators in production environments.