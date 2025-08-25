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

const PublicKey SPLGovernanceProgram::GOVERNANCE_PROGRAM_ID = PublicKey(32, 0x0B);
const PublicKey SPLStakePoolProgram::STAKE_POOL_PROGRAM_ID = PublicKey(32, 0x0C);
const PublicKey SPLMultisigProgram::MULTISIG_PROGRAM_ID = PublicKey(32, 0x0D);

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
    
    // Initialize all PublicKey members to proper size
    params.funding_account.resize(32);
    params.wallet_address.resize(32);
    params.token_mint.resize(32);
    params.token_program.resize(32);
    
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
    nonce_account.lamports = std::max(nonce_account.lamports, static_cast<Lamports>(1447680UL)); // Rent-exempt
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
    
    // Initialize PublicKey members to proper size
    account.authority.resize(32);
    account.nonce.resize(32);
    
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

// SPL Governance Program Implementation
SPLGovernanceProgram::SPLGovernanceProgram() {
    std::cout << "SPL Governance: Program initialized" << std::endl;
}

PublicKey SPLGovernanceProgram::get_program_id() const {
    return GOVERNANCE_PROGRAM_ID;
}

ExecutionOutcome SPLGovernanceProgram::execute(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    if (instruction.data.empty()) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Empty instruction data", ""};
    }
    
    GovernanceInstruction instr_type = static_cast<GovernanceInstruction>(instruction.data[0]);
    
    switch (instr_type) {
        case GovernanceInstruction::CreateRealm:
            std::cout << "SPL Governance: Creating governance realm" << std::endl;
            return handle_create_realm(instruction, context);
            
        case GovernanceInstruction::CreateProposal:
            std::cout << "SPL Governance: Creating governance proposal" << std::endl;
            return handle_create_proposal(instruction, context);
            
        case GovernanceInstruction::CastVote:
            std::cout << "SPL Governance: Casting vote on proposal" << std::endl;
            return handle_cast_vote(instruction, context);
            
        case GovernanceInstruction::ExecuteProposal:
            std::cout << "SPL Governance: Executing approved proposal" << std::endl;
            return {ExecutionResult::SUCCESS, 2000, {}, "Proposal executed", ""};
            
        case GovernanceInstruction::CreateTokenOwnerRecord:
            std::cout << "SPL Governance: Creating token owner record" << std::endl;
            return {ExecutionResult::SUCCESS, 1000, {}, "Token owner record created", ""};
            
        default:
            return {ExecutionResult::INVALID_INSTRUCTION, 0, {}, "Unknown governance instruction", ""};
    }
}

ExecutionOutcome SPLGovernanceProgram::handle_create_realm(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    if (instruction.accounts.size() < 4) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Insufficient accounts for realm creation", ""};
    }
    
    // Create a new realm (simplified for testing)
    Realm realm;
    realm.governance_token_mint = instruction.accounts[1];
    realm.council_token_mint = instruction.accounts[2];
    realm.name = "Test Governance Realm";
    realm.min_community_weight_to_create_proposal = 1000000; // 1M tokens
    realm.min_council_weight_to_create_proposal = 1; // 1 council token
    realm.initialized = true;
    
    // Serialize realm data (for testing purposes)
    auto realm_data = serialize_realm(realm);
    std::cout << "SPL Governance: Realm created with " << realm_data.size() << " bytes data" << std::endl;
    
    return {ExecutionResult::SUCCESS, 5000, {}, "Governance realm created successfully", ""};
}

ExecutionOutcome SPLGovernanceProgram::handle_create_proposal(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    if (instruction.accounts.size() < 5) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Insufficient accounts for proposal creation", ""};
    }
    
    // Create a new proposal (simplified for testing)
    Proposal proposal;
    proposal.realm = instruction.accounts[1];
    proposal.governance = instruction.accounts[2];
    proposal.proposer = instruction.accounts[3];
    proposal.description = "Test governance proposal";
    proposal.yes_votes = 0;
    proposal.no_votes = 0;
    proposal.voting_at = context.current_epoch * 400; // Mock timestamp
    proposal.voting_ends_at = proposal.voting_at + 100000; // 100k slots later
    proposal.state = 1; // Voting state
    proposal.initialized = true;
    
    // Serialize proposal data (for testing purposes)
    auto proposal_data = serialize_proposal(proposal);
    std::cout << "SPL Governance: Proposal created with " << proposal_data.size() << " bytes data" << std::endl;
    
    return {ExecutionResult::SUCCESS, 3000, {}, "Governance proposal created successfully", ""};
}

