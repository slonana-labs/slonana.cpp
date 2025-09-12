#include "../tests/test_framework.h"
#include "genesis/config.h"
#include "genesis/manager.h"
#include "network/discovery.h"
#include <iostream>

void test_genesis_config_creation() {
  std::cout << "Testing genesis configuration creation..." << std::endl;

  // Test mainnet config creation
  auto mainnet_config = slonana::genesis::GenesisManager::create_network_config(
      slonana::genesis::NetworkType::MAINNET);

  ASSERT_EQ("mainnet", mainnet_config.network_id);
  ASSERT_EQ(101, mainnet_config.chain_id);
  ASSERT_EQ(1000000000ULL, mainnet_config.economics.total_supply);
  ASSERT_TRUE(mainnet_config.economics.min_validator_stake > 0);

  // Test testnet config creation
  auto testnet_config = slonana::genesis::GenesisManager::create_network_config(
      slonana::genesis::NetworkType::TESTNET);

  ASSERT_EQ("testnet", testnet_config.network_id);
  ASSERT_EQ(102, testnet_config.chain_id);

  std::cout << "✓ Genesis configuration creation test passed" << std::endl;
}

void test_genesis_validation() {
  std::cout << "Testing genesis configuration validation..." << std::endl;

  auto config = slonana::genesis::GenesisManager::create_network_config(
      slonana::genesis::NetworkType::MAINNET);

  // Test valid configuration
  auto validation_result =
      slonana::genesis::GenesisManager::validate_genesis_config(config);
  ASSERT_TRUE(validation_result.is_ok());

  // Test invalid configuration (invalid allocation)
  config.economics.foundation_allocation = 0.5;
  config.economics.team_allocation = 0.5;
  config.economics.community_allocation = 0.5;
  config.economics.validator_allocation = 0.5; // Total > 1.0

  validation_result =
      slonana::genesis::GenesisManager::validate_genesis_config(config);
  ASSERT_FALSE(validation_result.is_ok());

  std::cout << "✓ Genesis validation test passed" << std::endl;
}

void test_genesis_hash_computation() {
  std::cout << "Testing genesis hash computation..." << std::endl;

  auto config1 = slonana::genesis::GenesisManager::create_network_config(
      slonana::genesis::NetworkType::MAINNET);
  auto config2 = slonana::genesis::GenesisManager::create_network_config(
      slonana::genesis::NetworkType::MAINNET);

  // Same configs should produce same hash
  auto hash1 = slonana::genesis::GenesisManager::compute_genesis_hash(config1);
  auto hash2 = slonana::genesis::GenesisManager::compute_genesis_hash(config2);

  ASSERT_EQ(hash1, hash2);
  ASSERT_EQ(32UL, hash1.size()); // SHA256 hash is 32 bytes

  // Different configs should produce different hashes
  config2.economics.total_supply = 2000000000;
  auto hash3 = slonana::genesis::GenesisManager::compute_genesis_hash(config2);

  ASSERT_TRUE(hash1 != hash3);

  std::cout << "✓ Genesis hash computation test passed" << std::endl;
}

void test_mainnet_entrypoints() {
  std::cout << "Testing mainnet entrypoint configuration..." << std::endl;

  auto mainnet_entrypoints =
      slonana::network::MainnetEntrypoints::get_mainnet_entrypoints();
  auto testnet_entrypoints =
      slonana::network::MainnetEntrypoints::get_testnet_entrypoints();

  ASSERT_TRUE(mainnet_entrypoints.size() > 0);
  ASSERT_TRUE(testnet_entrypoints.size() > 0);

  // Check that mainnet has more entrypoints than testnet (for redundancy)
  ASSERT_TRUE(mainnet_entrypoints.size() >= testnet_entrypoints.size());

  // Verify entrypoint format
  for (const auto &entrypoint : mainnet_entrypoints) {
    ASSERT_TRUE(entrypoint.find(":") != std::string::npos); // Should have port
    ASSERT_TRUE(entrypoint.find("slonana.org") !=
                std::string::npos); // Should be our domain
  }

  std::cout << "✓ Mainnet entrypoints test passed" << std::endl;
}

void test_network_type_conversion() {
  std::cout << "Testing network type string conversion..." << std::endl;

  // Test enum to string
  ASSERT_EQ("mainnet", slonana::genesis::GenesisManager::network_type_to_string(
                           slonana::genesis::NetworkType::MAINNET));
  ASSERT_EQ("testnet", slonana::genesis::GenesisManager::network_type_to_string(
                           slonana::genesis::NetworkType::TESTNET));
  ASSERT_EQ("devnet", slonana::genesis::GenesisManager::network_type_to_string(
                          slonana::genesis::NetworkType::DEVNET));

  // Test string to enum (compare as strings since enum can't be directly
  // compared in ASSERT_EQ)
  ASSERT_EQ(
      "mainnet",
      slonana::genesis::GenesisManager::network_type_to_string(
          slonana::genesis::GenesisManager::string_to_network_type("mainnet")));
  ASSERT_EQ(
      "testnet",
      slonana::genesis::GenesisManager::network_type_to_string(
          slonana::genesis::GenesisManager::string_to_network_type("testnet")));
  ASSERT_EQ(
      "devnet",
      slonana::genesis::GenesisManager::network_type_to_string(
          slonana::genesis::GenesisManager::string_to_network_type("devnet")));

  std::cout << "✓ Network type conversion test passed" << std::endl;
}

void test_economic_parameters() {
  std::cout << "Testing economic parameter configuration..." << std::endl;

  auto mainnet_config = slonana::genesis::GenesisManager::create_network_config(
      slonana::genesis::NetworkType::MAINNET);

  // Check mainnet economic parameters
  ASSERT_EQ(1000000000ULL, mainnet_config.economics.total_supply); // 1B tokens
  ASSERT_TRUE(mainnet_config.economics.initial_inflation_rate > 0.0);
  ASSERT_TRUE(mainnet_config.economics.initial_inflation_rate <= 0.1); // <= 10%
  ASSERT_EQ(1000000ULL,
            mainnet_config.economics.min_validator_stake);     // 1M lamports
  ASSERT_EQ(1000ULL, mainnet_config.economics.min_delegation); // 1K lamports

  // Check allocation percentages sum to 1.0
  double total_allocation = mainnet_config.economics.foundation_allocation +
                            mainnet_config.economics.team_allocation +
                            mainnet_config.economics.community_allocation +
                            mainnet_config.economics.validator_allocation;

  ASSERT_TRUE(std::abs(total_allocation - 1.0) < 0.01); // Within 1% tolerance

  std::cout << "✓ Economic parameters test passed" << std::endl;
}

int main() {
  std::cout << "=== Genesis and Mainnet Configuration Tests ===" << std::endl;

  try {
    test_genesis_config_creation();
    test_genesis_validation();
    test_genesis_hash_computation();
    test_mainnet_entrypoints();
    test_network_type_conversion();
    test_economic_parameters();

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "All genesis and mainnet tests PASSED!" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }
}