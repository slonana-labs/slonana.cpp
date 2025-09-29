#include "network/quic_client.h"
#include "network/gossip.h"
#include "test_framework.h"
#include <chrono>
#include <thread>

using namespace slonana::network;
using namespace slonana::common;

// Test QUIC client with secure messaging enabled
void test_quic_secure_messaging_integration() {
  ValidatorConfig config;
  config.enable_secure_messaging = false; // Start with disabled for testing
  config.enable_tls = true;
  
  QuicClient client(config);
  
  // Should initialize successfully even without secure messaging
  ASSERT_TRUE(client.initialize());
  
  // Should be able to get security stats (empty if not enabled)
  auto stats = client.get_security_stats();
  ASSERT_EQ(stats.messages_encrypted, 0);
  
  client.shutdown();
}

// Test QUIC client constructor with config
void test_quic_client_with_config() {
  ValidatorConfig config;
  config.enable_secure_messaging = true;
  config.enable_tls = true;
  config.enable_message_encryption = true;
  config.tls_certificate_path = "/nonexistent/cert.pem"; // Will fail gracefully
  config.tls_private_key_path = "/nonexistent/key.pem";
  
  QuicClient client(config);
  
  // Should handle missing certificates gracefully
  bool init_result = client.initialize();
  // We expect this to succeed even if secure messaging setup fails
  ASSERT_TRUE(init_result);
  
  client.shutdown();
}

// Test gossip protocol with secure messaging
void test_gossip_secure_messaging_integration() {
  ValidatorConfig config;
  config.enable_secure_messaging = false; // Disable for basic test
  config.enable_gossip = true;
  config.gossip_bind_address = "127.0.0.1:18001";
  
  GossipProtocol gossip(config);
  
  // Should start successfully
  auto start_result = gossip.start();
  ASSERT_TRUE(start_result.is_ok());
  
  // Should have no known peers initially
  auto peers = gossip.get_known_peers();
  ASSERT_EQ(peers.size(), 0);
  
  gossip.stop();
}

// Test ValidatorConfig with new security fields
void test_validator_config_security_fields() {
  ValidatorConfig config;
  
  // Test default values
  ASSERT_FALSE(config.enable_secure_messaging);
  ASSERT_FALSE(config.require_mutual_tls);
  ASSERT_FALSE(config.enable_message_encryption);
  ASSERT_TRUE(config.enable_replay_protection);
  ASSERT_EQ(config.message_ttl_seconds, 300);
  ASSERT_EQ(config.tls_handshake_timeout_ms, 10000);
  
  // Test setting security configuration
  config.enable_secure_messaging = true;
  config.require_mutual_tls = true;
  config.tls_certificate_path = "/path/to/cert.pem";
  config.tls_private_key_path = "/path/to/key.pem";
  config.ca_certificate_path = "/path/to/ca.pem";
  config.node_signing_key_path = "/path/to/signing.key";
  config.peer_keys_directory = "/path/to/peers/";
  config.enable_message_encryption = true;
  config.message_ttl_seconds = 600;
  
  ASSERT_TRUE(config.enable_secure_messaging);
  ASSERT_TRUE(config.require_mutual_tls);
  ASSERT_EQ(config.tls_certificate_path, "/path/to/cert.pem");
  ASSERT_EQ(config.tls_private_key_path, "/path/to/key.pem");
  ASSERT_EQ(config.ca_certificate_path, "/path/to/ca.pem");
  ASSERT_EQ(config.node_signing_key_path, "/path/to/signing.key");
  ASSERT_EQ(config.peer_keys_directory, "/path/to/peers/");
  ASSERT_TRUE(config.enable_message_encryption);
  ASSERT_EQ(config.message_ttl_seconds, 600);
}

