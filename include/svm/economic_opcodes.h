#pragma once

#include "svm/engine.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <queue>
#include <condition_variable>

namespace slonana {
namespace svm {

/**
 * Native Economic Opcodes Module
 * 
 * Implements built-in instructions for common economic patterns,
 * providing massive compute unit savings over manual implementations:
 * 
 * - BPF_ECON_AUCTION: Create VCG/GSP auctions (50x CU reduction)
 * - BPF_ECON_BID: Submit sealed bids
 * - BPF_ECON_SETTLE: Compute optimal allocation (40x CU reduction)
 * - BPF_ECON_ESCROW: Multi-party escrow
 * - BPF_ECON_RELEASE: Condition-based release
 * - BPF_ECON_STAKE: Lock tokens with slash capability
 * - BPF_ECON_SLASH: Economic punishment
 * - BPF_ECON_REPUTE: Update reputation scores
 */

// ============================================================================
// Constants and Compute Unit Costs
// ============================================================================

// Compute unit costs for economic opcodes (much lower than manual implementation)
namespace econ_compute_costs {
    constexpr uint64_t AUCTION_CREATE = 1000;      // Manual: 50,000 CU
    constexpr uint64_t BID_SUBMIT = 500;           // Manual: 10,000 CU
    constexpr uint64_t AUCTION_SETTLE = 5000;      // Manual: 200,000 CU
    constexpr uint64_t ESCROW_CREATE = 500;        // Manual: 10,000 CU
    constexpr uint64_t ESCROW_RELEASE = 200;       // Manual: 5,000 CU
    constexpr uint64_t STAKE_LOCK = 400;           // Manual: 8,000 CU
    constexpr uint64_t STAKE_SLASH = 300;          // Manual: 6,000 CU
    constexpr uint64_t REPUTATION_UPDATE = 100;    // Manual: 2,000 CU
}

// Limits
constexpr uint64_t MAX_AUCTION_ITEMS = 64;
constexpr uint64_t MAX_BIDS_PER_AUCTION = 256;
constexpr uint64_t MAX_ESCROW_PARTIES = 16;
constexpr uint64_t MAX_STAKE_DURATION_SLOTS = 100000000;
constexpr uint64_t MIN_STAKE_AMOUNT = 1000;
constexpr uint64_t MAX_SLASH_PERCENTAGE = 100;

// ============================================================================
// Auction Types
// ============================================================================

/**
 * Auction mechanism type
 */
enum class AuctionType : uint8_t {
    VCG = 0,          // Vickrey-Clarke-Groves (truthful multi-item auction)
    GSP = 1,          // Generalized Second Price
    ENGLISH = 2,      // English ascending price
    DUTCH = 3,        // Dutch descending price
    SEALED_FIRST = 4, // Sealed-bid first price
    SEALED_SECOND = 5 // Sealed-bid second price (Vickrey)
};

/**
 * Auction state
 */
enum class AuctionState : uint8_t {
    CREATED = 0,      // Auction created, accepting bids
    BIDDING = 1,      // Bidding phase active
    SETTLING = 2,     // Computing allocations
    SETTLED = 3,      // Auction complete
    CANCELLED = 4     // Auction cancelled
};

/**
 * Bid entry structure
 */
struct AuctionBid {
    uint64_t bid_id;
    std::string bidder_pubkey;
    std::vector<uint64_t> item_values;  // Value per item (for VCG)
    uint64_t total_bid_amount;
    uint64_t timestamp_slot;
    bool is_sealed;
    std::vector<uint8_t> sealed_hash;   // For sealed bids
    std::vector<uint8_t> revealed_data; // Revealed bid data
    bool is_revealed;
    bool is_winner;
    uint64_t allocation_mask;           // Which items won (bit mask)
    uint64_t payment_amount;            // Amount to pay
    
    AuctionBid()
        : bid_id(0), total_bid_amount(0), timestamp_slot(0),
          is_sealed(false), is_revealed(false), is_winner(false),
          allocation_mask(0), payment_amount(0) {}
};

/**
 * Auction structure
 */
struct EconAuction {
    uint64_t auction_id;
    std::string creator_pubkey;
    AuctionType auction_type;
    AuctionState state;
    
