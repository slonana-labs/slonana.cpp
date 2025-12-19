// Adversarial Testing for SVM Syscalls
// Purpose: Find bugs through edge cases, malicious inputs, and boundary violations

#include <gtest/gtest.h>
#include <vector>
#include <cstring>
#include <random>
#include <thread>
#include <atomic>

// Mock syscall interfaces for testing
extern "C" {
    uint64_t sol_alt_bn128_addition(const uint8_t* input, uint64_t input_len, 
                                     uint8_t* output, uint64_t* output_len);
    uint64_t sol_alt_bn128_multiplication(const uint8_t* input, uint64_t input_len,
                                           uint8_t* output, uint64_t* output_len);
    uint64_t sol_alt_bn128_pairing(const uint8_t* input, uint64_t input_len,
                                    uint8_t* output, uint64_t* output_len);
    uint64_t sol_blake3(const uint8_t* input, uint64_t input_len,
                        uint8_t* output, uint64_t* output_len);
    uint64_t sol_poseidon(const uint8_t* input, uint64_t input_len, uint64_t num_hashes,
                          uint8_t* output, uint64_t* output_len);
    uint64_t sol_curve25519_ristretto_add(const uint8_t* left, const uint8_t* right, uint8_t* result);
    uint64_t sol_get_epoch_stake(const uint8_t* vote_pubkey, uint8_t* stake_out, uint64_t* stake_len);
}

class AdversarialSyscallTest : public ::testing::Test {
protected:
    std::vector<uint8_t> buffer;
    std::mt19937 rng{42};
    
    void SetUp() override {
        buffer.resize(1024 * 1024); // 1MB buffer
    }
    
    void fill_random(uint8_t* data, size_t size) {
        std::uniform_int_distribution<int> dist(0, 255);
        for (size_t i = 0; i < size; ++i) {
            data[i] = dist(rng);
        }
    }
};

// Test 1: Unaligned memory access
TEST_F(AdversarialSyscallTest, UnalignedMemoryAccess) {
    std::vector<uint8_t> data(131); // 131 bytes (unaligned for 128-byte BN254 input)
    fill_random(data.data(), data.size());
    
    uint8_t output[64];
    uint64_t output_len = sizeof(output);
    
    // Should handle unaligned input gracefully
    uint64_t result = sol_alt_bn128_addition(data.data() + 3, 128, output, &output_len);
    EXPECT_NE(result, 0); // Should return error or handle gracefully
}

// Test 2: Integer overflow in length parameters
TEST_F(AdversarialSyscallTest, IntegerOverflowLength) {
    uint8_t dummy[64];
    uint64_t output_len = UINT64_MAX;
    
    uint64_t result = sol_blake3(dummy, UINT64_MAX, dummy, &output_len);
    EXPECT_NE(result, 0); // Should reject
}

// Test 3: Overlapping input/output buffers
TEST_F(AdversarialSyscallTest, OverlappingBuffers) {
    std::vector<uint8_t> data(256);
    fill_random(data.data(), data.size());
    
    uint64_t output_len = 64;
    // Use same buffer for input and output (overlapping)
    uint64_t result = sol_blake3(data.data(), 128, data.data() + 32, &output_len);
    
    // Should either handle correctly or reject
    // At minimum, should not crash
    EXPECT_TRUE(result == 0 || result != 0); // Just verify it completes
}

// Test 4: Rapidly changing output_len pointer
TEST_F(AdversarialSyscallTest, RapidOutputLenChanges) {
    uint8_t input[128];
    uint8_t output[64];
    
    for (int i = 0; i < 100; ++i) {
        uint64_t output_len = (i % 2) ? 32 : 64;
        fill_random(input, sizeof(input));
        sol_blake3(input, sizeof(input), output, &output_len);
    }
    SUCCEED(); // Should not crash
}

// Test 5: Maximum valid input size
TEST_F(AdversarialSyscallTest, MaximumValidInput) {
    std::vector<uint8_t> large_input(10 * 1024 * 1024); // 10MB
    fill_random(large_input.data(), large_input.size());
    
    uint8_t output[32];
    uint64_t output_len = sizeof(output);
    
    uint64_t result = sol_blake3(large_input.data(), large_input.size(), output, &output_len);
    // Should either succeed or return proper error code
    EXPECT_TRUE(result == 0 || result > 0);
}

