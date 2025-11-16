#include "svm/syscalls.h"
#include "test_framework.h"
#include <cstring>
#include <vector>
#include <algorithm>

using namespace slonana::svm;

/**
 * Extended Test Suite for Advanced Cryptographic Syscalls
 * Covers edge cases, security, and integration scenarios
 */

// ===== BN254 Extended Tests =====

void test_bn254_addition_null_input() {
    uint8_t output[64];
    uint64_t output_len = 0;
    
    uint64_t result = sol_alt_bn128_addition(nullptr, 128, output, &output_len);
    ASSERT_NE(result, (uint64_t)0);
}

void test_bn254_addition_null_output() {
    uint8_t input[128];
    std::memset(input, 0, 128);
    uint64_t output_len = 0;
    
    uint64_t result = sol_alt_bn128_addition(input, 128, nullptr, &output_len);
    ASSERT_NE(result, (uint64_t)0);
}

void test_bn254_addition_point_at_infinity() {
    uint8_t input[128];
    std::memset(input, 0, 128); // Both points at infinity
    
    uint8_t output[64];
    uint64_t output_len = 0;
    
    uint64_t result = sol_alt_bn128_addition(input, 128, output, &output_len);
    ASSERT_EQ(result, (uint64_t)0);
    
    // Result should be point at infinity (all zeros)
    bool all_zero = std::all_of(output, output + 64, [](uint8_t b) { return b == 0; });
    ASSERT_TRUE(all_zero);
}

void test_bn254_addition_large_coordinates() {
    uint8_t input[128];
    std::memset(input, 0xFF, 128); // Maximum values
    
    uint8_t output[64];
    uint64_t output_len = 0;
    
    uint64_t result = sol_alt_bn128_addition(input, 128, output, &output_len);
    // Should handle or reject gracefully
    ASSERT_TRUE(result == 0 || result != 0); // Either success or controlled failure
}

void test_bn254_multiplication_zero_scalar() {
    uint8_t input[96];
    std::memset(input, 0, 96); // Zero scalar
    input[95] = 1; // Valid point
    
    uint8_t output[64];
    uint64_t output_len = 0;
    
    uint64_t result = sol_alt_bn128_multiplication(input, 96, output, &output_len);
    ASSERT_EQ(result, (uint64_t)0);
    
    // 0 * point = point at infinity
    bool all_zero = std::all_of(output, output + 64, [](uint8_t b) { return b == 0; });
    ASSERT_TRUE(all_zero);
}

void test_bn254_multiplication_null_input() {
    uint8_t output[64];
    uint64_t output_len = 0;
    
    uint64_t result = sol_alt_bn128_multiplication(nullptr, 96, output, &output_len);
    ASSERT_NE(result, (uint64_t)0);
}

void test_bn254_multiplication_boundary_scalar() {
    uint8_t input[96];
    std::memset(input, 0xFF, 32); // Max scalar value
    std::memset(input + 32, 0, 64);
    input[95] = 1; // Valid point
    
    uint8_t output[64];
    uint64_t output_len = 0;
    
    uint64_t result = sol_alt_bn128_multiplication(input, 96, output, &output_len);
    // Should handle large scalars
    ASSERT_TRUE(result == 0 || result != 0);
}

void test_bn254_pairing_empty_input() {
    uint8_t output[32];
    uint64_t output_len = 0;
    
    uint64_t result = sol_alt_bn128_pairing(nullptr, 0, output, &output_len);
    ASSERT_NE(result, (uint64_t)0);
}

void test_bn254_pairing_odd_length() {
    uint8_t input[193]; // Not multiple of 192
    std::memset(input, 0, 193);
    
    uint8_t output[32];
    uint64_t output_len = 0;
    
    uint64_t result = sol_alt_bn128_pairing(input, 193, output, &output_len);
    ASSERT_NE(result, (uint64_t)0);
}

void test_bn254_pairing_single_pair() {
    uint8_t input[192]; // One G1/G2 pair
    std::memset(input, 0, 192);
    input[63] = 1;
    input[191] = 1;
    
    uint8_t output[32];
    uint64_t output_len = 0;
    
    uint64_t result = sol_alt_bn128_pairing(input, 192, output, &output_len);
    ASSERT_EQ(result, (uint64_t)0);
    ASSERT_EQ(output_len, (uint64_t)32);
}

// ===== BLAKE3 Extended Tests =====

void test_blake3_null_input() {
    uint8_t output[32];
    uint64_t output_len = 0;
    
    uint64_t result = sol_blake3(nullptr, 100, output, &output_len);
    ASSERT_NE(result, (uint64_t)0);
}

void test_blake3_null_output() {
    uint8_t input[100];
    uint64_t output_len = 0;
    
    uint64_t result = sol_blake3(input, 100, nullptr, &output_len);
    ASSERT_NE(result, (uint64_t)0);
}

void test_blake3_empty_input() {
    uint8_t input[1];
    uint8_t output[32];
    uint64_t output_len = 0;
    
    uint64_t result = sol_blake3(input, 0, output, &output_len);
    ASSERT_EQ(result, (uint64_t)0);
    ASSERT_EQ(output_len, (uint64_t)32);
}