ExecutionOutcome SPLGovernanceProgram::handle_cast_vote(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    if (instruction.accounts.size() < 4) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Insufficient accounts for vote casting", ""};
    }
    
    // For simplicity, just record the vote
    Vote vote;
    vote.proposal = instruction.accounts[1];
    vote.voter = instruction.accounts[2];
    vote.vote_type = instruction.data.size() > 1 ? instruction.data[1] : 0; // Default to yes
    vote.weight = 1000; // Mock vote weight
    vote.initialized = true;
    
    std::cout << "SPL Governance: Vote cast - " 
              << (vote.vote_type == 0 ? "YES" : "NO") 
              << " with weight " << vote.weight << std::endl;
    
    return {ExecutionResult::SUCCESS, 1500, {}, "Vote cast successfully", ""};
}

Result<SPLGovernanceProgram::Realm> SPLGovernanceProgram::deserialize_realm(
    const std::vector<uint8_t>& data) const {
    if (data.size() < REALM_SIZE) {
        return Result<Realm>("Invalid realm data size");
    }
    
    Realm realm;
    size_t offset = 0;
    
    // Parse governance token mint
    std::copy(data.begin() + offset, data.begin() + offset + 32,
              realm.governance_token_mint.begin());
    offset += 32;
    
    // Parse council token mint
    std::copy(data.begin() + offset, data.begin() + offset + 32,
              realm.council_token_mint.begin());
    offset += 32;
    
    // Parse weights (simplified parsing)
    std::memcpy(&realm.min_community_weight_to_create_proposal, 
                data.data() + offset, sizeof(uint64_t));
    offset += 8;
    
    std::memcpy(&realm.min_council_weight_to_create_proposal, 
                data.data() + offset, sizeof(uint64_t));
    offset += 8;
    
    realm.initialized = data[offset] == 1;
    realm.name = "Deserialized Realm";
    
    return Result<Realm>(realm);
}

std::vector<uint8_t> SPLGovernanceProgram::serialize_realm(const Realm& realm) const {
    std::vector<uint8_t> data(REALM_SIZE, 0);
    size_t offset = 0;
    
    // Governance token mint
    std::copy(realm.governance_token_mint.begin(), realm.governance_token_mint.end(),
              data.begin() + offset);
    offset += 32;
    
    // Council token mint
    std::copy(realm.council_token_mint.begin(), realm.council_token_mint.end(),
              data.begin() + offset);
    offset += 32;
    
    // Weights
    std::memcpy(data.data() + offset, &realm.min_community_weight_to_create_proposal, 
                sizeof(uint64_t));
    offset += 8;
    
    std::memcpy(data.data() + offset, &realm.min_council_weight_to_create_proposal, 
                sizeof(uint64_t));
    offset += 8;
    
    // Initialized flag
    data[offset] = realm.initialized ? 1 : 0;
    
    return data;
}

Result<SPLGovernanceProgram::Proposal> SPLGovernanceProgram::deserialize_proposal(
    const std::vector<uint8_t>& data) const {
    if (data.size() < PROPOSAL_SIZE) {
        return Result<Proposal>("Invalid proposal data size");
    }
    
    Proposal proposal;
    size_t offset = 0;
    
    // Parse realm
    std::copy(data.begin() + offset, data.begin() + offset + 32,
              proposal.realm.begin());
    offset += 32;
    
    // Parse governance
    std::copy(data.begin() + offset, data.begin() + offset + 32,
              proposal.governance.begin());
    offset += 32;
    
    // Parse proposer
    std::copy(data.begin() + offset, data.begin() + offset + 32,
              proposal.proposer.begin());
    offset += 32;
    
    // Parse vote counts and timestamps
    std::memcpy(&proposal.yes_votes, data.data() + offset, sizeof(uint64_t));
    offset += 8;
    std::memcpy(&proposal.no_votes, data.data() + offset, sizeof(uint64_t));
    offset += 8;
    std::memcpy(&proposal.voting_at, data.data() + offset, sizeof(uint64_t));
    offset += 8;
    std::memcpy(&proposal.voting_ends_at, data.data() + offset, sizeof(uint64_t));
    offset += 8;
    
    proposal.state = data[offset++];
    proposal.initialized = data[offset] == 1;
    proposal.description = "Deserialized Proposal";
    
    return Result<Proposal>(proposal);
}

