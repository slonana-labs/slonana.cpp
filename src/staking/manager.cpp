#include "staking/manager.h"
#include <iostream>
#include <algorithm>
#include <numeric>
#include <optional>

namespace slonana {
namespace staking {

// StakeAccount implementation
std::vector<uint8_t> StakeAccount::serialize() const {
    std::vector<uint8_t> result;
    
    // Serialize stake amount as 8 bytes
    for (int i = 0; i < 8; ++i) {
        result.push_back((stake_amount >> (i * 8)) & 0xFF);
    }
    
    // Add pubkeys (simplified)
    result.insert(result.end(), stake_pubkey.begin(), stake_pubkey.end());
    result.insert(result.end(), validator_pubkey.begin(), validator_pubkey.end());
    
    return result;
}

StakeAccount StakeAccount::deserialize(const std::vector<uint8_t>& data) {
    StakeAccount account;
    
    // Stub deserialization
    if (data.size() >= 8) {
        account.stake_amount = 0;
        for (int i = 0; i < 8; ++i) {
            account.stake_amount |= static_cast<uint64_t>(data[i]) << (i * 8);
        }
    }
    
    return account;
}

// ValidatorStakeInfo implementation
double ValidatorStakeInfo::calculate_apr() const {
    // Stub APR calculation - would use actual inflation and performance metrics
    const double base_inflation = 0.08; // 8% base
    const double performance_factor = std::min(1.0, static_cast<double>(vote_credits) / 1000.0);
    return base_inflation * performance_factor;
}

// RewardsCalculator implementation
class RewardsCalculator::Impl {
public:
    double inflation_rate_ = 0.08; // 8% annual inflation
};

RewardsCalculator::RewardsCalculator() : impl_(std::make_unique<Impl>()) {}
RewardsCalculator::~RewardsCalculator() = default;

common::Lamports RewardsCalculator::calculate_validator_rewards(
    const ValidatorStakeInfo& validator_info,
    common::Epoch epoch) const {
    
    // Simplified reward calculation
    const double epoch_reward_rate = impl_->inflation_rate_ / 365.0; // Daily approximation
    const double commission_factor = 1.0 - (validator_info.commission_rate / 10000.0);
    
    common::Lamports base_rewards = static_cast<common::Lamports>(
        validator_info.total_stake * epoch_reward_rate
    );
    
    common::Lamports commission = static_cast<common::Lamports>(
        base_rewards * (validator_info.commission_rate / 10000.0)
    );
    
    return commission;
}

common::Lamports RewardsCalculator::calculate_delegator_rewards(
    const StakeAccount& stake_account,
    const ValidatorStakeInfo& validator_info,
    common::Epoch epoch) const {
    
    if (!stake_account.is_active) {
        return 0;
    }
    
    const double epoch_reward_rate = impl_->inflation_rate_ / 365.0;
    const double commission_factor = 1.0 - (validator_info.commission_rate / 10000.0);
    
    return static_cast<common::Lamports>(
        stake_account.stake_amount * epoch_reward_rate * commission_factor
    );
}

void RewardsCalculator::set_inflation_rate(double annual_rate) {
    impl_->inflation_rate_ = annual_rate;
}

double RewardsCalculator::get_current_inflation_rate() const {
    return impl_->inflation_rate_;
}

// StakingManager implementation
class StakingManager::Impl {
public:
    std::vector<StakeAccount> stake_accounts_;
    std::vector<ValidatorStakeInfo> validator_infos_;
};

StakingManager::StakingManager()
    : rewards_calculator_(std::make_unique<RewardsCalculator>())
    , impl_(std::make_unique<Impl>()) {
}

StakingManager::~StakingManager() = default;

common::Result<bool> StakingManager::create_stake_account(const StakeAccount& account) {
    // Check for duplicate
    auto existing = get_stake_account(account.stake_pubkey);
    if (existing) {
        return common::Result<bool>("Stake account already exists");
    }
    
    impl_->stake_accounts_.push_back(account);
    std::cout << "Created stake account with " << account.stake_amount << " lamports" << std::endl;
    return common::Result<bool>(true);
}

common::Result<bool> StakingManager::delegate_stake(
    const PublicKey& stake_pubkey,
    const PublicKey& validator_pubkey,
    common::Lamports amount) {
    
    auto account_it = std::find_if(impl_->stake_accounts_.begin(), impl_->stake_accounts_.end(),
                                  [&stake_pubkey](const StakeAccount& account) {
                                      return account.stake_pubkey == stake_pubkey;
                                  });
    
    if (account_it == impl_->stake_accounts_.end()) {
        return common::Result<bool>("Stake account not found");
    }
    
    if (account_it->stake_amount < amount) {
        return common::Result<bool>("Insufficient stake amount");
    }
    
    account_it->validator_pubkey = validator_pubkey;
    account_it->stake_amount = amount;
    account_it->is_active = true;
    
    std::cout << "Delegated " << amount << " lamports to validator" << std::endl;
    return common::Result<bool>(true);
}

common::Result<bool> StakingManager::deactivate_stake(const PublicKey& stake_pubkey) {
    auto account_it = std::find_if(impl_->stake_accounts_.begin(), impl_->stake_accounts_.end(),
                                  [&stake_pubkey](StakeAccount& account) {
                                      return account.stake_pubkey == stake_pubkey;
                                  });
    
    if (account_it == impl_->stake_accounts_.end()) {
        return common::Result<bool>("Stake account not found");
    }
    
    account_it->is_active = false;
    // Clear the validator pubkey when deactivating stake
    account_it->validator_pubkey.assign(32, 0x00);
    std::cout << "Deactivated stake account" << std::endl;
    return common::Result<bool>(true);
}

std::optional<StakeAccount> StakingManager::get_stake_account(const PublicKey& stake_pubkey) const {
    auto it = std::find_if(impl_->stake_accounts_.begin(), impl_->stake_accounts_.end(),
                          [&stake_pubkey](const StakeAccount& account) {
                              return account.stake_pubkey == stake_pubkey;
                          });
    
    if (it != impl_->stake_accounts_.end()) {
        return *it;
    }
    return std::nullopt;
}

std::vector<StakeAccount> StakingManager::get_validator_stake_accounts(const PublicKey& validator_pubkey) const {
    std::vector<StakeAccount> result;
    
    std::copy_if(impl_->stake_accounts_.begin(), impl_->stake_accounts_.end(),
                 std::back_inserter(result),
                 [&validator_pubkey](const StakeAccount& account) {
                     return account.validator_pubkey == validator_pubkey && account.is_active;
                 });
    
    return result;
}

ValidatorStakeInfo StakingManager::get_validator_stake_info(const PublicKey& validator_pubkey) const {
    auto it = std::find_if(impl_->validator_infos_.begin(), impl_->validator_infos_.end(),
                          [&validator_pubkey](const ValidatorStakeInfo& info) {
                              return info.validator_identity == validator_pubkey;
                          });
    
    if (it != impl_->validator_infos_.end()) {
        return *it;
    }
    
    // Return default info if not found
    ValidatorStakeInfo info;
    info.validator_identity = validator_pubkey;
    return info;
}

common::Result<bool> StakingManager::distribute_epoch_rewards(common::Epoch epoch) {
    std::cout << "Distributing rewards for epoch " << epoch << std::endl;
    
    for (const auto& validator_info : impl_->validator_infos_) {
        auto validator_rewards = rewards_calculator_->calculate_validator_rewards(validator_info, epoch);
        
        auto stake_accounts = get_validator_stake_accounts(validator_info.validator_identity);
        for (const auto& stake_account : stake_accounts) {
            auto delegator_rewards = rewards_calculator_->calculate_delegator_rewards(
                stake_account, validator_info, epoch);
            
            // In a real implementation, these rewards would be distributed to accounts
            std::cout << "Calculated " << delegator_rewards << " lamports reward for delegator" << std::endl;
        }
    }
    
    return common::Result<bool>(true);
}

std::unordered_map<PublicKey, common::Lamports> StakingManager::calculate_pending_rewards(
    common::Epoch epoch) const {
    
    std::unordered_map<PublicKey, common::Lamports> rewards;
    
    for (const auto& stake_account : impl_->stake_accounts_) {
        if (stake_account.is_active) {
            auto validator_info = get_validator_stake_info(stake_account.validator_pubkey);
            auto reward = rewards_calculator_->calculate_delegator_rewards(
                stake_account, validator_info, epoch);
            rewards[stake_account.delegator_pubkey] += reward;
        }
    }
    
    return rewards;
}

common::Result<bool> StakingManager::register_validator(
    const PublicKey& validator_identity,
    uint32_t commission_rate) {
    
    if (commission_rate > 10000) {
        return common::Result<bool>("Commission rate cannot exceed 100%");
    }
    
    ValidatorStakeInfo info;
    info.validator_identity = validator_identity;
    info.commission_rate = commission_rate;
    
    impl_->validator_infos_.push_back(info);
    std::cout << "Registered validator with " << commission_rate << " basis points commission" << std::endl;
    
    return common::Result<bool>(true);
}

bool StakingManager::is_validator_registered(const PublicKey& validator_identity) const {
    return std::any_of(impl_->validator_infos_.begin(), impl_->validator_infos_.end(),
                      [&validator_identity](const ValidatorStakeInfo& info) {
                          return info.validator_identity == validator_identity;
                      });
}

common::Lamports StakingManager::get_total_stake() const {
    return std::accumulate(impl_->stake_accounts_.begin(), impl_->stake_accounts_.end(),
                          static_cast<common::Lamports>(0),
                          [](common::Lamports sum, const StakeAccount& account) {
                              return account.is_active ? sum + account.stake_amount : sum;
                          });
}

size_t StakingManager::get_active_validator_count() const {
    return impl_->validator_infos_.size();
}

std::vector<ValidatorStakeInfo> StakingManager::get_top_validators(size_t count) const {
    auto validators = impl_->validator_infos_;
    
    // Sort by total stake descending
    std::sort(validators.begin(), validators.end(),
             [](const ValidatorStakeInfo& a, const ValidatorStakeInfo& b) {
                 return a.total_stake > b.total_stake;
             });
    
    if (validators.size() > count) {
        validators.resize(count);
    }
    
    return validators;
}

} // namespace staking
} // namespace slonana