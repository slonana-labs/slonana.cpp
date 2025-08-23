#include "network/cluster_connection.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <random>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>

namespace slonana {
namespace network {

ClusterConnection::ClusterConnection(NetworkType network_type, const ValidatorConfig& config)
    : network_type_(network_type), config_(config), running_(false) {
    
    // Generate unique node ID
    node_id_ = cluster_utils::generate_node_id();
    
    // Initialize stats
    stats_ = {};
    stats_.current_slot = 0;
    
    // Load bootstrap nodes for the network
    load_bootstrap_nodes();
    
    std::cout << "Cluster connection initialized for " << network_type_to_string(network_type) 
              << " network with node ID: " << node_id_ << std::endl;
}

ClusterConnection::~ClusterConnection() {
    stop();
}

bool ClusterConnection::start() {
    if (running_.load()) return false;
    
    running_.store(true);
    
    // Start background threads
    message_handler_thread_ = std::thread(&ClusterConnection::message_handler_loop, this);
    discovery_thread_ = std::thread(&ClusterConnection::discover_peers, this);
    heartbeat_thread_ = std::thread(&ClusterConnection::heartbeat_loop, this);
    
    std::cout << "Cluster connection started for " << network_type_to_string(network_type_) << " network" << std::endl;
    
    // Try to join the cluster
    if (!join_cluster()) {
        std::cout << "Warning: Failed to join cluster immediately, will retry in background" << std::endl;
    }
    
    return true;
}

void ClusterConnection::stop() {
    if (!running_.exchange(false)) return;
    
    // Disconnect from all nodes (avoid deadlock by not calling disconnect_from_node while holding lock)
    std::vector<std::string> node_ids_to_disconnect;
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        for (const auto& pair : connected_nodes_) {
            node_ids_to_disconnect.push_back(pair.first);
        }
        connected_nodes_.clear();
        
        // Update stats
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.connected_nodes = 0;
    }
    
    // Notify callbacks for disconnected nodes
    if (node_disconnected_callback_) {
        for (const auto& node_id : node_ids_to_disconnect) {
            node_disconnected_callback_(node_id);
        }
    }
    
    // Wait for threads to finish
    if (message_handler_thread_.joinable()) {
        message_handler_thread_.join();
    }
    if (discovery_thread_.joinable()) {
        discovery_thread_.join();
    }
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    
    std::cout << "Cluster connection stopped" << std::endl;
}

void ClusterConnection::load_bootstrap_nodes() {
    bootstrap_nodes_ = get_default_bootstrap_nodes(network_type_);
    
    // Add any custom bootstrap nodes from config
    // For now, we'll use hardcoded ones based on network type
    std::cout << "Loaded " << bootstrap_nodes_.size() << " bootstrap nodes for " 
              << network_type_to_string(network_type_) << std::endl;
}

bool ClusterConnection::join_cluster() {
    if (bootstrap_nodes_.empty()) {
        std::cout << "No bootstrap nodes available for cluster join" << std::endl;
        return false;
    }
    
    std::cout << "Attempting to join " << network_type_to_string(network_type_) << " cluster..." << std::endl;
    
    // Try to connect to bootstrap nodes
    int successful_connections = 0;
    for (const auto& bootstrap_address : bootstrap_nodes_) {
        // Parse address (format: "ip:port")
        size_t colon_pos = bootstrap_address.find(':');
        if (colon_pos == std::string::npos) continue;
        
        std::string ip = bootstrap_address.substr(0, colon_pos);
        uint16_t port = static_cast<uint16_t>(std::stoi(bootstrap_address.substr(colon_pos + 1)));
        
        ClusterNode node;
        node.node_id = "bootstrap_" + std::to_string(successful_connections);
        node.ip_address = ip;
        node.gossip_port = port;
        node.rpc_port = port + 1;
        node.tvu_port = port + 2;
        node.tpu_port = port + 3;
        node.last_seen = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        node.version = "1.0.0";
        node.stake_weight = 0;
        node.is_leader = false;
        
        if (connect_to_node(node)) {
            successful_connections++;
            std::cout << "Connected to bootstrap node: " << bootstrap_address << std::endl;
        }
        
        // Try to connect to at least 3 bootstrap nodes
        if (successful_connections >= 3) break;
    }
    
    if (successful_connections > 0) {
        std::cout << "Successfully connected to " << successful_connections << " bootstrap nodes" << std::endl;
        
        // Send handshake to all connected nodes
        ClusterMessage handshake;
        handshake.type = ClusterMessageType::HANDSHAKE;
        handshake.sender_id = node_id_;
        handshake.data = cluster_utils::create_handshake_message(node_id_, "slonana-cpp-1.0.0");
        handshake.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        broadcast_message(handshake);
        return true;
    }
    
    std::cout << "Failed to connect to any bootstrap nodes" << std::endl;
    return false;
}

bool ClusterConnection::connect_to_node(const ClusterNode& node) {
    // For this implementation, we'll simulate the connection
    // In a real implementation, this would establish actual TCP/UDP connections
    
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        auto node_copy = std::make_shared<ClusterNode>(node);
        connected_nodes_[node.node_id] = node_copy;
        
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.connected_nodes++;
    }
    
