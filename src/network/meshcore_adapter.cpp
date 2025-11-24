/**
 * MeshCore Adapter Implementation
 *
 * Provides mesh networking capabilities on top of existing slonana networking
 * Uses QUIC as primary transport with TCP fallback
 */

#include "network/meshcore_adapter.h"
#include "network/cluster_connection.h"
#include "network/quic_server.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>
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
  explicit Impl(const MeshConfig &config)
      : config_(config), running_(false), joined_(false) {

    if (config_.node_id.empty()) {
      // Generate a random node ID if not provided
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<> dis(0, 0xFFFFFFFF);
      config_.node_id = "node_" + std::to_string(dis(gen));
    }

    // Initialize QUIC listener if port is configured
    if (config_.listen_port > 0) {
      quic_listener_ = std::make_unique<QuicListener>(config_.listen_port);
    }
  }

  ~Impl() { stop(); }

  Result<bool> start() {
    if (running_) {
      return Result<bool>("MeshCore already running");
    }

    if (!config_.enabled) {
      return Result<bool>("MeshCore is disabled in configuration");
    }

    // Start QUIC listener if configured
    if (quic_listener_ && !quic_listener_->start()) {
      return Result<bool>("Failed to start QUIC listener on port " +
                          std::to_string(config_.listen_port));
    }

    running_ = true;
    start_time_ = steady_clock::now();

    // Start background threads
    heartbeat_thread_ = std::thread(&Impl::heartbeat_loop, this);
    discovery_thread_ = std::thread(&Impl::discovery_loop, this);
    network_io_thread_ = std::thread(&Impl::network_io_loop, this);

    std::cout << "[MeshCore] Started mesh adapter for node: " << config_.node_id
              << " on port: " << config_.listen_port << std::endl;
    return Result<bool>(true, success_tag{});
  }

  void stop() {
    if (!running_) {
      return;
    }

    running_ = false;
    joined_ = false;

    // Stop QUIC listener
    if (quic_listener_) {
      quic_listener_->stop();
    }

    // Wait for threads to finish
    cv_.notify_all();
    if (heartbeat_thread_.joinable()) {
      heartbeat_thread_.join();
    }
    if (discovery_thread_.joinable()) {
      discovery_thread_.join();
    }
    if (network_io_thread_.joinable()) {
      network_io_thread_.join();
    }

    // Clear all peers and connections
    {
      std::lock_guard<std::mutex> lock(peers_mutex_);
      for (auto &[id, node] : peers_) {
        if (node.connection) {
          node.connection->disconnect();
        }
      }
      peers_.clear();
    }

    std::cout << "[MeshCore] Stopped mesh adapter" << std::endl;
  }

  bool is_running() const { return running_; }

  Result<bool> join_mesh() {
    if (!running_) {
      return Result<bool>("MeshCore not running");
    }

    if (joined_) {
      return Result<bool>(true, success_tag{});
    }

    auto join_start = steady_clock::now();

    // Connect to bootstrap nodes
    for (const auto &bootstrap : config_.bootstrap_nodes) {
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

    auto join_duration =
        duration_cast<milliseconds>(steady_clock::now() - join_start);
    std::cout << "[MeshCore] Joined mesh in " << join_duration.count() << "ms"
              << std::endl;

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
      for (const auto &[id, node] : peers_) {
        peer_ids.push_back(id);
      }
    }

    for (const auto &peer_id : peer_ids) {
      disconnect_from_peer_internal(peer_id);
    }

    joined_ = false;
    stats_.mesh_leaves++;

    std::cout << "[MeshCore] Left mesh" << std::endl;
    return Result<bool>(true, success_tag{});
  }

  bool is_joined() const { return joined_; }

  Result<bool> send_message(const MeshMessage &message) {
    if (!joined_) {
      return Result<bool>("Not joined to mesh");
    }

    // Find target peer
    std::lock_guard<std::mutex> lock(peers_mutex_);
    auto it = peers_.find(message.receiver_id);
    if (it == peers_.end()) {
      return Result<bool>("Peer not found: " + message.receiver_id);
    }

    if (it->second.state != NodeState::CONNECTED) {
      return Result<bool>("Peer not connected: " + message.receiver_id);
    }

    // Serialize message
    std::vector<uint8_t> serialized = serialize_mesh_message(message);

    if (!config_.test_mode) {
      // Real QUIC send
      if (!it->second.connection) {
        return Result<bool>("No connection to: " + message.receiver_id);
      }

      // Send via QUIC stream
      auto stream = it->second.connection->create_stream();
      if (!stream) {
        return Result<bool>("Failed to create stream to: " +
                            message.receiver_id);
      }

      if (!stream->send_data(serialized)) {
        return Result<bool>("Failed to send data to: " + message.receiver_id);
      }
    }

    // Update statistics
    stats_.messages_sent++;
    stats_.bytes_sent += serialized.size();
    it->second.messages_sent++;
    it->second.last_seen = steady_clock::now();

    // Call registered handlers (for test mode)
    if (config_.test_mode) {
      auto handler_it = message_handlers_.find(message.type);
      if (handler_it != message_handlers_.end()) {
        handler_it->second(message);
      }
    }

    return Result<bool>(true, success_tag{});
  }

  Result<bool> broadcast_message(const MeshMessage &message) {
    if (!joined_) {
      return Result<bool>("Not joined to mesh");
    }

    // Send to all connected peers
    std::vector<std::string> peer_ids;
    {
      std::lock_guard<std::mutex> lock(peers_mutex_);
      for (const auto &[id, node] : peers_) {
        if (node.state == NodeState::CONNECTED) {
          peer_ids.push_back(id);
        }
      }
    }

    for (const auto &peer_id : peer_ids) {
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
    for (const auto &[id, node] : peers_) {
      if (node.state == NodeState::CONNECTED) {
        result.push_back(node);
      }
    }
    return result;
  }

  std::optional<MeshNode> get_peer_info(const std::string &node_id) const {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    auto it = peers_.find(node_id);
    if (it != peers_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  Result<bool> connect_to_peer(const std::string &address, uint16_t port) {
    return connect_to_peer_internal(address, port);
  }

  Result<bool> disconnect_from_peer(const std::string &node_id) {
    return disconnect_from_peer_internal(node_id);
  }

  MeshStats get_stats() const {
    std::lock_guard<std::mutex> lock(peers_mutex_);

    MeshStats stats = stats_;
    stats.total_nodes = peers_.size();
    stats.connected_nodes = 0;

    std::vector<uint32_t> latencies;
    for (const auto &[id, node] : peers_) {
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

    for (const auto &[id, node] : peers_) {
      std::string entry = "Node: " + id + ", State: ";
      switch (node.state) {
      case NodeState::CONNECTED:
        entry += "CONNECTED";
        break;
      case NodeState::CONNECTING:
        entry += "CONNECTING";
        break;
      case NodeState::DISCONNECTED:
        entry += "DISCONNECTED";
        break;
      case NodeState::RECONNECTING:
        entry += "RECONNECTING";
        break;
      case NodeState::FAILED:
        entry += "FAILED";
        break;
      }
      entry += ", Latency: " + std::to_string(node.latency_ms) + "ms";
      topology.push_back(entry);
    }

    return topology;
  }

private:
  Result<bool> connect_to_peer_internal(const std::string &address,
                                        uint16_t port) {
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

    // Create actual QUIC connection (unless in test mode)
    if (!config_.test_mode) {
      node.connection = std::make_shared<QuicConnection>(address, port);
    }

    peers_[node_id] = node;

    // Connect asynchronously
    std::thread([this, node_id]() {
      std::lock_guard<std::mutex> lock(peers_mutex_);
      auto it = peers_.find(node_id);
      if (it != peers_.end()) {
        if (config_.test_mode) {
          // Simulate connection for testing
          std::random_device rd;
          std::mt19937 gen(rd());
          std::uniform_int_distribution<> latency_dist(20, 50);

          it->second.state = NodeState::CONNECTED;
          it->second.latency_ms = latency_dist(gen);
          std::cout << "[MeshCore] Simulated connection to " << node_id
                    << " (test mode, latency: " << it->second.latency_ms
                    << "ms)" << std::endl;
        } else {
          // Attempt real QUIC connection
          auto start_time = steady_clock::now();
          bool connected =
              it->second.connection && it->second.connection->connect();

          if (connected) {
            it->second.state = NodeState::CONNECTED;
            auto connect_time = steady_clock::now() - start_time;
            it->second.latency_ms =
                duration_cast<milliseconds>(connect_time).count();
            std::cout << "[MeshCore] Connected to " << node_id
                      << " via QUIC (latency: " << it->second.latency_ms
                      << "ms)" << std::endl;
          } else {
            it->second.state = NodeState::FAILED;
            std::cerr << "[MeshCore] Failed to connect to " << node_id
                      << std::endl;
          }
        }
      }
    }).detach();

    return Result<bool>(true, success_tag{});
  }

  Result<bool> disconnect_from_peer_internal(const std::string &node_id) {
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

      if (!running_)
        break;

      // Send heartbeats to all connected peers
      std::vector<std::string> peer_ids;
      {
        std::lock_guard<std::mutex> plock(peers_mutex_);
        for (const auto &[id, node] : peers_) {
          if (node.state == NodeState::CONNECTED) {
            peer_ids.push_back(id);
          }
        }
      }

      for (const auto &peer_id : peer_ids) {
        MeshMessage heartbeat;
        heartbeat.type = MeshMessageType::HEARTBEAT;
        heartbeat.sender_id = config_.node_id;
        heartbeat.receiver_id = peer_id;
        heartbeat.timestamp =
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
                .count();

        send_message(heartbeat);
      }
    }
  }

  void discovery_loop() {
    while (running_) {
      std::unique_lock<std::mutex> lock(cv_mutex_);
      cv_.wait_for(lock, milliseconds(config_.mesh_discovery_interval_ms),
                   [this] { return !running_; });

      if (!running_)
        break;

      // Perform mesh discovery (simplified)
      std::lock_guard<std::mutex> plock(peers_mutex_);
      uint64_t connected = 0;
      for (const auto &[id, node] : peers_) {
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

  void network_io_loop() {
    while (running_) {
      std::this_thread::sleep_for(milliseconds(100));

      if (!running_)
        break;

      // Process incoming messages from all peers
      std::vector<std::pair<std::string, std::shared_ptr<QuicConnection>>>
          active_connections;
      {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        for (const auto &[id, node] : peers_) {
          if (node.state == NodeState::CONNECTED && node.connection) {
            active_connections.push_back({id, node.connection});
          }
        }
      }

      for (const auto &[node_id, conn] : active_connections) {
        // Check active streams for incoming data
        // Use connection's stream count instead of magic number
        size_t stream_count = conn->get_stream_count();
        for (uint64_t stream_id = 0; stream_id < stream_count + 10;
             stream_id++) {
          auto stream = conn->get_stream(stream_id);
          if (stream && !stream->is_closed()) {
            auto data = stream->receive_data();
            if (!data.empty()) {
              // Deserialize and dispatch message
              auto message = deserialize_mesh_message(data);
              if (!message.sender_id.empty()) {
                dispatch_message(node_id, message);
              }
            }
          }
        }
      }
    }
  }

  void dispatch_message(const std::string &sender_node_id,
                        const MeshMessage &message) {
    // Update statistics
    stats_.messages_received++;
    stats_.bytes_received += message.payload.size();

    // Update peer's last seen time
    {
      std::lock_guard<std::mutex> lock(peers_mutex_);
      auto it = peers_.find(sender_node_id);
      if (it != peers_.end()) {
        it->second.messages_received++;
        it->second.last_seen = steady_clock::now();
      }
    }

    // Dispatch to registered handler
    auto handler_it = message_handlers_.find(message.type);
    if (handler_it != message_handlers_.end()) {
      try {
        handler_it->second(message);
      } catch (const std::exception &e) {
        // Call error handler if registered
        if (error_handler_) {
          error_handler_(sender_node_id,
                         "Handler exception: " + std::string(e.what()));
        }
      }
    }

    // Handle specific message types
    switch (message.type) {
    case MeshMessageType::HEARTBEAT:
      // Heartbeat received - peer is alive
      break;
    case MeshMessageType::DISCOVERY:
      // Process peer discovery information
      handle_discovery_message(message);
      break;
    case MeshMessageType::TOPOLOGY:
      // Update topology information
      handle_topology_message(message);
      break;
    case MeshMessageType::DATA:
      // Data already dispatched to handler
      break;
    case MeshMessageType::ERROR:
      // Handle error message
      if (error_handler_) {
        std::string error_str(message.payload.begin(), message.payload.end());
        error_handler_(sender_node_id, error_str);
      }
      break;
    }
  }

  void handle_discovery_message(const MeshMessage &message) {
    // Parse discovery payload to find new peers
    // Format: list of peer addresses separated by newlines
    std::string payload_str(message.payload.begin(), message.payload.end());
    std::istringstream iss(payload_str);
    std::string peer_addr;

    // Collect peers to connect to (outside the lock)
    std::vector<std::pair<std::string, uint16_t>> peers_to_connect;

    {
      std::lock_guard<std::mutex> lock(peers_mutex_);
      while (std::getline(iss, peer_addr)) {
        if (!peer_addr.empty() && peer_addr != config_.node_id) {
          // Extract address and port
          auto colon_pos = peer_addr.find(':');
          if (colon_pos != std::string::npos) {
            std::string addr = peer_addr.substr(0, colon_pos);
            uint16_t port = std::stoi(peer_addr.substr(colon_pos + 1));

            // Check if we should connect to this peer
            if (peers_.find(peer_addr) == peers_.end() &&
                peers_.size() < config_.max_direct_peers) {
              peers_to_connect.push_back({addr, port});
            }
          }
        }
      }
    }

    // Connect to discovered peers (without holding the lock)
    for (const auto &[addr, port] : peers_to_connect) {
      connect_to_peer_internal(addr, port);
    }
  }

  void handle_topology_message(const MeshMessage &message) {
    // Update mesh topology information for routing decisions
    // Parse topology payload containing node connectivity information
    if (message.payload.empty()) {
      return;
    }

    // Topology format: JSON-like structure or simple text
    // For now, log the topology update
    std::string topology_info(message.payload.begin(), message.payload.end());
    std::cout << "[MeshCore] Topology update from " << message.sender_id << ": "
              << topology_info.substr(0, 100) << "..." << std::endl;

    // Statistics update
    stats_.reconnections++; // Track topology changes
  }

  std::vector<uint8_t> serialize_mesh_message(const MeshMessage &message) {
    std::vector<uint8_t> data;

    // Simple serialization format:
    // [1 byte: type][8 bytes: timestamp][4 bytes: ttl][2 bytes:
    // sender_id_len][sender_id][2 bytes: receiver_id_len][receiver_id][4 bytes:
    // payload_len][payload]

    data.push_back(static_cast<uint8_t>(message.type));

    // Timestamp (8 bytes)
    for (int i = 7; i >= 0; i--) {
      data.push_back((message.timestamp >> (i * 8)) & 0xFF);
    }

    // TTL (4 bytes)
    for (int i = 3; i >= 0; i--) {
      data.push_back((message.ttl >> (i * 8)) & 0xFF);
    }

    // Sender ID
    uint16_t sender_len = message.sender_id.length();
    data.push_back((sender_len >> 8) & 0xFF);
    data.push_back(sender_len & 0xFF);
    data.insert(data.end(), message.sender_id.begin(), message.sender_id.end());

    // Receiver ID
    uint16_t receiver_len = message.receiver_id.length();
    data.push_back((receiver_len >> 8) & 0xFF);
    data.push_back(receiver_len & 0xFF);
    data.insert(data.end(), message.receiver_id.begin(),
                message.receiver_id.end());

    // Payload
    uint32_t payload_len = message.payload.size();
    for (int i = 3; i >= 0; i--) {
      data.push_back((payload_len >> (i * 8)) & 0xFF);
    }
    data.insert(data.end(), message.payload.begin(), message.payload.end());

    return data;
  }

  MeshMessage deserialize_mesh_message(const std::vector<uint8_t> &data) {
    MeshMessage message;
    size_t pos = 0;

    if (data.size() < 15) { // Minimum size
      return message;
    }

    // Type
    message.type = static_cast<MeshMessageType>(data[pos++]);

    // Timestamp
    message.timestamp = 0;
    for (int i = 0; i < 8; i++) {
      message.timestamp = (message.timestamp << 8) | data[pos++];
    }

    // TTL
    message.ttl = 0;
    for (int i = 0; i < 4; i++) {
      message.ttl = (message.ttl << 8) | data[pos++];
    }

    // Sender ID
    uint16_t sender_len = (data[pos] << 8) | data[pos + 1];
    pos += 2;
    message.sender_id =
        std::string(data.begin() + pos, data.begin() + pos + sender_len);
    pos += sender_len;

    // Receiver ID
    uint16_t receiver_len = (data[pos] << 8) | data[pos + 1];
    pos += 2;
    message.receiver_id =
        std::string(data.begin() + pos, data.begin() + pos + receiver_len);
    pos += receiver_len;

    // Payload
    uint32_t payload_len = 0;
    for (int i = 0; i < 4; i++) {
      payload_len = (payload_len << 8) | data[pos++];
    }
    message.payload = std::vector<uint8_t>(data.begin() + pos,
                                           data.begin() + pos + payload_len);

    return message;
  }

  MeshConfig config_;
  std::atomic<bool> running_;
  std::atomic<bool> joined_;
  steady_clock::time_point start_time_;

  // QUIC listener for incoming connections
  std::unique_ptr<QuicListener> quic_listener_;

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
  std::thread network_io_thread_;
  std::mutex cv_mutex_;
  std::condition_variable cv_;
};

// Main class implementation
MeshCoreAdapter::MeshCoreAdapter(const MeshConfig &config)
    : impl_(std::make_unique<Impl>(config)) {}

MeshCoreAdapter::~MeshCoreAdapter() = default;

Result<bool> MeshCoreAdapter::start() { return impl_->start(); }

void MeshCoreAdapter::stop() { impl_->stop(); }

bool MeshCoreAdapter::is_running() const { return impl_->is_running(); }

Result<bool> MeshCoreAdapter::join_mesh() { return impl_->join_mesh(); }

Result<bool> MeshCoreAdapter::leave_mesh() { return impl_->leave_mesh(); }

bool MeshCoreAdapter::is_joined() const { return impl_->is_joined(); }

Result<bool> MeshCoreAdapter::send_message(const MeshMessage &message) {
  return impl_->send_message(message);
}

Result<bool> MeshCoreAdapter::broadcast_message(const MeshMessage &message) {
  return impl_->broadcast_message(message);
}

void MeshCoreAdapter::register_message_handler(MeshMessageType type,
                                               MessageHandler handler) {
  impl_->register_message_handler(type, handler);
}

void MeshCoreAdapter::register_error_handler(ErrorHandler handler) {
  impl_->register_error_handler(handler);
}

std::vector<MeshNode> MeshCoreAdapter::get_connected_peers() const {
  return impl_->get_connected_peers();
}

std::optional<MeshNode>
MeshCoreAdapter::get_peer_info(const std::string &node_id) const {
  return impl_->get_peer_info(node_id);
}

Result<bool> MeshCoreAdapter::connect_to_peer(const std::string &address,
                                              uint16_t port) {
  return impl_->connect_to_peer(address, port);
}

Result<bool> MeshCoreAdapter::disconnect_from_peer(const std::string &node_id) {
  return impl_->disconnect_from_peer(node_id);
}

MeshStats MeshCoreAdapter::get_stats() const { return impl_->get_stats(); }

std::vector<std::string> MeshCoreAdapter::get_topology() const {
  return impl_->get_topology();
}

// Factory function
std::unique_ptr<MeshCoreAdapter>
create_meshcore_adapter(const ValidatorConfig &config) {
  MeshConfig mesh_config;
  mesh_config.enabled = false; // Default: disabled until explicitly enabled

  // Extract node ID from gossip bind address
  mesh_config.node_id = config.gossip_bind_address;

  // Extract port from gossip bind address (format: "127.0.0.1:8001")
  auto colon_pos = config.gossip_bind_address.find(':');
  if (colon_pos != std::string::npos) {
    mesh_config.listen_port =
        std::stoi(config.gossip_bind_address.substr(colon_pos + 1));
  } else {
    mesh_config.listen_port = 8001; // Default gossip port
  }

  // Bootstrap from known validators (if any)
  mesh_config.bootstrap_nodes = config.bootstrap_entrypoints;

  return std::make_unique<MeshCoreAdapter>(mesh_config);
}

} // namespace meshcore
} // namespace network
} // namespace slonana
