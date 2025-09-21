#include "security/secure_messaging.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <openssl/aes.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <random>
#include <sstream>

namespace slonana {
namespace security {

namespace {
/// Helper to get current timestamp in milliseconds
uint64_t get_current_timestamp_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

/// Helper to generate cryptographically secure random bytes
std::vector<uint8_t> generate_random_bytes(size_t count) {
  std::vector<uint8_t> bytes(count);
  if (RAND_bytes(bytes.data(), static_cast<int>(count)) != 1) {
    throw std::runtime_error("Failed to generate random bytes");
  }
  return bytes;
}

/// Helper to convert OpenSSL errors to string
std::string get_openssl_error() {
  char buffer[256];
  ERR_error_string_n(ERR_get_error(), buffer, sizeof(buffer));
  return std::string(buffer);
}
} // namespace

// ============================================================================
// SecureMessage Implementation
// ============================================================================

std::vector<uint8_t> SecureMessage::serialize() const {
  std::vector<uint8_t> result;
  
  // Version (1 byte) + format flags
  result.push_back(0x02); // Version 2 with network byte order
  
  // Message type length + data (network byte order)
  uint16_t type_len = static_cast<uint16_t>(message_type.length());
  result.push_back((type_len >> 8) & 0xFF); // Big-endian
  result.push_back(type_len & 0xFF);
  result.insert(result.end(), message_type.begin(), message_type.end());
  
  // Timestamp (8 bytes, network byte order)
  for (int i = 7; i >= 0; --i) {
    result.push_back((timestamp_ms >> (i * 8)) & 0xFF);
  }
  
  // Nonce (8 bytes, network byte order)
  for (int i = 7; i >= 0; --i) {
    result.push_back((nonce >> (i * 8)) & 0xFF);
  }
  
  // Sender public key (32 bytes for Ed25519)
  result.insert(result.end(), sender_public_key.begin(), sender_public_key.end());
  
  // IV length + data (network byte order)
  uint16_t iv_len = static_cast<uint16_t>(iv.size());
  result.push_back((iv_len >> 8) & 0xFF); // Big-endian
  result.push_back(iv_len & 0xFF);
  result.insert(result.end(), iv.begin(), iv.end());
  
  // Auth tag length + data (network byte order)
  uint16_t tag_len = static_cast<uint16_t>(auth_tag.size());
  result.push_back((tag_len >> 8) & 0xFF); // Big-endian
  result.push_back(tag_len & 0xFF);
  result.insert(result.end(), auth_tag.begin(), auth_tag.end());
  
  // Signature length + data (network byte order)
  uint16_t sig_len = static_cast<uint16_t>(signature.size());
  result.push_back((sig_len >> 8) & 0xFF); // Big-endian
  result.push_back(sig_len & 0xFF);
  result.insert(result.end(), signature.begin(), signature.end());
  
  // Encrypted payload length + data (network byte order)
  uint32_t payload_len = static_cast<uint32_t>(encrypted_payload.size());
  for (int i = 3; i >= 0; --i) {
    result.push_back((payload_len >> (i * 8)) & 0xFF);
  }
  result.insert(result.end(), encrypted_payload.begin(), encrypted_payload.end());
  
  return result;
}

Result<SecureMessage> SecureMessage::deserialize(const std::vector<uint8_t>& data) {
  if (data.size() < 64) { // Minimum size check
    return Result<SecureMessage>("Message too short");
  }
  
  size_t offset = 0;
  SecureMessage msg;
  
  try {
    // Version check - support both v1 (little-endian) and v2 (big-endian)
    uint8_t version = data[offset++];
    bool use_big_endian = false;
    
    if (version == 0x01) {
      use_big_endian = false; // Legacy little-endian format
    } else if (version == 0x02) {
      use_big_endian = true;  // New network byte order format
    } else {
      return Result<SecureMessage>("Unsupported message version: " + std::to_string(version));
    }
    
    // Message type (handle endianness)
    uint16_t type_len;
    if (use_big_endian) {
      type_len = (data[offset] << 8) | data[offset + 1];
    } else {
      type_len = data[offset] | (data[offset + 1] << 8);
    }
    offset += 2;
    
    if (offset + type_len > data.size()) {
      return Result<SecureMessage>("Invalid message type length");
    }
    msg.message_type = std::string(data.begin() + offset, data.begin() + offset + type_len);
    offset += type_len;
    
    // Timestamp (handle endianness)
    msg.timestamp_ms = 0;
    if (use_big_endian) {
      for (int i = 0; i < 8; ++i) {
        msg.timestamp_ms = (msg.timestamp_ms << 8) | data[offset + i];
      }
    } else {
      for (int i = 0; i < 8; ++i) {
        msg.timestamp_ms |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
      }
    }
    offset += 8;
    
    // Nonce (handle endianness)
    msg.nonce = 0;
    if (use_big_endian) {
      for (int i = 0; i < 8; ++i) {
        msg.nonce = (msg.nonce << 8) | data[offset + i];
      }
    } else {
      for (int i = 0; i < 8; ++i) {
        msg.nonce |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
      }
    }
    offset += 8;
    
    // Sender public key (32 bytes)
    if (offset + 32 > data.size()) {
      return Result<SecureMessage>("Invalid sender public key");
    }
    msg.sender_public_key.assign(data.begin() + offset, data.begin() + offset + 32);
    offset += 32;
    
    // IV (handle endianness)
    uint16_t iv_len;
    if (use_big_endian) {
      iv_len = (data[offset] << 8) | data[offset + 1];
    } else {
      iv_len = data[offset] | (data[offset + 1] << 8);
    }
    offset += 2;
    if (offset + iv_len > data.size()) {
      return Result<SecureMessage>("Invalid IV length");
    }
    msg.iv.assign(data.begin() + offset, data.begin() + offset + iv_len);
    offset += iv_len;
    
    // Auth tag (handle endianness)
    uint16_t tag_len;
    if (use_big_endian) {
      tag_len = (data[offset] << 8) | data[offset + 1];
    } else {
      tag_len = data[offset] | (data[offset + 1] << 8);
    }
    offset += 2;
    if (offset + tag_len > data.size()) {
      return Result<SecureMessage>("Invalid auth tag length");
    }
    msg.auth_tag.assign(data.begin() + offset, data.begin() + offset + tag_len);
    offset += tag_len;
    
    // Signature (handle endianness)
    uint16_t sig_len;
    if (use_big_endian) {
      sig_len = (data[offset] << 8) | data[offset + 1];
    } else {
      sig_len = data[offset] | (data[offset + 1] << 8);
    }
    offset += 2;
    if (offset + sig_len > data.size()) {
      return Result<SecureMessage>("Invalid signature length");
    }
    msg.signature.assign(data.begin() + offset, data.begin() + offset + sig_len);
    offset += sig_len;
    
    // Encrypted payload (handle endianness)
    uint32_t payload_len = 0;
    if (use_big_endian) {
      for (int i = 0; i < 4; ++i) {
        payload_len = (payload_len << 8) | data[offset + i];
      }
    } else {
      for (int i = 0; i < 4; ++i) {
        payload_len |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
      }
    }
    offset += 4;
    
    if (offset + payload_len != data.size()) {
      return Result<SecureMessage>("Invalid payload length");
    }
    msg.encrypted_payload.assign(data.begin() + offset, data.end());
    
    return Result<SecureMessage>(std::move(msg));
    
  } catch (const std::exception& e) {
    return Result<SecureMessage>("Deserialization failed: " + std::string(e.what()));
  }
}

// ============================================================================
// TlsContextManager Implementation
// ============================================================================

TlsContextManager::TlsContextManager(const SecureMessagingConfig& config)
    : config_(config), client_ctx_(nullptr), server_ctx_(nullptr) {
}

TlsContextManager::~TlsContextManager() {
  if (client_ctx_) {
    SSL_CTX_free(client_ctx_);
  }
  if (server_ctx_) {
    SSL_CTX_free(server_ctx_);
  }
}

Result<bool> TlsContextManager::initialize() {
  // Initialize OpenSSL
  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();
  
  // Create client context
  client_ctx_ = SSL_CTX_new(TLS_client_method());
  if (!client_ctx_) {
    return Result<bool>("Failed to create client SSL context: " + get_openssl_error());
  }
  
  // Create server context
  server_ctx_ = SSL_CTX_new(TLS_server_method());
  if (!server_ctx_) {
    return Result<bool>("Failed to create server SSL context: " + get_openssl_error());
  }
  
  // Configure TLS version
  SSL_CTX_set_min_proto_version(client_ctx_, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(client_ctx_, TLS1_3_VERSION);
  SSL_CTX_set_min_proto_version(server_ctx_, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(server_ctx_, TLS1_3_VERSION);
  
  // Load certificates and keys
  if (!load_certificates()) {
    return Result<bool>("Failed to load certificates");
  }
  
  // Setup verification
  if (!setup_verification()) {
    return Result<bool>("Failed to setup certificate verification");
  }
  
  return Result<bool>(true);
}

bool TlsContextManager::load_certificates() {
  // Load certificate files
  if (!config_.tls_cert_path.empty()) {
    if (SSL_CTX_use_certificate_file(server_ctx_, config_.tls_cert_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
      std::cerr << "Failed to load server certificate from " << config_.tls_cert_path 
                << ": " << get_openssl_error() << std::endl;
      return false;
    }
    if (SSL_CTX_use_certificate_file(client_ctx_, config_.tls_cert_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
      std::cerr << "Failed to load client certificate from " << config_.tls_cert_path 
                << ": " << get_openssl_error() << std::endl;
      return false;
    }
    std::cout << "✅ Loaded TLS certificate from " << config_.tls_cert_path << std::endl;
  }
  
  // Load private key files
  if (!config_.tls_key_path.empty()) {
    if (SSL_CTX_use_PrivateKey_file(server_ctx_, config_.tls_key_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
      std::cerr << "Failed to load server private key from " << config_.tls_key_path 
                << ": " << get_openssl_error() << std::endl;
      return false;
    }
    if (SSL_CTX_use_PrivateKey_file(client_ctx_, config_.tls_key_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
      std::cerr << "Failed to load client private key from " << config_.tls_key_path 
                << ": " << get_openssl_error() << std::endl;
      return false;
    }
    
    // Verify that the private key matches the certificate
    if (SSL_CTX_check_private_key(server_ctx_) != 1) {
      std::cerr << "Server private key does not match certificate: " << get_openssl_error() << std::endl;
      return false;
    }
    if (SSL_CTX_check_private_key(client_ctx_) != 1) {
      std::cerr << "Client private key does not match certificate: " << get_openssl_error() << std::endl;
      return false;
    }
    
    std::cout << "✅ Loaded and verified TLS private key from " << config_.tls_key_path << std::endl;
  }
  
  return true;
}

bool TlsContextManager::setup_verification() {
  if (config_.require_mutual_auth) {
    // Require peer certificates
    SSL_CTX_set_verify(client_ctx_, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, verify_callback);
    SSL_CTX_set_verify(server_ctx_, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, verify_callback);
    
    // Load CA certificates for verification
    if (!config_.ca_cert_path.empty()) {
      if (SSL_CTX_load_verify_locations(client_ctx_, config_.ca_cert_path.c_str(), nullptr) != 1) {
        std::cerr << "Failed to load CA certificate for client from " << config_.ca_cert_path 
                  << ": " << get_openssl_error() << std::endl;
        return false;
      }
      if (SSL_CTX_load_verify_locations(server_ctx_, config_.ca_cert_path.c_str(), nullptr) != 1) {
        std::cerr << "Failed to load CA certificate for server from " << config_.ca_cert_path 
                  << ": " << get_openssl_error() << std::endl;
        return false;
      }
      std::cout << "✅ Loaded CA certificate for mutual TLS from " << config_.ca_cert_path << std::endl;
    } else {
      std::cout << "⚠️  Mutual TLS enabled but no CA certificate specified - using default locations" << std::endl;
    }
  } else {
    std::cout << "ℹ️  Mutual TLS authentication disabled" << std::endl;
  }
  
  return true;
}

std::unique_ptr<SSL, void(*)(SSL*)> TlsContextManager::create_ssl_connection(bool is_client) {
  SSL_CTX* ctx = is_client ? client_ctx_ : server_ctx_;
  if (!ctx) {
    return std::unique_ptr<SSL, void(*)(SSL*)>(nullptr, SSL_free);
  }
  
  SSL* ssl = SSL_new(ctx);
  if (!ssl) {
    return std::unique_ptr<SSL, void(*)(SSL*)>(nullptr, SSL_free);
  }
  
  return std::unique_ptr<SSL, void(*)(SSL*)>(ssl, SSL_free);
}

int TlsContextManager::verify_callback(int preverify_ok, X509_STORE_CTX* ctx) {
  // Additional certificate verification logic can be added here
  return preverify_ok;
}

bool TlsContextManager::verify_peer_certificate(SSL* ssl, const std::string& expected_peer_id) {
  if (!ssl) {
    return false;
  }
  
  X509* peer_cert = SSL_get_peer_certificate(ssl);
  if (!peer_cert) {
    return false;
  }
  
  // Verify certificate chain
  long verify_result = SSL_get_verify_result(ssl);
  X509_free(peer_cert);
  
  return verify_result == X509_V_OK;
}

// ============================================================================
// MessageCrypto Implementation
// ============================================================================

MessageCrypto::MessageCrypto(const SecureMessagingConfig& config)
    : config_(config), signing_key_(nullptr) {
}

MessageCrypto::~MessageCrypto() {
  if (signing_key_) {
    EVP_PKEY_free(signing_key_);
  }
  for (auto& pair : peer_keys_) {
    EVP_PKEY_free(pair.second);
  }
}

Result<bool> MessageCrypto::initialize() {
  if (!load_signing_key()) {
    return Result<bool>("Failed to load signing key");
  }
  
  if (!load_peer_keys()) {
    return Result<bool>("Failed to load peer verification keys");
  }
  
  return Result<bool>(true);
}

bool MessageCrypto::load_signing_key() {
  if (config_.signing_key_path.empty()) {
    return true; // Optional
  }
  
  FILE* key_file = fopen(config_.signing_key_path.c_str(), "r");
  if (!key_file) {
    return false;
  }
  
  signing_key_ = PEM_read_PrivateKey(key_file, nullptr, nullptr, nullptr);
  fclose(key_file);
  
  return signing_key_ != nullptr;
}

bool MessageCrypto::load_peer_keys() {
  // Implementation would load peer public keys from directory
  // For now, return true as it's optional for basic functionality
  return true;
}

Result<SecureMessage> MessageCrypto::protect_message(
    const std::vector<uint8_t>& plaintext,
    const std::string& message_type,
    const std::string& recipient_id) {
  
  SecureMessage msg;
  msg.message_type = message_type;
  msg.timestamp_ms = get_current_timestamp_ms();
  msg.nonce = generate_nonce();
  
  try {
    if (config_.enable_message_encryption) {
      // Generate random IV for AES-GCM
      msg.iv = generate_random_bytes(12); // 96-bit IV for GCM
      
      // Use a node-specific symmetric key derived from signing key
      std::vector<uint8_t> symmetric_key;
      if (!derive_symmetric_key(symmetric_key)) {
        return Result<SecureMessage>("Failed to derive symmetric key");
      }
      
      // Encrypt with AES-GCM
      auto encrypt_result = encrypt_aes_gcm(plaintext, symmetric_key, msg.iv, msg.auth_tag);
      if (!encrypt_result.is_ok()) {
        return Result<SecureMessage>("AES-GCM encryption failed: " + encrypt_result.error());
      }
      msg.encrypted_payload = encrypt_result.value();
    } else {
      msg.encrypted_payload = plaintext;
    }
    
    if (config_.enable_message_signing && signing_key_) {
      // Extract public key from signing key
      auto pubkey_result = extract_public_key();
      if (!pubkey_result.is_ok()) {
        return Result<SecureMessage>("Failed to extract public key: " + pubkey_result.error());
      }
      msg.sender_public_key = pubkey_result.value();
      
      // Create signature over the message content (payload + metadata)
      auto signature_data = create_signature_data(msg);
      auto signature_result = sign_ed25519(signature_data);
      if (!signature_result.is_ok()) {
        return Result<SecureMessage>("Ed25519 signing failed: " + signature_result.error());
      }
      msg.signature = signature_result.value();
    } else {
      // No signing - leave signature empty
      msg.signature.clear();
      msg.sender_public_key.clear();
    }
    
    return Result<SecureMessage>(std::move(msg));
    
  } catch (const std::exception& e) {
    return Result<SecureMessage>("Message protection failed: " + std::string(e.what()));
  }
}

Result<std::vector<uint8_t>> MessageCrypto::unprotect_message(
    const SecureMessage& secure_msg,
    const std::string& sender_id) {
  
  // Validate message freshness
  if (config_.enable_replay_protection && !validate_message_freshness(secure_msg)) {
    return Result<std::vector<uint8_t>>("Message failed freshness validation");
  }
  
  // Verify signature if enabled
  if (config_.enable_message_signing) {
    if (!verify_message_signature(secure_msg, secure_msg.sender_public_key)) {
      return Result<std::vector<uint8_t>>("Message signature verification failed");
    }
  }
  
  // Decrypt if enabled
  if (config_.enable_message_encryption) {
    // Derive the same symmetric key used for encryption
    std::vector<uint8_t> symmetric_key;
    if (!derive_symmetric_key(symmetric_key)) {
      return Result<std::vector<uint8_t>>("Failed to derive symmetric key for decryption");
    }
    
    // Decrypt with AES-GCM
    auto decrypt_result = decrypt_aes_gcm(secure_msg.encrypted_payload, symmetric_key, 
                                        secure_msg.iv, secure_msg.auth_tag);
    if (!decrypt_result.is_ok()) {
      return Result<std::vector<uint8_t>>("AES-GCM decryption failed: " + decrypt_result.error());
    }
    return decrypt_result;
  } else {
    return Result<std::vector<uint8_t>>(secure_msg.encrypted_payload);
  }
}

bool MessageCrypto::verify_message_signature(const SecureMessage& secure_msg,
                                            const std::vector<uint8_t>& sender_public_key) {
  if (sender_public_key.size() != 32) {
    std::cerr << "Invalid public key size: " << sender_public_key.size() << " (expected 32)" << std::endl;
    return false;
  }
  
  if (secure_msg.signature.size() != 64) {
    std::cerr << "Invalid signature size: " << secure_msg.signature.size() << " (expected 64)" << std::endl;
    return false;
  }
  
  try {
    // Create the same signature data that was signed
    auto signature_data = create_signature_data(secure_msg);
    
    // Verify Ed25519 signature using OpenSSL
    auto verify_result = verify_ed25519(signature_data, secure_msg.signature, sender_public_key);
    if (!verify_result.is_ok()) {
      std::cerr << "Ed25519 verification failed: " << verify_result.error() << std::endl;
      return false;
    }
    
    return verify_result.value();
    
  } catch (const std::exception& e) {
    std::cerr << "Signature verification exception: " << e.what() << std::endl;
    return false;
  }
}

bool MessageCrypto::validate_message_freshness(const SecureMessage& secure_msg) {
  uint64_t current_time = get_current_timestamp_ms();
  
  // Check if message is within valid time window
  if (current_time > secure_msg.timestamp_ms + (config_.message_ttl_seconds * 1000)) {
    return false;
  }
  
  // Check for replay attack
  std::lock_guard<std::mutex> lock(nonce_mutex_);
  if (is_nonce_used(secure_msg.nonce)) {
    return false;
  }
  
  add_used_nonce(secure_msg.nonce);
  return true;
}

uint64_t MessageCrypto::generate_nonce() {
  std::random_device rd;
  std::mt19937_64 gen(rd());
  return gen();
}

bool MessageCrypto::is_nonce_used(uint64_t nonce) {
  auto now = std::chrono::steady_clock::now();
  auto it = used_nonces_.find(nonce);
  
  if (it == used_nonces_.end()) {
    return false; // Nonce not found, so not used
  }
  
  // Check if the nonce is expired
  auto ttl = std::chrono::seconds(config_.message_ttl_seconds);
  if (now - it->second > ttl) {
    // Nonce is expired, remove it and consider it not used
    used_nonces_.erase(it);
    return false;
  }
  
  return true; // Nonce exists and is still valid
}

void MessageCrypto::add_used_nonce(uint64_t nonce) {
  auto now = std::chrono::steady_clock::now();
  used_nonces_[nonce] = now;
  
  // Cleanup expired nonces and check size limit
  cleanup_expired_nonces();
  
  // If still too large after cleanup, remove oldest entries
  if (used_nonces_.size() > config_.nonce_cache_size) {
    // Find the oldest entry by timestamp
    auto oldest = std::min_element(used_nonces_.begin(), used_nonces_.end(),
                                  [](const auto& a, const auto& b) {
                                    return a.second < b.second;
                                  });
    
    if (oldest != used_nonces_.end()) {
      used_nonces_.erase(oldest);
    }
  }
}

void MessageCrypto::cleanup_expired_nonces() {
  auto now = std::chrono::steady_clock::now();
  auto ttl = std::chrono::seconds(config_.message_ttl_seconds);
  
  // Remove all nonces that are older than TTL
  auto it = used_nonces_.begin();
  while (it != used_nonces_.end()) {
    if (now - it->second > ttl) {
      it = used_nonces_.erase(it);
    } else {
      ++it;
    }
  }
}

// ============================================================================
// Cryptographic Implementation Methods
// ============================================================================

bool MessageCrypto::derive_symmetric_key(std::vector<uint8_t>& key) {
  if (!signing_key_) {
    return false;
  }
  
  // Derive a 32-byte symmetric key from the Ed25519 private key using HKDF
  key.resize(32);
  
  // Get raw key material from EVP_PKEY
  size_t raw_key_len = 32;
  std::vector<uint8_t> raw_key(raw_key_len);
  
  if (EVP_PKEY_get_raw_private_key(signing_key_, raw_key.data(), &raw_key_len) != 1) {
    std::cerr << "Failed to extract raw private key: " << get_openssl_error() << std::endl;
    return false;
  }
  
  // Use SHA-256 to derive symmetric key from Ed25519 private key
  EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
  if (!md_ctx) {
    return false;
  }
  
  if (EVP_DigestInit_ex(md_ctx, EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(md_ctx, raw_key.data(), raw_key_len) != 1 ||
      EVP_DigestUpdate(md_ctx, "slonana_symmetric_key", 20) != 1 ||
      EVP_DigestFinal_ex(md_ctx, key.data(), nullptr) != 1) {
    EVP_MD_CTX_free(md_ctx);
    return false;
  }
  
  EVP_MD_CTX_free(md_ctx);
  return true;
}

Result<std::vector<uint8_t>> MessageCrypto::encrypt_aes_gcm(
    const std::vector<uint8_t>& plaintext,
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& iv,
    std::vector<uint8_t>& auth_tag) {
  
  if (key.size() != 32) {
    return Result<std::vector<uint8_t>>("Invalid key size for AES-256-GCM");
  }
  
  if (iv.size() != 12) {
    return Result<std::vector<uint8_t>>("Invalid IV size for AES-GCM");
  }
  
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    return Result<std::vector<uint8_t>>("Failed to create cipher context");
  }
  
  std::vector<uint8_t> ciphertext(plaintext.size() + 16); // Extra space for tag
  int len;
  int ciphertext_len;
  
  try {
    // Initialize encryption
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
      throw std::runtime_error("Failed to initialize AES-256-GCM");
    }
    
    // Set IV length
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.size(), nullptr) != 1) {
      throw std::runtime_error("Failed to set IV length");
    }
    
    // Set key and IV
    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data()) != 1) {
      throw std::runtime_error("Failed to set key and IV");
    }
    
    // Encrypt plaintext
    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext.data(), plaintext.size()) != 1) {
      throw std::runtime_error("Failed to encrypt data");
    }
    ciphertext_len = len;
    
    // Finalize encryption
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
      throw std::runtime_error("Failed to finalize encryption");
    }
    ciphertext_len += len;
    
