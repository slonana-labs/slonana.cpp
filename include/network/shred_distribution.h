#pragma once

#include "common/types.h"
#include "network/turbine.h"
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace slonana {
namespace network {

using namespace slonana::common;

/**
 * Shred type enumeration (compatible with Agave)
 */
enum class ShredType : uint8_t {
  DATA = 0,
  CODING = 1,
};

/**
 * Shred header structure (packed to match Agave format)
 */
struct ShredHeader {
  uint8_t signature[64];  // Ed25519 signature
  uint8_t variant;        // Shred variant flags
  uint64_t slot;          // Slot number
  uint32_t index;         // Shred index within slot
  uint16_t version;       // Shred version
  uint16_t fec_set_index; // FEC set index for coding shreds
} __attribute__((packed));

/**
 * Shred data structure compatible with Agave
 */
class Shred {
private:
  ShredHeader header_;
  std::vector<uint8_t> payload_;

  // Constants matching Agave
  static constexpr size_t MAX_SHRED_SIZE = 1280;
  static constexpr size_t SHRED_HEADER_SIZE = sizeof(ShredHeader);
  static constexpr size_t MAX_PAYLOAD_SIZE = MAX_SHRED_SIZE - SHRED_HEADER_SIZE;

public:
  Shred() = default;
  Shred(uint64_t slot, uint32_t index, const std::vector<uint8_t> &data,
        ShredType type = ShredType::DATA);

  /**
   * Verify shred signature
   * @param pubkey Public key to verify against
   * @return true if signature is valid
   */
  bool verify_signature(const std::vector<uint8_t> &pubkey) const;

  /**
   * Sign the shred with a private key
   * @param private_key Private key for signing
   * @return true if signing successful
   */
  bool sign(const std::vector<uint8_t> &private_key);

  /**
   * Get shred type
   * @return shred type (DATA or CODING)
   */
  ShredType get_type() const;

  /**
   * Get slot number
   * @return slot number
   */
  uint64_t slot() const { return header_.slot; }

  /**
   * Get shred index
   * @return shred index
   */
  uint32_t index() const { return header_.index; }

  /**
   * Get shred version
   * @return shred version
   */
  uint16_t version() const { return header_.version; }

  /**
   * Get FEC set index
   * @return FEC set index
   */
  uint16_t fec_set_index() const { return header_.fec_set_index; }

  /**
   * Get payload data
   * @return const reference to payload
   */
  const std::vector<uint8_t> &payload() const { return payload_; }

  /**
   * Get mutable payload data
   * @return reference to payload
   */
  std::vector<uint8_t> &payload() { return payload_; }

  /**
   * Get shred size
   * @return total size in bytes
   */
  size_t size() const { return SHRED_HEADER_SIZE + payload_.size(); }

  /**
   * Check if shred is valid
   * @return true if shred structure is valid
   */
  bool is_valid() const;

  /**
   * Serialize shred to bytes (Agave format)
   * @return serialized shred data
   */
  std::vector<uint8_t> serialize() const;

  /**
   * Deserialize shred from bytes
   * @param data Serialized shred data
   * @return optional shred if deserialization successful
   */
  static std::optional<Shred> deserialize(const std::vector<uint8_t> &data);

  /**
   * Create a data shred
   * @param slot Slot number
   * @param index Shred index
   * @param data Payload data
   * @return data shred
   */
  static Shred create_data_shred(uint64_t slot, uint32_t index,
                                 const std::vector<uint8_t> &data);

  /**
   * Create a coding shred
   * @param slot Slot number
   * @param index Shred index
   * @param fec_set_index FEC set index
   * @param coding_data Coding data
   * @return coding shred
   */
  static Shred create_coding_shred(uint64_t slot, uint32_t index,
                                   uint16_t fec_set_index,
                                   const std::vector<uint8_t> &coding_data);

  // Static constants
  static constexpr size_t max_shred_size() { return MAX_SHRED_SIZE; }
  static constexpr size_t header_size() { return SHRED_HEADER_SIZE; }
  static constexpr size_t max_payload_size() { return MAX_PAYLOAD_SIZE; }
};

/**
 * Shred distribution statistics
 */
struct ShredDistributionStats {
  uint64_t shreds_sent = 0;
  uint64_t shreds_received = 0;
  uint64_t shreds_retransmitted = 0;
  uint64_t duplicate_shreds = 0;
  uint64_t invalid_shreds = 0;
  std::chrono::milliseconds avg_propagation_time{0};
  std::chrono::steady_clock::time_point last_activity;
};

/**
 * Turbine broadcast engine for shred distribution
 */
class TurbineBroadcast {
private:
  std::unique_ptr<TurbineTree> tree_;
  TurbineNode self_node_;
  mutable std::mutex broadcast_mutex_;

  // Shred tracking
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      shred_timestamps_;
  std::unordered_map<std::string, uint32_t> retransmit_counts_;
  mutable std::mutex tracking_mutex_;

  // Statistics
  ShredDistributionStats stats_;
  mutable std::mutex stats_mutex_;

  // Configuration
  uint32_t max_retransmit_attempts_;
  std::chrono::milliseconds retransmit_timeout_;

  // Callbacks
  std::function<void(const Shred &, const std::vector<TurbineNode> &)>
      send_callback_;
  std::function<void(const Shred &, const std::string &)> receive_callback_;

  // Helper methods
  std::string shred_key(const Shred &shred) const;
  bool should_retransmit(const Shred &shred) const;
  std::vector<TurbineNode> select_broadcast_targets(const Shred &shred) const;
  void update_stats(const Shred &shred, bool sent);

public:
  explicit TurbineBroadcast(const TurbineNode &self_node);
  ~TurbineBroadcast() = default;

  /**
   * Initialize with turbine tree
   * @param tree Turbine tree for distribution
   */
  void initialize(std::unique_ptr<TurbineTree> tree);

  /**
   * Broadcast shreds to the network
   * @param shreds Shreds to broadcast
   */
  void broadcast_shreds(const std::vector<Shred> &shreds);

  /**
   * Handle a received shred
   * @param shred Received shred
   * @param from Source node identifier
   */
  void handle_received_shred(const Shred &shred, const std::string &from);

  /**
   * Retransmit a shred to specific peers
   * @param shred Shred to retransmit
   * @param peers Target peers for retransmission
   */
  void retransmit_shred(const Shred &shred,
                        const std::vector<TurbineNode> &peers);

  /**
   * Set send callback for actual network transmission
   * @param callback Function to call when sending shreds
   */
  void set_send_callback(
      std::function<void(const Shred &, const std::vector<TurbineNode> &)>
          callback);

  /**
   * Set receive callback for shred processing
   * @param callback Function to call when receiving shreds
   */
  void set_receive_callback(
      std::function<void(const Shred &, const std::string &)> callback);

  /**
   * Get distribution statistics
   * @return current statistics
   */
  ShredDistributionStats get_stats() const;

  /**
   * Reset statistics
   */
  void reset_stats();

  /**
   * Update turbine tree
   * @param new_tree New turbine tree
   */
  void update_tree(std::unique_ptr<TurbineTree> new_tree);

  /**
   * Get current tree (for inspection)
   * @return const reference to current tree
   */
  const TurbineTree &get_tree() const;

  /**
   * Check if shred is duplicate
   * @param shred Shred to check
   * @return true if duplicate
   */
  bool is_duplicate_shred(const Shred &shred) const;

  /**
   * Clean up old tracking data
   * @param max_age Maximum age for tracking data
   */
  void cleanup_tracking_data(std::chrono::milliseconds max_age);

  /**
   * Set retransmission parameters
   * @param max_attempts Maximum retransmit attempts
   * @param timeout Timeout between retransmits
   */
  void set_retransmit_params(uint32_t max_attempts,
                             std::chrono::milliseconds timeout);

  /**
   * Force retransmission of a shred
   * @param shred Shred to retransmit
   * @return true if retransmission was initiated
   */
  bool force_retransmit(const Shred &shred);
};

/**
 * Shred validator for integrity checking
 */
class ShredValidator {
public:
  /**
   * Validate shred structure and content
   * @param shred Shred to validate
   * @return true if shred is valid
   */
  static bool validate_shred(const Shred &shred);

  /**
   * Validate shred signature
   * @param shred Shred to validate
   * @param expected_pubkey Expected signer public key
   * @return true if signature is valid
   */
  static bool validate_signature(const Shred &shred,
                                 const std::vector<uint8_t> &expected_pubkey);

  /**
   * Validate shred slot progression
   * @param shred Shred to validate
   * @param last_slot Last valid slot
   * @return true if slot progression is valid
   */
  static bool validate_slot_progression(const Shred &shred, uint64_t last_slot);

  /**
   * Validate shred index within slot
   * @param shred Shred to validate
   * @param max_index Maximum expected index
   * @return true if index is valid
   */
  static bool validate_index(const Shred &shred, uint32_t max_index);
};

/**
 * Utility functions for shred operations
 */
namespace shred_utils {
/**
 * Split data into shreds
 * @param data Data to split
 * @param slot Slot number
 * @param start_index Starting shred index
 * @return vector of data shreds
 */
std::vector<Shred> split_data_into_shreds(const std::vector<uint8_t> &data,
                                          uint64_t slot,
                                          uint32_t start_index = 0);

/**
 * Reconstruct data from shreds
 * @param shreds Shreds to reconstruct from
 * @return reconstructed data, empty if reconstruction failed
 */
std::vector<uint8_t>
reconstruct_data_from_shreds(const std::vector<Shred> &shreds);

/**
 * Generate FEC coding shreds
 * @param data_shreds Data shreds to protect
 * @param num_coding_shreds Number of coding shreds to generate
 * @return vector of coding shreds
 */
std::vector<Shred> generate_coding_shreds(const std::vector<Shred> &data_shreds,
                                          size_t num_coding_shreds);

/**
 * Recover missing shreds using FEC
 * @param available_shreds Available shreds (data + coding)
 * @param missing_indices Indices of missing shreds
 * @return recovered shreds
 */
std::vector<Shred>
recover_missing_shreds(const std::vector<Shred> &available_shreds,
                       const std::vector<uint32_t> &missing_indices);

/**
 * Calculate shred hash for deduplication
 * @param shred Shred to hash
 * @return hash value
 */
std::string calculate_shred_hash(const Shred &shred);

/**
 * Compress shred payload
 * @param shred Shred to compress
 * @return compressed shred
 */
Shred compress_shred(const Shred &shred);

/**
 * Decompress shred payload
 * @param compressed_shred Compressed shred
 * @return decompressed shred
 */
Shred decompress_shred(const Shred &compressed_shred);
} // namespace shred_utils

} // namespace network
} // namespace slonana