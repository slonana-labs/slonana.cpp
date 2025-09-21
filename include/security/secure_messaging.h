#pragma once

#include "common/types.h"
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <set>
#include <string>
#include <vector>

namespace slonana {
namespace security {

using namespace slonana::common;

/**
 * @brief Security configuration for secure inter-node messaging
 * 
 * Contains all cryptographic parameters and file paths needed for
 * secure communication between validator nodes.
 */
struct SecureMessagingConfig {
  // TLS Configuration
  bool enable_tls = true;                    ///< Enable TLS for all inter-node communication
  bool require_mutual_auth = true;          ///< Require mutual (two-way) TLS authentication
  std::string tls_cert_path;                ///< Path to TLS certificate file
  std::string tls_key_path;                 ///< Path to TLS private key file
  std::string ca_cert_path;                 ///< Path to CA certificate for peer verification
  std::string tls_protocols = "TLSv1.3";   ///< Allowed TLS protocol versions
  
  // Message-level Cryptography
  bool enable_message_signing = true;       ///< Enable message-level signatures
  bool enable_message_encryption = true;    ///< Enable message-level encryption
  std::string signing_key_path;             ///< Path to Ed25519 signing key
  std::string verification_keys_dir;       ///< Directory containing peer verification keys
  
  // Replay Protection
  bool enable_replay_protection = true;     ///< Enable nonce-based replay protection
  uint64_t message_ttl_seconds = 300;       ///< Message validity time-to-live (5 minutes)
  uint32_t nonce_cache_size = 10000;        ///< Maximum cached nonces for replay protection
  
  // Performance and Security Tuning
  std::string cipher_suite = "ECDHE-ECDSA-AES256-GCM-SHA384"; ///< Preferred cipher suite
  uint32_t handshake_timeout_ms = 10000;    ///< TLS handshake timeout
  uint32_t renegotiation_interval_hours = 24; ///< TLS session renegotiation interval
  bool enable_perfect_forward_secrecy = true; ///< Require PFS cipher suites
};

/**
 * @brief Encrypted and authenticated message container
 * 
 * Wraps application messages with cryptographic protections including
 * encryption, authentication, and replay protection.
 */
struct SecureMessage {
  std::vector<uint8_t> encrypted_payload;   ///< AES-GCM encrypted message content
  std::vector<uint8_t> signature;           ///< Ed25519 signature over payload + metadata
  std::vector<uint8_t> sender_public_key;   ///< Sender's Ed25519 public key
  uint64_t timestamp_ms;                    ///< Message creation timestamp
  uint64_t nonce;                           ///< Cryptographic nonce for replay protection
  std::vector<uint8_t> iv;                  ///< Initialization vector for AES-GCM
  std::vector<uint8_t> auth_tag;            ///< Authentication tag from AES-GCM
  std::string message_type;                 ///< Application message type identifier
  
  /// Serialize message for transmission
  std::vector<uint8_t> serialize() const;
  
  /// Deserialize message from received data
  static Result<SecureMessage> deserialize(const std::vector<uint8_t>& data);
};

/**
 * @brief TLS context manager for secure connections
 * 
 * Manages SSL/TLS contexts, certificates, and connection state for
 * secure inter-node communication channels.
 */
class TlsContextManager {
public:
  explicit TlsContextManager(const SecureMessagingConfig& config);
  ~TlsContextManager();
  
  /// Initialize TLS contexts with certificates and keys
  Result<bool> initialize();
  
  /// Get client TLS context for outbound connections
  SSL_CTX* get_client_context() const { return client_ctx_; }
  
  /// Get server TLS context for inbound connections  
  SSL_CTX* get_server_context() const { return server_ctx_; }
  
  /// Verify peer certificate during handshake
  bool verify_peer_certificate(SSL* ssl, const std::string& expected_peer_id = "");
  
  /// Create new SSL connection object
  std::unique_ptr<SSL, void(*)(SSL*)> create_ssl_connection(bool is_client);
  
private:
  SecureMessagingConfig config_;
  SSL_CTX* client_ctx_;
  SSL_CTX* server_ctx_;
  
  bool load_certificates();
  bool setup_verification();
  static int verify_callback(int preverify_ok, X509_STORE_CTX* ctx);
};

/**
 * @brief Message-level cryptographic operations
 * 
 * Provides encryption, decryption, signing, and verification for
 * application messages independent of transport-level security.
 */
class MessageCrypto {
public:
  explicit MessageCrypto(const SecureMessagingConfig& config);
  ~MessageCrypto();
  
  /// Initialize cryptographic keys and contexts
  Result<bool> initialize();
  
  /// Encrypt and sign an application message
  Result<SecureMessage> protect_message(const std::vector<uint8_t>& plaintext,
                                       const std::string& message_type,
                                       const std::string& recipient_id = "");
  
