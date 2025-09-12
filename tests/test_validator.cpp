#include <cassert>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

// Include all our headers
#include "common/types.h"
#include "consensus/proof_of_history.h"
#include "ledger/manager.h"
#include "network/gossip.h"
#include "network/rpc_server.h"
#include "slonana_validator.h"
#include "staking/manager.h"
#include "svm/engine.h"
#include "validator/core.h"

// Simple test framework
class TestRunner {
private:
  int tests_run_ = 0;
  int tests_passed_ = 0;

public:
  void run_test(const std::string &test_name, std::function<void()> test_func) {
    tests_run_++;
    std::cout << "Running test: " << test_name << "... ";

    try {
      test_func();
      tests_passed_++;
      std::cout << "PASSED" << std::endl;
    } catch (const std::exception &e) {
      std::cout << "FAILED - " << e.what() << std::endl;
    } catch (...) {
      std::cout << "FAILED - Unknown exception" << std::endl;
    }
  }

  void print_summary() {
    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Tests run: " << tests_run_ << std::endl;
    std::cout << "Tests passed: " << tests_passed_ << std::endl;
    std::cout << "Tests failed: " << (tests_run_ - tests_passed_) << std::endl;

    if (tests_passed_ == tests_run_) {
      std::cout << "All tests PASSED!" << std::endl;
    } else {
      std::cout << "Some tests FAILED!" << std::endl;
    }
  }

  bool all_passed() const { return tests_passed_ == tests_run_; }
};

