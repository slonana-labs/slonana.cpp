#pragma once

#include "common/types.h"
#include <memory>
#include <unordered_map>
#include <optional>

namespace slonana {
namespace staking {

using namespace slonana::common;

/**
 * Stake account information
 */
struct StakeAccount {
    PublicKey stake_pubkey;
    PublicKey validator_pubkey;
    PublicKey delegator_pubkey;
    Lamports stake_amount;
    Epoch activation_epoch;
    Epoch deactivation_epoch;
    bool is_active;
    
    std::vector<uint8_t> serialize() const;
    static StakeAccount deserialize(const std::vector<uint8_t>& data);
};

/**
 * Validator stake information and performance metrics
 */
struct ValidatorStakeInfo {
    PublicKey validator_identity;
    Lamports total_stake;
    Lamports self_stake;
    Lamports delegated_stake;
    uint32_t commission_rate; // basis points (0-10000)
    Epoch last_vote_epoch;
    uint64_t vote_credits;
    
    double calculate_apr() const;
};

/**
 * Reward calculation and distribution
 */
class RewardsCalculator {
public:
    RewardsCalculator();
    ~RewardsCalculator();
    
    // Calculate rewards for an epoch
    Lamports calculate_validator_rewards(
        const ValidatorStakeInfo& validator_info,
        Epoch epoch
    ) const;
    
    Lamports calculate_delegator_rewards(
        const StakeAccount& stake_account,
        const ValidatorStakeInfo& validator_info,
        Epoch epoch
    ) const;
    
    // Inflation and reward parameters
    void set_inflation_rate(double annual_rate);
    double get_current_inflation_rate() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Staking manager for handling stake accounts and reward distribution
 */
class StakingManager {
public:
    StakingManager();
    ~StakingManager();
    
    // Stake account management
    Result<bool> create_stake_account(const StakeAccount& account);
    Result<bool> delegate_stake(
        const PublicKey& stake_pubkey,
        const PublicKey& validator_pubkey,
        Lamports amount
    );
    Result<bool> deactivate_stake(const PublicKey& stake_pubkey);
    
    // Queries
    std::optional<StakeAccount> get_stake_account(const PublicKey& stake_pubkey) const;
    std::vector<StakeAccount> get_validator_stake_accounts(const PublicKey& validator_pubkey) const;
    ValidatorStakeInfo get_validator_stake_info(const PublicKey& validator_pubkey) const;
    
    // Reward processing
    Result<bool> distribute_epoch_rewards(Epoch epoch);
    std::unordered_map<PublicKey, Lamports> calculate_pending_rewards(
        Epoch epoch
    ) const;
    
    // Validator operations
    Result<bool> register_validator(
        const PublicKey& validator_identity,
        uint32_t commission_rate
    );
    bool is_validator_registered(const PublicKey& validator_identity) const;
    
    // Statistics
    Lamports get_total_stake() const;
    size_t get_active_validator_count() const;
    std::vector<ValidatorStakeInfo> get_top_validators(size_t count) const;

private:
    std::unique_ptr<RewardsCalculator> rewards_calculator_;
    
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace staking
} // namespace slonana