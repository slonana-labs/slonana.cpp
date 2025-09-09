#pragma once

#include "common/types.h"
#include "svm/engine.h"
#include "svm/nonce_info.h"
#include <optional>

namespace slonana {
namespace svm {

using namespace slonana::common;

/**
 * Rollback account information for transaction failure recovery
 * Equivalent to Agave's rollback_accounts.rs
 */
class RollbackAccounts {
public:
  RollbackAccounts() = default;
  ~RollbackAccounts() = default;

  /**
   * Create rollback accounts with fee payer and optional nonce account
   */
  RollbackAccounts(std::optional<NonceInfo> nonce_account,
                   const PublicKey &fee_payer_address,
                   const AccountInfo &fee_payer_rollback_account,
                   Slot fee_payer_rent_epoch);

  /**
   * Get the nonce account for rollback (if any)
   */
  const std::optional<NonceInfo> &get_nonce_account() const {
    return nonce_account_;
  }

  /**
   * Get fee payer address
   */
  const PublicKey &get_fee_payer_address() const { return fee_payer_address_; }

  /**
   * Get fee payer rollback account state
   */
  const AccountInfo &get_fee_payer_rollback_account() const {
    return fee_payer_rollback_account_;
  }

  /**
   * Get fee payer rent epoch
   */
  Slot get_fee_payer_rent_epoch() const { return fee_payer_rent_epoch_; }

  /**
   * Check if rollback includes a nonce account
   */
  bool has_nonce_account() const { return nonce_account_.has_value(); }

  /**
   * Apply rollback to account states
   */
  void
  apply_rollback(std::unordered_map<PublicKey, ProgramAccount> &accounts) const;

  /**
   * Create fee-only rollback (for failed transactions that only pay fees)
   */
  static RollbackAccounts
  create_fee_only_rollback(const PublicKey &fee_payer_address,
                           const AccountInfo &fee_payer_account,
                           Lamports fee_amount);

  /**
   * Validate rollback accounts consistency
   */
  bool validate() const;

  /**
   * Get rollback summary for logging
   */
  std::string get_summary() const;

  /**
   * Clear all rollback data
   */
  void clear();

  /**
   * Check if rollback is empty
   */
  bool is_empty() const;

private:
  std::optional<NonceInfo> nonce_account_;
  PublicKey fee_payer_address_;
  AccountInfo fee_payer_rollback_account_;
  Slot fee_payer_rent_epoch_ = 0;

  void validate_internal() const;
};

/**
 * Rollback utilities
 */
namespace rollback_utils {
/**
 * Create rollback from transaction accounts
 */
RollbackAccounts create_rollback_from_transaction(
    const std::vector<PublicKey> &account_keys,
    const std::unordered_map<PublicKey, ProgramAccount> &original_accounts,
    const PublicKey &fee_payer, Lamports fee_amount,
    const std::optional<NonceInfo> &nonce_info = std::nullopt);

/**
 * Apply rollback selectively (only for specific accounts)
 */
void apply_selective_rollback(
    const RollbackAccounts &rollback,
    std::unordered_map<PublicKey, ProgramAccount> &accounts,
    const std::vector<PublicKey> &accounts_to_rollback);

/**
 * Calculate rollback size in bytes
 */
size_t calculate_rollback_size(const RollbackAccounts &rollback);

/**
 * Format rollback for debugging
 */
std::string format_rollback_debug(const RollbackAccounts &rollback);
} // namespace rollback_utils

} // namespace svm
} // namespace slonana