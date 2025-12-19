#include "banking/fee_market.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

namespace slonana {
namespace test {

using namespace slonana::banking;

class FeeMarketTester {
public:
  bool run_all_tests() {
    std::cout << "=== Running Fee Market Tests ===" << std::endl;

    bool all_passed = true;
    all_passed &= test_initialization();
    all_passed &= test_fee_classification();
    all_passed &= test_fee_estimation();
    all_passed &= test_base_fee_adjustment();
    all_passed &= test_fee_history_tracking();
    all_passed &= test_percentile_calculation();
    all_passed &= test_priority_fee_calculation();
    all_passed &= test_adaptive_fees();
    all_passed &= test_history_size_limits();
    all_passed &= test_concurrent_access();

    if (all_passed) {
      std::cout << "✅ All Fee Market tests passed!" << std::endl;
    } else {
      std::cout << "❌ Some Fee Market tests failed!" << std::endl;
    }

    return all_passed;
  }

private:
  bool test_initialization() {
    std::cout << "Testing fee market initialization..." << std::endl;

    FeeMarket market;
    
    // Check default base fee
    assert(market.get_current_base_fee() > 0);
    assert(market.get_tracked_fee_count() == 0);
    assert(market.get_inclusion_rate() == 1.0); // Default when no history

    std::cout << "✅ Initialization test passed" << std::endl;
    return true;
  }

  bool test_fee_classification() {
    std::cout << "Testing fee tier classification..." << std::endl;

    FeeMarket market;
    
    // Record various fees to build history
    market.record_transaction_fee(1000, true);
    market.record_transaction_fee(5000, true);
    market.record_transaction_fee(10000, true);
    market.record_transaction_fee(50000, true);
    market.record_transaction_fee(100000, true);
    market.record_transaction_fee(500000, true);

    // Test classification
    auto tier_low = market.classify_fee_tier(2000);
    auto tier_normal = market.classify_fee_tier(10000);
    auto tier_high = market.classify_fee_tier(75000);
    auto tier_urgent = market.classify_fee_tier(600000);

    assert(tier_low == FeeTier::LOW || tier_low == FeeTier::NORMAL);
    assert(tier_urgent == FeeTier::URGENT);

    std::cout << "✅ Fee classification test passed" << std::endl;
    return true;
  }

  bool test_fee_estimation() {
    std::cout << "Testing fee estimation..." << std::endl;

    FeeMarket market;
    
    // Add fee history
    for (int i = 0; i < 100; ++i) {
      market.record_transaction_fee(5000 + i * 1000, true);
    }

    // Estimate fees for different tiers
    uint64_t low_fee = market.estimate_fee_for_priority(FeeTier::LOW);
    uint64_t normal_fee = market.estimate_fee_for_priority(FeeTier::NORMAL);
    uint64_t high_fee = market.estimate_fee_for_priority(FeeTier::HIGH);
    uint64_t urgent_fee = market.estimate_fee_for_priority(FeeTier::URGENT);

    // Verify ordering
    assert(low_fee <= normal_fee);
    assert(normal_fee <= high_fee);
    assert(high_fee <= urgent_fee);

    std::cout << "  Low fee: " << low_fee << " lamports" << std::endl;
    std::cout << "  Normal fee: " << normal_fee << " lamports" << std::endl;
    std::cout << "  High fee: " << high_fee << " lamports" << std::endl;
    std::cout << "  Urgent fee: " << urgent_fee << " lamports" << std::endl;

    std::cout << "✅ Fee estimation test passed" << std::endl;
    return true;
  }

  bool test_base_fee_adjustment() {
    std::cout << "Testing base fee adjustment..." << std::endl;

    FeeMarket market;
    market.enable_adaptive_fees(true);
    
    uint64_t initial_base = market.get_current_base_fee();

    // Simulate high utilization (congested network)
    market.update_base_fee(0.9); // 90% utilization
    uint64_t congested_base = market.get_current_base_fee();
    
    // Fee should increase
    assert(congested_base > initial_base);

    // Simulate low utilization (underutilized network)
    market.update_base_fee(0.1); // 10% utilization
    uint64_t underutilized_base = market.get_current_base_fee();
    
    // Fee should decrease
    assert(underutilized_base < congested_base);

    std::cout << "  Initial base: " << initial_base << " lamports" << std::endl;
    std::cout << "  Congested base: " << congested_base << " lamports" << std::endl;
    std::cout << "  Underutilized base: " << underutilized_base << " lamports" << std::endl;

    std::cout << "✅ Base fee adjustment test passed" << std::endl;
    return true;
  }

