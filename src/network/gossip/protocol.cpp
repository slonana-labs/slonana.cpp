#include "network/gossip/protocol.h"
#include "network/gossip/crypto_utils.h"
#include "network/gossip/serializer.h"
#include <algorithm>
#include <cstring>
#include <random>

namespace slonana {
namespace network {
namespace gossip {

// PruneData implementations
PruneData::PruneData() : wallclock(0) {}

PruneData::PruneData(const PublicKey &pk,
                     const std::vector<PublicKey> &prune_list,
                     const PublicKey &dest, uint64_t wc)
    : pubkey(pk), prunes(prune_list), destination(dest), wallclock(wc) {}

void PruneData::sign(const Signature &external_sig) {
  // In production: serialize and sign the data
  signature = external_sig;
}

bool PruneData::verify() const {
  // Serialize and verify signature
  auto data_bytes = Serializer::serialize(*this);
  return CryptoUtils::verify_ed25519(data_bytes, signature, pubkey);
}

std::vector<uint8_t> PruneData::serialize() const {
  return Serializer::serialize(*this);
}

// CrdsFilter implementations
CrdsFilter::CrdsFilter() : num_bits_(1024), num_hashes_(3) {
  bits_.resize((num_bits_ + 63) / 64, 0);
}

CrdsFilter::CrdsFilter(size_t num_items)
    : num_bits_(num_items * 10), num_hashes_(3) {
  bits_.resize((num_bits_ + 63) / 64, 0);
}

void CrdsFilter::add(const Hash &hash) {
  if (hash.empty())
    return;

  for (size_t i = 0; i < num_hashes_; ++i) {
    // Use SipHash for production-grade hashing
    uint64_t h = CryptoUtils::siphash24(hash, i, i + 1);
    h = h % num_bits_;

    size_t word_idx = h / 64;
    size_t bit_idx = h % 64;
    if (word_idx < bits_.size()) {
      bits_[word_idx] |= (1ULL << bit_idx);
    }
  }
}

bool CrdsFilter::contains(const Hash &hash) const {
  if (hash.empty())
    return false;

  for (size_t i = 0; i < num_hashes_; ++i) {
    // Use SipHash for production-grade hashing
    uint64_t h = CryptoUtils::siphash24(hash, i, i + 1);
    h = h % num_bits_;

    size_t word_idx = h / 64;
    size_t bit_idx = h % 64;
    if (word_idx >= bits_.size() ||
        !(bits_[word_idx] & (1ULL << bit_idx))) {
      return false;
    }
  }

  return true;
}

void CrdsFilter::clear() { std::fill(bits_.begin(), bits_.end(), 0); }

std::vector<uint8_t> CrdsFilter::serialize() const {
  std::vector<uint8_t> result;
  // Simplified serialization
  return result;
}

CrdsFilter CrdsFilter::deserialize(const std::vector<uint8_t> &data) {
  return CrdsFilter();
}

// PingMessage implementations
PingMessage::PingMessage() { token.resize(GOSSIP_PING_TOKEN_SIZE, 0); }

PingMessage::PingMessage(const PublicKey &from_pk) : from(from_pk) {
  generate_token();
}

void PingMessage::generate_token() {
  token.resize(GOSSIP_PING_TOKEN_SIZE);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 255);

  for (auto &byte : token) {
    byte = dis(gen);
  }
}

void PingMessage::sign(const Signature &external_sig) {
  signature = external_sig;
}

bool PingMessage::verify() const { return !signature.empty(); }

// PongMessage implementations
PongMessage::PongMessage() {}

PongMessage::PongMessage(const PublicKey &from_pk,
                         const std::vector<uint8_t> &tok)
    : from(from_pk), token(tok) {}

void PongMessage::sign(const Signature &external_sig) {
  signature = external_sig;
}

bool PongMessage::verify() const { return !signature.empty(); }

// Protocol implementations
Protocol::Protocol() : type_(Type::PushMessage), from_() {}

Protocol::Protocol(Type t, const PublicKey &from) : type_(t), from_(from) {}

Protocol Protocol::create_pull_request(const CrdsFilter &filter,
                                       const CrdsValue &caller_info) {
  Protocol p(Type::PullRequest, caller_info.pubkey());
  p.filter_ = std::make_shared<CrdsFilter>(filter);
  p.values_.push_back(caller_info);
  return p;
}

Protocol Protocol::create_pull_response(const PublicKey &from,
                                        const std::vector<CrdsValue> &values) {
  Protocol p(Type::PullResponse, from);
  p.values_ = values;
  return p;
}

Protocol Protocol::create_push_message(const PublicKey &from,
                                       const std::vector<CrdsValue> &values) {
  Protocol p(Type::PushMessage, from);
  p.values_ = values;
  return p;
}

Protocol Protocol::create_prune_message(const PublicKey &from,
                                        const PruneData &prune) {
  Protocol p(Type::PruneMessage, from);
  p.prune_data_ = std::make_shared<PruneData>(prune);
  return p;
}

Protocol Protocol::create_ping_message(const PingMessage &ping) {
  Protocol p(Type::PingMessage, ping.from);
  p.ping_ = std::make_shared<PingMessage>(ping);
  return p;
}

Protocol Protocol::create_pong_message(const PongMessage &pong) {
  Protocol p(Type::PongMessage, pong.from);
  p.pong_ = std::make_shared<PongMessage>(pong);
  return p;
}

const CrdsFilter *Protocol::get_filter() const { return filter_.get(); }

const std::vector<CrdsValue> *Protocol::get_values() const {
  if (values_.empty())
    return nullptr;
  return &values_;
}

const PruneData *Protocol::get_prune_data() const {
  return prune_data_.get();
}

const PingMessage *Protocol::get_ping() const { return ping_.get(); }

const PongMessage *Protocol::get_pong() const { return pong_.get(); }

std::vector<uint8_t> Protocol::serialize() const {
  return Serializer::serialize(*this);
}

Result<Protocol> Protocol::deserialize(const std::vector<uint8_t> &data) {
  return Serializer::deserialize_protocol(data);
}

bool Protocol::is_valid() const {
  // Basic validation
  if (from_.empty()) {
    return false;
  }

  switch (type_) {
  case Type::PullRequest:
    return filter_ != nullptr && !values_.empty();

  case Type::PullResponse:
  case Type::PushMessage:
    return !values_.empty();

  case Type::PruneMessage:
    return prune_data_ != nullptr;

  case Type::PingMessage:
    return ping_ != nullptr;

  case Type::PongMessage:
    return pong_ != nullptr;
  }

  return false;
}

// Template instantiation for split_gossip_messages
template <typename T>
std::vector<std::vector<T>>
split_gossip_messages(size_t max_chunk_size, const std::vector<T> &values) {
  std::vector<std::vector<T>> result;
  std::vector<T> current_chunk;
  size_t current_size = 0;

  for (const auto &value : values) {
    // Rough size estimation - in production use proper serialization
    size_t value_size = sizeof(T);

    if (current_size + value_size > max_chunk_size && !current_chunk.empty()) {
      // Start a new chunk
      result.push_back(std::move(current_chunk));
      current_chunk.clear();
      current_size = 0;
    }

    current_chunk.push_back(value);
    current_size += value_size;
  }

  if (!current_chunk.empty()) {
    result.push_back(std::move(current_chunk));
  }

  return result;
}

// Explicit template instantiation for CrdsValue
template std::vector<std::vector<CrdsValue>>
split_gossip_messages<CrdsValue>(size_t, const std::vector<CrdsValue> &);

} // namespace gossip
} // namespace network
} // namespace slonana