    // Items
    std::vector<std::string> items;     // Item identifiers
    std::vector<uint64_t> reserve_prices;
    
    // Timing
    uint64_t created_at_slot;
    uint64_t deadline_slot;
    uint64_t settlement_slot;
    
    // Configuration
    std::string settlement_account;     // Where payments go
    uint64_t min_bid_increment;
    bool allow_sealed_bids;
    
    // Bids
    std::vector<AuctionBid> bids;
    
    // Results
    std::vector<std::string> winner_pubkeys;
    std::vector<uint64_t> final_prices;
    uint64_t total_revenue;
    
    EconAuction()
        : auction_id(0), auction_type(AuctionType::VCG),
          state(AuctionState::CREATED), created_at_slot(0),
          deadline_slot(0), settlement_slot(0), min_bid_increment(1),
          allow_sealed_bids(true), total_revenue(0) {}
};

// ============================================================================
// Escrow Types
// ============================================================================

/**
 * Escrow release condition type
 */
enum class EscrowCondition : uint8_t {
    TIME_LOCK = 0,          // Release after slot
    MULTI_SIG = 1,          // Release with M-of-N signatures
    ORACLE_CONDITION = 2,   // Release when oracle confirms
    SMART_CONTRACT = 3,     // Release by contract logic
    UNCONDITIONAL = 4       // Release any time by receiver
};

/**
 * Escrow state
 */
enum class EscrowState : uint8_t {
    ACTIVE = 0,       // Funds locked
    RELEASED = 1,     // Funds released to receiver
    REFUNDED = 2,     // Funds returned to sender
    DISPUTED = 3,     // Under dispute resolution
    EXPIRED = 4       // Escrow expired
};

/**
 * Escrow party role
 */
enum class EscrowRole : uint8_t {
    SENDER = 0,
    RECEIVER = 1,
    ARBITER = 2,
    WITNESS = 3
};

/**
 * Escrow party info
 */
struct EscrowParty {
    std::string pubkey;
    EscrowRole role;
    bool has_approved;
    uint64_t share_percentage;  // For multi-party splits
    
    EscrowParty()
        : role(EscrowRole::SENDER), has_approved(false),
          share_percentage(0) {}
};

/**
 * Multi-party escrow structure
 */
struct EconEscrow {
    uint64_t escrow_id;
    EscrowState state;
    EscrowCondition release_condition;
    
    // Parties
    std::vector<EscrowParty> parties;
    uint8_t required_approvals;     // For multi-sig
    uint8_t current_approvals;
    
    // Funds
    uint64_t total_amount;
    std::string token_mint;         // Empty for SOL
    
    // Timing
    uint64_t created_at_slot;
    uint64_t release_slot;          // For TIME_LOCK
    uint64_t expiry_slot;
    
    // Oracle condition (if applicable)
    std::string oracle_pubkey;
    std::vector<uint8_t> oracle_condition_data;
    
    // Metadata
    std::string description;
    std::vector<uint8_t> metadata;
    
    EconEscrow()
        : escrow_id(0), state(EscrowState::ACTIVE),
          release_condition(EscrowCondition::TIME_LOCK),
          required_approvals(1), current_approvals(0),
          total_amount(0), created_at_slot(0),
          release_slot(0), expiry_slot(0) {}
};

// ============================================================================
// Staking Types
// ============================================================================

/**
 * Stake state
 */
enum class StakeState : uint8_t {
    LOCKED = 0,       // Stake is locked
    UNLOCKING = 1,    // Unbonding period
    UNLOCKED = 2,     // Ready to withdraw
    SLASHED = 3       // Partially or fully slashed
};

/**
 * Slash reason
 */
enum class SlashReason : uint8_t {
    DOUBLE_SIGNING = 0,
    DOWNTIME = 1,
    MALICIOUS_BEHAVIOR = 2,
    PROTOCOL_VIOLATION = 3,
    GOVERNANCE_DECISION = 4,
    OTHER = 255
};

/**
 * Stake entry structure
 */
struct EconStake {
    uint64_t stake_id;
    std::string staker_pubkey;
    std::string validator_pubkey;
    StakeState state;
    
