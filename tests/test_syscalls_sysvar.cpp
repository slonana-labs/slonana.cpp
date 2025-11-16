#include "svm/syscalls.h"
#include "test_framework.h"
#include <cstring>

using namespace slonana::svm;

/**
 * Test Suite for Sysvar Syscalls
 */

void test_epoch_stake_valid() {
    uint8_t vote_pubkey[32];
    std::memset(vote_pubkey, 0, 32);
    vote_pubkey[0] = 0xAB;
    
    uint8_t stake_out[16];
    uint64_t stake_len = 0;
    
    uint64_t result = sol_get_epoch_stake(vote_pubkey, stake_out, &stake_len);
    
    ASSERT_EQ(result, (uint64_t)0);
    ASSERT_EQ(stake_len, (uint64_t)16);
}

void test_epoch_stake_null_pubkey() {
    uint8_t stake_out[16];
    uint64_t stake_len = 0;
    
    uint64_t result = sol_get_epoch_stake(nullptr, stake_out, &stake_len);
    
    ASSERT_NE(result, (uint64_t)0);
}

void test_epoch_stake_returns_amounts() {
    uint8_t vote_pubkey[32];
    std::memset(vote_pubkey, 0, 32);
    
    uint8_t stake_out[16];
    uint64_t stake_len = 0;
    
    uint64_t result = sol_get_epoch_stake(vote_pubkey, stake_out, &stake_len);
    
    ASSERT_EQ(result, (uint64_t)0);
    
    uint64_t activated_stake;
    std::memcpy(&activated_stake, stake_out, sizeof(uint64_t));
    
    ASSERT_GT(activated_stake, (uint64_t)0);
}

void test_epoch_rewards_valid() {
    uint8_t result[128];
    uint64_t result_len = 0;
    
    uint64_t ret = sol_get_epoch_rewards_sysvar(result, &result_len);
    
    ASSERT_EQ(ret, (uint64_t)0);
    ASSERT_GT(result_len, (uint64_t)0);
}

void test_epoch_rewards_null_output() {
    uint64_t result_len = 0;
    
    uint64_t ret = sol_get_epoch_rewards_sysvar(nullptr, &result_len);
    
    ASSERT_NE(ret, (uint64_t)0);
}

void test_epoch_rewards_returns_data() {
    uint8_t result[128];
    uint64_t result_len = 0;
    
    uint64_t ret = sol_get_epoch_rewards_sysvar(result, &result_len);
    
    ASSERT_EQ(ret, (uint64_t)0);
    
    uint64_t total_rewards;
    uint64_t distributed_rewards;
    
    std::memcpy(&total_rewards, result, sizeof(uint64_t));
    std::memcpy(&distributed_rewards, result + sizeof(uint64_t), sizeof(uint64_t));
    
    ASSERT_GT(total_rewards, (uint64_t)0);
    ASSERT_LE(distributed_rewards, total_rewards);
}

void test_last_restart_slot_valid() {
    uint64_t slot_out = UINT64_MAX;
    
    uint64_t result = sol_get_last_restart_slot(&slot_out);
    
    ASSERT_EQ(result, (uint64_t)0);
    ASSERT_NE(slot_out, (uint64_t)UINT64_MAX);
}

void test_last_restart_slot_null() {
    uint64_t result = sol_get_last_restart_slot(nullptr);
    
    ASSERT_NE(result, (uint64_t)0);
}

void test_all_sysvars_callable() {
    uint8_t vote_pubkey[32] = {0};
    uint8_t stake_out[16];
    uint64_t stake_len = 0;
    
    uint64_t result1 = sol_get_epoch_stake(vote_pubkey, stake_out, &stake_len);
    ASSERT_EQ(result1, (uint64_t)0);
    
    uint8_t rewards_out[128];
    uint64_t rewards_len = 0;
    
    uint64_t result2 = sol_get_epoch_rewards_sysvar(rewards_out, &rewards_len);
    ASSERT_EQ(result2, (uint64_t)0);
    
    uint64_t slot_out;
    
    uint64_t result3 = sol_get_last_restart_slot(&slot_out);
    ASSERT_EQ(result3, (uint64_t)0);
}

void test_compute_unit_costs_defined() {
    ASSERT_GT(compute_units::SYSVAR_BASE, (uint64_t)0);
    ASSERT_GT(compute_units::EPOCH_STAKE, (uint64_t)0);
    ASSERT_GT(compute_units::EPOCH_STAKE, compute_units::SYSVAR_BASE);
}

int main() {
    TestRunner runner;
    
    std::cout << "\n=== Sysvar Syscalls Tests ===\n" << std::endl;
    
    runner.run_test("Epoch Stake Valid", test_epoch_stake_valid);
    runner.run_test("Epoch Stake Null Pubkey", test_epoch_stake_null_pubkey);
    runner.run_test("Epoch Stake Returns Amounts", test_epoch_stake_returns_amounts);
    runner.run_test("Epoch Rewards Valid", test_epoch_rewards_valid);
    runner.run_test("Epoch Rewards Null Output", test_epoch_rewards_null_output);
    runner.run_test("Epoch Rewards Returns Data", test_epoch_rewards_returns_data);
    runner.run_test("Last Restart Slot Valid", test_last_restart_slot_valid);
    runner.run_test("Last Restart Slot Null", test_last_restart_slot_null);
    runner.run_test("All Sysvars Callable", test_all_sysvars_callable);
    runner.run_test("Compute Unit Costs Defined", test_compute_unit_costs_defined);
    
    runner.print_summary();
    
    return runner.all_passed() ? 0 : 1;
}
