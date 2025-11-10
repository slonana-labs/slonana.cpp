#include "banking/fee_market.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace slonana {
namespace banking {

FeeMarket::FeeMarket()
    : base_fee_(DEFAULT_BASE_FEE), target_utilization_(DEFAULT_TARGET_UTILIZATION),
      adaptive_fees_enabled_(true), max_history_size_(DEFAULT_MAX_HISTORY) {
  LOG_INFO("FeeMarket initialized with base fee: {} lamports", DEFAULT_BASE_FEE);
}

FeeMarket::~FeeMarket() {
  LOG_INFO("FeeMarket shutting down");
}

uint64_t FeeMarket::calculate_priority_fee(uint64_t base_fee) {
  uint64_t current_base = base_fee_.load();
  
  // Priority fee is the amount above the base fee
  if (base_fee > current_base) {
    return base_fee - current_base;
  }
  
  return 0;
}

FeeTier FeeMarket::classify_fee_tier(uint64_t fee) {
  std::lock_guard<std::mutex> lock(history_mutex_);
  
  if (recent_fees_.empty()) {
    // No history - classify based on base fee
    uint64_t current_base = base_fee_.load();
    if (fee >= current_base * 5) return FeeTier::URGENT;
    if (fee >= current_base * 3) return FeeTier::HIGH;
    if (fee >= current_base) return FeeTier::NORMAL;
    return FeeTier::LOW;
  }

  // Calculate percentiles from recent fees
  uint64_t p99 = calculate_percentile(P99_THRESHOLD);
  uint64_t p90 = calculate_percentile(P90_THRESHOLD);
  uint64_t p25 = calculate_percentile(P25_THRESHOLD);

  // Classify based on percentile thresholds
  if (fee >= p99) return FeeTier::URGENT;
  if (fee >= p90) return FeeTier::HIGH;
  if (fee >= p25) return FeeTier::NORMAL;
  return FeeTier::LOW;
}

uint64_t FeeMarket::estimate_fee_for_priority(FeeTier tier) {
  std::lock_guard<std::mutex> lock(history_mutex_);
  
  uint64_t current_base = base_fee_.load();
  
  if (recent_fees_.empty()) {
    // No history - estimate based on base fee multiples
    switch (tier) {
      case FeeTier::URGENT:
        return current_base * 5;
      case FeeTier::HIGH:
        return current_base * 3;
      case FeeTier::NORMAL:
        return current_base * 2;
      case FeeTier::LOW:
        return current_base;
    }
    return current_base;
  }

  // Estimate based on recent percentiles
  switch (tier) {
    case FeeTier::URGENT:
      return calculate_percentile(P99_THRESHOLD);
    case FeeTier::HIGH:
      return calculate_percentile(P90_THRESHOLD);
    case FeeTier::NORMAL:
      return calculate_percentile(0.5); // median
    case FeeTier::LOW:
      return calculate_percentile(P25_THRESHOLD);
  }

  return current_base;
}

FeeStats FeeMarket::get_recent_fee_stats() {
  std::lock_guard<std::mutex> lock(history_mutex_);
  
  FeeStats stats;
  stats.sample_count = recent_fees_.size();

  if (recent_fees_.empty()) {
    return stats;
  }

  auto sorted = get_sorted_fees();
  
  stats.min_fee = sorted.front();
  stats.max_fee = sorted.back();
  stats.median_fee = calculate_percentile(0.5);
  stats.p90_fee = calculate_percentile(P90_THRESHOLD);
  stats.p99_fee = calculate_percentile(P99_THRESHOLD);

  return stats;
}

void FeeMarket::update_base_fee(double block_utilization) {
  if (!adaptive_fees_enabled_) {
    return;
  }

  // Clamp utilization to valid range
  block_utilization = std::max(0.0, std::min(1.0, block_utilization));

  uint64_t current_base = base_fee_.load();
  
  // Calculate adjustment factor based on deviation from target
  double deviation = block_utilization - target_utilization_;
  
  // Use exponential adjustment for faster response
  // Positive deviation (congested) -> increase fee
  // Negative deviation (underutilized) -> decrease fee
  double adjustment_multiplier = 1.0 + (deviation * BASE_FEE_ADJUSTMENT_FACTOR);
  
  // Apply minimum and maximum bounds
  adjustment_multiplier = std::max(0.875, std::min(1.125, adjustment_multiplier));
  
  uint64_t new_base = static_cast<uint64_t>(current_base * adjustment_multiplier);
  
  // Ensure minimum base fee
  new_base = std::max(uint64_t(1000), new_base);
  
  base_fee_.store(new_base);
  
  LOG_DEBUG("Base fee adjusted: {} -> {} (utilization: {:.2f}, target: {:.2f})",
            current_base, new_base, block_utilization, target_utilization_);
}

uint64_t FeeMarket::get_current_base_fee() const {
  return base_fee_.load();
}

void FeeMarket::record_transaction_fee(uint64_t fee, bool included) {
  std::lock_guard<std::mutex> lock(history_mutex_);
  
  recent_fees_.push_back(fee);
  inclusion_status_.push_back(included);
  
  trim_history_if_needed();
}

void FeeMarket::set_target_utilization(double target) {
  target_utilization_ = std::max(0.0, std::min(1.0, target));
  LOG_INFO("Target utilization set to: {:.2f}", target_utilization_);
}

void FeeMarket::set_max_history_size(size_t max_history) {
  std::lock_guard<std::mutex> lock(history_mutex_);
  max_history_size_ = std::max(size_t(100), max_history);
  trim_history_if_needed();
}

void FeeMarket::enable_adaptive_fees(bool enabled) {
  adaptive_fees_enabled_ = enabled;
  LOG_INFO("Adaptive fees {}", enabled ? "enabled" : "disabled");
}

size_t FeeMarket::get_tracked_fee_count() const {
  std::lock_guard<std::mutex> lock(history_mutex_);
  return recent_fees_.size();
}

double FeeMarket::get_inclusion_rate() const {
  std::lock_guard<std::mutex> lock(history_mutex_);
  
  if (inclusion_status_.empty()) {
    return 1.0;
  }

  size_t included_count = std::count(inclusion_status_.begin(), 
                                     inclusion_status_.end(), true);
  return static_cast<double>(included_count) / inclusion_status_.size();
}

// Private helper methods

uint64_t FeeMarket::calculate_percentile(double percentile) {
  // Caller must hold history_mutex_
  
  if (recent_fees_.empty()) {
    return base_fee_.load();
  }

  auto sorted = get_sorted_fees();
  
  size_t index = static_cast<size_t>(percentile * (sorted.size() - 1));
  return sorted[index];
}

void FeeMarket::trim_history_if_needed() {
  // Caller must hold history_mutex_
  
  while (recent_fees_.size() > max_history_size_) {
    recent_fees_.pop_front();
  }
  
  while (inclusion_status_.size() > max_history_size_) {
    inclusion_status_.pop_front();
  }
}

std::vector<uint64_t> FeeMarket::get_sorted_fees() {
  // Caller must hold history_mutex_
  
  std::vector<uint64_t> sorted(recent_fees_.begin(), recent_fees_.end());
  std::sort(sorted.begin(), sorted.end());
  return sorted;
}

} // namespace banking
} // namespace slonana
