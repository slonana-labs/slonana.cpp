#include "wallet/wallet_manager.h"
#include "wallet/hardware_wallet.h"
#include "wallet/wallet_config.h"
#include <algorithm>
#include <iostream>
#include <thread>

namespace slonana {
namespace wallet {

// Global manager instance
std::unique_ptr<HardwareWalletManager> GlobalWalletManager::instance_ = nullptr;
std::mutex GlobalWalletManager::instance_mutex_;

HardwareWalletManager::HardwareWalletManager(const Config &config)
    : config_(config) {}

HardwareWalletManager::HardwareWalletManager() : config_() {}

HardwareWalletManager::~HardwareWalletManager() { shutdown(); }

bool HardwareWalletManager::initialize() {
  if (initialized_.load()) {
    return true;
  }

  // Initialize hardware wallet subsystems
  // This would typically initialize USB/HID libraries

  initialized_.store(true);

  if (config_.enable_auto_discovery) {
    start_discovery();
  }

  return true;
}

void HardwareWalletManager::shutdown() {
  if (shutting_down_.load()) {
    return;
  }

  shutting_down_.store(true);

  stop_discovery();
  disconnect_all();

  initialized_.store(false);
}

void HardwareWalletManager::start_discovery() {
  if (discovery_running_.load()) {
    return;
  }

  discovery_running_.store(true);
  discovery_thread_ =
      std::thread(&HardwareWalletManager::discovery_thread_func, this);
}

void HardwareWalletManager::stop_discovery() {
  if (!discovery_running_.load()) {
    return;
  }

  discovery_running_.store(false);

  if (discovery_thread_.joinable()) {
    discovery_thread_.join();
  }
}

std::vector<DeviceInfo> HardwareWalletManager::discover_devices() {
  std::vector<DeviceInfo> discovered_devices;

  // Skip device discovery if mock devices are disabled and real SDKs are not
  // available
  if (!config_.enable_mock_devices) {
#ifndef USE_LEDGER_SDK
#ifndef USE_TREZOR_SDK
    // No real SDKs available and mock devices disabled - return empty list
    return discovered_devices;
#endif
#endif
  }

  // Discover Ledger devices
  try {
    auto ledger = create_hardware_wallet(DeviceType::LEDGER_NANO_X);
    if (ledger && ledger->initialize()) {
      auto ledger_devices = ledger->discover_devices();

      // Filter out mock devices if they're disabled
      if (!config_.enable_mock_devices) {
        ledger_devices.erase(
            std::remove_if(ledger_devices.begin(), ledger_devices.end(),
                           [](const DeviceInfo &device) {
                             return device.device_id.find("mock") !=
                                    std::string::npos;
                           }),
            ledger_devices.end());
      }

      discovered_devices.insert(discovered_devices.end(),
                                ledger_devices.begin(), ledger_devices.end());
      ledger->shutdown();
    }
  } catch (const std::exception &e) {
    // Log error but continue with other device types
    if (config_.enable_logging) {
      std::cerr << "Ledger discovery error: " << e.what() << std::endl;
    }
  }

  // Discover Trezor devices
  try {
    auto trezor = create_hardware_wallet(DeviceType::TREZOR_MODEL_T);
    if (trezor && trezor->initialize()) {
      auto trezor_devices = trezor->discover_devices();

      // Filter out mock devices if they're disabled
      if (!config_.enable_mock_devices) {
        trezor_devices.erase(
            std::remove_if(trezor_devices.begin(), trezor_devices.end(),
                           [](const DeviceInfo &device) {
                             return device.device_id.find("mock") !=
                                    std::string::npos;
                           }),
            trezor_devices.end());
      }

      discovered_devices.insert(discovered_devices.end(),
                                trezor_devices.begin(), trezor_devices.end());
      trezor->shutdown();
    }
  } catch (const std::exception &e) {
    // Log error but continue
    if (config_.enable_logging) {
      std::cerr << "Trezor discovery error: " << e.what() << std::endl;
    }
  }

  return discovered_devices;
}

std::vector<DeviceInfo> HardwareWalletManager::get_available_devices() const {
  std::lock_guard<std::mutex> lock(devices_mutex_);

  std::vector<DeviceInfo> available_devices;
  for (const auto &[device_id, state] : devices_) {
    if (state.status != ConnectionStatus::DISCONNECTED) {
      available_devices.push_back(state.info);
    }
  }

  return available_devices;
}

bool HardwareWalletManager::connect_device(const std::string &device_id) {
  std::lock_guard<std::mutex> lock(devices_mutex_);

  auto it = devices_.find(device_id);
  if (it == devices_.end()) {
    return false;
  }

  auto &state = it->second;
  if (state.status == ConnectionStatus::CONNECTED ||
      state.status == ConnectionStatus::READY) {
    return true;
  }

  // Create wallet instance if not exists
  if (!state.wallet) {
    state.wallet = create_wallet_for_device(state.info);
    if (!state.wallet) {
      return false;
    }
  }

  // Attempt connection
  state.status = ConnectionStatus::CONNECTING;
  bool success = state.wallet->connect(device_id);

  if (success) {
    state.status = ConnectionStatus::CONNECTED;
    state.retry_count = 0;
    handle_device_event(state.info, ConnectionStatus::CONNECTED);
  } else {
    state.status = ConnectionStatus::ERROR;
    state.retry_count++;
    handle_device_event(state.info, ConnectionStatus::ERROR);
  }

  return success;
}

void HardwareWalletManager::disconnect_device(const std::string &device_id) {
  std::lock_guard<std::mutex> lock(devices_mutex_);

  auto it = devices_.find(device_id);
  if (it != devices_.end() && it->second.wallet) {
    it->second.wallet->disconnect();
    it->second.status = ConnectionStatus::DISCONNECTED;
    handle_device_event(it->second.info, ConnectionStatus::DISCONNECTED);
  }
}

void HardwareWalletManager::disconnect_all() {
  std::lock_guard<std::mutex> lock(devices_mutex_);

  for (auto &[device_id, state] : devices_) {
    if (state.wallet) {
      state.wallet->disconnect();
      state.status = ConnectionStatus::DISCONNECTED;
    }
  }
}

IHardwareWallet *
HardwareWalletManager::get_wallet(const std::string &device_id) {
  std::lock_guard<std::mutex> lock(devices_mutex_);

  auto it = devices_.find(device_id);
  if (it != devices_.end() && it->second.status == ConnectionStatus::READY) {
    return it->second.wallet.get();
  }

  return nullptr;
}

IHardwareWallet *HardwareWalletManager::get_primary_wallet() {
  std::lock_guard<std::mutex> lock(devices_mutex_);

  for (const auto &[device_id, state] : devices_) {
    if (state.status == ConnectionStatus::READY) {
      return state.wallet.get();
    }
  }

  return nullptr;
}

bool HardwareWalletManager::is_device_ready(
    const std::string &device_id) const {
  return get_device_status(device_id) == ConnectionStatus::READY;
}

ConnectionStatus
HardwareWalletManager::get_device_status(const std::string &device_id) const {
  std::lock_guard<std::mutex> lock(devices_mutex_);

  auto it = devices_.find(device_id);
  if (it != devices_.end()) {
    return it->second.status;
  }

  return ConnectionStatus::DISCONNECTED;
}

void HardwareWalletManager::register_device_callback(DeviceCallback callback) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);
  callbacks_.push_back(std::move(callback));
}

