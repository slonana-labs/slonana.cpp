#include "svm/account_loader.h"
#include <algorithm>
#include <iostream>
#include <unordered_set>

namespace slonana {
namespace svm {

// AccountLoader implementation
class AccountLoader::Impl {
public:
  AccountLoadingCallback *callback_;
  std::unordered_map<PublicKey, LoadedAccount> account_cache_;
  AccountLoader::CacheStats stats_;
  std::unordered_set<PublicKey> accessed_accounts_;

  explicit Impl(AccountLoadingCallback *callback) : callback_(callback) {}

  std::optional<LoadedAccount> load_account_internal(const PublicKey &address,
                                                     bool is_writable,
                                                     bool is_signer) {
    // Check cache first
    auto cache_it = account_cache_.find(address);
    if (cache_it != account_cache_.end()) {
      stats_.hits++;
      return cache_it->second;
    }

    // Load from callback
    auto account_opt = callback_->get_account(address);
    if (!account_opt) {
      stats_.misses++;
      return std::nullopt;
    }

    LoadedAccount loaded_account(address, *account_opt,
                                 account_opt->data.size() +
                                     128, // Account metadata overhead
                                 callback_->get_slot(), is_writable, is_signer);

    // Cache the account
    account_cache_[address] = loaded_account;
    stats_.total_loaded_size += loaded_account.loaded_size;
    accessed_accounts_.insert(address);

    return loaded_account;
  }

  TransactionLoadResult
  validate_account_constraints(const std::vector<PublicKey> &account_keys,
                               const std::vector<bool> &is_signer,
                               const std::vector<bool> &is_writable,
                               size_t max_loaded_accounts_data_size) {
    // Check for duplicate accounts in writable positions
    std::unordered_set<PublicKey> writable_accounts;
    for (size_t i = 0; i < account_keys.size() && i < is_writable.size(); ++i) {
      if (is_writable[i]) {
        if (writable_accounts.count(account_keys[i])) {
          return TransactionLoadResult::DUPLICATE_INSTRUCTION;
        }
        writable_accounts.insert(account_keys[i]);
      }
    }

    // Check total data size constraint
    size_t total_data_size = 0;
    for (const auto &key : account_keys) {
      auto account_opt = callback_->get_account(key);
      if (account_opt) {
        total_data_size += account_opt->data.size() + 128; // Metadata overhead
      }
    }

    if (total_data_size > max_loaded_accounts_data_size) {
      return TransactionLoadResult::MAX_LOADED_ACCOUNTS_DATA_SIZE_EXCEEDED;
    }

    return TransactionLoadResult::SUCCESS;
  }
};

AccountLoader::AccountLoader(AccountLoadingCallback *callback)
    : impl_(std::make_unique<Impl>(callback)) {}

AccountLoader::~AccountLoader() = default;

LoadedTransaction AccountLoader::load_transaction_accounts(
    const std::vector<PublicKey> &account_keys,
    const std::vector<bool> &is_signer, const std::vector<bool> &is_writable,
    const PublicKey &fee_payer, Lamports fee_amount,
    size_t max_loaded_accounts_data_size) {
  LoadedTransaction loaded_tx;

  // Validate constraints first
  auto constraint_result = impl_->validate_account_constraints(
      account_keys, is_signer, is_writable, max_loaded_accounts_data_size);
  if (constraint_result != TransactionLoadResult::SUCCESS) {
    loaded_tx.load_result = constraint_result;
    return loaded_tx;
  }

  // Validate fee payer first
  auto fee_payer_result = validate_fee_payer(fee_payer, fee_amount);
  if (fee_payer_result != TransactionLoadResult::SUCCESS) {
    loaded_tx.load_result = fee_payer_result;
    return loaded_tx;
  }

  // Load all accounts
  loaded_tx.accounts.reserve(account_keys.size());
  loaded_tx.fee = fee_amount;

  for (size_t i = 0; i < account_keys.size(); ++i) {
    bool writable = i < is_writable.size() ? is_writable[i] : false;
    bool signer = i < is_signer.size() ? is_signer[i] : false;

    auto loaded_account =
        impl_->load_account_internal(account_keys[i], writable, signer);
    if (!loaded_account) {
      loaded_tx.load_result = TransactionLoadResult::ACCOUNT_NOT_FOUND;
      loaded_tx.error_message =
          "Account not found: " +
          std::string(account_keys[i].begin(), account_keys[i].end());
      return loaded_tx;
    }

    loaded_tx.accounts.push_back(*loaded_account);
    loaded_tx.loaded_accounts_data_size += loaded_account->loaded_size;
  }

  // Calculate rent for new accounts (simplified)
  loaded_tx.rent = 0;
  for (const auto &account : loaded_tx.accounts) {
    if (account.account.lamports == 0) {
      loaded_tx.rent +=
          impl_->callback_->calculate_rent(account.account.data.size());
    }
  }

  loaded_tx.load_result = TransactionLoadResult::SUCCESS;
  return loaded_tx;
}

std::optional<LoadedAccount>
AccountLoader::load_account(const PublicKey &address, bool is_writable,
                            bool is_signer) {
  return impl_->load_account_internal(address, is_writable, is_signer);
}

TransactionLoadResult
AccountLoader::validate_fee_payer(const PublicKey &fee_payer,
                                  Lamports fee_amount, Lamports rent_amount) {
  auto account_opt = impl_->callback_->get_account(fee_payer);
  if (!account_opt) {
    return TransactionLoadResult::ACCOUNT_NOT_FOUND;
  }

  // Check if fee payer has sufficient funds
  Lamports required_amount = fee_amount + rent_amount;
  if (account_opt->lamports < required_amount) {
    return TransactionLoadResult::INSUFFICIENT_FUNDS;
  }

  // Check if fee payer is a valid account (owned by system program)
  // For now, we accept any account as fee payer (simplified)

  return TransactionLoadResult::SUCCESS;
}

bool AccountLoader::is_account_accessible(const PublicKey &address) const {
  return impl_->callback_->account_exists(address);
}

AccountLoader::CacheStats AccountLoader::get_cache_stats() const {
  return impl_->stats_;
}

void AccountLoader::reset() {
  impl_->account_cache_.clear();
  impl_->accessed_accounts_.clear();
  impl_->stats_ = CacheStats{};
}

} // namespace svm
} // namespace slonana