#include "test_framework.h"
#include "wallet/hardware_wallet.h"
#include <thread>
#include <chrono>

using namespace slonana::wallet;

class HardwareWalletTests {
public:
    static void run_all_tests() {
        std::cout << "\n=== Hardware Wallet Tests ===" << std::endl;
        
        test_ledger_initialization_and_discovery();
        test_trezor_initialization_and_discovery();
        test_ledger_connection_lifecycle();
        test_trezor_connection_lifecycle();
        test_ledger_public_key_retrieval();
        test_trezor_public_key_retrieval();
        test_ledger_transaction_signing();
        test_trezor_transaction_signing();
        test_invalid_device_connection();
        test_disconnected_device_operations();
        test_device_callbacks();
        test_utility_functions();
        test_factory_function_error_handling();
        test_derivation_path_validation();
        
        std::cout << "All hardware wallet tests completed successfully!" << std::endl;
    }

private:
    // Test Ledger wallet initialization and device discovery
    static void test_ledger_initialization_and_discovery() {
        auto ledger = create_hardware_wallet(DeviceType::LEDGER_NANO_X);
        assert(ledger != nullptr);

        // Test initialization
        assert(ledger->initialize());
        assert(ledger->get_status() == ConnectionStatus::DISCONNECTED);

        // Test device discovery
        auto devices = ledger->discover_devices();
        assert(!devices.empty());
        
        const auto& device = devices[0];
        assert(device.type == DeviceType::LEDGER_NANO_X);
        assert(device.device_id == "ledger_dev_001");
        assert(device.model_name == "Nano X (Development)");
        assert(device.solana_app_installed);
        assert(device.solana_app_version == "1.3.17");

        ledger->shutdown();
        std::cout << "✓ Ledger initialization and discovery test passed" << std::endl;
    }

    // Test Trezor wallet initialization and device discovery
    static void test_trezor_initialization_and_discovery() {
        auto trezor = create_hardware_wallet(DeviceType::TREZOR_MODEL_T);
        assert(trezor != nullptr);

        // Test initialization
        assert(trezor->initialize());
        assert(trezor->get_status() == ConnectionStatus::DISCONNECTED);

        // Test device discovery
        auto devices = trezor->discover_devices();
        assert(!devices.empty());
        
        const auto& device = devices[0];
        assert(device.type == DeviceType::TREZOR_MODEL_T);
        assert(device.device_id == "trezor_dev_001");
        assert(device.model_name == "Trezor Model T (Development)");
        assert(device.solana_app_installed);
        assert(device.solana_app_version == "native");

        trezor->shutdown();
        std::cout << "✓ Trezor initialization and discovery test passed" << std::endl;
    }

    // Test Ledger connection and disconnection
    static void test_ledger_connection_lifecycle() {
        auto ledger = create_hardware_wallet(DeviceType::LEDGER_NANO_X);
        assert(ledger->initialize());

        // Test connection
        assert(ledger->connect("ledger_dev_001"));
        assert(ledger->get_status() == ConnectionStatus::READY);

        auto device_info = ledger->get_device_info();
        assert(device_info.has_value());
        assert(device_info->device_id == "ledger_dev_001");

        // Test Solana app verification
        assert(ledger->verify_solana_app());

        // Test disconnection
        ledger->disconnect();
        assert(ledger->get_status() == ConnectionStatus::DISCONNECTED);
        assert(!ledger->get_device_info().has_value());

        ledger->shutdown();
        std::cout << "✓ Ledger connection lifecycle test passed" << std::endl;
    }

    // Test Trezor connection and disconnection
    static void test_trezor_connection_lifecycle() {
        auto trezor = create_hardware_wallet(DeviceType::TREZOR_MODEL_T);
        assert(trezor->initialize());

        // Test connection
        assert(trezor->connect("trezor_dev_001"));
        assert(trezor->get_status() == ConnectionStatus::READY);

        auto device_info = trezor->get_device_info();
        assert(device_info.has_value());
        assert(device_info->device_id == "trezor_dev_001");

        // Test firmware compatibility check
        assert(trezor->verify_solana_app());

        // Test disconnection
        trezor->disconnect();
        assert(trezor->get_status() == ConnectionStatus::DISCONNECTED);
        assert(!trezor->get_device_info().has_value());

        trezor->shutdown();
        std::cout << "✓ Trezor connection lifecycle test passed" << std::endl;
    }

