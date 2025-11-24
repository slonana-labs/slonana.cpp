# MeshCore Integration Guide

## Overview

The MeshCore adapter provides mesh networking capabilities for slonana nodes, enabling:
- Decentralized peer-to-peer communication
- Automatic peer discovery and mesh topology management
- Encrypted communication using existing QUIC/TLS infrastructure
- Mesh healing and automatic reconnection
- Multiple transport protocol support (QUIC, TCP, UDP)

## Features

### Core Capabilities
- **Peer Discovery**: Automatic discovery of peers via bootstrap nodes
- **Mesh Topology**: Self-organizing mesh with configurable peer limits
- **Encrypted Communication**: Leverages existing QUIC/TLS for secure channels
- **NAT Traversal**: Support for STUN/TURN servers (when configured)
- **Resilience**: Automatic reconnection and mesh healing
- **Performance**: Meets strict latency and recovery requirements

### Performance Characteristics
- **Mesh Join Time**: <2s average, <5s p95
- **Message Latency**: <40ms p50, <75ms p95  
- **Peer Churn Recovery**: >95% within 2s
- **Multi-hop Support**: Configurable maximum hop distance

## Configuration

### Build-Time Configuration

Enable MeshCore during build:

```bash
cmake .. -DENABLE_MESHCORE=ON
make
```

### Runtime Configuration

Configure MeshCore in your `ValidatorConfig`:

```cpp
#include "network/meshcore_adapter.h"

using namespace slonana::network::meshcore;

// Create mesh configuration
MeshConfig config;
config.enabled = true;
config.node_id = "my_node_1";
config.listen_port = 9000;

// Bootstrap nodes for initial peer discovery
config.bootstrap_nodes = {
    "peer1.example.com:9000",
    "peer2.example.com:9000",
    "peer3.example.com:9000"
};

// Transport preferences
config.preferred_transport = TransportType::QUIC;
config.enable_nat_traversal = true;

// STUN/TURN servers for NAT traversal
config.stun_servers = {
    "stun:stun.example.com:3478"
};
config.turn_servers = {
    "turn:turn.example.com:3478"
};

// Performance tuning
config.heartbeat_interval_ms = 5000;
config.reconnect_delay_ms = 1000;
config.max_reconnect_attempts = 5;
config.mesh_discovery_interval_ms = 30000;

// Mesh topology
config.max_direct_peers = 20;
config.desired_direct_peers = 8;
config.max_hops = 5;

// Create adapter
auto adapter = std::make_unique<MeshCoreAdapter>(config);
```

### Feature Toggle

MeshCore can be enabled/disabled at runtime via the `enabled` flag:

```cpp
config.enabled = false;  // Disable MeshCore
config.enabled = true;   // Enable MeshCore
```

When disabled, the adapter will refuse to start, allowing for backward compatibility with existing networking.

## Usage

### Starting the Mesh

```cpp
// Start the adapter
auto result = adapter->start();
if (!result.is_ok()) {
    std::cerr << "Failed to start MeshCore: " << result.error() << std::endl;
    return;
}

// Join the mesh network
result = adapter->join_mesh();
if (!result.is_ok()) {
    std::cerr << "Failed to join mesh: " << result.error() << std::endl;
    return;
}
```

### Sending Messages

```cpp
// Create a message
MeshMessage msg;
msg.type = MeshMessageType::DATA;
msg.sender_id = "my_node_1";
msg.receiver_id = "peer_node_2";
msg.payload = {0x01, 0x02, 0x03, 0x04};
msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
msg.ttl = 10;  // Max hops

// Send to specific peer
auto result = adapter->send_message(msg);
if (!result.is_ok()) {
    std::cerr << "Failed to send message: " << result.error() << std::endl;
}
```

### Broadcasting Messages

```cpp
// Create broadcast message
MeshMessage broadcast_msg;
broadcast_msg.type = MeshMessageType::TOPOLOGY;
broadcast_msg.sender_id = "my_node_1";
// receiver_id left empty for broadcast
broadcast_msg.payload = serialize_topology_data();

// Broadcast to all connected peers
auto result = adapter->broadcast_message(broadcast_msg);
```

### Handling Incoming Messages

```cpp
// Register message handler
adapter->register_message_handler(
    MeshMessageType::DATA,
    [](const MeshMessage& msg) {
        std::cout << "Received data from: " << msg.sender_id << std::endl;
        // Process message payload
        process_data(msg.payload);
    }
);

// Register error handler
adapter->register_error_handler(
    [](const std::string& node_id, const std::string& error_msg) {
        std::cerr << "Error with node " << node_id << ": " << error_msg << std::endl;
    }
);
```

### Peer Management

```cpp
// Get all connected peers
auto peers = adapter->get_connected_peers();
for (const auto& peer : peers) {
    std::cout << "Peer: " << peer.node_id 
              << " at " << peer.address << ":" << peer.port
              << " latency: " << peer.latency_ms << "ms" << std::endl;
}

// Get specific peer info
auto peer_info = adapter->get_peer_info("peer_node_2");
if (peer_info) {
    std::cout << "Peer state: " << static_cast<int>(peer_info->state) << std::endl;
}

// Connect to new peer
adapter->connect_to_peer("newpeer.example.com", 9000);

// Disconnect from peer
adapter->disconnect_from_peer("peer_node_3");
```

### Monitoring and Statistics