    // Get authentication tag
    auth_tag.resize(16);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, auth_tag.data()) != 1) {
      throw std::runtime_error("Failed to get authentication tag");
    }
    
    ciphertext.resize(ciphertext_len);
    EVP_CIPHER_CTX_free(ctx);
    
    return Result<std::vector<uint8_t>>(std::move(ciphertext));
    
  } catch (const std::exception& e) {
    EVP_CIPHER_CTX_free(ctx);
    return Result<std::vector<uint8_t>>("AES-GCM encryption failed: " + std::string(e.what()));
  }
}

Result<std::vector<uint8_t>> MessageCrypto::decrypt_aes_gcm(
    const std::vector<uint8_t>& ciphertext,
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& auth_tag) {
  
  if (key.size() != 32) {
    return Result<std::vector<uint8_t>>("Invalid key size for AES-256-GCM");
  }
  
  if (iv.size() != 12) {
    return Result<std::vector<uint8_t>>("Invalid IV size for AES-GCM");
  }
  
  if (auth_tag.size() != 16) {
    return Result<std::vector<uint8_t>>("Invalid authentication tag size");
  }
  
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    return Result<std::vector<uint8_t>>("Failed to create cipher context");
  }
  
  std::vector<uint8_t> plaintext(ciphertext.size());
  int len;
  int plaintext_len;
  
  try {
    // Initialize decryption
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
      throw std::runtime_error("Failed to initialize AES-256-GCM");
    }
    
    // Set IV length
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.size(), nullptr) != 1) {
      throw std::runtime_error("Failed to set IV length");
    }
    
    // Set key and IV
    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data()) != 1) {
      throw std::runtime_error("Failed to set key and IV");
    }
    
    // Decrypt ciphertext
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), ciphertext.size()) != 1) {
      throw std::runtime_error("Failed to decrypt data");
    }
    plaintext_len = len;
    
    // Set authentication tag
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, auth_tag.size(), 
                           const_cast<uint8_t*>(auth_tag.data())) != 1) {
      throw std::runtime_error("Failed to set authentication tag");
    }
    
    // Finalize decryption and verify tag
    int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
    if (ret <= 0) {
      throw std::runtime_error("Authentication tag verification failed");
    }
    plaintext_len += len;
    
    plaintext.resize(plaintext_len);
    EVP_CIPHER_CTX_free(ctx);
    
    return Result<std::vector<uint8_t>>(std::move(plaintext));
    
  } catch (const std::exception& e) {
    EVP_CIPHER_CTX_free(ctx);
    return Result<std::vector<uint8_t>>("AES-GCM decryption failed: " + std::string(e.what()));
  }
}

