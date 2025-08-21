#include "svm/engine.h"
#include <iostream>
#include <algorithm>
#include <optional>

namespace slonana {
namespace svm {

// ProgramAccount implementation
std::vector<uint8_t> ProgramAccount::serialize() const {
    std::vector<uint8_t> result;
    
    // Add program_id
    result.insert(result.end(), program_id.begin(), program_id.end());
    
    // Add lamports as 8 bytes
    for (int i = 0; i < 8; ++i) {
        result.push_back((lamports >> (i * 8)) & 0xFF);
    }
    
    // Add data length and data
    uint32_t data_len = data.size();
    for (int i = 0; i < 4; ++i) {
        result.push_back((data_len >> (i * 8)) & 0xFF);
    }
    result.insert(result.end(), data.begin(), data.end());
    
    return result;
}

ProgramAccount ProgramAccount::deserialize(const std::vector<uint8_t>& data) {
    ProgramAccount account;
    
    if (data.size() < 32 + 8 + 4) {
        return account; // Invalid data
    }
    
    // Extract program_id (32 bytes)
    account.program_id.assign(data.begin(), data.begin() + 32);
    
    // Extract lamports (8 bytes)
    account.lamports = 0;
    for (int i = 0; i < 8; ++i) {
        account.lamports |= static_cast<uint64_t>(data[32 + i]) << (i * 8);
    }
    
    // Extract data length and data
    uint32_t data_len = 0;
    for (int i = 0; i < 4; ++i) {
        data_len |= static_cast<uint32_t>(data[32 + 8 + i]) << (i * 8);
    }
    
    if (data.size() >= 32 + 8 + 4 + data_len) {
        account.data.assign(data.begin() + 32 + 8 + 4, data.begin() + 32 + 8 + 4 + data_len);
    }
    
    return account;
}

// Instruction implementation
std::vector<uint8_t> Instruction::serialize() const {
    std::vector<uint8_t> result;
    
    // Add program_id
    result.insert(result.end(), program_id.begin(), program_id.end());
    
    // Add accounts count and accounts
    uint8_t accounts_count = accounts.size();
    result.push_back(accounts_count);
    for (const auto& account : accounts) {
        result.insert(result.end(), account.begin(), account.end());
    }
    
    // Add data length and data
    uint32_t data_len = data.size();
    for (int i = 0; i < 4; ++i) {
        result.push_back((data_len >> (i * 8)) & 0xFF);
    }
    result.insert(result.end(), data.begin(), data.end());
    
    return result;
}

Instruction Instruction::deserialize(const std::vector<uint8_t>& data) {
    Instruction instruction;
    
    if (data.size() < 32 + 1) {
        return instruction; // Invalid data
    }
    
    size_t offset = 0;
    
    // Extract program_id
    instruction.program_id.assign(data.begin(), data.begin() + 32);
    offset += 32;
    
    // Extract accounts
    uint8_t accounts_count = data[offset++];
    for (uint8_t i = 0; i < accounts_count && offset + 32 <= data.size(); ++i) {
        PublicKey account;
        account.assign(data.begin() + offset, data.begin() + offset + 32);
        instruction.accounts.push_back(account);
        offset += 32;
    }
    
    // Extract data
    if (offset + 4 <= data.size()) {
        uint32_t data_len = 0;
        for (int i = 0; i < 4; ++i) {
            data_len |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
        }
        offset += 4;
        
        if (offset + data_len <= data.size()) {
            instruction.data.assign(data.begin() + offset, data.begin() + offset + data_len);
        }
    }
    
    return instruction;
}

// SystemProgram implementation
class SystemProgram::Impl {
public:
    PublicKey program_id_;
    
