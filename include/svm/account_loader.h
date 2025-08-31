#pragma once

#include "common/types.h"
#include "svm/engine.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <optional>
#include <functional>

namespace slonana {
namespace svm {

using namespace slonana::common;

/**
 * Loaded account data with metadata
 */
struct LoadedAccount {
    PublicKey address;
    AccountInfo account;
    size_t loaded_size;
    Slot slot;
    bool is_writable;
    bool is_signer;
    
    LoadedAccount() = default;
    LoadedAccount(const PublicKey& addr, const AccountInfo& acc, size_t size, Slot s, bool write, bool sign)
        : address(addr), account(acc), loaded_size(size), slot(s), is_writable(write), is_signer(sign) {}
};

/**
 * Transaction load result
 */
enum class TransactionLoadResult {
    SUCCESS,
    ACCOUNT_NOT_FOUND,
    INSUFFICIENT_FUNDS,
    INVALID_ACCOUNT_FOR_FEE,
    ACCOUNT_IN_USE,
    INVALID_PROGRAM_FOR_EXECUTION,
    INVALID_RENT_PAYING_ACCOUNT,
    WOULD_EXCEED_MAX_ACCOUNT_COST_LIMIT,
    WOULD_EXCEED_MAX_VOTE_COST_LIMIT,
    WOULD_EXCEED_MAX_ACCOUNT_DATA_COST_LIMIT,
    TOO_MANY_ACCOUNT_LOCKS,
    INVALID_ACCOUNT_INDEX,
    DUPLICATE_INSTRUCTION,
    INSUFFICIENT_FUNDS_FOR_RENT,
    MAX_LOADED_ACCOUNTS_DATA_SIZE_EXCEEDED,
    INVALID_LOADED_ACCOUNTS_DATA_SIZE_LIMIT,
    RESANITIZATION_NEEDED,
    PROGRAM_ACCOUNT_NOT_FOUND,
    MISSING_SIGNATURE_FOR_FEE,
    MIN_CONTEXT_SLOT_NOT_REACHED,
    ACCOUNT_SHRINKAGE,
    WOULD_EXCEED_ACCOUNT_DATA_BLOCK_LIMIT,
    UNSUPPORTED_VERSION,
    INVALID_WRITABLE_ACCOUNT,
    WOULD_EXCEED_ACCOUNT_DATA_TOTAL_LIMIT
};

/**
 * Loaded transaction with all required accounts and metadata
 */
struct LoadedTransaction {
    std::vector<LoadedAccount> accounts;
    Lamports fee;
    Lamports rent;
    size_t loaded_accounts_data_size;
    TransactionLoadResult load_result;
    std::string error_message;
    
    LoadedTransaction() : fee(0), rent(0), loaded_accounts_data_size(0), 
                         load_result(TransactionLoadResult::SUCCESS) {}
    
    bool is_success() const { return load_result == TransactionLoadResult::SUCCESS; }
};

/**
 * Account loading callback interface
 */
class AccountLoadingCallback {
public:
    virtual ~AccountLoadingCallback() = default;
    
    /**
     * Get account data by address
     */
    virtual std::optional<AccountInfo> get_account(const PublicKey& address) = 0;
    
    /**
     * Check if account exists
     */
    virtual bool account_exists(const PublicKey& address) = 0;
    
    /**
     * Get current slot
     */
    virtual Slot get_slot() = 0;
    
    /**
     * Get rent calculator
     */
    virtual Lamports calculate_rent(size_t data_size) = 0;
};

/**
 * Account loader for loading and validating transaction accounts
 * Equivalent to Agave's account_loader.rs
 */
class AccountLoader {
public:
    explicit AccountLoader(AccountLoadingCallback* callback);
    ~AccountLoader();
    
    /**
     * Load all accounts required for a transaction
     */
    LoadedTransaction load_transaction_accounts(
        const std::vector<PublicKey>& account_keys,
        const std::vector<bool>& is_signer,
        const std::vector<bool>& is_writable,
        const PublicKey& fee_payer,
        Lamports fee_amount,
        size_t max_loaded_accounts_data_size = SIZE_MAX
    );
    
    /**
     * Load a single account
     */
    std::optional<LoadedAccount> load_account(
        const PublicKey& address,
        bool is_writable = false,
        bool is_signer = false
    );
    
    /**
     * Validate fee payer account
     */
    TransactionLoadResult validate_fee_payer(
        const PublicKey& fee_payer,
        Lamports fee_amount,
        Lamports rent_amount = 0
    );
    
    /**
     * Check account accessibility
     */
    bool is_account_accessible(const PublicKey& address) const;
    
    /**
     * Get loaded accounts cache statistics
     */
    struct CacheStats {
        size_t hits = 0;
        size_t misses = 0;
        size_t total_loaded_size = 0;
    };
    CacheStats get_cache_stats() const;
    
    /**
     * Reset the loader state
     */
    void reset();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace svm
} // namespace slonana