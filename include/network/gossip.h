/**
 * @file gossip.h
 * @brief Defines the core components for the cluster's gossip and RPC communication.
 *
 * This file contains the primary classes for peer-to-peer communication, including
 * the `GossipProtocol` for cluster-wide message dissemination and peer discovery,
 * and the `RpcServer` for handling external client requests.
 */
#pragma once

#include "common/types.h"
#include <functional>
#include <memory>

namespace slonana {
namespace network {

using namespace slonana::common;

/**
 * @brief Defines the types of messages that can be exchanged over the gossip network.
 */
enum class MessageType {
  /// @brief A health check message to a peer.
  PING,
  /// @brief A response to a PING message.
  PONG,
  /// @brief A general-purpose message for disseminating cluster state information.
  GOSSIP_UPDATE,
  /// @brief A notification that a new block has been produced.
  BLOCK_NOTIFICATION,
  /// @brief A notification containing a validator's vote.
  VOTE_NOTIFICATION
};

/**
 * @brief Represents a single message transmitted over the gossip network.
 */
struct NetworkMessage {
  /// @brief The type of the message.
  MessageType type;
  /// @brief The public key of the node that sent the message.
  PublicKey sender;
  /// @brief The message payload, which is type-dependent.
  std::vector<uint8_t> payload;
  /// @brief The timestamp when the message was created.
  uint64_t timestamp;
};

/**
 * @brief Implements the gossip protocol for peer discovery and cluster-wide communication.
 * @details This class is responsible for maintaining a list of known peers,
 * broadcasting messages to the cluster, and handling incoming gossip messages.
 */
class GossipProtocol {
public:
  /// @brief A callback function type for handling specific types of network messages.
  using MessageHandler = std::function<void(const NetworkMessage &)>;

  explicit GossipProtocol(const ValidatorConfig &config);
  ~GossipProtocol();

  Result<bool> start();
  void stop();

  /**
   * @brief Registers a handler function for a specific message type.
   * @param type The message type to handle.
   * @param handler The function to be called when a message of that type is received.
   */
  void register_handler(MessageType type, MessageHandler handler);

  Result<bool> broadcast_message(const NetworkMessage &message);
  Result<bool> send_to_peer(const PublicKey &peer_id, const NetworkMessage &message);

  std::vector<PublicKey> get_known_peers() const;
  bool is_peer_connected(const PublicKey &peer_id) const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;

  std::vector<uint8_t> serialize_network_message(const NetworkMessage &message);
  bool send_message_to_peer_socket(const PublicKey &peer_id, const std::vector<uint8_t> &serialized_message);
};

/**
 * @brief Provides an RPC server for external interaction with the validator.
 * @details This class sets up an endpoint to handle JSON-RPC requests, allowing
 * clients to query ledger state, submit transactions, and interact with the validator.
 */
class RpcServer {
public:
  using RpcHandler = std::function<std::string(const std::string &)>;

  explicit RpcServer(const ValidatorConfig &config);
  ~RpcServer();

  Result<bool> start();
  void stop();

  /**
   * @brief Registers a handler for a specific JSON-RPC method.
   * @param method The name of the RPC method (e.g., "getAccountInfo").
   * @param handler The function that will process requests for this method.
   */
  void register_method(const std::string &method, RpcHandler handler);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace network
} // namespace slonana