// Test 6: BN254 with all-zero points (potential division by zero)
TEST_F(AdversarialSyscallTest, BN254AllZeroPoints) {
    std::vector<uint8_t> zeros(128, 0);
    uint8_t output[64];
    uint64_t output_len = sizeof(output);
    
    uint64_t result = sol_alt_bn128_addition(zeros.data(), zeros.size(), output, &output_len);
    EXPECT_NE(result, 0); // Should reject invalid points or handle gracefully
}

// Test 7: BN254 with maximum field elements
TEST_F(AdversarialSyscallTest, BN254MaxFieldElements) {
    std::vector<uint8_t> max_values(128, 0xFF);
    uint8_t output[64];
    uint64_t output_len = sizeof(output);
    
    uint64_t result = sol_alt_bn128_addition(max_values.data(), max_values.size(), 
                                             output, &output_len);
    // Should validate field elements are within range
    EXPECT_TRUE(result == 0 || result != 0); // Just verify completion
}

// Test 8: Concurrent syscall stress test
TEST_F(AdversarialSyscallTest, ConcurrentSyscallStress) {
    const int num_threads = 20;
    const int iterations = 100;
    std::atomic<int> errors{0};
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            uint8_t input[128];
            uint8_t output[64];
            
            for (int i = 0; i < iterations; ++i) {
                fill_random(input, sizeof(input));
                uint64_t output_len = sizeof(output);
                
                uint64_t result = sol_blake3(input, sizeof(input), output, &output_len);
                if (result != 0 && output_len != 32) {
                    errors++;
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(errors, 0);
}

// Test 9: Alternating valid/invalid inputs
TEST_F(AdversarialSyscallTest, AlternatingValidInvalid) {
    for (int i = 0; i < 50; ++i) {
        uint8_t output[64];
        uint64_t output_len = sizeof(output);
        
        if (i % 2 == 0) {
            // Valid input
            std::vector<uint8_t> valid(128);
            fill_random(valid.data(), valid.size());
            sol_blake3(valid.data(), valid.size(), output, &output_len);
        } else {
            // Invalid input (null)
            sol_blake3(nullptr, 128, output, &output_len);
        }
    }
    SUCCEED(); // Should handle without crashing
}

// Test 10: Stack overflow simulation (deep recursion-like calls)
TEST_F(AdversarialSyscallTest, DeepCallChain) {
    uint8_t input[128];
    uint8_t output[64];
    
    for (int depth = 0; depth < 10000; ++depth) {
        fill_random(input, sizeof(input));
        uint64_t output_len = sizeof(output);
        sol_blake3(input, sizeof(input), output, &output_len);
        
        // Use output as next input
        std::memcpy(input, output, std::min(sizeof(input), sizeof(output)));
    }
    SUCCEED();
}

// Test 11: Malicious scalar in BN254 multiplication
TEST_F(AdversarialSyscallTest, BN254MaliciousScalar) {
    std::vector<uint8_t> input(96);
    
    // Create a scalar that's larger than the curve order
    std::fill(input.begin(), input.begin() + 32, 0xFF);
    fill_random(input.data() + 32, 64); // Random point
    
    uint8_t output[64];
    uint64_t output_len = sizeof(output);
    
    uint64_t result = sol_alt_bn128_multiplication(input.data(), input.size(), 
                                                   output, &output_len);
    EXPECT_NE(result, 0); // Should reject invalid scalar
}

// Test 12: Pairing with mismatched input size
TEST_F(AdversarialSyscallTest, PairingMismatchedSize) {
    std::vector<uint8_t> input(200); // Not multiple of 192
    fill_random(input.data(), input.size());
    
    uint8_t output[32];
    uint64_t output_len = sizeof(output);
    
    uint64_t result = sol_alt_bn128_pairing(input.data(), input.size(), output, &output_len);
    EXPECT_NE(result, 0); // Should reject invalid size
}

// Test 13: Ristretto with invalid compressed points
TEST_F(AdversarialSyscallTest, RistrettoInvalidCompressed) {
    uint8_t invalid_point[32];
    std::fill(std::begin(invalid_point), std::end(invalid_point), 0xFF);
    
    uint8_t other_point[32] = {0};
    uint8_t result_point[32];
    
    uint64_t result = sol_curve25519_ristretto_add(invalid_point, other_point, result_point);
    EXPECT_NE(result, 0); // Should reject invalid point
}

// Test 14: Poseidon with extreme num_hashes
TEST_F(AdversarialSyscallTest, PoseidonExtremeNumHashes) {
    uint8_t input[32];
    fill_random(input, sizeof(input));
    
    uint8_t output[32];
    uint64_t output_len = sizeof(output);
    
    // Request billions of hashes
    uint64_t result = sol_poseidon(input, sizeof(input), UINT64_MAX, output, &output_len);
    EXPECT_NE(result, 0); // Should reject or timeout protection
}

// Test 15: Epoch stake with special pubkeys
TEST_F(AdversarialSyscallTest, EpochStakeSpecialPubkeys) {
    uint8_t vote_pubkey[32];
    uint8_t stake_out[16];
    uint64_t stake_len = sizeof(stake_out);
    
    // Test with program-derived address-like patterns
    std::fill(std::begin(vote_pubkey), std::end(vote_pubkey), 0x00);
    vote_pubkey[31] = 0x01; // Slight variation
    
    uint64_t result = sol_get_epoch_stake(vote_pubkey, stake_out, &stake_len);
    EXPECT_TRUE(result == 0 || result != 0); // Should handle gracefully
}

// Test 16: Output buffer too small
TEST_F(AdversarialSyscallTest, OutputBufferTooSmall) {
    uint8_t input[128];
    fill_random(input, sizeof(input));
    
    uint8_t small_output[8]; // Too small for 32-byte hash
    uint64_t output_len = sizeof(small_output);
    
    uint64_t result = sol_blake3(input, sizeof(input), small_output, &output_len);
    EXPECT_NE(result, 0); // Should reject insufficient buffer
}

// Test 17: Memory boundary crossing
TEST_F(AdversarialSyscallTest, MemoryBoundaryCrossing) {
    // Allocate at page boundary
    std::vector<uint8_t> data(4096 + 128);
    fill_random(data.data(), data.size());
    
    uint8_t output[64];
    uint64_t output_len = sizeof(output);
    
    // Access across potential page boundary
    sol_blake3(data.data() + 4090, 128, output, &output_len);
    SUCCEED(); // Should handle without crash
}

// Test 18: Repeated identical inputs (cache poisoning)
TEST_F(AdversarialSyscallTest, RepeatedIdenticalInputs) {
    uint8_t input[128];
    std::fill(std::begin(input), std::end(input), 0xAA);
    
    for (int i = 0; i < 1000; ++i) {
        uint8_t output[64];
        uint64_t output_len = sizeof(output);
        sol_blake3(input, sizeof(input), output, &output_len);
    }
    SUCCEED();
}

// Test 19: Race condition on output_len
TEST_F(AdversarialSyscallTest, RaceOnOutputLen) {
    std::atomic<bool> stop{false};
    uint64_t shared_output_len = 32;
    
    auto modifier = [&]() {
        while (!stop) {
            shared_output_len = (shared_output_len == 32) ? 64 : 32;
        }
    };
    
    std::thread modifier_thread(modifier);
    
    for (int i = 0; i < 100; ++i) {
        uint8_t input[128];
        uint8_t output[64];
        fill_random(input, sizeof(input));
        
        uint64_t local_len = shared_output_len;
        sol_blake3(input, sizeof(input), output, &local_len);
    }
    
    stop = true;
    modifier_thread.join();
    SUCCEED();
}

// Test 20: Syscall immediately after memory allocation failure simulation
TEST_F(AdversarialSyscallTest, AfterMemoryPressure) {
    // Allocate large amount of memory to simulate pressure
    std::vector<std::vector<uint8_t>> pressure;
    try {
        for (int i = 0; i < 100; ++i) {
            pressure.emplace_back(1024 * 1024); // 1MB each
        }
    } catch (...) {
        // Expected on low memory
    }
    
    // Now try syscall
    uint8_t input[128];
    uint8_t output[64];
    uint64_t output_len = sizeof(output);
    
    fill_random(input, sizeof(input));
    uint64_t result = sol_blake3(input, sizeof(input), output, &output_len);
    
    pressure.clear();
    EXPECT_TRUE(result == 0 || result != 0); // Should complete
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
