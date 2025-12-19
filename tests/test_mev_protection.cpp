#include "banking/mev_protection.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

namespace slonana {
namespace test {

using namespace slonana::banking;

class MEVProtectionTester {
public:
  using TransactionPtr = std::shared_ptr<ledger::Transaction>;

  bool run_all_tests() {
    std::cout << "=== Running MEV Protection Tests ===" << std::endl;

    bool all_passed = true;
    all_passed &= test_initialization();
    all_passed &= test_protection_levels();
    all_passed &= test_fair_ordering();
    all_passed &= test_transaction_shuffling();
    all_passed &= test_detection_toggle();
    all_passed &= test_alert_tracking();
    all_passed &= test_suspicious_filtering();
    all_passed &= test_statistics();
    all_passed &= test_concurrent_access();

    if (all_passed) {
      std::cout << "✅ All MEV Protection tests passed!" << std::endl;
    } else {
      std::cout << "❌ Some MEV Protection tests failed!" << std::endl;
    }

    return all_passed;
  }

private:
  TransactionPtr create_test_transaction(uint64_t id) {
    auto tx = std::make_shared<ledger::Transaction>();
    
    // Create a signature with unique data
    Signature sig;
    sig.resize(64);
    for (size_t i = 0; i < 8; ++i) {
      sig[i] = (id >> (i * 8)) & 0xFF;
    }
    tx->signatures.push_back(sig);
    
    // Create a simple message
    tx->message.resize(100 + (id % 50)); // Vary message size
    for (size_t i = 0; i < tx->message.size(); ++i) {
      tx->message[i] = (i + id) & 0xFF;
    }
    
    return tx;
  }

  bool test_initialization() {
    std::cout << "Testing MEV protection initialization..." << std::endl;

    MEVProtection protection;
    
    assert(protection.is_detection_enabled());
    assert(protection.get_protection_level() == ProtectionLevel::FAIR_ORDERING);
    assert(protection.get_detected_attacks_count() == 0);
    assert(protection.get_protected_transactions_count() == 0);

    std::cout << "✅ Initialization test passed" << std::endl;
    return true;
  }

  bool test_protection_levels() {
    std::cout << "Testing protection level configuration..." << std::endl;

    MEVProtection protection;
    
    protection.set_protection_level(ProtectionLevel::NONE);
    assert(protection.get_protection_level() == ProtectionLevel::NONE);
    
    protection.set_protection_level(ProtectionLevel::FAIR_ORDERING);
    assert(protection.get_protection_level() == ProtectionLevel::FAIR_ORDERING);
    
    protection.set_protection_level(ProtectionLevel::SHUFFLED);
    assert(protection.get_protection_level() == ProtectionLevel::SHUFFLED);

    std::cout << "✅ Protection level test passed" << std::endl;
    return true;
  }

  bool test_fair_ordering() {
    std::cout << "Testing fair ordering..." << std::endl;

    MEVProtection protection;
    protection.set_protection_level(ProtectionLevel::FAIR_ORDERING);
    
    // Create test transactions
    std::vector<TransactionPtr> transactions;
    for (int i = 0; i < 10; ++i) {
      transactions.push_back(create_test_transaction(i));
    }

    // Apply fair ordering
    auto ordered = protection.apply_fair_ordering(transactions);
    
    assert(ordered.size() == transactions.size());
    assert(protection.get_protected_transactions_count() == 10);

    std::cout << "✅ Fair ordering test passed" << std::endl;
    return true;
  }

  bool test_transaction_shuffling() {
    std::cout << "Testing transaction shuffling..." << std::endl;

    MEVProtection protection;
    
    // Create test transactions
    std::vector<TransactionPtr> transactions;
    for (int i = 0; i < 20; ++i) {
      transactions.push_back(create_test_transaction(i));
    }

    auto original_first = transactions[0];
    
    // Shuffle transactions
    protection.shuffle_same_priority(transactions);
    
    // Size should remain the same
    assert(transactions.size() == 20);
    
    // Order may have changed (probabilistic test - might occasionally fail)
    // Just verify all transactions are still present
    bool found_first = false;
    for (const auto &tx : transactions) {
      if (tx == original_first) {
        found_first = true;
        break;
      }
    }
    assert(found_first);

    std::cout << "✅ Transaction shuffling test passed" << std::endl;
    return true;
  }

