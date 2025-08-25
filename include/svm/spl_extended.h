#pragma once

#include "svm/engine.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace slonana {
namespace svm {

// Token Program Implementation (SPL Token)
class TokenProgram {
public:
    static const std::string PROGRAM_ID;
    
    enum class Instruction {
        INITIALIZE_MINT,
        INITIALIZE_ACCOUNT,
        INITIALIZE_MULTISIG,
        TRANSFER,
        APPROVE,
        REVOKE,
        SET_AUTHORITY,
        MINT_TO,
        BURN,
        CLOSE_ACCOUNT,
        FREEZE_ACCOUNT,
        THAW_ACCOUNT,
        TRANSFER_CHECKED,
        APPROVE_CHECKED,
        MINT_TO_CHECKED,
        BURN_CHECKED
    };
    
    struct MintAccount {
        bool is_initialized;
        uint8_t decimals;
        std::string mint_authority;
        uint64_t supply;
        std::string freeze_authority;
    };
    
    struct TokenAccount {
        std::string mint;
        std::string owner;
        uint64_t amount;
        std::string delegate;
        uint8_t state; // 0=uninitialized, 1=initialized, 2=frozen
        bool is_native;
        uint64_t delegated_amount;
        std::string close_authority;
    };
    
    static ExecutionResult execute_instruction(
        const std::vector<uint8_t>& instruction_data,
        const std::vector<AccountInfo>& accounts
    );
    
private:
    static ExecutionResult initialize_mint(const std::vector<uint8_t>& data, const std::vector<AccountInfo>& accounts);
    static ExecutionResult initialize_account(const std::vector<uint8_t>& data, const std::vector<AccountInfo>& accounts);
    static ExecutionResult transfer(const std::vector<uint8_t>& data, const std::vector<AccountInfo>& accounts);
    static ExecutionResult approve(const std::vector<uint8_t>& data, const std::vector<AccountInfo>& accounts);
    static ExecutionResult mint_to(const std::vector<uint8_t>& data, const std::vector<AccountInfo>& accounts);
    static ExecutionResult burn(const std::vector<uint8_t>& data, const std::vector<AccountInfo>& accounts);
    static ExecutionResult transfer_checked(const std::vector<uint8_t>& data, const std::vector<AccountInfo>& accounts);
    
    static bool verify_mint_authority(const std::string& mint_account, const std::string& authority);
    static bool verify_token_owner(const std::string& token_account, const std::string& owner);
    static uint64_t get_token_balance(const std::string& token_account);
    static void update_token_balance(const std::string& token_account, uint64_t new_balance);
};

// Associated Token Account Program
class AssociatedTokenProgram {
public:
    static const std::string PROGRAM_ID;
    
    enum class Instruction {
        CREATE,
        CREATE_IDEMPOTENT,
        RECOVER_NESTED
    };
    
    static ExecutionResult execute_instruction(
        const std::vector<uint8_t>& instruction_data,
        const std::vector<AccountInfo>& accounts
    );
    
    static std::string derive_associated_token_address(
        const std::string& wallet_address,
        const std::string& token_mint_address
    );
    
private:
    static ExecutionResult create_associated_token_account(
        const std::vector<AccountInfo>& accounts,
        bool idempotent = false
    );
    
    static ExecutionResult recover_nested_account(
        const std::vector<AccountInfo>& accounts
    );
};

// Memo Program Implementation
class MemoProgram {
public:
    static const std::string PROGRAM_ID;
    
    struct MemoData {
        std::string memo_text;
        std::vector<std::string> required_signers;
        uint64_t timestamp;
    };
    
    static ExecutionResult execute_instruction(
        const std::vector<uint8_t>& instruction_data,
        const std::vector<AccountInfo>& accounts
    );
    
    static bool validate_memo_length(const std::string& memo);
    static bool validate_signers(const std::vector<std::string>& signers, const std::vector<AccountInfo>& accounts);
    
private:
    static const size_t MAX_MEMO_LENGTH = 566; // Maximum memo length in bytes
};

// Name Service Program
class NameServiceProgram {
public:
    static const std::string PROGRAM_ID;
    
    enum class Instruction {
        CREATE,
        UPDATE,
        TRANSFER,
        DELETE,
        REALLOC,
        CREATE_REVERSE,
        UPDATE_REVERSE
    };
    
    struct NameRegistryState {
        std::string parent_name;
        std::string owner;
        std::string class_hash;
        std::vector<uint8_t> data;
    };
    
    // Alias for easier use
    using NameRecord = NameRegistryState;
    
    static ExecutionResult execute_instruction(
        const std::vector<uint8_t>& instruction_data,
        const std::vector<AccountInfo>& accounts
    );
    
    static std::string derive_name_account_key(
        const std::string& name,
        const std::string& name_class,
        const std::string& parent_name
    );
    
private:
    static ExecutionResult create_name_registry(const std::vector<uint8_t>& data, const std::vector<AccountInfo>& accounts);
    static ExecutionResult update_name_registry(const std::vector<uint8_t>& data, const std::vector<AccountInfo>& accounts);
    static ExecutionResult transfer_ownership(const std::vector<uint8_t>& data, const std::vector<AccountInfo>& accounts);
    static ExecutionResult delete_name_registry(const std::vector<uint8_t>& data, const std::vector<AccountInfo>& accounts);
    
    static bool verify_name_ownership(const std::string& name_account, const std::string& owner);
};

// Metadata Program (for NFTs)
class MetadataProgram {
public:
    static const std::string PROGRAM_ID;
    
