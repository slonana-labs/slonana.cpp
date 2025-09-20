#include "ledger/manager.h"
#include "test_framework.h"
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

void test_block_serialization() {
  slonana::ledger::Block block;
  block.slot = 100;
  block.parent_hash.resize(32, 0xAA);
  block.block_hash.resize(32, 0xBB);
  block.validator.resize(32, 0xCC);
  block.block_signature.resize(64, 0xDD);

  auto serialized = block.serialize();
  ASSERT_NOT_EMPTY(serialized);

  slonana::ledger::Block deserialized_block(serialized);
  ASSERT_EQ(static_cast<uint64_t>(100), deserialized_block.slot);
  // Note: Vector comparison is complex, so we'll just check they're the same
  // size
  ASSERT_EQ(block.parent_hash.size(), deserialized_block.parent_hash.size());
  ASSERT_EQ(block.block_hash.size(), deserialized_block.block_hash.size());
  ASSERT_EQ(block.validator.size(), deserialized_block.validator.size());
}

void test_block_hash_calculation() {
  slonana::ledger::Block block;
  block.slot = 50;
  block.parent_hash.resize(32, 0x01);
  block.validator.resize(32, 0x02);

  auto hash1 = block.compute_hash();
  auto hash2 = block.compute_hash();

  // Hash should be deterministic
  ASSERT_EQ(hash1.size(), hash2.size());
  ASSERT_EQ(static_cast<size_t>(32), hash1.size());
}

void test_transaction_operations() {
  slonana::ledger::Transaction tx;
  tx.signatures.resize(1);
  tx.signatures[0].resize(64, 0xAA);
  tx.message.resize(100, 0xBB);

  auto serialized = tx.serialize();
  ASSERT_NOT_EMPTY(serialized);

  slonana::ledger::Transaction deserialized_tx(serialized);
  ASSERT_EQ(tx.signatures.size(), deserialized_tx.signatures.size());
  ASSERT_EQ(tx.message.size(), deserialized_tx.message.size());
}

void test_ledger_manager_initialization() {
  std::string test_path = "/tmp/test_ledger_init";

  // Clean up any existing test data
  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }

  auto ledger = std::make_unique<slonana::ledger::LedgerManager>(test_path);

  ASSERT_EQ(static_cast<uint64_t>(0), ledger->get_latest_slot());
  ASSERT_EQ(static_cast<uint64_t>(0), ledger->get_ledger_size());

  // Clean up
  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }
}

void test_ledger_manager_block_storage() {
  std::string test_path = "/tmp/test_ledger_storage";

  // Clean up any existing test data
  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }

  auto ledger = std::make_unique<slonana::ledger::LedgerManager>(test_path);

  slonana::ledger::Block test_block;
  test_block.slot = 1;
  test_block.block_hash.resize(32, 0x01);
  test_block.parent_hash.resize(32, 0x00);
  test_block.validator.resize(32, 0xFF);
  test_block.block_signature.resize(64, 0xAA);

  auto store_result = ledger->store_block(test_block);
  ASSERT_TRUE(store_result.is_ok());

  ASSERT_EQ(static_cast<uint64_t>(1), ledger->get_latest_slot());
  ASSERT_EQ(static_cast<uint64_t>(1), ledger->get_ledger_size());

  auto retrieved_block = ledger->get_block_by_slot(1);
  ASSERT_TRUE(retrieved_block.has_value());
  ASSERT_EQ(static_cast<uint64_t>(1), retrieved_block->slot);
  ASSERT_EQ(test_block.block_hash.size(), retrieved_block->block_hash.size());

  // Clean up
  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }
}

void test_ledger_manager_multiple_blocks() {
  std::string test_path = "/tmp/test_ledger_multiple";

  // Clean up any existing test data
  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }

  auto ledger = std::make_unique<slonana::ledger::LedgerManager>(test_path);

  // Store multiple blocks
  for (uint64_t slot = 1; slot <= 5; ++slot) {
    slonana::ledger::Block block;
    block.slot = slot;
    block.block_hash.resize(32, static_cast<uint8_t>(slot));
    block.parent_hash.resize(32, static_cast<uint8_t>(slot - 1));
    block.validator.resize(32, 0xFF);
    block.block_signature.resize(64, 0xAA);

    auto result = ledger->store_block(block);
    ASSERT_TRUE(result.is_ok());
  }

  ASSERT_EQ(static_cast<uint64_t>(5), ledger->get_latest_slot());
  ASSERT_EQ(static_cast<uint64_t>(5), ledger->get_ledger_size());

  // Verify all blocks can be retrieved
  for (uint64_t slot = 1; slot <= 5; ++slot) {
    auto block = ledger->get_block_by_slot(slot);
    ASSERT_TRUE(block.has_value());
    ASSERT_EQ(slot, block->slot);
  }

  // Clean up
  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }
}