Result<std::vector<uint8_t>> MessageCrypto::extract_public_key() {
  if (!signing_key_) {
    return Result<std::vector<uint8_t>>("No signing key available");
  }
  
  size_t pub_key_len = 32;
  std::vector<uint8_t> pub_key(pub_key_len);
  
  if (EVP_PKEY_get_raw_public_key(signing_key_, pub_key.data(), &pub_key_len) != 1) {
    return Result<std::vector<uint8_t>>("Failed to extract public key: " + get_openssl_error());
  }
  
  if (pub_key_len != 32) {
    return Result<std::vector<uint8_t>>("Invalid public key length");
  }
  
  return Result<std::vector<uint8_t>>(std::move(pub_key));
}

std::vector<uint8_t> MessageCrypto::create_signature_data(const SecureMessage& msg) {
  std::vector<uint8_t> data;
  
  // Include all message fields except the signature itself
  // Use network byte order (big-endian) for better compatibility
  
  // Message type
  data.insert(data.end(), msg.message_type.begin(), msg.message_type.end());
  
  // Timestamp (8 bytes, big-endian)
  for (int i = 7; i >= 0; --i) {
    data.push_back((msg.timestamp_ms >> (i * 8)) & 0xFF);
  }
  
  // Nonce (8 bytes, big-endian)
  for (int i = 7; i >= 0; --i) {
    data.push_back((msg.nonce >> (i * 8)) & 0xFF);
  }
  
  // IV
  data.insert(data.end(), msg.iv.begin(), msg.iv.end());
  
  // Auth tag
  data.insert(data.end(), msg.auth_tag.begin(), msg.auth_tag.end());
  
  // Encrypted payload
  data.insert(data.end(), msg.encrypted_payload.begin(), msg.encrypted_payload.end());
  
  return data;
}

