# Mainnet Network Launch Guide

This guide provides comprehensive step-by-step instructions for launching your own Slonana mainnet network.

## Prerequisites

- Slonana.cpp validator built and installed
- Minimum 3-5 genesis validators ready
- DNS infrastructure for seed nodes
- Monitoring and infrastructure setup

## Phase 1: Pre-Launch Infrastructure

### Step 1: Genesis Block Creation

Create the mainnet genesis configuration:

```bash
# Create mainnet genesis configuration
./slonana-genesis create-network \
  --network-type mainnet \
  --initial-supply 1000000000 \
  --inflation-rate 0.05 \
  --epoch-length 432000 \
  --output mainnet-genesis.json

# Verify the configuration
./slonana-genesis verify mainnet-genesis.json

# Display genesis information
./slonana-genesis info mainnet-genesis.json
```

### Step 2: Network Parameters Configuration

The mainnet configuration includes:

- **Network ID**: `mainnet`
- **Chain ID**: `101`
- **Magic Bytes**: `4D 41 49 4E` ("MAIN")
- **Total Supply**: 1,000,000,000 tokens
- **Initial Inflation**: 5% annually
- **Epoch Length**: 432,000 slots (~2 days)
- **Min Validator Stake**: 1,000,000 lamports

### Step 3: Bootstrap Infrastructure Setup

Configure DNS seed nodes:

```bash
# DNS seed configuration
mainnet-seed1.slonana.org:8001
mainnet-seed2.slonana.org:8001
mainnet-seed3.slonana.org:8001
```

Hardcoded bootstrap entrypoints are automatically included:
- `mainnet-seed1.slonana.org:8001` (US East)
- `mainnet-seed2.slonana.org:8001` (EU West)
- `mainnet-seed3.slonana.org:8001` (Asia Pacific)
- `seed1.slonana.org:8001` (US West)
- `seed2.slonana.org:8001` (EU Central)

## Phase 2: Validator Setup

### Step 1: Generate Validator Identity

```bash
# Generate mainnet validator identity
mkdir -p ~/.config/slonana/mainnet/
slonana-keygen new --outdir ~/.config/slonana/mainnet/

# Verify the keypair
slonana-keygen verify ~/.config/slonana/mainnet/validator-keypair.json
```

### Step 2: Configure Validator

Use the provided mainnet configuration template:

```bash
# Copy mainnet configuration
cp config/mainnet/validator.toml /etc/slonana/mainnet-validator.toml

# Edit configuration paths
vim /etc/slonana/mainnet-validator.toml
```

Key configuration parameters:
```toml
# Network Configuration
network_id = "mainnet"
expected_genesis_hash = "YOUR_GENESIS_HASH_HERE"

# Validator Identity and Paths
identity_keypair_path = "/etc/slonana/mainnet-keypair.json"
ledger_path = "/var/lib/slonana/mainnet-ledger"

# Network Binding
rpc_bind_address = "0.0.0.0:8899"
gossip_bind_address = "0.0.0.0:8001"

# Security
require_validator_identity = true
enable_tls = true
minimum_validator_stake = 1000000
```

### Step 3: Validator Requirements

**Hardware Requirements:**
- CPU: 16+ cores (recommended: 24+ cores)
- RAM: 32GB+ (recommended: 64GB)
- Storage: 1TB+ NVMe SSD
- Network: 1Gbps+ connection
- OS: Ubuntu 20.04+ or similar Linux distribution

**Staking Requirements:**
- Minimum stake: 1,000,000 lamports (1M tokens)
- Minimum delegation: 1,000 lamports (1K tokens)

## Phase 3: Network Launch Coordination

### Genesis Validator Bootstrap Sequence

**T-24h: Pre-launch Preparation**
```bash
# Deploy all genesis validators
# Each validator should:
slonana-validator \
  --config /etc/slonana/mainnet-validator.toml \
  --ledger-path /var/lib/slonana/mainnet-ledger \
  --identity /etc/slonana/mainnet-keypair.json \
  --expected-genesis-hash YOUR_GENESIS_HASH \
  --wait-for-bootstrap
```

