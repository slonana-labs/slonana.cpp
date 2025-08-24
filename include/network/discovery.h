#pragma once

#include "common/types.h"
#include "genesis/config.h"
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <set>

namespace slonana {
namespace network {

/**
 * Peer information for network discovery
 */
struct NetworkPeer {
    std::string address;
    uint16_t port;
    std::string region;
    bool is_trusted = false;
    uint64_t last_seen = 0;
    uint32_t connection_attempts = 0;
};

/**
 * Network discovery and peer management
 */
class NetworkDiscovery {
public:
    explicit NetworkDiscovery(const common::ValidatorConfig& config);
    ~NetworkDiscovery();

    /**
     * Initialize discovery with genesis configuration
     */
    common::Result<bool> initialize(const genesis::GenesisConfig& genesis_config);
    
    /**
     * Start network discovery process
     */
    common::Result<bool> start();
    
    /**
     * Stop network discovery
     */
    void stop();
    
    /**
     * Get list of discovered peers
     */
    std::vector<NetworkPeer> get_peers() const;
    
    /**
     * Get bootstrap entrypoints for the network
     */
    std::vector<NetworkPeer> get_bootstrap_peers() const;
    
    /**
     * Add a known peer manually
     */
    void add_peer(const NetworkPeer& peer);
    
    /**
     * Remove a peer
     */
    void remove_peer(const std::string& address, uint16_t port);
    
    /**
     * Check if running
     */
    bool is_running() const { return running_.load(); }
    
    /**
     * Get peer count
     */
    size_t get_peer_count() const;
    
    /**
     * Update peer last seen timestamp
     */
    void update_peer_last_seen(const std::string& address, uint16_t port);

private:
    /**
     * Discovery thread main loop
     */
    void discovery_loop();
    
    /**
     * Try to discover peers via DNS
     */
    std::vector<NetworkPeer> discover_dns_peers();
    
    /**
     * Validate peer connectivity
     */
    bool validate_peer_connection(const NetworkPeer& peer);
    
    /**
     * Parse entrypoint string (address:port)
     */
    static NetworkPeer parse_entrypoint(const std::string& entrypoint);
    
    /**
     * Resolve DNS seed to peer addresses
     */
    std::vector<NetworkPeer> resolve_dns_seed(const std::string& dns_seed);
    
    /**
     * Validate IP address format
     */
    bool is_valid_ip_address(const std::string& address);

    common::ValidatorConfig config_;
    genesis::GenesisConfig genesis_config_;
    std::vector<NetworkPeer> peers_;
    std::vector<NetworkPeer> bootstrap_peers_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> discovery_thread_;
    mutable std::mutex peers_mutex_;
};

/**
 * Enhanced gossip protocol with network discovery
 */
class EnhancedGossipProtocol {
public:
    explicit EnhancedGossipProtocol(const common::ValidatorConfig& config);
    ~EnhancedGossipProtocol();
    
    /**
     * Initialize with network discovery
     */
    common::Result<bool> initialize(std::shared_ptr<NetworkDiscovery> discovery);
    
    /**
     * Start enhanced gossip with peer discovery
     */
    common::Result<bool> start();
    
    /**
     * Stop gossip protocol
     */
    void stop();
    
    /**
     * Send message to all peers
     */
    void broadcast_message(const std::vector<uint8_t>& message);
    
    /**
     * Send message to specific peer
     */
    bool send_message_to_peer(const NetworkPeer& peer, const std::vector<uint8_t>& message);
    
    /**
     * Get connected peer count
     */
    size_t get_connected_peer_count() const;
    
    /**
     * Check if running
     */
    bool is_running() const { return running_.load(); }

private:
    /**
     * Peer management thread
     */
    void peer_management_loop();
    
    /**
     * Try to connect to a peer
     */
    bool connect_to_peer(const NetworkPeer& peer);
    
    /**
     * Handle incoming connections
     */
    void handle_incoming_connections();
    
    /**
     * Validate peer connectivity
     */
    bool validate_peer_connectivity(const NetworkPeer& peer);
    
    /**
     * Update peer transmission statistics
     */
    void update_peer_transmission_stats(const NetworkPeer& peer, size_t bytes_sent, int64_t transmission_time_ms);
    
    /**
     * Perform connection handshake with peer
     */
    bool perform_connection_handshake(const NetworkPeer& peer);
    
    /**
     * Negotiate protocol version with peer
     */
    bool negotiate_protocol_version(const NetworkPeer& peer);
    
    /**
     * Check if peer is blacklisted
     */
    bool is_peer_blacklisted(const NetworkPeer& peer);

    common::ValidatorConfig config_;
    std::shared_ptr<NetworkDiscovery> discovery_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> peer_management_thread_;
    std::set<std::string> connected_peers_;  // Track connected peer endpoints
    mutable std::mutex connected_peers_mutex_;
};

/**
 * Mainnet-specific entrypoint configuration
 */
class MainnetEntrypoints {
public:
    /**
     * Get hardcoded mainnet entrypoints
     */
    static std::vector<std::string> get_mainnet_entrypoints();
    
    /**
     * Get hardcoded testnet entrypoints
     */
    static std::vector<std::string> get_testnet_entrypoints();
    
    /**
     * Get hardcoded devnet entrypoints
     */
    static std::vector<std::string> get_devnet_entrypoints();
    
    /**
     * Get entrypoints for network type
     */
    static std::vector<std::string> get_entrypoints_for_network(const std::string& network_id);
};

} // namespace network
} // namespace slonana