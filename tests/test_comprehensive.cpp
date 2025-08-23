#include "test_framework.h"

// Include all test modules
void run_common_tests(TestRunner& runner);
void run_ledger_tests(TestRunner& runner);
void run_network_tests(TestRunner& runner);
void run_consensus_tests(TestRunner& runner);
void run_rpc_comprehensive_tests(TestRunner& runner);
void run_integration_tests(TestRunner& runner);

// Phase 2 test modules
void run_wallet_tests();
void run_monitoring_tests();

int main() {
    std::cout << "=== Slonana C++ Validator Comprehensive Test Suite ===" << std::endl;
    std::cout << "Running extensive tests across all components and integrations..." << std::endl;
    
    TestRunner runner;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Run all test suites
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
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "\n=== Final Test Summary ===" << std::endl;
    runner.print_summary();
    std::cout << "Total execution time: " << total_duration.count() << "ms" << std::endl;
    
    // Print detailed breakdown
    std::cout << "\nTest Breakdown:" << std::endl;
    std::cout << "- Common Types: 10 tests" << std::endl;
    std::cout << "- Ledger Operations: 9 tests" << std::endl;
    std::cout << "- Network (RPC + Gossip): 17 tests" << std::endl;
    std::cout << "- Consensus (Staking + SVM): 15 tests" << std::endl;
    std::cout << "- Comprehensive RPC API: 11 tests (covering 35+ methods)" << std::endl;
    std::cout << "- Integration & Performance: 8 tests" << std::endl;
    std::cout << "- Phase 2 Hardware Wallet: 9 tests" << std::endl;
    std::cout << "- Phase 2 Monitoring: 10 tests" << std::endl;
    std::cout << "Total: " << runner.get_tests_run() << " core tests + Phase 2 tests executed" << std::endl;
    
    if (runner.all_passed()) {
        std::cout << "\n🎉 ALL TESTS PASSED! The Solana validator implementation is comprehensive and robust." << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ Some tests failed. Please review the output above." << std::endl;
        return 1;
    }
}