    // Amount
    uint64_t staked_amount;
    uint64_t slashed_amount;
    uint64_t rewards_earned;
    
    // Timing
    uint64_t locked_at_slot;
    uint64_t unlock_slot;           // When unlocking completes
    uint64_t lock_duration_slots;
    
    // Delegation
    bool is_delegated;
    uint64_t delegation_fee_bps;    // Basis points (e.g., 500 = 5%)
    
    // Slash history
    struct SlashEvent {
        SlashReason reason;
        uint64_t amount;
        uint64_t slot;
    };
    std::vector<SlashEvent> slash_history;
    
    EconStake()
        : stake_id(0), state(StakeState::LOCKED),
          staked_amount(0), slashed_amount(0), rewards_earned(0),
          locked_at_slot(0), unlock_slot(0), lock_duration_slots(0),
          is_delegated(false), delegation_fee_bps(0) {}
};

// ============================================================================
// Reputation Types
// ============================================================================

/**
 * Reputation score update type
 */
enum class ReputationAction : uint8_t {
    TRADE_COMPLETED = 0,
    TRADE_FAILED = 1,
    PAYMENT_ON_TIME = 2,
    PAYMENT_LATE = 3,
    DISPUTE_WON = 4,
    DISPUTE_LOST = 5,
    REFERRAL = 6,
    VERIFICATION = 7,
    PENALTY = 8,
    BONUS = 9
};

/**
 * Reputation score structure
 */
struct ReputationScore {
    uint64_t reputation_id;
    std::string entity_pubkey;
    
    // Core scores (scaled 0-10000 for precision)
    int32_t overall_score;          // Composite score
    int32_t trade_score;            // Trading history
    int32_t payment_score;          // Payment reliability
    int32_t dispute_score;          // Dispute resolution
    int32_t activity_score;         // Activity level
    
    // Counts
    uint64_t total_transactions;
    uint64_t successful_transactions;
    uint64_t failed_transactions;
    uint64_t total_disputes;
    uint64_t disputes_won;
    
    // History
    uint64_t created_at_slot;
    uint64_t last_update_slot;
    uint64_t total_volume;          // Total transaction volume
    
    ReputationScore()
        : reputation_id(0), overall_score(5000),
          trade_score(5000), payment_score(5000),
          dispute_score(5000), activity_score(0),
          total_transactions(0), successful_transactions(0),
          failed_transactions(0), total_disputes(0),
          disputes_won(0), created_at_slot(0),
          last_update_slot(0), total_volume(0) {}
};

// ============================================================================
// Auction Manager
// ============================================================================

/**
 * Manages native auction operations
 */
class AuctionManager {
public:
    AuctionManager();
    ~AuctionManager();
    
    /**
     * Create auction (BPF_ECON_AUCTION)
     * @return Auction ID or 0 on failure
     */
    uint64_t create_auction(
        const std::string& creator_pubkey,
        AuctionType type,
        const std::vector<std::string>& items,
        const std::vector<uint64_t>& reserve_prices,
        uint64_t deadline_slot,
        const std::string& settlement_account,
        uint64_t min_bid_increment = 1,
        bool allow_sealed = true
    );
    
    /**
     * Submit bid (BPF_ECON_BID)
     */
    uint64_t submit_bid(
        uint64_t auction_id,
        const std::string& bidder_pubkey,
        const std::vector<uint64_t>& item_values,
        uint64_t total_amount,
        uint64_t current_slot,
        const std::vector<uint8_t>& sealed_hash = {}
    );
    
    /**
     * Reveal sealed bid
     */
    bool reveal_bid(
        uint64_t auction_id,
        uint64_t bid_id,
        const std::vector<uint8_t>& revealed_data
    );
    
    /**
     * Settle auction (BPF_ECON_SETTLE)
     * Computes optimal allocation and prices
     */
    bool settle_auction(uint64_t auction_id, uint64_t current_slot);
    