  bool test_fee_history_tracking() {
    std::cout << "Testing fee history tracking..." << std::endl;

    FeeMarket market;
    
    // Record fees
    market.record_transaction_fee(5000, true);
    market.record_transaction_fee(10000, false);
    market.record_transaction_fee(15000, true);
    market.record_transaction_fee(20000, true);

    assert(market.get_tracked_fee_count() == 4);
    
    // Check inclusion rate (3 out of 4 included)
    double inclusion_rate = market.get_inclusion_rate();
    assert(inclusion_rate == 0.75);

    std::cout << "  Tracked fees: " << market.get_tracked_fee_count() << std::endl;
    std::cout << "  Inclusion rate: " << inclusion_rate << std::endl;

    std::cout << "✅ Fee history tracking test passed" << std::endl;
    return true;
  }

  bool test_percentile_calculation() {
    std::cout << "Testing percentile calculation..." << std::endl;

    FeeMarket market;
    
    // Add ordered fees
    for (int i = 1; i <= 100; ++i) {
      market.record_transaction_fee(i * 1000, true);
    }

    FeeStats stats = market.get_recent_fee_stats();
    
    assert(stats.min_fee == 1000);
    assert(stats.max_fee == 100000);
    assert(stats.median_fee >= 40000 && stats.median_fee <= 60000);
    assert(stats.p90_fee >= 85000);
    assert(stats.p99_fee >= 95000);
    assert(stats.sample_count == 100);

    std::cout << "  Min fee: " << stats.min_fee << std::endl;
    std::cout << "  Median fee: " << stats.median_fee << std::endl;
    std::cout << "  P90 fee: " << stats.p90_fee << std::endl;
    std::cout << "  P99 fee: " << stats.p99_fee << std::endl;
    std::cout << "  Max fee: " << stats.max_fee << std::endl;

    std::cout << "✅ Percentile calculation test passed" << std::endl;
    return true;
  }

  bool test_priority_fee_calculation() {
    std::cout << "Testing priority fee calculation..." << std::endl;

    FeeMarket market;
    
    uint64_t base_fee = market.get_current_base_fee();
    uint64_t high_fee = base_fee * 2;
    uint64_t low_fee = base_fee / 2;

    // Calculate priority fees
    uint64_t priority_high = market.calculate_priority_fee(high_fee);
    uint64_t priority_low = market.calculate_priority_fee(low_fee);

    // High fee should have priority fee
    assert(priority_high > 0);
    // Low fee should have no priority fee
    assert(priority_low == 0);

    std::cout << "  Base fee: " << base_fee << std::endl;
    std::cout << "  High fee priority: " << priority_high << std::endl;
    std::cout << "  Low fee priority: " << priority_low << std::endl;

    std::cout << "✅ Priority fee calculation test passed" << std::endl;
    return true;
  }

  bool test_adaptive_fees() {
    std::cout << "Testing adaptive fee toggle..." << std::endl;

    FeeMarket market;
    uint64_t initial_base = market.get_current_base_fee();

    // Disable adaptive fees
    market.enable_adaptive_fees(false);
    market.update_base_fee(0.9);
    uint64_t after_disabled = market.get_current_base_fee();
    
    // Should not change when disabled
    assert(after_disabled == initial_base);

    // Enable adaptive fees
    market.enable_adaptive_fees(true);
    market.update_base_fee(0.9);
    uint64_t after_enabled = market.get_current_base_fee();
    
    // Should change when enabled
    assert(after_enabled > initial_base);

    std::cout << "✅ Adaptive fee toggle test passed" << std::endl;
    return true;
  }

  bool test_history_size_limits() {
    std::cout << "Testing history size limits..." << std::endl;

    FeeMarket market;
    market.set_max_history_size(100);

    // Add more than the limit
    for (int i = 0; i < 200; ++i) {
      market.record_transaction_fee(1000 + i, true);
    }

    // Should be capped at limit
    assert(market.get_tracked_fee_count() == 100);

    std::cout << "✅ History size limits test passed" << std::endl;
    return true;
  }

  bool test_concurrent_access() {
    std::cout << "Testing concurrent access..." << std::endl;

    FeeMarket market;

    // Launch multiple threads
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
      threads.emplace_back([&market, i]() {
        for (int j = 0; j < 100; ++j) {
          market.record_transaction_fee(5000 + i * 100 + j, true);
          market.get_recent_fee_stats();
          market.classify_fee_tier(10000);
        }
      });
    }

    // Wait for all threads
    for (auto &thread : threads) {
      thread.join();
    }

    // Verify some data was recorded
    assert(market.get_tracked_fee_count() > 0);

    std::cout << "✅ Concurrent access test passed" << std::endl;
    return true;
  }
};

} // namespace test
} // namespace slonana

int main() {
  slonana::test::FeeMarketTester tester;
  return tester.run_all_tests() ? 0 : 1;
}
