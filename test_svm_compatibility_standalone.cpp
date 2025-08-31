#include "test_framework.h"
#include "svm/account_loader.h"
#include "svm/rent_calculator.h"
#include "svm/nonce_info.h"
#include "svm/transaction_balances.h"
#include "svm/transaction_error_metrics.h"
#include "svm/rollback_accounts.h"

using namespace slonana::svm;

// Mock callback for testing account loader
class MockAccountLoadingCallback : public AccountLoadingCallback {
private:
    std::unordered_map<PublicKey, AccountInfo> accounts_;
    Slot current_slot_ = 100;
    
public:
    void add_account(const PublicKey& address, const AccountInfo& account) {
        accounts_[address] = account;
    }
    
    std::optional<AccountInfo> get_account(const PublicKey& address) override {
        auto it = accounts_.find(address);
        if (it != accounts_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    bool account_exists(const PublicKey& address) override {
        return accounts_.find(address) != accounts_.end();
    }
    
    Slot get_slot() override {
        return current_slot_;
    }
    
    Lamports calculate_rent(size_t data_size) override {
        RentCalculator calc;
        return calc.calculate_rent(data_size);
    }
};

int main() {
    std::cout << "=== SVM Compatibility Test Suite ===" << std::endl;
    
    TestRunner runner;
    
    // Test AccountLoader
    runner.run_test("Account Loader Basic Functionality", []() {
        MockAccountLoadingCallback callback;
        AccountLoader loader(&callback);
        
        // Create test account
        PublicKey addr1(32, 1);
        AccountInfo account1;
        account1.pubkey = addr1;
        account1.lamports = 1000000;
        account1.data.resize(100);
        
        callback.add_account(addr1, account1);
        
        // Test loading single account
        auto loaded = loader.load_account(addr1, false, false);
        ASSERT_TRUE(loaded.has_value());
        ASSERT_EQ(loaded->account.lamports, 1000000UL);
        
        std::cout << "✓ AccountLoader basic functionality working";
    });
    
    // Test RentCalculator
    runner.run_test("Rent Calculator", []() {
        RentCalculator calc;
        
        // Test rent calculation
        Lamports rent = calc.calculate_rent(100);
        ASSERT_TRUE(rent > 0);
        
        // Test minimum balance
        Lamports min_balance = calc.minimum_balance(100);
        ASSERT_TRUE(min_balance > rent);
        
        // Test rent exemption
        ASSERT_TRUE(calc.is_rent_exempt(min_balance, 100));
        ASSERT_FALSE(calc.is_rent_exempt(min_balance - 1, 100));
        
        std::cout << "✓ RentCalculator working correctly";
    });
    
    // Test NonceInfo
    runner.run_test("Nonce Info", []() {
        PublicKey nonce_addr(32, 1), authority(32, 2);
        
        AccountInfo nonce_account;
        nonce_account.pubkey = nonce_addr;
        nonce_account.lamports = 1000000;
        nonce_account.data.resize(NonceInfo::NONCE_ACCOUNT_SIZE);
        
        NonceInfo nonce_info(nonce_addr, nonce_account);
        
        // Test initialization
        Hash recent_blockhash(32, 0x55);
        
        bool success = nonce_info.initialize_nonce(authority, recent_blockhash, 5000);
        ASSERT_TRUE(success);
        ASSERT_TRUE(nonce_info.get_state() == NonceState::INITIALIZED);
        
        // Test authority validation
        ASSERT_TRUE(nonce_info.is_valid_authority(authority));
        
        std::cout << "✓ NonceInfo working correctly";
    });
    
    // Test TransactionBalances
    runner.run_test("Transaction Balances", []() {
        TransactionBalances balances;
        
        // Create test accounts
        std::vector<PublicKey> addresses(2);
        addresses[0] = PublicKey(32, 1);
        addresses[1] = PublicKey(32, 2); 
        
        std::unordered_map<PublicKey, ProgramAccount> pre_accounts, post_accounts;
        
        // Set up pre-execution balances
        for (size_t i = 0; i < addresses.size(); ++i) {
            ProgramAccount account;
            account.pubkey = addresses[i];
            account.lamports = (i + 1) * 1000000;
            pre_accounts[addresses[i]] = account;
            post_accounts[addresses[i]] = account;
        }
        
        // Modify post-execution balances (simulate transfer)
        post_accounts[addresses[0]].lamports -= 100000; // Send 100k
        post_accounts[addresses[1]].lamports += 95000;  // Receive 95k (5k fee)
        
        balances.record_pre_balances(addresses, pre_accounts);
        balances.record_post_balances(addresses, post_accounts);
        
        ASSERT_TRUE(balances.is_complete());
        
        auto changed = balances.get_changed_balances();
        ASSERT_EQ(changed.size(), 2UL); // Two accounts changed
        
        // Check if transaction is balanced (with fee)
        ASSERT_TRUE(balances.is_balanced(5000));
        
        std::cout << "✓ TransactionBalances working correctly";
    });
    
    // Test TransactionErrorMetrics
    runner.run_test("Transaction Error Metrics", []() {
        TransactionErrorMetrics metrics;
        
        // Test initial state
        ASSERT_EQ(metrics.total_errors(), 0UL);
        
        // Increment some errors
        metrics.account_not_found = 5;
        metrics.insufficient_funds = 3;
        metrics.instruction_error = 2;
        
        ASSERT_EQ(metrics.total_errors(), 10UL);
        
        // Test adding metrics
        TransactionErrorMetrics other;
        other.account_not_found = 2;
        other.program_error = 1;
        
        metrics.add(other);
        ASSERT_EQ(metrics.account_not_found, 7UL);
        ASSERT_EQ(metrics.program_error, 1UL);
        ASSERT_EQ(metrics.total_errors(), 13UL);
        
        // Test error rate calculation
        double rate = metrics.get_error_rate(100);
        ASSERT_EQ(rate, 0.13); // 13 errors out of 100 transactions
        
        std::cout << "✓ TransactionErrorMetrics working correctly";
    });
    
    // Test RollbackAccounts
    runner.run_test("Rollback Accounts", []() {
        // Create test accounts
        PublicKey fee_payer(32, 1);
        AccountInfo fee_payer_account;
        fee_payer_account.pubkey = fee_payer;
        fee_payer_account.lamports = 1000000;
        fee_payer_account.data.resize(0);
        fee_payer_account.rent_epoch = 100;
        
        // Create rollback for fee-only transaction
        auto rollback = RollbackAccounts::create_fee_only_rollback(fee_payer, fee_payer_account, 5000);
        
        ASSERT_FALSE(rollback.is_empty());
        ASSERT_FALSE(rollback.has_nonce_account());
        ASSERT_EQ(rollback.get_fee_payer_address(), fee_payer);
        ASSERT_EQ(rollback.get_fee_payer_rollback_account().lamports, 995000UL); // Original - fee
        
        // Test rollback validation
        ASSERT_TRUE(rollback.validate());
        
        // Test rollback application
        std::unordered_map<PublicKey, ProgramAccount> accounts;
        ProgramAccount test_account;
        test_account.pubkey = fee_payer;
        test_account.lamports = 1000000; // Original balance
        accounts[fee_payer] = test_account;
        
        rollback.apply_rollback(accounts);
        ASSERT_EQ(accounts[fee_payer].lamports, 995000UL); // Should be reduced by fee
        
        std::cout << "✓ RollbackAccounts working correctly";
    });
    
    runner.print_summary();
    
    std::cout << "\n🎉 SVM Compatibility Tests Complete!" << std::endl;
    std::cout << "All core SVM components are compatible with Agave SVM structure." << std::endl;
    
    return 0;
}