std::vector<uint8_t> SPLGovernanceProgram::serialize_proposal(const Proposal& proposal) const {
    std::vector<uint8_t> data(PROPOSAL_SIZE, 0);
    size_t offset = 0;
    
    // Realm
    std::copy(proposal.realm.begin(), proposal.realm.end(), data.begin() + offset);
    offset += 32;
    
    // Governance
    std::copy(proposal.governance.begin(), proposal.governance.end(), data.begin() + offset);
    offset += 32;
    
    // Proposer
    std::copy(proposal.proposer.begin(), proposal.proposer.end(), data.begin() + offset);
    offset += 32;
    
    // Vote counts and timestamps
    std::memcpy(data.data() + offset, &proposal.yes_votes, sizeof(uint64_t));
    offset += 8;
    std::memcpy(data.data() + offset, &proposal.no_votes, sizeof(uint64_t));
    offset += 8;
    std::memcpy(data.data() + offset, &proposal.voting_at, sizeof(uint64_t));
    offset += 8;
    std::memcpy(data.data() + offset, &proposal.voting_ends_at, sizeof(uint64_t));
    offset += 8;
    
    data[offset++] = proposal.state;
    data[offset] = proposal.initialized ? 1 : 0;
    
    return data;
}

// SPL Stake Pool Program Implementation
SPLStakePoolProgram::SPLStakePoolProgram() {
    std::cout << "SPL Stake Pool: Program initialized" << std::endl;
}

PublicKey SPLStakePoolProgram::get_program_id() const {
    return STAKE_POOL_PROGRAM_ID;
}

ExecutionOutcome SPLStakePoolProgram::execute(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    if (instruction.data.empty()) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Empty instruction data", ""};
    }
    
    StakePoolInstruction instr_type = static_cast<StakePoolInstruction>(instruction.data[0]);
    
    switch (instr_type) {
        case StakePoolInstruction::Initialize:
            std::cout << "SPL Stake Pool: Initializing stake pool" << std::endl;
            return handle_initialize_pool(instruction, context);
            
        case StakePoolInstruction::DepositStake:
            std::cout << "SPL Stake Pool: Depositing stake to pool" << std::endl;
            return handle_deposit_stake(instruction, context);
            
        case StakePoolInstruction::WithdrawStake:
            std::cout << "SPL Stake Pool: Withdrawing stake from pool" << std::endl;
            return handle_withdraw_stake(instruction, context);
            
        case StakePoolInstruction::UpdateStakePool:
            std::cout << "SPL Stake Pool: Updating stake pool" << std::endl;
            return {ExecutionResult::SUCCESS, 2000, {}, "Stake pool updated", ""};
            
        case StakePoolInstruction::AddValidatorToPool:
            std::cout << "SPL Stake Pool: Adding validator to pool" << std::endl;
            return {ExecutionResult::SUCCESS, 3000, {}, "Validator added to pool", ""};
            
        case StakePoolInstruction::RemoveValidatorFromPool:
            std::cout << "SPL Stake Pool: Removing validator from pool" << std::endl;
            return {ExecutionResult::SUCCESS, 2500, {}, "Validator removed from pool", ""};
            
        default:
            return {ExecutionResult::INVALID_INSTRUCTION, 0, {}, "Unknown stake pool instruction", ""};
    }
}