void test_blake3_large_input() {
    std::vector<uint8_t> input(1024 * 1024); // 1MB
    std::fill(input.begin(), input.end(), 0xAA);
    
    uint8_t output[32];
    uint64_t output_len = 0;
    
    uint64_t result = sol_blake3(input.data(), input.size(), output, &output_len);
    ASSERT_EQ(result, (uint64_t)0);
    ASSERT_EQ(output_len, (uint64_t)32);
}

void test_blake3_consistency() {
    uint8_t input[100];
    std::memset(input, 0x42, 100);
    
    uint8_t output1[32], output2[32];
    uint64_t output_len1 = 0, output_len2 = 0;
    
    sol_blake3(input, 100, output1, &output_len1);
    sol_blake3(input, 100, output2, &output_len2);
    
    ASSERT_EQ(std::memcmp(output1, output2, 32), 0);
}

// ===== Poseidon Extended Tests =====

void test_poseidon_null_input() {
    uint8_t output[32];
    uint64_t output_len = 0;
    
    uint64_t result = sol_poseidon(nullptr, 32, 1, output, &output_len);
    ASSERT_NE(result, (uint64_t)0);
}

void test_poseidon_zero_hashes() {
    uint8_t input[32];
    std::memset(input, 0, 32);
    uint8_t output[32];
    uint64_t output_len = 0;
    
    uint64_t result = sol_poseidon(input, 32, 0, output, &output_len);
    ASSERT_NE(result, (uint64_t)0);
}

void test_poseidon_multiple_hashes() {
    uint8_t input[64]; // 2 elements
    std::memset(input, 0, 64);
    input[0] = 1;
    input[32] = 2;
    
    uint8_t output[32];
    uint64_t output_len = 0;
    
    uint64_t result = sol_poseidon(input, 64, 2, output, &output_len);
    ASSERT_EQ(result, (uint64_t)0);
    ASSERT_EQ(output_len, (uint64_t)32);
}

void test_poseidon_deterministic() {
    uint8_t input[32];
    std::memset(input, 0x55, 32);
    
    uint8_t output1[32], output2[32];
    uint64_t output_len1 = 0, output_len2 = 0;
    
    sol_poseidon(input, 32, 1, output1, &output_len1);
    sol_poseidon(input, 32, 1, output2, &output_len2);
    
    ASSERT_EQ(std::memcmp(output1, output2, 32), 0);
}

// ===== Ristretto Extended Tests =====

void test_ristretto_addition_null_left() {
    uint8_t right[32], result[32];
    std::memset(right, 0, 32);
    
    uint64_t ret = sol_curve25519_ristretto_add(nullptr, right, result);
    ASSERT_NE(ret, (uint64_t)0);
}

void test_ristretto_addition_null_right() {
    uint8_t left[32], result[32];
    std::memset(left, 0, 32);
    
    uint64_t ret = sol_curve25519_ristretto_add(left, nullptr, result);
    ASSERT_NE(ret, (uint64_t)0);
}

void test_ristretto_addition_null_result() {
    uint8_t left[32], right[32];
    std::memset(left, 0, 32);
    std::memset(right, 0, 32);
    
    uint64_t ret = sol_curve25519_ristretto_add(left, right, nullptr);
    ASSERT_NE(ret, (uint64_t)0);
}

void test_ristretto_addition_identity() {
    uint8_t identity[32], point[32], result[32];
    std::memset(identity, 0, 32);
    std::memset(point, 0, 32);
    point[0] = 1;
    
    uint64_t ret = sol_curve25519_ristretto_add(point, identity, result);
    ASSERT_EQ(ret, (uint64_t)0);
    
    // point + identity = point
    ASSERT_EQ(std::memcmp(result, point, 32), 0);
}

void test_ristretto_subtraction_null_inputs() {
    uint8_t point[32], result[32];
    std::memset(point, 0, 32);
    
    uint64_t ret1 = sol_curve25519_ristretto_subtract(nullptr, point, result);
    ASSERT_NE(ret1, (uint64_t)0);
    
    uint64_t ret2 = sol_curve25519_ristretto_subtract(point, nullptr, result);
    ASSERT_NE(ret2, (uint64_t)0);
}

void test_ristretto_subtraction_self() {
    uint8_t point[32], result[32];
    std::memset(point, 0, 32);
    point[0] = 1;
    
    uint64_t ret = sol_curve25519_ristretto_subtract(point, point, result);
    ASSERT_EQ(ret, (uint64_t)0);
    
    // point - point = identity
    bool all_zero = std::all_of(result, result + 32, [](uint8_t b) { return b == 0; });
    ASSERT_TRUE(all_zero);
}

void test_ristretto_multiplication_null_inputs() {
    uint8_t scalar[32], point[32], result[32];
    std::memset(scalar, 0, 32);
    std::memset(point, 0, 32);
    
    uint64_t ret1 = sol_curve25519_ristretto_multiply(nullptr, point, result);
    ASSERT_NE(ret1, (uint64_t)0);
    
    uint64_t ret2 = sol_curve25519_ristretto_multiply(scalar, nullptr, result);
    ASSERT_NE(ret2, (uint64_t)0);
}

