#include "wallet/wallet_config.h"
#include <algorithm>
#include <map>
#include <regex>
#include <set>

namespace slonana {
namespace wallet {

HardwareWalletConfig WalletConfigManager::create_default() {
  HardwareWalletConfig config;

  // Enable hardware wallets by default
  config.enabled = true;
  config.auto_discovery = true;
  config.discovery_interval = std::chrono::milliseconds(5000);
  config.connection_timeout = std::chrono::milliseconds(10000);
  config.max_retry_attempts = 3;
  config.auto_reconnect = true;

  // Default security settings (conservative)
  config.default_security.require_device_confirmation = true;
  config.default_security.enable_display_verification = true;
  config.default_security.max_signing_attempts = 3;
  config.default_security.signing_timeout = std::chrono::milliseconds(30000);
  config.default_security.allow_cached_sessions = false;
  config.default_security.session_timeout = std::chrono::minutes(10);

  // Logging and monitoring enabled
  config.enable_logging = true;
  config.log_level = "INFO";
  config.enable_metrics = true;

  // RPC integration enabled, but don't require hardware for all signing
  config.integrate_with_rpc = true;
  config.require_hardware_for_signing = false;

  return config;
}

std::string
WalletConfigManager::validate_config(const HardwareWalletConfig &config) {
  // Basic validation
  if (config.discovery_interval.count() < 1000) {
    return "Discovery interval must be at least 1000ms";
  }

  if (config.connection_timeout.count() < 5000) {
    return "Connection timeout must be at least 5000ms";
  }

  if (config.max_retry_attempts == 0 || config.max_retry_attempts > 10) {
    return "Max retry attempts must be between 1 and 10";
  }

  // Security validation
  if (config.default_security.max_signing_attempts == 0 ||
      config.default_security.max_signing_attempts > 10) {
    return "Max signing attempts must be between 1 and 10";
  }

  if (config.default_security.signing_timeout.count() < 5000) {
    return "Signing timeout must be at least 5000ms";
  }

  // Device validation
  std::set<std::string> device_ids;
  for (const auto &device : config.devices) {
    if (device.device_id.empty()) {
      return "Device ID cannot be empty";
    }

    if (device_ids.count(device.device_id)) {
      return "Duplicate device ID: " + device.device_id;
    }
    device_ids.insert(device.device_id);

    if (!ConfigValidator::validate_derivation_path(
            device.default_derivation_path)) {
      return "Invalid derivation path for device " + device.device_id + ": " +
             device.default_derivation_path;
    }
  }

  // Log level validation
  static const std::set<std::string> valid_log_levels = {
      "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL"};
  if (valid_log_levels.count(config.log_level) == 0) {
    return "Invalid log level: " + config.log_level;
  }

  return ""; // Valid
}

HardwareWalletConfig
WalletConfigManager::merge_configs(const HardwareWalletConfig &base,
                                   const HardwareWalletConfig &override) {
  HardwareWalletConfig result = base;

  // Override basic settings if they differ from defaults
  result.enabled = override.enabled;
  result.auto_discovery = override.auto_discovery;
  result.discovery_interval = override.discovery_interval;
  result.connection_timeout = override.connection_timeout;
  result.max_retry_attempts = override.max_retry_attempts;
  result.auto_reconnect = override.auto_reconnect;

  // Override security settings
  result.default_security = override.default_security;

  // Merge device configurations (override devices replace base devices with
  // same ID)
  std::map<std::string, DeviceConfig> merged_devices;

  // Add base devices
  for (const auto &device : base.devices) {
    merged_devices[device.device_id] = device;
  }

  // Override with new devices
  for (const auto &device : override.devices) {
    merged_devices[device.device_id] = device;
  }

  // Convert back to vector
  result.devices.clear();
  for (const auto &pair : merged_devices) {
    result.devices.push_back(pair.second);
  }

  // Override remaining settings
  result.enable_logging = override.enable_logging;
  result.log_level = override.log_level;
  result.enable_metrics = override.enable_metrics;
  result.integrate_with_rpc = override.integrate_with_rpc;
  result.require_hardware_for_signing = override.require_hardware_for_signing;
  result.trusted_device_ids = override.trusted_device_ids;

  return result;
}

std::optional<DeviceConfig>
WalletConfigManager::get_device_config(const HardwareWalletConfig &config,
                                       const std::string &device_id) {
  for (const auto &device : config.devices) {
    if (device.device_id == device_id) {
      return device;
    }
  }
  return std::nullopt;
}

void WalletConfigManager::set_device_config(HardwareWalletConfig &config,
                                            const DeviceConfig &device_config) {
  // Find existing device or add new one
  for (auto &device : config.devices) {
    if (device.device_id == device_config.device_id) {
      device = device_config;
      return;
    }
  }

  // Device not found, add new one
  config.devices.push_back(device_config);
}

bool WalletConfigManager::remove_device_config(HardwareWalletConfig &config,
                                               const std::string &device_id) {
  auto it = std::remove_if(config.devices.begin(), config.devices.end(),
                           [&device_id](const DeviceConfig &device) {
                             return device.device_id == device_id;
                           });

  if (it != config.devices.end()) {
    config.devices.erase(it, config.devices.end());
    return true;
  }

  return false;
}

// ConfigValidator implementation
ValidationResult ConfigValidator::validate(const HardwareWalletConfig &config) {
  ValidationResult result;

  // Validate timeouts
  if (config.discovery_interval.count() < 1000) {
    result.error = ConfigError::INVALID_TIMEOUT_VALUE;
    result.message = "Discovery interval must be at least 1000ms";
    result.field_path = "discovery_interval";
    return result;
  }

  if (config.connection_timeout.count() < 5000) {
    result.error = ConfigError::INVALID_TIMEOUT_VALUE;
    result.message = "Connection timeout must be at least 5000ms";
    result.field_path = "connection_timeout";
    return result;
  }

  // Validate retry attempts
  if (config.max_retry_attempts == 0 || config.max_retry_attempts > 10) {
    result.error = ConfigError::INVALID_VALUE_RANGE;
    result.message = "Max retry attempts must be between 1 and 10";
    result.field_path = "max_retry_attempts";
    return result;
  }

  // Validate security config
  auto sec_result = validate_security(config.default_security);
  if (!sec_result.is_valid()) {
    sec_result.field_path = "default_security." + sec_result.field_path;
    return sec_result;
  }

  // Validate devices
  std::set<std::string> device_ids;
  for (size_t i = 0; i < config.devices.size(); ++i) {
    const auto &device = config.devices[i];

    if (device.device_id.empty()) {
      result.error = ConfigError::MISSING_REQUIRED_FIELD;
      result.message = "Device ID cannot be empty";
      result.field_path = "devices[" + std::to_string(i) + "].device_id";
      return result;
    }

    if (device_ids.count(device.device_id)) {
      result.error = ConfigError::DUPLICATE_DEVICE_ID;
      result.message = "Duplicate device ID: " + device.device_id;
      result.field_path = "devices[" + std::to_string(i) + "].device_id";
      return result;
    }
    device_ids.insert(device.device_id);

    auto device_result = validate_device(device);
    if (!device_result.is_valid()) {
      device_result.field_path =
          "devices[" + std::to_string(i) + "]." + device_result.field_path;
      return device_result;
    }
  }

  return result; // Valid
}

ValidationResult
ConfigValidator::validate_security(const SecurityConfig &security) {
  ValidationResult result;

  if (security.max_signing_attempts == 0 ||
      security.max_signing_attempts > 10) {
    result.error = ConfigError::INVALID_VALUE_RANGE;
    result.message = "Max signing attempts must be between 1 and 10";
    result.field_path = "max_signing_attempts";
    return result;
  }

  if (security.signing_timeout.count() < 5000) {
    result.error = ConfigError::INVALID_TIMEOUT_VALUE;
    result.message = "Signing timeout must be at least 5000ms";
    result.field_path = "signing_timeout";
    return result;
  }

  if (security.session_timeout.count() < 1) {
    result.error = ConfigError::INVALID_TIMEOUT_VALUE;
    result.message = "Session timeout must be at least 1 minute";
    result.field_path = "session_timeout";
    return result;
  }

  return result; // Valid
}

ValidationResult ConfigValidator::validate_device(const DeviceConfig &device) {
  ValidationResult result;

  if (device.device_id.empty()) {
    result.error = ConfigError::MISSING_REQUIRED_FIELD;
    result.message = "Device ID is required";
    result.field_path = "device_id";
    return result;
  }

  if (!validate_derivation_path(device.default_derivation_path)) {
    result.error = ConfigError::INVALID_DERIVATION_PATH;
    result.message =
        "Invalid derivation path: " + device.default_derivation_path;
    result.field_path = "default_derivation_path";
    return result;
  }

  auto sec_result = validate_security(device.security);
  if (!sec_result.is_valid()) {
    sec_result.field_path = "security." + sec_result.field_path;
    return sec_result;
  }

  return result; // Valid
}

bool ConfigValidator::validate_derivation_path(const std::string &path) {
  // Validate BIP44 derivation path format
  std::regex path_regex(R"(^m(\/\d+'?)*$)");
  return std::regex_match(path, path_regex);
}

std::string ConfigValidator::get_error_message(ConfigError error) {
  switch (error) {
  case ConfigError::NONE:
    return "No error";
  case ConfigError::INVALID_JSON:
    return "Invalid JSON format";
  case ConfigError::MISSING_REQUIRED_FIELD:
    return "Missing required field";
  case ConfigError::INVALID_VALUE_RANGE:
    return "Value out of valid range";
  case ConfigError::INVALID_DERIVATION_PATH:
    return "Invalid derivation path format";
  case ConfigError::DUPLICATE_DEVICE_ID:
    return "Duplicate device identifier";
  case ConfigError::INVALID_TIMEOUT_VALUE:
    return "Invalid timeout value";
  case ConfigError::UNSUPPORTED_SECURITY_SETTING:
    return "Unsupported security setting";
  default:
    return "Unknown error";
  }
}

} // namespace wallet
} // namespace slonana