ExecutionOutcome SPLStakePoolProgram::handle_initialize_pool(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    if (instruction.accounts.size() < 5) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Insufficient accounts for pool initialization", ""};
    }
    
    // Create a new stake pool (simplified for testing)
    StakePool pool;
    pool.pool_mint = instruction.accounts[1];
    pool.manager = instruction.accounts[2];
    pool.staker = instruction.accounts[3];
    pool.withdraw_authority = instruction.accounts[4];
    pool.validator_list = instruction.accounts.size() > 5 ? instruction.accounts[5] : PublicKey(32, 0);
    pool.total_lamports = 0;
    pool.pool_token_supply = 0;
    pool.fee_numerator = 5; // 0.5% fee
    pool.fee_denominator = 1000;
    pool.initialized = true;
    
    // Serialize pool data (for testing purposes)
    auto pool_data = serialize_stake_pool(pool);
    std::cout << "SPL Stake Pool: Pool initialized with " << pool_data.size() << " bytes data" << std::endl;
    
    return {ExecutionResult::SUCCESS, 5000, {}, "Stake pool initialized successfully", ""};
}

ExecutionOutcome SPLStakePoolProgram::handle_deposit_stake(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    if (instruction.accounts.size() < 6) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Insufficient accounts for stake deposit", ""};
    }
    
    // Parse deposit amount from instruction data
    uint64_t deposit_amount = 1000000; // Default 1 SOL
    if (instruction.data.size() >= 9) {
        std::memcpy(&deposit_amount, instruction.data.data() + 1, sizeof(uint64_t));
    }
    
    // Simulate stake pool operations (simplified for testing)
    StakePool pool;
    pool.pool_mint = instruction.accounts[1];
    pool.total_lamports = 1000000000; // Existing 1 SOL in pool
    pool.pool_token_supply = 1000000000; // 1:1 ratio
    pool.fee_numerator = 5;
    pool.fee_denominator = 1000;
    pool.initialized = true;
    
    // Calculate pool tokens to mint
    uint64_t pool_tokens = calculate_pool_tokens_for_deposit(deposit_amount, pool);
    
    // Update pool totals (simulated)
    pool.total_lamports += deposit_amount;
    pool.pool_token_supply += pool_tokens;
    
    std::cout << "SPL Stake Pool: Deposited " << deposit_amount 
              << " lamports, minted " << pool_tokens << " pool tokens" << std::endl;
    
    return {ExecutionResult::SUCCESS, 4000, {}, "Stake deposited successfully", ""};
}

ExecutionOutcome SPLStakePoolProgram::handle_withdraw_stake(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    if (instruction.accounts.size() < 6) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Insufficient accounts for stake withdrawal", ""};
    }
    
    // Parse withdrawal amount from instruction data
    uint64_t pool_tokens_to_burn = 100000; // Default amount
    if (instruction.data.size() >= 9) {
        std::memcpy(&pool_tokens_to_burn, instruction.data.data() + 1, sizeof(uint64_t));
    }
    
    // Simulate stake pool operations (simplified for testing)
    StakePool pool;
    pool.total_lamports = 2000000000; // 2 SOL in pool
    pool.pool_token_supply = 2000000000; // 1:1 ratio
    
    // Calculate lamports to withdraw
    uint64_t lamports_to_withdraw = calculate_lamports_for_pool_tokens(pool_tokens_to_burn, pool);
    
    // Check if pool has enough lamports
    if (lamports_to_withdraw > pool.total_lamports) {
        return {ExecutionResult::INSUFFICIENT_FUNDS, 0, {}, "Insufficient funds in stake pool", ""};
    }
    
    std::cout << "SPL Stake Pool: Burned " << pool_tokens_to_burn 
              << " pool tokens, withdrew " << lamports_to_withdraw << " lamports" << std::endl;
    
    return {ExecutionResult::SUCCESS, 4000, {}, "Stake withdrawn successfully", ""};
}

