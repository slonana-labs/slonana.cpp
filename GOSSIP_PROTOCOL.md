# Agave-Compatible Gossip Protocol Implementation

## Overview

This document describes the C++ implementation of the Agave (Solana) gossip protocol in slonana.cpp. The implementation is based on the official Agave codebase at: https://github.com/anza-xyz/agave/tree/master/gossip

## Architecture

The gossip protocol implementation follows Agave's architecture with five main components:

### 1. CRDS (Conflict-free Replicated Data Store)

**Location:** `include/network/gossip/crds_data.h`, `include/network/gossip/crds_value.h`, `include/network/gossip/crds.h`

The CRDS is the core data structure for storing and replicating gossip data across the cluster. It implements:

- **CrdsData**: Variant type containing all possible gossip data types:
  - `ContactInfo`: Node network addresses and metadata
  - `Vote`: Validator vote information
  - `LowestSlot`: Lowest slot tracking
  - `EpochSlots`: Epoch slot information
  - `NodeInstance`: Node instance identification
  - `SnapshotHashes`: Snapshot hash distribution
  - `RestartLastVotedForkSlots`: Fork restart data
  - `RestartHeaviestFork`: Heaviest fork tracking

- **CrdsValue**: Signed CRDS entry with:
  - Signature for authenticity
  - Hash for deterministic ordering
  - Wallclock timestamp
  - Conflict resolution logic

- **Crds Table**: Main storage with:
  - Thread-safe concurrent access
  - Conflict resolution on insert
  - Indexing by pubkey and label
  - Entry timeout and trimming
  - Query operations

### 2. Protocol Messages

**Location:** `include/network/gossip/protocol.h`

Implements all six Agave gossip message types:

1. **PullRequest**: Request data from peers
   - Contains a bloom filter of known values
   - Includes caller's ContactInfo

2. **PullResponse**: Respond with CRDS values
   - Returns values not in requester's filter
   - Chunked for large responses

3. **PushMessage**: Push gossip to peers
   - Proactively send new values
   - Sent to active set of peers

4. **PruneMessage**: Prune connections
   - Remove peers from active set
   - Signed for authenticity

5. **PingMessage**: Measure latency
   - Random token for matching
   - Used for peer selection

6. **PongMessage**: Respond to ping
   - Echoes token from ping
   - Calculates round-trip time

### 3. Gossip Service

**Location:** `include/network/gossip/gossip_service.h`, `src/network/gossip/gossip_service.cpp`

Multi-threaded service that orchestrates the gossip protocol:

**Threads:**
- **Receiver Thread**: Receives and processes incoming messages
- **Push Gossip Thread**: Periodically broadcasts new values to peers
- **Pull Gossip Thread**: Periodically requests missing values from peers
- **Trim Thread**: Removes expired entries from CRDS
- **Ping/Pong Thread**: Monitors peer latency

**Features:**
- Callback system for new contact info and votes
- Statistics tracking
- Configurable intervals and fanouts
- Socket management

### 4. Push Gossip

Push gossip proactively distributes new information:

1. Maintain an "active set" of peers
2. Periodically select values to push
3. Send to multiple peers (fanout)
4. Handle prune messages to remove unresponsive peers

### 5. Pull Gossip

Pull gossip ensures eventual consistency:

1. Build bloom filter of known values
2. Send pull request to random peers
3. Receive values not in our filter
4. Insert into CRDS table

## Data Flow

```
┌─────────────────┐
│  Network Socket │
└────────┬────────┘
         │
         v
┌────────────────────┐
│  Receiver Thread   │
└────────┬───────────┘
         │
         v
┌─────────────────────┐
│  Message Handler    │
│  - PullRequest      │
│  - PullResponse     │
│  - PushMessage      │
│  - PruneMessage     │
│  - Ping/Pong        │
└────────┬────────────┘
         │
         v
┌────────────────┐
│  CRDS Table    │
│  - Insert      │
│  - Update      │
│  - Query       │
└────────┬───────┘
         │
         v
┌────────────────┐
│  Callbacks     │
│  - ContactInfo │
│  - Votes       │
└────────────────┘
```

## Conflict Resolution

When a new value is inserted into CRDS:

