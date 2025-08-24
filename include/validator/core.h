#pragma once

#include "common/types.h"
#include "ledger/manager.h"
#include <memory>
#include <functional>

namespace slonana {
namespace validator {

using namespace slonana::common;

/**
 * Vote structure for consensus participation
 */
struct Vote {
    Slot slot;
    Hash block_hash;
    PublicKey validator_identity;
    Signature vote_signature;
    uint64_t timestamp;
    
    std::vector<uint8_t> serialize() const;
    bool verify() const;
};

/**
 * Fork choice algorithm for determining the canonical chain
 */
class ForkChoice {
public:
    ForkChoice();
    ~ForkChoice();
    
    // Add blocks to the fork choice
    void add_block(const ledger::Block& block);
    void add_vote(const Vote& vote);
    
    // Get the canonical chain head
    Hash get_head() const;
    Slot get_head_slot() const;
    
    // Fork management
    std::vector<Hash> get_forks() const;
    uint64_t get_fork_weight(const Hash& fork_head) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Block validation logic
 */
class BlockValidator {
public:
    explicit BlockValidator(std::shared_ptr<ledger::LedgerManager> ledger);
    ~BlockValidator();
    
    // Validate individual components
    bool validate_block_structure(const ledger::Block& block) const;
    bool validate_block_signature(const ledger::Block& block) const;
    bool validate_transactions(const ledger::Block& block) const;
    
    // Full block validation
    Result<bool> validate_block(const ledger::Block& block) const;
    
    // Chain validation
    bool validate_chain_continuity(const ledger::Block& block) const;

private:
    std::shared_ptr<ledger::LedgerManager> ledger_;
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Core validator logic orchestrating consensus participation
 */
class ValidatorCore {
public:
    using VoteCallback = std::function<void(const Vote&)>;
    using BlockCallback = std::function<void(const ledger::Block&)>;
    
    ValidatorCore(
        std::shared_ptr<ledger::LedgerManager> ledger,
        const PublicKey& validator_identity
    );
    ~ValidatorCore();
    
    // Start/stop validator operations
    Result<bool> start();
    void stop();
    
    // Process incoming blocks and votes
    void process_block(const ledger::Block& block);
    void process_vote(const Vote& vote);
    
    // Register callbacks
    void set_vote_callback(VoteCallback callback);
    void set_block_callback(BlockCallback callback);
    
    // Validator status
    bool is_running() const;
    Slot get_current_slot() const;
    Hash get_current_head() const;
    
    // Getter methods for RPC server
    PublicKey get_validator_identity() const { return validator_identity_; }
    Result<std::vector<uint8_t>> get_genesis_block() const;
    std::string get_slot_leader(Slot slot) const;

private:
    std::shared_ptr<ledger::LedgerManager> ledger_;
    std::unique_ptr<ForkChoice> fork_choice_;
    std::unique_ptr<BlockValidator> block_validator_;
    PublicKey validator_identity_;
    
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace validator
} // namespace slonana