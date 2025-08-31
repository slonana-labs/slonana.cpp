#pragma once

#include "common/types.h"
#include "svm/engine.h"
#include <string>
#include <optional>

namespace slonana {
namespace svm {

using namespace slonana::common;

/**
 * Nonce account state
 */
enum class NonceState {
    UNINITIALIZED,
    INITIALIZED
};

/**
 * Nonce account data structure
 */
struct NonceData {
    PublicKey authority;         // Authority that can advance the nonce
    Hash blockhash;             // Current blockhash stored in nonce
    Lamports fee_calculator;    // Fee calculator lamports per signature
    
    NonceData() = default;
    NonceData(const PublicKey& auth, const Hash& hash, Lamports fee)
        : authority(auth), blockhash(hash), fee_calculator(fee) {}
    
    std::vector<uint8_t> serialize() const;
    static NonceData deserialize(const std::vector<uint8_t>& data);
};

/**
 * Complete nonce account information
 * Equivalent to Agave's nonce_info.rs
 */
class NonceInfo {
public:
    NonceInfo() = default;
    NonceInfo(const PublicKey& address, const AccountInfo& account);
    ~NonceInfo() = default;
    
    /**
     * Get nonce account address
     */
    const PublicKey& get_address() const { return address_; }
    
    /**
     * Get nonce account data
     */
    const AccountInfo& get_account() const { return account_; }
    
    /**
     * Get nonce state
     */
    NonceState get_state() const { return state_; }
    
    /**
     * Get nonce data (only valid if state is INITIALIZED)
     */
    const std::optional<NonceData>& get_nonce_data() const { return nonce_data_; }
    
    /**
     * Check if nonce is valid for given authority
     */
    bool is_valid_authority(const PublicKey& authority) const;
    
    /**
     * Check if nonce can be advanced with given blockhash
     */
    bool can_advance_nonce(const Hash& new_blockhash) const;
    
    /**
     * Advance the nonce with new blockhash
     */
    bool advance_nonce(const Hash& new_blockhash, Lamports fee_calculator);
    
    /**
     * Initialize nonce account
     */
    bool initialize_nonce(const PublicKey& authority, const Hash& recent_blockhash, Lamports fee_calculator);
    
    /**
     * Authorize new authority for nonce account
     */
    bool authorize_nonce(const PublicKey& current_authority, const PublicKey& new_authority);
    
    /**
     * Withdraw from nonce account
     */
    bool withdraw_nonce(const PublicKey& authority, Lamports amount, Lamports remaining_balance);
    
    /**
     * Validate nonce account structure
     */
    bool validate() const;
    
    /**
     * Get updated account data after modifications
     */
    AccountInfo get_updated_account() const;
    
    /**
     * Check if account is a valid nonce account
     */
    static bool is_nonce_account(const AccountInfo& account);
    
    /**
     * Parse nonce account data
     */
    static std::optional<NonceData> parse_nonce_data(const std::vector<uint8_t>& data);
    
    /**
     * Create nonce account data
     */
    static std::vector<uint8_t> create_nonce_account_data(const NonceData& nonce_data);
    
    /**
     * Minimum balance required for nonce account
     */
    static constexpr size_t NONCE_ACCOUNT_SIZE = 80; // Size of nonce account data
    
private:
    PublicKey address_;
    AccountInfo account_;
    NonceState state_ = NonceState::UNINITIALIZED;
    std::optional<NonceData> nonce_data_;
    bool modified_ = false;
    
    void parse_account_data();
    void update_account_data();
};

/**
 * Nonce validation result
 */
enum class NonceValidationResult {
    SUCCESS,
    ACCOUNT_NOT_FOUND,
    INVALID_ACCOUNT_OWNER,
    INVALID_ACCOUNT_DATA,
    NONCE_NOT_INITIALIZED,
    INVALID_AUTHORITY,
    BLOCKHASH_NOT_EXPIRED,
    INSUFFICIENT_FUNDS_FOR_FEE
};

/**
 * Nonce utilities
 */
namespace nonce_utils {
    /**
     * Validate nonce for transaction
     */
    NonceValidationResult validate_nonce(
        const NonceInfo& nonce_info,
        const PublicKey& authority,
        const Hash& current_blockhash
    );
    
    /**
     * Get system program ID for nonce accounts
     */
    PublicKey get_system_program_id();
    
    /**
     * Calculate minimum balance for nonce account
     */
    Lamports calculate_nonce_minimum_balance();
    
    /**
     * Format nonce validation result
     */
    std::string format_validation_result(NonceValidationResult result);
}

} // namespace svm
} // namespace slonana