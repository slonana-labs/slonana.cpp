#include "svm/economic_opcodes.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace slonana {
namespace svm {

// ============================================================================
// AuctionManager Implementation
// ============================================================================

AuctionManager::AuctionManager() = default;
AuctionManager::~AuctionManager() = default;

uint64_t AuctionManager::create_auction(
    const std::string& creator_pubkey,
    AuctionType type,
    const std::vector<std::string>& items,
    const std::vector<uint64_t>& reserve_prices,
    uint64_t deadline_slot,
    const std::string& settlement_account,
    uint64_t min_bid_increment,
    bool allow_sealed
) {
    if (items.empty() || items.size() > MAX_AUCTION_ITEMS) {
        return 0;
    }
    if (items.size() != reserve_prices.size()) {
        return 0;
    }
    if (creator_pubkey.empty() || settlement_account.empty()) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    EconAuction auction;
    auction.auction_id = next_auction_id_++;
    auction.creator_pubkey = creator_pubkey;
    auction.auction_type = type;
    auction.state = AuctionState::CREATED;
    auction.items = items;
    auction.reserve_prices = reserve_prices;
    auction.deadline_slot = deadline_slot;
    auction.settlement_account = settlement_account;
    auction.min_bid_increment = min_bid_increment;
    auction.allow_sealed_bids = allow_sealed;
    
    auctions_[auction.auction_id] = std::move(auction);
    return auction.auction_id;
}

uint64_t AuctionManager::submit_bid(
    uint64_t auction_id,
    const std::string& bidder_pubkey,
    const std::vector<uint64_t>& item_values,
    uint64_t total_amount,
    uint64_t current_slot,
    const std::vector<uint8_t>& sealed_hash
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = auctions_.find(auction_id);
    if (it == auctions_.end()) {
        return 0;
    }
    
    EconAuction& auction = it->second;
    
    // Check state
    if (auction.state != AuctionState::CREATED && 
        auction.state != AuctionState::BIDDING) {
        return 0;
    }
    
    // Check deadline
    if (current_slot >= auction.deadline_slot) {
        return 0;
    }
    
    // Check bid count
    if (auction.bids.size() >= MAX_BIDS_PER_AUCTION) {
        return 0;
    }
    
    // Check total amount meets reserve for at least one item
    bool meets_reserve = false;
    for (size_t i = 0; i < item_values.size() && i < auction.reserve_prices.size(); ++i) {
        if (item_values[i] >= auction.reserve_prices[i]) {
            meets_reserve = true;
            break;
        }
    }
    
    if (!meets_reserve && total_amount == 0) {
        return 0;
    }
    
    // Create bid
    AuctionBid bid;
    bid.bid_id = next_bid_id_++;
    bid.bidder_pubkey = bidder_pubkey;
    bid.item_values = item_values;
    bid.total_bid_amount = total_amount;
    bid.timestamp_slot = current_slot;
    bid.is_sealed = !sealed_hash.empty();
    bid.sealed_hash = sealed_hash;
    bid.is_revealed = sealed_hash.empty();
    
    auction.bids.push_back(std::move(bid));
    auction.state = AuctionState::BIDDING;
    
    return bid.bid_id;
}

bool AuctionManager::reveal_bid(
    uint64_t auction_id,
    uint64_t bid_id,
    const std::vector<uint8_t>& revealed_data
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = auctions_.find(auction_id);
    if (it == auctions_.end()) {
        return false;
    }
    
    for (auto& bid : it->second.bids) {
        if (bid.bid_id == bid_id) {
            if (!bid.is_sealed || bid.is_revealed) {
                return false;
            }
            // In a real implementation, verify the hash matches
            bid.revealed_data = revealed_data;
            bid.is_revealed = true;
            return true;
        }
    }
    
    return false;
}

bool AuctionManager::settle_auction(uint64_t auction_id, uint64_t current_slot) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = auctions_.find(auction_id);
    if (it == auctions_.end()) {
        return false;
    }
    
    EconAuction& auction = it->second;
    
    // Check state
    if (auction.state != AuctionState::BIDDING) {
        return false;
    }
    
    // Check deadline passed
    if (current_slot < auction.deadline_slot) {
        return false;
    }
    
    // Check all sealed bids revealed
    for (const auto& bid : auction.bids) {
        if (bid.is_sealed && !bid.is_revealed) {
            return false;  // Wait for reveals
        }
    }
    
    auction.state = AuctionState::SETTLING;
    auction.settlement_slot = current_slot;
    