Result<std::vector<uint8_t>> MessageCrypto::sign_ed25519(const std::vector<uint8_t>& data) {
  if (!signing_key_) {
    return Result<std::vector<uint8_t>>("No signing key available");
  }
  
  EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
  if (!md_ctx) {
    return Result<std::vector<uint8_t>>("Failed to create signing context");
  }
  
  std::vector<uint8_t> signature(64); // Ed25519 signatures are always 64 bytes
  size_t sig_len = signature.size();
  
  try {
    if (EVP_DigestSignInit(md_ctx, nullptr, nullptr, nullptr, signing_key_) != 1) {
      throw std::runtime_error("Failed to initialize signing");
    }
    
    if (EVP_DigestSign(md_ctx, signature.data(), &sig_len, data.data(), data.size()) != 1) {
      throw std::runtime_error("Failed to sign data: " + get_openssl_error());
    }
    
    if (sig_len != 64) {
      throw std::runtime_error("Invalid signature length: " + std::to_string(sig_len));
    }
    
    EVP_MD_CTX_free(md_ctx);
    return Result<std::vector<uint8_t>>(std::move(signature));
    
  } catch (const std::exception& e) {
    EVP_MD_CTX_free(md_ctx);
    return Result<std::vector<uint8_t>>("Ed25519 signing failed: " + std::string(e.what()));
  }
}