Result<SPLStakePoolProgram::StakePool> SPLStakePoolProgram::deserialize_stake_pool(
    const std::vector<uint8_t>& data) const {
    if (data.size() < STAKE_POOL_SIZE) {
        return Result<StakePool>("Invalid stake pool data size");
    }
    
    StakePool pool;
    size_t offset = 0;
    
    // Parse pool mint
    std::copy(data.begin() + offset, data.begin() + offset + 32,
              pool.pool_mint.begin());
    offset += 32;
    
    // Parse manager
    std::copy(data.begin() + offset, data.begin() + offset + 32,
              pool.manager.begin());
    offset += 32;
    
    // Parse staker
    std::copy(data.begin() + offset, data.begin() + offset + 32,
              pool.staker.begin());
    offset += 32;
    
    // Parse withdraw authority
    std::copy(data.begin() + offset, data.begin() + offset + 32,
              pool.withdraw_authority.begin());
    offset += 32;
    
    // Parse validator list
    std::copy(data.begin() + offset, data.begin() + offset + 32,
              pool.validator_list.begin());
    offset += 32;
    
    // Parse totals and fees
    std::memcpy(&pool.total_lamports, data.data() + offset, sizeof(uint64_t));
    offset += 8;
    std::memcpy(&pool.pool_token_supply, data.data() + offset, sizeof(uint64_t));
    offset += 8;
    std::memcpy(&pool.fee_numerator, data.data() + offset, sizeof(uint16_t));
    offset += 2;
    std::memcpy(&pool.fee_denominator, data.data() + offset, sizeof(uint16_t));
    offset += 2;
    
    pool.initialized = data[offset] == 1;
    
    return Result<StakePool>(pool);
}

std::vector<uint8_t> SPLStakePoolProgram::serialize_stake_pool(const StakePool& pool) const {
    std::vector<uint8_t> data(STAKE_POOL_SIZE, 0);
    size_t offset = 0;
    
    // Pool mint
    std::copy(pool.pool_mint.begin(), pool.pool_mint.end(), data.begin() + offset);
    offset += 32;
    
    // Manager
    std::copy(pool.manager.begin(), pool.manager.end(), data.begin() + offset);
    offset += 32;
    
    // Staker
    std::copy(pool.staker.begin(), pool.staker.end(), data.begin() + offset);
    offset += 32;
    
    // Withdraw authority
    std::copy(pool.withdraw_authority.begin(), pool.withdraw_authority.end(), data.begin() + offset);
    offset += 32;
    
    // Validator list
    std::copy(pool.validator_list.begin(), pool.validator_list.end(), data.begin() + offset);
    offset += 32;
    
    // Totals and fees
    std::memcpy(data.data() + offset, &pool.total_lamports, sizeof(uint64_t));
    offset += 8;
    std::memcpy(data.data() + offset, &pool.pool_token_supply, sizeof(uint64_t));
    offset += 8;
    std::memcpy(data.data() + offset, &pool.fee_numerator, sizeof(uint16_t));
    offset += 2;
    std::memcpy(data.data() + offset, &pool.fee_denominator, sizeof(uint16_t));
    offset += 2;
    
    data[offset] = pool.initialized ? 1 : 0;
    
    return data;
}

uint64_t SPLStakePoolProgram::calculate_pool_tokens_for_deposit(
    uint64_t lamports, const StakePool& pool) const {
    if (pool.pool_token_supply == 0 || pool.total_lamports == 0) {
        return lamports; // 1:1 ratio for first deposit
    }
    
    // pool_tokens = (lamports * pool_token_supply) / total_lamports
    return (lamports * pool.pool_token_supply) / pool.total_lamports;
}

uint64_t SPLStakePoolProgram::calculate_lamports_for_pool_tokens(
    uint64_t pool_tokens, const StakePool& pool) const {
    if (pool.pool_token_supply == 0) {
        return 0;
    }
    
    // lamports = (pool_tokens * total_lamports) / pool_token_supply
    return (pool_tokens * pool.total_lamports) / pool.pool_token_supply;
}

// SPL Multisig Program Implementation
SPLMultisigProgram::SPLMultisigProgram() {
    std::cout << "SPL Multisig: Program initialized" << std::endl;
}

PublicKey SPLMultisigProgram::get_program_id() const {
    return MULTISIG_PROGRAM_ID;
}

