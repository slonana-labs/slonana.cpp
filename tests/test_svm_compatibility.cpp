#include "svm/account_loader.h"
#include "svm/nonce_info.h"
#include "svm/rent_calculator.h"
#include "svm/transaction_balances.h"
#include "svm/transaction_error_metrics.h"
#include "test_framework.h"
#include <iostream>

using namespace slonana::svm;

// Mock callback for testing account loader
class MockAccountLoadingCallback : public AccountLoadingCallback {
private:
  std::unordered_map<PublicKey, AccountInfo> accounts_;
  Slot current_slot_ = 100;

public:
  void add_account(const PublicKey &address, const AccountInfo &account) {
    accounts_[address] = account;
  }

  std::optional<AccountInfo> get_account(const PublicKey &address) override {
    auto it = accounts_.find(address);
    if (it != accounts_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool account_exists(const PublicKey &address) override {
    return accounts_.find(address) != accounts_.end();
  }

  Slot get_slot() override { return current_slot_; }

  Lamports calculate_rent(size_t data_size) override {
    RentCalculator calc;
    return calc.calculate_rent(data_size);
  }
};

void test_account_loader() {
  MockAccountLoadingCallback callback;
  AccountLoader loader(&callback);

  // Create test accounts
  PublicKey addr1(32, 1), addr2(32, 2);

  AccountInfo account1;
  account1.pubkey = addr1;
  account1.lamports = 1000000;
  account1.data.resize(100);

  AccountInfo account2;
  account2.pubkey = addr2;
  account2.lamports = 500000;
  account2.data.resize(50);

  callback.add_account(addr1, account1);
  callback.add_account(addr2, account2);

  // Test loading single account
  auto loaded = loader.load_account(addr1, false, false);
  ASSERT_TRUE(loaded.has_value());
  ASSERT_EQ(loaded->account.lamports, 1000000);

  // Test loading transaction accounts
  std::vector<PublicKey> account_keys = {addr1, addr2};
  std::vector<bool> is_signer = {true, false};
  std::vector<bool> is_writable = {true, false};

  auto loaded_tx = loader.load_transaction_accounts(account_keys, is_signer,
                                                    is_writable, addr1, 5000);

  ASSERT_TRUE(loaded_tx.is_success());
  ASSERT_EQ(loaded_tx.accounts.size(), 2);

  std::cout << "✓ AccountLoader test passed" << std::endl;
}

void test_rent_calculator() {
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

  // Test rent collection
  auto collection = calc.collect_rent(min_balance - 1, 100, 200, 100);
  ASSERT_TRUE(collection.collected_rent > 0);

  std::cout << "✓ RentCalculator test passed" << std::endl;
}

void test_nonce_info() {
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

  PublicKey wrong_authority(32, 3);
  ASSERT_FALSE(nonce_info.is_valid_authority(wrong_authority));

  std::cout << "✓ NonceInfo test passed" << std::endl;
}

void test_transaction_balances() {
  TransactionBalances balances;

  // Create test accounts
  std::vector<PublicKey> addresses(3);
  addresses[0] = PublicKey(32, 1);
  addresses[1] = PublicKey(32, 2);
  addresses[2] = PublicKey(32, 3);

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
  ASSERT_EQ(changed.size(), 2); // Two accounts changed

  // Check if transaction is balanced (with fee)
  ASSERT_TRUE(balances.is_balanced(5000));

  std::cout << "✓ TransactionBalances test passed" << std::endl;
}

void test_transaction_error_metrics() {
  TransactionErrorMetrics metrics;

  // Test initial state
  ASSERT_EQ(metrics.total_errors(), 0);

  // Increment some errors
  metrics.account_not_found = 5;
  metrics.insufficient_funds = 3;
  metrics.instruction_error = 2;

  ASSERT_EQ(metrics.total_errors(), 10);

  // Test adding metrics
  TransactionErrorMetrics other;
  other.account_not_found = 2;
  other.program_error = 1;

  metrics.add(other);
  ASSERT_EQ(metrics.account_not_found, 7);
  ASSERT_EQ(metrics.program_error, 1);
  ASSERT_EQ(metrics.total_errors(), 13);

  // Test error rate calculation
  double rate = metrics.get_error_rate(100);
  ASSERT_EQ(rate, 0.13); // 13 errors out of 100 transactions

  std::cout << "✓ TransactionErrorMetrics test passed" << std::endl;
}

void run_svm_compatibility_tests(TestRunner &runner) {
  std::cout << "\n=== SVM Compatibility Test Suite ===" << std::endl;

  runner.run_test("Account Loader", test_account_loader);
  runner.run_test("Rent Calculator", test_rent_calculator);
  runner.run_test("Nonce Info", test_nonce_info);
  runner.run_test("Transaction Balances", test_transaction_balances);
  runner.run_test("Transaction Error Metrics", test_transaction_error_metrics);
}