    // Notify callback if set
    if (node_discovered_callback_) {
        node_discovered_callback_(node);
    }
    
    return true;
}

void ClusterConnection::disconnect_from_node(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    auto it = connected_nodes_.find(node_id);
    if (it != connected_nodes_.end()) {
        connected_nodes_.erase(it);
        
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        if (stats_.connected_nodes > 0) {
            stats_.connected_nodes--;
        }
        
        // Notify callback if set
        if (node_disconnected_callback_) {
            node_disconnected_callback_(node_id);
        }
    }
}

void ClusterConnection::discover_peers() {
    while (running_.load()) {
        // Send discovery requests to connected nodes
        if (get_connection_count() > 0) {
            ClusterMessage discovery_msg;
            discovery_msg.type = ClusterMessageType::CLUSTER_INFO;
            discovery_msg.sender_id = node_id_;
            discovery_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            broadcast_message(discovery_msg);
        }
        
        // Sleep in shorter intervals to allow quick exit when stopping
        for (int i = 0; i < 300 && running_.load(); ++i) {  // 300 * 100ms = 30 seconds
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void ClusterConnection::message_handler_loop() {
    while (running_.load()) {
        // In a real implementation, this would read from network sockets
        // For now, we simulate periodic message processing
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Simulate processing some messages
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.messages_received++;
        }
    }
}

void ClusterConnection::heartbeat_loop() {
    while (running_.load()) {
        send_ping_to_all();
        cleanup_stale_connections();
        
        // Update uptime
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.uptime_seconds++;
        }
        
        // Sleep in shorter intervals to allow quick exit when stopping
        for (int i = 0; i < 10 && running_.load(); ++i) {  // 10 * 100ms = 1 second
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void ClusterConnection::send_ping_to_all() {
    ClusterMessage ping;
    ping.type = ClusterMessageType::PING;
    ping.sender_id = node_id_;
    ping.data = cluster_utils::create_ping_message();
    ping.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    broadcast_message(ping);
}

void ClusterConnection::cleanup_stale_connections() {
    auto current_time = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    auto it = connected_nodes_.begin();
    while (it != connected_nodes_.end()) {
        if (current_time - it->second->last_seen > 60) { // 60 seconds timeout
            std::string node_id = it->first;
            it = connected_nodes_.erase(it);
            std::cout << "Removed stale connection to node: " << node_id << std::endl;
        } else {
            ++it;
        }
    }
}

bool ClusterConnection::send_message(const ClusterMessage& msg, const std::string& target_node_id) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    if (target_node_id.empty()) {
        return broadcast_message(msg);
    }
    
    auto it = connected_nodes_.find(target_node_id);
    if (it == connected_nodes_.end()) {
        return false;
    }
    
    // Simulate sending message
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.messages_sent++;
    stats_.bytes_sent += msg.data.size() + 64; // Approximate message overhead
    
    return true;
}

bool ClusterConnection::broadcast_message(const ClusterMessage& msg) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    if (connected_nodes_.empty()) {
        return false;
    }
    
    // Simulate broadcasting to all connected nodes
    for (const auto& pair : connected_nodes_) {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.messages_sent++;
        stats_.bytes_sent += msg.data.size() + 64;
    }
    
    return true;
}

bool ClusterConnection::forward_transaction(const std::vector<uint8_t>& transaction_data) {
    ClusterMessage tx_msg;
    tx_msg.type = ClusterMessageType::TRANSACTION_FORWARD;
    tx_msg.sender_id = node_id_;
    tx_msg.data = transaction_data;
    tx_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    return broadcast_message(tx_msg);
}

bool ClusterConnection::announce_block(uint64_t slot, const std::string& block_hash, const std::vector<uint8_t>& block_data) {
    ClusterMessage block_msg;
    block_msg.type = ClusterMessageType::BLOCK_ANNOUNCEMENT;
    block_msg.sender_id = node_id_;
    
    // Create block announcement data
    std::ostringstream block_info;
    block_info << "{\"slot\":" << slot << ",\"hash\":\"" << block_hash << "\",\"size\":" << block_data.size() << "}";
    std::string block_info_str = block_info.str();
    block_msg.data = std::vector<uint8_t>(block_info_str.begin(), block_info_str.end());
    
    block_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Update current slot
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.current_slot = slot;
    }
    
    return broadcast_message(block_msg);
}

std::vector<ClusterNode> ClusterConnection::get_connected_nodes() const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    std::vector<ClusterNode> nodes;
    for (const auto& pair : connected_nodes_) {
        nodes.push_back(*pair.second);
    }
    return nodes;
}

size_t ClusterConnection::get_connection_count() const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    return connected_nodes_.size();
}

ClusterStats ClusterConnection::get_stats() const {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    ClusterStats stats_copy = stats_;
    stats_copy.connected_nodes = get_connection_count();
    return stats_copy;
}

