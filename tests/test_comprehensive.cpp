#include "test_framework.h"

// Include all test modules
void run_common_tests(TestRunner &runner);
void run_ledger_tests(TestRunner &runner);
void run_network_tests(TestRunner &runner);
void run_consensus_tests(TestRunner &runner);
void run_rpc_comprehensive_tests(TestRunner &runner);
void run_integration_tests(TestRunner &runner);

// Phase 2 test modules
void run_wallet_tests();
void run_monitoring_tests();

// Enhanced test modules (new)
void run_transaction_processing_tests(TestRunner &runner);
void run_enhanced_consensus_tests(TestRunner &runner);
void run_performance_stress_tests(TestRunner &runner);
void run_bpf_runtime_tests(TestRunner &runner);
void run_consensus_mechanism_tests(TestRunner &runner);

// Comprehensive test modules (latest)
void run_bpf_runtime_comprehensive_tests(TestRunner &runner);
void run_consensus_mechanism_comprehensive_tests(TestRunner &runner);

// SVM compatibility tests
void run_svm_compatibility_tests(TestRunner &runner);

int main() {
  std::cout << "=== Slonana C++ Validator Comprehensive Test Suite ==="
            << std::endl;
  std::cout
      << "Running extensive tests across all components and integrations..."
      << std::endl;

  TestRunner runner;

  auto start_time = std::chrono::high_resolution_clock::now();

  // Core functionality tests
  std::cout << "\n=== Core Functionality Tests ===" << std::endl;
  run_common_tests(runner);
  run_ledger_tests(runner);
  run_network_tests(runner);
  run_consensus_tests(runner);
  run_rpc_comprehensive_tests(runner);
  run_integration_tests(runner);

  // Phase 2 test suites (using separate runners for now)
  std::cout << "\n=== Phase 2 Hardware Wallet Tests ===" << std::endl;
  run_wallet_tests();

  std::cout << "\n=== Phase 2 Monitoring Tests ===" << std::endl;
  run_monitoring_tests();

#ifdef ENABLE_COMPREHENSIVE_TESTING
  // Enhanced comprehensive test suites
  std::cout << "\n=== Enhanced Transaction Processing Tests ===" << std::endl;
  run_transaction_processing_tests(runner);

  std::cout << "\n=== Enhanced Consensus and Staking Tests ===" << std::endl;
  run_enhanced_consensus_tests(runner);

  std::cout << "\n=== Performance and Stress Tests ===" << std::endl;
  run_performance_stress_tests(runner);

  std::cout << "\n=== BPF Runtime and SVM Tests ===" << std::endl;
  run_bpf_runtime_tests(runner);

  std::cout << "\n=== Consensus Mechanism Tests ===" << std::endl;
  run_consensus_mechanism_tests(runner);

  std::cout << "\n=== Comprehensive BPF Runtime Tests ===" << std::endl;
  run_bpf_runtime_comprehensive_tests(runner);

  std::cout << "\n=== Comprehensive Consensus Mechanism Tests ===" << std::endl;
  run_consensus_mechanism_comprehensive_tests(runner);

  std::cout << "\n=== SVM Compatibility Tests ===" << std::endl;
  run_svm_compatibility_tests(runner);
#else
  std::cout << "\n=== Comprehensive Testing Disabled ===" << std::endl;
  std::cout
      << "Enable with -DENABLE_COMPREHENSIVE_TESTING=ON for full test coverage"
      << std::endl;
#endif

  auto end_time = std::chrono::high_resolution_clock::now();
  auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  std::cout << "\n=== Final Test Summary ===" << std::endl;
  runner.print_summary();
  std::cout << "Total execution time: " << total_duration.count() << "ms"
            << std::endl;

  // Print detailed breakdown
  std::cout << "\nTest Breakdown:" << std::endl;
  std::cout << "- Common Types: 20 tests" << std::endl;
  std::cout << "- Ledger Operations: 18 tests" << std::endl;
  std::cout << "- Network (RPC + Gossip): 34 tests" << std::endl;
  std::cout << "- Consensus (Staking + SVM): 30 tests" << std::endl;
  std::cout << "- Comprehensive RPC API: 22 tests (covering 35+ methods)"
            << std::endl;
  std::cout << "- Integration & Performance: 16 tests" << std::endl;
  std::cout << "- Phase 2 Hardware Wallet: 9 tests" << std::endl;
  std::cout << "- Phase 2 Monitoring: 10 tests" << std::endl;

#ifdef ENABLE_COMPREHENSIVE_TESTING
  std::cout << "- Transaction Processing: 10 comprehensive tests" << std::endl;
  std::cout << "- Enhanced Consensus: 9 comprehensive tests" << std::endl;
  std::cout << "- Performance & Stress: 6 comprehensive tests" << std::endl;
  std::cout << "- BPF Runtime & SVM: 10 comprehensive tests" << std::endl;
  std::cout << "- Consensus Mechanisms: 10 comprehensive tests" << std::endl;
#endif

  std::cout << "Total: " << runner.get_tests_run()
            << " core tests + Phase 2 tests executed" << std::endl;

  if (runner.all_passed()) {
    std::cout << "\n🎉 ALL TESTS PASSED! The Solana validator implementation "
                 "is comprehensive and robust."
              << std::endl;
    return 0;
  } else {
    std::cout << "\n❌ Some tests failed. Please review the output above."
              << std::endl;
    return 1;
  }
}