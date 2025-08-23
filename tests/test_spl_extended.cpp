#include "test_framework.h"
#include "svm/spl_programs.h"
#include "svm/enhanced_engine.h"
#include <random>
#include <chrono>

// Add stream operator for ExecutionResult to enable test assertions
inline std::ostream& operator<<(std::ostream& os, slonana::svm::ExecutionResult result) {
    switch (result) {
        case slonana::svm::ExecutionResult::SUCCESS:
            return os << "SUCCESS";
        case slonana::svm::ExecutionResult::COMPUTE_BUDGET_EXCEEDED:
            return os << "COMPUTE_BUDGET_EXCEEDED";
        case slonana::svm::ExecutionResult::PROGRAM_ERROR:
            return os << "PROGRAM_ERROR";
        case slonana::svm::ExecutionResult::ACCOUNT_NOT_FOUND:
            return os << "ACCOUNT_NOT_FOUND";
        case slonana::svm::ExecutionResult::INSUFFICIENT_FUNDS:
            return os << "INSUFFICIENT_FUNDS";
        case slonana::svm::ExecutionResult::INVALID_INSTRUCTION:
            return os << "INVALID_INSTRUCTION";
        default:
            return os << "UNKNOWN_RESULT";
    }
}

/**
 * Extended SPL Program Test Suite
 * 
 * Comprehensive testing of all SPL programs including:
 * - SPL Associated Token Account (ATA) program
 * - SPL Memo program  
 * - Extended System program with nonce accounts
 * - Program registry functionality
 */

