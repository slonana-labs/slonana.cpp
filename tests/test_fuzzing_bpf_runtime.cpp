// Fuzzing-Style Tests for BPF Runtime
// Purpose: Discover bugs through randomized, malformed, and extreme inputs

#include <gtest/gtest.h>
#include "../include/svm/bpf_runtime_enhanced.h"
#include <vector>
#include <random>
#include <thread>
#include <atomic>
#include <chrono>

using namespace slonana::svm;

class FuzzingBPFRuntimeTest : public ::testing::Test {
protected:
    EnhancedBpfRuntime runtime;
    std::mt19937_64 rng{std::random_device{}()};
    
    uint64_t random_address() {
        std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);
        return dist(rng);
    }
    
    size_t random_size() {
        std::uniform_int_distribution<size_t> dist(0, 1024 * 1024);
        return dist(rng);
    }
    
    MemoryPermission random_permission() {
        std::uniform_int_distribution<int> dist(0, 7);
        return static_cast<MemoryPermission>(dist(rng));
    }
};

// Test 1: Random region additions
TEST_F(FuzzingBPFRuntimeTest, RandomRegionAdditions) {
    for (int i = 0; i < 1000; ++i) {
        uint64_t start = random_address();
        size_t size = random_size();
        MemoryPermission perm = random_permission();
        
        try {
            MemoryRegion region(start, size, perm, "fuzz_region");
            runtime.add_memory_region(region);
        } catch (...) {
            // Expected for invalid combinations
        }
    }
    SUCCEED(); // Should not crash
}

// Test 2: Random validation attempts
TEST_F(FuzzingBPFRuntimeTest, RandomValidationAttempts) {
    // Add some valid regions first
    runtime.add_memory_region(MemoryRegion(0x1000, 4096, MemoryPermission::READ, "r1"));
    runtime.add_memory_region(MemoryRegion(0x10000, 8192, MemoryPermission::WRITE, "r2"));
    
    for (int i = 0; i < 5000; ++i) {
        uint64_t addr = random_address();
        size_t size = random_size();
        MemoryPermission perm = random_permission();
        
        bool result = runtime.validate_memory_access(addr, size, perm);
        // Just verify it completes without crashing
        (void)result;
    }
    SUCCEED();
}

// Test 3: Extreme address wrapping
TEST_F(FuzzingBPFRuntimeTest, ExtremeAddressWrapping) {
    // Region at end of address space
    try {
        MemoryRegion region(UINT64_MAX - 1000, 2000, MemoryPermission::READ, "wrap");
        runtime.add_memory_region(region);
        
        // Try to access beyond wraparound point
        bool result = runtime.validate_memory_access(UINT64_MAX - 10, 20, MemoryPermission::READ);
        EXPECT_FALSE(result); // Should reject wraparound
    } catch (...) {
        SUCCEED(); // Or reject at region creation
    }
}

// Test 4: Stack frame fuzzing
TEST_F(FuzzingBPFRuntimeTest, StackFrameFuzzing) {
    StackFrameManager stack;
    
    for (int i = 0; i < 10000; ++i) {
        if (rng() % 3 == 0) {
            // Push
            stack.push_frame(random_address(), random_address(), random_size());
        } else if (rng() % 2 == 0 && stack.get_current_depth() > 0) {
            // Pop
            try {
                stack.pop_frame();
            } catch (...) {
                // May throw if empty
            }
        } else {
            // Query
            (void)stack.get_current_depth();
            (void)stack.is_max_depth_exceeded();
        }
    }
    SUCCEED();
}

// Test 5: Overlapping region fuzzing
TEST_F(FuzzingBPFRuntimeTest, OverlappingRegionFuzzing) {
    std::vector<MemoryRegion> regions;
    
    // Create many potentially overlapping regions
    for (int i = 0; i < 500; ++i) {
        uint64_t base = (rng() % 100) * 1024; // Cluster addresses
        size_t size = (rng() % 50 + 1) * 1024;
        MemoryPermission perm = random_permission();
        
        try {
            MemoryRegion region(base, size, perm, "overlap_" + std::to_string(i));
            runtime.add_memory_region(region);
            regions.push_back(region);
        } catch (...) {
            // Expected for overlaps
        }
    }
    
    // Now validate random accesses
    for (int i = 0; i < 1000; ++i) {
        uint64_t addr = (rng() % 100) * 1024;
        size_t size = (rng() % 10 + 1) * 1024;
        runtime.validate_memory_access(addr, size, MemoryPermission::READ);
    }
    SUCCEED();
}