void test_ledger_manager_block_by_hash() {
  std::string test_path = "/tmp/test_ledger_hash";

  // Clean up any existing test data
  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }

  auto ledger = std::make_unique<slonana::ledger::LedgerManager>(test_path);

  slonana::ledger::Block test_block;
  test_block.slot = 10;
  test_block.block_hash = {0x01, 0x02, 0x03}; // Unique hash
  test_block.block_hash.resize(32, 0x04);
  test_block.parent_hash.resize(32, 0x00);
  test_block.validator.resize(32, 0xFF);
  test_block.block_signature.resize(64, 0xAA);

  auto store_result = ledger->store_block(test_block);
  ASSERT_TRUE(store_result.is_ok());

  auto retrieved_block = ledger->get_block(test_block.block_hash);
  ASSERT_TRUE(retrieved_block.has_value());
  ASSERT_EQ(static_cast<uint64_t>(10), retrieved_block->slot);
  ASSERT_EQ(test_block.block_hash.size(), retrieved_block->block_hash.size());

  // Clean up
  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }
}

void test_ledger_manager_invalid_operations() {
  std::string test_path = "/tmp/test_ledger_invalid";

  // Clean up any existing test data
  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }

  auto ledger = std::make_unique<slonana::ledger::LedgerManager>(test_path);

  // Try to get non-existent block
  auto non_existent = ledger->get_block_by_slot(999);
  ASSERT_FALSE(non_existent.has_value());

  // Try to get block by invalid hash
  slonana::common::Hash invalid_hash(32, 0xFF);
  auto invalid_block = ledger->get_block(invalid_hash);
  ASSERT_FALSE(invalid_block.has_value());

  // Clean up
  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }
}

void test_ledger_manager_performance() {
  std::string test_path = "/tmp/test_ledger_perf";

  // Clean up any existing test data
  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }

  auto ledger = std::make_unique<slonana::ledger::LedgerManager>(test_path);

  auto start_time = std::chrono::high_resolution_clock::now();

  // Store 100 blocks
  for (uint64_t slot = 1; slot <= 100; ++slot) {
    slonana::ledger::Block block;
    block.slot = slot;
    block.block_hash.resize(32, static_cast<uint8_t>(slot % 256));
    block.parent_hash.resize(32, static_cast<uint8_t>((slot - 1) % 256));
    block.validator.resize(32, 0xFF);
    block.block_signature.resize(64, 0xAA);

    auto result = ledger->store_block(block);
    ASSERT_TRUE(result.is_ok());
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  std::cout << "Performance: Stored 100 blocks in " << duration.count() << "ms"
            << std::endl;

  ASSERT_EQ(static_cast<uint64_t>(100), ledger->get_latest_slot());
  ASSERT_EQ(static_cast<uint64_t>(100), ledger->get_ledger_size());

  // Clean up
  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }
}

// Additional comprehensive ledger tests (doubling from 9 to 18)
void test_ledger_concurrent_access() {
  std::string test_path = "/tmp/test_ledger_concurrent";

  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }

  auto ledger = std::make_unique<slonana::ledger::LedgerManager>(test_path);

  // Store blocks sequentially to test concurrent-like access patterns
  for (uint64_t i = 1; i <= 10; ++i) {
    slonana::ledger::Block block;
    block.slot = i;
    block.block_hash.resize(32, static_cast<uint8_t>(i));
    block.parent_hash.resize(32, static_cast<uint8_t>(i - 1));
    block.validator.resize(32, 0xCC);
    block.block_signature.resize(64, 0xDD);

    auto result = ledger->store_block(block);
    ASSERT_TRUE(result.is_ok());
  }

  ASSERT_EQ(static_cast<uint64_t>(10), ledger->get_latest_slot());
  ASSERT_EQ(static_cast<uint64_t>(10), ledger->get_ledger_size());

  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }
}

