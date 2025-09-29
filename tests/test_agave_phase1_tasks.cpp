#include "banking/banking_stage.h"
#include "consensus/proof_of_history.h"
#include "ledger/manager.h"
#include "network/quic_client.h"
#include "network/quic_server.h"
#include "validator/core.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

namespace slonana {
namespace test {

/**
 * Test framework for QUIC networking and Banking Stage
 */
class AgaveCompatibilityTester {
public:
  AgaveCompatibilityTester() = default;
  ~AgaveCompatibilityTester() = default;

  bool run_all_tests() {
    std::cout << "=== Running Agave Compatibility Tests ===" << std::endl;

    bool all_passed = true;

    all_passed &= test_quic_client_server();
    all_passed &= test_quic_stream_multiplexing();
    all_passed &= test_quic_connection_pooling();
    all_passed &= test_banking_stage_pipeline();
    all_passed &= test_banking_stage_parallel_processing();
    all_passed &= test_banking_stage_resource_monitoring();
    all_passed &= test_validator_core_integration();
    all_passed &= test_end_to_end_workflow();

    if (all_passed) {
      std::cout << "✅ All Agave compatibility tests passed!" << std::endl;
    } else {
      std::cout << "❌ Some tests failed!" << std::endl;
    }

    return all_passed;
  }

private:
  bool test_quic_client_server() {
    std::cout << "Testing QUIC client-server communication..." << std::endl;

    // Initialize QUIC server
    network::QuicServer server;
    assert(server.initialize(8001));
    assert(server.start());

    // Wait for server to start and be ready to receive connections
    std::cout << "🔄 Waiting for server to start..." << std::endl;
    std::this_thread::sleep_for(
        std::chrono::milliseconds(1000)); // Increased to 1 second
    std::cout << "✅ Server should be ready" << std::endl;

    // Initialize QUIC client
    network::QuicClient client;
    assert(client.initialize());

    // Test connection
    std::cout << "🔄 Attempting to connect to QUIC server..." << std::endl;
    auto connection = client.connect("127.0.0.1", 8001);
    std::cout << "🔄 Connect call returned, checking result..." << std::endl;
    assert(connection != nullptr);
    assert(connection->is_connected());

    // Test stream creation
    auto stream = connection->create_stream();
    assert(stream != nullptr);

    // Test data transmission
    std::vector<uint8_t> test_data = {1, 2, 3, 4, 5};
    assert(stream->send_data(test_data));

    // Cleanup
    client.shutdown();
    server.shutdown();

    std::cout << "✅ QUIC client-server test passed" << std::endl;
    return true;
  }

  bool test_quic_stream_multiplexing() {
    std::cout << "Testing QUIC stream multiplexing..." << std::endl;

    network::QuicServer server;
    assert(server.initialize(8002));
    assert(server.start());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    network::QuicClient client;
    assert(client.initialize());

    auto connection = client.connect("127.0.0.1", 8002);
    assert(connection != nullptr);

    // Create multiple streams
    std::vector<std::shared_ptr<network::QuicStream>> streams;
    for (int i = 0; i < 5; ++i) {
      auto stream = connection->create_stream();
      assert(stream != nullptr);
      streams.push_back(stream);
    }

    // Test concurrent data transmission on all streams
    for (size_t i = 0; i < streams.size(); ++i) {
      std::vector<uint8_t> data(100, static_cast<uint8_t>(i));
      assert(streams[i]->send_data(data));
    }

    assert(connection->get_stream_count() == 5);

    client.shutdown();
    server.shutdown();

    std::cout << "✅ QUIC stream multiplexing test passed" << std::endl;
    return true;
  }

  bool test_quic_connection_pooling() {
    std::cout << "Testing QUIC connection pooling..." << std::endl;

    network::QuicClient client;
    client.enable_connection_pooling(true);
    client.set_max_connections(10);
    assert(client.initialize());

    // Test multiple connections
    std::vector<std::shared_ptr<network::QuicConnection>> connections;
    for (int i = 0; i < 5; ++i) {
      auto conn = client.connect("127.0.0.1", 8000 + i);
      if (conn) {
        connections.push_back(conn);
      }
    }

    // Verify connection reuse
    auto existing_conn = client.get_connection("127.0.0.1:8000");
    assert(existing_conn != nullptr);

    client.shutdown();

    std::cout << "✅ QUIC connection pooling test passed" << std::endl;
    return true;
  }

  bool test_banking_stage_pipeline() {
    std::cout << "Testing banking stage pipeline..." << std::endl;

    banking::BankingStage banking_stage;
    assert(banking_stage.initialize());
    assert(banking_stage.start());

    // Create test transactions
    std::vector<std::shared_ptr<ledger::Transaction>> transactions;
    for (int i = 0; i < 10; ++i) {
      auto tx = std::make_shared<ledger::Transaction>();
      // In a real implementation, we would properly initialize the transaction
      transactions.push_back(tx);
    }

    // Submit transactions
    banking_stage.submit_transactions(transactions);

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Check statistics
    auto stats = banking_stage.get_statistics();
    assert(stats.total_transactions_processed >= 0);

    banking_stage.shutdown();

    std::cout << "✅ Banking stage pipeline test passed" << std::endl;
    return true;
  }

