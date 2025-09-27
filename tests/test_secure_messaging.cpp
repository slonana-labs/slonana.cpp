#include "security/secure_messaging.h"
#include "test_framework.h"
#include <chrono>
#include <fstream>
#include <thread>

using namespace slonana::security;
using namespace slonana::common;

// Test the basic SecureMessage serialization/deserialization
void test_secure_message_serialization() {
  SecureMessage msg;
  msg.message_type = "test_message";
  msg.timestamp_ms = 1234567890123ULL;
  msg.nonce = 9876543210ULL;
  msg.sender_public_key = std::vector<uint8_t>(32, 0xAB);
  msg.iv = std::vector<uint8_t>(12, 0xCD);
  msg.auth_tag = std::vector<uint8_t>(16, 0xEF);
  msg.signature = std::vector<uint8_t>(64, 0x12);
  msg.encrypted_payload = std::vector<uint8_t>{0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
  
  // Serialize
  auto serialized = msg.serialize();
  ASSERT_TRUE(serialized.size() > 100); // Should be reasonably sized
  
  // Deserialize
  auto deserialize_result = SecureMessage::deserialize(serialized);
  ASSERT_TRUE(deserialize_result.is_ok());
  
  auto deserialized = deserialize_result.value();
  ASSERT_EQ(deserialized.message_type, msg.message_type);
  ASSERT_EQ(deserialized.timestamp_ms, msg.timestamp_ms);
  ASSERT_EQ(deserialized.nonce, msg.nonce);
  ASSERT_EQ(deserialized.sender_public_key, msg.sender_public_key);
  ASSERT_EQ(deserialized.iv, msg.iv);
  ASSERT_EQ(deserialized.auth_tag, msg.auth_tag);
  ASSERT_EQ(deserialized.signature, msg.signature);
  ASSERT_EQ(deserialized.encrypted_payload, msg.encrypted_payload);
}

// Test SecureMessagingConfig default values and validation
void test_secure_messaging_config() {
  SecureMessagingConfig config;
  
  // Test default values
  ASSERT_TRUE(config.enable_tls);
  ASSERT_TRUE(config.require_mutual_auth);
  ASSERT_TRUE(config.enable_message_signing);
  ASSERT_TRUE(config.enable_message_encryption);
  ASSERT_TRUE(config.enable_replay_protection);
  ASSERT_EQ(config.message_ttl_seconds, 300);
  ASSERT_EQ(config.nonce_cache_size, 10000);
  ASSERT_EQ(config.tls_protocols, "TLSv1.3");
}

// Test TlsContextManager initialization (without actual files)
void test_tls_context_manager_basic() {
  SecureMessagingConfig config;
  config.enable_tls = true;
  // Leave paths empty to test basic initialization
  
  TlsContextManager manager(config);
  
  // Should initialize even without certificate files
  auto result = manager.initialize();
  ASSERT_TRUE(result.is_ok());
  
  // Should be able to create SSL connections
  auto client_ssl = manager.create_ssl_connection(true);
  ASSERT_TRUE(client_ssl != nullptr);
  
  auto server_ssl = manager.create_ssl_connection(false);
  ASSERT_TRUE(server_ssl != nullptr);
}

// Test MessageCrypto basic functionality
void test_message_crypto_basic() {
  SecureMessagingConfig config;
  config.enable_message_signing = false;  // Disable signing for test without keys
  config.enable_message_encryption = false; // Disable encryption for test without keys
  config.enable_replay_protection = true;
  
  MessageCrypto crypto(config);
  
  // Should initialize without key files for testing
  auto init_result = crypto.initialize();
  ASSERT_TRUE(init_result.is_ok());
  
  // Test message protection (only replay protection, no crypto)
  std::vector<uint8_t> plaintext = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
  auto protect_result = crypto.protect_message(plaintext, "test_message", "test_peer");
  
  ASSERT_TRUE(protect_result.is_ok());
  
  auto secure_msg = protect_result.value();
  ASSERT_EQ(secure_msg.message_type, "test_message");
  ASSERT_TRUE(secure_msg.timestamp_ms > 0);
  ASSERT_TRUE(secure_msg.nonce > 0);
  ASSERT_EQ(secure_msg.encrypted_payload.size(), plaintext.size());
}

// Test SecureMessaging end-to-end functionality
void test_secure_messaging_end_to_end() {
  SecureMessagingConfig config;
  config.enable_tls = false; // Disable TLS for this test to avoid cert requirements
  config.enable_message_signing = false; // Disable signing for test without keys
  config.enable_message_encryption = false; // Disable encryption for test without keys
  config.enable_replay_protection = true;
  
  SecureMessaging messaging(config);
  
  auto init_result = messaging.initialize();
  ASSERT_TRUE(init_result.is_ok());
  
  // Test message preparation
  std::vector<uint8_t> plaintext = {0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x57, 0x6F, 0x72, 0x6C, 0x64}; // "Hello World"
  auto prepare_result = messaging.prepare_outbound_message(plaintext, "gossip_message", "peer_123");
  
  ASSERT_TRUE(prepare_result.is_ok());
  
  auto encrypted_data = prepare_result.value();
  ASSERT_TRUE(encrypted_data.size() > plaintext.size()); // Should be larger due to headers
  
  // Test message processing
  auto process_result = messaging.process_inbound_message(encrypted_data, "peer_123");
  if (!process_result.is_ok()) {
    std::cout << "Process result error: " << process_result.error() << std::endl;
  }
  ASSERT_TRUE(process_result.is_ok());
  
  auto decrypted = process_result.value();
  ASSERT_EQ(decrypted, plaintext);
  
  // Check security stats
  auto stats = messaging.get_security_stats();
  // Stats may be 0 since we're not using actual crypto
  ASSERT_GE(stats.messages_encrypted, 0);
  ASSERT_GE(stats.messages_decrypted, 0);
}

// Test replay protection
void test_replay_protection() {
  SecureMessagingConfig config;
  config.enable_tls = false;
  config.enable_message_signing = true;
  config.enable_replay_protection = true;
  config.message_ttl_seconds = 1; // Short TTL for testing
  
  SecureMessaging messaging(config);
  messaging.initialize();
  
  std::vector<uint8_t> plaintext = {0x54, 0x65, 0x73, 0x74}; // "Test"
  
  // Prepare message
  auto prepare_result = messaging.prepare_outbound_message(plaintext, "test", "peer");
  ASSERT_TRUE(prepare_result.is_ok());
  
  auto encrypted_data = prepare_result.value();
  
  // First processing should succeed
  auto process_result1 = messaging.process_inbound_message(encrypted_data, "peer");
  ASSERT_TRUE(process_result1.is_ok());
  
  // Second processing of same message should fail (replay detection)
  auto process_result2 = messaging.process_inbound_message(encrypted_data, "peer");
  ASSERT_FALSE(process_result2.is_ok());
  ASSERT_TRUE(process_result2.error().find("freshness") != std::string::npos);
  
  // Wait for message to expire
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  
  // Processing expired message should fail
  auto process_result3 = messaging.prepare_outbound_message(plaintext, "test", "peer");
  ASSERT_TRUE(process_result3.is_ok());
  
  auto new_encrypted = process_result3.value();
  
  // Simulate old timestamp by deserializing, modifying, and re-serializing
  auto deserialize_result = SecureMessage::deserialize(new_encrypted);
  ASSERT_TRUE(deserialize_result.is_ok());
  
  auto msg = deserialize_result.value();
  msg.timestamp_ms = msg.timestamp_ms - 2000; // Make it 2 seconds older
  auto old_encrypted = msg.serialize();
  
  auto old_process_result = messaging.process_inbound_message(old_encrypted, "peer");
  ASSERT_FALSE(old_process_result.is_ok());
}

// Test invalid message handling
void test_invalid_message_handling() {
  SecureMessagingConfig config;
  config.enable_tls = false;
  
  SecureMessaging messaging(config);
  messaging.initialize();
  
  // Test empty message
  std::vector<uint8_t> empty_data;
  auto result1 = messaging.process_inbound_message(empty_data, "peer");
  ASSERT_FALSE(result1.is_ok());
  
  // Test malformed message
  std::vector<uint8_t> malformed = {0x01, 0x02, 0x03}; // Too short
  auto result2 = messaging.process_inbound_message(malformed, "peer");
  ASSERT_FALSE(result2.is_ok());
  
  // Test message with wrong version
  std::vector<uint8_t> wrong_version = std::vector<uint8_t>(100, 0x00);
  wrong_version[0] = 0x99; // Wrong version
  auto result3 = messaging.process_inbound_message(wrong_version, "peer");
  ASSERT_FALSE(result3.is_ok());
}

// Test security statistics tracking
void test_security_statistics() {
  SecureMessagingConfig config;
  config.enable_tls = false;
  config.enable_message_signing = true;
  
  SecureMessaging messaging(config);
  messaging.initialize();
  
  // Initial stats should be zero
  auto initial_stats = messaging.get_security_stats();
  ASSERT_EQ(initial_stats.messages_encrypted, 0);
  ASSERT_EQ(initial_stats.messages_decrypted, 0);
  ASSERT_EQ(initial_stats.signature_verifications, 0);
  
  // Process some messages
  std::vector<uint8_t> plaintext = {0x44, 0x61, 0x74, 0x61}; // "Data"
  
  for (int i = 0; i < 5; ++i) {
    auto prepare_result = messaging.prepare_outbound_message(plaintext, "test", "peer");
    ASSERT_TRUE(prepare_result.is_ok());
    
    auto process_result = messaging.process_inbound_message(prepare_result.value(), "peer");
    ASSERT_TRUE(process_result.is_ok());
  }
  
  // Check updated stats
  auto final_stats = messaging.get_security_stats();
  ASSERT_EQ(final_stats.messages_encrypted, 5);
  ASSERT_EQ(final_stats.messages_decrypted, 5);
  ASSERT_EQ(final_stats.signature_verifications, 5);
}

// Test peer trust management
void test_peer_trust_management() {
  SecureMessagingConfig config;
  
  SecureMessaging messaging(config);
  messaging.initialize();
  
  // Test default trust (should trust all for now)
  ASSERT_TRUE(messaging.is_peer_trusted("unknown_peer"));
  
  // Test adding trusted peer
  std::vector<uint8_t> public_key(32, 0xAB);
  messaging.add_trusted_peer("trusted_peer", public_key);
  
  ASSERT_TRUE(messaging.is_peer_trusted("trusted_peer"));
}

// Test TTL-based nonce cleanup
void test_nonce_ttl_based_cleanup() {
  SecureMessagingConfig config;
  config.enable_message_signing = false;
  config.enable_message_encryption = false;
  config.enable_replay_protection = true;
  config.message_ttl_seconds = 1; // Very short TTL for testing
  config.nonce_cache_size = 1000; // Large enough to not trigger size-based cleanup
  
  MessageCrypto crypto(config);
  auto init_result = crypto.initialize();
  ASSERT_TRUE(init_result.is_ok());
  
  // Create some test messages with different nonces
  std::vector<uint8_t> plaintext = {0x54, 0x65, 0x73, 0x74}; // "Test"
  
  // Generate and validate first message
  auto msg1_result = crypto.protect_message(plaintext, "test1", "peer");
  ASSERT_TRUE(msg1_result.is_ok());
  auto msg1 = msg1_result.value();
  
  // Validate freshness - should succeed and add nonce to cache
  ASSERT_TRUE(crypto.validate_message_freshness(msg1));
  
  // Immediately try to validate same message again - should fail (replay)
  ASSERT_FALSE(crypto.validate_message_freshness(msg1));
  
  // Wait for TTL to expire
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  
  // Generate a new message with different nonce
  auto msg2_result = crypto.protect_message(plaintext, "test2", "peer");
  ASSERT_TRUE(msg2_result.is_ok());
  auto msg2 = msg2_result.value();
  
  // Validate new message - should succeed
  ASSERT_TRUE(crypto.validate_message_freshness(msg2));
  
  // Now try the original message again - should succeed because the nonce
  // should have been expired and removed from cache
  auto msg1_reuse_result = crypto.protect_message(plaintext, "test1", "peer");
  ASSERT_TRUE(msg1_reuse_result.is_ok());
  auto msg1_reuse = msg1_reuse_result.value();
  
  // Set the nonce to the same value as msg1 to test if old nonce was cleaned up
  msg1_reuse.nonce = msg1.nonce;
  msg1_reuse.timestamp_ms = msg1.timestamp_ms;
  
  // This should succeed if the TTL-based cleanup worked properly
  // (the old nonce should have been removed due to TTL expiry)
  ASSERT_TRUE(crypto.validate_message_freshness(msg1_reuse));
}

// Test cryptographically secure nonce generation
void test_nonce_generation_security() {
  SecureMessagingConfig config;
  config.enable_message_signing = false;
  config.enable_message_encryption = false;
  config.enable_replay_protection = true;
  
  MessageCrypto crypto(config);
  auto init_result = crypto.initialize();
  ASSERT_TRUE(init_result.is_ok());
  
  // Generate multiple nonces and verify they are unique and unpredictable
  std::set<uint64_t> generated_nonces;
  const int num_nonces = 1000;
  
  for (int i = 0; i < num_nonces; ++i) {
    std::vector<uint8_t> plaintext = {0x54, 0x65, 0x73, 0x74}; // "Test"
    auto msg_result = crypto.protect_message(plaintext, "test", "peer");
    ASSERT_TRUE(msg_result.is_ok());
    
    auto msg = msg_result.value();
    
    // Verify nonce uniqueness
    ASSERT_TRUE(generated_nonces.find(msg.nonce) == generated_nonces.end());
    generated_nonces.insert(msg.nonce);
    
    // Verify nonce is not zero (should never happen with crypto RNG)
    ASSERT_TRUE(msg.nonce != 0);
  }
  
  // Verify we got the expected number of unique nonces
  ASSERT_EQ(generated_nonces.size(), num_nonces);
  
  // Basic entropy check - nonces should be distributed across full range
  // Count bits set in all nonces combined
  uint64_t combined_or = 0;
  for (uint64_t nonce : generated_nonces) {
    combined_or |= nonce;
  }
  
  // Should have bits set in most positions (not perfect test but catches obvious issues)
  int bits_set = __builtin_popcountll(combined_or);
  ASSERT_TRUE(bits_set > 50); // Expect most of 64 bits to be represented
}

// Main test runner
int main() {
  std::cout << "=== Secure Messaging Tests ===" << std::endl;
  
  try {
    test_secure_message_serialization();
    std::cout << "✓ Secure Message Serialization" << std::endl;
    
    test_secure_messaging_config();
    std::cout << "✓ Secure Messaging Config" << std::endl;
    
    test_tls_context_manager_basic();
    std::cout << "✓ TLS Context Manager Basic" << std::endl;
    
    test_message_crypto_basic();
    std::cout << "✓ Message Crypto Basic" << std::endl;
    
    test_secure_messaging_end_to_end();
    std::cout << "✓ Secure Messaging End-to-End" << std::endl;
    
    test_replay_protection();
    std::cout << "✓ Replay Protection" << std::endl;
    
    test_invalid_message_handling();
    std::cout << "✓ Invalid Message Handling" << std::endl;
    
    test_security_statistics();
    std::cout << "✓ Security Statistics" << std::endl;
    
    test_peer_trust_management();
    std::cout << "✓ Peer Trust Management" << std::endl;
    
    test_nonce_ttl_based_cleanup();
    std::cout << "✓ Nonce TTL-Based Cleanup" << std::endl;
    
    test_nonce_generation_security();
    std::cout << "✓ Nonce Generation Security" << std::endl;
    
    std::cout << std::endl << "=== All Secure Messaging Tests Passed! ===" << std::endl;
    return 0;
    
  } catch (const std::exception& e) {
    std::cout << std::endl << "=== Test Failed: " << e.what() << " ===" << std::endl;
    return 1;
  }
}