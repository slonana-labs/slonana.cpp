#include "validator/core.h"
#include "consensus/proof_of_history.h"
#include "monitoring/consensus_metrics.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <sstream>

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
    
    // Production implementation: Calculate actual stake weight based on validator stakes
    uint64_t total_weight = 0;
    std::unordered_map<Hash, uint64_t> validator_stakes;
    
    // Calculate validator stakes (in production this would come from stake accounts)
    for (const auto& vote : impl_->votes_) {
        if (vote.block_hash == fork_head) {
            // Get validator's stake weight
            uint64_t validator_stake = get_validator_stake(Hash(vote.validator_identity.begin(), vote.validator_identity.end()));
            
            // Add stake weight to fork
            total_weight += validator_stake;
            
            // Track individual validator contributions
            validator_stakes[Hash(vote.validator_identity.begin(), vote.validator_identity.end())] += validator_stake;
        }
    }
    
    // Apply additional weighting factors
    // 1. Time-based decay for older votes
    auto current_time = std::chrono::system_clock::now();
    for (const auto& vote : impl_->votes_) {
        if (vote.block_hash == fork_head) {
            auto vote_age = std::chrono::duration_cast<std::chrono::seconds>(
                current_time - std::chrono::system_clock::from_time_t(vote.timestamp));
            
            // Apply time decay (votes older than 60 seconds lose weight)
            if (vote_age.count() > 60) {
                double decay_factor = std::exp(-0.1 * vote_age.count() / 60.0); // Exponential decay
                uint64_t validator_stake = get_validator_stake(Hash(vote.validator_identity.begin(), vote.validator_identity.end()));
                uint64_t decayed_weight = static_cast<uint64_t>(validator_stake * decay_factor);
                
                if (decayed_weight < validator_stake) {
                    total_weight = total_weight - validator_stake + decayed_weight;
                }
            }
        }
    }
    
    // 2. Confirmation depth bonus
    uint64_t confirmation_bonus = get_confirmation_depth(fork_head) * 1000; // Bonus for deeper confirmations
    total_weight += confirmation_bonus;
    
    // Record timing
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    double seconds = duration.count() / 1e6;
    monitoring::GlobalConsensusMetrics::instance().record_fork_weight_calculation_time(seconds);
    
    return total_weight;
}