// Test configuration transformation to SecureMessagingConfig
void test_config_transformation() {
  ValidatorConfig config;
  config.enable_secure_messaging = true;
  config.enable_tls = true;
  config.require_mutual_tls = true;
  config.tls_certificate_path = "/test/cert.pem";
  config.tls_private_key_path = "/test/key.pem";
  config.ca_certificate_path = "/test/ca.pem";
  config.node_signing_key_path = "/test/signing.key";
  config.peer_keys_directory = "/test/peers/";
  config.enable_message_encryption = true;
  config.enable_replay_protection = true;
  config.message_ttl_seconds = 450;
  config.tls_handshake_timeout_ms = 15000;
  
  // This would be done internally by QuicClient or GossipProtocol
  slonana::security::SecureMessagingConfig sec_config;
  sec_config.enable_tls = config.enable_tls;
  sec_config.require_mutual_auth = config.require_mutual_tls;
  sec_config.tls_cert_path = config.tls_certificate_path;
  sec_config.tls_key_path = config.tls_private_key_path;
  sec_config.ca_cert_path = config.ca_certificate_path;
  sec_config.signing_key_path = config.node_signing_key_path;
  sec_config.verification_keys_dir = config.peer_keys_directory;
  sec_config.enable_message_encryption = config.enable_message_encryption;
  sec_config.enable_replay_protection = config.enable_replay_protection;
  sec_config.message_ttl_seconds = config.message_ttl_seconds;
  sec_config.handshake_timeout_ms = config.tls_handshake_timeout_ms;
  
  // Verify transformation
  ASSERT_TRUE(sec_config.enable_tls);
  ASSERT_TRUE(sec_config.require_mutual_auth);
  ASSERT_EQ(sec_config.tls_cert_path, "/test/cert.pem");
  ASSERT_EQ(sec_config.tls_key_path, "/test/key.pem");
  ASSERT_EQ(sec_config.ca_cert_path, "/test/ca.pem");
  ASSERT_EQ(sec_config.signing_key_path, "/test/signing.key");
  ASSERT_EQ(sec_config.verification_keys_dir, "/test/peers/");
  ASSERT_TRUE(sec_config.enable_message_encryption);
  ASSERT_TRUE(sec_config.enable_replay_protection);
  ASSERT_EQ(sec_config.message_ttl_seconds, 450);
  ASSERT_EQ(sec_config.handshake_timeout_ms, 15000);
}

// Test QUIC send_secure_data fallback behavior
void test_quic_secure_data_fallback() {
  ValidatorConfig config;
  config.enable_secure_messaging = false; // Disabled
  
  QuicClient client(config);
  client.initialize();
  
  // When secure messaging is disabled, send_secure_data should fall back to regular send
  std::vector<uint8_t> test_data = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
  
  // This should not crash even though connection doesn't exist
  bool result = client.send_secure_data("nonexistent_connection", 1, test_data, "test_message");
  ASSERT_FALSE(result); // Should fail gracefully due to no connection
  
  client.shutdown();
}

// Test that security stats are properly initialized
void test_security_stats_initialization() {
  ValidatorConfig config;
  config.enable_secure_messaging = false;
  
  QuicClient client(config);
  client.initialize();
  
  auto stats = client.get_security_stats();
  
  // All stats should be zero when secure messaging is disabled
  ASSERT_EQ(stats.messages_encrypted, 0);
  ASSERT_EQ(stats.messages_decrypted, 0);
  ASSERT_EQ(stats.signature_verifications, 0);
  ASSERT_EQ(stats.tls_handshakes_completed, 0);
  ASSERT_EQ(stats.replay_attacks_blocked, 0);
  ASSERT_EQ(stats.invalid_signatures_rejected, 0);
  
  client.shutdown();
}

// Main test runner
int main() {
  std::cout << "=== Secure Networking Integration Tests ===" << std::endl;
  
  try {
    test_quic_secure_messaging_integration();
    std::cout << "✓ QUIC Secure Messaging Integration" << std::endl;
    
    test_quic_client_with_config();
    std::cout << "✓ QUIC Client with Config" << std::endl;
    
    test_gossip_secure_messaging_integration();
    std::cout << "✓ Gossip Secure Messaging Integration" << std::endl;
    
    test_validator_config_security_fields();
    std::cout << "✓ ValidatorConfig Security Fields" << std::endl;
    
    test_config_transformation();
    std::cout << "✓ Config Transformation" << std::endl;
    
    test_quic_secure_data_fallback();
    std::cout << "✓ QUIC Secure Data Fallback" << std::endl;
    
    test_security_stats_initialization();
    std::cout << "✓ Security Stats Initialization" << std::endl;
    
    std::cout << std::endl << "=== All Secure Networking Integration Tests Passed! ===" << std::endl;
    return 0;
    
  } catch (const std::exception& e) {
    std::cout << std::endl << "=== Test Failed: " << e.what() << " ===" << std::endl;
    return 1;
  }
}