    /**
     * Cancel auction
     */
    bool cancel_auction(uint64_t auction_id);
    
    /**
     * Get auction info
     */
    const EconAuction* get_auction(uint64_t auction_id) const;
    
    /**
     * Get bid info
     */
    const AuctionBid* get_bid(uint64_t auction_id, uint64_t bid_id) const;
    
    /**
     * Get compute cost for operation
     */
    uint64_t get_compute_cost(const std::string& operation) const;
    
private:
    std::unordered_map<uint64_t, EconAuction> auctions_;
    mutable std::mutex mutex_;
    std::atomic<uint64_t> next_auction_id_{1};
    std::atomic<uint64_t> next_bid_id_{1};
    
    // VCG allocation algorithm
    void compute_vcg_allocation(EconAuction& auction);
    
    // GSP allocation algorithm
    void compute_gsp_allocation(EconAuction& auction);
    
    // Second price auction
    void compute_second_price_allocation(EconAuction& auction);
};

// ============================================================================
// Escrow Manager
// ============================================================================

/**
 * Manages native escrow operations
 */
class EscrowManager {
public:
    EscrowManager();
    ~EscrowManager();
    
    /**
     * Create escrow (BPF_ECON_ESCROW)
     */
    uint64_t create_escrow(
        const std::vector<EscrowParty>& parties,
        uint64_t amount,
        EscrowCondition condition,
        uint64_t release_slot,
        uint64_t expiry_slot,
        uint8_t required_approvals = 1,
        const std::string& token_mint = "",
        const std::string& description = ""
    );
    
    /**
     * Approve escrow release
     */
    bool approve_release(
        uint64_t escrow_id,
        const std::string& approver_pubkey
    );
    
    /**
     * Release escrow (BPF_ECON_RELEASE)
     */
    bool release_escrow(uint64_t escrow_id, uint64_t current_slot);
    
    /**
     * Refund escrow
     */
    bool refund_escrow(uint64_t escrow_id);
    
    /**
     * Start dispute
     */
    bool start_dispute(uint64_t escrow_id, const std::string& disputer_pubkey);
    
    /**
     * Resolve dispute
     */
    bool resolve_dispute(
        uint64_t escrow_id,
        const std::string& arbiter_pubkey,
        bool release_to_receiver
    );
    
    /**
     * Get escrow info
     */
    const EconEscrow* get_escrow(uint64_t escrow_id) const;
    
    /**
     * Check expired escrows
     */
    std::vector<uint64_t> check_expired(uint64_t current_slot);
    
private:
    std::unordered_map<uint64_t, EconEscrow> escrows_;
    mutable std::mutex mutex_;
    std::atomic<uint64_t> next_escrow_id_{1};
    
    bool can_release(const EconEscrow& escrow, uint64_t current_slot) const;
};

// ============================================================================
// Staking Manager
// ============================================================================

/**
 * Manages native staking operations
 */
class StakingManager {
public:
    StakingManager();
    ~StakingManager();
    
    /**
     * Lock stake (BPF_ECON_STAKE)
     */
    uint64_t lock_stake(
        const std::string& staker_pubkey,
        const std::string& validator_pubkey,
        uint64_t amount,
        uint64_t duration_slots,
        uint64_t current_slot,
        bool is_delegated = false,
        uint64_t delegation_fee_bps = 0
    );
    
    /**
     * Begin unstaking
     */
    bool begin_unstake(uint64_t stake_id, uint64_t current_slot);
    
    /**
     * Complete unstake (after unbonding period)
     */
    bool complete_unstake(uint64_t stake_id, uint64_t current_slot);
    
    /**
     * Slash stake (BPF_ECON_SLASH)
     */
    bool slash_stake(
        uint64_t stake_id,
        uint64_t percentage,
        SlashReason reason,
        uint64_t current_slot
    );
    
    /**
     * Distribute rewards
     */
    bool distribute_rewards(
        uint64_t stake_id,
        uint64_t reward_amount
    );
    
