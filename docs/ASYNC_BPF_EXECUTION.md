# Async BPF Execution in sBPF Runtime

**Autonomous Program Execution for On-Chain AI Agents**

This document describes the asynchronous BPF execution capabilities implemented in the Solana Virtual Machine (SVM) runtime, enabling programs to self-schedule and react to state changes autonomously.

---

## Executive Summary

The Async BPF Execution module provides:
- **Block-based Timers** for self-scheduling programs at future slots
- **Account Watchers** for automatic triggers on state changes
- **Background Task Scheduler** for async execution without blocking
- **Priority Queue** for time-critical operations

### Key Features

| Feature | Description |
|---------|-------------|
| `sol_timer_create` | Create one-shot timer at future slot |
| `sol_timer_create_periodic` | Create recurring timer every N slots |
| `sol_timer_cancel` | Cancel a pending timer |
| `sol_watcher_create` | Watch account for any change |
| `sol_watcher_create_threshold` | Watch for value crossing threshold |
| `sol_watcher_remove` | Remove an account watcher |
| `sol_ring_buffer_create` | Create ring buffer for event queuing |
| `sol_ring_buffer_push` | Push data to ring buffer |
| `sol_ring_buffer_pop` | Pop data from ring buffer |
| `sol_get_slot` | Get current slot number |

### Performance Results

| Operation | Throughput/Latency |
|-----------|-------------------|
| Timer creation | 0.07 μs/timer |
| Watcher check (100 watchers) | 18 μs/check |
| Task throughput | 263,158 tasks/sec |

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│  AsyncBpfExecutionEngine                                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌────────────────────────────────────────────────────────────┐│
│  │  TimerManager                                              ││
│  │  • One-shot timers (fire once at slot X)                   ││
│  │  • Periodic timers (fire every N slots)                    ││
│  │  • Deadline timers (must fire before slot X)               ││
│  │  • Max 16 timers per program                               ││
│  └────────────────────────────────────────────────────────────┘│
│                                                                 │
│  ┌────────────────────────────────────────────────────────────┐│
│  │  AccountWatcherManager                                     ││
│  │  • ANY_CHANGE - trigger on any modification                ││
│  │  • LAMPORT_CHANGE - trigger on balance change              ││
│  │  • DATA_CHANGE - trigger on data modification              ││
│  │  • THRESHOLD_ABOVE/BELOW - trigger on value crossing       ││
│  │  • PATTERN_MATCH - trigger on data matching pattern        ││
│  │  • Max 32 watchers per program                             ││
│  └────────────────────────────────────────────────────────────┘│
│                                                                 │
│  ┌────────────────────────────────────────────────────────────┐│
│  │  RingBufferManager                                         ││
│  │  • Lock-free ring buffer implementation                    ││
│  │  • Event queuing for inter-program communication           ││
│  │  • FIFO ordering with sequence numbers                     ││
│  │  • Max 8 buffers per program, up to 1MB each               ││
│  └────────────────────────────────────────────────────────────┘│
│                                                                 │
│  ┌────────────────────────────────────────────────────────────┐│
│  │  AsyncTaskScheduler                                        ││
│  │  • Priority-based task queue                               ││
│  │  • Multi-threaded worker pool                              ││
│  │  • Future/promise result handling                          ││
│  │  • Completion callbacks                                    ││
│  └────────────────────────────────────────────────────────────┘│
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Timer API

### sol_timer_create

Create a one-shot timer that fires at a specific slot.

```c
uint64_t sol_timer_create(
    uint64_t trigger_slot,          // Slot when timer fires
    const uint8_t* callback_data,   // Data passed to callback
    uint64_t callback_data_len,     // Length of callback data
    uint64_t compute_budget         // Max CU for callback
);
// Returns: Timer ID or 0 on failure
```

**Example Usage:**
```c
// Schedule a rebalance operation 100 slots from now
uint64_t current = sol_get_slot();
uint8_t data[] = {0x01, 0x02};  // Action code

uint64_t timer_id = sol_timer_create(
    current + 100,   // Fire at slot current+100
    data,
    sizeof(data),
    200000           // 200K compute budget
);
```

### sol_timer_create_periodic

Create a timer that fires repeatedly at fixed intervals.

```c
uint64_t sol_timer_create_periodic(
    uint64_t start_slot,            // First slot to trigger
    uint64_t period_slots,          // Slots between triggers
    const uint8_t* callback_data,   // Data passed to callback
    uint64_t callback_data_len,     // Length of callback data
    uint64_t compute_budget         // Max CU for callback
);
// Returns: Timer ID or 0 on failure
```

**Example Usage:**
```c
// Create a timer that checks market conditions every 10 slots
uint64_t timer_id = sol_timer_create_periodic(
    sol_get_slot() + 1,  // Start next slot
    10,                   // Every 10 slots (~4 seconds)
    NULL,
    0,
    150000
);
```

