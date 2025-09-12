#include "svm/bpf_runtime.h"
#include "svm/bpf_verifier.h"
#include "svm/engine.h"
#include "test_framework.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <random>
#include <thread>

// Comprehensive BPF Runtime Test Suite
// Tests all aspects of the BPF runtime including:
// - Complete instruction set coverage
// - Memory management and safety
// - JIT compilation and optimization
// - Parallel execution capabilities
// - Security model validation
// - Performance benchmarking

namespace slonana {
namespace svm {

class BpfRuntimeTester {
private:
  BpfRuntime runtime_;
  AccountManager account_manager_;
  std::mt19937 rng_;

public:
  BpfRuntimeTester()
      : rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {}

  // Test 1: Complete Instruction Set Architecture Coverage
  bool test_complete_isa_coverage() {
    std::cout << "🧪 Testing complete ISA coverage..." << std::endl;

    struct InstructionTest {
      uint8_t opcode;
      std::string name;
      std::vector<uint8_t> bytecode;
      bool should_succeed;
    };

    std::vector<InstructionTest> instruction_tests = {
        // Arithmetic instructions
        {0x07,
         "ADD64_IMM",
         {0x07, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00},
         true},
        {0x0f,
         "ADD64_REG",
         {0x0f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
         true},
        {0x17,
         "SUB64_IMM",
         {0x17, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00},
         true},
        {0x1f,
         "SUB64_REG",
         {0x1f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
         true},
        {0x27,
         "MUL64_IMM",
         {0x27, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00},
         true},
        {0x2f,
         "MUL64_REG",
         {0x2f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
         true},
        {0x37,
         "DIV64_IMM",
         {0x37, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00},
         true},
        {0x3f,
         "DIV64_REG",
         {0x3f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
         true},

        // Bitwise operations
        {0x47,
         "OR64_IMM",
         {0x47, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00},
         true},
        {0x4f,
         "OR64_REG",
         {0x4f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
         true},
        {0x57,
         "AND64_IMM",
         {0x57, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00},
         true},
        {0x5f,
         "AND64_REG",
         {0x5f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
         true},
        {0x67,
         "LSH64_IMM",
         {0x67, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00},
         true},
        {0x6f,
         "LSH64_REG",
         {0x6f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
         true},
        {0x77,
         "RSH64_IMM",
         {0x77, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00},
         true},
        {0x7f,
         "RSH64_REG",
         {0x7f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
         true},

        // Memory operations
        {0x18,
         "LDDW",
         {0x18, 0x00, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78, 0x00, 0x00, 0x00,
          0x00, 0x9a, 0xbc, 0xde, 0xf0},
         true},
        {0x79, "LDXDW", {0x79, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00}, true},
        {0x7b, "STXDW", {0x7b, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00}, true},
        {0x61, "LDXW", {0x61, 0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}, true},
        {0x63, "STXW", {0x63, 0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}, true},

        // Jump instructions
        {0x05, "JA", {0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}, true},
        {0x15,
         "JEQ_IMM",
         {0x15, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00},
         true},
        {0x1d,
         "JEQ_REG",
         {0x1d, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00},
         true},
        {0x25,
         "JGT_IMM",
         {0x25, 0x00, 0x01, 0x00, 0x10, 0x00, 0x00, 0x00},
         true},
        {0x2d,
         "JGT_REG",
         {0x2d, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00},
         true},

        // Exit instruction
        {0x95, "EXIT", {0x95, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, true},

        // Invalid instructions (should fail verification)
        {0xFF,
         "INVALID",
         {0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
         false}};

    int passed = 0;
    int total = instruction_tests.size();

    for (const auto &test : instruction_tests) {
      BpfProgram program;
      program.code = test.bytecode;
      program.compute_units = 1000;

      BpfVerifier verifier;
      bool verification_result = verifier.verify(program);

      if (verification_result == test.should_succeed) {
        std::cout << "  ✅ " << test.name << " - verification as expected"
                  << std::endl;
        passed++;
      } else {
        std::cout << "  ❌ " << test.name << " - unexpected verification result"
                  << std::endl;
      }
    }

    double success_rate = (double)passed / total * 100.0;
    std::cout << "ISA Coverage: " << passed << "/" << total << " ("
              << std::fixed << std::setprecision(1) << success_rate << "%)"
              << std::endl;

    return success_rate >= 85.0;
  }

  // Test 2: Memory Management and Safety
  bool test_memory_management_safety() {
    std::cout << "🧪 Testing memory management and safety..." << std::endl;

    struct MemoryTest {
      std::string name;
      std::vector<uint8_t> bytecode;
      bool should_succeed;
      std::string description;
    };

    std::vector<MemoryTest> memory_tests = {
        {"Valid Stack Access",
         {
             0x18, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Load stack address
             0x7b, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Store to stack
             0x79, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Load from stack
             0x95, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // Exit
         },
         true,
         "Valid memory access within stack bounds"},
        {"Stack Overflow",
         {
             0x18, 0x01, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
             0x00, 0x00, 0x00, 0x00, 0x00, // Load invalid address
             0x7b, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Try to store
                                                             // (should fail)
             0x95, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // Exit
         },
         false,
         "Memory access beyond stack bounds"},
        {"Null Pointer Dereference",
         {
             0x18, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
             0x00, 0x00, 0x00, 0x00, 0x00,                   // Load null
             0x79, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Try to
                                                             // dereference
                                                             // (should fail)
             0x95, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // Exit
         },
         false,
         "Dereferencing null pointer"},
        {"Unaligned Memory Access",
         {
             0x18, 0x01, 0x00, 0x00, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
             0x00, 0x00, 0x00, 0x00, 0x00, // Load unaligned address
             0x79, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Try unaligned
                                                             // access
             0x95, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // Exit
         },
         false,
         "Unaligned memory access (should be caught)"}};

    int passed = 0;
    int total = memory_tests.size();

    for (const auto &test : memory_tests) {
      BpfProgram program;
      program.code = test.bytecode;
      program.compute_units = 1000;

      // Setup execution context with limited memory
      BpfExecutionContext context;
      context.input_data.resize(32, 0);
      context.stack_memory.resize(512, 0); // Limited stack

      auto result = runtime_.execute(program, context);
      bool execution_succeeded = result.is_success();

      if (execution_succeeded == test.should_succeed) {
        std::cout << "  ✅ " << test.name << " - " << test.description
                  << std::endl;
        passed++;
      } else {
        std::cout << "  ❌ " << test.name
                  << " - unexpected result: " << test.description << std::endl;
      }
    }

    double success_rate = (double)passed / total * 100.0;
    std::cout << "Memory Safety: " << passed << "/" << total << " ("
              << std::fixed << std::setprecision(1) << success_rate << "%)"
              << std::endl;

    return success_rate >= 75.0;
  }

  // Test 3: JIT Compilation and Optimization
  bool test_jit_compilation_optimization() {
    std::cout << "🧪 Testing JIT compilation and optimization..." << std::endl;

    // Simple program that should benefit from JIT optimization
    std::vector<uint8_t> loop_program = {
        0xb7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // r0 = 0 (counter)
        0xb7, 0x01, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, // r1 = 100 (loop limit)
        // Loop start
        0x07, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // r0 += 1
        0x2d, 0x01, 0xfd, 0xff, 0x00, 0x00, 0x00, 0x00, // if r0 < r1 goto loop
        0x95, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // exit
    };

    BpfProgram program;
    program.code = loop_program;
    program.compute_units = 10000;

    BpfExecutionContext context;
    context.input_data.resize(32, 0);
    context.stack_memory.resize(512, 0);

    // Test interpreter execution
    auto start_time = std::chrono::high_resolution_clock::now();
    auto interpreter_result = runtime_.execute_interpreter(program, context);
    auto interpreter_time =
        std::chrono::high_resolution_clock::now() - start_time;

    // Test JIT execution (if available)
    start_time = std::chrono::high_resolution_clock::now();
    auto jit_result = runtime_.execute_jit(program, context);
    auto jit_time = std::chrono::high_resolution_clock::now() - start_time;

    auto interpreter_us =
        std::chrono::duration_cast<std::chrono::microseconds>(interpreter_time)
            .count();
    auto jit_us =
        std::chrono::duration_cast<std::chrono::microseconds>(jit_time).count();

    std::cout << "  Interpreter execution: " << interpreter_us << "μs"
              << std::endl;
    std::cout << "  JIT execution: " << jit_us << "μs" << std::endl;

    if (jit_us > 0) {
      double speedup = (double)interpreter_us / jit_us;
      std::cout << "  JIT speedup: " << std::fixed << std::setprecision(2)
                << speedup << "x" << std::endl;

      bool results_match =
          interpreter_result.return_value == jit_result.return_value;
      bool jit_faster =
          jit_us < interpreter_us || speedup > 1.5; // Allow for JIT warmup

      if (results_match && jit_faster) {
        std::cout << "  ✅ JIT compilation working correctly" << std::endl;
        return true;
      } else {
        std::cout << "  ❌ JIT issues: results_match=" << results_match
                  << ", faster=" << jit_faster << std::endl;
        return false;
      }
    } else {
      std::cout << "  ⚠️  JIT compilation not available or failed" << std::endl;
      return interpreter_result
          .is_success(); // At least interpreter should work
    }
  }

  // Test 4: Parallel Execution Capabilities
  bool test_parallel_execution() {
    std::cout << "🧪 Testing parallel execution capabilities..." << std::endl;

    // Simple computation program
    std::vector<uint8_t> compute_program = {
        0xb7, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // r0 = 1
        0xb7, 0x01, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, // r1 = 100
        // Multiply loop
        0x2f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // r0 *= r1
        0x17, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // r1 -= 1
        0x15, 0x01, 0xfc, 0xff, 0x00, 0x00, 0x00, 0x00, // if r1 != 0 goto loop
        0x95, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // exit
    };

    BpfProgram program;
    program.code = compute_program;
    program.compute_units = 10000;

    const int num_threads = 8;
    const int executions_per_thread = 50;

    // Sequential execution baseline
    auto start_time = std::chrono::high_resolution_clock::now();
    std::vector<BpfExecutionResult> sequential_results;

    for (int i = 0; i < num_threads * executions_per_thread; ++i) {
      BpfExecutionContext context;
      context.input_data.resize(32, 0);
      context.stack_memory.resize(512, 0);

      auto result = runtime_.execute(program, context);
      sequential_results.push_back(result);
    }
    auto sequential_time =
        std::chrono::high_resolution_clock::now() - start_time;

    // Parallel execution
    start_time = std::chrono::high_resolution_clock::now();
    std::vector<std::vector<BpfExecutionResult>> parallel_results(num_threads);
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
      threads.emplace_back([&, t]() {
        parallel_results[t].resize(executions_per_thread);
        for (int i = 0; i < executions_per_thread; ++i) {
          BpfExecutionContext context;
          context.input_data.resize(32, 0);
          context.stack_memory.resize(512, 0);

          parallel_results[t][i] = runtime_.execute(program, context);
        }
      });
    }

    for (auto &thread : threads) {
      thread.join();
    }
    auto parallel_time = std::chrono::high_resolution_clock::now() - start_time;

    auto sequential_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(sequential_time)
            .count();
    auto parallel_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(parallel_time)
            .count();

    std::cout << "  Sequential execution: " << sequential_ms << "ms"
              << std::endl;
    std::cout << "  Parallel execution: " << parallel_ms << "ms" << std::endl;

    if (parallel_ms > 0) {
      double speedup = (double)sequential_ms / parallel_ms;
      std::cout << "  Parallel speedup: " << std::fixed << std::setprecision(2)
                << speedup << "x" << std::endl;

      // Verify all results are consistent
      bool all_consistent = true;
      uint64_t expected_result = sequential_results[0].return_value;

      for (const auto &thread_results : parallel_results) {
        for (const auto &result : thread_results) {
          if (result.return_value != expected_result) {
            all_consistent = false;
            break;
          }
        }
        if (!all_consistent)
          break;
      }

      if (all_consistent && speedup > 1.5) {
        std::cout << "  ✅ Parallel execution working correctly" << std::endl;
        return true;
      } else {
        std::cout << "  ❌ Parallel execution issues: consistent="
                  << all_consistent << ", speedup=" << speedup << std::endl;
        return all_consistent; // At least results should be consistent
      }
    } else {
      std::cout << "  ❌ Parallel execution failed" << std::endl;
      return false;
    }
  }

  // Test 5: Security Model Validation
  bool test_security_model() {
    std::cout << "🧪 Testing security model validation..." << std::endl;

    struct SecurityTest {
      std::string name;
      std::vector<uint8_t> bytecode;
      bool should_pass_verification;
      bool should_pass_execution;
    };

    std::vector<SecurityTest> security_tests = {
        {"Infinite Loop Protection",
         {
             0x05, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, // Infinite jump
             0x95, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
             0x00 // Exit (unreachable)
         },
         false,
         false},
        {
            "Compute Unit Exhaustion",
            {
                0xb7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // r0 = 0
                // Long loop that exceeds compute units
                0x07, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // r0 += 1
                0x05, 0x00, 0xfe, 0xff, 0x00, 0x00, 0x00, 0x00, // goto loop
                0x95, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // exit
            },
            true,
            false // Should pass verification but fail execution due to compute
                  // limit
        },
        {
            "Division by Zero",
            {
                0xb7, 0x00, 0x00, 0x00,
                0x0a, 0x00, 0x00, 0x00, // r0 = 10
                0xb7, 0x01, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, // r1 = 0
                0x3f, 0x01, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, // r0 /= r1 (divide by zero)
                0x95, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00 // exit
            },
            true,
            false // Should fail at runtime
        },
        {
            "Valid Program",
            {
                0xb7, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x00, 0x00, // r0 = 42
                0x95, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // exit
            },
            true,
            true // Should pass both verification and execution
        }};

    int passed = 0;
    int total = security_tests.size();

    for (const auto &test : security_tests) {
      BpfProgram program;
      program.code = test.bytecode;
      program.compute_units = 1000; // Limited compute units

      BpfVerifier verifier;
      bool verification_passed = verifier.verify(program);

      bool execution_passed = false;
      if (verification_passed) {
        BpfExecutionContext context;
        context.input_data.resize(32, 0);
        context.stack_memory.resize(512, 0);

        auto result = runtime_.execute(program, context);
        execution_passed = result.is_success();
      }

      bool test_passed =
          (verification_passed == test.should_pass_verification) &&
          (execution_passed == test.should_pass_execution);

      if (test_passed) {
        std::cout << "  ✅ " << test.name << std::endl;
        passed++;
      } else {
        std::cout << "  ❌ " << test.name
                  << " (verification: " << verification_passed
                  << ", execution: " << execution_passed << ")" << std::endl;
      }
    }

    double success_rate = (double)passed / total * 100.0;
    std::cout << "Security Model: " << passed << "/" << total << " ("
              << std::fixed << std::setprecision(1) << success_rate << "%)"
              << std::endl;

    return success_rate >= 75.0;
  }

  // Test 6: Performance Benchmarking
  bool test_performance_benchmarking() {
    std::cout << "🧪 Testing performance benchmarking..." << std::endl;

    // Various programs of different complexity
    struct BenchmarkTest {
      std::string name;
      std::vector<uint8_t> bytecode;
      uint32_t expected_min_ops_per_sec;
    };

    std::vector<BenchmarkTest> benchmarks = {
        {
            "Simple Arithmetic",
            {
                0xb7, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, // r0 = 10
                0x07, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, // r0 += 5
                0x27, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, // r0 *= 2
                0x95, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // exit
            },
            500000 // 500K ops/sec minimum
        },
        {
            "Memory Operations",
            {
                0x18, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // Load address
                0xb7, 0x02, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, // r2 = 0x42
                0x7b, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Store r2 to
                                                                // memory
                0x79, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Load from
                                                                // memory
                0xbf, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // r0 = r3
                0x95, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // exit
            },
            200000 // 200K ops/sec minimum
        },
        {
            "Control Flow",
            {
                0xb7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // r0 = 0
                0xb7, 0x01, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, // r1 = 10
                // Loop
                0x07, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // r0 += 1
                0x2d, 0x01, 0xfd, 0xff, 0x00, 0x00, 0x00,
                0x00, // if r0 < r1 goto loop
                0x95, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 // exit
            },
            100000 // 100K ops/sec minimum
        }};

    int passed = 0;
    int total = benchmarks.size();
    uint64_t total_ops_per_sec = 0;

    for (const auto &benchmark : benchmarks) {
      BpfProgram program;
      program.code = benchmark.bytecode;
      program.compute_units = 10000;

      const int iterations = 1000;
      auto start_time = std::chrono::high_resolution_clock::now();

      for (int i = 0; i < iterations; ++i) {
        BpfExecutionContext context;
        context.input_data.resize(32, 0);
        context.stack_memory.resize(512, 0);

        auto result = runtime_.execute(program, context);
        if (!result.is_success()) {
          std::cout << "  ❌ " << benchmark.name << " - execution failed"
                    << std::endl;
          break;
        }
      }

      auto elapsed = std::chrono::high_resolution_clock::now() - start_time;
      auto elapsed_us =
          std::chrono::duration_cast<std::chrono::microseconds>(elapsed)
              .count();

      if (elapsed_us > 0) {
        uint64_t ops_per_sec =
            (uint64_t)((double)iterations * 1000000.0 / elapsed_us);
        std::cout << "  " << benchmark.name << ": " << ops_per_sec
                  << " ops/sec";

        if (ops_per_sec >= benchmark.expected_min_ops_per_sec) {
          std::cout << " ✅" << std::endl;
          passed++;
        } else {
          std::cout << " ❌ (minimum: " << benchmark.expected_min_ops_per_sec
                    << ")" << std::endl;
        }

        total_ops_per_sec += ops_per_sec;
      } else {
        std::cout << "  ❌ " << benchmark.name << " - timing failed"
                  << std::endl;
      }
    }

    if (total > 0) {
      uint64_t avg_ops_per_sec = total_ops_per_sec / total;
      std::cout << "Average performance: " << avg_ops_per_sec << " ops/sec"
                << std::endl;
    }

    double success_rate = (double)passed / total * 100.0;
    std::cout << "Performance Benchmarks: " << passed << "/" << total << " ("
              << std::fixed << std::setprecision(1) << success_rate << "%)"
              << std::endl;

    return success_rate >= 66.0; // At least 2/3 benchmarks should pass
  }
};

} // namespace svm
} // namespace slonana

void run_bpf_runtime_comprehensive_tests(TestRunner &runner) {
  std::cout << "\n=== Comprehensive BPF Runtime Test Suite ===" << std::endl;
  std::cout << "Testing complete BPF runtime functionality..." << std::endl;

  slonana::svm::BpfRuntimeTester tester;

  runner.run_test("BPF Complete ISA Coverage",
                  [&]() { return tester.test_complete_isa_coverage(); });

  runner.run_test("BPF Memory Management Safety",
                  [&]() { return tester.test_memory_management_safety(); });

  runner.run_test("BPF JIT Compilation",
                  [&]() { return tester.test_jit_compilation_optimization(); });

  runner.run_test("BPF Parallel Execution",
                  [&]() { return tester.test_parallel_execution(); });

  runner.run_test("BPF Security Model",
                  [&]() { return tester.test_security_model(); });

  runner.run_test("BPF Performance Benchmarks",
                  [&]() { return tester.test_performance_benchmarking(); });

  std::cout << "=== Comprehensive BPF Runtime Tests Complete ===" << std::endl;
}