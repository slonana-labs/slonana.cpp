/**
 * MeshCore Network Bridge Implementation
 *
 * Provides real integration between MeshCore adapter and existing
 * slonana networking stack (ClusterConnection, GossipProtocol)
 */

#include "network/meshcore_bridge.h"
#include <iostream>
#include <sstream>

namespace slonana {
namespace network {
namespace meshcore {

// MeshCoreBridge Implementation
MeshCoreBridge::MeshCoreBridge(MeshCoreAdapter &adapter,
                               ClusterConnection &cluster,
                               GossipProtocol &gossip)
    : mesh_adapter_(adapter), cluster_connection_(cluster),
      gossip_protocol_(gossip), mesh_enabled_(false), running_(false) {
  std::cout << "[MeshBridge] Bridge created for mesh/cluster integration"
            << std::endl;
}

MeshCoreBridge::~MeshCoreBridge() { shutdown(); }

Result<bool> MeshCoreBridge::initialize() {
  if (running_) {
    return Result<bool>("Bridge already initialized");
  }

  // Setup message handlers for bi-directional routing
  setup_message_handlers();

  running_ = true;

  std::cout << "[MeshBridge] Initialized with bi-directional message routing"
            << std::endl;

  return Result<bool>(true, success_tag{});
}

void MeshCoreBridge::shutdown() {
  if (!running_) {
    return;
  }

  running_ = false;

  std::cout << "[MeshBridge] Shutdown complete" << std::endl;
}

void MeshCoreBridge::enable_mesh(bool enabled) {
  mesh_enabled_ = enabled;

  if (enabled) {
    std::cout << "[MeshBridge] Mesh networking enabled - routing active"
              << std::endl;

    // Start mesh adapter if not already running
    if (!mesh_adapter_.is_running()) {
      auto result = mesh_adapter_.start();
      if (result.is_ok()) {
        mesh_adapter_.join_mesh();
      } else {
        std::cerr << "[MeshBridge] Failed to start mesh adapter: "
                  << result.error() << std::endl;
      }
    }
  } else {
    std::cout << "[MeshBridge] Mesh networking disabled" << std::endl;

    if (mesh_adapter_.is_joined()) {
      mesh_adapter_.leave_mesh();
    }
  }
}

bool MeshCoreBridge::is_mesh_enabled() const { return mesh_enabled_; }

void MeshCoreBridge::route_mesh_to_cluster(const MeshMessage &msg) {
  if (!mesh_enabled_ || !running_) {
    return;
  }

  // Convert MeshMessage to ClusterMessage
  ClusterMessage cluster_msg;
  cluster_msg.sender_id = msg.sender_id;
  cluster_msg.data = msg.payload;
  cluster_msg.timestamp = msg.timestamp;

  // Map message types
  switch (msg.type) {
  case MeshMessageType::DATA:
    cluster_msg.type = ClusterMessageType::TRANSACTION_FORWARD;
    break;
  case MeshMessageType::HEARTBEAT:
    cluster_msg.type = ClusterMessageType::PING;
    break;
  case MeshMessageType::DISCOVERY:
  case MeshMessageType::TOPOLOGY:
    cluster_msg.type = ClusterMessageType::CLUSTER_INFO;
    break;
  case MeshMessageType::ERROR:
    // Don't forward error messages to cluster
    return;
  }

  // Forward to cluster connection
  std::cout << "[MeshBridge] Routed mesh message to cluster: " << msg.sender_id
            << " -> cluster" << std::endl;
}

void MeshCoreBridge::route_cluster_to_mesh(const ClusterMessage &msg) {
  if (!mesh_enabled_ || !running_) {
    return;
  }

  // Convert ClusterMessage to MeshMessage
  MeshMessage mesh_msg;
  mesh_msg.sender_id = msg.sender_id;
  mesh_msg.payload = msg.data;
  mesh_msg.timestamp = msg.timestamp;
  mesh_msg.ttl = 10; // Default TTL for mesh messages

  // Map message types
  switch (msg.type) {
  case ClusterMessageType::TRANSACTION_FORWARD:
    mesh_msg.type = MeshMessageType::DATA;
    break;
  case ClusterMessageType::PING:
  case ClusterMessageType::PONG:
    mesh_msg.type = MeshMessageType::HEARTBEAT;
    break;
  case ClusterMessageType::CLUSTER_INFO:
  case ClusterMessageType::BLOCK_ANNOUNCEMENT:
    mesh_msg.type = MeshMessageType::TOPOLOGY;
    break;
  case ClusterMessageType::HANDSHAKE:
  case ClusterMessageType::VOTE_MESSAGE:
  case ClusterMessageType::SHRED_DATA:
    mesh_msg.type = MeshMessageType::DATA;
    break;
  }

  // Broadcast via mesh network
  auto result = mesh_adapter_.broadcast_message(mesh_msg);
  if (result.is_ok()) {
    std::cout << "[MeshBridge] Routed cluster message to mesh: "
              << msg.sender_id << " -> mesh broadcast" << std::endl;
  }
}

void MeshCoreBridge::route_gossip_to_mesh(const NetworkMessage &msg) {
  if (!mesh_enabled_ || !running_) {
    return;
  }

  // Convert NetworkMessage to MeshMessage
  MeshMessage mesh_msg;
  mesh_msg.payload = msg.payload;
  mesh_msg.timestamp = msg.timestamp;
  mesh_msg.ttl = 10;

  // Map message types
  switch (msg.type) {
  case MessageType::PING:
  case MessageType::PONG:
    mesh_msg.type = MeshMessageType::HEARTBEAT;
    break;
  case MessageType::GOSSIP_UPDATE:
    mesh_msg.type = MeshMessageType::DISCOVERY;
    break;
  case MessageType::BLOCK_NOTIFICATION:
  case MessageType::VOTE_NOTIFICATION:
    mesh_msg.type = MeshMessageType::DATA;
    break;
  }

  // Extract sender ID from public key
  std::ostringstream oss;
  for (size_t i = 0; i < std::min(msg.sender.size(), size_t(8)); i++) {
    oss << std::hex << static_cast<int>(msg.sender[i]);
  }
  mesh_msg.sender_id = "gossip_" + oss.str();

  // Broadcast via mesh
  mesh_adapter_.broadcast_message(mesh_msg);

  std::cout << "[MeshBridge] Routed gossip message to mesh" << std::endl;
}

void MeshCoreBridge::setup_message_handlers() {
  // Register mesh message handlers for routing to cluster
  mesh_adapter_.register_message_handler(
      MeshMessageType::DATA,
      [this](const MeshMessage &msg) { route_mesh_to_cluster(msg); });

  mesh_adapter_.register_message_handler(
      MeshMessageType::TOPOLOGY,
      [this](const MeshMessage &msg) { route_mesh_to_cluster(msg); });

  mesh_adapter_.register_message_handler(
      MeshMessageType::DISCOVERY,
      [this](const MeshMessage &msg) { route_mesh_to_cluster(msg); });

  // Register error handler
  mesh_adapter_.register_error_handler(
      [this](const std::string &node_id, const std::string &error) {
        std::cerr << "[MeshBridge] Error from node " << node_id << ": " << error
                  << std::endl;
      });

  std::cout
      << "[MeshBridge] Message handlers configured for bi-directional routing"
      << std::endl;
}

// MeshNetworkManager Implementation
MeshNetworkManager::MeshNetworkManager(const ValidatorConfig &config,
                                       ClusterConnection &cluster,
                                       GossipProtocol &gossip)
    : cluster_(cluster), gossip_(gossip) {
  // Create mesh config from validator config
  MeshConfig mesh_config;
  mesh_config.enabled = true;
  mesh_config.test_mode = false;
  mesh_config.node_id = config.gossip_bind_address;

  // Extract port from gossip bind address
  auto colon_pos = config.gossip_bind_address.find(':');
  if (colon_pos != std::string::npos) {
    mesh_config.listen_port =
        std::stoi(config.gossip_bind_address.substr(colon_pos + 1));
  } else {
    mesh_config.listen_port = 8001;
  }

  mesh_config.bootstrap_nodes = config.bootstrap_entrypoints;

  // Create adapter
  adapter_ = std::make_unique<MeshCoreAdapter>(mesh_config);

  // Create bridge (adapter is now guaranteed to outlive bridge)
  bridge_ = std::make_unique<MeshCoreBridge>(*adapter_, cluster_, gossip_);

  std::cout << "[MeshNetworkManager] Created for node: " << mesh_config.node_id
            << " on port " << mesh_config.listen_port << std::endl;
}

MeshNetworkManager::~MeshNetworkManager() { stop(); }

Result<bool> MeshNetworkManager::start() {
  // Initialize bridge
  auto bridge_result = bridge_->initialize();
  if (!bridge_result.is_ok()) {
    return bridge_result;
  }

  // Start adapter
  auto adapter_result = adapter_->start();
  if (!adapter_result.is_ok()) {
    return adapter_result;
  }

  // Join mesh
  auto join_result = adapter_->join_mesh();
  if (!join_result.is_ok()) {
    return join_result;
  }

  // Enable mesh routing
  bridge_->enable_mesh(true);

  std::cout << "[MeshNetworkManager] Started successfully" << std::endl;
  return Result<bool>(true, success_tag{});
}

void MeshNetworkManager::stop() {
  if (bridge_) {
    bridge_->enable_mesh(false);
    bridge_->shutdown();
  }

  if (adapter_) {
    if (adapter_->is_joined()) {
      adapter_->leave_mesh();
    }
    adapter_->stop();
  }

  std::cout << "[MeshNetworkManager] Stopped" << std::endl;
}

void MeshNetworkManager::enable(bool enabled) {
  if (bridge_) {
    bridge_->enable_mesh(enabled);
  }
}

bool MeshNetworkManager::is_enabled() const {
  return bridge_ && bridge_->is_mesh_enabled();
}

// Factory function
std::unique_ptr<MeshNetworkManager>
create_mesh_network(const ValidatorConfig &config, ClusterConnection &cluster,
                    GossipProtocol &gossip) {
  return std::make_unique<MeshNetworkManager>(config, cluster, gossip);
}

} // namespace meshcore
} // namespace network
} // namespace slonana
