# ML Trading Agent Example

This example demonstrates how to create a C++ program that compiles into an sBPF (Solana BPF) program, deploy it to the slonana validator, and invoke it via SVM transactions.

## Overview

The example consists of:

1. **Trading Agent sBPF Program** (`ml_trading_agent.cpp`) - The on-chain program that runs ML inference
2. **Client Application** (`deploy_and_invoke.cpp`) - Deploys the program and sends transactions
3. **Model Training Script** (`train_model.py`) - Trains and exports the ML model

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     Client Application                          │
│  deploy_and_invoke.cpp                                          │
│  - Deploys sBPF program to validator                           │
│  - Creates and sends transactions                               │
│  - Reads results from on-chain state                           │
└─────────────────────────────────────┬───────────────────────────┘
                                      │ Transactions
                                      ▼
┌─────────────────────────────────────────────────────────────────┐
│                   slonana Validator (SVM)                       │
│  - Receives transactions                                        │
│  - Executes sBPF programs                                       │
│  - Persists account state                                       │
└─────────────────────────────────────┬───────────────────────────┘
                                      │ Execute
                                      ▼
┌─────────────────────────────────────────────────────────────────┐
│              ML Trading Agent sBPF Program                      │
│  ml_trading_agent.cpp → .so                                     │
│  - Reads oracle data from accounts                              │
│  - Runs ML inference (decision tree)                            │
│  - Writes trading signal to state account                       │
└─────────────────────────────────────────────────────────────────┘
```

## Building the Example

### Prerequisites

- CMake 3.16+
- Clang with BPF target support
- slonana validator built with ML inference

### Build Steps

```bash
# From slonana.cpp root
cd examples/ml_trading_agent
mkdir build && cd build
cmake ..
make
```

## Running the Example

### 1. Start the Validator

```bash
cd /path/to/slonana.cpp/build
./slonana-validator --dev
```

### 2. Deploy and Run the Agent

```bash
cd examples/ml_trading_agent/build
./deploy_and_invoke
```

Expected output:
```
=== ML Trading Agent Deployment and Invocation ===
1. Creating accounts...
   ✓ Program account created
   ✓ State account created
   ✓ Oracle account created (mock)

2. Deploying sBPF program...
   ✓ Program deployed successfully

3. Initializing agent state...
   ✓ Agent initialized

4. Simulating market updates...
   Slot 1: Oracle updated, Signal: BUY
   Slot 2: Oracle updated, Signal: HOLD
   Slot 3: Oracle updated, Signal: SELL
   ...

5. Performance metrics:
   Total inferences: 100
   Average latency: 15.3 ns
   Signals: BUY=42, HOLD=35, SELL=23
```

## Files

- `ml_trading_agent.cpp` - The sBPF program source
- `deploy_and_invoke.cpp` - Client that deploys and invokes the program
- `train_model.py` - Python script to train and export ML model
- `CMakeLists.txt` - Build configuration

## How It Works

### 1. sBPF Program Entry Point

The sBPF program has a standard entry point:

```cpp
extern "C" uint64_t entrypoint(const uint8_t* input) {
    // Parse accounts from input
    // Run business logic (ML inference)
    // Return success/error code
}
```

### 2. ML Inference Syscalls

The program uses these syscalls for ML inference:

```cpp
// Decision tree inference (9.7ns latency)
sol_ml_decision_tree(features, feature_count, nodes, node_count, max_depth, &result);

// Neural network forward pass (626ns for 32x32)
sol_ml_forward(input, input_len, model_data, model_len, output, &output_len);
```

### 3. Transaction Invocation

Client sends transactions with instruction data:

```cpp
Instruction instr;
instr.program_id = program_pubkey;
instr.accounts = {state_account, oracle_account};
instr.data = instruction_data;

ExecutionOutcome result = engine.execute_transaction({instr}, accounts);
```

## Customization

### Using Your Own Model

1. Train your model in Python:
   ```bash
   python train_model.py --input data.csv --output model.bin
   ```

2. Update the model in `ml_trading_agent.cpp`:
   ```cpp
   // Load model from account data
   DecisionTreeModel model;
   deserialize_decision_tree(model_account->data.data(), model_account->data.size(), model);
   ```

### Adding More Features

Edit the feature extraction in `ml_trading_agent.cpp`:

```cpp
void extract_features(const OracleData* oracle, int32_t* features) {
    features[0] = normalize_price(oracle->current_price);
    features[1] = normalize_volume(oracle->volume_24h);
    // Add your custom features
}
```

## License

MIT License - See LICENSE file
