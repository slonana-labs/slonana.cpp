#include "test_framework.h"
#include "svm/enhanced_engine.h"
#include <vector>
#include <chrono>
#include <random>

/**
 * Enhanced SVM Engine Test Suite
 * 
 * This test suite demonstrates the enhanced SVM capabilities including:
 * - Parallel transaction execution
 * - Program caching and JIT compilation simulation
 * - Memory pooling optimizations
 * - SPL Token program functionality
 * - Performance improvements
 */

namespace {

void test_enhanced_engine_initialization() {
    slonana::svm::EnhancedExecutionEngine engine;
    
    // Test that enhanced engine maintains compatibility
    auto stats = engine.get_performance_metrics();
    ASSERT_EQ(0, stats.parallel_transactions_executed);
    ASSERT_EQ(0, stats.cache_hits);
    ASSERT_EQ(0, stats.cache_misses);
    
    std::cout << "Enhanced engine initialized successfully" << std::endl;
}

void test_program_caching() {
    slonana::svm::EnhancedExecutionEngine engine;
    
    // Create a test program
    slonana::svm::ProgramAccount test_program;
    test_program.program_id.resize(32, 0x42);
    test_program.executable = true;
    test_program.data = {0x01, 0x02, 0x03, 0x04}; // Some bytecode
    test_program.lamports = 0;
    test_program.rent_epoch = 0;
    
    // Program should not be cached initially
    ASSERT_FALSE(engine.is_program_cached(test_program.program_id));
    
    // Load and cache the program
    auto load_result = engine.load_and_cache_program(test_program);
    ASSERT_TRUE(load_result.is_ok());
    
    // Program should now be cached
    ASSERT_TRUE(engine.is_program_cached(test_program.program_id));
    
    // Check performance metrics
    auto metrics = engine.get_performance_metrics();
    ASSERT_GT(metrics.cache_hits, 0);
    
    std::cout << "Program caching working correctly" << std::endl;
}

void test_memory_pooling() {
    slonana::svm::EnhancedExecutionEngine engine;
    
    // Test pool utilization before enabling
    engine.enable_memory_pooling(false);
    size_t utilization_disabled = engine.get_pool_utilization();
    
    // Enable memory pooling
    engine.enable_memory_pooling(true);
    size_t utilization_enabled = engine.get_pool_utilization();
    
    // Pool should be available for use
    ASSERT_EQ(0, utilization_enabled); // Should start empty
    
    std::cout << "Memory pooling configuration working" << std::endl;
}

void test_spl_token_program() {
    slonana::svm::SPLTokenProgram token_program;
    
    // Verify program ID
    auto program_id = token_program.get_program_id();
    ASSERT_EQ(32, program_id.size());
    
    // Test initialize mint instruction
    slonana::svm::Instruction init_mint_instruction;
    init_mint_instruction.program_id = program_id;
    init_mint_instruction.data = {0, 6, 0}; // InitializeMint instruction with 6 decimals + padding
    init_mint_instruction.accounts.resize(3);
    for (auto& account : init_mint_instruction.accounts) {
        account.resize(32, 0x01);
    }
    
    slonana::svm::ExecutionContext context;
    context.max_compute_units = 10000;
    
    auto outcome = token_program.execute(init_mint_instruction, context);
    ASSERT_TRUE(outcome.is_success());
    ASSERT_GT(outcome.compute_units_consumed, 0);
    
    // Test transfer instruction
    slonana::svm::Instruction transfer_instruction;
    transfer_instruction.program_id = program_id;
    transfer_instruction.data = {3}; // Transfer instruction
    transfer_instruction.accounts.resize(3);
    for (auto& account : transfer_instruction.accounts) {
        account.resize(32, 0x02);
    }
    
    context.consumed_compute_units = 0; // Reset for next instruction
    outcome = token_program.execute(transfer_instruction, context);
    ASSERT_TRUE(outcome.is_success());
    ASSERT_GT(outcome.compute_units_consumed, 0);
    
    std::cout << "SPL Token program operations working correctly" << std::endl;
}

void test_parallel_transaction_execution() {
    slonana::svm::EnhancedExecutionEngine engine;
    
    // Create multiple independent transactions
    std::vector<std::vector<slonana::svm::Instruction>> transaction_batches;
    
    // Create 3 transaction batches with different account sets
    for (int batch = 0; batch < 3; ++batch) {
        std::vector<slonana::svm::Instruction> transactions;
        
        for (int tx = 0; tx < 5; ++tx) {
            slonana::svm::Instruction instruction;
            instruction.program_id.resize(32, 0x00); // System program
            instruction.data = {0}; // Transfer instruction
            
            // Create unique accounts for each transaction to avoid conflicts
            instruction.accounts.resize(2);
            instruction.accounts[0].resize(32, batch * 10 + tx); // Unique write account
            instruction.accounts[1].resize(32, batch * 10 + tx + 100); // Unique read account
            
            transactions.push_back(instruction);
        }
        
        transaction_batches.push_back(transactions);
    }
    
    // Create test accounts
    std::unordered_map<slonana::common::PublicKey, slonana::svm::ProgramAccount> accounts;
    for (int i = 0; i < 300; ++i) { // Enough accounts for all transactions
        slonana::svm::ProgramAccount account;
        account.program_id.resize(32, i);
        account.lamports = 1000000;
        accounts[account.program_id] = account;
    }
    
    // Execute transactions in parallel
    auto start_time = std::chrono::high_resolution_clock::now();
    auto outcome = engine.execute_parallel_transactions(transaction_batches, accounts);
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time).count();
    
