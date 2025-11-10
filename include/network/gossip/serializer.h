#pragma once

#include "crds_data.h"
#include "crds_value.h"
#include "protocol.h"
#include <cstdint>
#include <vector>

namespace slonana {
namespace network {
namespace gossip {

/**
 * Serialization utilities for gossip protocol
 * Implements bincode-compatible serialization matching Agave
 */
class Serializer {
public:
  /**
   * Serialize a CrdsValue
   */
  static std::vector<uint8_t> serialize(const CrdsValue &value);
  
  /**
   * Serialize CrdsData
   */
  static std::vector<uint8_t> serialize(const CrdsData &data);
  
  /**
   * Serialize PruneData
   */
  static std::vector<uint8_t> serialize(const PruneData &prune);
  
  /**
   * Serialize Protocol message
   */
  static std::vector<uint8_t> serialize(const Protocol &msg);
  
  /**
   * Deserialize CrdsValue
   */
  static Result<CrdsValue> deserialize_crds_value(const std::vector<uint8_t> &data);
  
  /**
   * Deserialize Protocol message
   */
  static Result<Protocol> deserialize_protocol(const std::vector<uint8_t> &data);
  
  // Helper methods for primitive types (public for use by other components)
  static void write_u8(std::vector<uint8_t> &buf, uint8_t val);
  static void write_u16(std::vector<uint8_t> &buf, uint16_t val);
  static void write_u32(std::vector<uint8_t> &buf, uint32_t val);
  static void write_u64(std::vector<uint8_t> &buf, uint64_t val);
  static void write_bytes(std::vector<uint8_t> &buf, const std::vector<uint8_t> &bytes);
  static void write_string(std::vector<uint8_t> &buf, const std::string &str);
  
private:
  static uint8_t read_u8(const uint8_t *&ptr, const uint8_t *end);
  static uint16_t read_u16(const uint8_t *&ptr, const uint8_t *end);
  static uint32_t read_u32(const uint8_t *&ptr, const uint8_t *end);
  static uint64_t read_u64(const uint8_t *&ptr, const uint8_t *end);
  static std::vector<uint8_t> read_bytes(const uint8_t *&ptr, const uint8_t *end, size_t len);
};

} // namespace gossip
} // namespace network
} // namespace slonana
