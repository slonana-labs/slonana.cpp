#include "svm/rollback_accounts.h"
#include <sstream>
#include <algorithm>

namespace slonana {
namespace svm {

RollbackAccounts::RollbackAccounts(
    std::optional<NonceInfo> nonce_account,
    const PublicKey& fee_payer_address,
    const AccountInfo& fee_payer_rollback_account,
    Slot fee_payer_rent_epoch
) : nonce_account_(std::move(nonce_account)),
    fee_payer_address_(fee_payer_address),
    fee_payer_rollback_account_(fee_payer_rollback_account),
    fee_payer_rent_epoch_(fee_payer_rent_epoch) {
    validate_internal();
}

void RollbackAccounts::apply_rollback(std::unordered_map<PublicKey, ProgramAccount>& accounts) const {
    if (is_empty()) {
        return;
    }
    
    // Rollback fee payer account
    auto fee_payer_it = accounts.find(fee_payer_address_);
    if (fee_payer_it != accounts.end()) {
        // Convert AccountInfo back to ProgramAccount for rollback
        ProgramAccount& account = fee_payer_it->second;
        account.lamports = fee_payer_rollback_account_.lamports;
        account.data = fee_payer_rollback_account_.data;
        account.owner = fee_payer_rollback_account_.owner;
        account.executable = fee_payer_rollback_account_.executable;
        account.rent_epoch = fee_payer_rent_epoch_;
    }
    
    // Rollback nonce account if present
    if (nonce_account_) {
        const PublicKey& nonce_address = nonce_account_->get_address();
        auto nonce_it = accounts.find(nonce_address);
        if (nonce_it != accounts.end()) {
            const AccountInfo& nonce_rollback = nonce_account_->get_account();
            ProgramAccount& account = nonce_it->second;
            account.lamports = nonce_rollback.lamports;
            account.data = nonce_rollback.data;
            account.owner = nonce_rollback.owner;
            account.executable = nonce_rollback.executable;
            account.rent_epoch = nonce_rollback.rent_epoch;
        }
    }
}

RollbackAccounts RollbackAccounts::create_fee_only_rollback(
    const PublicKey& fee_payer_address,
    const AccountInfo& fee_payer_account,
    Lamports fee_amount
) {
    // Create a copy of the fee payer account with reduced balance
    AccountInfo rollback_account = fee_payer_account;
    if (rollback_account.lamports >= fee_amount) {
        rollback_account.lamports -= fee_amount;
    } else {
        rollback_account.lamports = 0;
    }
    
    return RollbackAccounts(
        std::nullopt, // No nonce account
        fee_payer_address,
        rollback_account,
        fee_payer_account.rent_epoch
    );
}

bool RollbackAccounts::validate() const {
    try {
        validate_internal();
        return true;
    } catch (...) {
        return false;
    }
}

std::string RollbackAccounts::get_summary() const {
    std::stringstream ss;
    ss << "RollbackAccounts{";
    ss << "fee_payer=" << std::string(fee_payer_address_.begin(), 
                                    std::min(fee_payer_address_.begin() + 8, fee_payer_address_.end())) << "...";
    ss << ", fee_payer_balance=" << fee_payer_rollback_account_.lamports;
    if (nonce_account_) {
        ss << ", nonce_account=" << std::string(nonce_account_->get_address().begin(),
                                               std::min(nonce_account_->get_address().begin() + 8, nonce_account_->get_address().end())) << "...";
    }
    ss << "}";
    return ss.str();
}

void RollbackAccounts::clear() {
    nonce_account_.reset();
    fee_payer_address_.clear();
    fee_payer_rollback_account_ = AccountInfo{};
    fee_payer_rent_epoch_ = 0;
}

bool RollbackAccounts::is_empty() const {
    return fee_payer_address_.empty() && !nonce_account_;
}

void RollbackAccounts::validate_internal() const {
    if (fee_payer_address_.empty()) {
        throw std::invalid_argument("Fee payer address cannot be empty");
    }
    
    if (fee_payer_address_.size() != 32) {
        throw std::invalid_argument("Fee payer address must be 32 bytes");
    }
    
    // Additional validation for nonce account if present
    if (nonce_account_) {
        if (!nonce_account_->validate()) {
            throw std::invalid_argument("Invalid nonce account in rollback");
        }
    }
}

// Utility functions
namespace rollback_utils {

RollbackAccounts create_rollback_from_transaction(
    const std::vector<PublicKey>& account_keys,
    const std::unordered_map<PublicKey, ProgramAccount>& original_accounts,
    const PublicKey& fee_payer,
    Lamports fee_amount,
    const std::optional<NonceInfo>& nonce_info
) {
    // Find fee payer account
    auto fee_payer_it = original_accounts.find(fee_payer);
    if (fee_payer_it == original_accounts.end()) {
        throw std::invalid_argument("Fee payer account not found in original accounts");
    }
    
    // Convert ProgramAccount to AccountInfo for rollback
    const ProgramAccount& fee_payer_account = fee_payer_it->second;
    AccountInfo fee_payer_rollback;
    fee_payer_rollback.pubkey = fee_payer_account.pubkey;
    fee_payer_rollback.lamports = fee_payer_account.lamports;
    fee_payer_rollback.data = fee_payer_account.data;
    fee_payer_rollback.owner = fee_payer_account.owner;
    fee_payer_rollback.executable = fee_payer_account.executable;
    fee_payer_rollback.rent_epoch = fee_payer_account.rent_epoch;
    fee_payer_rollback.is_signer = false; // Will be set appropriately during transaction
    fee_payer_rollback.is_writable = true; // Fee payer is always writable
    
    return RollbackAccounts(
        nonce_info,
        fee_payer,
        fee_payer_rollback,
        fee_payer_account.rent_epoch
    );
}

void apply_selective_rollback(
    const RollbackAccounts& rollback,
    std::unordered_map<PublicKey, ProgramAccount>& accounts,
    const std::vector<PublicKey>& accounts_to_rollback
) {
    if (rollback.is_empty()) {
        return;
    }
    
    // Check if fee payer should be rolled back
    const PublicKey& fee_payer = rollback.get_fee_payer_address();
    if (std::find(accounts_to_rollback.begin(), accounts_to_rollback.end(), fee_payer) != accounts_to_rollback.end()) {
        auto fee_payer_it = accounts.find(fee_payer);
        if (fee_payer_it != accounts.end()) {
            const AccountInfo& rollback_account = rollback.get_fee_payer_rollback_account();
            ProgramAccount& account = fee_payer_it->second;
            account.lamports = rollback_account.lamports;
            account.data = rollback_account.data;
            account.owner = rollback_account.owner;
            account.executable = rollback_account.executable;
            account.rent_epoch = rollback.get_fee_payer_rent_epoch();
        }
    }
    
    // Check if nonce account should be rolled back
    if (rollback.has_nonce_account()) {
        const NonceInfo& nonce_info = *rollback.get_nonce_account();
        const PublicKey& nonce_address = nonce_info.get_address();
        
        if (std::find(accounts_to_rollback.begin(), accounts_to_rollback.end(), nonce_address) != accounts_to_rollback.end()) {
            auto nonce_it = accounts.find(nonce_address);
            if (nonce_it != accounts.end()) {
                const AccountInfo& nonce_rollback = nonce_info.get_account();
                ProgramAccount& account = nonce_it->second;
                account.lamports = nonce_rollback.lamports;
                account.data = nonce_rollback.data;
                account.owner = nonce_rollback.owner;
                account.executable = nonce_rollback.executable;
                account.rent_epoch = nonce_rollback.rent_epoch;
            }
        }
    }
}

size_t calculate_rollback_size(const RollbackAccounts& rollback) {
    if (rollback.is_empty()) {
        return 0;
    }
    
    size_t size = 0;
    
    // Fee payer account size
    size += 32; // PublicKey
    size += rollback.get_fee_payer_rollback_account().data.size();
    size += sizeof(Lamports) + sizeof(Slot); // lamports + rent_epoch
    
    // Nonce account size (if present)
    if (rollback.has_nonce_account()) {
        const NonceInfo& nonce_info = *rollback.get_nonce_account();
        size += 32; // PublicKey
        size += nonce_info.get_account().data.size();
        size += sizeof(Lamports) + sizeof(Slot);
    }
    
    return size;
}

std::string format_rollback_debug(const RollbackAccounts& rollback) {
    if (rollback.is_empty()) {
        return "RollbackAccounts: EMPTY";
    }
    
    std::stringstream ss;
    ss << "RollbackAccounts Debug:\n";
    ss << "  Fee Payer: " << std::string(rollback.get_fee_payer_address().begin(),
                                        std::min(rollback.get_fee_payer_address().begin() + 16, rollback.get_fee_payer_address().end())) << "...\n";
    ss << "  Fee Payer Balance: " << rollback.get_fee_payer_rollback_account().lamports << "\n";
    ss << "  Fee Payer Data Size: " << rollback.get_fee_payer_rollback_account().data.size() << "\n";
    ss << "  Fee Payer Rent Epoch: " << rollback.get_fee_payer_rent_epoch() << "\n";
    
    if (rollback.has_nonce_account()) {
        const NonceInfo& nonce_info = *rollback.get_nonce_account();
        ss << "  Nonce Account: " << std::string(nonce_info.get_address().begin(),
                                                std::min(nonce_info.get_address().begin() + 16, nonce_info.get_address().end())) << "...\n";
        ss << "  Nonce State: " << (nonce_info.get_state() == NonceState::INITIALIZED ? "INITIALIZED" : "UNINITIALIZED") << "\n";
    } else {
        ss << "  Nonce Account: NONE\n";
    }
    
    ss << "  Total Rollback Size: " << calculate_rollback_size(rollback) << " bytes";
    
    return ss.str();
}

} // namespace rollback_utils

} // namespace svm
} // namespace slonana