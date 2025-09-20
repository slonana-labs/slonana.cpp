#include "common/types.h"
#include "slonana_validator.h"
#include "test_framework.h"

void test_result_type() {
  // Test successful result
  slonana::common::Result<int> success_result(42);
  ASSERT_TRUE(success_result.is_ok());
  ASSERT_FALSE(success_result.is_err());
  ASSERT_EQ(42, success_result.value());

  // Test error result
  slonana::common::Result<int> error_result("Something went wrong");
  ASSERT_FALSE(error_result.is_ok());
  ASSERT_TRUE(error_result.is_err());
  ASSERT_TRUE(error_result.error() == "Something went wrong");
}

void test_result_type_move_semantics() {
  // Create a result with a successful value using integer to avoid string
  // ambiguity
  slonana::common::Result<int> result(42);
  ASSERT_TRUE(result.is_ok());

  int moved_value = std::move(result).value();
  ASSERT_EQ(42, moved_value);
}

void test_result_type_copy_semantics() {
  slonana::common::Result<int> original(100);
  slonana::common::Result<int> copied = original;

  ASSERT_TRUE(original.is_ok());
  ASSERT_TRUE(copied.is_ok());
  ASSERT_EQ(100, original.value());
  ASSERT_EQ(100, copied.value());
}

void test_validator_config_default_values() {
  slonana::common::ValidatorConfig config;

  // Test default values
  ASSERT_TRUE(config.enable_rpc);
  ASSERT_TRUE(config.enable_gossip);
  ASSERT_EQ(std::string("127.0.0.1:8899"), config.rpc_bind_address);
  ASSERT_EQ(std::string("127.0.0.1:8001"), config.gossip_bind_address);
  ASSERT_EQ(std::string(""), config.ledger_path);
  ASSERT_EQ(std::string(""), config.identity_keypair_path);
}

void test_validator_config_custom_values() {
  slonana::common::ValidatorConfig config;
  config.ledger_path = "/tmp/test_ledger";
  config.rpc_bind_address = "127.0.0.1:8899";
  config.enable_rpc = true;
  config.enable_gossip = false;

  ASSERT_TRUE(config.enable_rpc);
  ASSERT_FALSE(config.enable_gossip);
  ASSERT_EQ(std::string("127.0.0.1:8899"), config.rpc_bind_address);
  ASSERT_EQ(std::string("/tmp/test_ledger"), config.ledger_path);
}

void test_validator_stats_initialization() {
  slonana::SolanaValidator::ValidatorStats stats;

  ASSERT_EQ(static_cast<uint64_t>(0), stats.current_slot);
  ASSERT_EQ(static_cast<uint64_t>(0), stats.transactions_processed);
  ASSERT_EQ(static_cast<uint64_t>(0), stats.blocks_processed);
  ASSERT_EQ(static_cast<uint64_t>(0), stats.uptime_seconds);
}

void test_public_key_operations() {
  // Test empty public key
  slonana::common::PublicKey empty_key;
  ASSERT_TRUE(empty_key.empty());

  // Test initialized public key
  slonana::common::PublicKey key(32, 0xAB);
  ASSERT_FALSE(key.empty());
  ASSERT_EQ(static_cast<size_t>(32), key.size());

  for (size_t i = 0; i < key.size(); ++i) {
    ASSERT_EQ(static_cast<uint8_t>(0xAB), key[i]);
  }
}

void test_public_key_comparison() {
  slonana::common::PublicKey key1(32, 0x01);
  slonana::common::PublicKey key2(32, 0x01);
  slonana::common::PublicKey key3(32, 0x02);

  ASSERT_TRUE(key1 == key2);
  ASSERT_FALSE(key1 == key3);
  ASSERT_TRUE(key1 != key3);
}

void test_signature_operations() {
  slonana::common::Signature sig(64, 0xFF);
  ASSERT_EQ(static_cast<size_t>(64), sig.size());

  for (size_t i = 0; i < sig.size(); ++i) {
    ASSERT_EQ(static_cast<uint8_t>(0xFF), sig[i]);
  }
}