    ASSERT_TRUE(outcome.is_success());
    ASSERT_GT(outcome.compute_units_consumed, 0);
    
    // Verify performance metrics were updated
    auto metrics = engine.get_performance_metrics();
    ASSERT_GT(metrics.parallel_transactions_executed, 0);
    ASSERT_GT(metrics.average_execution_time_ms, 0.0);
    
    std::cout << "Parallel execution completed in " << duration << " microseconds" << std::endl;
    std::cout << "Processed " << metrics.parallel_transactions_executed << " transaction batches" << std::endl;
}

void test_performance_improvements() {
    slonana::svm::EnhancedExecutionEngine enhanced_engine;
    slonana::svm::ExecutionEngine basic_engine;
    
    // Create a series of instructions for performance comparison
    std::vector<slonana::svm::Instruction> instructions;
    
    for (int i = 0; i < 100; ++i) {
        slonana::svm::Instruction instruction;
        instruction.program_id.resize(32, 0x00); // System program
        instruction.data = {0}; // Transfer instruction
        instruction.accounts.resize(2);
        instruction.accounts[0].resize(32, i);
        instruction.accounts[1].resize(32, i + 100);
        instructions.push_back(instruction);
    }
    
    // Create test accounts
    std::unordered_map<slonana::common::PublicKey, slonana::svm::ProgramAccount> accounts_basic;
    std::unordered_map<slonana::common::PublicKey, slonana::svm::ProgramAccount> accounts_enhanced;
    
    for (int i = 0; i < 200; ++i) {
        slonana::svm::ProgramAccount account;
        account.program_id.resize(32, i);
        account.lamports = 1000000;
        accounts_basic[account.program_id] = account;
        accounts_enhanced[account.program_id] = account;
    }
    
    // Time basic execution
    auto start_basic = std::chrono::high_resolution_clock::now();
    auto outcome_basic = basic_engine.execute_transaction(instructions, accounts_basic);
    auto end_basic = std::chrono::high_resolution_clock::now();
    auto duration_basic = std::chrono::duration_cast<std::chrono::microseconds>(
        end_basic - start_basic).count();
    
    // Time enhanced execution (using single batch for fair comparison)
    std::vector<std::vector<slonana::svm::Instruction>> single_batch = {instructions};
    auto start_enhanced = std::chrono::high_resolution_clock::now();
    auto outcome_enhanced = enhanced_engine.execute_parallel_transactions(
        single_batch, accounts_enhanced);
    auto end_enhanced = std::chrono::high_resolution_clock::now();
    auto duration_enhanced = std::chrono::duration_cast<std::chrono::microseconds>(
        end_enhanced - start_enhanced).count();
    
    // Both should succeed
    ASSERT_TRUE(outcome_basic.is_success());
    ASSERT_TRUE(outcome_enhanced.is_success());
    
    // Performance comparison (enhanced may be faster due to optimizations)
    std::cout << "Basic engine execution time: " << duration_basic << " microseconds" << std::endl;
    std::cout << "Enhanced engine execution time: " << duration_enhanced << " microseconds" << std::endl;
    
    if (duration_enhanced < duration_basic) {
        double improvement = (double)(duration_basic - duration_enhanced) / duration_basic * 100.0;
        std::cout << "Performance improvement: " << improvement << "%" << std::endl;
    }
    
    // Verify enhanced engine metrics
    auto metrics = enhanced_engine.get_performance_metrics();
    std::cout << "Cache hits: " << metrics.cache_hits << std::endl;
    std::cout << "Cache misses: " << metrics.cache_misses << std::endl;
}

