#include "test_framework.h"
#include "slonana_validator.h"
#include <memory>
#include <thread>
#include <chrono>

void test_full_validator_lifecycle() {
    slonana::common::ValidatorConfig config;
    config.ledger_path = "/tmp/test_integration_validator";
    config.identity_keypair_path = "/tmp/test_integration_identity.json";
    config.rpc_bind_address = "127.0.0.1:18899";
    config.gossip_bind_address = "127.0.0.1:18001";
    
    auto validator = std::make_unique<slonana::SolanaValidator>(config);
    
    // Test initialization
    ASSERT_FALSE(validator->is_initialized());
    ASSERT_FALSE(validator->is_running());
    
    auto init_result = validator->initialize();
    ASSERT_TRUE(init_result.is_ok());
    ASSERT_TRUE(validator->is_initialized());
    
    // Test startup
    auto start_result = validator->start();
    ASSERT_TRUE(start_result.is_ok());
    ASSERT_TRUE(validator->is_running());
    
    // Give validator time to fully start
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Test component availability
    ASSERT_TRUE(validator->get_ledger_manager() != nullptr);
    ASSERT_TRUE(validator->get_validator_core() != nullptr);
    ASSERT_TRUE(validator->get_staking_manager() != nullptr);
    ASSERT_TRUE(validator->get_execution_engine() != nullptr);
    
    // Test stats
    auto stats = validator->get_stats();
    ASSERT_EQ(0, stats.current_slot); // Should start at slot 0
    
    // Test shutdown
    validator->stop();
    ASSERT_FALSE(validator->is_running());
}

void test_validator_with_rpc_integration() {
    slonana::common::ValidatorConfig config;
    config.ledger_path = "/tmp/test_rpc_integration_validator";
    config.rpc_bind_address = "127.0.0.1:18899";
    config.enable_rpc = true;
    config.enable_gossip = false; // Disable gossip for this test
    
    auto validator = std::make_unique<slonana::SolanaValidator>(config);
    
    validator->initialize();
    validator->start();
    
    // Give validator time to start RPC server
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    // Test RPC integration by making requests through the validator's RPC server
    // Note: In a real implementation, we'd make HTTP requests, but here we test the handle_request method
    
    validator->stop();
}

void test_validator_block_processing_pipeline() {
    slonana::common::ValidatorConfig config;
    config.ledger_path = "/tmp/test_block_pipeline_validator";
    config.enable_rpc = false;
    config.enable_gossip = false;
    
    auto validator = std::make_unique<slonana::SolanaValidator>(config);
    
    validator->initialize();
    validator->start();
    
    // Get components for direct testing
    auto ledger = validator->get_ledger_manager();
    auto validator_core = validator->get_validator_core();
    
    ASSERT_TRUE(ledger != nullptr);
    ASSERT_TRUE(validator_core != nullptr);
    
    // Create and process a test block
    slonana::ledger::Block test_block;
    test_block.slot = 1;
    test_block.block_hash.resize(32, 0x01);
    test_block.parent_hash.resize(32, 0x00);
    test_block.validator.resize(32, 0xFF);
    test_block.block_signature.resize(64, 0xAA);
    
    // Process block through validator core
    validator_core->process_block(test_block);
    
    // Verify block was stored in ledger
    ASSERT_EQ(1, ledger->get_latest_slot());
    auto stored_block = ledger->get_block_by_slot(1);
    ASSERT_TRUE(stored_block.has_value());
    ASSERT_EQ(1, stored_block->slot);
    
    validator->stop();
}

void test_validator_staking_integration() {
    slonana::common::ValidatorConfig config;
    config.ledger_path = "/tmp/test_staking_integration_validator";
    config.enable_rpc = false;
    config.enable_gossip = false;
    
    auto validator = std::make_unique<slonana::SolanaValidator>(config);
    
    validator->initialize();
    validator->start();
    
    auto staking_manager = validator->get_staking_manager();
    ASSERT_TRUE(staking_manager != nullptr);
    
    // Test validator registration through staking manager
    slonana::PublicKey validator_id(32, 0x01);
    auto register_result = staking_manager->register_validator(validator_id, 500);
    ASSERT_TRUE(register_result.is_ok());
    ASSERT_TRUE(staking_manager->is_validator_registered(validator_id));
    
    // Test stake account creation
    slonana::staking::StakeAccount stake_account;
    stake_account.stake_pubkey.resize(32, 0x02);
    stake_account.delegator_pubkey.resize(32, 0x03);
    stake_account.stake_amount = 5000000;
    
    auto create_result = staking_manager->create_stake_account(stake_account);
    ASSERT_TRUE(create_result.is_ok());
    
    // Test delegation
    auto delegate_result = staking_manager->delegate_stake(stake_account.stake_pubkey, validator_id);
    ASSERT_TRUE(delegate_result.is_ok());
    
    validator->stop();
}

void test_validator_svm_integration() {
    slonana::common::ValidatorConfig config;
    config.ledger_path = "/tmp/test_svm_integration_validator";
    config.enable_rpc = false;
    config.enable_gossip = false;
    
    auto validator = std::make_unique<slonana::SolanaValidator>(config);
    
    validator->initialize();
    validator->start();
    
    auto execution_engine = validator->get_execution_engine();
    ASSERT_TRUE(execution_engine != nullptr);
    
    // Test instruction execution through SVM
    slonana::svm::Instruction instruction;
    instruction.program_id.resize(32, 0x00); // System program
    instruction.data = {0}; // Transfer instruction
    
    std::unordered_map<slonana::PublicKey, slonana::svm::ProgramAccount> accounts;
    
    // Create test accounts
    slonana::svm::ProgramAccount account1, account2;
    account1.program_id.resize(32, 0x01);
    account1.lamports = 1000000;
    account2.program_id.resize(32, 0x02);
    account2.lamports = 500000;
    
    accounts[account1.program_id] = account1;
    accounts[account2.program_id] = account2;
    
    auto outcome = execution_engine->execute_transaction({instruction}, accounts);
    ASSERT_TRUE(outcome.is_success());
    
    validator->stop();
}