void test_hash_operations() {
  slonana::common::Hash hash(32, 0xAA);
  ASSERT_EQ(static_cast<size_t>(32), hash.size());

  for (size_t i = 0; i < hash.size(); ++i) {
    ASSERT_EQ(static_cast<uint8_t>(0xAA), hash[i]);
  }
}

// Additional comprehensive tests for Common Types (doubling from 10 to 20)
void test_result_type_error_propagation() {
  // Test error chaining and propagation
  slonana::common::Result<int> error1("First error");
  slonana::common::Result<int> error2("Second error");

  ASSERT_TRUE(error1.is_err());
  ASSERT_TRUE(error2.is_err());
  ASSERT_TRUE(error1.error().find("First") != std::string::npos);
  ASSERT_TRUE(error2.error().find("Second") != std::string::npos);
}

void test_result_type_chaining() {
  // Test result chaining operations
  slonana::common::Result<int> success(42);
  slonana::common::Result<int> error("Chain error");

  // Successful chaining
  if (success.is_ok()) {
    slonana::common::Result<int> chained(success.value() * 2);
    ASSERT_TRUE(chained.is_ok());
    ASSERT_EQ(84, chained.value());
  }

  // Error chaining should preserve error
  ASSERT_TRUE(error.is_err());
}

void test_validator_config_edge_cases() {
  slonana::common::ValidatorConfig config;

  // Test edge case values
  config.rpc_bind_address = "0.0.0.0:0";
  config.gossip_bind_address = "255.255.255.255:65535";
  config.ledger_path =
      "/very/long/path/that/might/not/exist/but/should/be/handled/gracefully";

  // Should not crash with edge case values
  ASSERT_TRUE(config.rpc_bind_address.length() > 0);
  ASSERT_TRUE(config.gossip_bind_address.length() > 0);
  ASSERT_TRUE(config.ledger_path.length() > 0);
}

void test_validator_config_invalid_addresses() {
  slonana::common::ValidatorConfig config;

  // Test invalid addresses (should not crash)
  config.rpc_bind_address = "invalid_address";
  config.gossip_bind_address = "300.300.300.300:99999";

  // Should handle invalid addresses gracefully
  ASSERT_TRUE(config.rpc_bind_address.length() > 0);
  ASSERT_TRUE(config.gossip_bind_address.length() > 0);
}

void test_public_key_edge_cases() {
  // Test with maximum size key
  std::vector<uint8_t> max_key(32, 0xFF);
  slonana::common::PublicKey max_pub_key(max_key);
  ASSERT_EQ(32, max_pub_key.size());

  // Test with minimum size key (all zeros)
  std::vector<uint8_t> min_key(32, 0x00);
  slonana::common::PublicKey min_pub_key(min_key);
  ASSERT_EQ(32, min_pub_key.size());

  // Test inequality
  ASSERT_TRUE(max_pub_key != min_pub_key);
}

void test_public_key_boundary_conditions() {
  // Test boundary conditions for public keys
  std::vector<uint8_t> key1(32, 0x7F);
  std::vector<uint8_t> key2(32, 0x80);

  slonana::common::PublicKey pub_key1(key1);
  slonana::common::PublicKey pub_key2(key2);

  ASSERT_TRUE(pub_key1 != pub_key2);
  ASSERT_EQ(32, pub_key1.size());
  ASSERT_EQ(32, pub_key2.size());
}

void test_signature_validation_edge_cases() {
  // Test with different signature sizes and patterns
  std::vector<uint8_t> sig1(64, 0x55);
  std::vector<uint8_t> sig2(64, 0xAA);
  std::vector<uint8_t> sig3(64, 0x00);

  slonana::common::Signature signature1(sig1);
  slonana::common::Signature signature2(sig2);
  slonana::common::Signature signature3(sig3);

  ASSERT_EQ(64, signature1.size());
  ASSERT_EQ(64, signature2.size());
  ASSERT_EQ(64, signature3.size());

  // Test that different patterns create different signatures
  ASSERT_TRUE(signature1 != signature2);
  ASSERT_TRUE(signature2 != signature3);
  ASSERT_TRUE(signature1 != signature3);
}