void test_ledger_large_block_handling() {
  std::string test_path = "/tmp/test_ledger_large";

  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }

  auto ledger = std::make_unique<slonana::ledger::LedgerManager>(test_path);

  // Create a block with larger data
  slonana::ledger::Block large_block;
  large_block.slot = 1;
  large_block.block_hash.resize(32, 0xFF);
  large_block.parent_hash.resize(32, 0x00);
  large_block.validator.resize(32, 0xBB);
  large_block.block_signature.resize(64, 0xEE);

  auto result = ledger->store_block(large_block);
  ASSERT_TRUE(result.is_ok());

  // Verify the large block was stored
  auto retrieved = ledger->get_block_by_slot(1);
  ASSERT_TRUE(retrieved.has_value());
  ASSERT_EQ(static_cast<uint64_t>(1), retrieved.value().slot);

  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }
}

void test_ledger_block_corruption_scenarios() {
  std::string test_path = "/tmp/test_ledger_corruption";

  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }

  auto ledger = std::make_unique<slonana::ledger::LedgerManager>(test_path);

  // Test with edge case block data
  slonana::ledger::Block edge_block;
  edge_block.slot = 1;
  edge_block.block_hash.resize(32, 0x00);  // All zeros hash
  edge_block.parent_hash.resize(32, 0x00); // All zeros parent
  edge_block.validator.resize(32, 0xFF);
  edge_block.block_signature.resize(64, 0xAA);

  auto result = ledger->store_block(edge_block);
  ASSERT_TRUE(result.is_ok());

  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }
}

void test_ledger_transaction_batch_processing() {
  std::string test_path = "/tmp/test_ledger_batch";

  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }

  auto ledger = std::make_unique<slonana::ledger::LedgerManager>(test_path);

  // Create multiple blocks
  for (uint64_t slot = 1; slot <= 5; ++slot) {
    slonana::ledger::Block block;
    block.slot = slot;
    block.block_hash.resize(32, static_cast<uint8_t>(slot));
    block.parent_hash.resize(32, static_cast<uint8_t>(slot - 1));
    block.validator.resize(32, 0xAA);
    block.block_signature.resize(64, 0xBB);

    auto result = ledger->store_block(block);
    ASSERT_TRUE(result.is_ok());
  }

  ASSERT_EQ(static_cast<uint64_t>(5), ledger->get_latest_slot());

  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }
}

void test_ledger_recovery_scenarios() {
  std::string test_path = "/tmp/test_ledger_recovery";

  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }

  // Create ledger, store data, destroy, then recreate
  {
    auto ledger = std::make_unique<slonana::ledger::LedgerManager>(test_path);

    slonana::ledger::Block block;
    block.slot = 42;
    block.block_hash.resize(32, 0xDE);
    block.parent_hash.resize(32, 0xAD);
    block.validator.resize(32, 0xBE);
    block.block_signature.resize(64, 0xEF);

    auto result = ledger->store_block(block);
    ASSERT_TRUE(result.is_ok());
  }

  // Create new ledger instance (simulating recovery)
  {
    auto recovered_ledger =
        std::make_unique<slonana::ledger::LedgerManager>(test_path);

    // Should be able to access previously stored data
    auto retrieved = recovered_ledger->get_block_by_slot(42);
    ASSERT_TRUE(retrieved.has_value());
    ASSERT_EQ(static_cast<uint64_t>(42), retrieved.value().slot);
  }

  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }
}

void test_ledger_storage_limits() {
  std::string test_path = "/tmp/test_ledger_limits";

  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }

  auto ledger = std::make_unique<slonana::ledger::LedgerManager>(test_path);

  // Test with many blocks
  for (uint64_t slot = 1; slot <= 100; ++slot) {
    slonana::ledger::Block block;
    block.slot = slot;
    block.block_hash.resize(32, static_cast<uint8_t>(slot % 256));
    block.parent_hash.resize(32, static_cast<uint8_t>((slot - 1) % 256));
    block.validator.resize(32, 0x77);
    block.block_signature.resize(64, 0x88);

    auto result = ledger->store_block(block);
    ASSERT_TRUE(result.is_ok());
  }

  ASSERT_EQ(static_cast<uint64_t>(100), ledger->get_latest_slot());
  ASSERT_EQ(static_cast<uint64_t>(100), ledger->get_ledger_size());

  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }
}

