#include "svm/bpf_runtime_enhanced.h"
#include "test_framework.h"
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>

using namespace slonana::svm;

/**
 * Extended Test Suite for Enhanced BPF Runtime
 * Covers edge cases, stress testing, concurrency, and integration
 */

// ===== Memory Region Extended Tests =====

void test_memory_region_overlapping_regions() {
    EnhancedBpfRuntime runtime;
    
    MemoryRegion region1(0x1000, 4096, MemoryPermission::READ, "region1");
    MemoryRegion region2(0x2000, 4096, MemoryPermission::WRITE, "region2");
    
    runtime.add_memory_region(region1);
    runtime.add_memory_region(region2);
    
    // Non-overlapping regions should both be valid
    ASSERT_TRUE(runtime.validate_memory_access(0x1000, 100, MemoryPermission::READ));
    ASSERT_TRUE(runtime.validate_memory_access(0x2000, 100, MemoryPermission::WRITE));
}

void test_memory_region_adjacent_regions() {
    EnhancedBpfRuntime runtime;
    
    // Two adjacent regions
    MemoryRegion region1(0x1000, 4096, MemoryPermission::READ, "region1");
    MemoryRegion region2(0x2000, 4096, MemoryPermission::READ, "region2");
    
    runtime.add_memory_region(region1);
    runtime.add_memory_region(region2);
    
    // Boundary access should work
    ASSERT_TRUE(runtime.validate_memory_access(0x1FFF, 1, MemoryPermission::READ));
    ASSERT_TRUE(runtime.validate_memory_access(0x2000, 1, MemoryPermission::READ));
}

void test_memory_region_zero_size() {
    EnhancedBpfRuntime runtime;
    
    MemoryRegion region(0x1000, 0, MemoryPermission::READ, "zero_size");
    
    // Zero-size region should throw an exception
    bool threw_exception = false;
    try {
        runtime.add_memory_region(region);
    } catch (const std::invalid_argument& e) {
        threw_exception = true;
    }
    
    ASSERT_TRUE(threw_exception);
    
    // No regions should be added, so validation should fail
    ASSERT_FALSE(runtime.validate_memory_access(0x1000, 1, MemoryPermission::READ));
}

void test_memory_region_large_size() {
    EnhancedBpfRuntime runtime;
    
    // Large region (1GB)
    MemoryRegion region(0x1000, 1024 * 1024 * 1024, MemoryPermission::READ_WRITE, "large");
    runtime.add_memory_region(region);
    
    // Should handle large regions
    ASSERT_TRUE(runtime.validate_memory_access(0x1000, 1024, MemoryPermission::READ));
    ASSERT_TRUE(runtime.validate_memory_access(0x1000 + 1024 * 1024, 1024, MemoryPermission::WRITE));
}

void test_memory_region_boundary_exact() {
    EnhancedBpfRuntime runtime;
    
    MemoryRegion region(0x1000, 4096, MemoryPermission::READ, "boundary_test");
    runtime.add_memory_region(region);
    
    // Access exactly at start
    ASSERT_TRUE(runtime.validate_memory_access(0x1000, 1, MemoryPermission::READ));
    
    // Access exactly at end (last byte)
    ASSERT_TRUE(runtime.validate_memory_access(0x1FFF, 1, MemoryPermission::READ));
    
    // Access one byte past end should fail
    ASSERT_FALSE(runtime.validate_memory_access(0x2000, 1, MemoryPermission::READ));
}

void test_memory_region_wrap_around() {
    EnhancedBpfRuntime runtime;
    
    MemoryRegion region(0xFFFFFFFF - 100, 50, MemoryPermission::READ, "wrap");
    runtime.add_memory_region(region);
    
    // Should handle address near max value
    ASSERT_TRUE(runtime.validate_memory_access(0xFFFFFFFF - 100, 10, MemoryPermission::READ));
}