### sol_timer_cancel

Cancel a pending timer.

```c
uint64_t sol_timer_cancel(uint64_t timer_id);
// Returns: 0 on success, error code on failure
```

---

## Account Watcher API

### sol_watcher_create

Create a watcher that triggers on account changes.

```c
uint64_t sol_watcher_create(
    const uint8_t* account_pubkey,  // 32-byte account address
    uint8_t trigger_type,           // WatcherTrigger enum value
    const uint8_t* callback_data,   // Data passed to callback
    uint64_t callback_data_len,     // Length of callback data
    uint64_t compute_budget         // Max CU for callback
);
// Returns: Watcher ID or 0 on failure
```

**Trigger Types:**
- `0` = ANY_CHANGE - Any modification to account
- `1` = LAMPORT_CHANGE - Balance change
- `2` = DATA_CHANGE - Data modification
- `3` = OWNER_CHANGE - Owner field change
- `4` = THRESHOLD_ABOVE - Value crosses threshold (above)
- `5` = THRESHOLD_BELOW - Value crosses threshold (below)
- `6` = PATTERN_MATCH - Data matches pattern

**Example Usage:**
```c
// Watch an oracle price account for any update
uint8_t oracle_pubkey[32] = { /* oracle address */ };

uint64_t watcher_id = sol_watcher_create(
    oracle_pubkey,
    0,        // ANY_CHANGE
    NULL,
    0,
    100000
);
```

### sol_watcher_create_threshold

Create a watcher that triggers when a value crosses a threshold.

```c
uint64_t sol_watcher_create_threshold(
    const uint8_t* account_pubkey,  // 32-byte account address
    uint8_t trigger_type,           // THRESHOLD_ABOVE (4) or THRESHOLD_BELOW (5)
    uint64_t offset,                // Byte offset in account data
    int64_t threshold,              // Value to compare against
    const uint8_t* callback_data,   // Data passed to callback
    uint64_t callback_data_len,     // Length of callback data
    uint64_t compute_budget         // Max CU for callback
);
// Returns: Watcher ID or 0 on failure
```

**Example Usage:**
```c
// Watch for SOL price going above $200 (stored at offset 8)
uint8_t price_account[32] = { /* price feed address */ };

uint64_t watcher_id = sol_watcher_create_threshold(
    price_account,
    4,              // THRESHOLD_ABOVE
    8,              // Price value at byte 8
    200_00000000,   // $200 with 8 decimals
    NULL,
    0,
    200000
);
```

### sol_watcher_remove

Remove an account watcher.

```c
uint64_t sol_watcher_remove(uint64_t watcher_id);
// Returns: 0 on success, error code on failure
```

---

## Ring Buffer API

Ring buffers provide high-performance inter-program communication and event queuing for async operations.

### sol_ring_buffer_create

Create a new ring buffer for event communication.

```c
uint64_t sol_ring_buffer_create(uint64_t size);
// Returns: Buffer ID or 0 on failure
```

**Example Usage:**
```c
// Create a 4KB event buffer
uint64_t buffer_id = sol_ring_buffer_create(4096);
if (buffer_id == 0) {
    // Handle error
    return;
}
```

### sol_ring_buffer_push

Push data to a ring buffer.

```c
uint64_t sol_ring_buffer_push(
    uint64_t buffer_id,    // Target buffer
    const uint8_t* data,   // Data to push
    uint64_t data_len      // Length of data
);
// Returns: 0 on success, error code on failure
```

**Example Usage:**
```c
// Push market event to buffer
uint8_t event[] = {0x01, 0x00, 0x50, 0xC3}; // Event type + price
uint64_t result = sol_ring_buffer_push(buffer_id, event, sizeof(event));
if (result != 0) {
    // Handle buffer full or error
}
```

### sol_ring_buffer_pop

Pop data from a ring buffer.

```c
uint64_t sol_ring_buffer_pop(
    uint64_t buffer_id,    // Source buffer
    uint8_t* output,       // Output buffer
    uint64_t output_len    // Max bytes to read
);
// Returns: Number of bytes read, 0 if empty
```

**Example Usage:**
```c
uint8_t event_data[256];
uint64_t bytes_read = sol_ring_buffer_pop(buffer_id, event_data, sizeof(event_data));
if (bytes_read > 0) {
    // Process event
    process_event(event_data, bytes_read);
}
```

### sol_ring_buffer_destroy

Destroy a ring buffer and free resources.

```c
uint64_t sol_ring_buffer_destroy(uint64_t buffer_id);
// Returns: 0 on success, error code on failure
```

### Ring Buffer Configuration