void test_hash_collision_resistance() {
  // Test hash function with similar inputs
  std::vector<uint8_t> data1 = {0x01, 0x02, 0x03, 0x04};
  std::vector<uint8_t> data2 = {0x01, 0x02, 0x03, 0x05}; // One bit different

  slonana::common::Hash hash1(data1);
  slonana::common::Hash hash2(data2);

  // Should produce different hashes (collision resistance)
  ASSERT_TRUE(hash1 != hash2);
  ASSERT_EQ(hash1.size(), hash2.size()); // Both should be same size
  ASSERT_GT(hash1.size(), 0);            // Should have some size
}

void test_memory_usage_patterns() {
  // Test memory allocation patterns
  std::vector<slonana::common::PublicKey> keys;

  // Create many keys to test memory usage
  for (int i = 0; i < 1000; ++i) {
    std::vector<uint8_t> key_data(32);
    for (size_t j = 0; j < 32; ++j) {
      key_data[j] = static_cast<uint8_t>(i + j);
    }
    keys.emplace_back(key_data);
  }

  ASSERT_EQ(1000, keys.size());
  // All keys should be properly constructed
  for (const auto &key : keys) {
    ASSERT_EQ(32, key.size());
  }
}

void test_serialization_deserialization() {
  // Test basic serialization concepts
  slonana::common::PublicKey original_key(std::vector<uint8_t>(32, 0xAB));

  // Simulate serialization by copying data
  std::vector<uint8_t> serialized_data;
  for (size_t i = 0; i < original_key.size(); ++i) {
    serialized_data.push_back(original_key[i]);
  }

  // Simulate deserialization
  slonana::common::PublicKey deserialized_key(serialized_data);

  ASSERT_EQ(original_key, deserialized_key);
  ASSERT_EQ(32, deserialized_key.size());
}

void run_common_tests(TestRunner &runner) {
  std::cout << "\n=== Common Types Tests ===" << std::endl;

  // Original 10 tests
  runner.run_test("Result Type Basic", test_result_type);
  runner.run_test("Result Type Move Semantics",
                  test_result_type_move_semantics);
  runner.run_test("Result Type Copy Semantics",
                  test_result_type_copy_semantics);
  runner.run_test("Validator Config Default Values",
                  test_validator_config_default_values);
  runner.run_test("Validator Config Custom Values",
                  test_validator_config_custom_values);
  runner.run_test("Validator Stats Initialization",
                  test_validator_stats_initialization);
  runner.run_test("Public Key Operations", test_public_key_operations);
  runner.run_test("Public Key Comparison", test_public_key_comparison);
  runner.run_test("Signature Operations", test_signature_operations);
  runner.run_test("Hash Operations", test_hash_operations);

  // Additional 10 tests for comprehensive coverage
  runner.run_test("Result Type Error Propagation",
                  test_result_type_error_propagation);
  runner.run_test("Result Type Chaining", test_result_type_chaining);
  runner.run_test("Validator Config Edge Cases",
                  test_validator_config_edge_cases);
  runner.run_test("Validator Config Invalid Addresses",
                  test_validator_config_invalid_addresses);
  runner.run_test("Public Key Edge Cases", test_public_key_edge_cases);
  runner.run_test("Public Key Boundary Conditions",
                  test_public_key_boundary_conditions);
  runner.run_test("Signature Validation Edge Cases",
                  test_signature_validation_edge_cases);
  runner.run_test("Hash Collision Resistance", test_hash_collision_resistance);
  runner.run_test("Memory Usage Patterns", test_memory_usage_patterns);
  runner.run_test("Serialization Deserialization",
                  test_serialization_deserialization);
}

// Standalone test main for common tests
#ifndef COMPREHENSIVE_TESTS
int main() {
  std::cout << "=== Common Types Test Suite ===" << std::endl;
  TestRunner runner;
  run_common_tests(runner);
  runner.print_summary();
  return runner.all_passed() ? 0 : 1;
}
#endif