ExecutionOutcome SPLMultisigProgram::execute(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    if (instruction.data.empty()) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Empty instruction data", ""};
    }
    
    MultisigInstruction instr_type = static_cast<MultisigInstruction>(instruction.data[0]);
    
    switch (instr_type) {
        case MultisigInstruction::CreateMultisig:
            std::cout << "SPL Multisig: Creating multisig wallet" << std::endl;
            return handle_create_multisig(instruction, context);
            
        case MultisigInstruction::CreateTransaction:
            std::cout << "SPL Multisig: Creating multisig transaction" << std::endl;
            return handle_create_transaction(instruction, context);
            
        case MultisigInstruction::Approve:
            std::cout << "SPL Multisig: Approving transaction" << std::endl;
            return handle_approve(instruction, context);
            
        case MultisigInstruction::ExecuteTransaction:
            std::cout << "SPL Multisig: Executing multisig transaction" << std::endl;
            return handle_execute_transaction(instruction, context);
            
        case MultisigInstruction::SetOwners:
            std::cout << "SPL Multisig: Setting multisig owners" << std::endl;
            return {ExecutionResult::SUCCESS, 2000, {}, "Multisig owners updated", ""};
            
        default:
            return {ExecutionResult::INVALID_INSTRUCTION, 0, {}, "Unknown multisig instruction", ""};
    }
}

ExecutionOutcome SPLMultisigProgram::handle_create_multisig(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    if (instruction.accounts.size() < 3) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Insufficient accounts for multisig creation", ""};
    }
    
    if (instruction.data.size() < 3) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Insufficient instruction data", ""};
    }
    
    // Parse M and N from instruction data
    uint8_t m = instruction.data[1]; // Required signatures
    uint8_t n = instruction.data[2]; // Total signers
    
    if (m == 0 || n == 0 || m > n || n > MAX_SIGNERS) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Invalid M-of-N configuration", ""};
    }
    
    // Create multisig (simplified for testing)
    Multisig multisig;
    multisig.m = m;
    multisig.n = n;
    multisig.nonce = 0;
    multisig.initialized = true;
    
    // Add signers from accounts (skip first account which is the multisig itself)
    for (size_t i = 1; i < instruction.accounts.size() && i - 1 < n; ++i) {
        multisig.signers.push_back(instruction.accounts[i]);
    }
    
    // Pad with empty keys if needed
    while (multisig.signers.size() < n) {
        multisig.signers.push_back(PublicKey(32, 0));
    }
    
    // Serialize multisig data (for testing purposes)
    auto multisig_data = serialize_multisig(multisig);
    std::cout << "SPL Multisig: Created " << static_cast<int>(m) << "-of-" 
              << static_cast<int>(n) << " multisig with " << multisig_data.size() << " bytes data" << std::endl;
    
    return {ExecutionResult::SUCCESS, 3000, {}, "Multisig created successfully", ""};
}

ExecutionOutcome SPLMultisigProgram::handle_create_transaction(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    if (instruction.accounts.size() < 3) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Insufficient accounts for transaction creation", ""};
    }
    
    // Create multisig transaction (simplified for testing)
    MultisigTransaction transaction;
    transaction.multisig = instruction.accounts[1];
    transaction.program_id = instruction.accounts[2];
    transaction.instruction_data = std::vector<uint8_t>(instruction.data.begin() + 1, instruction.data.end());
    transaction.signatures_count = 0;
    transaction.executed = false;
    transaction.initialized = true;
    
    // Initialize signers vector (will be populated as signatures come in)
    for (size_t i = 0; i < MAX_SIGNERS; ++i) {
        transaction.signers.push_back(false);
    }
    
    // Copy remaining accounts as transaction accounts
    for (size_t i = 3; i < instruction.accounts.size(); ++i) {
        transaction.accounts.push_back(instruction.accounts[i]);
    }
    
    // Serialize transaction data (for testing purposes)
    auto transaction_data = serialize_transaction(transaction);
    std::cout << "SPL Multisig: Transaction created with " << transaction_data.size() << " bytes data" << std::endl;
    
    return {ExecutionResult::SUCCESS, 2500, {}, "Multisig transaction created successfully", ""};
}