    enum class Instruction {
        CREATE_METADATA_ACCOUNT,
        UPDATE_METADATA_ACCOUNT,
        CREATE_MASTER_EDITION,
        MINT_NEW_EDITION_FROM_MASTER_EDITION_VIA_TOKEN,
        UPDATE_PRIMARY_SALE_HAPPENED_VIA_TOKEN,
        SIGN_METADATA,
        REMOVE_CREATOR_VERIFICATION,
        CREATE_METADATA_ACCOUNT_V2,
        CREATE_MASTER_EDITION_V3,
        VERIFY_COLLECTION,
        UTILIZE,
        APPROVE_USE_AUTHORITY,
        REVOKE_USE_AUTHORITY,
        UNVERIFY_COLLECTION,
        APPROVE_COLLECTION_AUTHORITY,
        REVOKE_COLLECTION_AUTHORITY,
        SET_AND_VERIFY_COLLECTION,
        FREEZE_DELEGATED_ACCOUNT,
        THAW_DELEGATED_ACCOUNT,
        UPDATE_METADATA_ACCOUNT_V2,
        CREATE_METADATA_ACCOUNT_V3,
        SET_AND_VERIFY_SIZED_COLLECTION_ITEM,
        CREATE_MASTER_EDITION_V3_DEPRECATED,
        SET_COLLECTION_SIZE,
        SET_TOKEN_STANDARD,
        BUBBLEGUM_SET_COLLECTION_SIZE,
        BURN_NFT,
        CREATE_ESCROW_ACCOUNT,
        CLOSE_ESCROW_ACCOUNT,
        TRANSFER_OUT_OF_ESCROW
    };
    
    struct Creator {
        std::string address;
        bool verified;
        uint8_t share;
    };
    
    struct Collection {
        bool verified;
        std::string key;
    };
    
    struct Uses {
        std::string use_method;
        uint64_t remaining;
        uint64_t total;
    };
    
    struct Metadata {
        std::string update_authority;
        std::string mint;
        std::string name;
        std::string symbol;
        std::string uri;
        uint16_t seller_fee_basis_points;
        std::vector<Creator> creators;
        Collection collection;
        Uses uses;
        bool primary_sale_happened;
        bool is_mutable;
        std::string edition_nonce;
        std::string token_standard;
    };
    
    static ExecutionResult execute_instruction(
        const std::vector<uint8_t>& instruction_data,
        const std::vector<AccountInfo>& accounts
    );
    
private:
    static ExecutionResult create_metadata_account(const std::vector<uint8_t>& data, const std::vector<AccountInfo>& accounts);
    static ExecutionResult update_metadata_account(const std::vector<uint8_t>& data, const std::vector<AccountInfo>& accounts);
    static ExecutionResult create_master_edition(const std::vector<uint8_t>& data, const std::vector<AccountInfo>& accounts);
    static ExecutionResult verify_collection(const std::vector<uint8_t>& data, const std::vector<AccountInfo>& accounts);
    
    static bool validate_creator_shares(const std::vector<Creator>& creators);
    static bool validate_metadata_uri(const std::string& uri);
};

// Enhanced SPL Program Registry
class ExtendedSPLProgramRegistry {
private:
    std::unordered_map<std::string, std::function<ExecutionResult(const std::vector<uint8_t>&, const std::vector<AccountInfo>&)>> programs_;

public:
    ExtendedSPLProgramRegistry();
    
    bool is_spl_program(const std::string& program_id) const;
    ExecutionResult execute_spl_program(
        const std::string& program_id,
        const std::vector<uint8_t>& instruction_data,
        const std::vector<AccountInfo>& accounts
    );
    
    std::vector<std::string> get_supported_programs() const;
    void register_custom_program(
        const std::string& program_id,
        std::function<ExecutionResult(const std::vector<uint8_t>&, const std::vector<AccountInfo>&)> executor
    );
};

// Utility functions for SPL program operations
namespace spl_utils {
    std::vector<uint8_t> pack_u64(uint64_t value);
    uint64_t unpack_u64(const std::vector<uint8_t>& data, size_t offset);
    std::vector<uint8_t> pack_string(const std::string& str);
    std::string unpack_string(const std::vector<uint8_t>& data, size_t offset, size_t length);
    
    bool verify_program_derived_address(
        const std::string& address,
        const std::vector<std::vector<uint8_t>>& seeds,
        const std::string& program_id
    );
    
    std::string derive_program_address(
        const std::vector<std::vector<uint8_t>>& seeds,
        const std::string& program_id
    );
    
    bool validate_account_size(const AccountInfo& account, size_t expected_size);
    bool validate_account_owner(const AccountInfo& account, const std::string& expected_owner);
    bool validate_signer(const AccountInfo& account);
    bool validate_writable(const AccountInfo& account);
    
    // Token-specific utilities
    bool is_mint_account(const AccountInfo& account);
    bool is_token_account(const AccountInfo& account);
    bool is_associated_token_account(const std::string& account_address, const std::string& wallet, const std::string& mint);
    
    // Metadata utilities
    std::string derive_metadata_account(const std::string& mint_address);
    std::string derive_master_edition_account(const std::string& mint_address);
    std::string derive_edition_marker_account(const std::string& mint_address, uint64_t edition);
    
    // Error handling
    ExecutionResult create_program_error(const std::string& message, uint32_t error_code = 1);
    ExecutionResult create_success_result();
    ExecutionResult create_success_result(const std::vector<uint8_t>& return_data);
}

}} // namespace slonana::svm