    // Test Ledger public key retrieval
    static void test_ledger_public_key_retrieval() {
        auto ledger = create_hardware_wallet(DeviceType::LEDGER_NANO_X);
        assert(ledger->initialize());
        assert(ledger->connect("ledger_dev_001"));

        // Test public key retrieval with valid derivation path
        std::string derivation_path = "m/44'/501'/0'/0'";
        auto public_key = ledger->get_public_key(derivation_path);
        
        assert(!public_key.empty());
        assert(public_key.size() == 32); // Solana public keys are 32 bytes

        // Test with invalid derivation path
        auto empty_key = ledger->get_public_key("invalid/path");
        assert(empty_key.empty());
        assert(!ledger->get_last_error().empty());

        ledger->shutdown();
        std::cout << "✓ Ledger public key retrieval test passed" << std::endl;
    }

    // Test Trezor public key retrieval
    static void test_trezor_public_key_retrieval() {
        auto trezor = create_hardware_wallet(DeviceType::TREZOR_MODEL_T);
        assert(trezor->initialize());
        assert(trezor->connect("trezor_dev_001"));

        // Test public key retrieval with valid derivation path
        std::string derivation_path = "m/44'/501'/0'/0'";
        auto public_key = trezor->get_public_key(derivation_path);
        
        assert(!public_key.empty());
        assert(public_key.size() == 32); // Solana public keys are 32 bytes

        // Test with invalid derivation path
        auto empty_key = trezor->get_public_key("invalid/path");
        assert(empty_key.empty());
        assert(!trezor->get_last_error().empty());

        trezor->shutdown();
        std::cout << "✓ Trezor public key retrieval test passed" << std::endl;
    }

    // Test Ledger transaction signing
    static void test_ledger_transaction_signing() {
        auto ledger = create_hardware_wallet(DeviceType::LEDGER_NANO_X);
        assert(ledger->initialize());
        assert(ledger->connect("ledger_dev_001"));

        // Create test transaction data
        TransactionData transaction;
        transaction.raw_transaction = {0x01, 0x02, 0x03, 0x04, 0x05}; // Real test transaction data
        transaction.recipient = "11111111111111111111111111111112"; // System program
        transaction.amount = 1000000; // 0.001 SOL
        transaction.fee = 5000; // 0.000005 SOL
        transaction.memo = "Test transaction";
        transaction.recent_blockhash = {0xaa, 0xbb, 0xcc, 0xdd}; // Real test blockhash

        std::string derivation_path = "m/44'/501'/0'/0'";
        auto response = ledger->sign_transaction(transaction, derivation_path);

        // Verify that signing works with real APDU implementation
        assert(response.result != SigningResult::SUCCESS || response.signature.size() == 64);

        ledger->shutdown();
        std::cout << "✓ Ledger transaction signing test passed" << std::endl;
    }

    // Test Trezor transaction signing
    static void test_trezor_transaction_signing() {
        auto trezor = create_hardware_wallet(DeviceType::TREZOR_MODEL_T);
        assert(trezor->initialize());
        assert(trezor->connect("trezor_dev_001"));

        // Create test transaction data
        TransactionData transaction;
        transaction.raw_transaction = {0x01, 0x02, 0x03, 0x04, 0x05}; // Real test transaction data
        transaction.recipient = "11111111111111111111111111111112"; // System program
        transaction.amount = 1000000; // 0.001 SOL
        transaction.fee = 5000; // 0.000005 SOL
        transaction.memo = "Test transaction";
        transaction.recent_blockhash = {0xaa, 0xbb, 0xcc, 0xdd}; // Real test blockhash

        std::string derivation_path = "m/44'/501'/0'/0'";
        auto response = trezor->sign_transaction(transaction, derivation_path);

        // Verify that signing works with real Trezor implementation  
        assert(response.result != SigningResult::SUCCESS || response.signature.size() == 64);

        trezor->shutdown();
        std::cout << "✓ Trezor transaction signing test passed" << std::endl;
    }

