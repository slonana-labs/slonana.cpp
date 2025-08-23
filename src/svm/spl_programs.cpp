#include "svm/spl_programs.h"
#include <algorithm>
#include <random>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <cstring>

namespace slonana {
namespace svm {

// Program IDs (using placeholder values - would be actual Solana program IDs in production)
const PublicKey SPLAssociatedTokenProgram::ATA_PROGRAM_ID = PublicKey(32, 0x08);
const std::string SPLAssociatedTokenProgram::ATA_SEED = "ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL";

const PublicKey SPLMemoProgram::MEMO_PROGRAM_ID = PublicKey(32, 0x09);

const PublicKey ExtendedSystemProgram::EXTENDED_SYSTEM_PROGRAM_ID = PublicKey(32, 0x0A);

// SPL Associated Token Account Program Implementation
SPLAssociatedTokenProgram::SPLAssociatedTokenProgram() {
    std::cout << "SPL ATA: Program initialized" << std::endl;
}

PublicKey SPLAssociatedTokenProgram::get_program_id() const {
    return ATA_PROGRAM_ID;
}

ExecutionOutcome SPLAssociatedTokenProgram::execute(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    if (instruction.data.empty()) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Empty instruction data", ""};
    }
    
    ATAInstruction instr_type = static_cast<ATAInstruction>(instruction.data[0]);
    
    switch (instr_type) {
        case ATAInstruction::Create:
            std::cout << "SPL ATA: Creating associated token account" << std::endl;
            return handle_create_ata(instruction, context);
            
        case ATAInstruction::CreateIdempotent:
            std::cout << "SPL ATA: Creating idempotent associated token account" << std::endl;
            return handle_create_ata_idempotent(instruction, context);
            
        case ATAInstruction::RecoverNested:
            std::cout << "SPL ATA: Recovering nested account" << std::endl;
            return {ExecutionResult::SUCCESS, 1000, {}, "Nested account recovered", ""};
            
        default:
            return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Unknown ATA instruction", ""};
    }
}

ExecutionOutcome SPLAssociatedTokenProgram::handle_create_ata(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    // Parse instruction parameters
    auto params_result = parse_create_instruction(instruction.data);
    if (!params_result.is_ok()) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Invalid create ATA parameters", ""};
    }
    
    auto params = params_result.value();
    
    // Derive the associated token address
    PublicKey ata_address = derive_associated_token_address(
        params.wallet_address, params.token_mint);
    
    // Check if account already exists
    auto account_it = context.accounts.find(ata_address);
    if (account_it != context.accounts.end()) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Associated token account already exists", ""};
    }
    
    // Create the associated token account
    ProgramAccount ata_account;
    ata_account.program_id = params.token_program;
    ata_account.executable = false;
    ata_account.lamports = 2039280; // Rent-exempt amount for token account
    ata_account.rent_epoch = context.current_epoch;
    
    // Initialize token account data
    ata_account.data.resize(165); // Token account size
    // Set owner
    std::copy(params.wallet_address.begin(), params.wallet_address.end(),
              ata_account.data.begin() + 32);
    // Set mint
    std::copy(params.token_mint.begin(), params.token_mint.end(),
              ata_account.data.begin());
    // Set amount to 0
    uint64_t amount = 0;
    std::memcpy(ata_account.data.data() + 64, &amount, sizeof(amount));
    // Mark as initialized
    ata_account.data[108] = 1;
    
    context.accounts[ata_address] = ata_account;
    
    return {ExecutionResult::SUCCESS, 10000, {}, "Associated token account created", ""};
}

ExecutionOutcome SPLAssociatedTokenProgram::handle_create_ata_idempotent(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    // Parse instruction parameters
    auto params_result = parse_create_instruction(instruction.data);
    if (!params_result.is_ok()) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Invalid create ATA parameters", ""};
    }
    
    auto params = params_result.value();
    
    // Derive the associated token address
    PublicKey ata_address = derive_associated_token_address(
        params.wallet_address, params.token_mint);
    
    // Check if account already exists
    auto account_it = context.accounts.find(ata_address);
    if (account_it != context.accounts.end()) {
        // Idempotent - return success if already exists
        return {ExecutionResult::SUCCESS, 1000, {}, "Associated token account already exists", ""};
    }
    
    // Create the account (same as regular create)
    return handle_create_ata(instruction, context);
}