    // Compute allocation based on auction type
    switch (auction.auction_type) {
        case AuctionType::VCG:
            compute_vcg_allocation(auction);
            break;
        case AuctionType::GSP:
            compute_gsp_allocation(auction);
            break;
        case AuctionType::SEALED_SECOND:
            compute_second_price_allocation(auction);
            break;
        default:
            // For other types, use simple highest bid wins
            compute_second_price_allocation(auction);
            break;
    }
    
    auction.state = AuctionState::SETTLED;
    return true;
}

bool AuctionManager::cancel_auction(uint64_t auction_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = auctions_.find(auction_id);
    if (it == auctions_.end()) {
        return false;
    }
    
    if (it->second.state == AuctionState::SETTLED) {
        return false;  // Can't cancel settled auction
    }
    
    it->second.state = AuctionState::CANCELLED;
    return true;
}

const EconAuction* AuctionManager::get_auction(uint64_t auction_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = auctions_.find(auction_id);
    if (it == auctions_.end()) {
        return nullptr;
    }
    return &it->second;
}

const AuctionBid* AuctionManager::get_bid(uint64_t auction_id, uint64_t bid_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = auctions_.find(auction_id);
    if (it == auctions_.end()) {
        return nullptr;
    }
    
    for (const auto& bid : it->second.bids) {
        if (bid.bid_id == bid_id) {
            return &bid;
        }
    }
    return nullptr;
}

uint64_t AuctionManager::get_compute_cost(const std::string& operation) const {
    if (operation == "create") return econ_compute_costs::AUCTION_CREATE;
    if (operation == "bid") return econ_compute_costs::BID_SUBMIT;
    if (operation == "settle") return econ_compute_costs::AUCTION_SETTLE;
    return 0;
}

void AuctionManager::compute_vcg_allocation(EconAuction& auction) {
    // VCG (Vickrey-Clarke-Groves) auction for multiple items
    // Each bidder pays the externality they impose on others
    
    if (auction.bids.empty()) {
        return;
    }
    
    size_t num_items = auction.items.size();
    
    // For simplicity, implement single-item VCG (second-price) for each item
    // A full VCG implementation would solve a combinatorial optimization problem
    
    for (size_t item_idx = 0; item_idx < num_items; ++item_idx) {
        // Find top two bidders for this item
        uint64_t highest_value = 0;
        uint64_t second_highest = 0;
        size_t winner_idx = SIZE_MAX;
        
        for (size_t bid_idx = 0; bid_idx < auction.bids.size(); ++bid_idx) {
            const auto& bid = auction.bids[bid_idx];
            if (bid.item_values.size() <= item_idx) continue;
            
            uint64_t value = bid.item_values[item_idx];
            if (value >= auction.reserve_prices[item_idx]) {
                if (value > highest_value) {
                    second_highest = highest_value;
                    highest_value = value;
                    winner_idx = bid_idx;
                } else if (value > second_highest) {
                    second_highest = value;
                }
            }
        }
        
        // Assign item to winner
        if (winner_idx != SIZE_MAX) {
            auto& winner = auction.bids[winner_idx];
            winner.is_winner = true;
            winner.allocation_mask |= (1ULL << item_idx);
            // VCG payment = second highest bid (or reserve if no second bid)
            uint64_t payment = std::max(second_highest, auction.reserve_prices[item_idx]);
            winner.payment_amount += payment;
            auction.total_revenue += payment;
            
            if (std::find(auction.winner_pubkeys.begin(), auction.winner_pubkeys.end(),
                         winner.bidder_pubkey) == auction.winner_pubkeys.end()) {
                auction.winner_pubkeys.push_back(winner.bidder_pubkey);
            }
            
            if (auction.final_prices.size() <= item_idx) {
                auction.final_prices.resize(item_idx + 1);
            }
            auction.final_prices[item_idx] = payment;
        }
    }
}

void AuctionManager::compute_gsp_allocation(EconAuction& auction) {
    // GSP (Generalized Second Price) - similar to Google AdWords
    // Bidders pay the next highest bid + minimum increment
    
    if (auction.bids.empty()) {
        return;
    }
    
    // Sort bids by total amount descending
    std::vector<size_t> bid_order(auction.bids.size());
    std::iota(bid_order.begin(), bid_order.end(), 0);
    std::sort(bid_order.begin(), bid_order.end(), [&](size_t a, size_t b) {
        return auction.bids[a].total_bid_amount > auction.bids[b].total_bid_amount;
    });
    
    size_t num_items = auction.items.size();
    
    for (size_t rank = 0; rank < std::min(bid_order.size(), num_items); ++rank) {
        size_t bid_idx = bid_order[rank];
        auto& bid = auction.bids[bid_idx];
        
        // Check reserve
        if (bid.total_bid_amount < auction.reserve_prices[rank]) {
            continue;
        }
        
        bid.is_winner = true;
        bid.allocation_mask |= (1ULL << rank);
        
        // GSP payment = next bidder's bid + increment (or reserve if last)
        uint64_t payment;
        if (rank + 1 < bid_order.size()) {
            payment = std::max(
                auction.bids[bid_order[rank + 1]].total_bid_amount + auction.min_bid_increment,
                auction.reserve_prices[rank]
            );
        } else {
            payment = auction.reserve_prices[rank];
        }
        
        bid.payment_amount = payment;
        auction.total_revenue += payment;
        auction.winner_pubkeys.push_back(bid.bidder_pubkey);
        auction.final_prices.push_back(payment);
    }
}

