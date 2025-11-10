#pragma once

#include "common/types.h"
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace slonana {
namespace banking {

/**
 * Fee tier classification for transaction priority
 */
enum class FeeTier {
  URGENT,  // >99th percentile
  HIGH,    // 90-99th percentile
  NORMAL,  // 25-90th percentile
  LOW      // <25th percentile
};

/**
 * Statistical summary of recent fees
 */
struct FeeStats {
  uint64_t min_fee = 0;
  uint64_t median_fee = 0;
  uint64_t p90_fee = 0;
  uint64_t p99_fee = 0;
  uint64_t max_fee = 0;
  size_t sample_count = 0;
  
  FeeStats() = default;
};

/**
 * Fee Market Manager
 * 
 * Implements sophisticated fee market mechanics for optimal transaction ordering
 * and priority fee handling, compatible with Agave's fee market implementation.
 */
class FeeMarket {
public:
  FeeMarket();
  ~FeeMarket();

  // Fee calculation
  /**
   * Calculate the priority fee for a transaction
   * @param fee Base fee from transaction
   * @return Calculated priority fee
   */
  uint64_t calculate_priority_fee(uint64_t base_fee);

  /**
   * Classify a fee into a priority tier
   * @param fee Fee amount to classify
   * @return Fee tier classification
   */
  FeeTier classify_fee_tier(uint64_t fee);

  // Fee estimation
  /**
   * Estimate the minimum fee needed for a specific priority tier
   * @param tier Desired priority tier
   * @return Estimated fee in lamports
   */
  uint64_t estimate_fee_for_priority(FeeTier tier);

  /**
   * Get statistical summary of recent fees
   * @return Fee statistics
   */
  FeeStats get_recent_fee_stats();

  // Dynamic adjustment
  /**
   * Update the base fee based on network congestion
   * @param block_utilization Block utilization percentage (0.0 to 1.0)
   */
  void update_base_fee(double block_utilization);

  /**
   * Get the current base fee
   * @return Current base fee in lamports
   */
  uint64_t get_current_base_fee() const;

  // History tracking
  /**
   * Record a transaction fee and its inclusion status
   * @param fee Transaction fee amount
   * @param included Whether the transaction was included in a block
   */
  void record_transaction_fee(uint64_t fee, bool included);

  // Configuration
  /**
   * Set the target block utilization for base fee adjustment
   * @param target Target utilization (0.0 to 1.0)
   */
  void set_target_utilization(double target);

  /**
   * Set the maximum number of recent fees to track
   * @param max_history Maximum history size
   */
  void set_max_history_size(size_t max_history);

  /**
   * Enable or disable adaptive fee adjustments
   * @param enabled True to enable adaptive adjustments
   */
  void enable_adaptive_fees(bool enabled);

  // Statistics
  /**
   * Get the number of fees currently tracked
   * @return Number of tracked fees
   */
  size_t get_tracked_fee_count() const;

  /**
   * Get the inclusion rate for recent transactions
   * @return Inclusion rate (0.0 to 1.0)
   */
  double get_inclusion_rate() const;

private:
  // Base fee state
  std::atomic<uint64_t> base_fee_;
  double target_utilization_;
  bool adaptive_fees_enabled_;

  // Fee history
  std::deque<uint64_t> recent_fees_;
  std::deque<bool> inclusion_status_;
  size_t max_history_size_;
  mutable std::mutex history_mutex_;

  // Configuration constants
  static constexpr uint64_t DEFAULT_BASE_FEE = 5000; // 5000 lamports
  static constexpr size_t DEFAULT_MAX_HISTORY = 10000;
  static constexpr double DEFAULT_TARGET_UTILIZATION = 0.5;
  static constexpr double BASE_FEE_ADJUSTMENT_FACTOR = 0.125; // 12.5%

  // Percentile thresholds
  static constexpr double P99_THRESHOLD = 0.99;
  static constexpr double P90_THRESHOLD = 0.90;
  static constexpr double P25_THRESHOLD = 0.25;

  // Internal helper methods
  uint64_t calculate_percentile(double percentile);
  void trim_history_if_needed();
  std::vector<uint64_t> get_sorted_fees();
};

} // namespace banking
} // namespace slonana