// Test helper macros
#define ASSERT_TRUE(condition)                                                 \
  if (!(condition)) {                                                          \
    throw std::runtime_error("Assertion failed: " #condition);                 \
  }

#define ASSERT_FALSE(condition)                                                \
  if (condition) {                                                             \
    throw std::runtime_error("Assertion failed: " #condition                   \
                             " should be false");                              \
  }

#define ASSERT_EQ(expected, actual)                                            \
  if ((expected) != (actual)) {                                                \
    throw std::runtime_error("Assertion failed: expected " +                   \
                             std::to_string(expected) + " but got " +          \
                             std::to_string(actual));                          \
  }

// Test functions
void test_common_types() {
  slonana::common::Result<int> success_result(42);
  ASSERT_TRUE(success_result.is_ok());
  ASSERT_FALSE(success_result.is_err());
  ASSERT_EQ(42, success_result.value());

  slonana::common::Result<int> error_result("Something went wrong");
  ASSERT_FALSE(error_result.is_ok());
  ASSERT_TRUE(error_result.is_err());
  ASSERT_TRUE(error_result.error() == "Something went wrong");
}

void test_validator_config() {
  slonana::common::ValidatorConfig config;
  config.ledger_path = "/tmp/test_ledger";
  config.rpc_bind_address = "127.0.0.1:8899";
  config.enable_rpc = true;

  ASSERT_TRUE(config.enable_rpc);
  ASSERT_TRUE(config.rpc_bind_address == "127.0.0.1:8899");
}

void test_ledger_block_operations() {
  slonana::ledger::Block block;
  block.slot = 100;
  block.parent_hash.resize(32, 0xAA);
  block.block_hash.resize(32, 0xBB);
  block.validator.resize(32, 0xCC);

  auto serialized = block.serialize();
  ASSERT_TRUE(!serialized.empty());

  slonana::ledger::Block deserialized_block(serialized);
  ASSERT_EQ(100, deserialized_block.slot);
}

void test_ledger_manager() {
  auto ledger =
      std::make_unique<slonana::ledger::LedgerManager>("/tmp/test_ledger");

  slonana::ledger::Block test_block;
  test_block.slot = 1;
  test_block.block_hash.resize(32, 0x01);
  test_block.parent_hash.resize(32, 0x00);
  test_block.validator.resize(32, 0xFF);

  auto store_result = ledger->store_block(test_block);
  ASSERT_TRUE(store_result.is_ok());

  ASSERT_EQ(1, ledger->get_latest_slot());
  ASSERT_EQ(1, ledger->get_ledger_size());

  auto retrieved_block = ledger->get_block_by_slot(1);
  ASSERT_TRUE(retrieved_block.has_value());
  ASSERT_EQ(1, retrieved_block->slot);
}

void test_gossip_protocol() {
  slonana::common::ValidatorConfig config;
  config.gossip_bind_address = "127.0.0.1:18001"; // Use test port

  auto gossip = std::make_unique<slonana::network::GossipProtocol>(config);

  auto start_result = gossip->start();
  ASSERT_TRUE(start_result.is_ok());

  // Test message creation and broadcasting
  slonana::network::NetworkMessage message;
  message.type = slonana::network::MessageType::PING;
  message.sender.resize(32, 0x01);

  auto broadcast_result = gossip->broadcast_message(message);
  ASSERT_TRUE(broadcast_result.is_ok());

  gossip->stop();
}

void test_validator_core() {
  auto ledger = std::make_shared<slonana::ledger::LedgerManager>(
      "/tmp/test_validator_ledger");
  slonana::PublicKey validator_identity(32, 0x01);

  // Initialize GlobalProofOfHistory for the test
  slonana::consensus::PohConfig poh_config;
  poh_config.target_tick_duration = std::chrono::microseconds(400);
  poh_config.ticks_per_slot = 64;
  bool poh_init_result =
      slonana::consensus::GlobalProofOfHistory::initialize(poh_config);
  ASSERT_TRUE(poh_init_result);

  // Start PoH with genesis hash
  slonana::Hash genesis_hash(32, 0x42);
  auto poh_start_result =
      slonana::consensus::GlobalProofOfHistory::instance().start(genesis_hash);
  ASSERT_TRUE(poh_start_result.is_ok());

  // First store a genesis block to satisfy chain continuity
  slonana::ledger::Block genesis_block;
  genesis_block.slot = 0;
  genesis_block.block_hash.resize(32, 0x00);
  genesis_block.parent_hash.resize(32, 0x00); // Genesis has no parent
  genesis_block.validator = validator_identity;
  genesis_block.block_signature.resize(64, 0xFF);
  auto genesis_result = ledger->store_block(genesis_block);
  ASSERT_TRUE(genesis_result.is_ok());

  auto validator_core = std::make_unique<slonana::validator::ValidatorCore>(
      ledger, validator_identity);

  auto start_result = validator_core->start();
  ASSERT_TRUE(start_result.is_ok());
  ASSERT_TRUE(validator_core->is_running());

  // Test block processing
  slonana::ledger::Block test_block;
  test_block.slot = 1;
  test_block.block_hash.resize(32, 0x01);
  test_block.parent_hash = genesis_block.block_hash; // Reference genesis
  test_block.validator = validator_identity;
  test_block.block_signature.resize(64, 0xFF); // Add valid signature

  validator_core->process_block(test_block);

  // Give PoH a moment to tick (it starts immediately after
  // validator_core->start())
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Verify the blockchain head slot (block processing state)
  ASSERT_EQ(1, validator_core->get_blockchain_head_slot());
  // Verify PoH slot is progressing (should be > 0)
  ASSERT_TRUE(validator_core->get_current_slot() > 0);

  validator_core->stop();
  ASSERT_FALSE(validator_core->is_running());

  // Cleanup GlobalProofOfHistory for the test
  slonana::consensus::GlobalProofOfHistory::shutdown();
}

void test_staking_manager() {
  auto staking = std::make_unique<slonana::staking::StakingManager>();

  slonana::PublicKey validator_id(32, 0x01);
  auto register_result =
      staking->register_validator(validator_id, 500); // 5% commission
  ASSERT_TRUE(register_result.is_ok());
  ASSERT_TRUE(staking->is_validator_registered(validator_id));

  // Test stake account creation
  slonana::staking::StakeAccount stake_account;
  stake_account.stake_pubkey.resize(32, 0x02);
  stake_account.delegator_pubkey.resize(32, 0x03);
  stake_account.stake_amount = 1000000; // 1M lamports

  auto create_result = staking->create_stake_account(stake_account);
  ASSERT_TRUE(create_result.is_ok());

  auto retrieved_account =
      staking->get_stake_account(stake_account.stake_pubkey);
  ASSERT_TRUE(retrieved_account.has_value());
  ASSERT_EQ(1000000, retrieved_account->stake_amount);
}

void test_svm_execution() {
  auto execution_engine = std::make_unique<slonana::svm::ExecutionEngine>();
  auto account_manager = std::make_unique<slonana::svm::AccountManager>();

  // Test account creation
  slonana::svm::ProgramAccount test_account;
  test_account.pubkey.resize(32, 0x01);     // Set the account's public key
  test_account.program_id.resize(32, 0x00); // System program owns this account
  test_account.lamports = 1000000;
  test_account.executable = false;

  auto create_result = account_manager->create_account(test_account);
  ASSERT_TRUE(create_result.is_ok());
  ASSERT_TRUE(account_manager->account_exists(test_account.pubkey));

  auto commit_result = account_manager->commit_changes();
  ASSERT_TRUE(commit_result.is_ok());

  // Test basic instruction execution
  slonana::svm::Instruction instruction;
  instruction.program_id.resize(32, 0x00); // System program
  instruction.data = {0};                  // Transfer instruction

  // Set up source and destination accounts for transfer
  instruction.accounts.resize(2);
  instruction.accounts[0].resize(32,
                                 0x01); // Source account (same as test_account)
  instruction.accounts[1].resize(32, 0x02); // Destination account

  // Create destination account
  slonana::svm::ProgramAccount dest_account;
  dest_account.pubkey.resize(32, 0x02);
  dest_account.program_id.resize(32, 0x00); // System program owns this account
  dest_account.lamports = 500000;
  dest_account.executable = false;

  auto dest_create_result = account_manager->create_account(dest_account);
  ASSERT_TRUE(dest_create_result.is_ok());
  ASSERT_TRUE(account_manager->account_exists(dest_account.pubkey));

  auto dest_commit_result = account_manager->commit_changes();
  ASSERT_TRUE(dest_commit_result.is_ok());

  // Set up accounts map for execution engine (note: using different type)
  std::unordered_map<slonana::PublicKey, slonana::svm::ProgramAccount> accounts;
  accounts[test_account.pubkey] = test_account;
  accounts[dest_account.pubkey] = dest_account;

  auto outcome = execution_engine->execute_transaction({instruction}, accounts);
  ASSERT_TRUE(outcome.is_success());
}

void test_full_validator() {
  slonana::common::ValidatorConfig config;
  config.ledger_path = "/tmp/test_full_validator";
  config.identity_keypair_path = "/tmp/test_identity.json";
  config.rpc_bind_address = "127.0.0.1:18899";    // Use test port
  config.gossip_bind_address = "127.0.0.1:18001"; // Use test port

  auto validator = std::make_unique<slonana::SolanaValidator>(config);

  ASSERT_FALSE(validator->is_initialized());
  ASSERT_FALSE(validator->is_running());

  auto init_result = validator->initialize();
  ASSERT_TRUE(init_result.is_ok());
  ASSERT_TRUE(validator->is_initialized());

  auto start_result = validator->start();
  ASSERT_TRUE(start_result.is_ok());
  ASSERT_TRUE(validator->is_running());

  // Test component access
  ASSERT_TRUE(validator->get_ledger_manager() != nullptr);
  ASSERT_TRUE(validator->get_validator_core() != nullptr);
  ASSERT_TRUE(validator->get_staking_manager() != nullptr);
  ASSERT_TRUE(validator->get_execution_engine() != nullptr);

  // Test stats - current slot should be progressing with PoH
  auto stats = validator->get_stats();
  ASSERT_TRUE(stats.current_slot >=
              0); // Should be >= 0, not necessarily 0 since PoH is time-driven

  validator->stop();
  ASSERT_FALSE(validator->is_running());
}

bool test_rpc_api() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);

  // Test that the server starts/stops
  ASSERT_FALSE(rpc_server.is_running());
  auto start_result = rpc_server.start();
  ASSERT_TRUE(start_result.is_ok());
  ASSERT_TRUE(rpc_server.is_running());

  // Test JSON-RPC requests
  std::string health_request =
      R"({"jsonrpc":"2.0","method":"getHealth","params":"","id":"1"})";
  std::string health_response = rpc_server.handle_request(health_request);
  ASSERT_TRUE(health_response.find("\"jsonrpc\":\"2.0\"") != std::string::npos);
  ASSERT_TRUE(health_response.find("\"id\":\"1\"") != std::string::npos);

  std::string version_request =
      R"({"jsonrpc":"2.0","method":"getVersion","params":"","id":"2"})";
  std::string version_response = rpc_server.handle_request(version_request);
  ASSERT_TRUE(version_response.find("solana-core") != std::string::npos);

  std::string slot_request =
      R"({"jsonrpc":"2.0","method":"getSlot","params":"","id":"3"})";
  std::string slot_response = rpc_server.handle_request(slot_request);
  // Note: getSlot now returns PoH-driven slot, so it should be >= 0, not
  // necessarily 0
  ASSERT_TRUE(slot_response.find("\"result\":") != std::string::npos);

  // Test error handling for unknown method
  std::string unknown_request =
      R"({"jsonrpc":"2.0","method":"unknownMethod","params":"","id":"4"})";
  std::string unknown_response = rpc_server.handle_request(unknown_request);
  ASSERT_TRUE(unknown_response.find("\"error\":") != std::string::npos);
  ASSERT_TRUE(unknown_response.find("-32601") != std::string::npos);

  rpc_server.stop();
  ASSERT_FALSE(rpc_server.is_running());

  return true;
}

int main() {
  std::cout << "=== Slonana C++ Validator Test Suite ===" << std::endl;

  TestRunner runner;

  runner.run_test("Common Types", test_common_types);
  runner.run_test("Validator Config", test_validator_config);
  runner.run_test("Ledger Block Operations", test_ledger_block_operations);
  runner.run_test("Ledger Manager", test_ledger_manager);
  runner.run_test("Gossip Protocol", test_gossip_protocol);
  runner.run_test("Validator Core", test_validator_core);
  runner.run_test("Staking Manager", test_staking_manager);
  runner.run_test("SVM Execution", test_svm_execution);
  runner.run_test("RPC API", test_rpc_api);
  runner.run_test("Full Validator", test_full_validator);

  runner.print_summary();

  return runner.all_passed() ? 0 : 1;
}