Result<bool> MessageCrypto::verify_ed25519(const std::vector<uint8_t>& data,
                                          const std::vector<uint8_t>& signature,
                                          const std::vector<uint8_t>& public_key) {
  if (signature.size() != 64) {
    return Result<bool>("Invalid signature size");
  }
  
  if (public_key.size() != 32) {
    return Result<bool>("Invalid public key size");
  }
  
  // Create EVP_PKEY from raw public key
  EVP_PKEY* pub_key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, 
                                                  public_key.data(), public_key.size());
  if (!pub_key) {
    return Result<bool>("Failed to create public key: " + get_openssl_error());
  }
  
  EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
  if (!md_ctx) {
    EVP_PKEY_free(pub_key);
    return Result<bool>("Failed to create verification context");
  }
  
  try {
    if (EVP_DigestVerifyInit(md_ctx, nullptr, nullptr, nullptr, pub_key) != 1) {
      throw std::runtime_error("Failed to initialize verification");
    }
    
    int result = EVP_DigestVerify(md_ctx, signature.data(), signature.size(), 
                                 data.data(), data.size());
    
    EVP_MD_CTX_free(md_ctx);
    EVP_PKEY_free(pub_key);
    
    if (result == 1) {
      return Result<bool>(true);
    } else if (result == 0) {
      return Result<bool>(false); // Signature verification failed
    } else {
      return Result<bool>("Verification error: " + get_openssl_error());
    }
    
  } catch (const std::exception& e) {
    EVP_MD_CTX_free(md_ctx);
    EVP_PKEY_free(pub_key);
    return Result<bool>("Ed25519 verification failed: " + std::string(e.what()));
  }
}