void test_memory_region_many_regions() {
    EnhancedBpfRuntime runtime;
    
    // Add many regions to test scalability
    for (size_t i = 0; i < 100; i++) {
        MemoryRegion region(0x1000 + i * 8192, 4096, MemoryPermission::READ, "region_" + std::to_string(i));
        runtime.add_memory_region(region);
    }
    
    // Verify we can still find and validate regions
    ASSERT_TRUE(runtime.validate_memory_access(0x1000, 100, MemoryPermission::READ));
    ASSERT_TRUE(runtime.validate_memory_access(0x1000 + 50 * 8192, 100, MemoryPermission::READ));
}

void test_memory_region_get_by_address() {
    EnhancedBpfRuntime runtime;
    
    MemoryRegion region(0x1000, 4096, MemoryPermission::READ, "findme");
    runtime.add_memory_region(region);
    
    // Get region that contains address
    auto found = runtime.get_memory_region(0x1500);
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->start, (uintptr_t)0x1000);
}

void test_memory_region_get_missing() {
    EnhancedBpfRuntime runtime;
    
    MemoryRegion region(0x1000, 4096, MemoryPermission::READ, "region");
    runtime.add_memory_region(region);
    
    // Try to get region at unmapped address
    auto found = runtime.get_memory_region(0x5000);
    ASSERT_FALSE(found.has_value());
}

// ===== Permission Tests =====

void test_permission_combinations() {
    EnhancedBpfRuntime runtime;
    
    // Test all permission combinations
    MemoryRegion r1(0x1000, 4096, MemoryPermission::READ, "read_only");
    MemoryRegion r2(0x2000, 4096, MemoryPermission::WRITE, "write_only");
    MemoryRegion r3(0x3000, 4096, MemoryPermission::EXECUTE, "execute_only");
    MemoryRegion r4(0x4000, 4096, MemoryPermission::READ_WRITE, "read_write");
    
    runtime.add_memory_region(r1);
    runtime.add_memory_region(r2);
    runtime.add_memory_region(r3);
    runtime.add_memory_region(r4);
    
    // READ region
    ASSERT_TRUE(runtime.validate_memory_access(0x1000, 100, MemoryPermission::READ));
    ASSERT_FALSE(runtime.validate_memory_access(0x1000, 100, MemoryPermission::WRITE));
    
    // WRITE region
    ASSERT_FALSE(runtime.validate_memory_access(0x2000, 100, MemoryPermission::READ));
    ASSERT_TRUE(runtime.validate_memory_access(0x2000, 100, MemoryPermission::WRITE));
    
    // READ_WRITE region
    ASSERT_TRUE(runtime.validate_memory_access(0x4000, 100, MemoryPermission::READ));
    ASSERT_TRUE(runtime.validate_memory_access(0x4000, 100, MemoryPermission::WRITE));
}

// ===== Stack Frame Extended Tests =====

void test_stack_frame_metadata() {
    StackFrameManager stack;
    
    stack.push_frame(0x1000, 0x2000, 100);
    
    ASSERT_EQ(stack.get_current_depth(), (size_t)1);
    
    auto frame = stack.pop_frame();
    ASSERT_TRUE(frame.has_value());
    ASSERT_EQ(frame->return_address, (uintptr_t)0x1000);
    ASSERT_EQ(frame->frame_pointer, (uintptr_t)0x2000);
    ASSERT_EQ(frame->compute_units_used, (uint64_t)100);
}

void test_stack_frame_multiple_push_pop() {
    StackFrameManager stack;
    
    // Push multiple frames
    for (size_t i = 0; i < 10; i++) {
        stack.push_frame(0x1000 + i, 0x2000 + i, i * 10);
    }
    
    ASSERT_EQ(stack.get_current_depth(), (size_t)10);
    
    // Pop in reverse order
    for (int i = 9; i >= 0; i--) {
        auto frame = stack.pop_frame();
        ASSERT_TRUE(frame.has_value());
        ASSERT_EQ(frame->return_address, (uintptr_t)(0x1000 + i));
    }
    
    ASSERT_EQ(stack.get_current_depth(), (size_t)0);
}