    /**
     * Get stake info
     */
    const EconStake* get_stake(uint64_t stake_id) const;
    
    /**
     * Get stakes by staker
     */
    std::vector<const EconStake*> get_staker_stakes(
        const std::string& staker_pubkey) const;
    
    /**
     * Get stakes by validator
     */
    std::vector<const EconStake*> get_validator_stakes(
        const std::string& validator_pubkey) const;
    
    /**
     * Get total staked amount
     */
    uint64_t get_total_staked() const;
    
private:
    std::unordered_map<uint64_t, EconStake> stakes_;
    std::unordered_map<std::string, std::vector<uint64_t>> staker_stakes_;
    std::unordered_map<std::string, std::vector<uint64_t>> validator_stakes_;
    mutable std::mutex mutex_;
    std::atomic<uint64_t> next_stake_id_{1};
    std::atomic<uint64_t> total_staked_{0};
};

// ============================================================================
// Reputation Manager
// ============================================================================

/**
 * Manages native reputation operations
 */
class ReputationManager {
public:
    ReputationManager();
    ~ReputationManager();
    
    /**
     * Create or get reputation score for entity
     */
    uint64_t get_or_create_reputation(
        const std::string& entity_pubkey,
        uint64_t current_slot
    );
    
    /**
     * Update reputation (BPF_ECON_REPUTE)
     */
    bool update_reputation(
        const std::string& entity_pubkey,
        ReputationAction action,
        int32_t value_change,
        uint64_t transaction_volume,
        uint64_t current_slot
    );
    
    /**
     * Get reputation score
     */
    const ReputationScore* get_reputation(const std::string& entity_pubkey) const;
    
    /**
     * Get reputation by ID
     */
    const ReputationScore* get_reputation_by_id(uint64_t reputation_id) const;
    
    /**
     * Check if entity has minimum reputation
     */
    bool meets_threshold(
        const std::string& entity_pubkey,
        int32_t min_score
    ) const;
    
    /**
     * Get top entities by reputation
     */
    std::vector<std::pair<std::string, int32_t>> get_top_entities(
        size_t count) const;
    
private:
    std::unordered_map<std::string, ReputationScore> reputations_;
    std::unordered_map<uint64_t, std::string> reputation_ids_;
    mutable std::mutex mutex_;
    std::atomic<uint64_t> next_reputation_id_{1};
    
    void recalculate_overall_score(ReputationScore& score);
};

// ============================================================================
// Concurrent Execution Lanes
// ============================================================================

/**
 * Execution lane state
 */
enum class LaneState : uint8_t {
    IDLE = 0,
    RUNNING = 1,
    BLOCKED = 2,
    COMPLETED = 3
};

/**
 * Agent execution context for parallel lanes
 */
struct AgentExecutionContext {
    uint64_t agent_id;
    std::string program_id;
    uint8_t lane_id;
    LaneState state;
    
    // Execution
    std::vector<uint8_t> bytecode;
    std::vector<AccountInfo> accounts;
    uint64_t compute_budget;
    uint64_t compute_used;
    
    // Isolation
    std::vector<std::string> read_set;   // Accounts to read
    std::vector<std::string> write_set;  // Accounts to write
    
    // Result
    bool success;
    std::string error_message;
    std::vector<uint8_t> return_data;
    
    AgentExecutionContext()
        : agent_id(0), lane_id(0), state(LaneState::IDLE),
          compute_budget(0), compute_used(0), success(false) {}
};

/**
 * Parallel execution lane
 */
class ExecutionLane {
public:
    ExecutionLane(uint8_t lane_id);
    ~ExecutionLane();
    
    /**
     * Start lane worker
     */
    bool start();
    
    /**
     * Stop lane worker
     */
    void stop();
    
    /**
     * Submit agent for execution
     */
    bool submit_agent(std::unique_ptr<AgentExecutionContext> ctx);
    
    /**
     * Check if lane is available
     */
    bool is_available() const;
    
    /**
     * Get lane ID
     */
    uint8_t get_lane_id() const { return lane_id_; }
    
