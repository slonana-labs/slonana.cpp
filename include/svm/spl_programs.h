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
 * SPL Governance Program
 * 
 * Implements decentralized governance mechanisms for DAOs.
 * Enables proposal creation, voting, and execution of governance decisions.
 */
class SPLGovernanceProgram : public BuiltinProgram {
public:
    SPLGovernanceProgram();
    ~SPLGovernanceProgram() override = default;

    PublicKey get_program_id() const override;
    
    ExecutionOutcome execute(
        const Instruction& instruction,
        ExecutionContext& context
    ) const override;

private:
    enum class GovernanceInstruction : uint8_t {
        CreateRealm = 0,
        CreateProposal = 1,
        CastVote = 2,
        ExecuteProposal = 3,
        CreateTokenOwnerRecord = 4
    };

    struct Realm {
        PublicKey governance_token_mint;
        PublicKey council_token_mint;
        std::string name;
        uint64_t min_community_weight_to_create_proposal;
        uint64_t min_council_weight_to_create_proposal;
        bool initialized;
    };

    struct Proposal {
        PublicKey realm;
        PublicKey governance;
        PublicKey proposer;
        std::string description;
        uint64_t yes_votes;
        uint64_t no_votes;
        uint64_t voting_at;
        uint64_t voting_ends_at;
        uint8_t state; // 0=Draft, 1=Voting, 2=Succeeded, 3=Defeated, 4=Executed
        bool initialized;
    };

    struct Vote {
        PublicKey proposal;
        PublicKey voter;
        uint8_t vote_type; // 0=Yes, 1=No
        uint64_t weight;
        bool initialized;
    };

    // Instruction handlers
    ExecutionOutcome handle_create_realm(
        const Instruction& instruction,
        ExecutionContext& context
    ) const;

    ExecutionOutcome handle_create_proposal(
        const Instruction& instruction,
        ExecutionContext& context
    ) const;

    ExecutionOutcome handle_cast_vote(
        const Instruction& instruction,
        ExecutionContext& context
    ) const;

    // Utility methods
    Result<Realm> deserialize_realm(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> serialize_realm(const Realm& realm) const;
    Result<Proposal> deserialize_proposal(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> serialize_proposal(const Proposal& proposal) const;

    static const PublicKey GOVERNANCE_PROGRAM_ID;
    static constexpr size_t REALM_SIZE = 256;
    static constexpr size_t PROPOSAL_SIZE = 512;
    static constexpr size_t VOTE_SIZE = 96;
};

/**
 * SPL Stake Pool Program
 * 
 * Manages validator stake pools for delegating stake across multiple validators.
 * Enables liquid staking with stake pool tokens.
 */
class SPLStakePoolProgram : public BuiltinProgram {
public:
    SPLStakePoolProgram();
    ~SPLStakePoolProgram() override = default;

    PublicKey get_program_id() const override;
    
    ExecutionOutcome execute(
        const Instruction& instruction,
        ExecutionContext& context
    ) const override;

private:
    enum class StakePoolInstruction : uint8_t {
        Initialize = 0,
        DepositStake = 1,
        WithdrawStake = 2,
        UpdateStakePool = 3,
        AddValidatorToPool = 4,
        RemoveValidatorFromPool = 5
    };

    struct StakePool {
        PublicKey pool_mint;
        PublicKey manager;
        PublicKey staker;
        PublicKey withdraw_authority;
        PublicKey validator_list;
        uint64_t total_lamports;
        uint64_t pool_token_supply;
        uint16_t fee_numerator;
        uint16_t fee_denominator;
        bool initialized;
    };

    struct ValidatorStakeInfo {
        PublicKey vote_account;
        PublicKey stake_account;
        uint64_t lamports;
        uint64_t transient_stake_lamports;
        bool active;
    };

    struct ValidatorList {
        uint32_t max_validators;
        uint32_t validator_count;
        std::vector<ValidatorStakeInfo> validators;
        bool initialized;
    };

    // Instruction handlers
    ExecutionOutcome handle_initialize_pool(
        const Instruction& instruction,
        ExecutionContext& context
    ) const;

    ExecutionOutcome handle_deposit_stake(
        const Instruction& instruction,
        ExecutionContext& context
    ) const;

    ExecutionOutcome handle_withdraw_stake(
        const Instruction& instruction,
        ExecutionContext& context
    ) const;

    // Utility methods
    Result<StakePool> deserialize_stake_pool(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> serialize_stake_pool(const StakePool& pool) const;
    uint64_t calculate_pool_tokens_for_deposit(uint64_t lamports, const StakePool& pool) const;
    uint64_t calculate_lamports_for_pool_tokens(uint64_t pool_tokens, const StakePool& pool) const;

    static const PublicKey STAKE_POOL_PROGRAM_ID;
    static constexpr size_t STAKE_POOL_SIZE = 256;
    static constexpr size_t VALIDATOR_LIST_SIZE = 8192; // Support up to 200 validators
};

/**
 * SPL Multisig Program
 * 
 * Implements multi-signature wallets requiring multiple signatures for transactions.
 * Supports M-of-N signature schemes for enhanced security.
 */
class SPLMultisigProgram : public BuiltinProgram {
public:
    SPLMultisigProgram();
    ~SPLMultisigProgram() override = default;

    PublicKey get_program_id() const override;
    
    ExecutionOutcome execute(
        const Instruction& instruction,
        ExecutionContext& context
    ) const override;

private:
    enum class MultisigInstruction : uint8_t {
        CreateMultisig = 0,
        CreateTransaction = 1,
        Approve = 2,
        ExecuteTransaction = 3,
        SetOwners = 4
    };

    struct Multisig {
        uint8_t m; // Required signatures
        uint8_t n; // Total signers
        std::vector<PublicKey> signers;
        uint64_t nonce;
        bool initialized;
    };

    struct MultisigTransaction {
        PublicKey multisig;
        std::vector<uint8_t> instruction_data;
        std::vector<PublicKey> accounts;
        PublicKey program_id;
        std::vector<bool> signers; // Tracks who has signed
        uint8_t signatures_count;
        bool executed;
        bool initialized;
    };

    // Instruction handlers
    ExecutionOutcome handle_create_multisig(
        const Instruction& instruction,
        ExecutionContext& context
    ) const;

    ExecutionOutcome handle_create_transaction(
        const Instruction& instruction,
        ExecutionContext& context
    ) const;

    ExecutionOutcome handle_approve(
        const Instruction& instruction,
        ExecutionContext& context
    ) const;

    ExecutionOutcome handle_execute_transaction(
        const Instruction& instruction,
        ExecutionContext& context
    ) const;

    // Utility methods
    Result<Multisig> deserialize_multisig(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> serialize_multisig(const Multisig& multisig) const;
    Result<MultisigTransaction> deserialize_transaction(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> serialize_transaction(const MultisigTransaction& transaction) const;
    bool is_valid_signer(const PublicKey& signer, const Multisig& multisig) const;

    static const PublicKey MULTISIG_PROGRAM_ID;
    static constexpr size_t MULTISIG_SIZE = 512;
    static constexpr size_t MULTISIG_TRANSACTION_SIZE = 2048;
    static constexpr uint8_t MAX_SIGNERS = 11;
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
    void register_governance_program(ExecutionEngine& engine);
    void register_stake_pool_program(ExecutionEngine& engine);
    void register_multisig_program(ExecutionEngine& engine);

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