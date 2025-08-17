#include "test_framework.h"
#include "staking/manager.h"
#include "svm/engine.h"
#include <memory>

void test_staking_manager_initialization() {
    auto staking = std::make_unique<slonana::staking::StakingManager>();
    
    // Should initialize without errors
    ASSERT_TRUE(true);
}

void test_validator_registration() {
    auto staking = std::make_unique<slonana::staking::StakingManager>();
    
    slonana::PublicKey validator_id(32, 0x01);
    auto register_result = staking->register_validator(validator_id, 500); // 5% commission
    
    ASSERT_TRUE(register_result.is_ok());
    ASSERT_TRUE(staking->is_validator_registered(validator_id));
}

void test_multiple_validator_registration() {
    auto staking = std::make_unique<slonana::staking::StakingManager>();
    
    for (int i = 1; i <= 5; ++i) {
        slonana::PublicKey validator_id(32, static_cast<uint8_t>(i));
        auto register_result = staking->register_validator(validator_id, 500 + i * 10);
        
        ASSERT_TRUE(register_result.is_ok());
        ASSERT_TRUE(staking->is_validator_registered(validator_id));
    }
}

void test_stake_account_creation() {
    auto staking = std::make_unique<slonana::staking::StakingManager>();
    
    slonana::staking::StakeAccount stake_account;
    stake_account.stake_pubkey.resize(32, 0x02);
    stake_account.delegator_pubkey.resize(32, 0x03);
    stake_account.stake_amount = 1000000; // 1M lamports
    
    auto create_result = staking->create_stake_account(stake_account);
    ASSERT_TRUE(create_result.is_ok());
    
    auto retrieved_account = staking->get_stake_account(stake_account.stake_pubkey);
    ASSERT_TRUE(retrieved_account.has_value());
    ASSERT_EQ(1000000, retrieved_account->stake_amount);
}

void test_stake_account_delegation() {
    auto staking = std::make_unique<slonana::staking::StakingManager>();
    
    // First register a validator
    slonana::PublicKey validator_id(32, 0x01);
    auto register_result = staking->register_validator(validator_id, 500);
    ASSERT_TRUE(register_result.is_ok());
    
    // Create stake account
    slonana::staking::StakeAccount stake_account;
    stake_account.stake_pubkey.resize(32, 0x02);
    stake_account.delegator_pubkey.resize(32, 0x03);
    stake_account.stake_amount = 2000000; // 2M lamports
    
    auto create_result = staking->create_stake_account(stake_account);
    ASSERT_TRUE(create_result.is_ok());
    
    // Delegate stake
    auto delegate_result = staking->delegate_stake(stake_account.stake_pubkey, validator_id);
    ASSERT_TRUE(delegate_result.is_ok());
    
    auto retrieved_account = staking->get_stake_account(stake_account.stake_pubkey);
    ASSERT_TRUE(retrieved_account.has_value());
    ASSERT_EQ(validator_id, retrieved_account->validator_pubkey);
}

void test_stake_account_undelegation() {
    auto staking = std::make_unique<slonana::staking::StakingManager>();
    
    // Register validator and create delegated stake
    slonana::PublicKey validator_id(32, 0x01);
    staking->register_validator(validator_id, 500);
    
    slonana::staking::StakeAccount stake_account;
    stake_account.stake_pubkey.resize(32, 0x02);
    stake_account.delegator_pubkey.resize(32, 0x03);
    stake_account.stake_amount = 1500000;
    
    staking->create_stake_account(stake_account);
    staking->delegate_stake(stake_account.stake_pubkey, validator_id);
    
    // Undelegate
    auto undelegate_result = staking->undelegate_stake(stake_account.stake_pubkey);
    ASSERT_TRUE(undelegate_result.is_ok());
    
    auto retrieved_account = staking->get_stake_account(stake_account.stake_pubkey);
    ASSERT_TRUE(retrieved_account.has_value());
    // Validator pubkey should be cleared (all zeros)
    slonana::PublicKey empty_key(32, 0x00);
    ASSERT_EQ(empty_key, retrieved_account->validator_pubkey);
}

