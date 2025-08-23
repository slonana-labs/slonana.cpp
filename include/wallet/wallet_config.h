#pragma once

#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <nlohmann/json.hpp>

namespace slonana {
namespace wallet {

/**
 * @brief Hardware wallet security configuration
 */
struct SecurityConfig {
    bool require_device_confirmation = true;
    bool enable_display_verification = true;
    uint32_t max_signing_attempts = 3;
    std::chrono::milliseconds signing_timeout{30000};
    bool allow_cached_sessions = false;
    std::chrono::minutes session_timeout{10};
};

/**
 * @brief Device-specific configuration
 */
struct DeviceConfig {
    std::string device_id;
    std::string alias;
    bool enabled = true;
    bool auto_connect = false;
    std::string default_derivation_path = "m/44'/501'/0'";
    SecurityConfig security;
    std::optional<std::string> pin_cache_policy;
};

/**
 * @brief Hardware wallet subsystem configuration
 */
struct HardwareWalletConfig {
    bool enabled = false;
    bool auto_discovery = true;
    std::chrono::milliseconds discovery_interval{5000};
    std::chrono::milliseconds connection_timeout{10000};
    uint32_t max_retry_attempts = 3;
    bool auto_reconnect = true;
    
    // Default security settings
    SecurityConfig default_security;
    
    // Device-specific configurations
    std::vector<DeviceConfig> devices;
    
    // Logging and monitoring
    bool enable_logging = true;
    std::string log_level = "INFO";
    bool enable_metrics = true;
    
    // Integration settings
    bool integrate_with_rpc = true;
    bool require_hardware_for_signing = false;
    std::vector<std::string> trusted_device_ids;
};

/**
 * @brief Hardware wallet configuration loader and manager
 */
class WalletConfigManager {
public:
    /**
     * @brief Load configuration from JSON file
     * @param config_path path to configuration file
     * @return loaded configuration, or nullopt if failed
     */
    static std::optional<HardwareWalletConfig> load_from_file(const std::string& config_path);

    /**
     * @brief Save configuration to JSON file
     * @param config configuration to save
     * @param config_path path to configuration file
     * @return true if save successful
     */
    static bool save_to_file(const HardwareWalletConfig& config, const std::string& config_path);

    /**
     * @brief Load configuration from JSON object
     * @param json JSON object containing configuration
     * @return loaded configuration, or nullopt if failed
     */
    static std::optional<HardwareWalletConfig> load_from_json(const nlohmann::json& json);

    /**
     * @brief Convert configuration to JSON object
     * @param config configuration to convert
     * @return JSON representation
     */
    static nlohmann::json to_json(const HardwareWalletConfig& config);

    /**
     * @brief Create default configuration
     * @return default hardware wallet configuration
     */
    static HardwareWalletConfig create_default();

    /**
     * @brief Validate configuration
     * @param config configuration to validate
     * @return validation error message, or empty string if valid
     */
    static std::string validate_config(const HardwareWalletConfig& config);

    /**
     * @brief Merge two configurations (second overrides first)
     * @param base base configuration
     * @param override configuration to merge
     * @return merged configuration
     */
    static HardwareWalletConfig merge_configs(
        const HardwareWalletConfig& base,
        const HardwareWalletConfig& override
    );

    /**
     * @brief Get device configuration by ID
     * @param config wallet configuration
     * @param device_id device identifier
     * @return device configuration if found, nullopt otherwise
     */
    static std::optional<DeviceConfig> get_device_config(
        const HardwareWalletConfig& config,
        const std::string& device_id
    );

    /**
     * @brief Add or update device configuration
     * @param config wallet configuration to modify
     * @param device_config device configuration to add/update
     */
    static void set_device_config(
        HardwareWalletConfig& config,
        const DeviceConfig& device_config
    );

    /**
     * @brief Remove device configuration
     * @param config wallet configuration to modify
     * @param device_id device identifier to remove
     * @return true if device was removed
     */
    static bool remove_device_config(
        HardwareWalletConfig& config,
        const std::string& device_id
    );
};

/**
 * @brief Configuration validation errors
 */
enum class ConfigError {
    NONE,
    INVALID_JSON,
    MISSING_REQUIRED_FIELD,
    INVALID_VALUE_RANGE,
    INVALID_DERIVATION_PATH,
    DUPLICATE_DEVICE_ID,
    INVALID_TIMEOUT_VALUE,
    UNSUPPORTED_SECURITY_SETTING
};

/**
 * @brief Configuration validation result
 */
struct ValidationResult {
    ConfigError error = ConfigError::NONE;
    std::string message;
    std::string field_path;
    
    bool is_valid() const { return error == ConfigError::NONE; }
};

/**
 * @brief Detailed configuration validator
 */
class ConfigValidator {
public:
    /**
     * @brief Validate complete hardware wallet configuration
     * @param config configuration to validate
     * @return validation result
     */
    static ValidationResult validate(const HardwareWalletConfig& config);

    /**
     * @brief Validate security configuration
     * @param security security config to validate
     * @return validation result
     */
    static ValidationResult validate_security(const SecurityConfig& security);

    /**
     * @brief Validate device configuration
     * @param device device config to validate
     * @return validation result
     */
    static ValidationResult validate_device(const DeviceConfig& device);

    /**
     * @brief Validate derivation path format
     * @param path derivation path to validate
     * @return true if valid BIP44 path
     */
    static bool validate_derivation_path(const std::string& path);

    /**
     * @brief Get error message for configuration error
     * @param error error code
     * @return human-readable error description
     */
    static std::string get_error_message(ConfigError error);
};

} // namespace wallet
} // namespace slonana

// JSON serialization support
namespace nlohmann {

template<>
struct adl_serializer<slonana::wallet::SecurityConfig> {
    static void to_json(json& j, const slonana::wallet::SecurityConfig& config);
    static void from_json(const json& j, slonana::wallet::SecurityConfig& config);
};

template<>
struct adl_serializer<slonana::wallet::DeviceConfig> {
    static void to_json(json& j, const slonana::wallet::DeviceConfig& config);
    static void from_json(const json& j, slonana::wallet::DeviceConfig& config);
};

template<>
struct adl_serializer<slonana::wallet::HardwareWalletConfig> {
    static void to_json(json& j, const slonana::wallet::HardwareWalletConfig& config);
    static void from_json(const json& j, slonana::wallet::HardwareWalletConfig& config);
};

} // namespace nlohmann