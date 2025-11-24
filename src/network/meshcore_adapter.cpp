/**
 * MeshCore Adapter Implementation
 * 
 * Provides mesh networking capabilities on top of existing slonana networking
 * Uses QUIC as primary transport with TCP fallback
 */

#include "network/meshcore_adapter.h"
#include "network/cluster_connection.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>

namespace slonana {
namespace network {
namespace meshcore {

using namespace std::chrono;

/**
 * Internal implementation class
 */
class MeshCoreAdapter::Impl {
public:
    explicit Impl(const MeshConfig& config)
        : config_(config), running_(false), joined_(false) {
        
        if (config_.node_id.empty()) {
            // Generate a random node ID if not provided
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 0xFFFFFFFF);
            config_.node_id = "node_" + std::to_string(dis(gen));
        }
    }
    
    ~Impl() {
        stop();
    }
    
    Result<bool> start() {
        if (running_) {
            return Result<bool>("MeshCore already running");
        }
        
        if (!config_.enabled) {
            return Result<bool>("MeshCore is disabled in configuration");
        }
        
        running_ = true;
        start_time_ = steady_clock::now();
        
        // Start background threads
        heartbeat_thread_ = std::thread(&Impl::heartbeat_loop, this);
        discovery_thread_ = std::thread(&Impl::discovery_loop, this);
        
        std::cout << "[MeshCore] Started mesh adapter for node: " << config_.node_id << std::endl;
        return Result<bool>(true, success_tag{});
    }
    
    void stop() {
        if (!running_) {
            return;
        }
        
        running_ = false;
        joined_ = false;
        
        // Wait for threads to finish
        cv_.notify_all();
        if (heartbeat_thread_.joinable()) {
            heartbeat_thread_.join();
        }
        if (discovery_thread_.joinable()) {
            discovery_thread_.join();
        }
        
        // Clear all peers
        {
            std::lock_guard<std::mutex> lock(peers_mutex_);
            peers_.clear();
        }
        
        std::cout << "[MeshCore] Stopped mesh adapter" << std::endl;
    }
    
    bool is_running() const {
        return running_;
    }
    
    Result<bool> join_mesh() {
        if (!running_) {
            return Result<bool>("MeshCore not running");
        }
        
        if (joined_) {
            return Result<bool>(true, success_tag{});
        }
        
        auto join_start = steady_clock::now();
        
        // Connect to bootstrap nodes
        for (const auto& bootstrap : config_.bootstrap_nodes) {
            auto pos = bootstrap.find(':');
            if (pos != std::string::npos) {
                std::string address = bootstrap.substr(0, pos);
                uint16_t port = std::stoi(bootstrap.substr(pos + 1));
                
                // Attempt connection
                connect_to_peer_internal(address, port);
            }
        }
        
        joined_ = true;
        stats_.mesh_joins++;
        
        auto join_duration = duration_cast<milliseconds>(steady_clock::now() - join_start);
        std::cout << "[MeshCore] Joined mesh in " << join_duration.count() << "ms" << std::endl;
        
        return Result<bool>(true, success_tag{});
    }
    
    Result<bool> leave_mesh() {
        if (!joined_) {
            return Result<bool>(true, success_tag{});
        }
        
        // Disconnect from all peers
        std::vector<std::string> peer_ids;
        {
            std::lock_guard<std::mutex> lock(peers_mutex_);
            for (const auto& [id, node] : peers_) {
                peer_ids.push_back(id);
            }
        }
        
        for (const auto& peer_id : peer_ids) {
            disconnect_from_peer_internal(peer_id);
        }
        
        joined_ = false;
        stats_.mesh_leaves++;
        
        std::cout << "[MeshCore] Left mesh" << std::endl;
        return Result<bool>(true, success_tag{});
    }
    
    bool is_joined() const {
        return joined_;
    }
    
    Result<bool> send_message(const MeshMessage& message) {
        if (!joined_) {
            return Result<bool>("Not joined to mesh");
        }
        
        // Find target peer
        std::lock_guard<std::mutex> lock(peers_mutex_);
        auto it = peers_.find(message.receiver_id);
        if (it == peers_.end()) {
            return Result<bool>("Peer not found: " + message.receiver_id);
        }
        
        // Send message (simplified - in production would use QUIC/TCP)
        stats_.messages_sent++;
        stats_.bytes_sent += message.payload.size();
        it->second.messages_sent++;
        
        // Call registered handlers
        auto handler_it = message_handlers_.find(message.type);
        if (handler_it != message_handlers_.end()) {
            handler_it->second(message);
        }
        
        return Result<bool>(true, success_tag{});
    }
    
