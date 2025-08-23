#pragma once

#include "svm/engine.h"
#include <unordered_map>
#include <array>

namespace slonana {
namespace svm {

/**
 * SPL Associated Token Account (ATA) Program
 * 
 * This program manages the creation and management of associated token accounts.
 * Associated token accounts are deterministically derived token accounts that
 * allow each wallet to have exactly one token account per mint.
 */
class SPLAssociatedTokenProgram : public BuiltinProgram {
public:
    SPLAssociatedTokenProgram();
    ~SPLAssociatedTokenProgram() override = default;

    PublicKey get_program_id() const override;
    
    ExecutionOutcome execute(
        const Instruction& instruction,
        ExecutionContext& context
    ) const override;

private:
    enum class ATAInstruction : uint8_t {
        Create = 0,
        CreateIdempotent = 1,
        RecoverNested = 2
    };

    struct CreateATAParams {
        PublicKey funding_account;
        PublicKey wallet_address;
        PublicKey token_mint;
        PublicKey token_program;
    };

    // Instruction handlers
    ExecutionOutcome handle_create_ata(
        const Instruction& instruction,
        ExecutionContext& context
    ) const;

    ExecutionOutcome handle_create_ata_idempotent(
        const Instruction& instruction,
        ExecutionContext& context
    ) const;

    // Utility methods
    PublicKey derive_associated_token_address(
        const PublicKey& wallet_address,
        const PublicKey& token_mint
    ) const;

    Result<CreateATAParams> parse_create_instruction(
        const std::vector<uint8_t>& data
    ) const;

    static const PublicKey ATA_PROGRAM_ID;
    static const std::string ATA_SEED;
};

/**
 * SPL Memo Program
 * 
 * A simple program that validates and logs memo data.
 * Commonly used for adding human-readable information to transactions.
 */
class SPLMemoProgram : public BuiltinProgram {
public:
    SPLMemoProgram();
    ~SPLMemoProgram() override = default;

    PublicKey get_program_id() const override;
    
    ExecutionOutcome execute(
        const Instruction& instruction,
        ExecutionContext& context
    ) const override;

private:
    // Memo validation
    bool validate_memo_data(const std::vector<uint8_t>& data) const;
    std::string extract_memo_text(const std::vector<uint8_t>& data) const;

    static const PublicKey MEMO_PROGRAM_ID;
    static constexpr size_t MAX_MEMO_LENGTH = 566; // Solana transaction size limit
};

/**
 * System Program Extensions
 * 
 * Additional system-level operations beyond the core system program.
 */
class ExtendedSystemProgram : public BuiltinProgram {
public:
    ExtendedSystemProgram();
    ~ExtendedSystemProgram() override = default;

    PublicKey get_program_id() const override;
    
    ExecutionOutcome execute(
        const Instruction& instruction,
        ExecutionContext& context
    ) const override;

private:
    enum class ExtendedSystemInstruction : uint8_t {
        AdvanceNonceAccount = 4,
        WithdrawNonceAccount = 5,
        InitializeNonceAccount = 6,
        AuthorizeNonceAccount = 7,
        UpgradeNonceAccount = 8
    };

    struct NonceAccount {
        PublicKey authority;
        PublicKey nonce;
        uint64_t lamports;
        bool initialized;
    };

    // Nonce account management
    ExecutionOutcome handle_initialize_nonce(
        const Instruction& instruction,
        ExecutionContext& context
    ) const;

    ExecutionOutcome handle_advance_nonce(
        const Instruction& instruction,
        ExecutionContext& context
    ) const;

    ExecutionOutcome handle_withdraw_nonce(
        const Instruction& instruction,
        ExecutionContext& context
    ) const;

    // Utility methods
    Result<NonceAccount> deserialize_nonce_account(
        const std::vector<uint8_t>& data
    ) const;

    std::vector<uint8_t> serialize_nonce_account(
        const NonceAccount& account
    ) const;

    PublicKey generate_nonce_value() const;

    static const PublicKey EXTENDED_SYSTEM_PROGRAM_ID;
    static constexpr size_t NONCE_ACCOUNT_SIZE = 80;
};

/**
 * Program Registry for managing all SPL programs
 * 
 * Centralized registry that enables easy registration and discovery
 * of all available SPL programs in the enhanced SVM engine.
 */
class SPLProgramRegistry {
public:
    SPLProgramRegistry();
    ~SPLProgramRegistry() = default;

    // Register all standard SPL programs
    void register_all_programs(ExecutionEngine& engine);

    // Individual program registration
    void register_token_program(ExecutionEngine& engine);
    void register_ata_program(ExecutionEngine& engine);
    void register_memo_program(ExecutionEngine& engine);
    void register_extended_system_program(ExecutionEngine& engine);

    // Program discovery
    std::vector<PublicKey> get_all_program_ids() const;
    bool is_spl_program(const PublicKey& program_id) const;
    std::string get_program_name(const PublicKey& program_id) const;

private:
    std::unordered_map<PublicKey, std::string> program_names_;
    
    void register_program_name(const PublicKey& program_id, const std::string& name);
};

} // namespace svm
} // namespace slonana