    /**
     * Get current agent ID (0 if none)
     */
    uint64_t get_current_agent_id() const;
    
    /**
     * Get completed count
     */
    uint64_t get_completed_count() const { return completed_count_.load(); }
    
private:
    uint8_t lane_id_;
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    
    // Queue
    std::queue<std::unique_ptr<AgentExecutionContext>> pending_;
    std::unique_ptr<AgentExecutionContext> current_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    
    // Stats
    std::atomic<uint64_t> completed_count_{0};
    
    void worker_loop();
    void execute_agent(AgentExecutionContext& ctx);
};

/**
 * Concurrent Execution Lane Manager
 * Manages parallel execution of agents across multiple lanes
 */
class ExecutionLaneManager {
public:
    explicit ExecutionLaneManager(uint8_t num_lanes = 8);
    ~ExecutionLaneManager();
    
    /**
     * Initialize all lanes
     */
    bool initialize();
    
    /**
     * Shutdown all lanes
     */
    void shutdown();
    
    /**
     * Submit agent for execution
     * Automatically assigns to available lane
     * @return Lane ID assigned, or 255 if all lanes busy
     */
    uint8_t submit_agent(std::unique_ptr<AgentExecutionContext> ctx);
    
    /**
     * Submit multiple agents with conflict detection
     * Agents with overlapping write sets go to same lane
     */
    std::vector<uint8_t> submit_agents(
        std::vector<std::unique_ptr<AgentExecutionContext>> contexts
    );
    
    /**
     * Get lane by ID
     */
    ExecutionLane* get_lane(uint8_t lane_id);
    
    /**
     * Get number of available lanes
     */
    uint8_t get_available_lane_count() const;
    
    /**
     * Get total completed executions
     */
    uint64_t get_total_completed() const;
    
    /**
     * Check for write set conflicts between two agents
     */
    static bool has_conflict(
        const AgentExecutionContext& a,
        const AgentExecutionContext& b
    );
    
private:
    std::vector<std::unique_ptr<ExecutionLane>> lanes_;
    uint8_t num_lanes_;
    std::atomic<bool> running_{false};
    
    uint8_t find_available_lane() const;
    uint8_t find_lane_for_agent(
        const AgentExecutionContext& ctx,
        const std::vector<uint8_t>& assigned_lanes
    ) const;
};

// ============================================================================
// Economic Opcodes Engine
// ============================================================================

/**
 * Main engine for economic opcodes and concurrent execution
 */
class EconomicOpcodesEngine {
public:
    EconomicOpcodesEngine(uint8_t num_lanes = 8);
    ~EconomicOpcodesEngine();
    
    /**
     * Initialize the engine
     */
    bool initialize();
    
    /**
     * Shutdown the engine
     */
    void shutdown();
    
    /**
     * Check if engine is running
     */
    bool is_running() const { return running_.load(); }
    
    // ========================================================================
    // Auction Syscalls
    // ========================================================================
    
    uint64_t sol_econ_auction_create(
        const std::string& creator_pubkey,
        AuctionType type,
        const std::vector<std::string>& items,
        const std::vector<uint64_t>& reserve_prices,
        uint64_t deadline_slot,
        const std::string& settlement_account
    );
    
    uint64_t sol_econ_bid(
        uint64_t auction_id,
        const std::string& bidder_pubkey,
        const std::vector<uint64_t>& item_values,
        uint64_t total_amount,
        uint64_t current_slot
    );
    
    bool sol_econ_settle(uint64_t auction_id, uint64_t current_slot);
    
    // ========================================================================
    // Escrow Syscalls
    // ========================================================================
    
    uint64_t sol_econ_escrow(
        const std::vector<EscrowParty>& parties,
        uint64_t amount,
        EscrowCondition condition,
        uint64_t release_slot,
        uint64_t expiry_slot
    );
    
    bool sol_econ_release(uint64_t escrow_id, uint64_t current_slot);
    
    // ========================================================================
    // Staking Syscalls
    // ========================================================================
    