// ============================================================================
// SecureMessaging Implementation
// ============================================================================

SecureMessaging::SecureMessaging(const SecureMessagingConfig& config)
    : config_(config), tls_manager_(config), message_crypto_(config) {
  memset(&stats_, 0, sizeof(stats_));
}

SecureMessaging::~SecureMessaging() = default;

Result<bool> SecureMessaging::initialize() {
  if (config_.enable_tls) {
    auto tls_result = tls_manager_.initialize();
    if (!tls_result.is_ok()) {
      return Result<bool>("TLS initialization failed: " + tls_result.error());
    }
  }
  
  if (config_.enable_message_signing || config_.enable_message_encryption) {
    auto crypto_result = message_crypto_.initialize();
    if (!crypto_result.is_ok()) {
      return Result<bool>("Message crypto initialization failed: " + crypto_result.error());
    }
  }
  
  return Result<bool>(true);
}

Result<std::vector<uint8_t>> SecureMessaging::prepare_outbound_message(
    const std::vector<uint8_t>& plaintext,
    const std::string& message_type,
    const std::string& recipient_id) {
  
  auto protect_result = message_crypto_.protect_message(plaintext, message_type, recipient_id);
  if (!protect_result.is_ok()) {
    return Result<std::vector<uint8_t>>(protect_result.error());
  }
  
  auto serialized = protect_result.value().serialize();
  update_stats("messages_encrypted");
  
  return Result<std::vector<uint8_t>>(std::move(serialized));
}

