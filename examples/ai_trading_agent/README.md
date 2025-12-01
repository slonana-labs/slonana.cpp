# AI Trading Agent Example

This example demonstrates how to create a C++ program that uses slonana's ML inference syscalls, compiles into sBPF bytecode, deploys to a slonana validator, and executes via SVM transactions.

## Overview

The AI Trading Agent is an autonomous on-chain program that:
1. Reads market data from oracle accounts
2. Runs ML inference to predict trading signals
3. Executes trades based on predictions
4. Self-schedules periodic execution using async timers

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                       slonana Validator                              │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                    SVM Execution Engine                      │    │
│  │  ┌─────────────┐   ┌──────────────┐   ┌────────────────┐   │    │
│  │  │ sBPF Loader │   │ ML Inference │   │ Async Execution│   │    │
│  │  │             │   │   Syscalls   │   │    Timers      │   │    │
│  │  └──────┬──────┘   └──────┬───────┘   └───────┬────────┘   │    │
│  │         │                 │                   │             │    │
│  │         ▼                 ▼                   ▼             │    │
│  │  ┌────────────────────────────────────────────────────────┐    │    │
│  │  │           AI Trading Agent Program                  │    │    │
│  │  │  • Decision Tree Model (9.7ns inference)           │    │    │
│  │  │  • Feature Extraction from Oracle Data             │    │    │
│  │  │  • Trade Execution Logic                           │    │    │
│  │  │  • Periodic Timer (auto-execution every slot)      │    │    │
│  │  └────────────────────────────────────────────────────┘    │    │
│  └─────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────┘
```

## Files

- `ai_trading_agent.cpp` - Main program source code
- `Makefile` - Build instructions for sBPF compilation
- `deploy.sh` - Deployment script
- `test_client.cpp` - Client to send transactions to the agent

## Prerequisites

1. slonana validator built with ML inference support:
   ```bash
   cd /path/to/slonana.cpp
   mkdir build && cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   make -j4
   ```

2. LLVM/Clang with BPF target support (for sBPF compilation)

## Building

```bash
cd examples/ai_trading_agent
make
```

## Deploying

```bash
# Start local validator
./slonana-validator --dev

# Deploy program
./deploy.sh
```

## Running

```bash
# Send initialization transaction
./test_client init

# Send manual trigger
./test_client trigger

# Check agent status
./test_client status
```

## Program Structure

### Instruction Types

| ID | Instruction | Description |
|----|-------------|-------------|
| 0  | Initialize  | Deploy ML model, create accounts |
| 1  | UpdateModel | Update ML model weights |
| 2  | Trigger     | Manual trigger for inference |
| 3  | Configure   | Update agent parameters |
| 4  | Pause       | Pause autonomous execution |
| 5  | Resume      | Resume autonomous execution |

### Account Layout

```
Account 0: Program State (PDA)
  - is_initialized: bool
  - model_version: u32
  - last_trade_slot: u64
  - position: i8 (-1=short, 0=neutral, 1=long)
  - accumulated_pnl: i64
  - total_trades: u64

Account 1: ML Model Data
  - Decision tree nodes
  - Feature normalization params

Account 2: Oracle Data
  - Price feed
  - Volume data

Account 3: Trade Escrow
  - Funds for trading
```

## Performance

- **Inference Latency**: 9.7ns per decision
- **Feature Extraction**: ~50ns
- **Total Transaction Time**: <1ms
- **Autonomous Updates**: Every slot (~400ms)

## Compute Budget

| Operation | Compute Units |
|-----------|---------------|
| Feature extraction | 100 CU |
| ML inference | 50 CU |
| Position update | 200 CU |
| Trade execution | 500 CU |
| **Total per trigger** | **~850 CU** |

## Safety Features

- Bounded loop depth for verifier safety
- Fixed-point arithmetic (no floats)
- Null pointer validation on all syscalls
- Rate limiting on trade frequency
- Maximum position limits

## License

MIT License - See LICENSE file