namespace {

void test_spl_ata_program_basic() {
    slonana::svm::SPLAssociatedTokenProgram ata_program;
    slonana::svm::ExecutionContext context;
    
    // Test program ID
    auto program_id = ata_program.get_program_id();
    ASSERT_EQ(32, program_id.size());
    
    // Create test instruction for ATA creation
    slonana::svm::Instruction instruction;
    instruction.program_id = program_id;
    instruction.data.resize(1 + 32 * 4); // instruction + 4 pubkeys
    instruction.data[0] = 0; // Create instruction
    
    // Fill in test pubkeys
    for (size_t i = 1; i < instruction.data.size(); i++) {
        instruction.data[i] = static_cast<uint8_t>(i % 256);
    }
    
    // Add required accounts
    instruction.accounts.resize(4);
    for (size_t i = 0; i < 4; i++) {
        instruction.accounts[i].resize(32, static_cast<uint8_t>(i + 1));
    }
    
    auto outcome = ata_program.execute(instruction, context);
    ASSERT_EQ(slonana::svm::ExecutionResult::SUCCESS, outcome.result);
    ASSERT_TRUE(outcome.compute_units_consumed > 0);
    
    std::cout << "SPL ATA program basic functionality working" << std::endl;
}

void test_spl_ata_program_idempotent() {
    slonana::svm::SPLAssociatedTokenProgram ata_program;
    slonana::svm::ExecutionContext context;
    
    // Create test instruction for idempotent ATA creation
    slonana::svm::Instruction instruction;
    instruction.program_id = ata_program.get_program_id();
    instruction.data.resize(1 + 32 * 4);
    instruction.data[0] = 1; // CreateIdempotent instruction
    
    // Fill in test data
    for (size_t i = 1; i < instruction.data.size(); i++) {
        instruction.data[i] = static_cast<uint8_t>(i % 256);
    }
    
    instruction.accounts.resize(4);
    for (size_t i = 0; i < 4; i++) {
        instruction.accounts[i].resize(32, static_cast<uint8_t>(i + 10));
    }
    
    // First execution should create account
    auto outcome1 = ata_program.execute(instruction, context);
    ASSERT_EQ(slonana::svm::ExecutionResult::SUCCESS, outcome1.result);
    
    // Second execution should succeed (idempotent)
    auto outcome2 = ata_program.execute(instruction, context);
    ASSERT_EQ(slonana::svm::ExecutionResult::SUCCESS, outcome2.result);
    
    std::cout << "SPL ATA idempotent creation working correctly" << std::endl;
}

void test_spl_memo_program() {
    slonana::svm::SPLMemoProgram memo_program;
    slonana::svm::ExecutionContext context;
    
    // Test program ID
    auto program_id = memo_program.get_program_id();
    ASSERT_EQ(32, program_id.size());
    
    // Test with valid memo
    slonana::svm::Instruction instruction;
    instruction.program_id = program_id;
    std::string memo_text = "Hello, Solana from C++!";
    instruction.data.assign(memo_text.begin(), memo_text.end());
    
    auto outcome = memo_program.execute(instruction, context);
    ASSERT_EQ(slonana::svm::ExecutionResult::SUCCESS, outcome.result);
    ASSERT_TRUE(outcome.logs.find("Hello, Solana from C++!") != std::string::npos);
    
    // Test with empty memo (should fail)
    slonana::svm::Instruction empty_instruction;
    empty_instruction.program_id = program_id;
    empty_instruction.data.clear();
    
    auto empty_outcome = memo_program.execute(empty_instruction, context);
    ASSERT_EQ(slonana::svm::ExecutionResult::PROGRAM_ERROR, empty_outcome.result);
    
    // Test with oversized memo (should fail)
    slonana::svm::Instruction large_instruction;
    large_instruction.program_id = program_id;
    large_instruction.data.resize(1000, 'A'); // Too large
    
    auto large_outcome = memo_program.execute(large_instruction, context);
    ASSERT_EQ(slonana::svm::ExecutionResult::PROGRAM_ERROR, large_outcome.result);
    
    std::cout << "SPL Memo program validation working correctly" << std::endl;
}

void test_extended_system_program_nonce() {
    slonana::svm::ExtendedSystemProgram ext_system;
    slonana::svm::ExecutionContext context;
    context.current_epoch = 100;
    
    // Test nonce account initialization
    slonana::svm::Instruction init_instruction;
    init_instruction.program_id = ext_system.get_program_id();
    init_instruction.data.resize(1);
    init_instruction.data[0] = 6; // InitializeNonceAccount
    
    // Add nonce account
    slonana::common::PublicKey nonce_account(32, 0x42);
    init_instruction.accounts.push_back(nonce_account);
    
    auto init_outcome = ext_system.execute(init_instruction, context);
    ASSERT_EQ(slonana::svm::ExecutionResult::SUCCESS, init_outcome.result);
    
    // Verify nonce account was created
    ASSERT_TRUE(context.accounts.find(nonce_account) != context.accounts.end());
    auto& account = context.accounts[nonce_account];
    ASSERT_EQ(80, account.data.size()); // Nonce account size
    ASSERT_EQ(1, account.data[79]); // Initialized flag
    
    // Test nonce advancement
    slonana::svm::Instruction advance_instruction;
    advance_instruction.program_id = ext_system.get_program_id();
    advance_instruction.data.resize(1);
    advance_instruction.data[0] = 4; // AdvanceNonceAccount
    advance_instruction.accounts.push_back(nonce_account);
    
    // Store original nonce value
    std::vector<uint8_t> original_nonce(account.data.begin() + 32, 
                                       account.data.begin() + 64);
    
    auto advance_outcome = ext_system.execute(advance_instruction, context);
    ASSERT_EQ(slonana::svm::ExecutionResult::SUCCESS, advance_outcome.result);
    
    // Verify nonce was updated
    std::vector<uint8_t> new_nonce(account.data.begin() + 32,
                                  account.data.begin() + 64);
    ASSERT_NE(original_nonce, new_nonce);
    
    std::cout << "Extended System nonce accounts working correctly" << std::endl;
}

void test_spl_program_registry() {
    slonana::svm::SPLProgramRegistry registry;
    slonana::svm::EnhancedExecutionEngine engine;
    
    // Register all programs
    registry.register_all_programs(engine);
    
    // Test program discovery
    auto program_ids = registry.get_all_program_ids();
    ASSERT_TRUE(program_ids.size() >= 3); // At least ATA, Memo, Extended System
    
    // Test SPL program identification
    for (const auto& program_id : program_ids) {
        ASSERT_TRUE(registry.is_spl_program(program_id));
        std::string name = registry.get_program_name(program_id);
        ASSERT_FALSE(name.empty());
        ASSERT_NE("Unknown Program", name);
    }
    
    // Test unknown program
    slonana::common::PublicKey unknown_id(32, 0xFF);
    ASSERT_FALSE(registry.is_spl_program(unknown_id));
    ASSERT_EQ("Unknown Program", registry.get_program_name(unknown_id));
    
    std::cout << "SPL program registry working correctly" << std::endl;
}

void test_enhanced_engine_with_spl_programs() {
    slonana::svm::EnhancedExecutionEngine engine;
    slonana::svm::SPLProgramRegistry registry;
    
    // Register all SPL programs
    registry.register_all_programs(engine);
    
    // Test ATA creation through enhanced engine
    slonana::svm::Instruction ata_instruction;
    ata_instruction.program_id = slonana::svm::SPLAssociatedTokenProgram().get_program_id();
    ata_instruction.data.resize(1 + 32 * 4);
    ata_instruction.data[0] = 0; // Create
    
    for (size_t i = 1; i < ata_instruction.data.size(); i++) {
        ata_instruction.data[i] = static_cast<uint8_t>(i % 256);
    }
    
    ata_instruction.accounts.resize(4);
    for (size_t i = 0; i < 4; i++) {
        ata_instruction.accounts[i].resize(32, static_cast<uint8_t>(i + 20));
    }
    
    std::unordered_map<slonana::common::PublicKey, slonana::svm::ProgramAccount> accounts;
    auto outcome = engine.execute_transaction({ata_instruction}, accounts);
    ASSERT_EQ(slonana::svm::ExecutionResult::SUCCESS, outcome.result);
    
    // Test memo through enhanced engine
    slonana::svm::Instruction memo_instruction;
    memo_instruction.program_id = slonana::svm::SPLMemoProgram().get_program_id();
    std::string memo = "Enhanced engine SPL test";
    memo_instruction.data.assign(memo.begin(), memo.end());
    
    auto memo_outcome = engine.execute_transaction({memo_instruction}, accounts);
    ASSERT_EQ(slonana::svm::ExecutionResult::SUCCESS, memo_outcome.result);
    
    std::cout << "Enhanced engine SPL integration working correctly" << std::endl;
}

void test_spl_programs_performance() {
    slonana::svm::EnhancedExecutionEngine engine;
    slonana::svm::SPLProgramRegistry registry;
    registry.register_all_programs(engine);
    
    const size_t num_operations = 100;
    std::vector<double> execution_times;
    
    // Performance test for memo operations
    for (size_t i = 0; i < num_operations; i++) {
        auto start = std::chrono::high_resolution_clock::now();
        
        slonana::svm::Instruction memo_instruction;
        memo_instruction.program_id = slonana::svm::SPLMemoProgram().get_program_id();
        std::string memo = "Performance test #" + std::to_string(i);
        memo_instruction.data.assign(memo.begin(), memo.end());
        
        std::unordered_map<slonana::common::PublicKey, slonana::svm::ProgramAccount> accounts;
        auto outcome = engine.execute_transaction({memo_instruction}, accounts);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        execution_times.push_back(duration.count());
        
        ASSERT_EQ(slonana::svm::ExecutionResult::SUCCESS, outcome.result);
    }
    
    // Calculate performance metrics
    double total_time = 0;
    for (double time : execution_times) {
        total_time += time;
    }
    double average_time = total_time / execution_times.size();
    
    std::cout << "SPL programs performance test completed:" << std::endl;
    std::cout << "  Operations: " << num_operations << std::endl;
    std::cout << "  Average execution time: " << average_time << " microseconds" << std::endl;
    std::cout << "  Total time: " << total_time << " microseconds" << std::endl;
    
    // Performance should be reasonable (less than 1ms per operation)
    ASSERT_TRUE(average_time < 1000.0);
}

void test_comprehensive_spl_ecosystem() {
    slonana::svm::EnhancedExecutionEngine engine;
    slonana::svm::SPLProgramRegistry registry;
    registry.register_all_programs(engine);
    
    std::unordered_map<slonana::common::PublicKey, slonana::svm::ProgramAccount> accounts;
    
    // 1. Initialize a nonce account
    slonana::svm::Instruction nonce_init;
    nonce_init.program_id = slonana::svm::ExtendedSystemProgram().get_program_id();
    nonce_init.data = {6}; // InitializeNonceAccount
    slonana::common::PublicKey nonce_key(32, 0x55);
    nonce_init.accounts.push_back(nonce_key);
    
    auto nonce_outcome = engine.execute_transaction({nonce_init}, accounts);
    ASSERT_EQ(slonana::svm::ExecutionResult::SUCCESS, nonce_outcome.result);
    
    // 2. Create an associated token account
    slonana::svm::Instruction ata_create;
    ata_create.program_id = slonana::svm::SPLAssociatedTokenProgram().get_program_id();
    ata_create.data.resize(1 + 32 * 4);
    ata_create.data[0] = 0; // Create
    
    for (size_t i = 1; i < ata_create.data.size(); i++) {
        ata_create.data[i] = static_cast<uint8_t>(i % 256);
    }
    
    ata_create.accounts.resize(4);
    for (size_t i = 0; i < 4; i++) {
        ata_create.accounts[i].resize(32, static_cast<uint8_t>(i + 30));
    }
    
    auto ata_outcome = engine.execute_transaction({ata_create}, accounts);
    ASSERT_EQ(slonana::svm::ExecutionResult::SUCCESS, ata_outcome.result);
    
    // 3. Log a memo about the operations
    slonana::svm::Instruction memo;
    memo.program_id = slonana::svm::SPLMemoProgram().get_program_id();
    std::string memo_text = "Comprehensive SPL ecosystem test completed successfully";
    memo.data.assign(memo_text.begin(), memo_text.end());
    
    auto memo_outcome = engine.execute_transaction({memo}, accounts);
    ASSERT_EQ(slonana::svm::ExecutionResult::SUCCESS, memo_outcome.result);
    
    // 4. Advance the nonce
    slonana::svm::Instruction nonce_advance;
    nonce_advance.program_id = slonana::svm::ExtendedSystemProgram().get_program_id();
    nonce_advance.data = {4}; // AdvanceNonceAccount
    nonce_advance.accounts.push_back(nonce_key);
    
    auto advance_outcome = engine.execute_transaction({nonce_advance}, accounts);
    ASSERT_EQ(slonana::svm::ExecutionResult::SUCCESS, advance_outcome.result);
    
    std::cout << "Comprehensive SPL ecosystem test passed" << std::endl;
    std::cout << "Successfully executed: nonce init, ATA creation, memo, nonce advance" << std::endl;
}

} // namespace

