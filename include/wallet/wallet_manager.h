#pragma once

#include "hardware_wallet.h"
#include <unordered_map>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

namespace slonana {
namespace wallet {

/**
 * @brief Hardware wallet discovery and connection management
 * 
 * This class manages the lifecycle of hardware wallet connections,
 * including device discovery, connection pooling, and automatic reconnection.
 */
class HardwareWalletManager {
public:
    /**
     * @brief Configuration for hardware wallet manager
     */
    struct Config {
        bool enable_auto_discovery = true;
        std::chrono::milliseconds discovery_interval{5000};
        std::chrono::milliseconds connection_timeout{10000};
        uint32_t max_retry_attempts = 3;
        bool auto_reconnect = true;
        bool enable_logging = true;
        bool enable_mock_devices = true; // Disable for testing scenarios requiring no devices
    };

    /**
     * @brief Device connection state tracking
     */
    struct DeviceState {
        DeviceInfo info;
        std::unique_ptr<IHardwareWallet> wallet;
        ConnectionStatus status;
        std::chrono::steady_clock::time_point last_seen;
        uint32_t retry_count;
    };

    explicit HardwareWalletManager(const Config& config);
    HardwareWalletManager(); // Default constructor
    ~HardwareWalletManager();

    // Non-copyable, non-movable
    HardwareWalletManager(const HardwareWalletManager&) = delete;
    HardwareWalletManager& operator=(const HardwareWalletManager&) = delete;
    HardwareWalletManager(HardwareWalletManager&&) = delete;
    HardwareWalletManager& operator=(HardwareWalletManager&&) = delete;

    /**
     * @brief Initialize the hardware wallet manager
     * @return true if initialization successful
     */
    bool initialize();

    /**
     * @brief Shutdown the hardware wallet manager
     */
    void shutdown();

    /**
     * @brief Start automatic device discovery
     */
    void start_discovery();

    /**
     * @brief Stop automatic device discovery
     */
    void stop_discovery();

    /**
     * @brief Manually trigger device discovery
     * @return vector of discovered devices
     */
    std::vector<DeviceInfo> discover_devices();

    /**
     * @brief Get list of currently available devices
     * @return vector of available device information
     */
    std::vector<DeviceInfo> get_available_devices() const;

    /**
     * @brief Connect to a specific hardware wallet
     * @param device_id unique identifier of the device
     * @return true if connection successful
     */
    bool connect_device(const std::string& device_id);

    /**
     * @brief Disconnect from a specific device
     * @param device_id unique identifier of the device
     */
    void disconnect_device(const std::string& device_id);

    /**
     * @brief Disconnect from all devices
     */
    void disconnect_all();

    /**
     * @brief Get hardware wallet instance for a connected device
     * @param device_id unique identifier of the device
     * @return pointer to hardware wallet instance, nullptr if not connected
     */
    IHardwareWallet* get_wallet(const std::string& device_id);

    /**
     * @brief Get the primary (first available) hardware wallet
     * @return pointer to hardware wallet instance, nullptr if none available
     */
    IHardwareWallet* get_primary_wallet();

    /**
     * @brief Check if a device is connected and ready
     * @param device_id unique identifier of the device
     * @return true if device is connected and ready
     */
    bool is_device_ready(const std::string& device_id) const;

    /**
     * @brief Get connection status for a device
     * @param device_id unique identifier of the device
     * @return connection status
     */
    ConnectionStatus get_device_status(const std::string& device_id) const;

    /**
     * @brief Register callback for device events
     * @param callback function to call on device events
     */
    void register_device_callback(DeviceCallback callback);

    /**
     * @brief Get current manager configuration
     * @return configuration structure
     */
    const Config& get_config() const { return config_; }

    /**
     * @brief Update manager configuration
     * @param config new configuration
     */
    void update_config(const Config& config);

    /**
     * @brief Get statistics about managed devices
     * @return map of device ID to connection statistics
     */
    std::unordered_map<std::string, uint32_t> get_connection_stats() const;

private:
    /**
     * @brief Background thread function for device discovery
     */
    void discovery_thread_func();

    /**
     * @brief Handle device connection events
     */
    void handle_device_event(const DeviceInfo& device, ConnectionStatus status);

    /**
     * @brief Attempt to reconnect to a device
     */
    bool attempt_reconnect(const std::string& device_id);

    /**
     * @brief Create hardware wallet instance for device type
     */
    std::unique_ptr<IHardwareWallet> create_wallet_for_device(const DeviceInfo& device);

    /**
     * @brief Update device state
     */
    void update_device_state(const std::string& device_id, ConnectionStatus status);

    Config config_;
    mutable std::mutex devices_mutex_;
    std::unordered_map<std::string, DeviceState> devices_;
    
    std::atomic<bool> discovery_running_{false};
    std::thread discovery_thread_;
    
    std::vector<DeviceCallback> callbacks_;
    mutable std::mutex callbacks_mutex_;
    
    std::atomic<bool> initialized_{false};
    std::atomic<bool> shutting_down_{false};
};

/**
 * @brief Global hardware wallet manager instance
 * 
 * Provides a singleton interface for accessing hardware wallets
 * throughout the application.
 */
class GlobalWalletManager {
public:
    /**
     * @brief Get the global hardware wallet manager instance
     * @return reference to the global manager
     */
    static HardwareWalletManager& instance();

    /**
     * @brief Initialize the global manager with configuration
     * @param config manager configuration
     * @return true if initialization successful
     */
    static bool initialize(const HardwareWalletManager::Config& config);

    /**
     * @brief Shutdown the global manager
     */
    static void shutdown();

private:
    static std::unique_ptr<HardwareWalletManager> instance_;
    static std::mutex instance_mutex_;
};

} // namespace wallet
} // namespace slonana