    Result<bool> broadcast_message(const MeshMessage& message) {
        if (!joined_) {
            return Result<bool>("Not joined to mesh");
        }
        
        // Send to all connected peers
        std::vector<std::string> peer_ids;
        {
            std::lock_guard<std::mutex> lock(peers_mutex_);
            for (const auto& [id, node] : peers_) {
                if (node.state == NodeState::CONNECTED) {
                    peer_ids.push_back(id);
                }
            }
        }
        
        for (const auto& peer_id : peer_ids) {
            MeshMessage peer_msg = message;
            peer_msg.receiver_id = peer_id;
            send_message(peer_msg);
        }
        
        return Result<bool>(true, success_tag{});
    }
    
    void register_message_handler(MeshMessageType type, MessageHandler handler) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        message_handlers_[type] = handler;
    }
    
    void register_error_handler(ErrorHandler handler) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        error_handler_ = handler;
    }
    
    std::vector<MeshNode> get_connected_peers() const {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        std::vector<MeshNode> result;
        for (const auto& [id, node] : peers_) {
            if (node.state == NodeState::CONNECTED) {
                result.push_back(node);
            }
        }
        return result;
    }
    
    std::optional<MeshNode> get_peer_info(const std::string& node_id) const {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        auto it = peers_.find(node_id);
        if (it != peers_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    Result<bool> connect_to_peer(const std::string& address, uint16_t port) {
        return connect_to_peer_internal(address, port);
    }
    
    Result<bool> disconnect_from_peer(const std::string& node_id) {
        return disconnect_from_peer_internal(node_id);
    }
    
    MeshStats get_stats() const {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        
        MeshStats stats = stats_;
        stats.total_nodes = peers_.size();
        stats.connected_nodes = 0;
        
        std::vector<uint32_t> latencies;
        for (const auto& [id, node] : peers_) {
            if (node.state == NodeState::CONNECTED) {
                stats.connected_nodes++;
                latencies.push_back(node.latency_ms);
            }
        }
        
        // Calculate latency statistics
        if (!latencies.empty()) {
            std::sort(latencies.begin(), latencies.end());
            uint64_t sum = 0;
            for (auto lat : latencies) {
                sum += lat;
            }
            stats.average_latency_ms = sum / latencies.size();
            stats.p50_latency_ms = latencies[latencies.size() / 2];
            stats.p95_latency_ms = latencies[latencies.size() * 95 / 100];
        }
        
        stats.uptime = steady_clock::now() - start_time_;
        
        return stats;
    }
    
    std::vector<std::string> get_topology() const {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        std::vector<std::string> topology;
        
        for (const auto& [id, node] : peers_) {
            std::string entry = "Node: " + id + ", State: ";
            switch (node.state) {
                case NodeState::CONNECTED: entry += "CONNECTED"; break;
                case NodeState::CONNECTING: entry += "CONNECTING"; break;
                case NodeState::DISCONNECTED: entry += "DISCONNECTED"; break;
                case NodeState::RECONNECTING: entry += "RECONNECTING"; break;
                case NodeState::FAILED: entry += "FAILED"; break;
            }
            entry += ", Latency: " + std::to_string(node.latency_ms) + "ms";
            topology.push_back(entry);
        }
        
        return topology;
    }

private:
    Result<bool> connect_to_peer_internal(const std::string& address, uint16_t port) {
        std::string node_id = address + ":" + std::to_string(port);
        
        std::lock_guard<std::mutex> lock(peers_mutex_);
        
        // Check if already connected
        auto it = peers_.find(node_id);
        if (it != peers_.end() && it->second.state == NodeState::CONNECTED) {
            return Result<bool>(true, success_tag{});
        }
        
        // Create new peer node
        MeshNode node;
        node.node_id = node_id;
        node.address = address;
        node.port = port;
        node.state = NodeState::CONNECTING;
        node.transport = config_.preferred_transport;
        node.last_seen = steady_clock::now();
        node.messages_sent = 0;
        node.messages_received = 0;
        node.latency_ms = 0;
        node.is_direct_peer = true;
        
        peers_[node_id] = node;
        
        // Simulate connection (in production, use QUIC/TCP)
        std::thread([this, node_id]() {
            std::this_thread::sleep_for(milliseconds(100 + (rand() % 200)));
            
            std::lock_guard<std::mutex> lock(peers_mutex_);
            auto it = peers_.find(node_id);
            if (it != peers_.end()) {
                it->second.state = NodeState::CONNECTED;
                it->second.latency_ms = 20 + (rand() % 30);  // Simulate 20-50ms latency
            }
        }).detach();
        
        return Result<bool>(true, success_tag{});
    }
    
    Result<bool> disconnect_from_peer_internal(const std::string& node_id) {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        
        auto it = peers_.find(node_id);
        if (it == peers_.end()) {
            return Result<bool>("Peer not found");
        }
        
        it->second.state = NodeState::DISCONNECTED;
        peers_.erase(it);
        
        return Result<bool>(true, success_tag{});
    }
    
    void heartbeat_loop() {
        while (running_) {
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait_for(lock, milliseconds(config_.heartbeat_interval_ms), 
                        [this] { return !running_; });
            
            if (!running_) break;
            
            // Send heartbeats to all connected peers
            std::vector<std::string> peer_ids;
            {
                std::lock_guard<std::mutex> plock(peers_mutex_);
                for (const auto& [id, node] : peers_) {
                    if (node.state == NodeState::CONNECTED) {
                        peer_ids.push_back(id);
                    }
                }
            }
            
            for (const auto& peer_id : peer_ids) {
                MeshMessage heartbeat;
                heartbeat.type = MeshMessageType::HEARTBEAT;
                heartbeat.sender_id = config_.node_id;
                heartbeat.receiver_id = peer_id;
                heartbeat.timestamp = duration_cast<milliseconds>(
                    steady_clock::now().time_since_epoch()).count();
                
                send_message(heartbeat);
            }
        }
    }
    
    void discovery_loop() {
        while (running_) {
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait_for(lock, milliseconds(config_.mesh_discovery_interval_ms),
                        [this] { return !running_; });
            
            if (!running_) break;
            
            // Perform mesh discovery (simplified)
            std::lock_guard<std::mutex> plock(peers_mutex_);
            uint64_t connected = 0;
            for (const auto& [id, node] : peers_) {
                if (node.state == NodeState::CONNECTED) {
                    connected++;
                }
            }
            
            // If below desired peers, attempt to discover more
            if (connected < config_.desired_direct_peers) {
                std::cout << "[MeshCore] Discovery: " << connected << "/" 
                         << config_.desired_direct_peers << " peers" << std::endl;
            }
        }
    }
    
    MeshConfig config_;
    std::atomic<bool> running_;
    std::atomic<bool> joined_;
    steady_clock::time_point start_time_;
    
    // Peers
    mutable std::mutex peers_mutex_;
    std::unordered_map<std::string, MeshNode> peers_;
    
    // Message handlers
    mutable std::mutex handlers_mutex_;
    std::unordered_map<MeshMessageType, MessageHandler> message_handlers_;
    ErrorHandler error_handler_;
    
    // Statistics
    MeshStats stats_{};
    
    // Threads
    std::thread heartbeat_thread_;
    std::thread discovery_thread_;
    std::mutex cv_mutex_;
    std::condition_variable cv_;
};

// Main class implementation
MeshCoreAdapter::MeshCoreAdapter(const MeshConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

MeshCoreAdapter::~MeshCoreAdapter() = default;

Result<bool> MeshCoreAdapter::start() {
    return impl_->start();
}

void MeshCoreAdapter::stop() {
    impl_->stop();
}

bool MeshCoreAdapter::is_running() const {
    return impl_->is_running();
}

Result<bool> MeshCoreAdapter::join_mesh() {
    return impl_->join_mesh();
}

Result<bool> MeshCoreAdapter::leave_mesh() {
    return impl_->leave_mesh();
}

bool MeshCoreAdapter::is_joined() const {
    return impl_->is_joined();
}

Result<bool> MeshCoreAdapter::send_message(const MeshMessage& message) {
    return impl_->send_message(message);
}

Result<bool> MeshCoreAdapter::broadcast_message(const MeshMessage& message) {
    return impl_->broadcast_message(message);
}

void MeshCoreAdapter::register_message_handler(MeshMessageType type, MessageHandler handler) {
    impl_->register_message_handler(type, handler);
}

void MeshCoreAdapter::register_error_handler(ErrorHandler handler) {
    impl_->register_error_handler(handler);
}

std::vector<MeshNode> MeshCoreAdapter::get_connected_peers() const {
    return impl_->get_connected_peers();
}

std::optional<MeshNode> MeshCoreAdapter::get_peer_info(const std::string& node_id) const {
    return impl_->get_peer_info(node_id);
}

Result<bool> MeshCoreAdapter::connect_to_peer(const std::string& address, uint16_t port) {
    return impl_->connect_to_peer(address, port);
}

Result<bool> MeshCoreAdapter::disconnect_from_peer(const std::string& node_id) {
    return impl_->disconnect_from_peer(node_id);
}

MeshStats MeshCoreAdapter::get_stats() const {
    return impl_->get_stats();
}

std::vector<std::string> MeshCoreAdapter::get_topology() const {
    return impl_->get_topology();
}

// Factory function
std::unique_ptr<MeshCoreAdapter> create_meshcore_adapter(const ValidatorConfig& config) {
    MeshConfig mesh_config;
    mesh_config.enabled = false;  // Default: disabled until explicitly enabled
    mesh_config.node_id = config.node_address;  // Use validator node address as ID
    mesh_config.listen_port = config.node_port;
    
    // Bootstrap from known validators (if any)
    mesh_config.bootstrap_nodes = config.bootstrap_entrypoints;
    
    return std::make_unique<MeshCoreAdapter>(mesh_config);
}

} // namespace meshcore
} // namespace network
} // namespace slonana
