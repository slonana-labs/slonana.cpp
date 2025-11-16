#include "svm/syscalls.h"
#include "test_framework.h"
#include <cstring>
#include <vector>

using namespace slonana::svm;

/**
 * Test Suite for Advanced Cryptographic Syscalls
 */

void test_bn254_addition_valid() {
    uint8_t input[128];
    std::memset(input, 0, 128);
    input[63] = 1;
    input[127] = 1;
    
    uint8_t output[64];
    uint64_t output_len = 0;
    
    uint64_t result = sol_alt_bn128_addition(input, 128, output, &output_len);
    
    ASSERT_EQ(result, (uint64_t)0);
    ASSERT_EQ(output_len, (uint64_t)64);
}

void test_bn254_addition_invalid_length() {
    uint8_t input[100];
    uint8_t output[64];
    uint64_t output_len = 0;
    
    uint64_t result = sol_alt_bn128_addition(input, 100, output, &output_len);
    
    ASSERT_NE(result, (uint64_t)0);
}

void test_bn254_multiplication_valid() {
    uint8_t input[96];
    std::memset(input, 0, 96);
    input[31] = 1;
    input[95] = 1;
    
    uint8_t output[64];
    uint64_t output_len = 0;
    
    uint64_t result = sol_alt_bn128_multiplication(input, 96, output, &output_len);
    
    ASSERT_EQ(result, (uint64_t)0);
    ASSERT_EQ(output_len, (uint64_t)64);
}

void test_bn254_pairing_valid() {
    uint8_t input[192];
    std::memset(input, 0, 192);
    
    uint8_t output[32];
    uint64_t output_len = 0;
    
    uint64_t result = sol_alt_bn128_pairing(input, 192, output, &output_len);
    
    ASSERT_EQ(result, (uint64_t)0);
    ASSERT_EQ(output_len, (uint64_t)32);
}

void test_blake3_short_input() {
    const char* msg = "hello";
    uint8_t output[32];
    uint64_t output_len = 0;
    
    uint64_t result = sol_blake3((const uint8_t*)msg, strlen(msg), output, &output_len);
    
    ASSERT_EQ(result, (uint64_t)0);
    ASSERT_EQ(output_len, (uint64_t)32);
}

void test_blake3_deterministic() {
    const char* msg = "test message";
    uint8_t output1[32];
    uint8_t output2[32];
    uint64_t output_len = 0;
    
    sol_blake3((const uint8_t*)msg, strlen(msg), output1, &output_len);
    sol_blake3((const uint8_t*)msg, strlen(msg), output2, &output_len);
    
    ASSERT_TRUE(std::memcmp(output1, output2, 32) == 0);
}

void test_poseidon_single_element() {
    uint8_t input[32];
    std::memset(input, 0, 32);
    input[31] = 1;
    
    uint8_t output[32];
    uint64_t output_len = 0;
    
    uint64_t result = sol_poseidon(input, 32, 1, output, &output_len);
    
    ASSERT_EQ(result, (uint64_t)0);
    ASSERT_EQ(output_len, (uint64_t)32);
}

void test_ristretto_addition() {
    uint8_t left[32], right[32], result[32];
    std::memset(left, 0, 32);
    std::memset(right, 0, 32);
    left[0] = 1;
    right[0] = 2;
    
    uint64_t ret = sol_curve25519_ristretto_add(left, right, result);
    
    ASSERT_EQ(ret, (uint64_t)0);
}

void test_ristretto_multiplication() {
    uint8_t scalar[32], point[32], result[32];
    std::memset(scalar, 0, 32);
    std::memset(point, 0, 32);
    scalar[0] = 2;
    point[0] = 1;
    
    uint64_t ret = sol_curve25519_ristretto_multiply(scalar, point, result);
    
    ASSERT_EQ(ret, (uint64_t)0);
}

void test_compute_unit_costs() {
    ASSERT_GT(compute_units::BLAKE3, (uint64_t)0);
    ASSERT_GT(compute_units::POSEIDON, (uint64_t)0);
    ASSERT_GT(compute_units::ALT_BN128_ADDITION, (uint64_t)0);
    ASSERT_GT(compute_units::ALT_BN128_MULTIPLICATION, (uint64_t)0);
}

int main() {
    TestRunner runner;
    
    std::cout << "\n=== Cryptographic Syscalls Tests ===\n" << std::endl;
    
    runner.run_test("BN254 Addition Valid Input", test_bn254_addition_valid);
    runner.run_test("BN254 Addition Invalid Length", test_bn254_addition_invalid_length);
    runner.run_test("BN254 Multiplication Valid", test_bn254_multiplication_valid);
    runner.run_test("BN254 Pairing Valid", test_bn254_pairing_valid);
    runner.run_test("BLAKE3 Short Input", test_blake3_short_input);
    runner.run_test("BLAKE3 Deterministic", test_blake3_deterministic);
    runner.run_test("Poseidon Single Element", test_poseidon_single_element);
    runner.run_test("Ristretto Addition", test_ristretto_addition);
    runner.run_test("Ristretto Multiplication", test_ristretto_multiplication);
    runner.run_test("Compute Unit Costs", test_compute_unit_costs);
    
    runner.print_summary();
    
    return runner.all_passed() ? 0 : 1;
}
