#include "validator/core.h"
#include "consensus/proof_of_history.h"
#include "monitoring/consensus_metrics.h"
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
    // Time the fork choice operation
    auto timer = monitoring::GlobalConsensusMetrics::instance().create_fork_choice_timer();
    
    impl_->blocks_.push_back(block);
    
    // Simple fork choice - always choose the highest slot
    if (block.slot > impl_->head_slot_) {
        impl_->head_hash_ = block.block_hash;
        impl_->head_slot_ = block.slot;
    }
    
    // Update metrics
    monitoring::GlobalConsensusMetrics::instance().set_current_slot(block.slot);
    monitoring::GlobalConsensusMetrics::instance().set_active_forks_count(impl_->blocks_.size());
    
    std::cout << "Added block to fork choice, slot: " << block.slot 
              << " (fork choice time: " << timer.stop() * 1000 << "ms)" << std::endl;
}

void ForkChoice::add_vote(const Vote& vote) {
    // Time the vote processing
    auto timer = monitoring::GlobalConsensusMetrics::instance().create_vote_processing_timer();
    
    impl_->votes_.push_back(vote);
    
    // Update metrics
    monitoring::GlobalConsensusMetrics::instance().increment_votes_processed();
    
    std::cout << "Added vote to fork choice, slot: " << vote.slot 
              << " (processing time: " << timer.stop() * 1000 << "ms)" << std::endl;
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
    // Time the fork weight calculation
    auto start_time = std::chrono::steady_clock::now();
    
    // Stub implementation - would calculate actual stake weight
    uint64_t weight = 0;
    for (const auto& vote : impl_->votes_) {
        if (vote.block_hash == fork_head) {
            weight += 1; // Each vote counts as 1 unit for stub
        }
    }
    
    // Record timing
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    double seconds = duration.count() / 1e6;
    monitoring::GlobalConsensusMetrics::instance().record_fork_weight_calculation_time(seconds);
    
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
    // Time signature verification
    auto start_time = std::chrono::steady_clock::now();
    
    // Validate signature and validator identity are present
    if (block.block_signature.empty() || block.validator.empty()) {
        return false;
    }
    
    // For production validator, verify Ed25519 signature
    // Create the message to verify (block hash)
    auto block_hash = block.compute_hash();
    
    // Note: In a full implementation, this would:
    // 1. Parse the Ed25519 public key from block.validator
    // 2. Parse the signature from block.block_signature 
    // 3. Verify the signature against the block hash using Ed25519
    // For now, we do basic validation that signature and validator are valid format
    
    bool valid = true;
    
    // Validate signature format (should be 64 bytes for Ed25519)
    if (block.block_signature.size() != 64) {
        valid = false;
    }
    
    // Validate public key format (should be 32 bytes for Ed25519)
    if (block.validator.size() != 32) {
        valid = false;
    }
    
    // Basic sanity check - signature shouldn't be all zeros
    bool all_zeros = std::all_of(block.block_signature.begin(), block.block_signature.end(), 
                                [](uint8_t b) { return b == 0; });
    if (all_zeros) {
        valid = false;
    }
    
    // Record timing
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    double seconds = duration.count() / 1e6;
    monitoring::GlobalConsensusMetrics::instance().record_signature_verification_time(seconds);
    
    return valid;
}

bool BlockValidator::validate_transactions(const ledger::Block& block) const {
    // Time transaction verification
    auto start_time = std::chrono::steady_clock::now();
    
    for (const auto& tx : block.transactions) {
        if (!tx.verify()) {
            return false;
        }
    }
    
    // Record timing
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    double seconds = duration.count() / 1e6;
    monitoring::GlobalConsensusMetrics::instance().record_transaction_verification_time(seconds);
    
    return true;
}