PublicKey SPLAssociatedTokenProgram::derive_associated_token_address(
    const PublicKey& wallet_address,
    const PublicKey& token_mint) const {
    
    // Simplified derivation - in production would use proper PDA derivation
    PublicKey derived_address;
    derived_address.resize(32);
    
    // Combine wallet and mint for deterministic address
    for (size_t i = 0; i < 32; ++i) {
        derived_address[i] = wallet_address[i] ^ token_mint[i];
    }
    
    // XOR with program ID for uniqueness
    for (size_t i = 0; i < 32; ++i) {
        derived_address[i] ^= ATA_PROGRAM_ID[i];
    }
    
    return derived_address;
}

Result<SPLAssociatedTokenProgram::CreateATAParams> 
SPLAssociatedTokenProgram::parse_create_instruction(
    const std::vector<uint8_t>& data) const {
    
    if (data.size() < 1 + 32 * 4) { // instruction + 4 pubkeys
        return Result<CreateATAParams>("Insufficient instruction data");
    }
    
    CreateATAParams params;
    size_t offset = 1; // Skip instruction byte
    
    // Parse funding account
    std::copy(data.begin() + offset, data.begin() + offset + 32,
              params.funding_account.begin());
    offset += 32;
    
    // Parse wallet address
    std::copy(data.begin() + offset, data.begin() + offset + 32,
              params.wallet_address.begin());
    offset += 32;
    
    // Parse token mint
    std::copy(data.begin() + offset, data.begin() + offset + 32,
              params.token_mint.begin());
    offset += 32;
    
    // Parse token program
    std::copy(data.begin() + offset, data.begin() + offset + 32,
              params.token_program.begin());
    
    return Result<CreateATAParams>(params);
}

// SPL Memo Program Implementation
SPLMemoProgram::SPLMemoProgram() {
    std::cout << "SPL Memo: Program initialized" << std::endl;
}

PublicKey SPLMemoProgram::get_program_id() const {
    return MEMO_PROGRAM_ID;
}

ExecutionOutcome SPLMemoProgram::execute(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    // Validate memo data
    if (!validate_memo_data(instruction.data)) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Invalid memo data", ""};
    }
    
    // Extract and log memo text
    std::string memo_text = extract_memo_text(instruction.data);
    std::cout << "SPL Memo: " << memo_text << std::endl;
    
    // Memo program doesn't modify any accounts, just validates and logs
    return {ExecutionResult::SUCCESS, 1000, {}, "", "Memo processed: " + memo_text};
}

bool SPLMemoProgram::validate_memo_data(const std::vector<uint8_t>& data) const {
    // Check length constraints
    if (data.empty() || data.size() > MAX_MEMO_LENGTH) {
        return false;
    }
    
    // Check for valid UTF-8 (simplified check)
    for (uint8_t byte : data) {
        if (byte == 0) {
            return false; // No null bytes allowed
        }
    }
    
    return true;
}

std::string SPLMemoProgram::extract_memo_text(const std::vector<uint8_t>& data) const {
    return std::string(data.begin(), data.end());
}

// Extended System Program Implementation
ExtendedSystemProgram::ExtendedSystemProgram() {
    std::cout << "Extended System: Program initialized" << std::endl;
}

PublicKey ExtendedSystemProgram::get_program_id() const {
    return EXTENDED_SYSTEM_PROGRAM_ID;
}

ExecutionOutcome ExtendedSystemProgram::execute(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    if (instruction.data.empty()) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Empty instruction data", ""};
    }
    
    ExtendedSystemInstruction instr_type = 
        static_cast<ExtendedSystemInstruction>(instruction.data[0]);
    
    switch (instr_type) {
        case ExtendedSystemInstruction::InitializeNonceAccount:
            std::cout << "Extended System: Initializing nonce account" << std::endl;
            return handle_initialize_nonce(instruction, context);
            
        case ExtendedSystemInstruction::AdvanceNonceAccount:
            std::cout << "Extended System: Advancing nonce account" << std::endl;
            return handle_advance_nonce(instruction, context);
            
        case ExtendedSystemInstruction::WithdrawNonceAccount:
            std::cout << "Extended System: Withdrawing from nonce account" << std::endl;
            return handle_withdraw_nonce(instruction, context);
            
        default:
            return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Unknown extended system instruction", ""};
    }
}