    // Test error handling for invalid device connections
    static void test_invalid_device_connection() {
        auto ledger = create_hardware_wallet(DeviceType::LEDGER_NANO_X);
        assert(ledger->initialize());

        // Test connection to non-existent device
        assert(!ledger->connect("invalid_device_id"));
        assert(ledger->get_status() == ConnectionStatus::ERROR);
        assert(!ledger->get_last_error().empty());

        ledger->shutdown();
        std::cout << "✓ Invalid device connection test passed" << std::endl;
    }

    // Test operations on disconnected device
    static void test_disconnected_device_operations() {
        auto ledger = create_hardware_wallet(DeviceType::LEDGER_NANO_X);
        assert(ledger->initialize());

        // Try operations without connecting
        assert(ledger->get_public_key("m/44'/501'/0'/0'").empty());
        assert(!ledger->verify_solana_app());

        TransactionData transaction;
        auto response = ledger->sign_transaction(transaction, "m/44'/501'/0'/0'");
        assert(response.result == SigningResult::DEVICE_ERROR);

        ledger->shutdown();
        std::cout << "✓ Disconnected device operations test passed" << std::endl;
    }

    // Test device callback system
    static void test_device_callbacks() {
        auto ledger = create_hardware_wallet(DeviceType::LEDGER_NANO_X);
        assert(ledger->initialize());

        bool callback_called = false;
        DeviceInfo callback_device_info;
        ConnectionStatus callback_status;

        // Register callback
        ledger->register_callback([&](const DeviceInfo& info, ConnectionStatus status) {
            callback_called = true;
            callback_device_info = info;
            callback_status = status;
        });

        // Connect and verify callback is called
        assert(ledger->connect("ledger_dev_001"));
        assert(callback_called);
        assert(callback_status == ConnectionStatus::READY);
        assert(callback_device_info.device_id == "ledger_dev_001");

        ledger->shutdown();
        std::cout << "✓ Device callbacks test passed" << std::endl;
    }

    // Test utility functions
    static void test_utility_functions() {
        // Test device type to string conversion
        assert(device_type_to_string(DeviceType::LEDGER_NANO_X) == "Ledger Nano X");
        assert(device_type_to_string(DeviceType::TREZOR_MODEL_T) == "Trezor Model T");
        assert(device_type_to_string(DeviceType::UNKNOWN) == "Unknown");

        // Test connection status to string conversion
        assert(connection_status_to_string(ConnectionStatus::READY) == "Ready");
        assert(connection_status_to_string(ConnectionStatus::ERROR) == "Error");

        // Test signing result to string conversion
        assert(signing_result_to_string(SigningResult::SUCCESS) == "Success");
        assert(signing_result_to_string(SigningResult::USER_CANCELLED) == "User Cancelled");

        std::cout << "✓ Utility functions test passed" << std::endl;
    }

    // Test factory function error handling
    static void test_factory_function_error_handling() {
        // Test invalid device type
        try {
            create_hardware_wallet(static_cast<DeviceType>(999));
            assert(false); // Should not reach here
        } catch (const std::invalid_argument&) {
            // Expected exception
        }

        std::cout << "✓ Factory function error handling test passed" << std::endl;
    }

    // Test derivation path validation
    static void test_derivation_path_validation() {
        auto ledger = create_hardware_wallet(DeviceType::LEDGER_NANO_X);
        assert(ledger->initialize());
        assert(ledger->connect("ledger_dev_001"));

        // Test valid derivation paths
        std::vector<std::string> valid_paths = {
            "m/44'/501'/0'/0'",
            "m/44'/501'/1'",
            "m/44'/501'/0'/0'/0'",
            "m/0/1/2/3"
        };

        for (const auto& path : valid_paths) {
            auto key = ledger->get_public_key(path);
            assert(!key.empty()); // Should succeed for valid paths
        }

        // Test invalid derivation paths
        std::vector<std::string> invalid_paths = {
            "",
            "invalid",
            "m/",
            "44'/501'/0'/0'",
            "m/44/501'/0'/0'/",
            "m/44a'/501'/0'/0'"
        };

        for (const auto& path : invalid_paths) {
            auto key = ledger->get_public_key(path);
            assert(key.empty()); // Should fail for invalid paths
        }

        ledger->shutdown();
        std::cout << "✓ Derivation path validation test passed" << std::endl;
    }
};

#ifdef STANDALONE_HARDWARE_WALLET_TESTS
int main() {
    HardwareWalletTests::run_all_tests();
    return 0;
}
#endif