common::Result<bool> BlockValidator::validate_block(const ledger::Block& block) const {
    // Time the entire block validation process
    auto timer = monitoring::GlobalConsensusMetrics::instance().create_block_validation_timer();
    
    if (!validate_block_structure(block)) {
        monitoring::GlobalConsensusMetrics::instance().increment_blocks_rejected();
        return common::Result<bool>("Invalid block structure");
    }
    
    if (!validate_block_signature(block)) {
        monitoring::GlobalConsensusMetrics::instance().increment_blocks_rejected();
        return common::Result<bool>("Invalid block signature");
    }
    
    if (!validate_transactions(block)) {
        monitoring::GlobalConsensusMetrics::instance().increment_blocks_rejected();
        return common::Result<bool>("Invalid transactions in block");
    }
    
    if (!validate_chain_continuity(block)) {
        monitoring::GlobalConsensusMetrics::instance().increment_blocks_rejected();
        return common::Result<bool>("Block breaks chain continuity");
    }
    
    double validation_time = timer.stop();
    std::cout << "Block validation completed in " << validation_time * 1000 << "ms" << std::endl;
    
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
    
    // Initialize and start Proof of History
    if (!consensus::GlobalProofOfHistory::initialize()) {
        return common::Result<bool>("Failed to initialize Proof of History");
    }
    
    // Start PoH with a genesis hash
    Hash genesis_hash(32, 0x42); // Simple genesis hash
    auto& poh = consensus::GlobalProofOfHistory::instance();
    auto start_result = poh.start(genesis_hash);
    if (!start_result.is_ok()) {
        return common::Result<bool>("Failed to start Proof of History: " + start_result.error());
    }
    
    // Set up PoH callbacks for metrics
    poh.set_tick_callback([this](const consensus::PohEntry& entry) {
        monitoring::GlobalConsensusMetrics::instance().increment_poh_ticks_generated();
        monitoring::GlobalConsensusMetrics::instance().set_poh_sequence_number(static_cast<int64_t>(entry.sequence_number));
    });
    
    poh.set_slot_callback([this](Slot slot, const std::vector<consensus::PohEntry>& entries) {
        monitoring::GlobalConsensusMetrics::instance().set_poh_current_slot(static_cast<int64_t>(slot));
        std::cout << "PoH completed slot " << slot << " with " << entries.size() << " entries" << std::endl;
    });
    
    impl_->running_ = true;
    return common::Result<bool>(true);
}

void ValidatorCore::stop() {
    if (impl_->running_) {
        std::cout << "Stopping validator core" << std::endl;
        
        // Stop Proof of History
        consensus::GlobalProofOfHistory::shutdown();
        
        impl_->running_ = false;
    }
}

void ValidatorCore::process_block(const ledger::Block& block) {
    if (!impl_->running_) {
        return;
    }
    
    // Time the entire block processing operation
    auto processing_timer = monitoring::GlobalConsensusMetrics::instance().create_block_validation_timer();
    
    auto validation_result = block_validator_->validate_block(block);
    if (validation_result.is_ok()) {
        // Mix block hash into PoH for timestamping
        auto& poh = consensus::GlobalProofOfHistory::instance();
        uint64_t poh_sequence = poh.mix_data(block.block_hash);
        
        std::cout << "Mixed block hash into PoH at sequence " << poh_sequence << std::endl;
        
        fork_choice_->add_block(block);
        
        // Time block storage
        auto storage_start = std::chrono::steady_clock::now();
        auto store_result = ledger_->store_block(block);
        auto storage_end = std::chrono::steady_clock::now();
        
        auto storage_duration = std::chrono::duration_cast<std::chrono::microseconds>(storage_end - storage_start);
        double storage_seconds = storage_duration.count() / 1e6;
        monitoring::GlobalConsensusMetrics::instance().record_block_storage_time(storage_seconds);
        
        if (store_result.is_ok()) {
            double processing_time = processing_timer.stop();
            monitoring::GlobalConsensusMetrics::instance().increment_blocks_processed();
            
            std::cout << "Processed and stored block at slot " << block.slot 
                      << " (total processing time: " << processing_time * 1000 << "ms, PoH sequence: " << poh_sequence << ")" << std::endl;
            
            if (impl_->block_callback_) {
                impl_->block_callback_(block);
            }
        }
    } else {
        monitoring::GlobalConsensusMetrics::instance().increment_blocks_rejected();
        std::cout << "Rejected invalid block: " << validation_result.error() << std::endl;
    }
}

void ValidatorCore::process_vote(const Vote& vote) {
    if (!impl_->running_) {
        return;
    }
    
    // Time vote verification
    auto verification_start = std::chrono::steady_clock::now();
    bool vote_valid = vote.verify();
    auto verification_end = std::chrono::steady_clock::now();
    
    auto verification_duration = std::chrono::duration_cast<std::chrono::microseconds>(verification_end - verification_start);
    double verification_seconds = verification_duration.count() / 1e6;
    monitoring::GlobalConsensusMetrics::instance().record_vote_verification_time(verification_seconds);
    
    if (vote_valid) {
        fork_choice_->add_vote(vote);
        std::cout << "Processed vote for slot " << vote.slot 
                  << " (verification time: " << verification_seconds * 1000 << "ms)" << std::endl;
        
        if (impl_->vote_callback_) {
            impl_->vote_callback_(vote);
        }
    } else {
        monitoring::GlobalConsensusMetrics::instance().increment_votes_rejected();
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
    // For validator state, prioritize the blockchain state (fork choice) over time-based PoH
    // This ensures get_current_slot() reflects the actual blockchain progression
    common::Slot fork_choice_slot = fork_choice_->get_head_slot();
    
    // If we have processed blocks, return the highest processed block slot
    if (fork_choice_slot > 0) {
        return fork_choice_slot;
    }
    
    // If no blocks processed yet, return 0 to indicate initial state
    // (PoH slot advancement without blocks doesn't change validator's blockchain state)
    return 0;
}

Hash ValidatorCore::get_current_head() const {
    return fork_choice_->get_head();
}

} // namespace validator
} // namespace slonana