void AuctionManager::compute_second_price_allocation(EconAuction& auction) {
    // Simple second-price (Vickrey) auction - winner pays second highest bid
    
    if (auction.bids.empty()) {
        return;
    }
    
    // Find top two bids
    uint64_t highest = 0;
    uint64_t second_highest = 0;
    size_t winner_idx = SIZE_MAX;
    
    for (size_t i = 0; i < auction.bids.size(); ++i) {
        uint64_t amount = auction.bids[i].total_bid_amount;
        if (amount > highest) {
            second_highest = highest;
            highest = amount;
            winner_idx = i;
        } else if (amount > second_highest) {
            second_highest = amount;
        }
    }
    
    // Check reserve
    if (winner_idx == SIZE_MAX || highest < auction.reserve_prices[0]) {
        return;
    }
    
    // Set winner
    auto& winner = auction.bids[winner_idx];
    winner.is_winner = true;
    winner.allocation_mask = (1ULL << auction.items.size()) - 1;  // All items
    winner.payment_amount = std::max(second_highest, auction.reserve_prices[0]);
    
    auction.winner_pubkeys.push_back(winner.bidder_pubkey);
    auction.final_prices = {winner.payment_amount};
    auction.total_revenue = winner.payment_amount;
}

// ============================================================================
// EscrowManager Implementation
// ============================================================================

EscrowManager::EscrowManager() = default;
EscrowManager::~EscrowManager() = default;

uint64_t EscrowManager::create_escrow(
    const std::vector<EscrowParty>& parties,
    uint64_t amount,
    EscrowCondition condition,
    uint64_t release_slot,
    uint64_t expiry_slot,
    uint8_t required_approvals,
    const std::string& token_mint,
    const std::string& description
) {
    if (parties.empty() || parties.size() > MAX_ESCROW_PARTIES) {
        return 0;
    }
    if (amount == 0) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    EconEscrow escrow;
    escrow.escrow_id = next_escrow_id_++;
    escrow.state = EscrowState::ACTIVE;
    escrow.release_condition = condition;
    escrow.parties = parties;
    escrow.required_approvals = required_approvals;
    escrow.current_approvals = 0;
    escrow.total_amount = amount;
    escrow.token_mint = token_mint;
    escrow.release_slot = release_slot;
    escrow.expiry_slot = expiry_slot;
    escrow.description = description;
    
    escrows_[escrow.escrow_id] = std::move(escrow);
    return escrow.escrow_id;
}

bool EscrowManager::approve_release(
    uint64_t escrow_id,
    const std::string& approver_pubkey
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = escrows_.find(escrow_id);
    if (it == escrows_.end()) {
        return false;
    }
    
    EconEscrow& escrow = it->second;
    
    if (escrow.state != EscrowState::ACTIVE) {
        return false;
    }
    
    // Find party and approve
    for (auto& party : escrow.parties) {
        if (party.pubkey == approver_pubkey && !party.has_approved) {
            party.has_approved = true;
            escrow.current_approvals++;
            return true;
        }
    }
    
    return false;
}

bool EscrowManager::release_escrow(uint64_t escrow_id, uint64_t current_slot) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = escrows_.find(escrow_id);
    if (it == escrows_.end()) {
        return false;
    }
    
    EconEscrow& escrow = it->second;
    
    if (escrow.state != EscrowState::ACTIVE) {
        return false;
    }
    
    if (!can_release(escrow, current_slot)) {
        return false;
    }
    
    escrow.state = EscrowState::RELEASED;
    return true;
}

bool EscrowManager::refund_escrow(uint64_t escrow_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = escrows_.find(escrow_id);
    if (it == escrows_.end()) {
        return false;
    }
    
    EconEscrow& escrow = it->second;
    
    if (escrow.state != EscrowState::ACTIVE && 
        escrow.state != EscrowState::DISPUTED) {
        return false;
    }
    
    escrow.state = EscrowState::REFUNDED;
    return true;
}

