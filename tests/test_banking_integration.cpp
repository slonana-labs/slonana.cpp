#include "banking/banking_stage.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

namespace slonana {
namespace test {

using namespace slonana::banking;

class BankingIntegrationTester {
public:
  bool run_all_tests() {
    std::cout << "=== Running Banking Stage Integration Tests ===" << std::endl;

    bool all_passed = true;
    all_passed &= test_fee_market_integration();
    all_passed &= test_mev_protection_integration();
    all_passed &= test_combined_integration();
    all_passed &= test_statistics();

    if (all_passed) {
      std::cout << "✅ All Banking Stage integration tests passed!" << std::endl;
    } else {
      std::cout << "❌ Some Banking Stage integration tests failed!" << std::endl;
    }

    return all_passed;
  }

private:
  bool test_fee_market_integration() {
    std::cout << "Testing fee market integration with banking stage..." << std::endl;

    BankingStage banking;
    assert(banking.initialize());
    
    // Test fee market methods
    banking.enable_fee_market(true);
    
    uint64_t base_fee = banking.get_current_base_fee();
    assert(base_fee > 0);
    
    FeeStats stats = banking.get_fee_market_stats();
    // Stats may be empty initially
    
    std::cout << "  Base fee: " << base_fee << " lamports" << std::endl;

    std::cout << "✅ Fee market integration test passed" << std::endl;
    return true;
  }

  bool test_mev_protection_integration() {
    std::cout << "Testing MEV protection integration with banking stage..." << std::endl;

    BankingStage banking;
    assert(banking.initialize());
    
    // Test MEV protection methods
    banking.enable_mev_protection(true);
    banking.set_mev_protection_level(ProtectionLevel::FAIR_ORDERING);
    
    size_t attacks = banking.get_detected_mev_attacks();
    size_t protected_count = banking.get_protected_transactions();
    
    assert(attacks == 0); // Initially zero
    assert(protected_count == 0); // Initially zero
    
    std::cout << "  Detected attacks: " << attacks << std::endl;
    std::cout << "  Protected transactions: " << protected_count << std::endl;

    std::cout << "✅ MEV protection integration test passed" << std::endl;
    return true;
  }

  bool test_combined_integration() {
    std::cout << "Testing combined fee market and MEV protection..." << std::endl;

    BankingStage banking;
    assert(banking.initialize());
    assert(banking.start());
    
    // Enable both features
    banking.enable_fee_market(true);
    banking.enable_mev_protection(true);
    banking.enable_priority_processing(true);
    banking.set_mev_protection_level(ProtectionLevel::FAIR_ORDERING);
    
    // Submit some test transactions
    for (int i = 0; i < 5; ++i) {
      auto tx = std::make_shared<ledger::Transaction>();
      tx->signatures.push_back(std::vector<uint8_t>(64, i));
      tx->message.resize(100, i);
      banking.submit_transaction(tx);
    }
    
    // Allow some processing time
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Check statistics
    uint64_t base_fee = banking.get_current_base_fee();
    size_t protected_count = banking.get_protected_transactions();
    
    std::cout << "  Current base fee: " << base_fee << " lamports" << std::endl;
    std::cout << "  Protected transactions: " << protected_count << std::endl;
    
    banking.stop();

    std::cout << "✅ Combined integration test passed" << std::endl;
    return true;
  }

  bool test_statistics() {
    std::cout << "Testing integrated statistics..." << std::endl;

    BankingStage banking;
    assert(banking.initialize());
    
    // Get statistics
    auto stats = banking.get_statistics();
    
    std::cout << "  Total transactions: " << stats.total_transactions_processed << std::endl;
    std::cout << "  Total batches: " << stats.total_batches_processed << std::endl;
    std::cout << "  Failed transactions: " << stats.failed_transactions << std::endl;

    std::cout << "✅ Statistics test passed" << std::endl;
    return true;
  }
};

} // namespace test
} // namespace slonana

int main() {
  slonana::test::BankingIntegrationTester tester;
  return tester.run_all_tests() ? 0 : 1;
}
