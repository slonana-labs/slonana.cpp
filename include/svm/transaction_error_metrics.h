#pragma once

#include "common/types.h"
#include <cstdint>

namespace slonana {
namespace svm {

/**
 * Comprehensive transaction error metrics
 * Equivalent to Agave's transaction_error_metrics.rs
 */
struct TransactionErrorMetrics {
    // Account-related errors
    uint64_t account_not_found = 0;
    uint64_t invalid_account_for_fee = 0;
    uint64_t invalid_account_index = 0;
    uint64_t invalid_rent_paying_account = 0;
    uint64_t account_in_use = 0;
    uint64_t account_loaded_twice = 0;
    uint64_t account_not_shared = 0;
    uint64_t account_shrinkage = 0;
    
    // Fund-related errors
    uint64_t insufficient_funds = 0;
    uint64_t insufficient_funds_for_fee = 0;
    uint64_t insufficient_funds_for_rent = 0;
    
    // Instruction and program errors
    uint64_t instruction_error = 0;
    uint64_t program_error = 0;
    uint64_t invalid_instruction = 0;
    uint64_t duplicate_instruction = 0;
    uint64_t program_account_not_found = 0;
    uint64_t invalid_program_for_execution = 0;
    
    // Blockhash and nonce errors
    uint64_t blockhash_not_found = 0;
    uint64_t blockhash_too_old = 0;
    uint64_t invalid_nonce = 0;
    uint64_t nonce_no_recent_blockhashes = 0;
    
    // Resource limit errors
    uint64_t max_loaded_accounts_data_size_exceeded = 0;
    uint64_t invalid_loaded_accounts_data_size_limit = 0;
    uint64_t would_exceed_max_account_cost_limit = 0;
    uint64_t would_exceed_max_vote_cost_limit = 0;
    uint64_t would_exceed_max_account_data_cost_limit = 0;
    uint64_t would_exceed_account_data_block_limit = 0;
    uint64_t would_exceed_account_data_total_limit = 0;
    uint64_t too_many_account_locks = 0;
    
    // Compute budget errors
    uint64_t invalid_compute_budget = 0;
    uint64_t compute_budget_exceeded = 0;
    
    // Signature and authorization errors
    uint64_t missing_signature_for_fee = 0;
    uint64_t signature_failure = 0;
    uint64_t invalid_signature = 0;
    
    // Transaction format errors
    uint64_t unsupported_version = 0;
    uint64_t resanitization_needed = 0;
    uint64_t unbalanced_transaction = 0;
    uint64_t invalid_writable_account = 0;
    
    // Slot and epoch errors
    uint64_t min_context_slot_not_reached = 0;
    uint64_t cluster_maintenance = 0;
    
    // Program cache errors
    uint64_t program_cache_hit_max_limit = 0;
    
    /**
     * Get total error count
     */
    uint64_t total_errors() const;
    
    /**
     * Reset all metrics
     */
    void reset();
    
    /**
     * Add metrics from another instance
     */
    void add(const TransactionErrorMetrics& other);
    
    /**
     * Get error rate (0.0 to 1.0) given total transactions
     */
    double get_error_rate(uint64_t total_transactions) const;
    
    /**
     * Get most common error type
     */
    std::string get_most_common_error() const;
    
    /**
     * Format metrics for logging
     */
    std::string format() const;
    
    /**
     * Export metrics as key-value pairs
     */
    std::vector<std::pair<std::string, uint64_t>> export_metrics() const;
};

/**
 * Error classification utilities
 */
namespace error_metrics_utils {
    /**
     * Classify error type from ExecutionResult
     */
    void increment_for_execution_result(TransactionErrorMetrics& metrics, 
                                       const std::string& error_type);
    
    /**
     * Get error category name
     */
    std::string get_error_category(const std::string& error_type);
    
    /**
     * Check if error is recoverable
     */
    bool is_recoverable_error(const std::string& error_type);
    
    /**
     * Get error severity level (1-5, 5 being most severe)
     */
    int get_error_severity(const std::string& error_type);
}

} // namespace svm
} // namespace slonana