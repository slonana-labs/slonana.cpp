#include "network/gossip/crypto_utils.h"
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <cstring>
#include <stdexcept>

namespace slonana {
namespace network {
namespace gossip {

Hash CryptoUtils::sha256(const std::vector<uint8_t> &data) {
  Hash hash(SHA256_DIGEST_LENGTH);
  
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  if (!ctx) {
    return hash;
  }
  
  if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
    EVP_MD_CTX_free(ctx);
    return hash;
  }
  
  if (EVP_DigestUpdate(ctx, data.data(), data.size()) != 1) {
    EVP_MD_CTX_free(ctx);
    return hash;
  }
  
  unsigned int len = SHA256_DIGEST_LENGTH;
  if (EVP_DigestFinal_ex(ctx, hash.data(), &len) != 1) {
    EVP_MD_CTX_free(ctx);
    return hash;
  }
  
  EVP_MD_CTX_free(ctx);
  return hash;
}

Hash CryptoUtils::sha256_multi(const std::vector<std::vector<uint8_t>> &data_chunks) {
  Hash hash(SHA256_DIGEST_LENGTH);
  
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  if (!ctx) {
    return hash;
  }
  
  if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
    EVP_MD_CTX_free(ctx);
    return hash;
  }
  
  for (const auto &chunk : data_chunks) {
    if (!chunk.empty()) {
      if (EVP_DigestUpdate(ctx, chunk.data(), chunk.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        return hash;
      }
    }
  }
  
  unsigned int len = SHA256_DIGEST_LENGTH;
  if (EVP_DigestFinal_ex(ctx, hash.data(), &len) != 1) {
    EVP_MD_CTX_free(ctx);
    return hash;
  }
  
  EVP_MD_CTX_free(ctx);
  return hash;
}

bool CryptoUtils::verify_ed25519(const std::vector<uint8_t> &message,
                                  const Signature &signature,
                                  const PublicKey &public_key) {
  // Verify signature and public key sizes
  if (signature.size() != 64 || public_key.size() != 32) {
    return false;
  }
  
  // Use OpenSSL EVP API for Ed25519 verification
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  if (!ctx) {
    return false;
  }
  
  // Create public key
  EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                                 public_key.data(), public_key.size());
  if (!pkey) {
    EVP_MD_CTX_free(ctx);
    return false;
  }
  
  // Initialize verification context
  if (EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey) != 1) {
    EVP_PKEY_free(pkey);
    EVP_MD_CTX_free(ctx);
    return false;
  }
  
  // Verify signature
  int result = EVP_DigestVerify(ctx, signature.data(), signature.size(),
                                message.data(), message.size());
  
  EVP_PKEY_free(pkey);
  EVP_MD_CTX_free(ctx);
  
  return result == 1;
}

Signature CryptoUtils::sign_ed25519(const std::vector<uint8_t> &message,
                                     const std::vector<uint8_t> &private_key) {
  if (private_key.size() != 32) {
    return Signature();
  }
  
  Signature signature(64);
  
  // Create private key
  EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                                  private_key.data(), private_key.size());
  if (!pkey) {
    return Signature();
  }
  
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  if (!ctx) {
    EVP_PKEY_free(pkey);
    return Signature();
  }
  
  if (EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, pkey) != 1) {
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return Signature();
  }
  
  size_t sig_len = signature.size();
  if (EVP_DigestSign(ctx, signature.data(), &sig_len,
                     message.data(), message.size()) != 1) {
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return Signature();
  }
  
  EVP_MD_CTX_free(ctx);
  EVP_PKEY_free(pkey);
  
  return signature;
}

// SipHash-2-4 implementation for bloom filter
static uint64_t rotl64(uint64_t x, int b) {
  return (x << b) | (x >> (64 - b));
}

static void sipround(uint64_t &v0, uint64_t &v1, uint64_t &v2, uint64_t &v3) {
  v0 += v1;
  v1 = rotl64(v1, 13);
  v1 ^= v0;
  v0 = rotl64(v0, 32);
  
  v2 += v3;
  v3 = rotl64(v3, 16);
  v3 ^= v2;
  
  v0 += v3;
  v3 = rotl64(v3, 21);
  v3 ^= v0;
  
  v2 += v1;
  v1 = rotl64(v1, 17);
  v1 ^= v2;
  v2 = rotl64(v2, 32);
}

uint64_t CryptoUtils::siphash24(const std::vector<uint8_t> &data,
                                uint64_t key0, uint64_t key1) {
  uint64_t v0 = 0x736f6d6570736575ULL ^ key0;
  uint64_t v1 = 0x646f72616e646f6dULL ^ key1;
  uint64_t v2 = 0x6c7967656e657261ULL ^ key0;
  uint64_t v3 = 0x7465646279746573ULL ^ key1;
  
  size_t len = data.size();
  const uint8_t *ptr = data.data();
  const uint8_t *end = ptr + len - (len % 8);
  
  uint64_t b = ((uint64_t)len) << 56;
  
  // Process 8-byte blocks
  while (ptr < end) {
    uint64_t m = 0;
    for (int i = 0; i < 8; ++i) {
      m |= ((uint64_t)ptr[i]) << (i * 8);
    }
    ptr += 8;
    
    v3 ^= m;
    sipround(v0, v1, v2, v3);
    sipround(v0, v1, v2, v3);
    v0 ^= m;
  }
  
  // Process remaining bytes
  switch (len & 7) {
    case 7: b |= ((uint64_t)ptr[6]) << 48; [[fallthrough]];
    case 6: b |= ((uint64_t)ptr[5]) << 40; [[fallthrough]];
    case 5: b |= ((uint64_t)ptr[4]) << 32; [[fallthrough]];
    case 4: b |= ((uint64_t)ptr[3]) << 24; [[fallthrough]];
    case 3: b |= ((uint64_t)ptr[2]) << 16; [[fallthrough]];
    case 2: b |= ((uint64_t)ptr[1]) << 8;  [[fallthrough]];
    case 1: b |= ((uint64_t)ptr[0]);       [[fallthrough]];
    case 0: break;
  }
  
  v3 ^= b;
  sipround(v0, v1, v2, v3);
  sipround(v0, v1, v2, v3);
  v0 ^= b;
  
  v2 ^= 0xff;
  sipround(v0, v1, v2, v3);
  sipround(v0, v1, v2, v3);
  sipround(v0, v1, v2, v3);
  sipround(v0, v1, v2, v3);
  
  return v0 ^ v1 ^ v2 ^ v3;
}

} // namespace gossip
} // namespace network
} // namespace slonana
