#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace slonana {
namespace wallet {

/**
 * @brief Hardware wallet device types supported by the system
 */
enum class DeviceType {
  LEDGER_NANO_S,
  LEDGER_NANO_S_PLUS,
  LEDGER_NANO_X,
  TREZOR_MODEL_T,
  TREZOR_MODEL_ONE,
  UNKNOWN
};

/**
 * @brief Hardware wallet connection status
 */
enum class ConnectionStatus {
  DISCONNECTED,
  CONNECTING,
  CONNECTED,
  READY,
  BUSY,
  ERROR
};

/**
 * @brief Transaction signing result codes
 */
enum class SigningResult {
  SUCCESS,
  USER_CANCELLED,
  DEVICE_ERROR,
  INVALID_TRANSACTION,
  COMMUNICATION_ERROR,
  TIMEOUT
};

/**
 * @brief Hardware wallet device information
 */
struct DeviceInfo {
  DeviceType type;
  std::string device_id;
  std::string firmware_version;
  std::string model_name;
  std::string serial_number;
  bool solana_app_installed;
  std::string solana_app_version;
};

/**
 * @brief Transaction data for hardware wallet signing
 */
struct TransactionData {
  std::vector<uint8_t> raw_transaction;
  std::string recipient;
  uint64_t amount;
  uint64_t fee;
  std::string memo;
  std::vector<uint8_t> recent_blockhash;
};

/**
 * @brief Hardware wallet signing response
 */
struct SigningResponse {
  SigningResult result;
  std::vector<uint8_t> signature;
  std::string error_message;
};

/**
 * @brief Callback function type for device connection events
 */
using DeviceCallback =
    std::function<void(const DeviceInfo &, ConnectionStatus)>;

/**
 * @brief Abstract base class for hardware wallet implementations
 *
 * This interface defines the contract for all hardware wallet integrations.
 * It provides secure transaction signing without exposing private keys.
 *
 * Security Requirements:
 * - Private keys never leave the hardware device
 * - All transactions must be verified on device display
 * - Secure communication channel with device
 * - Proper error handling and user feedback
 */
class IHardwareWallet {
public:
  virtual ~IHardwareWallet() = default;

  /**
   * @brief Initialize the hardware wallet interface
   * @return true if initialization successful, false otherwise
   */
  virtual bool initialize() = 0;

  /**
   * @brief Cleanup and shutdown the hardware wallet interface
   */
  virtual void shutdown() = 0;

  /**
   * @brief Discover and enumerate connected hardware wallets
   * @return vector of discovered device information
   */
  virtual std::vector<DeviceInfo> discover_devices() = 0;

  /**
   * @brief Connect to a specific hardware wallet device
   * @param device_id unique identifier of the device to connect to
   * @return true if connection successful, false otherwise
   */
  virtual bool connect(const std::string &device_id) = 0;

  /**
   * @brief Disconnect from the currently connected device
   */
  virtual void disconnect() = 0;

  /**
   * @brief Get current connection status
   * @return current connection status
   */
  virtual ConnectionStatus get_status() const = 0;

  /**
   * @brief Get information about the currently connected device
   * @return device information if connected, nullopt otherwise
   */
  virtual std::optional<DeviceInfo> get_device_info() const = 0;

  /**
   * @brief Get the public key from the hardware wallet
   * @param derivation_path BIP44 derivation path for the key
   * @return public key bytes if successful, empty vector otherwise
   */
  virtual std::vector<uint8_t>
  get_public_key(const std::string &derivation_path) = 0;

  /**
   * @brief Sign a transaction using the hardware wallet
   * @param transaction transaction data to sign
   * @param derivation_path key derivation path to use for signing
   * @return signing response with result and signature
   */
  virtual SigningResponse
  sign_transaction(const TransactionData &transaction,
                   const std::string &derivation_path) = 0;

  /**
   * @brief Verify that the Solana app is installed and ready
   * @return true if Solana app is available, false otherwise
   */
  virtual bool verify_solana_app() = 0;

  /**
   * @brief Register callback for device connection events
   * @param callback function to call on device events
   */
  virtual void register_callback(DeviceCallback callback) = 0;

  /**
   * @brief Get the last error message from the device
   * @return error message string
   */
  virtual std::string get_last_error() const = 0;
};

/**
 * @brief Factory function to create hardware wallet instances
 * @param type type of hardware wallet to create
 * @return unique pointer to hardware wallet instance
 */
std::unique_ptr<IHardwareWallet> create_hardware_wallet(DeviceType type);

/**
 * @brief Get string representation of device type
 * @param type device type enum value
 * @return human-readable string name
 */
std::string device_type_to_string(DeviceType type);

/**
 * @brief Get string representation of connection status
 * @param status connection status enum value
 * @return human-readable string description
 */
std::string connection_status_to_string(ConnectionStatus status);

/**
 * @brief Get string representation of signing result
 * @param result signing result enum value
 * @return human-readable string description
 */
std::string signing_result_to_string(SigningResult result);

} // namespace wallet
} // namespace slonana