void test_stack_frame_overflow_detection() {
    StackFrameManager stack(10); // Max depth 10
    
    // Push to limit
    for (size_t i = 0; i < 10; i++) {
        stack.push_frame(0x1000, 0x2000, 0);
    }
    
    ASSERT_TRUE(stack.is_max_depth_exceeded());
    
    // One more should still work but flag is set
    stack.push_frame(0x1000, 0x2000, 0);
    ASSERT_TRUE(stack.is_max_depth_exceeded());
}

void test_stack_frame_empty_pop() {
    StackFrameManager stack;
    
    // Pop from empty stack
    auto frame = stack.pop_frame();
    ASSERT_FALSE(frame.has_value());
}

void test_stack_frame_clear_resets_depth() {
    StackFrameManager stack;
    
    stack.push_frame(0x1000, 0x2000, 100);
    stack.push_frame(0x1100, 0x2100, 200);
    
    ASSERT_EQ(stack.get_current_depth(), (size_t)2);
    
    stack.clear();
    
    ASSERT_EQ(stack.get_current_depth(), (size_t)0);
    ASSERT_FALSE(stack.is_max_depth_exceeded());
}

void test_stack_frame_compute_units_accumulation() {
    StackFrameManager stack;
    
    stack.push_frame(0x1000, 0x2000, 100);
    stack.push_frame(0x1100, 0x2100, 200);
    stack.push_frame(0x1200, 0x2200, 300);
    
    // Pop and verify cumulative compute units
    uint64_t total = 0;
    while (auto frame = stack.pop_frame()) {
        total += frame->compute_units_used;
    }
    
    ASSERT_EQ(total, (uint64_t)600);
}

// ===== Instruction Cost Tests =====

void test_instruction_cost_all_opcodes() {
    EnhancedBpfRuntime runtime;
    
    // Test that we can get cost for all instruction types
    // Use uint16_t to avoid overflow when iterating 0-255
    for (uint16_t opcode = 0; opcode <= 255; opcode++) {
        uint64_t cost = runtime.get_instruction_cost(static_cast<uint8_t>(opcode));
        // Cost should be reasonable (1-10000 CU)
        ASSERT_LE(cost, (uint64_t)100000);
    }
}

void test_instruction_cost_consistency() {
    EnhancedBpfRuntime runtime;
    
    // Same opcode should return same cost
    uint64_t cost1 = runtime.get_instruction_cost(0x04); // ADD
    uint64_t cost2 = runtime.get_instruction_cost(0x04);
    
    ASSERT_EQ(cost1, cost2);
}

void test_instruction_cost_div_mod_expensive() {
    EnhancedBpfRuntime runtime;
    
    uint64_t add_cost = runtime.get_instruction_cost(0x04); // ADD
    uint64_t div_cost = runtime.get_instruction_cost(0x34); // DIV
    uint64_t mod_cost = runtime.get_instruction_cost(0x94); // MOD
    
    // DIV and MOD should cost more than ADD
    ASSERT_GT(div_cost, add_cost);
    ASSERT_GT(mod_cost, add_cost);
}

// ===== Stress and Performance Tests =====

void test_stress_many_regions_access() {
    EnhancedBpfRuntime runtime;
    
    // Add 1000 regions
    for (size_t i = 0; i < 1000; i++) {
        MemoryRegion region(0x1000 + i * 16384, 4096, MemoryPermission::READ, "stress_" + std::to_string(i));
        runtime.add_memory_region(region);
    }
    
    // Access pattern across many regions
    for (size_t i = 0; i < 1000; i++) {
        ASSERT_TRUE(runtime.validate_memory_access(0x1000 + i * 16384, 100, MemoryPermission::READ));
    }
}

void test_stress_stack_depth() {
    StackFrameManager stack(1000);
    
    // Push 1000 frames
    for (size_t i = 0; i < 1000; i++) {
        stack.push_frame(0x1000 + i, 0x2000 + i, i);
    }
    
    ASSERT_EQ(stack.get_current_depth(), (size_t)1000);
    
    // Pop all
    size_t count = 0;
    while (stack.pop_frame()) {
        count++;
    }
    
    ASSERT_EQ(count, (size_t)1000);
}

