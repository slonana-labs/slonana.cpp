# Economic Opcodes for sBPF Runtime

This document describes the native economic opcodes implemented in the sBPF runtime, providing high-performance built-in primitives for common blockchain economic patterns.

## Overview

Traditional implementations of economic mechanisms (auctions, escrows, staking) require extensive custom code and consume significant compute units. Native economic opcodes provide 20-50x compute unit savings by implementing these patterns directly in the runtime.

### Compute Unit Comparison

| Operation | Manual Implementation | Native Opcode | Savings |
|-----------|----------------------|---------------|---------|
| Create VCG Auction | 50,000 CU | 1,000 CU | 50x |
| Submit Bid | 10,000 CU | 500 CU | 20x |
| Settle Auction | 200,000 CU | 5,000 CU | 40x |
| Create Escrow | 10,000 CU | 500 CU | 20x |
| Release Escrow | 5,000 CU | 200 CU | 25x |
| Lock Stake | 8,000 CU | 400 CU | 20x |
| Slash Stake | 6,000 CU | 300 CU | 20x |
| Update Reputation | 2,000 CU | 100 CU | 20x |

## Auction System (BPF_ECON_AUCTION)

### Supported Auction Types

1. **VCG (Vickrey-Clarke-Groves)**: Truthful multi-item auction where winners pay the externality they impose on other bidders
2. **GSP (Generalized Second Price)**: Similar to Google AdWords, each winner pays the next highest bid
3. **Sealed-Bid Second Price**: Classic Vickrey auction for single items
4. **English**: Ascending price auction
5. **Dutch**: Descending price auction

### API

```cpp
// Create auction
uint64_t sol_econ_auction_create(
    uint8_t auction_type,
    const uint8_t* items_data,
    uint64_t items_len,
    const uint64_t* reserve_prices,
    uint64_t reserve_len,
    uint64_t deadline_slot,
    const uint8_t* settlement_pubkey
);

// Submit bid
uint64_t sol_econ_bid(
    uint64_t auction_id,
    const uint8_t* bidder_pubkey,
    const uint64_t* item_values,
    uint64_t values_len,
    uint64_t total_amount
);

// Settle auction (computes optimal allocation)
uint64_t sol_econ_settle(uint64_t auction_id);
```

### Usage Example

```cpp
// Create a VCG auction for ad slots
auto auction_id = engine.sol_econ_auction_create(
    "creator_pubkey",
    AuctionType::VCG,
    {"premium_slot", "standard_slot"},
    {1000, 500},  // reserve prices
    deadline_slot,
    "settlement_account"
);

// Submit bids
engine.sol_econ_bid(auction_id, "bidder1", {3000, 2000}, 5000, current_slot);
engine.sol_econ_bid(auction_id, "bidder2", {2500, 1800}, 4300, current_slot);

// Settle after deadline
engine.sol_econ_settle(auction_id, current_slot);
// Winners pay second-price (VCG mechanism)
```

## Escrow System (BPF_ECON_ESCROW)

### Release Conditions

- **TIME_LOCK**: Release after a specific slot
- **MULTI_SIG**: Release when M-of-N parties approve
- **ORACLE_CONDITION**: Release when oracle confirms
- **SMART_CONTRACT**: Release by contract logic
- **UNCONDITIONAL**: Release anytime by receiver

### API

```cpp
// Create escrow
uint64_t sol_econ_escrow(
    const uint8_t* parties_data,
    uint64_t parties_len,
    uint64_t amount,
    uint8_t condition,
    uint64_t release_slot,
    uint64_t expiry_slot
);

// Release escrow
uint64_t sol_econ_release(uint64_t escrow_id);
```

### Usage Example

```cpp
// Create a 2-of-3 multi-sig escrow
std::vector<EscrowParty> parties;
// Add sender, receiver, arbiter
parties.push_back({sender_pubkey, EscrowRole::SENDER});
parties.push_back({receiver_pubkey, EscrowRole::RECEIVER});
parties.push_back({arbiter_pubkey, EscrowRole::ARBITER});

auto escrow_id = engine.sol_econ_escrow(
    parties,
    1000000,  // 1M lamports
    EscrowCondition::MULTI_SIG,
    0,        // no time lock
    expiry_slot,
    2         // require 2 approvals
);

// Approve and release
engine.get_escrow_manager().approve_release(escrow_id, sender_pubkey);
engine.get_escrow_manager().approve_release(escrow_id, receiver_pubkey);
engine.sol_econ_release(escrow_id, current_slot);
```

## Staking System (BPF_ECON_STAKE)

### Features

- Lock tokens with configurable duration
- Delegation support with fee configuration
- Slashing for misbehavior
- Reward distribution