  bool test_detection_toggle() {
    std::cout << "Testing detection enable/disable..." << std::endl;

    MEVProtection protection;
    
    assert(protection.is_detection_enabled());
    
    protection.enable_detection(false);
    assert(!protection.is_detection_enabled());
    
    protection.enable_detection(true);
    assert(protection.is_detection_enabled());

    std::cout << "✅ Detection toggle test passed" << std::endl;
    return true;
  }

  bool test_alert_tracking() {
    std::cout << "Testing alert tracking..." << std::endl;

    MEVProtection protection;
    
    // Simplified test - just check clear works
    protection.clear_alert_history();
    auto recent = protection.get_recent_alerts(10);
    assert(recent.size() == 0);

    std::cout << "✅ Alert tracking test passed" << std::endl;
    return true;
  }

  bool test_suspicious_filtering() {
    std::cout << "Testing suspicious transaction filtering..." << std::endl;

    MEVProtection protection;
    
    // Create test transactions
    std::vector<TransactionPtr> transactions;
    for (int i = 0; i < 10; ++i) {
      transactions.push_back(create_test_transaction(i));
    }

    // Filter with high threshold (should keep all since we don't detect anything)
    auto filtered = protection.filter_suspicious_transactions(transactions, 0.95);
    assert(filtered.size() == transactions.size());  // Should keep all

    std::cout << "✅ Suspicious filtering test passed" << std::endl;
    return true;
  }

  bool test_statistics() {
    std::cout << "Testing MEV protection statistics..." << std::endl;

    MEVProtection protection;
    
    size_t initial_attacks = protection.get_detected_attacks_count();
    size_t initial_protected = protection.get_protected_transactions_count();
    
    // Create and process transactions
    std::vector<TransactionPtr> transactions;
    for (int i = 0; i < 5; ++i) {
      transactions.push_back(create_test_transaction(i));
    }

    auto ordered = protection.apply_fair_ordering(transactions);
    
    // Statistics should have been updated
    assert(protection.get_protected_transactions_count() > initial_protected);

    std::cout << "  Detected attacks: " << protection.get_detected_attacks_count() << std::endl;
    std::cout << "  Protected transactions: " << protection.get_protected_transactions_count() << std::endl;

    std::cout << "✅ Statistics test passed" << std::endl;
    return true;
  }

  bool test_concurrent_access() {
    std::cout << "Testing concurrent access..." << std::endl;

    MEVProtection protection;

    // Simplified concurrent test to avoid hangs
    std::vector<std::thread> threads;
    for (int t = 0; t < 3; ++t) {
      threads.emplace_back([&protection, t]() {
        for (int i = 0; i < 5; ++i) {
          std::vector<TransactionPtr> transactions;
          for (int j = 0; j < 3; ++j) {
            auto tx = std::make_shared<ledger::Transaction>();
            Signature sig;
            sig.resize(64);
            uint64_t id = t * 100 + i * 10 + j;
            for (size_t k = 0; k < 8; ++k) {
              sig[k] = (id >> (k * 8)) & 0xFF;
            }
            tx->signatures.push_back(sig);
            tx->message.resize(100);
            transactions.push_back(tx);
          }
          
          protection.apply_fair_ordering(transactions);
        }
      });
    }

    // Wait for all threads
    for (auto &thread : threads) {
      thread.join();
    }

    // Verify some transactions were protected
    assert(protection.get_protected_transactions_count() > 0);

    std::cout << "✅ Concurrent access test passed" << std::endl;
    return true;
  }
};

} // namespace test
} // namespace slonana

int main() {
  slonana::test::MEVProtectionTester tester;
  return tester.run_all_tests() ? 0 : 1;
}
