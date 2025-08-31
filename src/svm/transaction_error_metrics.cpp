#include "svm/transaction_error_metrics.h"
#include <sstream>
#include <algorithm>
#include <vector>

namespace slonana {
namespace svm {

uint64_t TransactionErrorMetrics::total_errors() const {
    return account_not_found + invalid_account_for_fee + invalid_account_index +
           invalid_rent_paying_account + account_in_use + account_loaded_twice +
           account_not_shared + account_shrinkage + insufficient_funds +
           insufficient_funds_for_fee + insufficient_funds_for_rent +
           instruction_error + program_error + invalid_instruction +
           duplicate_instruction + program_account_not_found +
           invalid_program_for_execution + blockhash_not_found +
           blockhash_too_old + invalid_nonce + nonce_no_recent_blockhashes +
           max_loaded_accounts_data_size_exceeded +
           invalid_loaded_accounts_data_size_limit +
           would_exceed_max_account_cost_limit +
           would_exceed_max_vote_cost_limit +
           would_exceed_max_account_data_cost_limit +
           would_exceed_account_data_block_limit +
           would_exceed_account_data_total_limit + too_many_account_locks +
           invalid_compute_budget + compute_budget_exceeded +
           missing_signature_for_fee + signature_failure + invalid_signature +
           unsupported_version + resanitization_needed + unbalanced_transaction +
           invalid_writable_account + min_context_slot_not_reached +
           cluster_maintenance + program_cache_hit_max_limit;
}

void TransactionErrorMetrics::reset() {
    *this = TransactionErrorMetrics{};
}

void TransactionErrorMetrics::add(const TransactionErrorMetrics& other) {
    account_not_found += other.account_not_found;
    invalid_account_for_fee += other.invalid_account_for_fee;
    invalid_account_index += other.invalid_account_index;
    invalid_rent_paying_account += other.invalid_rent_paying_account;
    account_in_use += other.account_in_use;
    account_loaded_twice += other.account_loaded_twice;
    account_not_shared += other.account_not_shared;
    account_shrinkage += other.account_shrinkage;
    
    insufficient_funds += other.insufficient_funds;
    insufficient_funds_for_fee += other.insufficient_funds_for_fee;
    insufficient_funds_for_rent += other.insufficient_funds_for_rent;
    
    instruction_error += other.instruction_error;
    program_error += other.program_error;
    invalid_instruction += other.invalid_instruction;
    duplicate_instruction += other.duplicate_instruction;
    program_account_not_found += other.program_account_not_found;
    invalid_program_for_execution += other.invalid_program_for_execution;
    
    blockhash_not_found += other.blockhash_not_found;
    blockhash_too_old += other.blockhash_too_old;
    invalid_nonce += other.invalid_nonce;
    nonce_no_recent_blockhashes += other.nonce_no_recent_blockhashes;
    
    max_loaded_accounts_data_size_exceeded += other.max_loaded_accounts_data_size_exceeded;
    invalid_loaded_accounts_data_size_limit += other.invalid_loaded_accounts_data_size_limit;
    would_exceed_max_account_cost_limit += other.would_exceed_max_account_cost_limit;
    would_exceed_max_vote_cost_limit += other.would_exceed_max_vote_cost_limit;
    would_exceed_max_account_data_cost_limit += other.would_exceed_max_account_data_cost_limit;
    would_exceed_account_data_block_limit += other.would_exceed_account_data_block_limit;
    would_exceed_account_data_total_limit += other.would_exceed_account_data_total_limit;
    too_many_account_locks += other.too_many_account_locks;
    
    invalid_compute_budget += other.invalid_compute_budget;
    compute_budget_exceeded += other.compute_budget_exceeded;
    
    missing_signature_for_fee += other.missing_signature_for_fee;
    signature_failure += other.signature_failure;
    invalid_signature += other.invalid_signature;
    
    unsupported_version += other.unsupported_version;
    resanitization_needed += other.resanitization_needed;
    unbalanced_transaction += other.unbalanced_transaction;
    invalid_writable_account += other.invalid_writable_account;
    
    min_context_slot_not_reached += other.min_context_slot_not_reached;
    cluster_maintenance += other.cluster_maintenance;
    program_cache_hit_max_limit += other.program_cache_hit_max_limit;
}

double TransactionErrorMetrics::get_error_rate(uint64_t total_transactions) const {
    if (total_transactions == 0) {
        return 0.0;
    }
    return static_cast<double>(total_errors()) / static_cast<double>(total_transactions);
}

std::string TransactionErrorMetrics::get_most_common_error() const {
    std::vector<std::pair<std::string, uint64_t>> errors = export_metrics();
    
    if (errors.empty()) {
        return "None";
    }
    
    auto max_error = std::max_element(errors.begin(), errors.end(),
                                     [](const auto& a, const auto& b) {
                                         return a.second < b.second;
                                     });
    
    if (max_error->second == 0) {
        return "None";
    }
    
    return max_error->first;
}

std::string TransactionErrorMetrics::format() const {
    std::stringstream ss;
    ss << "Transaction Error Metrics:\n";
    ss << "  Total errors: " << total_errors() << "\n";
    
    auto metrics = export_metrics();
    for (const auto& metric : metrics) {
        if (metric.second > 0) {
            ss << "  " << metric.first << ": " << metric.second << "\n";
        }
    }
    
    return ss.str();
}

std::vector<std::pair<std::string, uint64_t>> TransactionErrorMetrics::export_metrics() const {
    return {
        {"account_not_found", account_not_found},
        {"invalid_account_for_fee", invalid_account_for_fee},
        {"invalid_account_index", invalid_account_index},
        {"invalid_rent_paying_account", invalid_rent_paying_account},
        {"account_in_use", account_in_use},
        {"account_loaded_twice", account_loaded_twice},
        {"account_not_shared", account_not_shared},
        {"account_shrinkage", account_shrinkage},
        {"insufficient_funds", insufficient_funds},
        {"insufficient_funds_for_fee", insufficient_funds_for_fee},
        {"insufficient_funds_for_rent", insufficient_funds_for_rent},
        {"instruction_error", instruction_error},
        {"program_error", program_error},
        {"invalid_instruction", invalid_instruction},
        {"duplicate_instruction", duplicate_instruction},
        {"program_account_not_found", program_account_not_found},
        {"invalid_program_for_execution", invalid_program_for_execution},
        {"blockhash_not_found", blockhash_not_found},
        {"blockhash_too_old", blockhash_too_old},
        {"invalid_nonce", invalid_nonce},
        {"nonce_no_recent_blockhashes", nonce_no_recent_blockhashes},
        {"max_loaded_accounts_data_size_exceeded", max_loaded_accounts_data_size_exceeded},
        {"invalid_loaded_accounts_data_size_limit", invalid_loaded_accounts_data_size_limit},
        {"would_exceed_max_account_cost_limit", would_exceed_max_account_cost_limit},
        {"would_exceed_max_vote_cost_limit", would_exceed_max_vote_cost_limit},
        {"would_exceed_max_account_data_cost_limit", would_exceed_max_account_data_cost_limit},
        {"would_exceed_account_data_block_limit", would_exceed_account_data_block_limit},
        {"would_exceed_account_data_total_limit", would_exceed_account_data_total_limit},
        {"too_many_account_locks", too_many_account_locks},
        {"invalid_compute_budget", invalid_compute_budget},
        {"compute_budget_exceeded", compute_budget_exceeded},
        {"missing_signature_for_fee", missing_signature_for_fee},
        {"signature_failure", signature_failure},
        {"invalid_signature", invalid_signature},
        {"unsupported_version", unsupported_version},
        {"resanitization_needed", resanitization_needed},
        {"unbalanced_transaction", unbalanced_transaction},
        {"invalid_writable_account", invalid_writable_account},
        {"min_context_slot_not_reached", min_context_slot_not_reached},
        {"cluster_maintenance", cluster_maintenance},
        {"program_cache_hit_max_limit", program_cache_hit_max_limit}
    };
}

// Utility functions
namespace error_metrics_utils {

void increment_for_execution_result(TransactionErrorMetrics& metrics, 
                                   const std::string& error_type) {
    if (error_type == "ACCOUNT_NOT_FOUND") {
        metrics.account_not_found++;
    } else if (error_type == "INSUFFICIENT_FUNDS") {
        metrics.insufficient_funds++;
    } else if (error_type == "INVALID_INSTRUCTION") {
        metrics.invalid_instruction++;
    } else if (error_type == "PROGRAM_ERROR") {
        metrics.program_error++;
    } else if (error_type == "COMPUTE_BUDGET_EXCEEDED") {
        metrics.compute_budget_exceeded++;
    } else if (error_type == "INVALID_ACCOUNT_ACCESS") {
        metrics.invalid_account_index++;
    } else if (error_type == "MEMORY_ACCESS_VIOLATION") {
        metrics.program_error++;
    } else if (error_type == "EXECUTION_ERROR") {
        metrics.instruction_error++;
    } else {
        // Default to instruction error for unknown types
        metrics.instruction_error++;
    }
}

std::string get_error_category(const std::string& error_type) {
    if (error_type.find("account") != std::string::npos) {
        return "Account";
    } else if (error_type.find("funds") != std::string::npos) {
        return "Funds";
    } else if (error_type.find("instruction") != std::string::npos || 
               error_type.find("program") != std::string::npos) {
        return "Execution";
    } else if (error_type.find("blockhash") != std::string::npos || 
               error_type.find("nonce") != std::string::npos) {
        return "Validation";
    } else if (error_type.find("compute") != std::string::npos) {
        return "Resources";
    } else if (error_type.find("signature") != std::string::npos) {
        return "Authorization";
    } else {
        return "Other";
    }
}

bool is_recoverable_error(const std::string& error_type) {
    // Some errors can be recovered by retrying with different parameters
    return error_type == "blockhash_too_old" || 
           error_type == "account_in_use" || 
           error_type == "cluster_maintenance" ||
           error_type == "min_context_slot_not_reached";
}

int get_error_severity(const std::string& error_type) {
    if (error_type.find("insufficient_funds") != std::string::npos) {
        return 3; // Medium severity - user can add funds
    } else if (error_type.find("account_not_found") != std::string::npos) {
        return 2; // Low-medium severity - account may need to be created
    } else if (error_type.find("signature") != std::string::npos) {
        return 4; // High severity - security related
    } else if (error_type.find("program_error") != std::string::npos) {
        return 3; // Medium severity - program issue
    } else if (error_type.find("blockhash") != std::string::npos) {
        return 1; // Low severity - can retry
    } else {
        return 2; // Default medium-low severity
    }
}

} // namespace error_metrics_utils

} // namespace svm
} // namespace slonana