    uint64_t sol_econ_stake(
        const std::string& staker_pubkey,
        const std::string& validator_pubkey,
        uint64_t amount,
        uint64_t duration_slots,
        uint64_t current_slot
    );
    
    bool sol_econ_slash(
        uint64_t stake_id,
        uint64_t percentage,
        SlashReason reason,
        uint64_t current_slot
    );
    
    // ========================================================================
    // Reputation Syscalls
    // ========================================================================
    
    bool sol_econ_repute(
        const std::string& entity_pubkey,
        ReputationAction action,
        int32_t value_change,
        uint64_t transaction_volume,
        uint64_t current_slot
    );
    
    const ReputationScore* sol_econ_get_reputation(
        const std::string& entity_pubkey
    ) const;
    
    // ========================================================================
    // Concurrent Execution
    // ========================================================================
    
    uint8_t submit_agent(std::unique_ptr<AgentExecutionContext> ctx);
    std::vector<uint8_t> submit_agents_parallel(
        std::vector<std::unique_ptr<AgentExecutionContext>> contexts
    );
    
    // ========================================================================
    // Getters
    // ========================================================================
    
    AuctionManager& get_auction_manager() { return *auction_manager_; }
    EscrowManager& get_escrow_manager() { return *escrow_manager_; }
    StakingManager& get_staking_manager() { return *staking_manager_; }
    ReputationManager& get_reputation_manager() { return *reputation_manager_; }
    ExecutionLaneManager& get_lane_manager() { return *lane_manager_; }
    
private:
    std::unique_ptr<AuctionManager> auction_manager_;
    std::unique_ptr<EscrowManager> escrow_manager_;
    std::unique_ptr<StakingManager> staking_manager_;
    std::unique_ptr<ReputationManager> reputation_manager_;
    std::unique_ptr<ExecutionLaneManager> lane_manager_;
    
    std::atomic<bool> running_{false};
};

// ============================================================================
// Extern "C" Syscall Wrappers
// ============================================================================

extern "C" {

/**
 * Create auction
 * @return Auction ID or 0 on failure
 */
uint64_t sol_econ_auction_create(
    uint8_t auction_type,
    const uint8_t* items_data,
    uint64_t items_len,
    const uint64_t* reserve_prices,
    uint64_t reserve_len,
    uint64_t deadline_slot,
    const uint8_t* settlement_pubkey
);

/**
 * Submit bid
 * @return Bid ID or 0 on failure
 */
uint64_t sol_econ_bid(
    uint64_t auction_id,
    const uint8_t* bidder_pubkey,
    const uint64_t* item_values,
    uint64_t values_len,
    uint64_t total_amount
);

/**
 * Settle auction
 * @return 0 on success, error code on failure
 */
uint64_t sol_econ_settle(uint64_t auction_id);

/**
 * Create escrow
 * @return Escrow ID or 0 on failure
 */
uint64_t sol_econ_escrow(
    const uint8_t* parties_data,
    uint64_t parties_len,
    uint64_t amount,
    uint8_t condition,
    uint64_t release_slot,
    uint64_t expiry_slot
);

/**
 * Release escrow
 * @return 0 on success, error code on failure
 */
uint64_t sol_econ_release(uint64_t escrow_id);

/**
 * Lock stake
 * @return Stake ID or 0 on failure
 */
uint64_t sol_econ_stake(
    const uint8_t* staker_pubkey,
    const uint8_t* validator_pubkey,
    uint64_t amount,
    uint64_t duration_slots
);

/**
 * Slash stake
 * @return 0 on success, error code on failure
 */
uint64_t sol_econ_slash(
    uint64_t stake_id,
    uint64_t percentage,
    uint8_t reason
);

/**
 * Update reputation
 * @return 0 on success, error code on failure
 */
uint64_t sol_econ_repute(
    const uint8_t* entity_pubkey,
    uint8_t action,
    int32_t value_change,
    uint64_t transaction_volume
);

/**
 * Get current slot
 * @return Current slot number
 */
uint64_t sol_get_slot();

} // extern "C"

} // namespace svm
} // namespace slonana