    Impl() {
        // Initialize with a known system program ID (all zeros for stub)
        program_id_.resize(32, 0);
    }
};

SystemProgram::SystemProgram() : impl_(std::make_unique<Impl>()) {}
SystemProgram::~SystemProgram() = default;

PublicKey SystemProgram::get_program_id() const {
    return impl_->program_id_;
}

ExecutionOutcome SystemProgram::execute(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    ExecutionOutcome outcome;
    outcome.result = ExecutionResult::SUCCESS;
    outcome.compute_units_consumed = 100; // Base cost for system operations
    
    // Stub implementation - would handle different system program instructions
    if (instruction.data.empty()) {
        outcome.result = ExecutionResult::INVALID_INSTRUCTION;
        outcome.error_details = "Empty instruction data";
        return outcome;
    }
    
    uint8_t instruction_type = instruction.data[0];
    
    switch (instruction_type) {
        case 0: // Transfer
            std::cout << "Executing system transfer instruction" << std::endl;
            break;
        case 1: // Create account
            std::cout << "Executing create account instruction" << std::endl;
            outcome.compute_units_consumed = 500;
            break;
        default:
            outcome.result = ExecutionResult::INVALID_INSTRUCTION;
            outcome.error_details = "Unknown system instruction type";
            break;
    }
    
    context.consumed_compute_units += outcome.compute_units_consumed;
    return outcome;
}

// ExecutionEngine implementation
class ExecutionEngine::Impl {
public:
    std::unordered_map<PublicKey, ProgramAccount> loaded_programs_;
    std::vector<std::unique_ptr<BuiltinProgram>> builtin_programs_;
    uint64_t max_compute_units_ = 200000; // Default compute budget
    std::vector<std::string> feature_set_;
    
