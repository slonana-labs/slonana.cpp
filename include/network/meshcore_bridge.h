#pragma once

/**
 * MeshCore Network Bridge (STUB/PLACEHOLDER)
 *
 * This is a stub implementation that demonstrates the integration architecture
 * between MeshCore adapter and the actual slonana networking stack.
 *
 * **STATUS: Phase 2 - Not Yet Implemented**
 *
 * The bridge design shows how MeshCore would integrate with:
 * - QUIC/TCP transport layers
 * - ClusterConnection for validator communication
 * - GossipProtocol for peer discovery
 *
 * For production implementation, see: MESHCORE_IMPLEMENTATION_STATUS.md
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
 * MeshCore Bridge - Integration with real networking (STUB)
 *
 * This stub demonstrates the architecture for integrating MeshCore
 * with the existing networking stack. Production implementation pending.
 */
class MeshCoreBridge {
public:
  MeshCoreBridge(MeshCoreAdapter &adapter, ClusterConnection &cluster,
                 GossipProtocol &gossip);
  ~MeshCoreBridge();

  // Initialize bridge and start networking
  Result<bool> initialize();

  // Shutdown bridge
  void shutdown();

  // Enable/disable mesh networking
  void enable_mesh(bool enabled);
  bool is_mesh_enabled() const;

  // Forward messages between mesh and cluster/gossip
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
 */
std::unique_ptr<MeshCoreBridge>
create_mesh_bridge(const ValidatorConfig &config, ClusterConnection &cluster,
                   GossipProtocol &gossip);

} // namespace meshcore
} // namespace network
} // namespace slonana
