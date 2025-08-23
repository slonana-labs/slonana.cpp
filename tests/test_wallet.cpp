#include "test_framework.h"
#include "wallet/hardware_wallet.h"
#include "wallet/wallet_manager.h"
#include <memory>

using namespace slonana::wallet;

void test_device_type_to_string() {
    ASSERT_EQ(std::string("Ledger Nano S"), device_type_to_string(DeviceType::LEDGER_NANO_S));
    ASSERT_EQ(std::string("Ledger Nano X"), device_type_to_string(DeviceType::LEDGER_NANO_X));
    ASSERT_EQ(std::string("Trezor Model T"), device_type_to_string(DeviceType::TREZOR_MODEL_T));
    ASSERT_EQ(std::string("Unknown"), device_type_to_string(DeviceType::UNKNOWN));
}

void test_connection_status_to_string() {
    ASSERT_EQ(std::string("Disconnected"), connection_status_to_string(ConnectionStatus::DISCONNECTED));
    ASSERT_EQ(std::string("Connected"), connection_status_to_string(ConnectionStatus::CONNECTED));
    ASSERT_EQ(std::string("Ready"), connection_status_to_string(ConnectionStatus::READY));
    ASSERT_EQ(std::string("Error"), connection_status_to_string(ConnectionStatus::ERROR));
}

void test_signing_result_to_string() {
    ASSERT_EQ(std::string("Success"), signing_result_to_string(SigningResult::SUCCESS));
    ASSERT_EQ(std::string("User Cancelled"), signing_result_to_string(SigningResult::USER_CANCELLED));
    ASSERT_EQ(std::string("Device Error"), signing_result_to_string(SigningResult::DEVICE_ERROR));
    ASSERT_EQ(std::string("Timeout"), signing_result_to_string(SigningResult::TIMEOUT));
}

void test_hardware_wallet_factory() {
    // Test factory creation for different device types
    auto ledger_wallet = create_hardware_wallet(DeviceType::LEDGER_NANO_S);
    ASSERT_TRUE(ledger_wallet.get() != nullptr);
    
    auto trezor_wallet = create_hardware_wallet(DeviceType::TREZOR_MODEL_T);
    ASSERT_TRUE(trezor_wallet.get() != nullptr);
    
    // Test invalid device type throws exception
    ASSERT_THROWS(create_hardware_wallet(DeviceType::UNKNOWN), std::invalid_argument);
}

void test_wallet_manager_initialization() {
    HardwareWalletManager::Config config;
    config.enable_auto_discovery = false; // Disable for testing
    
    HardwareWalletManager manager(config);
    
    ASSERT_TRUE(manager.initialize());
    ASSERT_EQ(static_cast<size_t>(0), manager.get_available_devices().size()); // No devices discovered yet
    
    manager.shutdown();
}

void test_wallet_manager_configuration() {
    HardwareWalletManager::Config config;
    config.enable_auto_discovery = false;
    config.auto_reconnect = true;
    config.max_retry_attempts = 5;
    
    HardwareWalletManager manager(config);
    
    const auto& actual_config = manager.get_config();
    ASSERT_EQ(false, actual_config.enable_auto_discovery);
    ASSERT_EQ(true, actual_config.auto_reconnect);
    ASSERT_EQ(static_cast<uint32_t>(5), actual_config.max_retry_attempts);
}

void test_device_discovery() {
    HardwareWalletManager::Config config;
    config.enable_auto_discovery = false;
    
    HardwareWalletManager manager(config);
    manager.initialize();
    
    // Test manual discovery (should return empty list since no real devices)
    auto devices = manager.discover_devices();
    ASSERT_EQ(static_cast<size_t>(0), devices.size());
    
    manager.shutdown();
}

void test_device_connection_failure() {
    HardwareWalletManager::Config config;
    config.enable_auto_discovery = false;
    
    HardwareWalletManager manager(config);
    manager.initialize();
    
    // Test connecting to non-existent device
    bool connected = manager.connect_device("non_existent_device");
    ASSERT_EQ(false, connected);
    
    manager.shutdown();
}

void test_global_wallet_manager() {
    // Test singleton pattern
    HardwareWalletManager::Config config;
    config.enable_auto_discovery = false;
    
    ASSERT_TRUE(GlobalWalletManager::initialize(config));
    
    auto& manager1 = GlobalWalletManager::instance();
    auto& manager2 = GlobalWalletManager::instance();
    
    // Should be the same instance
    ASSERT_EQ(&manager1, &manager2);
    
    GlobalWalletManager::shutdown();
}

// Test runner function
void run_wallet_tests() {
    TestRunner runner;
    
    runner.run_test("device_type_to_string", test_device_type_to_string);
    runner.run_test("connection_status_to_string", test_connection_status_to_string);
    runner.run_test("signing_result_to_string", test_signing_result_to_string);
    runner.run_test("hardware_wallet_factory", test_hardware_wallet_factory);
    runner.run_test("wallet_manager_initialization", test_wallet_manager_initialization);
    runner.run_test("wallet_manager_configuration", test_wallet_manager_configuration);
    runner.run_test("device_discovery", test_device_discovery);
    runner.run_test("device_connection_failure", test_device_connection_failure);
    runner.run_test("global_wallet_manager", test_global_wallet_manager);
    
    runner.print_summary();
}

#ifdef STANDALONE_WALLET_TESTS
// Main function for standalone test execution
int main() {
    run_wallet_tests();
    return 0;
}
#endif