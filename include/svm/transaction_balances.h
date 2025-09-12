#pragma once

#include "common/types.h"
#include "svm/engine.h"
#include <unordered_map>
#include <vector>

namespace slonana {
namespace svm {

using namespace slonana::common;

/**
 * Balance information for an account before and after transaction
 */
struct AccountBalance {
  PublicKey address;
  Lamports pre_balance;
  Lamports post_balance;

  AccountBalance() = default;
  AccountBalance(const PublicKey &addr, Lamports pre, Lamports post)
      : address(addr), pre_balance(pre), post_balance(post) {}

  Lamports balance_change() const { return post_balance - pre_balance; }

  bool has_changed() const { return pre_balance != post_balance; }
};

/**
 * Transaction balance information
 * Equivalent to Agave's transaction_balances.rs
 */
class TransactionBalances {
public:
  TransactionBalances() = default;
  ~TransactionBalances() = default;

  /**
   * Record pre-execution balances
   */
  void record_pre_balances(
      const std::vector<PublicKey> &addresses,
      const std::unordered_map<PublicKey, ProgramAccount> &accounts);

  /**
   * Record post-execution balances
   */
  void record_post_balances(
      const std::vector<PublicKey> &addresses,
      const std::unordered_map<PublicKey, ProgramAccount> &accounts);

  /**
   * Get all account balances
   */
  const std::vector<AccountBalance> &get_balances() const { return balances_; }

  /**
   * Get balance for specific account
   */
  std::optional<AccountBalance> get_balance(const PublicKey &address) const;

  /**
   * Get total balance change for the transaction
   */
  Lamports get_total_balance_change() const;

  /**
   * Get accounts that had balance changes
   */
  std::vector<AccountBalance> get_changed_balances() const;

  /**
   * Check if transaction is balanced (total change = 0, except for fees)
   */
  bool is_balanced(Lamports fee_amount = 0) const;

  /**
   * Get balance statistics
   */
  struct BalanceStats {
    size_t total_accounts = 0;
    size_t changed_accounts = 0;
    Lamports total_change = 0;
    Lamports max_increase = 0;
    Lamports max_decrease = 0;
  };
  BalanceStats get_statistics() const;

  /**
   * Reset all recorded balances
   */
  void reset();

  /**
   * Check if balances have been recorded
   */
  bool has_pre_balances() const { return !pre_balances_.empty(); }
  bool has_post_balances() const { return !post_balances_.empty(); }
  bool is_complete() const { return has_pre_balances() && has_post_balances(); }

private:
  std::unordered_map<PublicKey, Lamports> pre_balances_;
  std::unordered_map<PublicKey, Lamports> post_balances_;
  std::vector<AccountBalance> balances_;

  void update_balances();
  Lamports get_account_balance(
      const PublicKey &address,
      const std::unordered_map<PublicKey, ProgramAccount> &accounts) const;
};

/**
 * Balance collection routines for multiple transactions
 */
class BalanceCollector {
public:
  BalanceCollector() = default;
  ~BalanceCollector() = default;

  /**
   * Start collecting balances for a new transaction
   */
  void start_transaction();

  /**
   * Finish collecting balances for current transaction
   */
  void finish_transaction();

  /**
   * Record pre-execution balances for current transaction
   */
  void collect_pre_balances(
      const std::vector<PublicKey> &addresses,
      const std::unordered_map<PublicKey, ProgramAccount> &accounts);

  /**
   * Record post-execution balances for current transaction
   */
  void collect_post_balances(
      const std::vector<PublicKey> &addresses,
      const std::unordered_map<PublicKey, ProgramAccount> &accounts);

  /**
   * Get balances for all collected transactions
   */
  const std::vector<TransactionBalances> &get_all_balances() const {
    return all_balances_;
  }

  /**
   * Get balances for specific transaction
   */
  std::optional<TransactionBalances>
  get_transaction_balances(size_t transaction_index) const;

  /**
   * Get total number of collected transactions
   */
  size_t get_transaction_count() const { return all_balances_.size(); }

  /**
   * Check if any transaction has unbalanced changes
   */
  bool
  has_unbalanced_transactions(const std::vector<Lamports> &fees = {}) const;

  /**
   * Get summary statistics for all transactions
   */
  struct CollectionStats {
    size_t total_transactions = 0;
    size_t balanced_transactions = 0;
    size_t total_accounts_affected = 0;
    Lamports total_balance_changes = 0;
  };
  CollectionStats
  get_collection_statistics(const std::vector<Lamports> &fees = {}) const;

  /**
   * Reset all collected data
   */
  void reset();

private:
  std::vector<TransactionBalances> all_balances_;
  TransactionBalances current_transaction_;
  bool transaction_in_progress_ = false;
};

/**
 * Balance validation utilities
 */
namespace balance_utils {
/**
 * Validate that transaction maintains balance conservation
 */
bool validate_balance_conservation(const TransactionBalances &balances,
                                   Lamports fee_amount = 0);

/**
 * Calculate net balance change ignoring fees
 */
Lamports calculate_net_change(const TransactionBalances &balances);

/**
 * Find accounts with largest balance changes
 */
std::vector<AccountBalance>
find_largest_changes(const TransactionBalances &balances, size_t limit = 10);

/**
 * Format balance changes for logging
 */
std::string format_balance_changes(const TransactionBalances &balances);

/**
 * Check for suspicious balance changes
 */
struct BalanceAlert {
  PublicKey account;
  Lamports change;
  std::string reason;
};
std::vector<BalanceAlert>
check_for_suspicious_changes(const TransactionBalances &balances);
} // namespace balance_utils

} // namespace svm
} // namespace slonana