bool EscrowManager::start_dispute(uint64_t escrow_id, const std::string& disputer_pubkey) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = escrows_.find(escrow_id);
    if (it == escrows_.end()) {
        return false;
    }
    
    EconEscrow& escrow = it->second;
    
    if (escrow.state != EscrowState::ACTIVE) {
        return false;
    }
    
    // Verify disputer is a party
    bool is_party = false;
    for (const auto& party : escrow.parties) {
        if (party.pubkey == disputer_pubkey) {
            is_party = true;
            break;
        }
    }
    
    if (!is_party) {
        return false;
    }
    
    escrow.state = EscrowState::DISPUTED;
    return true;
}

bool EscrowManager::resolve_dispute(
    uint64_t escrow_id,
    const std::string& arbiter_pubkey,
    bool release_to_receiver
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = escrows_.find(escrow_id);
    if (it == escrows_.end()) {
        return false;
    }
    
    EconEscrow& escrow = it->second;
    
    if (escrow.state != EscrowState::DISPUTED) {
        return false;
    }
    
    // Verify arbiter
    bool is_arbiter = false;
    for (const auto& party : escrow.parties) {
        if (party.pubkey == arbiter_pubkey && party.role == EscrowRole::ARBITER) {
            is_arbiter = true;
            break;
        }
    }
    
    if (!is_arbiter) {
        return false;
    }
    
    escrow.state = release_to_receiver ? EscrowState::RELEASED : EscrowState::REFUNDED;
    return true;
}

const EconEscrow* EscrowManager::get_escrow(uint64_t escrow_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = escrows_.find(escrow_id);
    if (it == escrows_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<uint64_t> EscrowManager::check_expired(uint64_t current_slot) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<uint64_t> expired;
    for (auto& [id, escrow] : escrows_) {
        if (escrow.state == EscrowState::ACTIVE && 
            escrow.expiry_slot > 0 && 
            current_slot >= escrow.expiry_slot) {
            escrow.state = EscrowState::EXPIRED;
            expired.push_back(id);
        }
    }
    return expired;
}

bool EscrowManager::can_release(const EconEscrow& escrow, uint64_t current_slot) const {
    switch (escrow.release_condition) {
        case EscrowCondition::TIME_LOCK:
            return current_slot >= escrow.release_slot;
            
        case EscrowCondition::MULTI_SIG:
            return escrow.current_approvals >= escrow.required_approvals;
            
        case EscrowCondition::UNCONDITIONAL:
            return true;
            
        case EscrowCondition::ORACLE_CONDITION:
            // Would need oracle integration
            return false;
            
        case EscrowCondition::SMART_CONTRACT:
            // Would need contract execution
            return false;
            
        default:
            return false;
    }
}

// ============================================================================
// StakingManager Implementation
// ============================================================================

StakingManager::StakingManager() = default;
StakingManager::~StakingManager() = default;

uint64_t StakingManager::lock_stake(
    const std::string& staker_pubkey,
    const std::string& validator_pubkey,
    uint64_t amount,
    uint64_t duration_slots,
    uint64_t current_slot,
    bool is_delegated,
    uint64_t delegation_fee_bps
) {
    if (amount < MIN_STAKE_AMOUNT) {
        return 0;
    }
    if (duration_slots > MAX_STAKE_DURATION_SLOTS) {
        return 0;
    }
    if (staker_pubkey.empty() || validator_pubkey.empty()) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    EconStake stake;
    stake.stake_id = next_stake_id_++;
    stake.staker_pubkey = staker_pubkey;
    stake.validator_pubkey = validator_pubkey;
    stake.state = StakeState::LOCKED;
    stake.staked_amount = amount;
    stake.locked_at_slot = current_slot;
    stake.unlock_slot = current_slot + duration_slots;
    stake.lock_duration_slots = duration_slots;
    stake.is_delegated = is_delegated;
    stake.delegation_fee_bps = delegation_fee_bps;
    
    uint64_t stake_id = stake.stake_id;
    
    staker_stakes_[staker_pubkey].push_back(stake_id);
    validator_stakes_[validator_pubkey].push_back(stake_id);
    stakes_[stake_id] = std::move(stake);
    
    total_staked_ += amount;
    
    return stake_id;
}

bool StakingManager::begin_unstake(uint64_t stake_id, uint64_t current_slot) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = stakes_.find(stake_id);
    if (it == stakes_.end()) {
        return false;
    }
    
    EconStake& stake = it->second;
    
    if (stake.state != StakeState::LOCKED) {
        return false;
    }
    
    // Check lock period has passed
    if (current_slot < stake.unlock_slot) {
        return false;
    }
    
    // Begin unbonding period (default 7 days worth of slots)
    constexpr uint64_t UNBONDING_PERIOD = 7 * 24 * 60 * 60 / 0.4;  // ~7 days at 400ms slots
    stake.state = StakeState::UNLOCKING;
    stake.unlock_slot = current_slot + UNBONDING_PERIOD;
    
    return true;
}