// Test 6: Permission bit fuzzing
TEST_F(FuzzingBPFRuntimeTest, PermissionBitFuzzing) {
    runtime.add_memory_region(MemoryRegion(0x1000, 4096, 
        static_cast<MemoryPermission>(7), "rwx")); // All permissions
    
    // Test all 256 possible permission values (including invalid ones)
    for (int perm = 0; perm < 256; ++perm) {
        bool result = runtime.validate_memory_access(0x1000, 100, 
            static_cast<MemoryPermission>(perm));
        // Should handle invalid permissions gracefully
        (void)result;
    }
    SUCCEED();
}

// Test 7: Instruction cost fuzzing
TEST_F(FuzzingBPFRuntimeTest, InstructionCostFuzzing) {
    for (int i = 0; i < 10000; ++i) {
        uint8_t opcode = rng() % 256;
        uint64_t cost = runtime.get_instruction_cost(opcode);
        EXPECT_GE(cost, 0); // Should return valid cost or default
    }
}

// Test 8: Concurrent region modifications
TEST_F(FuzzingBPFRuntimeTest, ConcurrentRegionModifications) {
    const int num_threads = 10;
    std::atomic<int> errors{0};
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < 100; ++i) {
                try {
                    uint64_t base = t * 100000 + i * 1000;
                    MemoryRegion region(base, 512, MemoryPermission::READ, 
                        "thread_" + std::to_string(t) + "_" + std::to_string(i));
                    runtime.add_memory_region(region);
                    
                    // Validate some accesses
                    for (int j = 0; j < 10; ++j) {
                        runtime.validate_memory_access(base + j * 50, 32, MemoryPermission::READ);
                    }
                } catch (...) {
                    errors++;
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Some errors acceptable due to race conditions, but should not crash
    SUCCEED();
}

// Test 9: Zero-size accesses everywhere
TEST_F(FuzzingBPFRuntimeTest, ZeroSizeAccessesEverywhere) {
    runtime.add_memory_region(MemoryRegion(0x1000, 4096, MemoryPermission::READ, "r1"));
    
    for (int i = 0; i < 1000; ++i) {
        uint64_t addr = random_address();
        bool result = runtime.validate_memory_access(addr, 0, MemoryPermission::READ);
        // Zero-size should be valid or rejected consistently
        (void)result;
    }
    SUCCEED();
}

// Test 10: Maximum region count stress
TEST_F(FuzzingBPFRuntimeTest, MaximumRegionCount) {
    int successful = 0;
    for (int i = 0; i < 10000; ++i) {
        try {
            MemoryRegion region(i * 10000, 1024, MemoryPermission::READ, 
                "region_" + std::to_string(i));
            runtime.add_memory_region(region);
            successful++;
        } catch (...) {
            break; // Hit limit
        }
    }
    
    EXPECT_GT(successful, 100); // Should support at least 100 regions
}

// Test 11: Alternating add/remove pattern
TEST_F(FuzzingBPFRuntimeTest, AlternatingAddRemove) {
    for (int i = 0; i < 1000; ++i) {
        // Add region
        try {
            MemoryRegion region(i * 1000, 512, MemoryPermission::READ, "temp");
            runtime.add_memory_region(region);
        } catch (...) {}
        
        // Validate
        runtime.validate_memory_access(i * 1000 + 256, 64, MemoryPermission::READ);
    }
    SUCCEED();
}

// Test 12: Stack depth fuzzing
TEST_F(FuzzingBPFRuntimeTest, StackDepthFuzzing) {
    StackFrameManager stack;
    
    // Push to random depths
    for (int trial = 0; trial < 100; ++trial) {
        int target_depth = rng() % 200;
        
        // Push to target depth
        for (int i = 0; i < target_depth; ++i) {
            try {
                stack.push_frame(0x1000 + i, 0x2000 + i, 100);
            } catch (...) {
                break; // Hit limit
            }
        }
        
        // Pop all
        while (stack.get_current_depth() > 0) {
            stack.pop_frame();
        }
    }
    SUCCEED();
}

// Test 13: Malformed memory region parameters
TEST_F(FuzzingBPFRuntimeTest, MalformedRegionParameters) {
    std::vector<std::tuple<uint64_t, size_t, std::string>> test_cases = {
        {0, 0, "zero_size"},
        {0, UINT64_MAX, "max_size"},
        {UINT64_MAX, 1, "max_address"},
        {1, UINT64_MAX, "overflow_size"},
        {0xFFFFFFFF00000000ULL, 0xFFFFFFFFULL, "near_overflow"},
    };
    
    for (const auto& [start, size, name] : test_cases) {
        try {
            MemoryRegion region(start, size, MemoryPermission::READ, name);
            runtime.add_memory_region(region);
        } catch (...) {
            // Expected for many cases
        }
    }
    SUCCEED();
}

// Test 14: Rapid permission changes
TEST_F(FuzzingBPFRuntimeTest, RapidPermissionChanges) {
    runtime.add_memory_region(MemoryRegion(0x1000, 4096, 
        static_cast<MemoryPermission>(7), "multi_perm"));
    
    MemoryPermission perms[] = {
        MemoryPermission::READ,
        MemoryPermission::WRITE,
        MemoryPermission::EXECUTE,
        static_cast<MemoryPermission>(3), // READ | WRITE
        static_cast<MemoryPermission>(5), // READ | EXECUTE
    };
    
    for (int i = 0; i < 10000; ++i) {
        MemoryPermission perm = perms[rng() % 5];
        runtime.validate_memory_access(0x1500, 256, perm);
    }
    SUCCEED();
}

// Test 15: Boundary spanning accesses
TEST_F(FuzzingBPFRuntimeTest, BoundarySpanningAccesses) {
    runtime.add_memory_region(MemoryRegion(0x1000, 4096, MemoryPermission::READ, "r1"));
    runtime.add_memory_region(MemoryRegion(0x2000, 4096, MemoryPermission::READ, "r2"));
    
    // Access that spans from end of first region to start of second
    for (size_t offset = 0; offset < 100; ++offset) {
        for (size_t size = 1; size <= 200; ++size) {
            uint64_t addr = 0x1000 + 4096 - offset;
            runtime.validate_memory_access(addr, size, MemoryPermission::READ);
        }
    }
    SUCCEED();
}

// Test 16: Time-based race conditions
TEST_F(FuzzingBPFRuntimeTest, TimeBasedRaceConditions) {
    std::atomic<bool> stop{false};
    
    auto reader = [&]() {
        while (!stop) {
            runtime.validate_memory_access(0x1000, 100, MemoryPermission::READ);
        }
    };
    
    auto writer = [&]() {
        int counter = 0;
        while (!stop) {
            try {
                MemoryRegion region(0x1000 + (counter % 100) * 1000, 512,
                    MemoryPermission::READ, "race_" + std::to_string(counter++));
                runtime.add_memory_region(region);
            } catch (...) {}
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    };
    
    std::thread t1(reader);
    std::thread t2(writer);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stop = true;
    
    t1.join();
    t2.join();
    SUCCEED();
}

// Test 17: Stack frame with invalid pointers
TEST_F(FuzzingBPFRuntimeTest, StackFrameInvalidPointers) {
    StackFrameManager stack;
    
    std::vector<uint64_t> invalid_addresses = {
        0,
        1,
        UINT64_MAX,
        UINT64_MAX - 1,
        0xDEADBEEF,
        0xFFFFFFFF,
    };
    
    for (auto addr : invalid_addresses) {
        try {
            stack.push_frame(addr, addr, 100);
            stack.pop_frame();
        } catch (...) {
            // May reject invalid addresses
        }
    }
    SUCCEED();
}

// Test 18: Compute unit overflow
TEST_F(FuzzingBPFRuntimeTest, ComputeUnitOverflow) {
    StackFrameManager stack;
    
    // Try to overflow compute units
    for (int i = 0; i < 100; ++i) {
        try {
            stack.push_frame(0x1000, 0x2000, UINT64_MAX);
        } catch (...) {
            // Expected if overflow detected
        }
    }
    
    while (stack.get_current_depth() > 0) {
        stack.pop_frame();
    }
    SUCCEED();
}

// Test 19: Mixed valid/invalid operations
TEST_F(FuzzingBPFRuntimeTest, MixedValidInvalidOperations) {
    runtime.add_memory_region(MemoryRegion(0x1000, 4096, MemoryPermission::READ, "valid"));
    
    for (int i = 0; i < 1000; ++i) {
        if (rng() % 2) {
            // Valid operation
            runtime.validate_memory_access(0x1500, 256, MemoryPermission::READ);
        } else {
            // Invalid operation
            runtime.validate_memory_access(0x9999, 256, MemoryPermission::WRITE);
        }
    }
    SUCCEED();
}

// Test 20: Exhaustive opcode coverage
TEST_F(FuzzingBPFRuntimeTest, ExhaustiveOpcodeCoverage) {
    std::map<uint64_t, int> cost_histogram;
    
    for (int opcode = 0; opcode < 256; ++opcode) {
        uint64_t cost = runtime.get_instruction_cost(static_cast<uint8_t>(opcode));
        cost_histogram[cost]++;
    }
    
    EXPECT_GT(cost_histogram.size(), 1); // Should have different costs
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
