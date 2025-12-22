#include "svm/bpf_runtime_enhanced.h"
#include "test_framework.h"
#include <cstring>

using namespace slonana::svm;

/**
 * Test Suite for Enhanced BPF Runtime Features
 */

void test_memory_region_add() {
    EnhancedBpfRuntime runtime;
    
    MemoryRegion region(0x1000, 4096, MemoryPermission::READ_WRITE, "test_region");
    
    EXPECT_NO_THROW(runtime.add_memory_region(region));
}

void test_memory_region_validate_read() {
    EnhancedBpfRuntime runtime;
    
    MemoryRegion region(0x1000, 4096, MemoryPermission::READ, "test_region");
    runtime.add_memory_region(region);
    
    // Should be able to read
    ASSERT_TRUE(runtime.validate_memory_access(0x1000, 100, MemoryPermission::READ));
    
    // Should not be able to write
    ASSERT_FALSE(runtime.validate_memory_access(0x1000, 100, MemoryPermission::WRITE));
}

void test_memory_region_validate_write() {
    EnhancedBpfRuntime runtime;
    
    MemoryRegion region(0x1000, 4096, MemoryPermission::READ_WRITE, "rw_region");
    runtime.add_memory_region(region);
    
    // Should be able to read
    ASSERT_TRUE(runtime.validate_memory_access(0x1000, 100, MemoryPermission::READ));
    
    // Should be able to write
    ASSERT_TRUE(runtime.validate_memory_access(0x1000, 100, MemoryPermission::WRITE));
}

void test_memory_region_boundary_check() {
    EnhancedBpfRuntime runtime;
    
    MemoryRegion region(0x1000, 4096, MemoryPermission::READ_WRITE, "bounded");
    runtime.add_memory_region(region);
    
    // Access within bounds
    ASSERT_TRUE(runtime.validate_memory_access(0x1000, 4096, MemoryPermission::READ));
    
    // Access beyond bounds
    ASSERT_FALSE(runtime.validate_memory_access(0x1000, 4097, MemoryPermission::READ));
    
    // Access before region
    ASSERT_FALSE(runtime.validate_memory_access(0x0FFF, 2, MemoryPermission::READ));
}

void test_memory_region_get() {
    EnhancedBpfRuntime runtime;
    
    MemoryRegion region(0x1000, 4096, MemoryPermission::READ, "test");
    runtime.add_memory_region(region);
    
    const MemoryRegion* found = runtime.get_region(0x1500);
    
    ASSERT_TRUE(found != nullptr);
    ASSERT_EQ(found->start, (uintptr_t)0x1000);
    ASSERT_EQ(found->size, (size_t)4096);
}

void test_memory_region_clear() {
    EnhancedBpfRuntime runtime;
    
    MemoryRegion region(0x1000, 4096, MemoryPermission::READ, "test");
    runtime.add_memory_region(region);
    
    ASSERT_TRUE(runtime.get_region(0x1000) != nullptr);
    
    runtime.clear_regions();
    
    ASSERT_TRUE(runtime.get_region(0x1000) == nullptr);
}

void test_compute_costs_defined() {
    ASSERT_GT(compute_costs::DEFAULT, (uint64_t)0);
    ASSERT_GT(compute_costs::ALU_DIV, compute_costs::ALU_ADD);
    ASSERT_GT(compute_costs::CALL, compute_costs::JUMP);
    ASSERT_EQ(compute_costs::EXIT, (uint64_t)0);
}

void test_get_instruction_cost() {
    // Test ALU operations
    ASSERT_EQ(EnhancedBpfRuntime::get_instruction_cost(0x04), compute_costs::ALU_ADD);
    ASSERT_EQ(EnhancedBpfRuntime::get_instruction_cost(0x34), compute_costs::ALU_DIV);
    
    // Test jump operations
    ASSERT_EQ(EnhancedBpfRuntime::get_instruction_cost(0x85), compute_costs::CALL);
    ASSERT_EQ(EnhancedBpfRuntime::get_instruction_cost(0x95), compute_costs::EXIT);
}

void test_stack_frame_push_pop() {
    StackFrameManager manager;
    
    ASSERT_TRUE(manager.push_frame(0x1000, 0x2000, 100));
    ASSERT_EQ(manager.get_depth(), (size_t)1);
    
    StackFrame frame(0, 0, 0);
    ASSERT_TRUE(manager.pop_frame(frame));
    ASSERT_EQ(frame.return_address, (uintptr_t)0x1000);
    ASSERT_EQ(frame.frame_pointer, (uint64_t)0x2000);
    ASSERT_EQ(frame.compute_units_at_entry, (uint64_t)100);
    ASSERT_EQ(manager.get_depth(), (size_t)0);
}

void test_stack_frame_max_depth() {
    StackFrameManager manager;
    manager.set_max_depth(3);
    
    ASSERT_TRUE(manager.push_frame(0x1000, 0x2000, 100));
    ASSERT_TRUE(manager.push_frame(0x1100, 0x2100, 200));
    ASSERT_TRUE(manager.push_frame(0x1200, 0x2200, 300));
    
    // Should fail - max depth exceeded
    ASSERT_FALSE(manager.push_frame(0x1300, 0x2300, 400));
    
    ASSERT_EQ(manager.get_depth(), (size_t)3);
}

void test_stack_frame_clear() {
    StackFrameManager manager;
    
    manager.push_frame(0x1000, 0x2000, 100);
    manager.push_frame(0x1100, 0x2100, 200);
    
    ASSERT_EQ(manager.get_depth(), (size_t)2);
    
    manager.clear();
    
    ASSERT_EQ(manager.get_depth(), (size_t)0);
}

void test_memory_permission_operators() {
    MemoryPermission rw = MemoryPermission::READ | MemoryPermission::WRITE;
    
    ASSERT_TRUE(has_permission(rw, MemoryPermission::READ));
    ASSERT_TRUE(has_permission(rw, MemoryPermission::WRITE));
    ASSERT_FALSE(has_permission(rw, MemoryPermission::EXECUTE));
    
    ASSERT_TRUE(has_permission(MemoryPermission::ALL, MemoryPermission::EXECUTE));
}

int main() {
    TestRunner runner;
    
    std::cout << "\n=== Enhanced BPF Runtime Tests ===\n" << std::endl;
    
    runner.run_test("Memory Region Add", test_memory_region_add);
    runner.run_test("Memory Region Validate Read", test_memory_region_validate_read);
    runner.run_test("Memory Region Validate Write", test_memory_region_validate_write);
    runner.run_test("Memory Region Boundary Check", test_memory_region_boundary_check);
    runner.run_test("Memory Region Get", test_memory_region_get);
    runner.run_test("Memory Region Clear", test_memory_region_clear);
    runner.run_test("Compute Costs Defined", test_compute_costs_defined);
    runner.run_test("Get Instruction Cost", test_get_instruction_cost);
    runner.run_test("Stack Frame Push/Pop", test_stack_frame_push_pop);
    runner.run_test("Stack Frame Max Depth", test_stack_frame_max_depth);
    runner.run_test("Stack Frame Clear", test_stack_frame_clear);
    runner.run_test("Memory Permission Operators", test_memory_permission_operators);
    
    runner.print_summary();
    
    return runner.all_passed() ? 0 : 1;
}