**T-1h: Final Verification**
```bash
# Verify configuration on all validators
./slonana-genesis verify mainnet-genesis.json YOUR_EXPECTED_HASH

# Check network connectivity
./slonana-validator --dry-run --config /etc/slonana/mainnet-validator.toml
```

**T-0: Coordinated Network Start**
```bash
# Start all genesis validators simultaneously
slonana-validator \
  --config /etc/slonana/mainnet-validator.toml \
  --ledger-path /var/lib/slonana/mainnet-ledger \
  --identity /etc/slonana/mainnet-keypair.json \
  --expected-genesis-hash YOUR_GENESIS_HASH
```

**T+1h: Verify Supermajority Consensus**
```bash
# Check validator status
curl http://localhost:8899 -X POST -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"getSlot"}'

# Verify cluster nodes
curl http://localhost:8899 -X POST -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"getClusterNodes"}'
```

**T+24h: Open Network to Additional Validators**

Once the network is stable with genesis validators, open it to additional validators.

## Phase 4: Validator Onboarding

### New Validator Joining Process

1. **Stake Requirement Verification**
```bash
# Verify minimum stake (1M lamports)
# Check account has sufficient balance
```

2. **Validator Setup**
```bash
# Generate validator identity
slonana-keygen new --outdir ~/.config/slonana/mainnet/

# Configure validator
cp config/mainnet/validator.toml ~/.config/slonana/validator.toml
# Edit paths and settings as needed
```

3. **Hardware Verification**
```bash
# Verify system requirements
./slonana-validator-requirements-check \
  --hardware \
  --network \
  --stake-account YOUR_STAKE_ACCOUNT
```

4. **Network Connection**
```bash
# Connect to mainnet
slonana-validator \
  --config ~/.config/slonana/validator.toml \
  --ledger-path ~/slonana-ledger \
  --identity ~/.config/slonana/mainnet/validator-keypair.json \
  --entrypoint mainnet-seed1.slonana.org:8001 \
  --expected-genesis-hash YOUR_GENESIS_HASH
```

5. **Stake Activation**
Wait 1-2 epochs for stake activation before beginning voting and block production.

## Phase 5: Monitoring and Operations

### Network Health Monitoring

**Key Metrics to Monitor:**
- Total validators: Target 150+
- Geographic distribution: 5+ continents
- Consensus participation: >90%
- Network throughput: 50,000+ TPS
- Finality time: <1 second
- Slot progression: Continuous

**Monitoring Commands:**
```bash
# Network status
curl http://localhost:8899 -X POST -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"getHealth"}'

# Validator performance
curl http://localhost:8899 -X POST -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"getVoteAccounts"}'

# Economic parameters
curl http://localhost:8899 -X POST -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"getInflationRate"}'
```

### Security and Incident Response

**Slashing Conditions:**
- Vote timeout: 5% stake slash for extended downtime
- Equivocation: 100% stake slash for double voting

**Emergency Procedures:**
- Network halt coordination through validator communication
- Incident response team activation
- Rollback procedures if needed

## Success Criteria

The mainnet launch is considered successful when:

- [x] Genesis block created and validated
- [x] 3+ genesis validators successfully bootstrapped
- [x] Supermajority consensus achieved (>66% stake)
- [x] Network producing blocks consistently
- [x] RPC endpoints responding correctly
- [x] Stake delegation and rewards working
- [x] 24+ hours of stable operation
- [x] Additional validators successfully joining

## Network Parameters Summary

| Parameter | Value |
|-----------|--------|
| Network ID | mainnet |
| Chain ID | 101 |
| Total Supply | 1,000,000,000 tokens |
| Initial Inflation | 5% annually |
| Epoch Length | 432,000 slots (~2 days) |
| Slot Duration | ~400ms |
| Min Validator Stake | 1,000,000 lamports |
| Min Delegation | 1,000 lamports |
| Vote Timeout Slash | 5% |
| Equivocation Slash | 100% |

## Support and Resources

- **Documentation**: `/docs/` directory
- **Configuration Examples**: `/config/mainnet/` directory  
- **Genesis Tool**: `./slonana-genesis --help`
- **Validator Manual**: `/docs/USER_MANUAL.md`
- **Architecture Guide**: `/docs/ARCHITECTURE.md`

For additional support during mainnet launch, contact the Slonana Labs development team.