void test_staking_rewards_calculation() {
    auto staking = std::make_unique<slonana::staking::StakingManager>();
    
    slonana::PublicKey validator_id(32, 0x01);
    staking->register_validator(validator_id, 500); // 5% commission
    
    slonana::staking::StakeAccount stake_account;
    stake_account.stake_pubkey.resize(32, 0x02);
    stake_account.delegator_pubkey.resize(32, 0x03);
    stake_account.stake_amount = 10000000; // 10M lamports
    
    staking->create_stake_account(stake_account);
    staking->delegate_stake(stake_account.stake_pubkey, validator_id);
    
    // Calculate rewards (mock calculation)
    uint64_t epoch_rewards = 100000; // 100K lamports
    auto reward_result = staking->calculate_staking_rewards(stake_account.stake_pubkey, epoch_rewards);
    
    ASSERT_TRUE(reward_result.is_ok());
    ASSERT_GT(reward_result.value(), 0);
}

void test_svm_execution_engine_initialization() {
    auto execution_engine = std::make_unique<slonana::svm::ExecutionEngine>();
    
    // Should initialize without errors
    ASSERT_TRUE(true);
}

void test_account_manager_initialization() {
    auto account_manager = std::make_unique<slonana::svm::AccountManager>();
    
    // Should initialize without errors
    ASSERT_TRUE(true);
}

void test_program_account_creation() {
    auto account_manager = std::make_unique<slonana::svm::AccountManager>();
    
    slonana::svm::ProgramAccount test_account;
    test_account.program_id.resize(32, 0x01);
    test_account.lamports = 1000000;
    test_account.executable = false;
    test_account.owner.resize(32, 0x00); // System program
    
    auto create_result = account_manager->create_account(test_account);
    ASSERT_TRUE(create_result.is_ok());
    ASSERT_TRUE(account_manager->account_exists(test_account.program_id));
}

void test_multiple_account_creation() {
    auto account_manager = std::make_unique<slonana::svm::AccountManager>();
    
    for (int i = 1; i <= 10; ++i) {
        slonana::svm::ProgramAccount account;
        account.program_id.resize(32, static_cast<uint8_t>(i));
        account.lamports = 1000000 * i;
        account.executable = (i % 2 == 0); // Every other account is executable
        account.owner.resize(32, 0x00);
        
        auto create_result = account_manager->create_account(account);
        ASSERT_TRUE(create_result.is_ok());
        ASSERT_TRUE(account_manager->account_exists(account.program_id));
    }
}

void test_account_balance_operations() {
    auto account_manager = std::make_unique<slonana::svm::AccountManager>();
    
    slonana::svm::ProgramAccount account;
    account.program_id.resize(32, 0x01);
    account.lamports = 2000000;
    account.executable = false;
    account.owner.resize(32, 0x00);
    
    account_manager->create_account(account);
    
    // Test balance retrieval
    auto balance = account_manager->get_account_balance(account.program_id);
    ASSERT_TRUE(balance.has_value());
    ASSERT_EQ(2000000, balance.value());
    
    // Test balance update
    auto update_result = account_manager->update_account_balance(account.program_id, 3000000);
    ASSERT_TRUE(update_result.is_ok());
    
    auto new_balance = account_manager->get_account_balance(account.program_id);
    ASSERT_TRUE(new_balance.has_value());
    ASSERT_EQ(3000000, new_balance.value());
}

