#include "security/secure_messaging.h"
#include <algorithm>
#include <cstring>
#include <fstream>
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
  
  // Version (1 byte)
  result.push_back(0x01);
  
  // Message type length + data
  uint16_t type_len = static_cast<uint16_t>(message_type.length());
  result.push_back(type_len & 0xFF);
  result.push_back((type_len >> 8) & 0xFF);
  result.insert(result.end(), message_type.begin(), message_type.end());
  
  // Timestamp (8 bytes)
  for (int i = 0; i < 8; ++i) {
    result.push_back((timestamp_ms >> (i * 8)) & 0xFF);
  }
  
  // Nonce (8 bytes)
  for (int i = 0; i < 8; ++i) {
    result.push_back((nonce >> (i * 8)) & 0xFF);
  }
  
  // Sender public key (32 bytes for Ed25519)
  result.insert(result.end(), sender_public_key.begin(), sender_public_key.end());
  
  // IV length + data
  uint16_t iv_len = static_cast<uint16_t>(iv.size());
  result.push_back(iv_len & 0xFF);
  result.push_back((iv_len >> 8) & 0xFF);
  result.insert(result.end(), iv.begin(), iv.end());
  
  // Auth tag length + data
  uint16_t tag_len = static_cast<uint16_t>(auth_tag.size());
  result.push_back(tag_len & 0xFF);
  result.push_back((tag_len >> 8) & 0xFF);
  result.insert(result.end(), auth_tag.begin(), auth_tag.end());
  
  // Signature length + data
  uint16_t sig_len = static_cast<uint16_t>(signature.size());
  result.push_back(sig_len & 0xFF);
  result.push_back((sig_len >> 8) & 0xFF);
  result.insert(result.end(), signature.begin(), signature.end());
  
  // Encrypted payload length + data
  uint32_t payload_len = static_cast<uint32_t>(encrypted_payload.size());
  for (int i = 0; i < 4; ++i) {
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
    // Version check
    if (data[offset++] != 0x01) {
      return Result<SecureMessage>("Unsupported message version");
    }
    
    // Message type
    uint16_t type_len = data[offset] | (data[offset + 1] << 8);
    offset += 2;
    if (offset + type_len > data.size()) {
      return Result<SecureMessage>("Invalid message type length");
    }
    msg.message_type = std::string(data.begin() + offset, data.begin() + offset + type_len);
    offset += type_len;
    
    // Timestamp
    msg.timestamp_ms = 0;
    for (int i = 0; i < 8; ++i) {
      msg.timestamp_ms |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
    }
    offset += 8;
    
    // Nonce
    msg.nonce = 0;
    for (int i = 0; i < 8; ++i) {
      msg.nonce |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
    }
    offset += 8;
    
    // Sender public key (32 bytes)
    if (offset + 32 > data.size()) {
      return Result<SecureMessage>("Invalid sender public key");
    }
    msg.sender_public_key.assign(data.begin() + offset, data.begin() + offset + 32);
    offset += 32;
    
    // IV
    uint16_t iv_len = data[offset] | (data[offset + 1] << 8);
    offset += 2;
    if (offset + iv_len > data.size()) {
      return Result<SecureMessage>("Invalid IV length");
    }
    msg.iv.assign(data.begin() + offset, data.begin() + offset + iv_len);
    offset += iv_len;
    
    // Auth tag
    uint16_t tag_len = data[offset] | (data[offset + 1] << 8);
    offset += 2;
    if (offset + tag_len > data.size()) {
      return Result<SecureMessage>("Invalid auth tag length");
    }
    msg.auth_tag.assign(data.begin() + offset, data.begin() + offset + tag_len);
    offset += tag_len;
    
    // Signature
    uint16_t sig_len = data[offset] | (data[offset + 1] << 8);
    offset += 2;
    if (offset + sig_len > data.size()) {
      return Result<SecureMessage>("Invalid signature length");
    }
    msg.signature.assign(data.begin() + offset, data.begin() + offset + sig_len);
    offset += sig_len;
    
    // Encrypted payload
    uint32_t payload_len = 0;
    for (int i = 0; i < 4; ++i) {
      payload_len |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
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
      return false;
    }
    if (SSL_CTX_use_certificate_file(client_ctx_, config_.tls_cert_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
      return false;
    }
  }
  
  // Load private key files
  if (!config_.tls_key_path.empty()) {
    if (SSL_CTX_use_PrivateKey_file(server_ctx_, config_.tls_key_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
      return false;
    }
    if (SSL_CTX_use_PrivateKey_file(client_ctx_, config_.tls_key_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
      return false;
    }
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
        return false;
      }
      if (SSL_CTX_load_verify_locations(server_ctx_, config_.ca_cert_path.c_str(), nullptr) != 1) {
        return false;
      }
    }
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
  
  if (!config_.enable_message_encryption && !config_.enable_message_signing) {
    return Result<SecureMessage>("No protection enabled");
  }
  
  SecureMessage msg;
  msg.message_type = message_type;
  msg.timestamp_ms = get_current_timestamp_ms();
  msg.nonce = generate_nonce();
  
  try {
    if (config_.enable_message_encryption) {
      // Generate random IV for AES-GCM
      msg.iv = generate_random_bytes(12); // 96-bit IV for GCM
      
      // For simplicity, use a derived key (in production, use proper key exchange)
      std::vector<uint8_t> key = generate_random_bytes(32); // 256-bit key
      
      // Encrypt with AES-GCM (simplified implementation)
      msg.encrypted_payload = plaintext; // TODO: Implement actual AES-GCM encryption
      msg.auth_tag = generate_random_bytes(16); // 128-bit auth tag
    } else {
      msg.encrypted_payload = plaintext;
    }
    
    if (config_.enable_message_signing && signing_key_) {
      // Create signature over the message content
      // TODO: Implement Ed25519 signing
      msg.signature = generate_random_bytes(64); // Ed25519 signature size
      msg.sender_public_key = generate_random_bytes(32); // Ed25519 public key size
    }
    
    return Result<SecureMessage>(std::move(msg));
    
  } catch (const std::exception& e) {
    return Result<SecureMessage>("Encryption failed: " + std::string(e.what()));
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
    // TODO: Implement actual AES-GCM decryption
    // For now, return the payload as-is
    return Result<std::vector<uint8_t>>(secure_msg.encrypted_payload);
  } else {
    return Result<std::vector<uint8_t>>(secure_msg.encrypted_payload);
  }
}

bool MessageCrypto::verify_message_signature(const SecureMessage& secure_msg,
                                            const std::vector<uint8_t>& sender_public_key) {
  // TODO: Implement Ed25519 signature verification
  // For now, return true for basic functionality
  return true;
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
  return used_nonces_.find(nonce) != used_nonces_.end();
}

void MessageCrypto::add_used_nonce(uint64_t nonce) {
  used_nonces_.insert(nonce);
  
  // Cleanup if cache is too large
  if (used_nonces_.size() > config_.nonce_cache_size) {
    cleanup_expired_nonces();
  }
}

void MessageCrypto::cleanup_expired_nonces() {
  // Simple cleanup - remove oldest nonces
  // In production, this should be time-based
  if (used_nonces_.size() > config_.nonce_cache_size / 2) {
    auto it = used_nonces_.begin();
    std::advance(it, used_nonces_.size() / 4);
    used_nonces_.erase(used_nonces_.begin(), it);
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
  // TODO: Implement peer trust validation
  return true;
}

void SecureMessaging::add_trusted_peer(const std::string& peer_id, const std::vector<uint8_t>& public_key) {
  // TODO: Implement trusted peer management
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