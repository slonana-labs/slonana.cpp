#pragma once

#include <cstdint>
#include <cstring>

namespace slonana {
namespace svm {

/**
 * SVM Syscall Interface
 * 
 * This header defines all syscalls available in the Solana Virtual Machine,
 * providing compatibility with Agave's syscall interface.
 */

// ============================================================================
// Cryptographic Syscalls (2024-2025 Additions)
// ============================================================================

/**
 * BN254 (alt_bn128) Elliptic Curve Addition
 * 
 * Performs addition on the BN254 elliptic curve used in zero-knowledge proofs.
 * 
 * @param input Pointer to 128 bytes containing two points (64 bytes each)
 * @param input_len Length of input (must be 128)
 * @param output Pointer to 64-byte output buffer for result point
 * @param output_len Pointer to output length (will be set to 64)
 * @return 0 on success, error code otherwise
 */
uint64_t sol_alt_bn128_addition(
    const uint8_t* input,
    uint64_t input_len,
    uint8_t* output,
    uint64_t* output_len);

/**
 * BN254 (alt_bn128) Elliptic Curve Scalar Multiplication
 * 
 * Performs scalar multiplication on the BN254 elliptic curve.
 * 
 * @param input Pointer to 96 bytes containing scalar (32 bytes) and point (64 bytes)
 * @param input_len Length of input (must be 96)
 * @param output Pointer to 64-byte output buffer for result point
 * @param output_len Pointer to output length (will be set to 64)
 * @return 0 on success, error code otherwise
 */
uint64_t sol_alt_bn128_multiplication(
    const uint8_t* input,
    uint64_t input_len,
    uint8_t* output,
    uint64_t* output_len);

/**
 * BN254 (alt_bn128) Pairing Check
 * 
 * Performs pairing check on BN254 curve, essential for zero-knowledge proof verification.
 * 
 * @param input Pointer to input containing G1/G2 point pairs (192 bytes per pair)
 * @param input_len Length of input (must be multiple of 192)
 * @param output Pointer to 32-byte output buffer (1 if check succeeds, 0 otherwise)
 * @param output_len Pointer to output length (will be set to 32)
 * @return 0 on success, error code otherwise
 */
uint64_t sol_alt_bn128_pairing(
    const uint8_t* input,
    uint64_t input_len,
    uint8_t* output,
    uint64_t* output_len);

/**
 * BLAKE3 Hash Function
 * 
 * Computes BLAKE3 hash, a high-performance cryptographic hash function.
 * 
 * @param input Pointer to input data
 * @param input_len Length of input data
 * @param output Pointer to 32-byte output buffer
 * @param output_len Pointer to output length (will be set to 32)
 * @return 0 on success, error code otherwise
 */
uint64_t sol_blake3(
    const uint8_t* input,
    uint64_t input_len,
    uint8_t* output,
    uint64_t* output_len);

/**
 * Poseidon Hash Function
 * 
 * Computes Poseidon hash, a ZK-friendly hash function optimized for
 * zero-knowledge proof systems.
 * 
 * @param input Pointer to input field elements
 * @param input_len Length of input data
 * @param num_hashes Number of consecutive hashes to compute
 * @param output Pointer to output buffer
 * @param output_len Pointer to output length
 * @return 0 on success, error code otherwise
 */
uint64_t sol_poseidon(
    const uint8_t* input,
    uint64_t input_len,
    uint64_t num_hashes,
    uint8_t* output,
    uint64_t* output_len);

/**
 * Curve25519 Ristretto Point Addition
 * 
 * Adds two points on the Ristretto group (Curve25519).
 * 
 * @param left_point Pointer to 32-byte left point
 * @param right_point Pointer to 32-byte right point
 * @param result Pointer to 32-byte result buffer
 * @return 0 on success, error code otherwise
 */
uint64_t sol_curve25519_ristretto_add(
    const uint8_t* left_point,
    const uint8_t* right_point,
    uint8_t* result);

/**
 * Curve25519 Ristretto Point Subtraction
 * 
 * Subtracts two points on the Ristretto group (Curve25519).
 * 
 * @param left_point Pointer to 32-byte left point
 * @param right_point Pointer to 32-byte right point
 * @param result Pointer to 32-byte result buffer
 * @return 0 on success, error code otherwise
 */
uint64_t sol_curve25519_ristretto_subtract(
    const uint8_t* left_point,
    const uint8_t* right_point,
    uint8_t* result);

/**
 * Curve25519 Ristretto Scalar Multiplication
 * 
 * Multiplies a point by a scalar on the Ristretto group (Curve25519).
 * 
 * @param scalar Pointer to 32-byte scalar
 * @param point Pointer to 32-byte point
 * @param result Pointer to 32-byte result buffer
 * @return 0 on success, error code otherwise
 */
uint64_t sol_curve25519_ristretto_multiply(
    const uint8_t* scalar,
    const uint8_t* point,
    uint8_t* result);

// ============================================================================
// Sysvar Access Syscalls
// ============================================================================

/**
 * Get Epoch Stake Information
 * 
 * Retrieves stake information for a specific vote account in the current epoch.
 * 
 * @param vote_pubkey Pointer to 32-byte vote account public key
 * @param stake_out Pointer to output buffer for stake amount
 * @param stake_len Pointer to output length
 * @return 0 on success, error code otherwise
 */
uint64_t sol_get_epoch_stake(
    const uint8_t* vote_pubkey,
    uint8_t* stake_out,
    uint64_t* stake_len);

/**
 * Get Epoch Rewards Sysvar
 * 
 * Retrieves epoch rewards information from the sysvar.
 * 
 * @param result Pointer to output buffer
 * @param result_len Pointer to output length
 * @return 0 on success, error code otherwise
 */
uint64_t sol_get_epoch_rewards_sysvar(
    uint8_t* result,
    uint64_t* result_len);

/**
 * Get Last Restart Slot
 * 
 * Retrieves the slot number of the last cluster restart.
 * 
 * @param slot_out Pointer to output slot number
 * @return 0 on success, error code otherwise
 */
uint64_t sol_get_last_restart_slot(
    uint64_t* slot_out);

// ============================================================================
// Compute Unit Costs for Syscalls
// ============================================================================

namespace compute_units {
    // Cryptographic operation costs
    constexpr uint64_t BLAKE3 = 20;
    constexpr uint64_t POSEIDON = 30;
    constexpr uint64_t ALT_BN128_ADDITION = 150;
    constexpr uint64_t ALT_BN128_MULTIPLICATION = 6000;
    constexpr uint64_t ALT_BN128_PAIRING_BASE = 45000;
    constexpr uint64_t ALT_BN128_PAIRING_PER_PAIR = 34000;
    constexpr uint64_t RISTRETTO_ADD = 25;
    constexpr uint64_t RISTRETTO_SUBTRACT = 25;
    constexpr uint64_t RISTRETTO_MULTIPLY = 150;
    
    // Sysvar access costs
    constexpr uint64_t SYSVAR_BASE = 100;
    constexpr uint64_t EPOCH_STAKE = 200;
} // namespace compute_units

} // namespace svm
} // namespace slonana
