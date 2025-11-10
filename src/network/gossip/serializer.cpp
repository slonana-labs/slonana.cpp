#include "network/gossip/serializer.h"
#include <cstring>
#include <stdexcept>

namespace slonana {
namespace network {
namespace gossip {

// Write primitive types in little-endian format (bincode compatible)
void Serializer::write_u8(std::vector<uint8_t> &buf, uint8_t val) {
  buf.push_back(val);
}

void Serializer::write_u16(std::vector<uint8_t> &buf, uint16_t val) {
  buf.push_back(val & 0xFF);
  buf.push_back((val >> 8) & 0xFF);
}

void Serializer::write_u32(std::vector<uint8_t> &buf, uint32_t val) {
  buf.push_back(val & 0xFF);
  buf.push_back((val >> 8) & 0xFF);
  buf.push_back((val >> 16) & 0xFF);
  buf.push_back((val >> 24) & 0xFF);
}

void Serializer::write_u64(std::vector<uint8_t> &buf, uint64_t val) {
  for (int i = 0; i < 8; ++i) {
    buf.push_back((val >> (i * 8)) & 0xFF);
  }
}

void Serializer::write_bytes(std::vector<uint8_t> &buf, const std::vector<uint8_t> &bytes) {
  write_u64(buf, bytes.size());
  buf.insert(buf.end(), bytes.begin(), bytes.end());
}

void Serializer::write_string(std::vector<uint8_t> &buf, const std::string &str) {
  write_u64(buf, str.size());
  buf.insert(buf.end(), str.begin(), str.end());
}

// Read primitive types
uint8_t Serializer::read_u8(const uint8_t *&ptr, const uint8_t *end) {
  if (ptr >= end) throw std::runtime_error("Buffer underflow");
  return *ptr++;
}

uint16_t Serializer::read_u16(const uint8_t *&ptr, const uint8_t *end) {
  if (ptr + 2 > end) throw std::runtime_error("Buffer underflow");
  uint16_t val = ptr[0] | (ptr[1] << 8);
  ptr += 2;
  return val;
}

uint32_t Serializer::read_u32(const uint8_t *&ptr, const uint8_t *end) {
  if (ptr + 4 > end) throw std::runtime_error("Buffer underflow");
  uint32_t val = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
  ptr += 4;
  return val;
}

uint64_t Serializer::read_u64(const uint8_t *&ptr, const uint8_t *end) {
  if (ptr + 8 > end) throw std::runtime_error("Buffer underflow");
  uint64_t val = 0;
  for (int i = 0; i < 8; ++i) {
    val |= ((uint64_t)ptr[i]) << (i * 8);
  }
  ptr += 8;
  return val;
}

std::vector<uint8_t> Serializer::read_bytes(const uint8_t *&ptr, const uint8_t *end, size_t len) {
  if (ptr + len > end) throw std::runtime_error("Buffer underflow");
  std::vector<uint8_t> result(ptr, ptr + len);
  ptr += len;
  return result;
}

std::vector<uint8_t> Serializer::serialize(const CrdsData &data) {
  std::vector<uint8_t> buf;
  
  // Write variant tag
  if (std::holds_alternative<ContactInfo>(data)) {
    write_u8(buf, 0);
    const auto &ci = std::get<ContactInfo>(data);
    write_bytes(buf, ci.pubkey);
    write_u64(buf, ci.wallclock);
    write_u64(buf, ci.outset);
    write_u16(buf, ci.shred_version);
    write_string(buf, ci.version);
    write_u64(buf, ci.addrs.size());
    for (const auto &addr : ci.addrs) {
      write_string(buf, addr);
    }
  } else if (std::holds_alternative<Vote>(data)) {
    write_u8(buf, 1);
    const auto &vote = std::get<Vote>(data);
    write_bytes(buf, vote.from);
    write_u64(buf, vote.slots.size());
    for (uint64_t slot : vote.slots) {
      write_u64(buf, slot);
    }
    write_bytes(buf, vote.vote_hash);
    write_u64(buf, vote.wallclock);
    write_u64(buf, vote.vote_timestamp);
  } else if (std::holds_alternative<LowestSlot>(data)) {
    write_u8(buf, 2);
    const auto &ls = std::get<LowestSlot>(data);
    write_bytes(buf, ls.from);
    write_u64(buf, ls.lowest);
    write_u64(buf, ls.wallclock);
  } else if (std::holds_alternative<EpochSlots>(data)) {
    write_u8(buf, 3);
    const auto &es = std::get<EpochSlots>(data);
    write_bytes(buf, es.from);
    write_u64(buf, es.slots.size());
    for (uint64_t slot : es.slots) {
      write_u64(buf, slot);
    }
    write_u64(buf, es.wallclock);
  } else if (std::holds_alternative<NodeInstance>(data)) {
    write_u8(buf, 4);
    const auto &ni = std::get<NodeInstance>(data);
    write_bytes(buf, ni.from);
    write_u64(buf, ni.instance_timestamp);
    write_u64(buf, ni.wallclock);
  } else if (std::holds_alternative<SnapshotHashes>(data)) {
    write_u8(buf, 5);
    const auto &sh = std::get<SnapshotHashes>(data);
    write_bytes(buf, sh.from);
    write_u64(buf, sh.hashes.size());
    for (const auto &[slot, hash] : sh.hashes) {
      write_u64(buf, slot);
      write_bytes(buf, hash);
    }
    write_u64(buf, sh.wallclock);
  } else if (std::holds_alternative<RestartLastVotedForkSlots>(data)) {
    write_u8(buf, 6);
    const auto &rlvfs = std::get<RestartLastVotedForkSlots>(data);
    write_bytes(buf, rlvfs.from);
    write_u64(buf, rlvfs.slots.size());
    for (uint64_t slot : rlvfs.slots) {
      write_u64(buf, slot);
    }
    write_bytes(buf, rlvfs.hash);
    write_u64(buf, rlvfs.wallclock);
  } else if (std::holds_alternative<RestartHeaviestFork>(data)) {
    write_u8(buf, 7);
    const auto &rhf = std::get<RestartHeaviestFork>(data);
    write_bytes(buf, rhf.from);
    write_u64(buf, rhf.slot);
    write_bytes(buf, rhf.hash);
    write_u64(buf, rhf.wallclock);
  }
  
  return buf;
}

std::vector<uint8_t> Serializer::serialize(const CrdsValue &value) {
  std::vector<uint8_t> buf;
  
  // Serialize signature
  write_bytes(buf, value.get_signature());
  
  // Serialize data
  auto data_buf = serialize(value.data());
  buf.insert(buf.end(), data_buf.begin(), data_buf.end());
  
  return buf;
}

std::vector<uint8_t> Serializer::serialize(const PruneData &prune) {
  std::vector<uint8_t> buf;
  
  write_bytes(buf, prune.pubkey);
  write_u64(buf, prune.prunes.size());
  for (const auto &pk : prune.prunes) {
    write_bytes(buf, pk);
  }
  write_bytes(buf, prune.destination);
  write_u64(buf, prune.wallclock);
  write_bytes(buf, prune.signature);
  
  return buf;
}

std::vector<uint8_t> Serializer::serialize(const Protocol &msg) {
  std::vector<uint8_t> buf;
  
  // Write message type
  write_u8(buf, static_cast<uint8_t>(msg.type()));
  
  // Write from pubkey
  write_bytes(buf, msg.get_from());
  
  // Type-specific data
  switch (msg.type()) {
    case Protocol::Type::PullRequest: {
      // Serialize filter and value
      const auto *values = msg.get_values();
      if (values && !values->empty()) {
        auto val_buf = serialize((*values)[0]);
        buf.insert(buf.end(), val_buf.begin(), val_buf.end());
      }
      break;
    }
    case Protocol::Type::PullResponse:
    case Protocol::Type::PushMessage: {
      const auto *values = msg.get_values();
      if (values) {
        write_u64(buf, values->size());
        for (const auto &val : *values) {
          auto val_buf = serialize(val);
          buf.insert(buf.end(), val_buf.begin(), val_buf.end());
        }
      } else {
        write_u64(buf, 0);
      }
      break;
    }
    case Protocol::Type::PruneMessage: {
      const auto *prune = msg.get_prune_data();
      if (prune) {
        auto prune_buf = serialize(*prune);
        buf.insert(buf.end(), prune_buf.begin(), prune_buf.end());
      }
      break;
    }
    case Protocol::Type::PingMessage: {
      const auto *ping = msg.get_ping();
      if (ping) {
        write_bytes(buf, ping->from);
        write_bytes(buf, ping->token);
        write_bytes(buf, ping->signature);
      }
      break;
    }
    case Protocol::Type::PongMessage: {
      const auto *pong = msg.get_pong();
      if (pong) {
        write_bytes(buf, pong->from);
        write_bytes(buf, pong->token);
        write_bytes(buf, pong->signature);
      }
      break;
    }
  }
  
  return buf;
}

Result<CrdsValue> Serializer::deserialize_crds_value(const std::vector<uint8_t> &data) {
  // Simplified deserialization - full implementation would parse all fields
  try {
    const uint8_t *ptr = data.data();
    const uint8_t *end = data.data() + data.size();
    
    // Read signature
    uint64_t sig_len = read_u64(ptr, end);
    auto signature = read_bytes(ptr, end, sig_len);
    
    // Read data type tag
    uint8_t tag = read_u8(ptr, end);
    
    // For now, create a basic ContactInfo
    ContactInfo ci;
    CrdsValue value(ci);
    
    return Result<CrdsValue>(value);
  } catch (const std::exception &e) {
    return Result<CrdsValue>(std::string("Deserialization failed: ") + e.what());
  }
}

Result<Protocol> Serializer::deserialize_protocol(const std::vector<uint8_t> &data) {
  // Simplified deserialization
  try {
    const uint8_t *ptr = data.data();
    const uint8_t *end = data.data() + data.size();
    
    uint8_t type_tag = read_u8(ptr, end);
    uint64_t from_len = read_u64(ptr, end);
    auto from_pk = read_bytes(ptr, end, from_len);
    
    // Create empty protocol message
    Protocol msg = Protocol::create_push_message(from_pk, std::vector<CrdsValue>());
    return Result<Protocol>(msg);
  } catch (const std::exception &e) {
    return Result<Protocol>(std::string("Deserialization failed: ") + e.what());
  }
}

} // namespace gossip
} // namespace network
} // namespace slonana