Result<std::vector<uint8_t>> SecureMessaging::process_inbound_message(
    const std::vector<uint8_t>& encrypted_data,
    const std::string& sender_id) {
  
  auto deserialize_result = SecureMessage::deserialize(encrypted_data);
  if (!deserialize_result.is_ok()) {
    return Result<std::vector<uint8_t>>(deserialize_result.error());
  }
  
  auto unprotect_result = message_crypto_.unprotect_message(deserialize_result.value(), sender_id);
  if (!unprotect_result.is_ok()) {
    if (unprotect_result.error().find("signature") != std::string::npos) {
      update_stats("invalid_signatures_rejected");
    } else if (unprotect_result.error().find("freshness") != std::string::npos) {
      update_stats("replay_attacks_blocked");
    }
    return unprotect_result;
  }
  
  update_stats("messages_decrypted");
  update_stats("signature_verifications");
  
  return unprotect_result;
}

bool SecureMessaging::is_peer_trusted(const std::string& peer_id) const {
  if (!config_.require_mutual_auth) {
    return true; // Trust all peers if mutual auth is not required
  }
  
  // Check if peer is in our trusted peer list
  std::lock_guard<std::mutex> lock(trusted_peers_mutex_);
  auto it = trusted_peers_.find(peer_id);
  if (it == trusted_peers_.end()) {
    std::cerr << "⚠️  Peer " << peer_id << " is not in trusted peer list" << std::endl;
    return false;
  }
  
  return true;
}