std::string ClusterConnection::get_network_info() const {
    auto stats = get_stats();
    std::ostringstream info;
    info << "Network: " << network_type_to_string(network_type_) << std::endl;
    info << "Node ID: " << node_id_ << std::endl;
    info << "Connected Nodes: " << stats.connected_nodes << std::endl;
    info << "Messages Sent: " << stats.messages_sent << std::endl;
    info << "Messages Received: " << stats.messages_received << std::endl;
    info << "Uptime: " << stats.uptime_seconds << " seconds" << std::endl;
    info << "Current Slot: " << stats.current_slot << std::endl;
    return info.str();
}

void ClusterConnection::set_message_callback(std::function<void(const ClusterMessage&)> callback) {
    message_callback_ = std::move(callback);
}

void ClusterConnection::set_node_discovered_callback(std::function<void(const ClusterNode&)> callback) {
    node_discovered_callback_ = std::move(callback);
}

void ClusterConnection::set_node_disconnected_callback(std::function<void(const std::string&)> callback) {
    node_disconnected_callback_ = std::move(callback);
}

bool ClusterConnection::add_bootstrap_node(const std::string& address) {
    if (!cluster_utils::is_valid_cluster_address(address)) {
        return false;
    }
    
    bootstrap_nodes_.push_back(address);
    std::cout << "Added bootstrap node: " << address << std::endl;
    return true;
}

void ClusterConnection::initiate_discovery() {
    std::cout << "Initiating cluster discovery..." << std::endl;
    
    // Send discovery messages to all connected nodes
    ClusterMessage discovery_msg;
    discovery_msg.type = ClusterMessageType::CLUSTER_INFO;
    discovery_msg.sender_id = node_id_;
    discovery_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    broadcast_message(discovery_msg);
}
std::string ClusterConnection::network_type_to_string(NetworkType type) {
    switch (type) {
        case NetworkType::MAINNET: return "mainnet";
        case NetworkType::TESTNET: return "testnet";
        case NetworkType::DEVNET: return "devnet";
        case NetworkType::LOCALNET: return "localnet";
        default: return "unknown";
    }
}

NetworkType ClusterConnection::string_to_network_type(const std::string& type) {
    if (type == "mainnet") return NetworkType::MAINNET;
    if (type == "testnet") return NetworkType::TESTNET;
    if (type == "devnet") return NetworkType::DEVNET;
    if (type == "localnet") return NetworkType::LOCALNET;
    return NetworkType::DEVNET; // Default to devnet
}

std::vector<std::string> ClusterConnection::get_default_bootstrap_nodes(NetworkType type) {
    switch (type) {
        case NetworkType::MAINNET:
            return {
                "entrypoint.mainnet-beta.solana.com:8001",
                "entrypoint2.mainnet-beta.solana.com:8001",
                "entrypoint3.mainnet-beta.solana.com:8001"
            };
        case NetworkType::TESTNET:
            return {
                "entrypoint.testnet.solana.com:8001",
                "entrypoint2.testnet.solana.com:8001",
                "entrypoint3.testnet.solana.com:8001"
            };
        case NetworkType::DEVNET:
            return {
                "entrypoint.devnet.solana.com:8001",
                "entrypoint2.devnet.solana.com:8001",
                "entrypoint3.devnet.solana.com:8001"
            };
        case NetworkType::LOCALNET:
            return {
                "127.0.0.1:8001",
                "127.0.0.1:8002",
                "127.0.0.1:8003"
            };
        default:
            return {};
    }
}

// Utility functions implementation
namespace cluster_utils {
    std::string generate_node_id() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);
        
        std::string node_id = "node_";
        for (int i = 0; i < 16; ++i) {
            node_id += "0123456789abcdef"[dis(gen)];
        }
        return node_id;
    }
    
    uint16_t find_available_port(uint16_t start_port) {
        // Simple implementation - in real code would check if port is available
        static uint16_t current_port = start_port;
        return current_port++;
    }
    
    bool is_valid_cluster_address(const std::string& address) {
        size_t colon_pos = address.find(':');
        if (colon_pos == std::string::npos) return false;
        
        std::string ip = address.substr(0, colon_pos);
        std::string port_str = address.substr(colon_pos + 1);
        
        // Basic validation
        return !ip.empty() && !port_str.empty();
    }
    
    std::vector<uint8_t> create_handshake_message(const std::string& node_id, const std::string& version) {
        std::string handshake = "{\"type\":\"handshake\",\"node_id\":\"" + node_id + 
                               "\",\"version\":\"" + version + "\"}";
        return std::vector<uint8_t>(handshake.begin(), handshake.end());
    }
    
    std::vector<uint8_t> create_ping_message() {
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::string ping = "{\"type\":\"ping\",\"timestamp\":" + std::to_string(timestamp) + "}";
        return std::vector<uint8_t>(ping.begin(), ping.end());
    }
    
    std::vector<uint8_t> create_pong_message(const std::string& ping_id) {
        std::string pong = "{\"type\":\"pong\",\"ping_id\":\"" + ping_id + "\"}";
        return std::vector<uint8_t>(pong.begin(), pong.end());
    }
}

}} // namespace slonana::network