void HardwareWalletManager::update_config(const Config &config) {
  config_ = config;

  // Apply configuration changes
  if (config_.enable_auto_discovery && !discovery_running_.load()) {
    start_discovery();
  } else if (!config_.enable_auto_discovery && discovery_running_.load()) {
    stop_discovery();
  }
}

std::unordered_map<std::string, uint32_t>
HardwareWalletManager::get_connection_stats() const {
  std::lock_guard<std::mutex> lock(devices_mutex_);

  std::unordered_map<std::string, uint32_t> stats;
  for (const auto &[device_id, state] : devices_) {
    stats[device_id] = state.retry_count;
  }

  return stats;
}

void HardwareWalletManager::discovery_thread_func() {
  while (discovery_running_.load() && !shutting_down_.load()) {
    try {
      auto discovered = discover_devices();

      std::lock_guard<std::mutex> lock(devices_mutex_);

      // Update existing devices and add new ones
      for (const auto &device : discovered) {
        auto it = devices_.find(device.device_id);
        if (it == devices_.end()) {
          // New device discovered
          DeviceState state;
          state.info = device;
          state.status = ConnectionStatus::DISCONNECTED;
          state.last_seen = std::chrono::steady_clock::now();
          state.retry_count = 0;

          devices_[device.device_id] = std::move(state);
          handle_device_event(device, ConnectionStatus::DISCONNECTED);
        } else {
          // Update existing device
          it->second.last_seen = std::chrono::steady_clock::now();
          it->second.info = device;
        }
      }

      // Auto-connect if configured
      for (auto &[device_id, state] : devices_) {
        if (config_.auto_reconnect &&
            state.status == ConnectionStatus::DISCONNECTED &&
            state.retry_count < config_.max_retry_attempts) {
          attempt_reconnect(device_id);
        }
      }

    } catch (const std::exception &e) {
      // Log error but continue discovery
    }

    std::this_thread::sleep_for(config_.discovery_interval);
  }
}

void HardwareWalletManager::handle_device_event(const DeviceInfo &device,
                                                ConnectionStatus status) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);

  for (const auto &callback : callbacks_) {
    try {
      callback(device, status);
    } catch (const std::exception &e) {
      // Log callback error but continue
    }
  }
}

bool HardwareWalletManager::attempt_reconnect(const std::string &device_id) {
  // This would be called without holding the mutex since connect_device
  // acquires it
  return connect_device(device_id);
}

std::unique_ptr<IHardwareWallet>
HardwareWalletManager::create_wallet_for_device(const DeviceInfo &device) {
  return create_hardware_wallet(device.type);
}

void HardwareWalletManager::update_device_state(const std::string &device_id,
                                                ConnectionStatus status) {
  std::lock_guard<std::mutex> lock(devices_mutex_);

  auto it = devices_.find(device_id);
  if (it != devices_.end()) {
    it->second.status = status;
    it->second.last_seen = std::chrono::steady_clock::now();
  }
}

// GlobalWalletManager implementation
HardwareWalletManager &GlobalWalletManager::instance() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) {
    instance_ = std::make_unique<HardwareWalletManager>();
  }
  return *instance_;
}

bool GlobalWalletManager::initialize(
    const HardwareWalletManager::Config &config) {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) {
    instance_ = std::make_unique<HardwareWalletManager>(config);
  }
  return instance_->initialize();
}

void GlobalWalletManager::shutdown() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (instance_) {
    instance_->shutdown();
    instance_.reset();
  }
}

} // namespace wallet
} // namespace slonana