#pragma once

/**
 * MeshCore Adapter - Mesh networking abstraction layer for slonana
 * 
 * This adapter provides a mesh networking interface on top of existing
 * slonana networking capabilities (QUIC, UDP, gossip protocol).
 * 
 * Features:
 * - Peer discovery and mesh topology management
 * - Encrypted communication using existing TLS/QUIC
 * - NAT traversal via STUN/TURN (when available)
 * - Mesh healing and automatic reconnection
 * - Multiple transport support (QUIC, TCP fallback)
 */

#include "common/types.h"
#include "network/gossip.h"
#include "network/quic_client.h"
#include "network/quic_server.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace slonana {
namespace network {
namespace meshcore {

using namespace slonana::common;

/**
 * Mesh node states
 */
enum class NodeState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING,
    FAILED
};

/**
 * Mesh transport protocol types
 */
enum class TransportType {
    QUIC,      // Primary: Fast, reliable, encrypted
    TCP,       // Fallback: Traditional TCP
    UDP        // Optional: For telemetry/pings
};

/**
 * Mesh node information
 */
struct MeshNode {
    std::string node_id;
    std::string address;
    uint16_t port;
    NodeState state;
    TransportType transport;
    std::chrono::steady_clock::time_point last_seen;
    uint64_t messages_sent;
    uint64_t messages_received;
    uint32_t latency_ms;
    bool is_direct_peer;  // true if direct connection, false if multi-hop
};

/**
 * Mesh message types
 */
enum class MeshMessageType {
    DATA,           // Application data
    HEARTBEAT,      // Keep-alive
    DISCOVERY,      // Peer discovery
    TOPOLOGY,       // Mesh topology update
    ERROR           // Error notification
};

/**
 * Mesh message structure
 */
struct MeshMessage {
    MeshMessageType type;
    std::string sender_id;
    std::string receiver_id;  // Empty for broadcast
    std::vector<uint8_t> payload;
    uint64_t timestamp;
    uint32_t ttl;  // Time-to-live for multi-hop
};

/**
 * Mesh statistics
 */
struct MeshStats {
    uint64_t total_nodes;
    uint64_t connected_nodes;
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint32_t average_latency_ms;
    uint32_t p50_latency_ms;
    uint32_t p95_latency_ms;
    std::chrono::steady_clock::duration uptime;
    uint64_t mesh_joins;
    uint64_t mesh_leaves;
    uint64_t reconnections;
};

/**
 * Mesh configuration
 */
struct MeshConfig {
    bool enabled = false;                      // Feature toggle
    std::string node_id;
    std::vector<std::string> bootstrap_nodes;  // Initial peers to connect
    uint16_t listen_port = 0;                  // 0 = auto-assign
    TransportType preferred_transport = TransportType::QUIC;
    bool enable_nat_traversal = true;
    std::vector<std::string> stun_servers;     // For NAT traversal
    std::vector<std::string> turn_servers;     // For relay when needed
    
    // Performance tuning
    uint32_t heartbeat_interval_ms = 5000;
    uint32_t reconnect_delay_ms = 1000;
    uint32_t max_reconnect_attempts = 5;
    uint32_t mesh_discovery_interval_ms = 30000;
    
    // Mesh topology
    uint32_t max_direct_peers = 20;            // Max direct connections
    uint32_t desired_direct_peers = 8;         // Target number of peers
    uint32_t max_hops = 5;                     // Max multi-hop distance
};

/**
 * MeshCore Adapter - Main interface for mesh networking
 */
class MeshCoreAdapter {
public:
    using MessageHandler = std::function<void(const MeshMessage&)>;
    using ErrorHandler = std::function<void(const std::string&, const std::string&)>;
    
    explicit MeshCoreAdapter(const MeshConfig& config);
    ~MeshCoreAdapter();
    
    // Lifecycle
    Result<bool> start();
    void stop();
    bool is_running() const;
    
    // Mesh operations
    Result<bool> join_mesh();
    Result<bool> leave_mesh();
    bool is_joined() const;
    
    // Communication
    Result<bool> send_message(const MeshMessage& message);
    Result<bool> broadcast_message(const MeshMessage& message);
    
    // Message handling
    void register_message_handler(MeshMessageType type, MessageHandler handler);
    void register_error_handler(ErrorHandler handler);
    
    // Peer management
    std::vector<MeshNode> get_connected_peers() const;
    std::optional<MeshNode> get_peer_info(const std::string& node_id) const;
    Result<bool> connect_to_peer(const std::string& address, uint16_t port);
    Result<bool> disconnect_from_peer(const std::string& node_id);
    
    // Statistics and monitoring
    MeshStats get_stats() const;
    std::vector<std::string> get_topology() const;  // Returns mesh topology as JSON
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Factory function to create MeshCore adapter from validator config
 */
std::unique_ptr<MeshCoreAdapter> create_meshcore_adapter(const ValidatorConfig& config);

} // namespace meshcore
} // namespace network
} // namespace slonana
