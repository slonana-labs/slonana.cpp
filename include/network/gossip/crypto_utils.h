#pragma once

#include "common/types.h"
#include <cstdint>
#include <vector>

namespace slonana {
namespace network {
namespace gossip {

using namespace slonana::common;

/**
 * Crypto utilities for gossip protocol
 * Provides Ed25519 signature verification and SHA256 hashing
 */
class CryptoUtils {
public:
  /**
   * Compute SHA256 hash of data
   */
  static Hash sha256(const std::vector<uint8_t> &data);
  
  /**
   * Compute SHA256 hash of multiple data chunks
   */
  static Hash sha256_multi(const std::vector<std::vector<uint8_t>> &data_chunks);
  
  /**
   * Verify Ed25519 signature
   * @param message The message that was signed
   * @param signature The 64-byte signature
   * @param public_key The 32-byte public key
   * @return true if signature is valid
   */
  static bool verify_ed25519(const std::vector<uint8_t> &message,
                             const Signature &signature,
                             const PublicKey &public_key);
  
  /**
   * Sign message with Ed25519 (stub for now - requires private key)
   * In production, this would use the node's private key
   */
  static Signature sign_ed25519(const std::vector<uint8_t> &message,
                                const std::vector<uint8_t> &private_key);

  /**
   * SipHash-2-4 for bloom filter hashing
   */
  static uint64_t siphash24(const std::vector<uint8_t> &data, 
                            uint64_t key0, uint64_t key1);
};

} // namespace gossip
} // namespace network
} // namespace slonana