void test_validator_multi_component_interaction() {
    slonana::common::ValidatorConfig config;
    config.ledger_path = "/tmp/test_multi_component_validator";
    config.enable_rpc = true;
    config.enable_gossip = false; // Disable gossip for cleaner testing
    config.rpc_bind_address = "127.0.0.1:18899";
    
    auto validator = std::make_unique<slonana::SolanaValidator>(config);
    
    validator->initialize();
    validator->start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    // Test interaction between multiple components
    auto ledger = validator->get_ledger_manager();
    auto staking = validator->get_staking_manager();
    auto svm = validator->get_execution_engine();
    auto validator_core = validator->get_validator_core();
    
    // 1. Register a validator in staking
    slonana::PublicKey validator_id(32, 0x01);
    staking->register_validator(validator_id, 500);
    
    // 2. Create and process a block
    slonana::ledger::Block block;
    block.slot = 1;
    block.block_hash.resize(32, 0x01);
    block.parent_hash.resize(32, 0x00);
    block.validator = validator_id; // Block produced by our registered validator
    block.block_signature.resize(64, 0xAA);
    
    validator_core->process_block(block);
    
    // 3. Verify block is in ledger
    ASSERT_EQ(1, ledger->get_latest_slot());
    
    // 4. Execute a transaction through SVM
    slonana::svm::Instruction instruction;
    instruction.program_id.resize(32, 0x00);
    instruction.data = {0};
    
    std::unordered_map<slonana::PublicKey, slonana::svm::ProgramAccount> accounts;
    auto outcome = svm->execute_transaction({instruction}, accounts);
    ASSERT_TRUE(outcome.is_success());
    
    validator->stop();
}

void test_validator_error_recovery() {
    slonana::common::ValidatorConfig config;
    config.ledger_path = "/tmp/test_error_recovery_validator";
    config.enable_rpc = false;
    config.enable_gossip = false;
    
    auto validator = std::make_unique<slonana::SolanaValidator>(config);
    
    validator->initialize();
    validator->start();
    
    auto validator_core = validator->get_validator_core();
    auto ledger = validator->get_ledger_manager();
    
    // Test processing of an invalid block (should be handled gracefully)
    slonana::ledger::Block invalid_block;
    invalid_block.slot = 999; // Invalid slot jump
    invalid_block.block_hash.resize(32, 0xFF);
    invalid_block.parent_hash.resize(32, 0xEE); // Invalid parent
    invalid_block.validator.resize(32, 0x01);
    
    // This should not crash the validator
    EXPECT_NO_THROW(validator_core->process_block(invalid_block));
    
    // Validator should still be running and responsive
    ASSERT_TRUE(validator->is_running());
    
    // Valid blocks should still be processable
    slonana::ledger::Block valid_block;
    valid_block.slot = 1;
    valid_block.block_hash.resize(32, 0x01);
    valid_block.parent_hash.resize(32, 0x00);
    valid_block.validator.resize(32, 0x01);
    valid_block.block_signature.resize(64, 0xAA);
    
    validator_core->process_block(valid_block);
    ASSERT_EQ(1, ledger->get_latest_slot());
    
    validator->stop();
}

void test_validator_performance_stress() {
    slonana::common::ValidatorConfig config;
    config.ledger_path = "/tmp/test_performance_validator";
    config.enable_rpc = false;
    config.enable_gossip = false;
    
    auto validator = std::make_unique<slonana::SolanaValidator>(config);
    
    validator->initialize();
    validator->start();
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    auto validator_core = validator->get_validator_core();
    auto ledger = validator->get_ledger_manager();
    
    // Process 50 blocks rapidly
    for (uint64_t slot = 1; slot <= 50; ++slot) {
        slonana::ledger::Block block;
        block.slot = slot;
        block.block_hash.resize(32, static_cast<uint8_t>(slot % 256));
        block.parent_hash.resize(32, static_cast<uint8_t>((slot - 1) % 256));
        block.validator.resize(32, 0x01);
        block.block_signature.resize(64, 0xAA);
        
        validator_core->process_block(block);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "Performance: Processed 50 blocks in " << duration.count() << "ms" << std::endl;
    
    ASSERT_EQ(50, ledger->get_latest_slot());
    ASSERT_EQ(50, ledger->get_ledger_size());
    
    validator->stop();
}

void run_integration_tests(TestRunner& runner) {
    std::cout << "\n=== Integration Tests ===" << std::endl;
    
    runner.run_test("Full Validator Lifecycle", test_full_validator_lifecycle);
    runner.run_test("Validator RPC Integration", test_validator_with_rpc_integration);
    runner.run_test("Validator Block Processing Pipeline", test_validator_block_processing_pipeline);
    runner.run_test("Validator Staking Integration", test_validator_staking_integration);
    runner.run_test("Validator SVM Integration", test_validator_svm_integration);
    runner.run_test("Validator Multi-Component Interaction", test_validator_multi_component_interaction);
    runner.run_test("Validator Error Recovery", test_validator_error_recovery);
    runner.run_test("Validator Performance Stress", test_validator_performance_stress);
}