bool StakingManager::complete_unstake(uint64_t stake_id, uint64_t current_slot) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = stakes_.find(stake_id);
    if (it == stakes_.end()) {
        return false;
    }
    
    EconStake& stake = it->second;
    
    if (stake.state != StakeState::UNLOCKING) {
        return false;
    }
    
    if (current_slot < stake.unlock_slot) {
        return false;
    }
    
    stake.state = StakeState::UNLOCKED;
    total_staked_ -= stake.staked_amount;
    
    return true;
}

bool StakingManager::slash_stake(
    uint64_t stake_id,
    uint64_t percentage,
    SlashReason reason,
    uint64_t current_slot
) {
    if (percentage > MAX_SLASH_PERCENTAGE) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = stakes_.find(stake_id);
    if (it == stakes_.end()) {
        return false;
    }
    
    EconStake& stake = it->second;
    
    if (stake.state == StakeState::UNLOCKED) {
        return false;  // Can't slash unlocked stake
    }
    
    // Calculate slash amount
    uint64_t slash_amount = (stake.staked_amount * percentage) / 100;
    stake.slashed_amount += slash_amount;
    stake.staked_amount -= slash_amount;
    total_staked_ -= slash_amount;
    
    // Record event
    EconStake::SlashEvent event;
    event.reason = reason;
    event.amount = slash_amount;
    event.slot = current_slot;
    stake.slash_history.push_back(event);
    
    if (stake.staked_amount == 0) {
        stake.state = StakeState::SLASHED;
    }
    
    return true;
}

bool StakingManager::distribute_rewards(uint64_t stake_id, uint64_t reward_amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = stakes_.find(stake_id);
    if (it == stakes_.end()) {
        return false;
    }
    
    EconStake& stake = it->second;
    
    if (stake.state != StakeState::LOCKED) {
        return false;
    }
    
    stake.rewards_earned += reward_amount;
    return true;
}

const EconStake* StakingManager::get_stake(uint64_t stake_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = stakes_.find(stake_id);
    if (it == stakes_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<const EconStake*> StakingManager::get_staker_stakes(
    const std::string& staker_pubkey) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<const EconStake*> result;
    auto it = staker_stakes_.find(staker_pubkey);
    if (it != staker_stakes_.end()) {
        for (uint64_t id : it->second) {
            auto stake_it = stakes_.find(id);
            if (stake_it != stakes_.end()) {
                result.push_back(&stake_it->second);
            }
        }
    }
    return result;
}

std::vector<const EconStake*> StakingManager::get_validator_stakes(
    const std::string& validator_pubkey) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<const EconStake*> result;
    auto it = validator_stakes_.find(validator_pubkey);
    if (it != validator_stakes_.end()) {
        for (uint64_t id : it->second) {
            auto stake_it = stakes_.find(id);
            if (stake_it != stakes_.end()) {
                result.push_back(&stake_it->second);
            }
        }
    }
    return result;
}

uint64_t StakingManager::get_total_staked() const {
    return total_staked_.load();
}

// ============================================================================
// ReputationManager Implementation
// ============================================================================

ReputationManager::ReputationManager() = default;
ReputationManager::~ReputationManager() = default;

uint64_t ReputationManager::get_or_create_reputation(
    const std::string& entity_pubkey,
    uint64_t current_slot
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = reputations_.find(entity_pubkey);
    if (it != reputations_.end()) {
        return it->second.reputation_id;
    }
    
    // Create new reputation
    ReputationScore score;
    score.reputation_id = next_reputation_id_++;
    score.entity_pubkey = entity_pubkey;
    score.created_at_slot = current_slot;
    score.last_update_slot = current_slot;
    
    uint64_t id = score.reputation_id;
    reputation_ids_[id] = entity_pubkey;
    reputations_[entity_pubkey] = std::move(score);
    
    return id;
}