    // Statistics
    uint64_t total_instructions_executed_ = 0;
    uint64_t total_compute_units_consumed_ = 0;
};

ExecutionEngine::ExecutionEngine() : impl_(std::make_unique<Impl>()) {
    // Register default system program
    register_builtin_program(std::make_unique<SystemProgram>());
}

ExecutionEngine::~ExecutionEngine() = default;

common::Result<bool> ExecutionEngine::load_program(const ProgramAccount& program) {
    if (!program.executable) {
        return common::Result<bool>("Account is not executable");
    }
    
    impl_->loaded_programs_[program.program_id] = program;
    std::cout << "Loaded program: " << program.program_id.size() << " bytes ID" << std::endl;
    return common::Result<bool>(true);
}

void ExecutionEngine::register_builtin_program(std::unique_ptr<BuiltinProgram> program) {
    std::cout << "Registered builtin program" << std::endl;
    impl_->builtin_programs_.push_back(std::move(program));
}

bool ExecutionEngine::is_program_loaded(const PublicKey& program_id) const {
    // Check loaded programs
    if (impl_->loaded_programs_.find(program_id) != impl_->loaded_programs_.end()) {
        return true;
    }
    
    // Check builtin programs
    for (const auto& builtin : impl_->builtin_programs_) {
        if (builtin->get_program_id() == program_id) {
            return true;
        }
    }
    
    return false;
}

ExecutionOutcome ExecutionEngine::execute_transaction(
    const std::vector<Instruction>& instructions,
    std::unordered_map<PublicKey, ProgramAccount>& accounts) {
    
    ExecutionOutcome final_outcome;
    final_outcome.result = ExecutionResult::SUCCESS;
    final_outcome.compute_units_consumed = 0;
    
    ExecutionContext context;
    context.instructions = instructions;
    context.accounts = accounts;
    context.max_compute_units = impl_->max_compute_units_;
    
    for (const auto& instruction : instructions) {
        // Check if we have compute budget left
        if (context.consumed_compute_units >= context.max_compute_units) {
            final_outcome.result = ExecutionResult::COMPUTE_BUDGET_EXCEEDED;
            final_outcome.error_details = "Transaction exceeded compute budget";
            break;
        }
        
        // Find the program to execute
        BuiltinProgram* program_to_execute = nullptr;
        
        for (const auto& builtin : impl_->builtin_programs_) {
            if (builtin->get_program_id() == instruction.program_id) {
                program_to_execute = builtin.get();
                break;
            }
        }
        
        if (!program_to_execute) {
            final_outcome.result = ExecutionResult::PROGRAM_ERROR;
            final_outcome.error_details = "Program not found";
            break;
        }
        
        // Execute the instruction
        auto outcome = program_to_execute->execute(instruction, context);
        final_outcome.compute_units_consumed += outcome.compute_units_consumed;
        
        if (outcome.result != ExecutionResult::SUCCESS) {
            final_outcome.result = outcome.result;
            final_outcome.error_details = outcome.error_details;
            break;
        }
        
        // Merge modified accounts
        final_outcome.modified_accounts.insert(
            final_outcome.modified_accounts.end(),
            outcome.modified_accounts.begin(),
            outcome.modified_accounts.end()
        );
        
        impl_->total_instructions_executed_++;
    }
    
    impl_->total_compute_units_consumed_ += final_outcome.compute_units_consumed;
    
    // Update the provided accounts with modifications
    for (const auto& modified_account : final_outcome.modified_accounts) {
        accounts[modified_account.program_id] = modified_account;
    }
    
    return final_outcome;
}

void ExecutionEngine::set_compute_budget(uint64_t max_compute_units) {
    impl_->max_compute_units_ = max_compute_units;
    std::cout << "Set compute budget to " << max_compute_units << " units" << std::endl;
}

void ExecutionEngine::set_feature_set(const std::vector<std::string>& features) {
    impl_->feature_set_ = features;
    std::cout << "Updated feature set with " << features.size() << " features" << std::endl;
}

uint64_t ExecutionEngine::get_total_instructions_executed() const {
    return impl_->total_instructions_executed_;
}

uint64_t ExecutionEngine::get_total_compute_units_consumed() const {
    return impl_->total_compute_units_consumed_;
}

// AccountManager implementation
class AccountManager::Impl {
public:
    std::unordered_map<PublicKey, ProgramAccount> accounts_;
    std::unordered_map<PublicKey, ProgramAccount> pending_changes_;
    bool transaction_active_ = false;
};

AccountManager::AccountManager() : impl_(std::make_unique<Impl>()) {}
AccountManager::~AccountManager() = default;

common::Result<bool> AccountManager::create_account(const ProgramAccount& account) {
    if (account_exists(account.program_id)) {
        return common::Result<bool>("Account already exists");
    }
    
    impl_->pending_changes_[account.program_id] = account;
    std::cout << "Created account with " << account.lamports << " lamports" << std::endl;
    return common::Result<bool>(true);
}

std::optional<ProgramAccount> AccountManager::get_account(const PublicKey& pubkey) const {
    // Check pending changes first
    auto pending_it = impl_->pending_changes_.find(pubkey);
    if (pending_it != impl_->pending_changes_.end()) {
        return pending_it->second;
    }
    
    // Check committed accounts
    auto it = impl_->accounts_.find(pubkey);
    if (it != impl_->accounts_.end()) {
        return it->second;
    }
    
    return std::nullopt;
}

common::Result<bool> AccountManager::update_account(const ProgramAccount& account) {
    impl_->pending_changes_[account.program_id] = account;
    std::cout << "Updated account" << std::endl;
    return common::Result<bool>(true);
}

std::vector<ProgramAccount> AccountManager::get_program_accounts(const PublicKey& program_id) const {
    std::vector<ProgramAccount> result;
    
    for (const auto& [pubkey, account] : impl_->accounts_) {
        if (account.owner == program_id) {
            result.push_back(account);
        }
    }
    
    // Also check pending changes
    for (const auto& [pubkey, account] : impl_->pending_changes_) {
        if (account.owner == program_id) {
            result.push_back(account);
        }
    }
    
    return result;
}

bool AccountManager::account_exists(const PublicKey& pubkey) const {
    return impl_->accounts_.find(pubkey) != impl_->accounts_.end() ||
           impl_->pending_changes_.find(pubkey) != impl_->pending_changes_.end();
}

common::Lamports AccountManager::get_account_balance(const PublicKey& pubkey) const {
    auto account = get_account(pubkey);
    return account ? account->lamports : 0;
}

common::Result<bool> AccountManager::commit_changes() {
    for (const auto& [pubkey, account] : impl_->pending_changes_) {
        impl_->accounts_[pubkey] = account;
    }
    impl_->pending_changes_.clear();
    
    std::cout << "Committed account changes" << std::endl;
    return common::Result<bool>(true);
}

void AccountManager::rollback_changes() {
    impl_->pending_changes_.clear();
    std::cout << "Rolled back account changes" << std::endl;
}

common::Result<bool> AccountManager::collect_rent(common::Epoch epoch) {
    std::cout << "Collecting rent for epoch " << epoch << std::endl;
    
    // Stub implementation - would calculate and collect rent from accounts
    for (auto& [pubkey, account] : impl_->accounts_) {
        if (account.lamports > 1000) { // Minimum balance to avoid rent
            continue;
        }
        
        // Calculate rent (simplified)
        common::Lamports rent = 100; // Fixed rent amount for stub
        if (account.lamports >= rent) {
            account.lamports -= rent;
            std::cout << "Collected " << rent << " lamports rent from account" << std::endl;
        }
    }
    
    return common::Result<bool>(true);
}

} // namespace svm
} // namespace slonana