ExecutionOutcome ExtendedSystemProgram::handle_initialize_nonce(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    if (instruction.accounts.empty()) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "No accounts provided", ""};
    }
    
    const PublicKey& nonce_account_key = instruction.accounts[0];
    
    // Check if account already exists and is initialized
    auto account_it = context.accounts.find(nonce_account_key);
    if (account_it != context.accounts.end() && 
        account_it->second.data.size() >= NONCE_ACCOUNT_SIZE &&
        account_it->second.data[NONCE_ACCOUNT_SIZE - 1] == 1) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Nonce account already initialized", ""};
    }
    
    // Create or update nonce account
    ProgramAccount& nonce_account = context.accounts[nonce_account_key];
    nonce_account.program_id = EXTENDED_SYSTEM_PROGRAM_ID;
    nonce_account.executable = false;
    nonce_account.lamports = std::max(nonce_account.lamports, 1447680UL); // Rent-exempt
    nonce_account.rent_epoch = context.current_epoch;
    
    // Initialize nonce account data
    NonceAccount nonce_data;
    nonce_data.authority = instruction.accounts.size() > 1 ? 
        instruction.accounts[1] : nonce_account_key;
    nonce_data.nonce = generate_nonce_value();
    nonce_data.lamports = nonce_account.lamports;
    nonce_data.initialized = true;
    
    nonce_account.data = serialize_nonce_account(nonce_data);
    
    return {ExecutionResult::SUCCESS, 5000, {}, "Nonce account initialized", ""};
}

ExecutionOutcome ExtendedSystemProgram::handle_advance_nonce(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    if (instruction.accounts.empty()) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "No accounts provided", ""};
    }
    
    const PublicKey& nonce_account_key = instruction.accounts[0];
    
    auto account_it = context.accounts.find(nonce_account_key);
    if (account_it == context.accounts.end()) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Nonce account not found", ""};
    }
    
    auto nonce_result = deserialize_nonce_account(account_it->second.data);
    if (!nonce_result.is_ok()) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Invalid nonce account", ""};
    }
    
    NonceAccount nonce_data = nonce_result.value();
    if (!nonce_data.initialized) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Nonce account not initialized", ""};
    }
    
    // Generate new nonce value
    nonce_data.nonce = generate_nonce_value();
    
    account_it->second.data = serialize_nonce_account(nonce_data);
    
    return {ExecutionResult::SUCCESS, 2000, {}, "Nonce account advanced", ""};
}

ExecutionOutcome ExtendedSystemProgram::handle_withdraw_nonce(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    if (instruction.accounts.size() < 2) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Insufficient accounts", ""};
    }
    
    const PublicKey& nonce_account_key = instruction.accounts[0];
    const PublicKey& destination_key = instruction.accounts[1];
    
    auto nonce_it = context.accounts.find(nonce_account_key);
    if (nonce_it == context.accounts.end()) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Nonce account not found", ""};
    }
    
    // Parse withdrawal amount from instruction data
    if (instruction.data.size() < 9) { // 1 byte instruction + 8 bytes amount
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Invalid withdrawal instruction", ""};
    }
    
    uint64_t withdraw_amount;
    std::memcpy(&withdraw_amount, instruction.data.data() + 1, sizeof(withdraw_amount));
    
    if (nonce_it->second.lamports < withdraw_amount) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Insufficient funds", ""};
    }
    
    // Transfer lamports
    nonce_it->second.lamports -= withdraw_amount;
    context.accounts[destination_key].lamports += withdraw_amount;
    
    return {ExecutionResult::SUCCESS, 3000, {}, "Withdrawal successful", ""};
}