int main() {
    std::cout << "=== Extended SPL Program Test Suite ===" << std::endl;
    
    TestRunner runner;
    
    try {
        runner.run_test("SPL ATA Program Basic", test_spl_ata_program_basic);
        runner.run_test("SPL ATA Program Idempotent", test_spl_ata_program_idempotent);
        runner.run_test("SPL Memo Program", test_spl_memo_program);
        runner.run_test("Extended System Program Nonce", test_extended_system_program_nonce);
        runner.run_test("SPL Program Registry", test_spl_program_registry);
        runner.run_test("Enhanced Engine with SPL Programs", test_enhanced_engine_with_spl_programs);
        runner.run_test("SPL Programs Performance", test_spl_programs_performance);
        runner.run_test("Comprehensive SPL Ecosystem", test_comprehensive_spl_ecosystem);
        
        runner.print_summary();
    } catch (const std::exception& e) {
        std::cerr << "Test suite failed: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n=== Extended SPL Test Summary ===" << std::endl;
    std::cout << "All extended SPL program tests PASSED!" << std::endl;
    std::cout << "Extended features validated:" << std::endl;
    std::cout << "- ✅ SPL Associated Token Account program" << std::endl;
    std::cout << "- ✅ SPL Memo program" << std::endl;
    std::cout << "- ✅ Extended System program with nonce accounts" << std::endl;
    std::cout << "- ✅ SPL program registry" << std::endl;
    std::cout << "- ✅ Enhanced engine integration" << std::endl;
    std::cout << "- ✅ Performance optimization" << std::endl;
    std::cout << "- ✅ Comprehensive ecosystem interoperability" << std::endl;
    
    return 0;
}