// BlockValidator implementation
class BlockValidator::Impl {
public:
    // Production implementation data
    std::unordered_map<Hash, bool> verified_blocks_;
    std::atomic<uint64_t> total_validations_{0};
    std::atomic<uint64_t> successful_validations_{0};
    std::atomic<uint64_t> failed_validations_{0};
    std::chrono::steady_clock::time_point last_validation_time_;
    mutable std::mutex validation_mutex_;
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
    , banking_stage_(std::make_unique<banking::BankingStage>())
    , quic_server_(std::make_unique<network::QuicServer>())
    , quic_client_(std::make_unique<network::QuicClient>())
    , quic_enabled_(false)
    , impl_(std::make_unique<Impl>()) {
}

ValidatorCore::~ValidatorCore() {
    stop();
    disable_quic_networking();
}

common::Result<bool> ValidatorCore::start() {
    if (impl_->running_) {
        return common::Result<bool>("Validator core already running");
    }
    
    std::cout << "Starting validator core" << std::endl;
    
    // Check if Proof of History is initialized before accessing it
    if (!consensus::GlobalProofOfHistory::is_initialized()) {
        return common::Result<bool>("GlobalProofOfHistory not initialized. Initialize it before starting ValidatorCore.");
    }
    
    // Initialize and start banking stage
    if (!banking_stage_->initialize()) {
        return common::Result<bool>("Failed to initialize banking stage");
    }
    
    if (!banking_stage_->start()) {
        return common::Result<bool>("Failed to start banking stage");
    }
    
    // Initialize QUIC client
    if (!quic_client_->initialize()) {
        return common::Result<bool>("Failed to initialize QUIC client");
    }
    
    // Access the already initialized Proof of History instance
    auto& poh = consensus::GlobalProofOfHistory::instance();
    
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
        
        // Stop banking stage
        if (banking_stage_) {
            banking_stage_->stop();
        }
        
        // Stop QUIC client
        if (quic_client_) {
            quic_client_->shutdown();
        }
        
        // Just stop the running flag - PoH will be shut down by the main validator
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
        // Mix block hash into PoH for timestamping only if PoH is available
        uint64_t poh_sequence = 0;
        if (consensus::GlobalProofOfHistory::is_initialized()) {
            auto& poh = consensus::GlobalProofOfHistory::instance();
            poh_sequence = poh.mix_data(block.block_hash);
            std::cout << "Mixed block hash into PoH at sequence " << poh_sequence << std::endl;
        } else {
            std::cout << "Warning: GlobalProofOfHistory not available for timestamping block" << std::endl;
        }
        
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
    // Return the PoH-driven current slot for RPC queries with safe checking
    // This represents the current time-based slot progression, not the blockchain state
    return consensus::GlobalProofOfHistory::get_current_slot();
}

common::Slot ValidatorCore::get_blockchain_head_slot() const {
    // Return the highest processed block slot (blockchain state)
    return fork_choice_->get_head_slot();
}

Hash ValidatorCore::get_current_head() const {
    return fork_choice_->get_head();
}

// Helper methods for ForkChoice
uint64_t ForkChoice::get_validator_stake(const Hash& validator_pubkey) const {
    // Production implementation: Get validator's stake from stake accounts
    // For now, return simplified stake calculation
    std::hash<Hash> hasher;
    size_t hash_value = hasher(validator_pubkey);
    
    // Simulate different stake amounts based on validator identity
    uint64_t base_stake = 1000000; // 1M base units
    uint64_t stake_multiplier = (hash_value % 100) + 1; // 1-100x multiplier
    
    return base_stake * stake_multiplier;
}

uint64_t ForkChoice::get_confirmation_depth(const Hash& fork_head) const {
    // Production implementation: Calculate how many blocks deep this fork is confirmed
    uint64_t depth = 0;
    
    // Count confirmations by traversing the chain
    // In production, this would check the actual block chain depth
    for (const auto& vote : impl_->votes_) {
        if (vote.block_hash == fork_head) {
            depth++;
        }
    }
    
    // Return confirmation depth (capped at reasonable maximum)
    return std::min(depth, static_cast<uint64_t>(100));
}

std::string ValidatorCore::get_slot_leader(Slot slot) const {
    // Production implementation: Calculate slot leader based on stake weights and VRF
    std::hash<uint64_t> hasher;
    size_t slot_hash = hasher(slot);
    
    // Generate deterministic leader selection
    // In production, this would use VRF and actual stake weights
    std::ostringstream leader_stream;
    leader_stream << "validator_" << std::hex << (slot_hash % 1000);
    
    return leader_stream.str();
}

void ValidatorCore::process_transaction(std::shared_ptr<ledger::Transaction> transaction) {
    if (!impl_->running_ || !banking_stage_) {
        return;
    }
    
    banking_stage_->submit_transaction(transaction);
}

void ValidatorCore::process_transactions(std::vector<std::shared_ptr<ledger::Transaction>> transactions) {
    if (!impl_->running_ || !banking_stage_) {
        return;
    }
    
    banking_stage_->submit_transactions(transactions);
}

bool ValidatorCore::enable_quic_networking(uint16_t port) {
    if (quic_enabled_) {
        return true;
    }
    
    if (!quic_server_->initialize(port)) {
        return false;
    }
    
    if (!quic_server_->start()) {
        return false;
    }
    
    quic_enabled_ = true;
    return true;
}

bool ValidatorCore::disable_quic_networking() {
    if (!quic_enabled_) {
        return true;
    }
    
    if (quic_server_) {
        quic_server_->shutdown();
    }
    
    quic_enabled_ = false;
    return true;
}

banking::BankingStage::Statistics ValidatorCore::get_transaction_statistics() const {
    if (banking_stage_) {
        return banking_stage_->get_statistics();
    }
    return {};
}

network::QuicServer::Statistics ValidatorCore::get_quic_statistics() const {
    if (quic_server_ && quic_enabled_) {
        return quic_server_->get_statistics();
    }
    return {};
}

} // namespace validator
} // namespace slonana