### Slash Reasons

- DOUBLE_SIGNING
- DOWNTIME
- MALICIOUS_BEHAVIOR
- PROTOCOL_VIOLATION
- GOVERNANCE_DECISION

### API

```cpp
// Lock stake
uint64_t sol_econ_stake(
    const uint8_t* staker_pubkey,
    const uint8_t* validator_pubkey,
    uint64_t amount,
    uint64_t duration_slots
);

// Slash stake
uint64_t sol_econ_slash(
    uint64_t stake_id,
    uint64_t percentage,
    uint8_t reason
);
```

### Usage Example

```cpp
// Lock 100 SOL for 1 epoch
auto stake_id = engine.sol_econ_stake(
    staker_pubkey,
    validator_pubkey,
    100_000_000_000,  // 100 SOL in lamports
    432000,           // ~1 epoch in slots
    current_slot
);

// Slash 10% for downtime
engine.sol_econ_slash(
    stake_id,
    10,  // 10%
    SlashReason::DOWNTIME,
    current_slot
);

// Distribute rewards
engine.get_staking_manager().distribute_rewards(stake_id, 1_000_000);
```

## Reputation System (BPF_ECON_REPUTE)

### Score Components

- **Trade Score** (35%): Trading history success rate
- **Payment Score** (30%): Payment reliability
- **Dispute Score** (20%): Dispute resolution record
- **Activity Score** (15%): Overall activity level

All scores range from 0 to 10,000 for precision.

### Actions

- TRADE_COMPLETED / TRADE_FAILED
- PAYMENT_ON_TIME / PAYMENT_LATE
- DISPUTE_WON / DISPUTE_LOST
- REFERRAL / VERIFICATION
- BONUS / PENALTY

### API

```cpp
// Update reputation
uint64_t sol_econ_repute(
    const uint8_t* entity_pubkey,
    uint8_t action,
    int32_t value_change,
    uint64_t transaction_volume
);
```

### Usage Example

```cpp
// Update reputation after successful trade
engine.sol_econ_repute(
    trader_pubkey,
    ReputationAction::TRADE_COMPLETED,
    100,        // +100 to trade score
    1000000,    // 1M volume
    current_slot
);

// Check if entity meets threshold for premium features
bool qualifies = engine.get_reputation_manager().meets_threshold(
    trader_pubkey,
    7500  // Require 75% reputation
);
```

## Concurrent Execution Lanes

The economic opcodes engine includes support for parallel execution across multiple CPU lanes.

### Features

- 8 parallel execution lanes (configurable)
- Automatic conflict detection based on read/write sets
- Lock-free queue submission
- ~10,000+ agents/second throughput

### Conflict Detection

Agents are automatically assigned to lanes based on their read/write sets:
- Agents with overlapping write sets go to the same lane
- Agents with non-overlapping access sets run in parallel
- Automatic dependency tracking

### Usage Example

```cpp
// Initialize with 8 lanes
EconomicOpcodesEngine engine(8);
engine.initialize();

// Submit agents for parallel execution
std::vector<std::unique_ptr<AgentExecutionContext>> agents;
for (int i = 0; i < 100; ++i) {
    auto ctx = std::make_unique<AgentExecutionContext>();
    ctx->agent_id = i;
    ctx->program_id = "ai_trader_" + std::to_string(i);
    ctx->compute_budget = 200000;
    ctx->write_set = {"agent_" + std::to_string(i) + "_state"};
    agents.push_back(std::move(ctx));
}

// Submit all - automatically parallelized
auto lane_assignments = engine.submit_agents_parallel(std::move(agents));
```

## Performance Results

From benchmark tests:

| Operation | Latency |
|-----------|---------|
| Auction creation | 0.35 μs |
| Escrow creation | 0.11 μs |
| Stake creation | 0.20 μs |
| Stake slashing | 0.04 μs |
| Reputation update | 0.06 μs |
| Lane throughput | 10,000 agents/sec |

## Security Considerations

1. **Input Validation**: All syscalls validate inputs for null pointers and bounds
2. **Overflow Protection**: Integer operations use checked arithmetic
3. **State Isolation**: Each manager maintains isolated state
4. **Atomic Operations**: Lane management uses lock-free atomics where possible
5. **Conflict Prevention**: Write set tracking prevents data races

## Future Enhancements

1. **Hardware Acceleration**: SIMD optimization for batch operations
2. **Persistent Storage**: BPF map integration for state persistence
3. **Cross-Program Invocation**: CPI support between economic primitives
4. **Oracle Integration**: Native oracle price feeds for auctions
5. **Governance Integration**: On-chain governance for parameter updates