```cpp
// Get mesh statistics
auto stats = adapter->get_stats();
std::cout << "Total nodes: " << stats.total_nodes << std::endl;
std::cout << "Connected nodes: " << stats.connected_nodes << std::endl;
std::cout << "Messages sent: " << stats.messages_sent << std::endl;
std::cout << "Messages received: " << stats.messages_received << std::endl;
std::cout << "Average latency: " << stats.average_latency_ms << "ms" << std::endl;
std::cout << "p50 latency: " << stats.p50_latency_ms << "ms" << std::endl;
std::cout << "p95 latency: " << stats.p95_latency_ms << "ms" << std::endl;
std::cout << "Mesh joins: " << stats.mesh_joins << std::endl;
std::cout << "Mesh leaves: " << stats.mesh_leaves << std::endl;
std::cout << "Reconnections: " << stats.reconnections << std::endl;

// Get mesh topology
auto topology = adapter->get_topology();
for (const auto& entry : topology) {
    std::cout << entry << std::endl;
}
```

### Graceful Shutdown

```cpp
// Leave mesh
adapter->leave_mesh();

// Stop adapter
adapter->stop();
```

## Integration with Validator

To integrate MeshCore with the slonana validator:

```cpp
#include "network/meshcore_adapter.h"

// In validator initialization
auto mesh_adapter = create_meshcore_adapter(validator_config);

// Start and join mesh if enabled
if (validator_config.enable_meshcore) {
    auto result = mesh_adapter->start();
    if (result.is_ok()) {
        mesh_adapter->join_mesh();
    }
}

// Use mesh for validator communication
// (replace or augment existing gossip protocol)
```

## Message Types

### MeshMessageType Enum

- **DATA**: Application-level data messages
- **HEARTBEAT**: Keep-alive messages to maintain connections
- **DISCOVERY**: Peer discovery and announcement messages
- **TOPOLOGY**: Mesh topology update messages
- **ERROR**: Error notification messages

## Node States

### NodeState Enum

- **DISCONNECTED**: Node is not connected
- **CONNECTING**: Connection attempt in progress
- **CONNECTED**: Node is fully connected
- **RECONNECTING**: Attempting to reconnect after failure
- **FAILED**: Connection permanently failed

## Transport Types

### TransportType Enum

- **QUIC**: Primary transport - fast, reliable, encrypted
- **TCP**: Fallback transport - traditional TCP connection
- **UDP**: Optional transport - for telemetry and pings

## Security Considerations

### Encryption

MeshCore leverages existing slonana security infrastructure:
- **QUIC Transport**: Encrypted by default using TLS 1.3
- **Message Authentication**: All messages signed with node identity keys
- **Replay Protection**: Built-in nonce-based replay protection

### NAT Traversal

When NAT traversal is enabled:
- Uses STUN servers to discover public endpoints
- Falls back to TURN relay servers when direct connection fails
- Validates peer identities after NAT traversal

### Configuration Security

```cpp
// Enable security features
config.enable_nat_traversal = true;
config.stun_servers = {"stun:trusted-stun.example.com:3478"};
config.turn_servers = {"turn:trusted-turn.example.com:3478"};

// Use authenticated TURN servers
config.turn_username = "username";
config.turn_password = "secure_password";
```

## Performance Tuning

### Heartbeat Interval

```cpp
config.heartbeat_interval_ms = 5000;  // 5 seconds default
```

Shorter intervals provide faster failure detection but increase network overhead.

### Discovery Interval

```cpp
config.mesh_discovery_interval_ms = 30000;  // 30 seconds default
```

Controls how often the mesh topology is refreshed.

### Reconnection Policy

```cpp
config.reconnect_delay_ms = 1000;      // Wait 1s before retry
config.max_reconnect_attempts = 5;     // Try up to 5 times
```

### Mesh Topology

```cpp
config.max_direct_peers = 20;          // Maximum direct connections
config.desired_direct_peers = 8;       // Target number of peers
config.max_hops = 5;                   // Maximum multi-hop distance
```

Balance between connectivity (higher peers) and resource usage.

## Testing

### Running MeshCore Tests

```bash
# Build with tests
cmake .. -DENABLE_MESHCORE=ON
make slonana_meshcore_tests

# Run tests
./slonana_meshcore_tests
```

### Test Coverage

The test suite includes:
- Basic lifecycle (start/stop)
- Mesh join/leave operations
- Peer connection/disconnection
- Message sending and broadcasting
- Message handler registration
- Performance benchmarks
- Multi-node mesh scenarios
- Peer churn recovery

All tests validate against performance requirements:
- Mesh join: <2s avg, <5s p95 ✓
- Message latency: <40ms p50, <75ms p95 ✓
- Recovery: >95% in 2s ✓

## Troubleshooting

### Mesh Won't Start

```
Error: MeshCore is disabled in configuration
```
**Solution**: Set `config.enabled = true` or build with `-DENABLE_MESHCORE=ON`

### Can't Join Mesh

```
Error: Not running
```
**Solution**: Call `adapter->start()` before `adapter->join_mesh()`

### Peer Connection Fails

**Check**:
1. Bootstrap nodes are reachable
2. Firewall rules allow incoming connections
3. NAT traversal is configured if behind NAT
4. STUN/TURN servers are accessible

### High Latency

**Optimize**:
1. Reduce number of hops: Lower `max_hops`
2. Connect to closer peers geographically
3. Check network congestion
4. Verify QUIC is being used (not TCP fallback)

## Future Enhancements

Planned improvements:
- [ ] libdatachannel integration for WebRTC support
- [ ] Advanced routing algorithms (DHT-based)
- [ ] Bandwidth-aware peer selection
- [ ] Mesh visualization tools
- [ ] Prometheus metrics export
- [ ] JSON logging with trace IDs

## References

- [MeshCore Adapter Header](../include/network/meshcore_adapter.h)
- [MeshCore Adapter Implementation](../src/network/meshcore_adapter.cpp)
- [MeshCore Test Suite](../tests/test_meshcore_adapter.cpp)
- [slonana Architecture](ARCHITECTURE.md)
- [Network Layer Documentation](SECURE_COMMUNICATION.md)
