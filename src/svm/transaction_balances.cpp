#include "svm/transaction_balances.h"
#include <algorithm>
#include <sstream>
#include <cmath>
#include <set>

namespace slonana {
namespace svm {

// TransactionBalances implementation
void TransactionBalances::record_pre_balances(const std::vector<PublicKey>& addresses,
                                            const std::unordered_map<PublicKey, ProgramAccount>& accounts) {
    pre_balances_.clear();
    for (const auto& address : addresses) {
        Lamports balance = get_account_balance(address, accounts);
        pre_balances_[address] = balance;
    }
    
    if (has_post_balances()) {
        update_balances();
    }
}

void TransactionBalances::record_post_balances(const std::vector<PublicKey>& addresses,
                                             const std::unordered_map<PublicKey, ProgramAccount>& accounts) {
    post_balances_.clear();
    for (const auto& address : addresses) {
        Lamports balance = get_account_balance(address, accounts);
        post_balances_[address] = balance;
    }
    
    if (has_pre_balances()) {
        update_balances();
    }
}

std::optional<AccountBalance> TransactionBalances::get_balance(const PublicKey& address) const {
    auto it = std::find_if(balances_.begin(), balances_.end(),
                          [&address](const AccountBalance& balance) {
                              return balance.address == address;
                          });
    
    if (it != balances_.end()) {
        return *it;
    }
    return std::nullopt;
}

Lamports TransactionBalances::get_total_balance_change() const {
    Lamports total = 0;
    for (const auto& balance : balances_) {
        total += balance.balance_change();
    }
    return total;
}

std::vector<AccountBalance> TransactionBalances::get_changed_balances() const {
    std::vector<AccountBalance> changed;
    for (const auto& balance : balances_) {
        if (balance.has_changed()) {
            changed.push_back(balance);
        }
    }
    return changed;
}

bool TransactionBalances::is_balanced(Lamports fee_amount) const {
    Lamports total_change = get_total_balance_change();
    // Transaction should be balanced except for the fee amount
    return std::abs(static_cast<int64_t>(total_change + fee_amount)) == 0;
}

TransactionBalances::BalanceStats TransactionBalances::get_statistics() const {
    BalanceStats stats;
    stats.total_accounts = balances_.size();
    
    for (const auto& balance : balances_) {
        if (balance.has_changed()) {
            stats.changed_accounts++;
        }
        
        Lamports change = balance.balance_change();
        stats.total_change += change;
        
        if (change > 0) {
            stats.max_increase = std::max(stats.max_increase, change);
        } else if (change < 0) {
            stats.max_decrease = std::max(stats.max_decrease, static_cast<Lamports>(-change));
        }
    }
    
    return stats;
}

void TransactionBalances::reset() {
    pre_balances_.clear();
    post_balances_.clear();
    balances_.clear();
}

void TransactionBalances::update_balances() {
    balances_.clear();
    
    // Combine all addresses from pre and post balances
    std::set<PublicKey> all_addresses;
    for (const auto& pair : pre_balances_) {
        all_addresses.insert(pair.first);
    }
    for (const auto& pair : post_balances_) {
        all_addresses.insert(pair.first);
    }
    
    // Create balance entries
    for (const auto& address : all_addresses) {
        Lamports pre_balance = 0;
        Lamports post_balance = 0;
        
        auto pre_it = pre_balances_.find(address);
        if (pre_it != pre_balances_.end()) {
            pre_balance = pre_it->second;
        }
        
        auto post_it = post_balances_.find(address);
        if (post_it != post_balances_.end()) {
            post_balance = post_it->second;
        }
        
        balances_.emplace_back(address, pre_balance, post_balance);
    }
}

Lamports TransactionBalances::get_account_balance(const PublicKey& address,
                                                const std::unordered_map<PublicKey, ProgramAccount>& accounts) const {
    auto it = accounts.find(address);
    if (it != accounts.end()) {
        return it->second.lamports;
    }
    return 0; // Account not found, assume zero balance
}

// BalanceCollector implementation
void BalanceCollector::start_transaction() {
    if (transaction_in_progress_) {
        // Finish previous transaction if not completed
        finish_transaction();
    }
    
    current_transaction_.reset();
    transaction_in_progress_ = true;
}

void BalanceCollector::finish_transaction() {
    if (transaction_in_progress_) {
        all_balances_.push_back(current_transaction_);
        current_transaction_.reset();
        transaction_in_progress_ = false;
    }
}

void BalanceCollector::collect_pre_balances(const std::vector<PublicKey>& addresses,
                                          const std::unordered_map<PublicKey, ProgramAccount>& accounts) {
    if (!transaction_in_progress_) {
        start_transaction();
    }
    
    current_transaction_.record_pre_balances(addresses, accounts);
}

void BalanceCollector::collect_post_balances(const std::vector<PublicKey>& addresses,
                                           const std::unordered_map<PublicKey, ProgramAccount>& accounts) {
    if (!transaction_in_progress_) {
        return; // No transaction in progress
    }
    
    current_transaction_.record_post_balances(addresses, accounts);
}

std::optional<TransactionBalances> BalanceCollector::get_transaction_balances(size_t transaction_index) const {
    if (transaction_index < all_balances_.size()) {
        return all_balances_[transaction_index];
    }
    return std::nullopt;
}

bool BalanceCollector::has_unbalanced_transactions(const std::vector<Lamports>& fees) const {
    for (size_t i = 0; i < all_balances_.size(); ++i) {
        Lamports fee = (i < fees.size()) ? fees[i] : 0;
        if (!all_balances_[i].is_balanced(fee)) {
            return true;
        }
    }
    return false;
}

BalanceCollector::CollectionStats BalanceCollector::get_collection_statistics(const std::vector<Lamports>& fees) const {
    CollectionStats stats;
    stats.total_transactions = all_balances_.size();
    
    std::set<PublicKey> affected_accounts;
    
    for (size_t i = 0; i < all_balances_.size(); ++i) {
        const auto& balances = all_balances_[i];
        
        Lamports fee = (i < fees.size()) ? fees[i] : 0;
        if (balances.is_balanced(fee)) {
            stats.balanced_transactions++;
        }
        
        auto changed = balances.get_changed_balances();
        for (const auto& balance : changed) {
            affected_accounts.insert(balance.address);
            stats.total_balance_changes += std::abs(static_cast<int64_t>(balance.balance_change()));
        }
    }
    
    stats.total_accounts_affected = affected_accounts.size();
    return stats;
}

void BalanceCollector::reset() {
    all_balances_.clear();
    current_transaction_.reset();
    transaction_in_progress_ = false;
}

// Utility functions
namespace balance_utils {

bool validate_balance_conservation(const TransactionBalances& balances, Lamports fee_amount) {
    return balances.is_balanced(fee_amount);
}

Lamports calculate_net_change(const TransactionBalances& balances) {
    return balances.get_total_balance_change();
}

std::vector<AccountBalance> find_largest_changes(const TransactionBalances& balances, size_t limit) {
    auto changed = balances.get_changed_balances();
    
    // Sort by absolute change amount
    std::sort(changed.begin(), changed.end(),
              [](const AccountBalance& a, const AccountBalance& b) {
                  return std::abs(static_cast<int64_t>(a.balance_change())) > 
                         std::abs(static_cast<int64_t>(b.balance_change()));
              });
    
    if (changed.size() > limit) {
        changed.resize(limit);
    }
    
    return changed;
}

std::string format_balance_changes(const TransactionBalances& balances) {
    std::stringstream ss;
    auto changed = balances.get_changed_balances();
    
    ss << "Balance Changes [" << changed.size() << " accounts]:\n";
    for (const auto& balance : changed) {
        ss << "  Account: " << std::string(balance.address.begin(), balance.address.begin() + 8) << "..."
           << " Change: " << static_cast<int64_t>(balance.balance_change()) << " lamports"
           << " (" << balance.pre_balance << " -> " << balance.post_balance << ")\n";
    }
    
    auto stats = balances.get_statistics();
    ss << "Total change: " << static_cast<int64_t>(stats.total_change) << " lamports";
    
    return ss.str();
}

std::vector<BalanceAlert> check_for_suspicious_changes(const TransactionBalances& balances) {
    std::vector<BalanceAlert> alerts;
    
    // Define thresholds for suspicious activity
    constexpr Lamports LARGE_DECREASE_THRESHOLD = 1000000000; // 1 SOL
    constexpr Lamports LARGE_INCREASE_THRESHOLD = 1000000000; // 1 SOL
    
    for (const auto& balance : balances.get_changed_balances()) {
        Lamports change = balance.balance_change();
        
        if (change < -static_cast<int64_t>(LARGE_DECREASE_THRESHOLD)) {
            alerts.push_back({
                balance.address,
                change,
                "Large balance decrease detected"
            });
        } else if (change > static_cast<int64_t>(LARGE_INCREASE_THRESHOLD)) {
            alerts.push_back({
                balance.address,
                change,
                "Large balance increase detected"
            });
        }
        
        // Check for complete balance drain
        if (balance.pre_balance > 0 && balance.post_balance == 0) {
            alerts.push_back({
                balance.address,
                change,
                "Account completely drained"
            });
        }
    }
    
    return alerts;
}

} // namespace balance_utils

} // namespace svm
} // namespace slonana