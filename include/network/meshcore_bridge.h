#pragma once

/**
 * MeshCore Network Bridge
 *
 * Provides integration between MeshCore adapter and existing slonana
 * networking stack (ClusterConnection, GossipProtocol)
 *
 * Features:
 * - Bi-directional message routing between mesh and cluster
 * - Message type conversion between protocols
 * - Automatic handler registration
 * - Enable/disable mesh networking at runtime
 */

#include "network/cluster_connection.h"
#include "network/gossip.h"
#include "network/meshcore_adapter.h"
#include <atomic>
#include <memory>

namespace slonana {
namespace network {
namespace meshcore {

/**
 * MeshCore Bridge - Integration with slonana networking
 *
 * Routes messages between MeshCore mesh network and existing
 * ClusterConnection/GossipProtocol infrastructure.
 */
class MeshCoreBridge {
public:
  MeshCoreBridge(MeshCoreAdapter &adapter, ClusterConnection &cluster,
                 GossipProtocol &gossip);
  ~MeshCoreBridge();

  // Initialize bridge and setup message routing
  Result<bool> initialize();

  // Shutdown bridge
  void shutdown();

  // Enable/disable mesh networking
  void enable_mesh(bool enabled);
  bool is_mesh_enabled() const;

  // Message routing methods
  void route_mesh_to_cluster(const MeshMessage &msg);
  void route_cluster_to_mesh(const ClusterMessage &msg);
  void route_gossip_to_mesh(const NetworkMessage &msg);

private:
  MeshCoreAdapter &mesh_adapter_;
  ClusterConnection &cluster_connection_;
  GossipProtocol &gossip_protocol_;

  std::atomic<bool> mesh_enabled_;
  std::atomic<bool> running_;

  void setup_message_handlers();
};

/**
 * Factory function to create integrated mesh networking
 *
 * Creates a MeshCoreAdapter configured from ValidatorConfig and
 * sets up the bridge for message routing.
 */
std::unique_ptr<MeshCoreBridge>
create_mesh_bridge(const ValidatorConfig &config, ClusterConnection &cluster,
                   GossipProtocol &gossip);

} // namespace meshcore
} // namespace network
} // namespace slonana