bool ReputationManager::update_reputation(
    const std::string& entity_pubkey,
    ReputationAction action,
    int32_t value_change,
    uint64_t transaction_volume,
    uint64_t current_slot
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = reputations_.find(entity_pubkey);
    if (it == reputations_.end()) {
        // Auto-create if doesn't exist
        ReputationScore score;
        score.reputation_id = next_reputation_id_++;
        score.entity_pubkey = entity_pubkey;
        score.created_at_slot = current_slot;
        reputation_ids_[score.reputation_id] = entity_pubkey;
        reputations_[entity_pubkey] = score;
        it = reputations_.find(entity_pubkey);
    }
    
    ReputationScore& score = it->second;
    score.last_update_slot = current_slot;
    score.total_volume += transaction_volume;
    
    // Update appropriate score based on action
    switch (action) {
        case ReputationAction::TRADE_COMPLETED:
            score.trade_score = std::min(10000, score.trade_score + value_change);
            score.total_transactions++;
            score.successful_transactions++;
            break;
            
        case ReputationAction::TRADE_FAILED:
            score.trade_score = std::max(0, score.trade_score + value_change);
            score.total_transactions++;
            score.failed_transactions++;
            break;
            
        case ReputationAction::PAYMENT_ON_TIME:
            score.payment_score = std::min(10000, score.payment_score + value_change);
            break;
            
        case ReputationAction::PAYMENT_LATE:
            score.payment_score = std::max(0, score.payment_score + value_change);
            break;
            
        case ReputationAction::DISPUTE_WON:
            score.dispute_score = std::min(10000, score.dispute_score + value_change);
            score.total_disputes++;
            score.disputes_won++;
            break;
            
        case ReputationAction::DISPUTE_LOST:
            score.dispute_score = std::max(0, score.dispute_score + value_change);
            score.total_disputes++;
            break;
            
        case ReputationAction::REFERRAL:
        case ReputationAction::VERIFICATION:
        case ReputationAction::BONUS:
            score.activity_score = std::min(10000, score.activity_score + value_change);
            break;
            
        case ReputationAction::PENALTY:
            score.activity_score = std::max(0, score.activity_score + value_change);
            break;
    }
    
    recalculate_overall_score(score);
    return true;
}

const ReputationScore* ReputationManager::get_reputation(
    const std::string& entity_pubkey) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = reputations_.find(entity_pubkey);
    if (it == reputations_.end()) {
        return nullptr;
    }
    return &it->second;
}

const ReputationScore* ReputationManager::get_reputation_by_id(uint64_t reputation_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto id_it = reputation_ids_.find(reputation_id);
    if (id_it == reputation_ids_.end()) {
        return nullptr;
    }
    
    auto it = reputations_.find(id_it->second);
    if (it == reputations_.end()) {
        return nullptr;
    }
    return &it->second;
}

bool ReputationManager::meets_threshold(
    const std::string& entity_pubkey,
    int32_t min_score) const {
    const ReputationScore* score = get_reputation(entity_pubkey);
    if (!score) {
        return false;
    }
    return score->overall_score >= min_score;
}

std::vector<std::pair<std::string, int32_t>> ReputationManager::get_top_entities(
    size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::pair<std::string, int32_t>> all_scores;
    for (const auto& [pubkey, score] : reputations_) {
        all_scores.emplace_back(pubkey, score.overall_score);
    }
    
    std::sort(all_scores.begin(), all_scores.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    
    if (all_scores.size() > count) {
        all_scores.resize(count);
    }
    
    return all_scores;
}

void ReputationManager::recalculate_overall_score(ReputationScore& score) {
    // Weighted average of component scores
    // Trade: 35%, Payment: 30%, Dispute: 20%, Activity: 15%
    score.overall_score = (score.trade_score * 35 + 
                          score.payment_score * 30 +
                          score.dispute_score * 20 +
                          score.activity_score * 15) / 100;
}

// ============================================================================
// ExecutionLane Implementation
// ============================================================================

ExecutionLane::ExecutionLane(uint8_t lane_id) : lane_id_(lane_id) {}

ExecutionLane::~ExecutionLane() {
    stop();
}

bool ExecutionLane::start() {
    if (running_.load()) {
        return false;
    }
    
    running_ = true;
    worker_thread_ = std::thread(&ExecutionLane::worker_loop, this);
    return true;
}

void ExecutionLane::stop() {
    running_ = false;
    cv_.notify_all();
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

bool ExecutionLane::submit_agent(std::unique_ptr<AgentExecutionContext> ctx) {
    if (!running_.load()) {
        return false;
    }
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ctx->lane_id = lane_id_;
        ctx->state = LaneState::IDLE;
        pending_.push(std::move(ctx));
    }
    
    cv_.notify_one();
    return true;
}

bool ExecutionLane::is_available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !current_ && pending_.empty();
}