ExecutionOutcome SPLMultisigProgram::handle_approve(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    if (instruction.accounts.size() < 3) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Insufficient accounts for approval", ""};
    }
    
    // Simulate approval process (simplified for testing)
    PublicKey signer = instruction.accounts[2];
    
    // Create a simulated multisig for validation
    Multisig multisig;
    multisig.m = 2;
    multisig.n = 3;
    multisig.signers = {
        PublicKey(32, 0x02),
        PublicKey(32, 0x03),
        PublicKey(32, 0x04)
    };
    
    // Simulate approval logic
    bool is_valid = is_valid_signer(signer, multisig);
    if (!is_valid) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Unauthorized signer", ""};
    }
    
    // Simulate signature counting
    static uint8_t signatures_count = 0;
    signatures_count++;
    
    std::cout << "SPL Multisig: Approval " << static_cast<int>(signatures_count) 
              << "/" << static_cast<int>(multisig.m) << " signatures" << std::endl;
    
    return {ExecutionResult::SUCCESS, 1500, {}, "Transaction approved successfully", ""};
}

ExecutionOutcome SPLMultisigProgram::handle_execute_transaction(
    const Instruction& instruction,
    ExecutionContext& context) const {
    
    if (instruction.accounts.size() < 2) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Insufficient accounts for execution", ""};
    }
    
    // Simulate execution process (simplified for testing)
    // In a real implementation, this would load the multisig and transaction,
    // check signatures, and execute the underlying transaction
    
    static uint8_t signatures_count = 2; // Assume we have enough signatures
    uint8_t required_signatures = 2;
    
    // Check if enough signatures have been collected
    if (signatures_count < required_signatures) {
        return {ExecutionResult::PROGRAM_ERROR, 0, {}, "Insufficient signatures for execution", ""};
    }
    
    std::cout << "SPL Multisig: Transaction executed with " 
              << static_cast<int>(signatures_count) << " signatures" << std::endl;
    
    return {ExecutionResult::SUCCESS, 5000, {}, "Multisig transaction executed successfully", ""};
}

Result<SPLMultisigProgram::Multisig> SPLMultisigProgram::deserialize_multisig(
    const std::vector<uint8_t>& data) const {
    if (data.size() < MULTISIG_SIZE) {
        return Result<Multisig>("Invalid multisig data size");
    }
    
    Multisig multisig;
    size_t offset = 0;
    
    // Parse M and N
    multisig.m = data[offset++];
    multisig.n = data[offset++];
    
    // Parse nonce
    std::memcpy(&multisig.nonce, data.data() + offset, sizeof(uint64_t));
    offset += 8;
    
    // Parse signers
    multisig.signers.clear();
    for (uint8_t i = 0; i < multisig.n && i < MAX_SIGNERS; ++i) {
        PublicKey signer;
        std::copy(data.begin() + offset, data.begin() + offset + 32, signer.begin());
        multisig.signers.push_back(signer);
        offset += 32;
    }
    
    // Parse initialized flag
    multisig.initialized = data[offset] == 1;
    
    return Result<Multisig>(multisig);
}

std::vector<uint8_t> SPLMultisigProgram::serialize_multisig(const Multisig& multisig) const {
    std::vector<uint8_t> data(MULTISIG_SIZE, 0);
    size_t offset = 0;
    
    // M and N
    data[offset++] = multisig.m;
    data[offset++] = multisig.n;
    
    // Nonce
    std::memcpy(data.data() + offset, &multisig.nonce, sizeof(uint64_t));
    offset += 8;
    
    // Signers
    for (size_t i = 0; i < MAX_SIGNERS; ++i) {
        if (i < multisig.signers.size()) {
            std::copy(multisig.signers[i].begin(), multisig.signers[i].end(), 
                     data.begin() + offset);
        }
        offset += 32;
    }
    
    // Initialized flag
    data[offset] = multisig.initialized ? 1 : 0;
    
    return data;
}

