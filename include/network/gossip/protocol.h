#pragma once

#include "crds_value.h"
#include "common/types.h"
#include <cstdint>
#include <vector>

namespace slonana {
namespace network {
namespace gossip {

// Protocol constants from Agave
constexpr size_t MAX_PRUNE_DATA_NODES = 32;
constexpr size_t PUSH_MESSAGE_MAX_PAYLOAD_SIZE = 1232; // PACKET_DATA_SIZE - 44
constexpr size_t PULL_RESPONSE_MAX_PAYLOAD_SIZE = 1232;
constexpr size_t GOSSIP_PING_TOKEN_SIZE = 32;

/**
 * PruneData - Data for pruning gossip connections
 * Agave: gossip/src/protocol.rs (PruneData struct)
 */
struct PruneData {
  PublicKey pubkey;                // Originator of the prune
  std::vector<PublicKey> prunes;   // Nodes to prune
  PublicKey destination;           // Destination of the prune message
  uint64_t wallclock;              // Wallclock timestamp
  Signature signature;             // Signature over the data

  PruneData();
  PruneData(const PublicKey &pk, const std::vector<PublicKey> &prune_list,
            const PublicKey &dest, uint64_t wc);

  void sign(const Signature &external_sig);
  bool verify() const;
  std::vector<uint8_t> serialize() const;
};

/**
 * CrdsFilter - Bloom filter for pull requests
 * Agave: gossip/src/crds_filter.rs
 */
class CrdsFilter {
public:
  CrdsFilter();
  explicit CrdsFilter(size_t num_items);

  void add(const Hash &hash);
  bool contains(const Hash &hash) const;
  void clear();

  std::vector<uint8_t> serialize() const;
  static CrdsFilter deserialize(const std::vector<uint8_t> &data);

private:
  std::vector<uint64_t> bits_;
  size_t num_bits_;
  size_t num_hashes_;
};

/**
 * PingMessage - Ping for measuring latency
 * Agave: gossip/src/ping_pong.rs (Ping struct)
 */
struct PingMessage {
  PublicKey from;
  std::vector<uint8_t> token; // Random token
  Signature signature;

  PingMessage();
  explicit PingMessage(const PublicKey &from_pk);

  void generate_token();
  void sign(const Signature &external_sig);
  bool verify() const;
};

/**
 * PongMessage - Response to ping
 * Agave: gossip/src/ping_pong.rs (Pong struct)
 */
struct PongMessage {
  PublicKey from;
  std::vector<uint8_t> token; // Token from the ping
  Signature signature;

  PongMessage();
  PongMessage(const PublicKey &from_pk, const std::vector<uint8_t> &tok);

  void sign(const Signature &external_sig);
  bool verify() const;
};

/**
 * Protocol - Main gossip protocol message types
 * Agave: gossip/src/protocol.rs (Protocol enum)
 */
class Protocol {
public:
  enum class Type {
    PullRequest,
    PullResponse,
    PushMessage,
    PruneMessage,
    PingMessage,
    PongMessage
  };

  static Protocol create_pull_request(const CrdsFilter &filter,
                                      const CrdsValue &caller_info);

  static Protocol create_pull_response(const PublicKey &from,
                                       const std::vector<CrdsValue> &values);

  static Protocol create_push_message(const PublicKey &from,
                                      const std::vector<CrdsValue> &values);

  static Protocol create_prune_message(const PublicKey &from,
                                       const PruneData &prune);

  static Protocol create_ping_message(const PingMessage &ping);

  static Protocol create_pong_message(const PongMessage &pong);

  // Default constructor for Result<Protocol>
  Protocol();

  // Accessors
  Type type() const { return type_; }
  const CrdsFilter *get_filter() const;
  const std::vector<CrdsValue> *get_values() const;
  const PruneData *get_prune_data() const;
  const PingMessage *get_ping() const;
  const PongMessage *get_pong() const;
  const PublicKey &get_from() const { return from_; }

  // Serialization
  std::vector<uint8_t> serialize() const;
  static Result<Protocol> deserialize(const std::vector<uint8_t> &data);

  // Validation
  bool is_valid() const;

private:
  Type type_;
  PublicKey from_;

  // Union-like storage for different message data
  std::shared_ptr<CrdsFilter> filter_;
  std::vector<CrdsValue> values_;
  std::shared_ptr<PruneData> prune_data_;
  std::shared_ptr<PingMessage> ping_;
  std::shared_ptr<PongMessage> pong_;

  Protocol(Type t, const PublicKey &from);
};

/**
 * Split values into chunks that fit within max_chunk_size
 * Agave: gossip/src/protocol.rs (split_gossip_messages)
 */
template <typename T>
std::vector<std::vector<T>>
split_gossip_messages(size_t max_chunk_size, const std::vector<T> &values);

} // namespace gossip
} // namespace network
} // namespace slonana
