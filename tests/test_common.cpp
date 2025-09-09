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

void run_common_tests(TestRunner &runner) {
  std::cout << "\n=== Common Types Tests ===" << std::endl;

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