void SecureMessaging::add_trusted_peer(const std::string& peer_id, const std::vector<uint8_t>& public_key) {
  if (public_key.size() != 32) {
    std::cerr << "Invalid public key size for peer " << peer_id << ": " << public_key.size() << " (expected 32)" << std::endl;
    return;
  }
  
  std::lock_guard<std::mutex> lock(trusted_peers_mutex_);
  trusted_peers_[peer_id] = public_key;
  std::cout << "✅ Added trusted peer: " << peer_id << std::endl;
}

SecureMessaging::SecurityStats SecureMessaging::get_security_stats() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  return stats_;
}

void SecureMessaging::update_stats(const std::string& metric, uint64_t count) {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  
  if (metric == "messages_encrypted") {
    stats_.messages_encrypted += count;
  } else if (metric == "messages_decrypted") {
    stats_.messages_decrypted += count;
  } else if (metric == "signature_verifications") {
    stats_.signature_verifications += count;
  } else if (metric == "tls_handshakes_completed") {
    stats_.tls_handshakes_completed += count;
  } else if (metric == "replay_attacks_blocked") {
    stats_.replay_attacks_blocked += count;
  } else if (metric == "invalid_signatures_rejected") {
    stats_.invalid_signatures_rejected += count;
  }
}

// ============================================================================
// SecureChannel Implementation
// ============================================================================

SecureChannel::SecureChannel(std::unique_ptr<SSL, void(*)(SSL*)> ssl_connection)
    : ssl_(std::move(ssl_connection)), authenticated_(false) {
}

SecureChannel::~SecureChannel() = default;

Result<size_t> SecureChannel::send(const std::vector<uint8_t>& data) {
  if (!ssl_ || !is_secure()) {
    return Result<size_t>("Channel not secure");
  }
  
  int bytes_sent = SSL_write(ssl_.get(), data.data(), static_cast<int>(data.size()));
  if (bytes_sent <= 0) {
    int error = SSL_get_error(ssl_.get(), bytes_sent);
    return Result<size_t>("SSL_write failed with error: " + std::to_string(error));
  }
  
  return Result<size_t>(static_cast<size_t>(bytes_sent));
}

Result<std::vector<uint8_t>> SecureChannel::receive(size_t max_bytes) {
  if (!ssl_ || !is_secure()) {
    return Result<std::vector<uint8_t>>("Channel not secure");
  }
  
  std::vector<uint8_t> buffer(max_bytes);
  int bytes_received = SSL_read(ssl_.get(), buffer.data(), static_cast<int>(max_bytes));
  
  if (bytes_received <= 0) {
    int error = SSL_get_error(ssl_.get(), bytes_received);
    return Result<std::vector<uint8_t>>("SSL_read failed with error: " + std::to_string(error));
  }
  
  buffer.resize(static_cast<size_t>(bytes_received));
  return Result<std::vector<uint8_t>>(std::move(buffer));
}

bool SecureChannel::is_secure() const {
  return ssl_ && authenticated_ && SSL_is_init_finished(ssl_.get());
}

std::string SecureChannel::get_peer_identity() const {
  if (!ssl_) {
    return "";
  }
  
  X509* peer_cert = SSL_get_peer_certificate(ssl_.get());
  if (!peer_cert) {
    return "";
  }
  
  // Extract subject name or other identifier
  char* subject_name = X509_NAME_oneline(X509_get_subject_name(peer_cert), nullptr, 0);
  std::string identity = subject_name ? subject_name : "";
  
  if (subject_name) {
    OPENSSL_free(subject_name);
  }
  X509_free(peer_cert);
  
  return identity;
}

std::string SecureChannel::get_cipher_suite() const {
  if (!ssl_) {
    return "";
  }
  
  const char* cipher = SSL_get_cipher(ssl_.get());
  return cipher ? cipher : "";
}

bool SecureChannel::complete_handshake() {
  if (!ssl_) {
    return false;
  }
  
  int result = SSL_do_handshake(ssl_.get());
  if (result == 1) {
    authenticated_ = true;
    return true;
  }
  
  return false;
}

} // namespace security
} // namespace slonana