void test_account_data_operations() {
    auto account_manager = std::make_unique<slonana::svm::AccountManager>();
    
    slonana::svm::ProgramAccount account;
    account.program_id.resize(32, 0x01);
    account.lamports = 1000000;
    account.data = {0x01, 0x02, 0x03, 0x04};
    account.executable = false;
    account.owner.resize(32, 0x00);
    
    account_manager->create_account(account);
    
    // Test data retrieval
    auto retrieved_account = account_manager->get_account_info(account.program_id);
    ASSERT_TRUE(retrieved_account.has_value());
    ASSERT_EQ(account.data, retrieved_account->data);
    
    // Test data update
    std::vector<uint8_t> new_data = {0x05, 0x06, 0x07, 0x08, 0x09};
    auto update_result = account_manager->update_account_data(account.program_id, new_data);
    ASSERT_TRUE(update_result.is_ok());
    
    auto updated_account = account_manager->get_account_info(account.program_id);
    ASSERT_TRUE(updated_account.has_value());
    ASSERT_EQ(new_data, updated_account->data);
}

void test_instruction_execution() {
    auto execution_engine = std::make_unique<slonana::svm::ExecutionEngine>();
    auto account_manager = std::make_unique<slonana::svm::AccountManager>();
    
    // Create test accounts
    slonana::svm::ProgramAccount source_account;
    source_account.program_id.resize(32, 0x01);
    source_account.lamports = 5000000;
    source_account.executable = false;
    source_account.owner.resize(32, 0x00);
    
    slonana::svm::ProgramAccount dest_account;
    dest_account.program_id.resize(32, 0x02);
    dest_account.lamports = 1000000;
    dest_account.executable = false;
    dest_account.owner.resize(32, 0x00);
    
    account_manager->create_account(source_account);
    account_manager->create_account(dest_account);
    
    // Create transfer instruction
    slonana::svm::Instruction instruction;
    instruction.program_id.resize(32, 0x00); // System program
    instruction.data = {0}; // Transfer instruction
    instruction.accounts = {source_account.program_id, dest_account.program_id};
    
    std::unordered_map<slonana::PublicKey, slonana::svm::ProgramAccount> accounts;
    accounts[source_account.program_id] = source_account;
    accounts[dest_account.program_id] = dest_account;
    
    auto outcome = execution_engine->execute_transaction({instruction}, accounts);
    ASSERT_TRUE(outcome.is_success());
}

void test_account_changes_commit() {
    auto account_manager = std::make_unique<slonana::svm::AccountManager>();
    
    slonana::svm::ProgramAccount account;
    account.program_id.resize(32, 0x01);
    account.lamports = 1000000;
    account.executable = false;
    account.owner.resize(32, 0x00);
    
    account_manager->create_account(account);
    
    auto commit_result = account_manager->commit_changes();
    ASSERT_TRUE(commit_result.is_ok());
    
    // Account should still exist after commit
    ASSERT_TRUE(account_manager->account_exists(account.program_id));
}

void run_consensus_tests(TestRunner& runner) {
    std::cout << "\n=== Consensus Tests ===" << std::endl;
    
    runner.run_test("Staking Manager Initialization", test_staking_manager_initialization);
    runner.run_test("Validator Registration", test_validator_registration);
    runner.run_test("Multiple Validator Registration", test_multiple_validator_registration);
    runner.run_test("Stake Account Creation", test_stake_account_creation);
    runner.run_test("Stake Account Delegation", test_stake_account_delegation);
    runner.run_test("Stake Account Undelegation", test_stake_account_undelegation);
    runner.run_test("Staking Rewards Calculation", test_staking_rewards_calculation);
    runner.run_test("SVM Execution Engine Initialization", test_svm_execution_engine_initialization);
    runner.run_test("Account Manager Initialization", test_account_manager_initialization);
    runner.run_test("Program Account Creation", test_program_account_creation);
    runner.run_test("Multiple Account Creation", test_multiple_account_creation);
    runner.run_test("Account Balance Operations", test_account_balance_operations);
    runner.run_test("Account Data Operations", test_account_data_operations);
    runner.run_test("Instruction Execution", test_instruction_execution);
    runner.run_test("Account Changes Commit", test_account_changes_commit);
}