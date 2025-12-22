#pragma once

#include "common/types.h"
#include "ledger/manager.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace slonana {
namespace banking {

using namespace slonana::common;

/**
 * Protection level for transaction ordering
 */
enum class ProtectionLevel {
  NONE,          // Standard ordering (no protection)
  FAIR_ORDERING, // FIFO within fee tier
  SHUFFLED,      // Randomized within same priority level
  PRIVATE        // Private transaction submission
};

/**
 * MEV (Maximal Extractable Value) alert information
 */
struct MEVAlert {
  enum class Type {
    SANDWICH_ATTACK,
    FRONT_RUNNING,
    BACK_RUNNING,
    BUNDLE_MANIPULATION,
    SUSPICIOUS_PATTERN
  };

  Type type;
  std::vector<Hash> suspicious_transactions;
  double confidence_score; // 0.0 to 1.0
  std::string description;
  std::chrono::steady_clock::time_point detected_at;

  MEVAlert(Type t, std::vector<Hash> txs, double score, std::string desc)
      : type(t), suspicious_transactions(std::move(txs)), confidence_score(score),
        description(std::move(desc)),
        detected_at(std::chrono::steady_clock::now()) {}

  // Copy constructor
  MEVAlert(const MEVAlert& other) = default;
  
  // Move constructor
  MEVAlert(MEVAlert&& other) = default;
  
  // Copy assignment
  MEVAlert& operator=(const MEVAlert& other) = default;
  
  // Move assignment
  MEVAlert& operator=(MEVAlert&& other) = default;
};

/**
 * MEV Protection Manager
 * 
 * Implements MEV detection and prevention mechanisms to protect against
 * transaction ordering manipulation and various MEV attack vectors.
 */
class MEVProtection {
public:
  using TransactionPtr = std::shared_ptr<ledger::Transaction>;

  MEVProtection();
  ~MEVProtection();

  // Detection
  /**
   * Detect potential MEV patterns in a batch of transactions
   * @param transactions Transaction batch to analyze
   * @return Vector of detected MEV alerts
   */
  std::vector<MEVAlert> detect_mev_patterns(
      const std::vector<TransactionPtr> &transactions);

  /**
   * Check if a transaction triplet forms a sandwich attack
   * @param tx1 First transaction (attacker front-run)
   * @param victim Middle transaction (victim)
   * @param tx2 Last transaction (attacker back-run)
   * @return True if sandwich attack pattern detected
   */
  bool is_sandwich_attack(const TransactionPtr &tx1, const TransactionPtr &victim,
                          const TransactionPtr &tx2);

  /**
   * Check if a transaction is front-running another
   * @param original Original transaction
   * @param frontrun Potential front-running transaction
   * @return True if front-running detected
   */
  bool is_front_running(const TransactionPtr &original,
                        const TransactionPtr &frontrun);

  /**
   * Check if a transaction is back-running another
   * @param target Target transaction
   * @param backrun Potential back-running transaction
   * @return True if back-running detected
   */
  bool is_back_running(const TransactionPtr &target,
                       const TransactionPtr &backrun);

  // Protection
  /**
   * Apply fair ordering to transactions
   * @param transactions Unordered transactions
   * @return Fairly ordered transactions
   */
  std::vector<TransactionPtr> apply_fair_ordering(
      std::vector<TransactionPtr> transactions);

  /**
   * Shuffle transactions with the same priority level
   * @param transactions Transactions to shuffle (modified in place)
   */
  void shuffle_same_priority(std::vector<TransactionPtr> &transactions);

  /**
   * Filter out suspicious transactions
   * @param transactions Input transactions
   * @param confidence_threshold Minimum confidence to filter (0.0 to 1.0)
   * @return Filtered transaction list
   */
  std::vector<TransactionPtr> filter_suspicious_transactions(
      const std::vector<TransactionPtr> &transactions,
      double confidence_threshold = 0.8);

  // Configuration
  /**
   * Set the protection level
   * @param level Protection level to use
   */
  void set_protection_level(ProtectionLevel level);

  /**
   * Get the current protection level
   * @return Current protection level
   */
  ProtectionLevel get_protection_level() const;

  /**
   * Enable or disable MEV detection
   * @param enable True to enable detection
   */
  void enable_detection(bool enable);

  /**
   * Check if detection is enabled
   * @return True if detection is enabled
   */
  bool is_detection_enabled() const;

  /**
   * Set the confidence threshold for alerts
   * @param threshold Minimum confidence (0.0 to 1.0)
   */
  void set_alert_threshold(double threshold);

  // Statistics
  /**
   * Get the number of MEV attacks detected
   * @return Total detected attacks
   */
  size_t get_detected_attacks_count() const;

  /**
   * Get the number of transactions protected
   * @return Total protected transactions
   */
  size_t get_protected_transactions_count() const;

  /**
   * Get recent MEV alerts
   * @param max_count Maximum number of alerts to return
   * @return Recent MEV alerts
   */
  std::vector<MEVAlert> get_recent_alerts(size_t max_count = 100);

  /**
   * Clear alert history
   */
  void clear_alert_history();

private:
  ProtectionLevel protection_level_;
  bool detection_enabled_;
  double alert_threshold_;

  // Statistics
  std::atomic<size_t> detected_attacks_;
  std::atomic<size_t> protected_transactions_;

  // Alert history
  std::vector<MEVAlert> alert_history_;
  mutable std::mutex alert_mutex_;
  static constexpr size_t MAX_ALERT_HISTORY = 1000;

  // Configuration
  static constexpr double DEFAULT_ALERT_THRESHOLD = 0.7;
  static constexpr size_t MAX_SAME_SENDER_CONSECUTIVE = 3;

  // Detection helper methods
  bool transactions_access_same_accounts(const TransactionPtr &tx1,
                                         const TransactionPtr &tx2);
  bool transactions_have_similar_operations(const TransactionPtr &tx1,
                                            const TransactionPtr &tx2);
  double calculate_mev_confidence(const TransactionPtr &tx1,
                                  const TransactionPtr &tx2,
                                  const TransactionPtr &tx3);
  Hash get_transaction_hash(const TransactionPtr &tx);
  PublicKey get_transaction_sender(const TransactionPtr &tx);

  // Protection helper methods
  void trim_alert_history();
};

} // namespace banking
} // namespace slonana