Result<ExtendedSystemProgram::NonceAccount> 
ExtendedSystemProgram::deserialize_nonce_account(
    const std::vector<uint8_t>& data) const {
    
    if (data.size() < NONCE_ACCOUNT_SIZE) {
        return Result<NonceAccount>("Invalid nonce account size");
    }
    
    NonceAccount account;
    size_t offset = 0;
    
    // Parse authority
    std::copy(data.begin() + offset, data.begin() + offset + 32,
              account.authority.begin());
    offset += 32;
    
    // Parse nonce
    std::copy(data.begin() + offset, data.begin() + offset + 32,
              account.nonce.begin());
    offset += 32;
    
    // Parse lamports
    std::memcpy(&account.lamports, data.data() + offset, sizeof(account.lamports));
    offset += 8;
    
    // Parse initialized flag
    account.initialized = data[offset] == 1;
    
    return Result<NonceAccount>(account);
}

std::vector<uint8_t> ExtendedSystemProgram::serialize_nonce_account(
    const NonceAccount& account) const {
    
    std::vector<uint8_t> data(NONCE_ACCOUNT_SIZE);
    size_t offset = 0;
    
    // Authority
    std::copy(account.authority.begin(), account.authority.end(),
              data.begin() + offset);
    offset += 32;
    
    // Nonce
    std::copy(account.nonce.begin(), account.nonce.end(),
              data.begin() + offset);
    offset += 32;
    
    // Lamports
    std::memcpy(data.data() + offset, &account.lamports, sizeof(account.lamports));
    offset += 8;
    
    // Initialized flag
    data[offset] = account.initialized ? 1 : 0;
    
    return data;
}

PublicKey ExtendedSystemProgram::generate_nonce_value() const {
    PublicKey nonce;
    nonce.resize(32);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);
    
    for (size_t i = 0; i < 32; ++i) {
        nonce[i] = dis(gen);
    }
    
    return nonce;
}

// SPL Program Registry Implementation
SPLProgramRegistry::SPLProgramRegistry() {
    // Initialize program name mappings
    SPLAssociatedTokenProgram ata_prog;
    SPLMemoProgram memo_prog;
    ExtendedSystemProgram ext_prog;
    
    register_program_name(ata_prog.get_program_id(), "SPL Associated Token Account");
    register_program_name(memo_prog.get_program_id(), "SPL Memo");
    register_program_name(ext_prog.get_program_id(), "Extended System");
}

void SPLProgramRegistry::register_all_programs(ExecutionEngine& engine) {
    register_token_program(engine);
    register_ata_program(engine);
    register_memo_program(engine);
    register_extended_system_program(engine);
    
    std::cout << "SPL Registry: All programs registered (" 
              << program_names_.size() << " programs)" << std::endl;
}

void SPLProgramRegistry::register_token_program(ExecutionEngine& engine) {
    // Note: Token program is registered in enhanced_engine.cpp
    // This would register it if not already done
}

void SPLProgramRegistry::register_ata_program(ExecutionEngine& engine) {
    engine.register_builtin_program(std::make_unique<SPLAssociatedTokenProgram>());
}

void SPLProgramRegistry::register_memo_program(ExecutionEngine& engine) {
    engine.register_builtin_program(std::make_unique<SPLMemoProgram>());
}

void SPLProgramRegistry::register_extended_system_program(ExecutionEngine& engine) {
    engine.register_builtin_program(std::make_unique<ExtendedSystemProgram>());
}

std::vector<PublicKey> SPLProgramRegistry::get_all_program_ids() const {
    std::vector<PublicKey> program_ids;
    program_ids.reserve(program_names_.size());
    
    for (const auto& pair : program_names_) {
        program_ids.push_back(pair.first);
    }
    
    return program_ids;
}

bool SPLProgramRegistry::is_spl_program(const PublicKey& program_id) const {
    return program_names_.find(program_id) != program_names_.end();
}

std::string SPLProgramRegistry::get_program_name(const PublicKey& program_id) const {
    auto it = program_names_.find(program_id);
    return it != program_names_.end() ? it->second : "Unknown Program";
}

void SPLProgramRegistry::register_program_name(
    const PublicKey& program_id, 
    const std::string& name) {
    program_names_[program_id] = name;
}

} // namespace svm
} // namespace slonana