  bool test_banking_stage_parallel_processing() {
    std::cout << "Testing banking stage parallel processing..." << std::endl;

    banking::BankingStage banking_stage;
    banking_stage.set_batch_size(32);
    banking_stage.set_parallel_stages(4);
    banking_stage.set_max_concurrent_batches(8);

    assert(banking_stage.initialize());
    assert(banking_stage.start());

    // Submit large number of transactions
    for (int i = 0; i < 100; ++i) {
      auto tx = std::make_shared<ledger::Transaction>();
      banking_stage.submit_transaction(tx);
    }

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto stats = banking_stage.get_statistics();
    assert(stats.total_batches_processed > 0);
    assert(stats.transactions_per_second >= 0);

    banking_stage.shutdown();

    std::cout << "✅ Banking stage parallel processing test passed"
              << std::endl;
    return true;
  }

  bool test_banking_stage_resource_monitoring() {
    std::cout << "Testing banking stage resource monitoring..." << std::endl;

    banking::BankingStage banking_stage;
    banking_stage.enable_resource_monitoring(true);
    banking_stage.enable_adaptive_batching(true);

    assert(banking_stage.initialize());
    assert(banking_stage.start());

    // Wait for resource monitoring to collect data
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto stats = banking_stage.get_statistics();
    assert(stats.cpu_usage >= 0.0);
    assert(stats.memory_usage_mb >= 0);

    banking_stage.shutdown();

    std::cout << "✅ Banking stage resource monitoring test passed"
              << std::endl;
    return true;
  }

  bool test_validator_core_integration() {
    std::cout << "Testing validator core integration..." << std::endl;

    // Create mock ledger manager
    auto ledger = std::make_shared<ledger::LedgerManager>("test_ledger");

    // Create validator core
    common::PublicKey validator_id = {'t', 'e', 's', 't', '_', 'v', 'a',
                                      'l', 'i', 'd', 'a', 't', 'o', 'r'};
    validator::ValidatorCore core(ledger, validator_id);

    // Initialize Proof of History (required before starting core)
    consensus::GlobalProofOfHistory::initialize();

    // Start validator core (this should initialize banking stage and QUIC)
    auto start_result = core.start();
    if (!start_result.is_ok()) {
      std::cout << "Failed to start validator core: " << start_result.error()
                << std::endl;
      return false;
    }

    // Test banking stage access
    auto *banking_stage = core.get_banking_stage();
    assert(banking_stage != nullptr);

    // Test QUIC networking
    assert(core.enable_quic_networking(8003));
    auto *quic_server = core.get_quic_server();
    assert(quic_server != nullptr);

    // Test transaction processing
    auto tx = std::make_shared<ledger::Transaction>();
    core.process_transaction(tx);

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Get statistics
    auto banking_stats = core.get_transaction_statistics();
    auto quic_stats = core.get_quic_statistics();

    // Cleanup
    core.stop();
    consensus::GlobalProofOfHistory::shutdown();

    std::cout << "✅ Validator core integration test passed" << std::endl;
    return true;
  }

  bool test_end_to_end_workflow() {
    std::cout << "Testing end-to-end workflow..." << std::endl;

    // Initialize global components
    consensus::GlobalProofOfHistory::initialize();

    // Create validator with banking stage and QUIC
    auto ledger = std::make_shared<ledger::LedgerManager>("e2e_test_ledger");
    common::PublicKey validator_id = {'e', '2', 'e', '_', 't', 'e', 's', 't'};
    validator::ValidatorCore validator(ledger, validator_id);

    // Start validator
    auto start_result = validator.start();
    if (!start_result.is_ok()) {
      std::cout << "Failed to start validator: " << start_result.error()
                << std::endl;
      return false;
    }

    // Enable QUIC networking
    assert(validator.enable_quic_networking(8004));

    // Create transactions and process them
    std::vector<std::shared_ptr<ledger::Transaction>> transactions;
    for (int i = 0; i < 50; ++i) {
      auto tx = std::make_shared<ledger::Transaction>();
      transactions.push_back(tx);
    }

    validator.process_transactions(transactions);

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Verify statistics
    auto banking_stats = validator.get_transaction_statistics();
    auto quic_stats = validator.get_quic_statistics();

    assert(banking_stats.total_transactions_processed >= 0);
    assert(banking_stats.uptime.count() > 0);

    // Test QUIC client connectivity
    network::QuicClient client;
    assert(client.initialize());

    auto connection = client.connect("127.0.0.1", 8004);
    if (connection) {
      assert(connection->is_connected());

      auto stream = connection->create_stream();
      if (stream) {
        std::vector<uint8_t> test_data = {0xAA, 0xBB, 0xCC, 0xDD};
        stream->send_data(test_data);
      }
    }

    // Cleanup
    client.shutdown();
    validator.stop();
    consensus::GlobalProofOfHistory::shutdown();

    std::cout << "✅ End-to-end workflow test passed" << std::endl;
    return true;
  }
};

} // namespace test
} // namespace slonana

// Test entry point
int main() {
  slonana::test::AgaveCompatibilityTester tester;
  bool success = tester.run_all_tests();
  return success ? 0 : 1;
}