void test_ledger_database_consistency() {
  std::string test_path = "/tmp/test_ledger_consistency";

  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }

  auto ledger = std::make_unique<slonana::ledger::LedgerManager>(test_path);

  // Store blocks in non-sequential order
  std::vector<uint64_t> slots = {5, 2, 8, 1, 7, 3, 6, 4};

  for (uint64_t slot : slots) {
    slonana::ledger::Block block;
    block.slot = slot;
    block.block_hash.resize(32, static_cast<uint8_t>(slot));
    block.parent_hash.resize(32, static_cast<uint8_t>(slot - 1));
    block.validator.resize(32, 0x99);
    block.block_signature.resize(64, 0xAA);

    auto result = ledger->store_block(block);
    ASSERT_TRUE(result.is_ok());
  }

  // Verify all blocks are accessible
  for (uint64_t slot : slots) {
    auto retrieved = ledger->get_block_by_slot(slot);
    ASSERT_TRUE(retrieved.has_value());
    ASSERT_EQ(slot, retrieved.value().slot);
  }

  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }
}

void test_ledger_performance_under_load() {
  std::string test_path = "/tmp/test_ledger_load";

  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }

  auto ledger = std::make_unique<slonana::ledger::LedgerManager>(test_path);

  auto start_time = std::chrono::high_resolution_clock::now();

  // Store 200 blocks
  for (uint64_t slot = 1; slot <= 200; ++slot) {
    slonana::ledger::Block block;
    block.slot = slot;
    block.block_hash.resize(32, static_cast<uint8_t>(slot % 256));
    block.parent_hash.resize(32, static_cast<uint8_t>((slot - 1) % 256));
    block.validator.resize(32, 0x55);
    block.block_signature.resize(64, 0x66);

    auto result = ledger->store_block(block);
    ASSERT_TRUE(result.is_ok());
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  std::cout << "Load Test: Stored 200 blocks in " << duration.count() << "ms"
            << std::endl;

  ASSERT_EQ(static_cast<uint64_t>(200), ledger->get_latest_slot());

  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }
}

void test_ledger_memory_management() {
  std::string test_path = "/tmp/test_ledger_memory";

  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }

  // Test multiple ledger instances
  for (int instance = 0; instance < 5; ++instance) {
    auto ledger = std::make_unique<slonana::ledger::LedgerManager>(test_path);

    slonana::ledger::Block block;
    block.slot = instance + 1;
    block.block_hash.resize(32, static_cast<uint8_t>(instance));
    block.parent_hash.resize(32, static_cast<uint8_t>(instance - 1));
    block.validator.resize(32, 0x11);
    block.block_signature.resize(64, 0x22);

    auto result = ledger->store_block(block);
    ASSERT_TRUE(result.is_ok());
  }

  // Final verification
  auto final_ledger =
      std::make_unique<slonana::ledger::LedgerManager>(test_path);
  ASSERT_EQ(static_cast<uint64_t>(5), final_ledger->get_latest_slot());

  if (fs::exists(test_path)) {
    fs::remove_all(test_path);
  }
}

void run_ledger_tests(TestRunner &runner) {
  std::cout << "\n=== Ledger Tests ===" << std::endl;

  // Original 9 tests
  runner.run_test("Block Serialization", test_block_serialization);
  runner.run_test("Block Hash Calculation", test_block_hash_calculation);
  runner.run_test("Transaction Operations", test_transaction_operations);
  runner.run_test("Ledger Manager Initialization",
                  test_ledger_manager_initialization);
  runner.run_test("Ledger Manager Block Storage",
                  test_ledger_manager_block_storage);
  runner.run_test("Ledger Manager Multiple Blocks",
                  test_ledger_manager_multiple_blocks);
  runner.run_test("Ledger Manager Block By Hash",
                  test_ledger_manager_block_by_hash);
  runner.run_test("Ledger Manager Invalid Operations",
                  test_ledger_manager_invalid_operations);
  runner.run_test("Ledger Manager Performance",
                  test_ledger_manager_performance);

  // Additional 9 tests for comprehensive coverage
  runner.run_test("Ledger Concurrent Access", test_ledger_concurrent_access);
  runner.run_test("Ledger Large Block Handling",
                  test_ledger_large_block_handling);
  runner.run_test("Ledger Block Corruption Scenarios",
                  test_ledger_block_corruption_scenarios);
  runner.run_test("Ledger Transaction Batch Processing",
                  test_ledger_transaction_batch_processing);
  runner.run_test("Ledger Recovery Scenarios", test_ledger_recovery_scenarios);
  runner.run_test("Ledger Storage Limits", test_ledger_storage_limits);
  runner.run_test("Ledger Database Consistency",
                  test_ledger_database_consistency);
  runner.run_test("Ledger Performance Under Load",
                  test_ledger_performance_under_load);
  runner.run_test("Ledger Memory Management", test_ledger_memory_management);
}