  /// Decrypt and verify a received secure message
  Result<std::vector<uint8_t>> unprotect_message(const SecureMessage& secure_msg,
                                                 const std::string& sender_id = "");
  
  /// Verify message signature without decryption
  bool verify_message_signature(const SecureMessage& secure_msg,
                               const std::vector<uint8_t>& sender_public_key);
  
  /// Check if message is within valid time window and not replayed
  bool validate_message_freshness(const SecureMessage& secure_msg);
  
private:
  SecureMessagingConfig config_;
  EVP_PKEY* signing_key_;
  std::map<std::string, EVP_PKEY*> peer_keys_;
  std::set<uint64_t> used_nonces_;
  std::mutex nonce_mutex_;
  
  bool load_signing_key();
  bool load_peer_keys();
  uint64_t generate_nonce();
  bool is_nonce_used(uint64_t nonce);
  void add_used_nonce(uint64_t nonce);
  void cleanup_expired_nonces();
  
  // Cryptographic implementation methods
  bool derive_symmetric_key(std::vector<uint8_t>& key);
  Result<std::vector<uint8_t>> encrypt_aes_gcm(const std::vector<uint8_t>& plaintext,
                                              const std::vector<uint8_t>& key,
                                              const std::vector<uint8_t>& iv,
                                              std::vector<uint8_t>& auth_tag);
  Result<std::vector<uint8_t>> decrypt_aes_gcm(const std::vector<uint8_t>& ciphertext,
                                              const std::vector<uint8_t>& key,
                                              const std::vector<uint8_t>& iv,
                                              const std::vector<uint8_t>& auth_tag);
  Result<std::vector<uint8_t>> extract_public_key();
  std::vector<uint8_t> create_signature_data(const SecureMessage& msg);
  Result<std::vector<uint8_t>> sign_ed25519(const std::vector<uint8_t>& data);
  Result<bool> verify_ed25519(const std::vector<uint8_t>& data,
                             const std::vector<uint8_t>& signature,
                             const std::vector<uint8_t>& public_key);
};

/**
 * @brief High-level secure messaging interface
 * 
 * Provides unified interface for secure inter-node communication
 * combining transport-level TLS with message-level cryptography.
 */
class SecureMessaging {
public:
  explicit SecureMessaging(const SecureMessagingConfig& config);
  ~SecureMessaging();
  
  /// Initialize secure messaging subsystem
  Result<bool> initialize();
  
  /// Get TLS context manager for connection setup
  TlsContextManager& get_tls_manager() { return tls_manager_; }
  
  /// Prepare message for secure transmission
  Result<std::vector<uint8_t>> prepare_outbound_message(
    const std::vector<uint8_t>& plaintext,
    const std::string& message_type,
    const std::string& recipient_id = "");
  
  /// Process received secure message
  Result<std::vector<uint8_t>> process_inbound_message(
    const std::vector<uint8_t>& encrypted_data,
    const std::string& sender_id = "");
  
  /// Check if peer identity is trusted
  bool is_peer_trusted(const std::string& peer_id) const;
  
  /// Add trusted peer identity
  void add_trusted_peer(const std::string& peer_id, const std::vector<uint8_t>& public_key);
  
  /// Get current security statistics
  struct SecurityStats {
    uint64_t messages_encrypted;
    uint64_t messages_decrypted;
    uint64_t signature_verifications;
    uint64_t tls_handshakes_completed;
    uint64_t replay_attacks_blocked;
    uint64_t invalid_signatures_rejected;
  };
  SecurityStats get_security_stats() const;
  
private:
  SecureMessagingConfig config_;
  TlsContextManager tls_manager_;
  MessageCrypto message_crypto_;
  mutable SecurityStats stats_;
  mutable std::mutex stats_mutex_;
  
  // Trusted peer management
  std::map<std::string, std::vector<uint8_t>> trusted_peers_;
  mutable std::mutex trusted_peers_mutex_;
  
  void update_stats(const std::string& metric, uint64_t count = 1);
};

/**
 * @brief Secure channel wrapper for network connections
 * 
 * Wraps existing network connections with transparent encryption
 * and authentication without changing application protocols.
 */
class SecureChannel {
public:
  SecureChannel(std::unique_ptr<SSL, void(*)(SSL*)> ssl_connection);
  ~SecureChannel();
  
  /// Send encrypted data over the secure channel
  Result<size_t> send(const std::vector<uint8_t>& data);
  
  /// Receive and decrypt data from the secure channel
  Result<std::vector<uint8_t>> receive(size_t max_bytes = 65536);
  
  /// Check if channel is properly established and authenticated
  bool is_secure() const;
  
  /// Get peer certificate information
  std::string get_peer_identity() const;
  
  /// Get current cipher suite in use
  std::string get_cipher_suite() const;
  
private:
  std::unique_ptr<SSL, void(*)(SSL*)> ssl_;
  bool authenticated_;
  
  bool complete_handshake();
};

} // namespace security
} // namespace slonana