uint64_t ExecutionLane::get_current_agent_id() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_ ? current_->agent_id : 0;
}

void ExecutionLane::worker_loop() {
    while (running_.load()) {
        std::unique_ptr<AgentExecutionContext> ctx;
        
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return !running_.load() || !pending_.empty();
            });
            
            if (!running_.load()) {
                break;
            }
            
            if (!pending_.empty()) {
                current_ = std::move(pending_.front());
                pending_.pop();
                ctx = std::move(current_);
            }
        }
        
        if (ctx) {
            execute_agent(*ctx);
            completed_count_++;
            
            std::lock_guard<std::mutex> lock(mutex_);
            current_.reset();
        }
    }
}

void ExecutionLane::execute_agent(AgentExecutionContext& ctx) {
    ctx.state = LaneState::RUNNING;
    
    // Simulate execution
    // In a real implementation, this would execute the BPF bytecode
    ctx.compute_used = ctx.compute_budget / 2;  // Simulated usage
    ctx.success = true;
    
    ctx.state = LaneState::COMPLETED;
}

// ============================================================================
// ExecutionLaneManager Implementation
// ============================================================================

ExecutionLaneManager::ExecutionLaneManager(uint8_t num_lanes) 
    : num_lanes_(num_lanes) {
    lanes_.reserve(num_lanes);
    for (uint8_t i = 0; i < num_lanes; ++i) {
        lanes_.push_back(std::make_unique<ExecutionLane>(i));
    }
}

ExecutionLaneManager::~ExecutionLaneManager() {
    shutdown();
}

bool ExecutionLaneManager::initialize() {
    if (running_.load()) {
        return false;
    }
    
    for (auto& lane : lanes_) {
        if (!lane->start()) {
            shutdown();
            return false;
        }
    }
    
    running_ = true;
    return true;
}

void ExecutionLaneManager::shutdown() {
    running_ = false;
    for (auto& lane : lanes_) {
        lane->stop();
    }
}

uint8_t ExecutionLaneManager::submit_agent(std::unique_ptr<AgentExecutionContext> ctx) {
    uint8_t lane_id = find_available_lane();
    if (lane_id == 255) {
        return 255;
    }
    
    if (lanes_[lane_id]->submit_agent(std::move(ctx))) {
        return lane_id;
    }
    
    return 255;
}

std::vector<uint8_t> ExecutionLaneManager::submit_agents(
    std::vector<std::unique_ptr<AgentExecutionContext>> contexts
) {
    std::vector<uint8_t> assignments;
    std::vector<uint8_t> assigned_lanes;
    
    for (auto& ctx : contexts) {
        uint8_t lane_id = find_lane_for_agent(*ctx, assigned_lanes);
        if (lane_id != 255 && lanes_[lane_id]->submit_agent(std::move(ctx))) {
            assignments.push_back(lane_id);
            assigned_lanes.push_back(lane_id);
        } else {
            assignments.push_back(255);
        }
    }
    
    return assignments;
}

ExecutionLane* ExecutionLaneManager::get_lane(uint8_t lane_id) {
    if (lane_id >= lanes_.size()) {
        return nullptr;
    }
    return lanes_[lane_id].get();
}

uint8_t ExecutionLaneManager::get_available_lane_count() const {
    uint8_t count = 0;
    for (const auto& lane : lanes_) {
        if (lane->is_available()) {
            count++;
        }
    }
    return count;
}

uint64_t ExecutionLaneManager::get_total_completed() const {
    uint64_t total = 0;
    for (const auto& lane : lanes_) {
        total += lane->get_completed_count();
    }
    return total;
}

bool ExecutionLaneManager::has_conflict(
    const AgentExecutionContext& a,
    const AgentExecutionContext& b
) {
    // Check if write sets overlap
    for (const auto& write_a : a.write_set) {
        for (const auto& write_b : b.write_set) {
            if (write_a == write_b) {
                return true;
            }
        }
        // Also check if A writes what B reads
        for (const auto& read_b : b.read_set) {
            if (write_a == read_b) {
                return true;
            }
        }
    }
    
    // Check if B writes what A reads
    for (const auto& write_b : b.write_set) {
        for (const auto& read_a : a.read_set) {
            if (write_b == read_a) {
                return true;
            }
        }
    }
    
    return false;
}

uint8_t ExecutionLaneManager::find_available_lane() const {
    for (uint8_t i = 0; i < lanes_.size(); ++i) {
        if (lanes_[i]->is_available()) {
            return i;
        }
    }
    return 255;
}

