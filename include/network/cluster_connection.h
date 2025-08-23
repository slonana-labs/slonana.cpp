#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <optional>

namespace slonana {

// Forward declarations
namespace common {
    struct ValidatorConfig;
}

namespace network {

using namespace slonana::common;

enum class NetworkType {
    MAINNET,
    TESTNET,
    DEVNET,
    LOCALNET
};

enum class ClusterMessageType {
    HANDSHAKE,
    PING,
    PONG,
    BLOCK_ANNOUNCEMENT,
    TRANSACTION_FORWARD,
    CLUSTER_INFO,
    VOTE_MESSAGE,
    SHRED_DATA
};

struct ClusterNode {
    std::string node_id;
    std::string ip_address;
    uint16_t gossip_port;
    uint16_t rpc_port;
    uint16_t tvu_port;
    uint16_t tpu_port;
    uint64_t last_seen;
    std::string version;
    uint64_t stake_weight;
    bool is_leader;
};

struct ClusterMessage {
    ClusterMessageType type;
    std::string sender_id;
    std::vector<uint8_t> data;
    uint64_t timestamp;
    std::string signature;
};

struct ClusterStats {
    uint64_t connected_nodes;
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t uptime_seconds;
    std::string current_leader;
    uint64_t current_slot;
};

class ClusterConnection {
private:
    NetworkType network_type_;
    std::string node_id_;
    ValidatorConfig config_;
    
    // Connection management
    std::vector<ClusterNode> known_nodes_;
    std::unordered_map<std::string, std::shared_ptr<ClusterNode>> connected_nodes_;
    mutable std::mutex nodes_mutex_;
    
    // Message handling
    std::thread message_handler_thread_;
    std::thread discovery_thread_;
    std::thread heartbeat_thread_;
    std::atomic<bool> running_;
    
    // Statistics
    ClusterStats stats_;
    mutable std::mutex stats_mutex_;
    
    // Callbacks
    std::function<void(const ClusterMessage&)> message_callback_;
    std::function<void(const ClusterNode&)> node_discovered_callback_;
    std::function<void(const std::string&)> node_disconnected_callback_;
    
    // Network discovery
    std::vector<std::string> bootstrap_nodes_;
    void load_bootstrap_nodes();
    void discover_peers();
    void handle_discovery_response(const std::vector<uint8_t>& data, const std::string& from_address);
    
    // Message processing
    void message_handler_loop();
    void process_handshake(const ClusterMessage& msg);
    void process_ping(const ClusterMessage& msg);
    void process_pong(const ClusterMessage& msg);
    void process_block_announcement(const ClusterMessage& msg);
    void process_transaction_forward(const ClusterMessage& msg);
    void process_cluster_info(const ClusterMessage& msg);
    void process_vote_message(const ClusterMessage& msg);
    void process_shred_data(const ClusterMessage& msg);
    
    // Connection management
    bool connect_to_node(const ClusterNode& node);
    void disconnect_from_node(const std::string& node_id);
    void heartbeat_loop();
    void send_ping_to_all();
    void cleanup_stale_connections();
    
    // Protocol implementation
    std::vector<uint8_t> serialize_message(const ClusterMessage& msg);
    ClusterMessage deserialize_message(const std::vector<uint8_t>& data);
    std::string sign_message(const std::vector<uint8_t>& data);
    bool verify_message_signature(const ClusterMessage& msg);
    
public:
    ClusterConnection(NetworkType network_type, const ValidatorConfig& config);
    ~ClusterConnection();
    
    // Lifecycle management
    bool start();
    void stop();
    bool is_running() const { return running_.load(); }
    
    // Node management
    bool add_bootstrap_node(const std::string& address);
    std::vector<ClusterNode> get_connected_nodes() const;
    std::optional<ClusterNode> get_node_info(const std::string& node_id) const;
    size_t get_connection_count() const;
    
    // Message handling
    void set_message_callback(std::function<void(const ClusterMessage&)> callback);
    void set_node_discovered_callback(std::function<void(const ClusterNode&)> callback);
    void set_node_disconnected_callback(std::function<void(const std::string&)> callback);
    
    // Communication
    bool send_message(const ClusterMessage& msg, const std::string& target_node_id = "");
    bool broadcast_message(const ClusterMessage& msg);
    bool forward_transaction(const std::vector<uint8_t>& transaction_data);
    bool announce_block(uint64_t slot, const std::string& block_hash, const std::vector<uint8_t>& block_data);
    
    // Network discovery
    void initiate_discovery();
    bool join_cluster();
    void update_cluster_info();
    
    // Leader election support
    std::string get_current_leader() const;
    bool is_leader() const;
    void vote_for_leader(const std::string& leader_id, uint64_t slot);
    
    // Statistics and monitoring
    ClusterStats get_stats() const;
    std::string get_network_info() const;
    void reset_stats();
    
    // Network type utilities
    static std::string network_type_to_string(NetworkType type);
    static NetworkType string_to_network_type(const std::string& type);
    static std::vector<std::string> get_default_bootstrap_nodes(NetworkType type);
};

// Utility functions for cluster connectivity
namespace cluster_utils {
    std::string generate_node_id();
    uint16_t find_available_port(uint16_t start_port = 8000);
    bool is_valid_cluster_address(const std::string& address);
    std::vector<uint8_t> create_handshake_message(const std::string& node_id, const std::string& version);
    std::vector<uint8_t> create_ping_message();
    std::vector<uint8_t> create_pong_message(const std::string& ping_id);
}

}} // namespace slonana::network