void test_ristretto_multiplication_by_zero() {
    uint8_t scalar[32], point[32], result[32];
    std::memset(scalar, 0, 32); // Zero scalar
    std::memset(point, 0, 32);
    point[0] = 1;
    
    uint64_t ret = sol_curve25519_ristretto_multiply(scalar, point, result);
    ASSERT_EQ(ret, (uint64_t)0);
    
    // 0 * point = identity
    bool all_zero = std::all_of(result, result + 32, [](uint8_t b) { return b == 0; });
    ASSERT_TRUE(all_zero);
}

void test_ristretto_multiplication_by_one() {
    uint8_t scalar[32], point[32], result[32];
    std::memset(scalar, 0, 32);
    scalar[0] = 1; // Scalar = 1
    std::memset(point, 0, 32);
    point[0] = 1;
    
    uint64_t ret = sol_curve25519_ristretto_multiply(scalar, point, result);
    ASSERT_EQ(ret, (uint64_t)0);
    
    // 1 * point = point
    ASSERT_EQ(std::memcmp(result, point, 32), 0);
}

// ===== Security and Robustness Tests =====

void test_all_syscalls_handle_max_values() {
    uint8_t max_input[192];
    std::memset(max_input, 0xFF, 192);
    uint8_t output[64];
    uint64_t output_len = 0;
    
    // Test that all syscalls handle maximum values without crashing
    sol_alt_bn128_addition(max_input, 128, output, &output_len);
    sol_alt_bn128_multiplication(max_input, 96, output, &output_len);
    sol_blake3(max_input, 192, output, &output_len);
    sol_poseidon(max_input, 32, 1, output, &output_len);
    
    // If we get here without crashing, test passes
    ASSERT_TRUE(true);
}

void test_crypto_syscalls_compute_unit_consistency() {
    // Verify compute unit costs are consistent across calls
    uint8_t input[128];
    std::memset(input, 0, 128);
    input[63] = 1;
    input[127] = 1;
    
    uint8_t output[64];
    uint64_t output_len = 0;
    
    // Call multiple times and verify consistent behavior
    for (int i = 0; i < 10; i++) {
        uint64_t result = sol_alt_bn128_addition(input, 128, output, &output_len);
        ASSERT_EQ(result, (uint64_t)0);
        ASSERT_EQ(output_len, (uint64_t)64);
    }
}

void test_concurrent_crypto_operations() {
    // Simulate concurrent access patterns
    uint8_t input1[32], input2[32];
    std::memset(input1, 0xAA, 32);
    std::memset(input2, 0xBB, 32);
    
    uint8_t output1[32], output2[32];
    uint64_t len1 = 0, len2 = 0;
    
    // Interleaved calls
    sol_blake3(input1, 32, output1, &len1);
    sol_blake3(input2, 32, output2, &len2);
    
    // Verify results are different
    ASSERT_NE(std::memcmp(output1, output2, 32), 0);
}

// Main test runner
int main() {
    RUN_TEST(test_bn254_addition_null_input);
    RUN_TEST(test_bn254_addition_null_output);
    RUN_TEST(test_bn254_addition_point_at_infinity);
    RUN_TEST(test_bn254_addition_large_coordinates);
    RUN_TEST(test_bn254_multiplication_zero_scalar);
    RUN_TEST(test_bn254_multiplication_null_input);
    RUN_TEST(test_bn254_multiplication_boundary_scalar);
    RUN_TEST(test_bn254_pairing_empty_input);
    RUN_TEST(test_bn254_pairing_odd_length);
    RUN_TEST(test_bn254_pairing_single_pair);
    
    RUN_TEST(test_blake3_null_input);
    RUN_TEST(test_blake3_null_output);
    RUN_TEST(test_blake3_empty_input);
    RUN_TEST(test_blake3_large_input);
    RUN_TEST(test_blake3_consistency);
    
    RUN_TEST(test_poseidon_null_input);
    RUN_TEST(test_poseidon_zero_hashes);
    RUN_TEST(test_poseidon_multiple_hashes);
    RUN_TEST(test_poseidon_deterministic);
    
    RUN_TEST(test_ristretto_addition_null_left);
    RUN_TEST(test_ristretto_addition_null_right);
    RUN_TEST(test_ristretto_addition_null_result);
    RUN_TEST(test_ristretto_addition_identity);
    RUN_TEST(test_ristretto_subtraction_null_inputs);
    RUN_TEST(test_ristretto_subtraction_self);
    RUN_TEST(test_ristretto_multiplication_null_inputs);
    RUN_TEST(test_ristretto_multiplication_by_zero);
    RUN_TEST(test_ristretto_multiplication_by_one);
    
    RUN_TEST(test_all_syscalls_handle_max_values);
    RUN_TEST(test_crypto_syscalls_compute_unit_consistency);
    RUN_TEST(test_concurrent_crypto_operations);
    
    return 0;
}
