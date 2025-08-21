#include "validator/core.h"
#include <iostream>
#include <algorithm>

namespace slonana {
namespace validator {

// Vote implementation
std::vector<uint8_t> Vote::serialize() const {
    std::vector<uint8_t> result;
    
    // Add slot as 8 bytes
    for (int i = 0; i < 8; ++i) {
        result.push_back((slot >> (i * 8)) & 0xFF);
    }
    
    // Add block hash
    result.insert(result.end(), block_hash.begin(), block_hash.end());
    
    return result;
}

bool Vote::verify() const {
    return slot > 0 && !block_hash.empty() && !validator_identity.empty();
}

// ForkChoice implementation
class ForkChoice::Impl {
public:
    std::vector<ledger::Block> blocks_;
    std::vector<Vote> votes_;
    Hash head_hash_;
    common::Slot head_slot_ = 0;
};

ForkChoice::ForkChoice() : impl_(std::make_unique<Impl>()) {}
ForkChoice::~ForkChoice() = default;

void ForkChoice::add_block(const ledger::Block& block) {
    impl_->blocks_.push_back(block);
    
    // Simple fork choice - always choose the highest slot
    if (block.slot > impl_->head_slot_) {
        impl_->head_hash_ = block.block_hash;
        impl_->head_slot_ = block.slot;
    }
    
    std::cout << "Added block to fork choice, slot: " << block.slot << std::endl;
}

void ForkChoice::add_vote(const Vote& vote) {
    impl_->votes_.push_back(vote);
    std::cout << "Added vote to fork choice, slot: " << vote.slot << std::endl;
}

Hash ForkChoice::get_head() const {
    return impl_->head_hash_;
}

common::Slot ForkChoice::get_head_slot() const {
    return impl_->head_slot_;
}

std::vector<Hash> ForkChoice::get_forks() const {
    std::vector<Hash> forks;
    for (const auto& block : impl_->blocks_) {
        forks.push_back(block.block_hash);
    }
    return forks;
}

uint64_t ForkChoice::get_fork_weight(const Hash& fork_head) const {
    // Stub implementation - would calculate actual stake weight
    uint64_t weight = 0;
    for (const auto& vote : impl_->votes_) {
        if (vote.block_hash == fork_head) {
            weight += 1; // Each vote counts as 1 unit for stub
        }
    }
    return weight;
}

// BlockValidator implementation
class BlockValidator::Impl {
public:
    // Stub implementation data
};

BlockValidator::BlockValidator(std::shared_ptr<ledger::LedgerManager> ledger)
    : ledger_(std::move(ledger)), impl_(std::make_unique<Impl>()) {
}

BlockValidator::~BlockValidator() = default;

bool BlockValidator::validate_block_structure(const ledger::Block& block) const {
    return block.verify();
}

bool BlockValidator::validate_block_signature(const ledger::Block& block) const {
    // Stub implementation - would verify cryptographic signature
    return !block.block_signature.empty() && !block.validator.empty();
}

bool BlockValidator::validate_transactions(const ledger::Block& block) const {
    for (const auto& tx : block.transactions) {
        if (!tx.verify()) {
            return false;
        }
    }
    return true;
}

common::Result<bool> BlockValidator::validate_block(const ledger::Block& block) const {
    if (!validate_block_structure(block)) {
        return common::Result<bool>("Invalid block structure");
    }
    
    if (!validate_block_signature(block)) {
        return common::Result<bool>("Invalid block signature");
    }
    
    if (!validate_transactions(block)) {
        return common::Result<bool>("Invalid transactions in block");
    }
    
    if (!validate_chain_continuity(block)) {
        return common::Result<bool>("Block breaks chain continuity");
    }
    
    return common::Result<bool>(true);
}

bool BlockValidator::validate_chain_continuity(const ledger::Block& block) const {
    if (block.slot == 0) {
        return true; // Genesis block
    }
    
    // Check if parent block exists and is valid
    auto parent = ledger_->get_block(block.parent_hash);
    return parent.has_value();
}

// ValidatorCore implementation
class ValidatorCore::Impl {
public:
    bool running_ = false;
    VoteCallback vote_callback_;
    BlockCallback block_callback_;
};

ValidatorCore::ValidatorCore(
    std::shared_ptr<ledger::LedgerManager> ledger,
    const PublicKey& validator_identity)
    : ledger_(std::move(ledger))
    , fork_choice_(std::make_unique<ForkChoice>())
    , block_validator_(std::make_unique<BlockValidator>(ledger_))
    , validator_identity_(validator_identity)
    , impl_(std::make_unique<Impl>()) {
}

ValidatorCore::~ValidatorCore() {
    stop();
}

common::Result<bool> ValidatorCore::start() {
    if (impl_->running_) {
        return common::Result<bool>("Validator core already running");
    }
    
    std::cout << "Starting validator core" << std::endl;
    impl_->running_ = true;
    return common::Result<bool>(true);
}

void ValidatorCore::stop() {
    if (impl_->running_) {
        std::cout << "Stopping validator core" << std::endl;
        impl_->running_ = false;
    }
}

void ValidatorCore::process_block(const ledger::Block& block) {
    if (!impl_->running_) {
        return;
    }
    
    auto validation_result = block_validator_->validate_block(block);
    if (validation_result.is_ok()) {
        fork_choice_->add_block(block);
        
        // Store valid block in ledger
        auto store_result = ledger_->store_block(block);
        if (store_result.is_ok()) {
            std::cout << "Processed and stored block at slot " << block.slot << std::endl;
            
            if (impl_->block_callback_) {
                impl_->block_callback_(block);
            }
        }
    } else {
        std::cout << "Rejected invalid block: " << validation_result.error() << std::endl;
    }
}

void ValidatorCore::process_vote(const Vote& vote) {
    if (!impl_->running_) {
        return;
    }
    
    if (vote.verify()) {
        fork_choice_->add_vote(vote);
        std::cout << "Processed vote for slot " << vote.slot << std::endl;
        
        if (impl_->vote_callback_) {
            impl_->vote_callback_(vote);
        }
    } else {
        std::cout << "Rejected invalid vote" << std::endl;
    }
}

void ValidatorCore::set_vote_callback(VoteCallback callback) {
    impl_->vote_callback_ = std::move(callback);
}

void ValidatorCore::set_block_callback(BlockCallback callback) {
    impl_->block_callback_ = std::move(callback);
}

bool ValidatorCore::is_running() const {
    return impl_->running_;
}

common::Slot ValidatorCore::get_current_slot() const {
    return fork_choice_->get_head_slot();
}

Hash ValidatorCore::get_current_head() const {
    return fork_choice_->get_head();
}

} // namespace validator
} // namespace slonana