| Parameter | Value | Description |
|-----------|-------|-------------|
| MAX_RING_BUFFERS_PER_PROGRAM | 8 | Maximum buffers per program |
| MAX_RING_BUFFER_SIZE | 1,048,576 | Maximum buffer size (1MB) |
| MIN_RING_BUFFER_SIZE | 64 | Minimum buffer size |
| DEFAULT_RING_BUFFER_SIZE | 4,096 | Default buffer size (4KB) |

---

## Integration with ML Inference

The async execution module integrates seamlessly with the ML inference syscalls for building autonomous AI trading agents:

```
┌────────────────────────────────────────────────────────────────┐
│  Autonomous AI Trading Agent                                   │
├────────────────────────────────────────────────────────────────┤
│                                                                │
│  Timer (every 10 slots):                                       │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │  1. Read market data from oracle accounts                │ │
│  │  2. Extract features (price, volume, volatility)         │ │
│  │  3. Run ML inference:                                    │ │
│  │     • sol_ml_forward() for neural network prediction     │ │
│  │     • sol_ml_decision_tree() for rule-based signals      │ │
│  │  4. Execute trade if confidence > threshold              │ │
│  └──────────────────────────────────────────────────────────┘ │
│                                                                │
│  Price Watcher (THRESHOLD_ABOVE):                              │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │  1. Triggered when SOL > $200                            │ │
│  │  2. Run profit-taking strategy                           │ │
│  │  3. Reduce position by 20%                               │ │
│  └──────────────────────────────────────────────────────────┘ │
│                                                                │
│  Stop-Loss Watcher (THRESHOLD_BELOW):                          │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │  1. Triggered when portfolio value < threshold           │ │
│  │  2. Emergency exit all positions                         │ │
│  │  3. Log event for analysis                               │ │
│  └──────────────────────────────────────────────────────────┘ │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

---

## Best Practices

### Timer Guidelines

1. **Use appropriate intervals**: Don't create timers with very short periods that waste compute
2. **Cancel unused timers**: Always cancel timers that are no longer needed
3. **Budget compute carefully**: Set realistic compute budgets to avoid failures
4. **Avoid slot 0 % 100**: Cleanup runs at these slots, may remove completed timers

### Watcher Guidelines

1. **Be specific with triggers**: Use THRESHOLD_* instead of ANY_CHANGE when possible
2. **Set rate limits**: Use `min_slots_between_triggers` to avoid spam
3. **Validate callback data**: Always validate data passed to callbacks
4. **Clean up watchers**: Remove watchers when no longer monitoring

### Performance Considerations

1. **Batch operations**: Group related operations in single callbacks
2. **Minimize account reads**: Cache frequently accessed data
3. **Use appropriate priority**: Set HIGH priority only for time-critical tasks
4. **Monitor statistics**: Use get_stats() to track performance

---

## Limits and Constants

| Constant | Value | Description |
|----------|-------|-------------|
| MAX_TIMERS_PER_PROGRAM | 16 | Maximum timers per program |
| MAX_WATCHERS_PER_PROGRAM | 32 | Maximum watchers per program |
| MAX_SCHEDULED_TASKS | 1024 | Maximum pending tasks |
| MIN_TIMER_SLOTS | 1 | Minimum timer interval |
| MAX_TIMER_SLOTS | 1,000,000 | Maximum timer (~5 days) |

---

## Security Considerations

1. **Compute Limits**: All callbacks have strict compute budgets
2. **Rate Limiting**: Watchers have configurable minimum intervals
3. **Resource Limits**: Per-program limits on timers and watchers
4. **Input Validation**: All syscalls validate inputs for null/invalid values
5. **Bounded Loops**: All internal loops have compile-time bounds

---

## Error Codes

| Code | Name | Description |
|------|------|-------------|
| 0 | SUCCESS | Operation completed successfully |
| 1 | NULL_POINTER | Invalid pointer parameter |
| 2 | INVALID_TIMER | Timer ID not found |
| 3 | INVALID_WATCHER | Watcher ID not found |
| 4 | MAX_TIMERS | Timer limit reached |
| 5 | MAX_WATCHERS | Watcher limit reached |
| 6 | INVALID_SLOT | Invalid slot number |
| 7 | SHUTDOWN | Engine is shutting down |
| 8 | INVALID_BUFFER | Buffer ID not found |
| 9 | BUFFER_FULL | Ring buffer is full |
| 10 | MAX_BUFFERS | Buffer limit reached |

---

## Future Enhancements

1. **Cross-Program Triggers**: Allow programs to trigger each other
2. **Event Batching**: Combine multiple triggers into single callback
3. **Persistent Timers**: Survive program upgrades
4. **Timer Inheritance**: Child programs inherit parent timers
5. **Conditional Timers**: Fire only if condition is met

---

## References

1. Solana Whitepaper - Proof of History
2. Helius Blog - Asynchronous Program Execution (APE)
3. Linux eBPF - KFuncs and timer infrastructure
4. Solana SVM - Program execution pipeline
