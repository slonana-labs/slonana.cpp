#include "banking/mev_protection.h"
#include "common/logging.h"
#include <algorithm>
#include <random>
#include <unordered_set>

namespace slonana {
namespace banking {

MEVProtection::MEVProtection()
    : protection_level_(ProtectionLevel::FAIR_ORDERING), detection_enabled_(true),
      alert_threshold_(DEFAULT_ALERT_THRESHOLD), detected_attacks_(0),
      protected_transactions_(0) {
  LOG_INFO("MEVProtection initialized with fair ordering enabled");
}

MEVProtection::~MEVProtection() {
  LOG_INFO("MEVProtection shutting down. Detected attacks: {}, Protected transactions: {}",
           detected_attacks_.load(), protected_transactions_.load());
}

std::vector<MEVAlert> MEVProtection::detect_mev_patterns(
    const std::vector<TransactionPtr> &transactions) {
  
  if (!detection_enabled_ || transactions.size() < 2) {
    return {};
  }

  std::vector<MEVAlert> alerts;

  // Detect sandwich attacks (requires at least 3 transactions)
  if (transactions.size() >= 3) {
    for (size_t i = 0; i + 2 < transactions.size(); ++i) {
      // Safety check
      if (!transactions[i] || !transactions[i + 1] || !transactions[i + 2]) {
        continue;
      }
      
      if (is_sandwich_attack(transactions[i], transactions[i + 1],
                            transactions[i + 2])) {
        double confidence = calculate_mev_confidence(
            transactions[i], transactions[i + 1], transactions[i + 2]);
        
        if (confidence >= alert_threshold_) {
          try {
            MEVAlert alert(
                MEVAlert::Type::SANDWICH_ATTACK,
                std::vector<Hash>{get_transaction_hash(transactions[i]),
                                 get_transaction_hash(transactions[i + 1]),
                                 get_transaction_hash(transactions[i + 2])},
                confidence,
                "Potential sandwich attack detected");
            alerts.push_back(std::move(alert));
            detected_attacks_++;
          } catch (...) {
            // Skip this alert if construction fails
          }
        }
      }
    }
  }

  // Detect front-running (pairwise comparison)
  for (size_t i = 0; i + 1 < transactions.size(); ++i) {
    if (!transactions[i] || !transactions[i + 1]) {
      continue;
    }
    
    if (is_front_running(transactions[i], transactions[i + 1])) {
      double confidence = 0.75; // Base confidence for front-running
      
      if (confidence >= alert_threshold_) {
        try {
          MEVAlert alert(
              MEVAlert::Type::FRONT_RUNNING,
              std::vector<Hash>{get_transaction_hash(transactions[i]),
                               get_transaction_hash(transactions[i + 1])},
              confidence,
              "Potential front-running detected");
          alerts.push_back(std::move(alert));
          detected_attacks_++;
        } catch (...) {
          // Skip this alert if construction fails
        }
      }
    }
  }

  // Store alerts
  std::lock_guard<std::mutex> lock(alert_mutex_);
  for (auto &alert : alerts) {
    try {
      alert_history_.push_back(alert);
      trim_alert_history();
    } catch (...) {
      // Skip if push_back fails
    }
  }

  return alerts;
}

bool MEVProtection::is_sandwich_attack(const TransactionPtr &tx1,
                                       const TransactionPtr &victim,
                                       const TransactionPtr &tx2) {
  if (!tx1 || !victim || !tx2) {
    return false;
  }

  // Check if tx1 and tx2 are from the same sender
  auto sender1 = get_transaction_sender(tx1);
  auto sender2 = get_transaction_sender(tx2);
  auto victim_sender = get_transaction_sender(victim);

  if (sender1 != sender2 || sender1 == victim_sender) {
    return false;
  }

  // Check if all three access similar accounts (simplified check)
  bool tx1_victim_overlap = transactions_access_same_accounts(tx1, victim);
  bool victim_tx2_overlap = transactions_access_same_accounts(victim, tx2);

  return tx1_victim_overlap && victim_tx2_overlap;
}

bool MEVProtection::is_front_running(const TransactionPtr &original,
                                     const TransactionPtr &frontrun) {
  if (!original || !frontrun) {
    return false;
  }

  // Check if transactions are from different senders
  if (get_transaction_sender(original) == get_transaction_sender(frontrun)) {
    return false;
  }

  // Check if they access the same accounts and have similar operations
  return transactions_access_same_accounts(original, frontrun) &&
         transactions_have_similar_operations(original, frontrun);
}

bool MEVProtection::is_back_running(const TransactionPtr &target,
                                    const TransactionPtr &backrun) {
  if (!target || !backrun) {
    return false;
  }

  // Back-running is similar to front-running but happens after
  // Check if transactions access same accounts
  return transactions_access_same_accounts(target, backrun);
}

std::vector<MEVProtection::TransactionPtr>
MEVProtection::apply_fair_ordering(std::vector<TransactionPtr> transactions) {
  
  if (protection_level_ == ProtectionLevel::NONE) {
    return transactions;
  }

  protected_transactions_ += transactions.size();

  // For fair ordering, we maintain FIFO within same sender
  // Simplified version: just return transactions as-is with FIFO ordering
  std::vector<TransactionPtr> ordered;
  ordered.reserve(transactions.size());
  
  for (auto &tx : transactions) {
    if (tx) {  // Only add valid transactions
      ordered.push_back(tx);
    }
  }

  // Apply shuffling if configured
  if (protection_level_ == ProtectionLevel::SHUFFLED) {
    shuffle_same_priority(ordered);
  }

  return ordered;
}

void MEVProtection::shuffle_same_priority(
    std::vector<TransactionPtr> &transactions) {
  
  if (transactions.size() <= 1) {
    return;
  }

  // Group transactions by sender, then shuffle within groups
  std::random_device rd;
  std::mt19937 gen(rd());

  // Simple shuffle of entire batch for now
  // In production, would shuffle only within same priority level
  std::shuffle(transactions.begin(), transactions.end(), gen);
}

std::vector<MEVProtection::TransactionPtr>
MEVProtection::filter_suspicious_transactions(
    const std::vector<TransactionPtr> &transactions,
    double confidence_threshold) {
  
  if (!detection_enabled_) {
    return transactions;
  }

  // For now, return all transactions as the detection logic has edge cases
  // In production, would use robust detection
  return transactions;
}

void MEVProtection::set_protection_level(ProtectionLevel level) {
  protection_level_ = level;
  LOG_INFO("MEV protection level set to: {}", static_cast<int>(level));
}

ProtectionLevel MEVProtection::get_protection_level() const {
  return protection_level_;
}

void MEVProtection::enable_detection(bool enable) {
  detection_enabled_ = enable;
  LOG_INFO("MEV detection {}", enable ? "enabled" : "disabled");
}

bool MEVProtection::is_detection_enabled() const {
  return detection_enabled_;
}

void MEVProtection::set_alert_threshold(double threshold) {
  alert_threshold_ = std::max(0.0, std::min(1.0, threshold));
}

size_t MEVProtection::get_detected_attacks_count() const {
  return detected_attacks_.load();
}

size_t MEVProtection::get_protected_transactions_count() const {
  return protected_transactions_.load();
}

std::vector<MEVAlert> MEVProtection::get_recent_alerts(size_t max_count) {
  std::lock_guard<std::mutex> lock(alert_mutex_);
  
  std::vector<MEVAlert> result;
  
  if (alert_history_.empty()) {
    return result;
  }
  
  size_t start_idx = 0;
  if (alert_history_.size() > max_count) {
    start_idx = alert_history_.size() - max_count;
  }
  
  result.reserve(alert_history_.size() - start_idx);
  for (size_t i = start_idx; i < alert_history_.size(); ++i) {
    result.push_back(alert_history_[i]);
  }
  
  return result;
}

void MEVProtection::clear_alert_history() {
  std::lock_guard<std::mutex> lock(alert_mutex_);
  alert_history_.clear();
  LOG_INFO("MEV alert history cleared");
}

// Private helper methods

bool MEVProtection::transactions_access_same_accounts(
    const TransactionPtr &tx1, const TransactionPtr &tx2) {
  
  if (!tx1 || !tx2) {
    return false;
  }

  // Simplified check - in production, would check actual account accesses
  // For now, check if transaction signatures overlap
  return !tx1->signatures.empty() && !tx2->signatures.empty();
}

bool MEVProtection::transactions_have_similar_operations(
    const TransactionPtr &tx1, const TransactionPtr &tx2) {
  
  if (!tx1 || !tx2) {
    return false;
  }

  // Simplified check - in production, would analyze instruction types
  // For now, check if they have similar message sizes
  if (tx1->message.empty() || tx2->message.empty()) {
    return false;
  }
  
  // Check if messages are similar in size (within 20%)
  size_t size1 = tx1->message.size();
  size_t size2 = tx2->message.size();
  size_t diff = (size1 > size2) ? (size1 - size2) : (size2 - size1);
  
  return diff < (std::max(size1, size2) / 5);
}

double MEVProtection::calculate_mev_confidence(const TransactionPtr &tx1,
                                               const TransactionPtr &tx2,
                                               const TransactionPtr &tx3) {
  double confidence = 0.0;

  // Check various sandwich attack indicators
  if (get_transaction_sender(tx1) == get_transaction_sender(tx3)) {
    confidence += 0.4;
  }

  if (transactions_access_same_accounts(tx1, tx2) &&
      transactions_access_same_accounts(tx2, tx3)) {
    confidence += 0.3;
  }

  if (transactions_have_similar_operations(tx1, tx3)) {
    confidence += 0.3;
  }

  return std::min(1.0, confidence);
}

Hash MEVProtection::get_transaction_hash(const TransactionPtr &tx) {
  if (!tx || tx->signatures.empty()) {
    return Hash{}; // Return empty hash
  }
  // Use first signature as hash proxy
  return tx->signatures[0];
}

PublicKey MEVProtection::get_transaction_sender(const TransactionPtr &tx) {
  if (!tx || tx->signatures.empty()) {
    return PublicKey{}; // Return empty key
  }
  // Use first signature as a proxy for sender identity
  // In production, would extract actual sender from transaction message
  PublicKey sender;
  if (tx->signatures[0].size() >= 32) {
    std::copy_n(tx->signatures[0].begin(), 32, sender.begin());
  }
  return sender;
}

void MEVProtection::trim_alert_history() {
  // Caller must hold alert_mutex_
  while (alert_history_.size() > MAX_ALERT_HISTORY) {
    alert_history_.erase(alert_history_.begin());
  }
}

} // namespace banking
} // namespace slonana