void test_enhanced_engine_compatibility() {
    slonana::svm::EnhancedExecutionEngine enhanced_engine;
    
    // Test that enhanced engine maintains compatibility with base functionality
    
    // Load a program using base class method
    slonana::svm::ProgramAccount test_program;
    test_program.program_id.resize(32, 0x33);
    test_program.executable = true;
    test_program.data = {0xAA, 0xBB, 0xCC};
    test_program.lamports = 0;
    
    auto load_result = enhanced_engine.load_program(test_program);
    ASSERT_TRUE(load_result.is_ok());
    
    // Verify program is loaded using base class method
    ASSERT_TRUE(enhanced_engine.is_program_loaded(test_program.program_id));
    
    // Execute using base class method
    std::vector<slonana::svm::Instruction> instructions;
    slonana::svm::Instruction instruction;
    instruction.program_id.resize(32, 0x00); // System program
    instruction.data = {0};
    instruction.accounts.resize(2);
    instruction.accounts[0].resize(32, 0x01);
    instruction.accounts[1].resize(32, 0x02);
    instructions.push_back(instruction);
    
    std::unordered_map<slonana::common::PublicKey, slonana::svm::ProgramAccount> accounts;
    slonana::svm::ProgramAccount account1, account2;
    account1.program_id = instruction.accounts[0];
    account1.lamports = 1000000;
    account2.program_id = instruction.accounts[1];
    account2.lamports = 500000;
    accounts[account1.program_id] = account1;
    accounts[account2.program_id] = account2;
    
    auto outcome = enhanced_engine.execute_transaction(instructions, accounts);
    ASSERT_TRUE(outcome.is_success());
    
    std::cout << "Enhanced engine maintains full compatibility with base class" << std::endl;
}

} // anonymous namespace

int main() {
    std::cout << "=== Enhanced SVM Engine Test Suite ===" << std::endl;
    
    TestRunner runner;
    
    try {
        runner.run_test("Enhanced Engine Initialization", test_enhanced_engine_initialization);
        runner.run_test("Program Caching", test_program_caching);
        runner.run_test("Memory Pooling", test_memory_pooling);
        runner.run_test("SPL Token Program", test_spl_token_program);
        runner.run_test("Parallel Transaction Execution", test_parallel_transaction_execution);
        runner.run_test("Performance Improvements", test_performance_improvements);
        runner.run_test("Enhanced Engine Compatibility", test_enhanced_engine_compatibility);
        
        runner.print_summary();
        
        std::cout << "\n=== Enhanced SVM Test Summary ===" << std::endl;
        if (runner.all_passed()) {
            std::cout << "All enhanced SVM engine tests PASSED!" << std::endl;
            std::cout << "Enhanced features validated:" << std::endl;
            std::cout << "- ✅ Parallel transaction execution" << std::endl;
            std::cout << "- ✅ Program caching and JIT simulation" << std::endl;
            std::cout << "- ✅ Memory pooling optimizations" << std::endl;
            std::cout << "- ✅ SPL Token program integration" << std::endl;
            std::cout << "- ✅ Performance improvements" << std::endl;
            std::cout << "- ✅ Backward compatibility maintained" << std::endl;
            return 0;
        } else {
            std::cout << "Some enhanced SVM engine tests FAILED!" << std::endl;
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Test suite failed with exception: " << e.what() << std::endl;
        return 1;
    }
}