1. **Check Label**: Does an entry with this label exist?
2. **ContactInfo Special Case**: If ContactInfo, compare `outset` (node restart time)
3. **Wallclock Comparison**: Compare timestamps
4. **Hash Tiebreaker**: If timestamps equal, compare hashes deterministically

This ensures all nodes converge to the same state.

## Configuration

The GossipService is configured with:

```cpp
GossipService::Config config;
config.bind_address = "0.0.0.0";
config.bind_port = 8001;
config.node_pubkey = my_pubkey;
config.shred_version = 12345;
config.gossip_push_fanout = 6;    // Peers to push to
config.gossip_pull_fanout = 3;    // Peers to pull from
config.push_interval_ms = 100;     // Push frequency
config.pull_interval_ms = 1000;    // Pull frequency
config.trim_interval_ms = 10000;   // Cleanup frequency
config.entry_timeout_ms = 30000;   // Entry expiration
```

## Usage Example

```cpp
#include "network/gossip/gossip_service.h"

// Create configuration
GossipService::Config config;
config.bind_address = "0.0.0.0";
config.bind_port = 8001;
config.node_pubkey = get_node_pubkey();
config.shred_version = 12345;

// Create and start service
GossipService gossip(config);
auto result = gossip.start();
if (!result.is_ok()) {
    std::cerr << "Failed to start gossip: " << result.error() << std::endl;
    return 1;
}

// Register callbacks
gossip.register_contact_info_callback([](const ContactInfo& ci) {
    std::cout << "New node discovered!" << std::endl;
});

gossip.register_vote_callback([](const Vote& vote) {
    std::cout << "New vote received!" << std::endl;
});

// Get cluster information
auto peers = gossip.get_known_peers();
std::cout << "Known peers: " << peers.size() << std::endl;

auto stats = gossip.get_stats();
std::cout << "Nodes: " << stats.num_nodes << std::endl;
std::cout << "Votes: " << stats.num_votes << std::endl;

// Stop when done
gossip.stop();
```

## Thread Safety

All components are thread-safe:

- CRDS table uses mutex for concurrent access
- GossipService coordinates multiple threads
- Callbacks are invoked with locks held (keep them fast!)

## Performance Considerations

1. **Bloom Filters**: Used in pull requests to minimize data transfer
2. **Message Chunking**: Large responses split into multiple packets
3. **Active Set**: Limited number of push peers to control bandwidth
4. **Entry Timeout**: Old entries removed to bound memory usage
5. **Ordinal Indexing**: Efficient replication of recent entries

## Compatibility with Agave

This implementation is designed to be wire-compatible with Agave:

✅ Same message types and structure
✅ Same CRDS conflict resolution logic
✅ Same data types (ContactInfo, Vote, etc.)
✅ Same push/pull gossip mechanics
✅ Same bloom filter approach
✅ Same prune mechanism

## Implementation Status

**Complete:**
- ✅ All core data structures
- ✅ CRDS table with conflict resolution
- ✅ All protocol messages
- ✅ Multi-threaded gossip service
- ✅ Push and pull gossip
- ✅ Bloom filters
- ✅ Active set management
- ✅ Entry timeout and trimming

**Simplified (Production-ready requires):**
- ⚠️ Cryptographic signatures (stubbed, needs proper Ed25519)
- ⚠️ Network serialization (needs bincode-compatible format)
- ⚠️ Weighted peer selection (needs stake-weighted shuffle)
- ⚠️ Full duplicate shred handling

## Testing

To test the gossip implementation:

```bash
# Build the project
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run tests (when added)
./test_gossip
```

## References

- Agave Gossip Source: https://github.com/anza-xyz/agave/tree/master/gossip
- Solana Documentation: https://docs.solana.com/
- Original Implementation Analysis: See `/tmp/agave_gossip/` (development)

## Future Enhancements

1. **Weighted Shuffle**: Stake-weighted peer selection for better security
2. **Duplicate Shred Detection**: Full implementation of duplicate shred handling
3. **Advanced Metrics**: More detailed performance and health metrics
4. **Dynamic Configuration**: Runtime adjustment of fanout and intervals
5. **Network Compression**: Compress large messages to reduce bandwidth
6. **Rate Limiting**: Per-peer rate limits to prevent abuse

## License

This implementation follows the same license as the slonana.cpp project.