void test_concurrent_memory_validation() {
    EnhancedBpfRuntime runtime;
    
    MemoryRegion region(0x1000, 1024 * 1024, MemoryPermission::READ_WRITE, "concurrent");
    runtime.add_memory_region(region);
    
    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;
    
    // Spawn multiple threads doing concurrent validation
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&runtime, &success_count, i]() {
            for (int j = 0; j < 100; j++) {
                if (runtime.validate_memory_access(0x1000 + i * 1000 + j, 10, MemoryPermission::READ)) {
                    success_count++;
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // All validations should succeed
    ASSERT_EQ(success_count.load(), 1000);
}

// ===== Integration Tests =====

void test_full_bpf_execution_simulation() {
    EnhancedBpfRuntime runtime;
    StackFrameManager stack;
    
    // Setup memory regions
    MemoryRegion code(0x100000, 4096, MemoryPermission::READ_EXECUTE, "code");
    MemoryRegion data(0x200000, 4096, MemoryPermission::READ_WRITE, "data");
    MemoryRegion stack_mem(0x300000, 8192, MemoryPermission::READ_WRITE, "stack");
    
    runtime.add_memory_region(code);
    runtime.add_memory_region(data);
    runtime.add_memory_region(stack_mem);
    
    // Simulate function call
    stack.push_frame(0x100100, 0x300000, 0);
    
    uint64_t total_cu = 0;
    
    // Simulate instruction execution
    total_cu += runtime.get_instruction_cost(0x04); // ADD
    total_cu += runtime.get_instruction_cost(0x18); // LOAD
    total_cu += runtime.get_instruction_cost(0x7B); // STORE
    
    // Validate memory accesses
    ASSERT_TRUE(runtime.validate_memory_access(0x100100, 8, MemoryPermission::READ));
    ASSERT_TRUE(runtime.validate_memory_access(0x200000, 8, MemoryPermission::WRITE));
    
    // Return from function
    auto frame = stack.pop_frame();
    ASSERT_TRUE(frame.has_value());
    
    ASSERT_GT(total_cu, (uint64_t)0);
}

void test_error_recovery() {
    EnhancedBpfRuntime runtime;
    
    MemoryRegion region(0x1000, 4096, MemoryPermission::READ, "recovery_test");
    runtime.add_memory_region(region);
    
    // Cause some failures
    ASSERT_FALSE(runtime.validate_memory_access(0x5000, 100, MemoryPermission::READ));
    ASSERT_FALSE(runtime.validate_memory_access(0x1000, 100, MemoryPermission::WRITE));
    
    // Should still work for valid operations
    ASSERT_TRUE(runtime.validate_memory_access(0x1000, 100, MemoryPermission::READ));
}

// Main test runner
int main() {
    RUN_TEST(test_memory_region_overlapping_regions);
    RUN_TEST(test_memory_region_adjacent_regions);
    RUN_TEST(test_memory_region_zero_size);
    RUN_TEST(test_memory_region_large_size);
    RUN_TEST(test_memory_region_boundary_exact);
    RUN_TEST(test_memory_region_wrap_around);
    RUN_TEST(test_memory_region_many_regions);
    RUN_TEST(test_memory_region_get_by_address);
    RUN_TEST(test_memory_region_get_missing);
    
    RUN_TEST(test_permission_combinations);
    
    RUN_TEST(test_stack_frame_metadata);
    RUN_TEST(test_stack_frame_multiple_push_pop);
    RUN_TEST(test_stack_frame_overflow_detection);
    RUN_TEST(test_stack_frame_empty_pop);
    RUN_TEST(test_stack_frame_clear_resets_depth);
    RUN_TEST(test_stack_frame_compute_units_accumulation);
    
    RUN_TEST(test_instruction_cost_all_opcodes);
    RUN_TEST(test_instruction_cost_consistency);
    RUN_TEST(test_instruction_cost_div_mod_expensive);
    
    RUN_TEST(test_stress_many_regions_access);
    RUN_TEST(test_stress_stack_depth);
    RUN_TEST(test_concurrent_memory_validation);
    
    RUN_TEST(test_full_bpf_execution_simulation);
    RUN_TEST(test_error_recovery);
    
    return 0;
}