uint8_t ExecutionLaneManager::find_lane_for_agent(
    const AgentExecutionContext& ctx,
    const std::vector<uint8_t>& assigned_lanes
) const {
    // First, try to find an empty lane
    for (uint8_t i = 0; i < lanes_.size(); ++i) {
        if (lanes_[i]->is_available()) {
            bool used = std::find(assigned_lanes.begin(), assigned_lanes.end(), i) 
                       != assigned_lanes.end();
            if (!used) {
                return i;
            }
        }
    }
    
    // If no empty lanes, return any available
    return find_available_lane();
}

// ============================================================================
// EconomicOpcodesEngine Implementation
// ============================================================================

EconomicOpcodesEngine::EconomicOpcodesEngine(uint8_t num_lanes)
    : auction_manager_(std::make_unique<AuctionManager>()),
      escrow_manager_(std::make_unique<EscrowManager>()),
      staking_manager_(std::make_unique<StakingManager>()),
      reputation_manager_(std::make_unique<ReputationManager>()),
      lane_manager_(std::make_unique<ExecutionLaneManager>(num_lanes)) {
}

EconomicOpcodesEngine::~EconomicOpcodesEngine() {
    shutdown();
}

bool EconomicOpcodesEngine::initialize() {
    if (running_.load()) {
        return false;
    }
    
    if (!lane_manager_->initialize()) {
        return false;
    }
    
    running_ = true;
    return true;
}

void EconomicOpcodesEngine::shutdown() {
    running_ = false;
    lane_manager_->shutdown();
}

uint64_t EconomicOpcodesEngine::sol_econ_auction_create(
    const std::string& creator_pubkey,
    AuctionType type,
    const std::vector<std::string>& items,
    const std::vector<uint64_t>& reserve_prices,
    uint64_t deadline_slot,
    const std::string& settlement_account
) {
    return auction_manager_->create_auction(
        creator_pubkey, type, items, reserve_prices,
        deadline_slot, settlement_account
    );
}

uint64_t EconomicOpcodesEngine::sol_econ_bid(
    uint64_t auction_id,
    const std::string& bidder_pubkey,
    const std::vector<uint64_t>& item_values,
    uint64_t total_amount,
    uint64_t current_slot
) {
    return auction_manager_->submit_bid(
        auction_id, bidder_pubkey, item_values,
        total_amount, current_slot
    );
}

bool EconomicOpcodesEngine::sol_econ_settle(uint64_t auction_id, uint64_t current_slot) {
    return auction_manager_->settle_auction(auction_id, current_slot);
}

uint64_t EconomicOpcodesEngine::sol_econ_escrow(
    const std::vector<EscrowParty>& parties,
    uint64_t amount,
    EscrowCondition condition,
    uint64_t release_slot,
    uint64_t expiry_slot
) {
    return escrow_manager_->create_escrow(
        parties, amount, condition, release_slot, expiry_slot
    );
}

bool EconomicOpcodesEngine::sol_econ_release(uint64_t escrow_id, uint64_t current_slot) {
    return escrow_manager_->release_escrow(escrow_id, current_slot);
}

uint64_t EconomicOpcodesEngine::sol_econ_stake(
    const std::string& staker_pubkey,
    const std::string& validator_pubkey,
    uint64_t amount,
    uint64_t duration_slots,
    uint64_t current_slot
) {
    return staking_manager_->lock_stake(
        staker_pubkey, validator_pubkey, amount,
        duration_slots, current_slot
    );
}

bool EconomicOpcodesEngine::sol_econ_slash(
    uint64_t stake_id,
    uint64_t percentage,
    SlashReason reason,
    uint64_t current_slot
) {
    return staking_manager_->slash_stake(stake_id, percentage, reason, current_slot);
}

bool EconomicOpcodesEngine::sol_econ_repute(
    const std::string& entity_pubkey,
    ReputationAction action,
    int32_t value_change,
    uint64_t transaction_volume,
    uint64_t current_slot
) {
    return reputation_manager_->update_reputation(
        entity_pubkey, action, value_change, transaction_volume, current_slot
    );
}

const ReputationScore* EconomicOpcodesEngine::sol_econ_get_reputation(
    const std::string& entity_pubkey
) const {
    return reputation_manager_->get_reputation(entity_pubkey);
}

uint8_t EconomicOpcodesEngine::submit_agent(std::unique_ptr<AgentExecutionContext> ctx) {
    return lane_manager_->submit_agent(std::move(ctx));
}

std::vector<uint8_t> EconomicOpcodesEngine::submit_agents_parallel(
    std::vector<std::unique_ptr<AgentExecutionContext>> contexts
) {
    return lane_manager_->submit_agents(std::move(contexts));
}

} // namespace svm
} // namespace slonana