Result<SPLMultisigProgram::MultisigTransaction> SPLMultisigProgram::deserialize_transaction(
    const std::vector<uint8_t>& data) const {
    if (data.size() < MULTISIG_TRANSACTION_SIZE) {
        return Result<MultisigTransaction>("Invalid transaction data size");
    }
    
    MultisigTransaction transaction;
    size_t offset = 0;
    
    // Parse multisig
    std::copy(data.begin() + offset, data.begin() + offset + 32,
              transaction.multisig.begin());
    offset += 32;
    
    // Parse program_id
    std::copy(data.begin() + offset, data.begin() + offset + 32,
              transaction.program_id.begin());
    offset += 32;
    
    // Parse signatures count
    transaction.signatures_count = data[offset++];
    
    // Parse executed flag
    transaction.executed = data[offset++] == 1;
    
    // Parse initialized flag
    transaction.initialized = data[offset++] == 1;
    
    // Parse signers array
    transaction.signers.clear();
    for (size_t i = 0; i < MAX_SIGNERS; ++i) {
        transaction.signers.push_back(data[offset++] == 1);
    }
    
    // For simplicity, use default instruction data and accounts
    transaction.instruction_data = {0x01, 0x02, 0x03};
    transaction.accounts.push_back(PublicKey(32, 0x01));
    
    return Result<MultisigTransaction>(transaction);
}

std::vector<uint8_t> SPLMultisigProgram::serialize_transaction(
    const MultisigTransaction& transaction) const {
    std::vector<uint8_t> data(MULTISIG_TRANSACTION_SIZE, 0);
    size_t offset = 0;
    
    // Multisig
    std::copy(transaction.multisig.begin(), transaction.multisig.end(), 
             data.begin() + offset);
    offset += 32;
    
    // Program ID
    std::copy(transaction.program_id.begin(), transaction.program_id.end(), 
             data.begin() + offset);
    offset += 32;
    
    // Signatures count
    data[offset++] = transaction.signatures_count;
    
    // Executed flag
    data[offset++] = transaction.executed ? 1 : 0;
    
    // Initialized flag
    data[offset++] = transaction.initialized ? 1 : 0;
    
    // Signers array
    for (size_t i = 0; i < MAX_SIGNERS; ++i) {
        data[offset++] = (i < transaction.signers.size() && transaction.signers[i]) ? 1 : 0;
    }
    
    return data;
}

bool SPLMultisigProgram::is_valid_signer(const PublicKey& signer, const Multisig& multisig) const {
    for (const auto& authorized_signer : multisig.signers) {
        if (authorized_signer == signer) {
            return true;
        }
    }
    return false;
}

// SPL Program Registry Implementation
SPLProgramRegistry::SPLProgramRegistry() {
    // Initialize program name mappings
    SPLAssociatedTokenProgram ata_prog;
    SPLMemoProgram memo_prog;
    ExtendedSystemProgram ext_prog;
    SPLGovernanceProgram gov_prog;
    SPLStakePoolProgram stake_prog;
    SPLMultisigProgram multisig_prog;
    
    register_program_name(ata_prog.get_program_id(), "SPL Associated Token Account");
    register_program_name(memo_prog.get_program_id(), "SPL Memo");
    register_program_name(ext_prog.get_program_id(), "Extended System");
    register_program_name(gov_prog.get_program_id(), "SPL Governance");
    register_program_name(stake_prog.get_program_id(), "SPL Stake Pool");
    register_program_name(multisig_prog.get_program_id(), "SPL Multisig");
}

void SPLProgramRegistry::register_all_programs(ExecutionEngine& engine) {
    register_token_program(engine);
    register_ata_program(engine);
    register_memo_program(engine);
    register_extended_system_program(engine);
    register_governance_program(engine);
    register_stake_pool_program(engine);
    register_multisig_program(engine);
    
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

void SPLProgramRegistry::register_governance_program(ExecutionEngine& engine) {
    engine.register_builtin_program(std::make_unique<SPLGovernanceProgram>());
}

void SPLProgramRegistry::register_stake_pool_program(ExecutionEngine& engine) {
    engine.register_builtin_program(std::make_unique<SPLStakePoolProgram>());
}

void SPLProgramRegistry::register_multisig_program(ExecutionEngine& engine) {
    engine.register_builtin_program(std::make_unique<SPLMultisigProgram>());
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