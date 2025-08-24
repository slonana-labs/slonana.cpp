#pragma once

#include "common/types.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace slonana {
namespace svm {

using namespace slonana::common;

/**
 * Program account data and executable information
 */
struct ProgramAccount {
    PublicKey pubkey;        // Account's public key address
    PublicKey program_id;
    std::vector<uint8_t> data;
    Lamports lamports;
    PublicKey owner;
    bool executable;
    Slot rent_epoch;
    
    std::vector<uint8_t> serialize() const;
    static ProgramAccount deserialize(const std::vector<uint8_t>& data);
};

/**
 * Instruction to be executed by the SVM
 */
struct Instruction {
    PublicKey program_id;
    std::vector<PublicKey> accounts;
    std::vector<uint8_t> data;
    
    std::vector<uint8_t> serialize() const;
    static Instruction deserialize(const std::vector<uint8_t>& data);
};

/**
 * Transaction execution context
 */
struct ExecutionContext {
    std::vector<Instruction> instructions;
    std::unordered_map<PublicKey, ProgramAccount> accounts;
    Lamports compute_budget;
    uint64_t max_compute_units;
    uint64_t current_epoch = 0;  // Added for SPL programs
    
    // Runtime state
    uint64_t consumed_compute_units = 0;
    bool transaction_succeeded = true;
    std::string error_message;
    
    // Track modified accounts during execution
    std::unordered_set<PublicKey> modified_accounts;
};

/**
 * Account information for program execution
 */
struct AccountInfo {
    PublicKey pubkey;
    bool is_signer;
    bool is_writable;
    Lamports lamports;
    std::vector<uint8_t> data;
    PublicKey owner;
    bool executable;
    Slot rent_epoch;
    
    AccountInfo() = default;
    AccountInfo(const AccountInfo&) = default;
    AccountInfo(AccountInfo&&) = default;
    AccountInfo& operator=(const AccountInfo&) = default;
    AccountInfo& operator=(AccountInfo&&) = default;
};

/**
 * SVM execution result
 */
enum class ExecutionResult {
    SUCCESS,
    COMPUTE_BUDGET_EXCEEDED,
    PROGRAM_ERROR,
    ACCOUNT_NOT_FOUND,
    INSUFFICIENT_FUNDS,
    INVALID_INSTRUCTION,
    FAILED  // Added for backward compatibility
};

struct ExecutionOutcome {
    ExecutionResult result;
    uint64_t compute_units_consumed;
    std::vector<ProgramAccount> modified_accounts;
    std::string error_details;
    std::string logs;  // Added for program output logs
    
    bool is_success() const { return result == ExecutionResult::SUCCESS; }
};

/**
 * Built-in program interface
 */
class BuiltinProgram {
public:
    virtual ~BuiltinProgram() = default;
    virtual PublicKey get_program_id() const = 0;
    virtual ExecutionOutcome execute(
        const Instruction& instruction,
        ExecutionContext& context
    ) const = 0;
};

/**
 * System program for basic operations (transfer, account creation, etc.)
 */
class SystemProgram : public BuiltinProgram {
public:
    SystemProgram();
    ~SystemProgram() override;
    
    PublicKey get_program_id() const override;
    ExecutionOutcome execute(
        const Instruction& instruction,
        ExecutionContext& context
    ) const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * SVM execution engine
 */
class ExecutionEngine {
public:
    ExecutionEngine();
    ~ExecutionEngine();
    
    // Program management
    Result<bool> load_program(const ProgramAccount& program);
    void register_builtin_program(std::unique_ptr<BuiltinProgram> program);
    bool is_program_loaded(const PublicKey& program_id) const;
    
    // Transaction execution
    ExecutionOutcome execute_transaction(
        const std::vector<Instruction>& instructions,
        std::unordered_map<PublicKey, ProgramAccount>& accounts
    );
    
    // Configuration
    void set_compute_budget(uint64_t max_compute_units);
    void set_feature_set(const std::vector<std::string>& features);
    
    // Statistics
    uint64_t get_total_instructions_executed() const;
    uint64_t get_total_compute_units_consumed() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Account state manager for SVM
 */
class AccountManager {
public:
    AccountManager();
    ~AccountManager();
    
    // Account operations
    Result<bool> create_account(const ProgramAccount& account);
    std::optional<ProgramAccount> get_account(const PublicKey& pubkey) const;
    Result<bool> update_account(const ProgramAccount& account);
    
    // Account queries
    std::vector<ProgramAccount> get_program_accounts(const PublicKey& program_id) const;
    std::vector<ProgramAccount> get_accounts_by_owner(const PublicKey& owner) const;
    std::vector<ProgramAccount> get_all_accounts() const;
    bool account_exists(const PublicKey& pubkey) const;
    Lamports get_account_balance(const PublicKey& pubkey) const;
    
    // State management
    Result<bool> commit_changes();
    void rollback_changes();
    
    // Rent collection
    Result<bool> collect_rent(Epoch epoch);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace svm
} // namespace slonana