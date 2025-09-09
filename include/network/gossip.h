#pragma once

#include "common/types.h"
#include <functional>
#include <memory>

namespace slonana {
namespace network {

using namespace slonana::common;

/**
 * Network message types for the gossip protocol
 */
enum class MessageType {
  PING,
  PONG,
  GOSSIP_UPDATE,
  BLOCK_NOTIFICATION,
  VOTE_NOTIFICATION
};

struct NetworkMessage {
  MessageType type;
  PublicKey sender;
  std::vector<uint8_t> payload;
  uint64_t timestamp;
};

/**
 * Gossip protocol handler for peer discovery and cluster communication
 */
class GossipProtocol {
public:
  using MessageHandler = std::function<void(const NetworkMessage &)>;

  explicit GossipProtocol(const ValidatorConfig &config);
  ~GossipProtocol();

  // Start/stop the gossip service
  Result<bool> start();
  void stop();

  // Register message handlers
  void register_handler(MessageType type, MessageHandler handler);

  // Send messages to the network
  Result<bool> broadcast_message(const NetworkMessage &message);
  Result<bool> send_to_peer(const PublicKey &peer_id,
                            const NetworkMessage &message);

  // Peer management
  std::vector<PublicKey> get_known_peers() const;
  bool is_peer_connected(const PublicKey &peer_id) const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;

  // Helper methods for message handling
  std::vector<uint8_t> serialize_network_message(const NetworkMessage &message);
  bool
  send_message_to_peer_socket(const PublicKey &peer_id,
                              const std::vector<uint8_t> &serialized_message);
};

/**
 * RPC server for external validator interaction
 */
class RpcServer {
public:
  explicit RpcServer(const ValidatorConfig &config);
  ~RpcServer();

  Result<bool> start();
  void stop();

  // Register RPC method handlers
  using RpcHandler = std::function<std::string(const std::string &)>;
  void register_method(const std::string &method, RpcHandler handler);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace network
} // namespace slonana