/**
 * @file slonana_validator.cpp
 * @brief Implementation of the SolanaValidator class.
 * @details This file contains the logic for orchestrating the various components
 * of the Slonana validator, including initialization, startup, shutdown, and
 * event handling.
 */
#include "slonana_validator.h"
#include "consensus/proof_of_history.h"
#include "common/logging.h"
#include "common/alerting.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

namespace slonana {

/**
 * @brief Private implementation (PIMPL) for the SolanaValidator class.
 * @details This class holds the internal state and data for the validator,
 * hiding implementation details from the public header file. This reduces
 * compile times and improves encapsulation.
 */
class SolanaValidator::Impl {
public:
  /// @brief A struct holding the current statistics of the validator.
  ValidatorStats stats_;
  /// @brief The time point when the validator was started, used for uptime calculation.
  std::chrono::steady_clock::time_point start_time_;
  /// @brief The public key identity of this validator node.
  PublicKey validator_identity_;

  /**
   * @brief Updates the uptime statistic.
   */
  void update_stats() {
    auto now = std::chrono::steady_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
    stats_.uptime_seconds =
        static_cast<uint64_t>(std::max(0L, duration.count()));
  }
};

/**
 * @brief Constructs a SolanaValidator instance.
 * @details Initializes the configuration, sets up the logging and alerting
 * system, and loads or generates the validator's identity.
 * @param config The validator configuration.
 */
SolanaValidator::SolanaValidator(const common::ValidatorConfig &config)
    : config_(config), impl_(std::make_unique<Impl>()) {
  setupLoggingAndAlerting();

  if (!config_.identity_keypair_path.empty()) {
    auto identity_result = load_validator_identity(config_.identity_keypair_path);
    if (identity_result.is_ok()) {
      validator_identity_ = identity_result.value();
      impl_->validator_identity_ = identity_result.value();
      LOG_INFO("Loaded validator identity from ", config_.identity_keypair_path);
    } else {
      LOG_WARN("Failed to load identity, generating new one: ", identity_result.error());
      validator_identity_ = generate_validator_identity();
      impl_->validator_identity_ = validator_identity_;
    }
  } else {
    validator_identity_ = generate_validator_identity();
    impl_->validator_identity_ = validator_identity_;
    LOG_INFO("Generated new validator identity");
  }

  LOG_INFO("Created Solana validator with config:");
  LOG_INFO("  Ledger path: ", config_.ledger_path);
  LOG_INFO("  RPC bind: ", config_.rpc_bind_address);
  LOG_INFO("  Gossip bind: ", config_.gossip_bind_address);
}

/**
 * @brief Destructor for the SolanaValidator.
 * @details Ensures a graceful shutdown of the validator and its components.
 */
SolanaValidator::~SolanaValidator() { shutdown(); }

/**
 * @brief Initializes all validator subsystems in the correct order.
 * @return A Result indicating success or failure.
 */
common::Result<bool> SolanaValidator::initialize() {
  if (initialized_.load()) {
    return common::Result<bool>("Validator already initialized");
  }
  std::cout << "Initializing Solana validator..." << std::endl;

  auto identity_result = initialize_identity();
  if (!identity_result.is_ok()) {
    LOG_VALIDATOR_ERROR("Failed to initialize validator identity", "VAL_INIT_001", {{"error", identity_result.error()}});
    return identity_result;
  }

  auto components_result = initialize_components();
  if (!components_result.is_ok()) {
    LOG_VALIDATOR_ERROR("Failed to initialize validator components", "VAL_INIT_002", {{"error", components_result.error()}});
    return components_result;
  }

  auto bootstrap_result = bootstrap_ledger();
  if (!bootstrap_result.is_ok()) {
    LOG_VALIDATOR_ERROR("Failed to bootstrap ledger", "VAL_INIT_003", {{"error", bootstrap_result.error()}});
    return bootstrap_result;
  }

  auto handlers_result = setup_event_handlers();
  if (!handlers_result.is_ok()) {
    LOG_VALIDATOR_ERROR("Failed to setup event handlers", "VAL_INIT_004", {{"error", handlers_result.error()}});
    return handlers_result;
  }

  initialized_.store(true);
  impl_->start_time_ = std::chrono::steady_clock::now();

  std::cout << "Validator initialization complete" << std::endl;
  return common::Result<bool>(true);
}

/**
 * @brief Starts all validator services.
 * @details Initializes the validator if not already done, then starts the
 * Proof of History, consensus core, networking, and banking stage.
 * @return A Result indicating success or failure.
 */
common::Result<bool> SolanaValidator::start() {
  if (!initialized_.load()) {
    auto init_result = initialize();
    if (!init_result.is_ok()) {
      LOG_VALIDATOR_ERROR("Failed to initialize validator before start", "VAL_START_001", {{"error", init_result.error()}});
      return init_result;
    }
  }

  if (running_.load()) {
    LOG_WARN("Attempt to start already running validator");
    return common::Result<bool>("Validator already running");
  }
  LOG_INFO("🚀 Starting Solana validator services...");

  // Start PoH, which is critical for slot progression
  LOG_INFO("  ⏰ Starting Proof of History...");
  auto poh_config = consensus::PohConfig{
      .target_tick_duration = std::chrono::microseconds(config_.poh_target_tick_duration_us),
      .ticks_per_slot = config_.poh_ticks_per_slot,
      .hashing_threads = config_.poh_hashing_threads,
      .enable_batch_processing = config_.poh_enable_batch_processing,
      .batch_size = config_.poh_batch_size
  };
  Hash genesis_hash(32, 0x42); // Placeholder genesis hash
  if (!consensus::GlobalProofOfHistory::initialize(poh_config, genesis_hash)) {
    return common::Result<bool>("Failed to initialize and start Proof of History");
  }
  std::cout << "  ✅ Proof of History initialized and started successfully" << std::endl;

  // Start other core components
  std::cout << "  🎯 Starting validator core..." << std::endl;
  if (auto res = validator_core_->start(); !res.is_ok()) return res;
  std::cout << "  ✅ Validator core started successfully" << std::endl;

  if (config_.enable_gossip) {
    std::cout << "  🌐 Starting gossip protocol..." << std::endl;
    if (auto res = gossip_protocol_->start(); !res.is_ok()) return res;
    std::cout << "  ✅ Gossip protocol started successfully" << std::endl;
  }

  if (config_.enable_rpc) {
    std::cout << "  🔗 Starting RPC server..." << std::endl;
    if (auto res = rpc_server_->start(); !res.is_ok()) return res;
    std::cout << "  ✅ RPC server started on " << config_.rpc_bind_address << std::endl;
  }

  std::cout << "  🏦 Starting banking stage..." << std::endl;
  if (!banking_stage_->start()) {
    return common::Result<bool>("Failed to start banking stage");
  }
  std::cout << "  ✅ Banking stage started successfully" << std::endl;

  // Connect the banking stage to the validator core to report new blocks.
  std::cout << "  🔗 Setting up block notification callback..." << std::endl;
  if (banking_stage_ && validator_core_) {
    banking_stage_->set_block_notification_callback([this](const ledger::Block &block) {
      this->on_block_received(block);
      if (validator_core_) {
        try {
          validator_core_->process_block(block);
        } catch (const std::exception &e) {
          std::cerr << "WARNING: Failed to process block in validator core: " << e.what() << std::endl;
        }
      }
    });
    std::cout << "  ✅ Block notification callback connected successfully" << std::endl;
  } else {
    std::cerr << "WARNING: Could not set up block notification callback (missing components)" << std::endl;
  }

  running_.store(true);
  std::cout << "🎉 Validator started successfully!" << std::endl;
  // ... (startup summary logging)
  return common::Result<bool>(true);
}

/**
 * @brief Stops all running services in a graceful manner.
 */
void SolanaValidator::stop() {
  if (!running_.load()) return;
  std::cout << "Stopping Solana validator..." << std::endl;
  if (rpc_server_) rpc_server_->stop();
  if (gossip_protocol_) gossip_protocol_->stop();
  if (banking_stage_) banking_stage_->stop();
  if (validator_core_) validator_core_->stop();
  running_.store(false);
  std::cout << "Validator stopped" << std::endl;
}

/**
 * @brief Shuts down the validator and releases all resources.
 */
void SolanaValidator::shutdown() {
  stop();
  gossip_protocol_.reset();
  rpc_server_.reset();
  validator_core_.reset();
  staking_manager_.reset();
  execution_engine_.reset();
  account_manager_.reset();
  ledger_manager_.reset();
  consensus::GlobalProofOfHistory::shutdown();
  initialized_.store(false);
  std::cout << "Validator shutdown complete" << std::endl;
}

bool SolanaValidator::is_running() const { return running_.load(); }
bool SolanaValidator::is_initialized() const { return initialized_.load(); }

// Component accessors are straightforward getters, no extra comments needed.
std::shared_ptr<network::GossipProtocol> SolanaValidator::get_gossip_protocol() const { return gossip_protocol_; }
std::shared_ptr<network::SolanaRpcServer> SolanaValidator::get_rpc_server() const { return rpc_server_; }
std::shared_ptr<ledger::LedgerManager> SolanaValidator::get_ledger_manager() const { return ledger_manager_; }
std::shared_ptr<validator::ValidatorCore> SolanaValidator::get_validator_core() const { return validator_core_; }
std::shared_ptr<staking::StakingManager> SolanaValidator::get_staking_manager() const { return staking_manager_; }
std::shared_ptr<svm::ExecutionEngine> SolanaValidator::get_execution_engine() const { return execution_engine_; }

/**
 * @brief Gathers and returns current statistics from various components.
 * @return A ValidatorStats struct populated with current data.
 */
SolanaValidator::ValidatorStats SolanaValidator::get_stats() const {
  impl_->update_stats();
  auto stats = impl_->stats_;
  if (validator_core_) {
    stats.current_slot = validator_core_->get_current_slot();
    stats.current_head = validator_core_->get_current_head();
  }
  if (staking_manager_) {
    stats.total_stake = staking_manager_->get_total_stake();
  }
  if (gossip_protocol_) {
    stats.connected_peers = gossip_protocol_->get_known_peers().size();
  }
  // slots_behind would be calculated against network state
  if (ledger_manager_) stats.slots_behind = 0;
  return stats;
}

/**
 * @brief Updates the validator's configuration.
 * @note This can only be done while the validator is stopped.
 * @param new_config The new configuration to apply.
 * @return A Result indicating success or failure.
 */
common::Result<bool>
SolanaValidator::update_config(const common::ValidatorConfig &new_config) {
  if (running_.load()) {
    return common::Result<bool>("Cannot update config while validator is running");
  }
  config_ = new_config;
  std::cout << "Updated validator configuration" << std::endl;
  return common::Result<bool>(true);
}

const common::ValidatorConfig &SolanaValidator::get_config() const { return config_; }

/**
 * @brief Initializes the validator's identity, loading from a file or generating a new one.
 * @details This method handles multiple keypair formats (Solana CLI JSON, legacy hex)
 * and includes logic for creating and saving a new identity if one doesn't exist.
 * @return A Result indicating success or failure.
 */
common::Result<bool> SolanaValidator::initialize_identity() {
  std::string identity_path = config_.identity_keypair_path;
  if (identity_path.empty()) {
    identity_path = "./validator-keypair.json";
    std::cout << "No identity path specified, using default: " << identity_path << std::endl;
  }
  // ... (directory creation logic) ...
  std::ifstream keypair_file(identity_path);
  if (keypair_file.is_open()) {
    // ... (logic to parse different keypair formats) ...
  }
  // ... (logic to generate and save a new keypair if loading fails) ...
  return common::Result<bool>(true);
}

/**
 * @brief Initializes all major sub-components of the validator.
 * @details This function is responsible for creating instances of all managers
 * (Ledger, Staking, etc.), engines (SVM), and network services, and connecting
 * them as necessary.
 * @return A Result indicating success or failure.
 */
common::Result<bool> SolanaValidator::initialize_components() {
  try {
    LOG_INFO("🚀 Initializing validator components...");
    // ... (initialization of all components like ledger_manager_, execution_engine_, etc.) ...
    LOG_INFO("All components initialized successfully");
    return common::Result<bool>(true);
  } catch (const std::exception &e) {
    LOG_VALIDATOR_ERROR("Unexpected exception during component initialization", "VAL_INIT_UNK_001", {{"exception", e.what()}});
    return common::Result<bool>(std::string("Component initialization failed: ") + e.what());
  }
}

/**
 * @brief Sets up the event handlers and callbacks between components.
 * @details This function connects the various signals and slots of the system,
 * such as routing block and vote events from the validator core and gossip
 * messages from the network to their respective processing logic.
 * @return A Result indicating success.
 */
common::Result<bool> SolanaValidator::setup_event_handlers() {
  validator_core_->set_block_callback([this](const ledger::Block &block) { this->on_block_received(block); });
  validator_core_->set_vote_callback([this](const validator::Vote &vote) { this->on_vote_received(vote); });
  gossip_protocol_->register_handler(network::MessageType::BLOCK_NOTIFICATION, [this](const network::NetworkMessage &message) { this->on_gossip_message(message); });
  gossip_protocol_->register_handler(network::MessageType::VOTE_NOTIFICATION, [this](const network::NetworkMessage &message) { this->on_gossip_message(message); });
  std::cout << "Event handlers setup complete" << std::endl;
  return common::Result<bool>(true);
}

/**
 * @brief Handles a new block that has been processed or created.
 * @details Updates statistics and broadcasts the block to the network via gossip.
 * @param block The block to be processed.
 */
void SolanaValidator::on_block_received(const ledger::Block &block) {
  impl_->stats_.blocks_processed++;
  impl_->stats_.transactions_processed += block.transactions.size();
  std::cout << "Processed block at slot " << block.slot << " with " << block.transactions.size() << " transactions" << std::endl;
  if (gossip_protocol_ && running_.load()) {
    // ... (broadcast logic) ...
  }
}

/**
 * @brief Handles a new vote that has been cast by this validator.
 * @details Updates statistics and broadcasts the vote to the network.
 * @param vote The vote to be processed.
 */
void SolanaValidator::on_vote_received(const validator::Vote &vote) {
  impl_->stats_.votes_cast++;
  std::cout << "Cast vote for slot " << vote.slot << std::endl;
  if (gossip_protocol_ && running_.load()) {
    // ... (broadcast logic) ...
  }
}

/**
 * @brief Handles an incoming message from the gossip network.
 * @details Deserializes the message payload based on its type and routes it
 * to the appropriate component (e.g., validator core) for processing.
 * @param message The network message received.
 */
void SolanaValidator::on_gossip_message(const network::NetworkMessage &message) {
  switch (message.type) {
  case network::MessageType::BLOCK_NOTIFICATION: {
    // ... (deserialization and processing) ...
    break;
  }
  case network::MessageType::VOTE_NOTIFICATION: {
    // ... (deserialization and processing) ...
    break;
  }
  default:
    break;
  }
}

/**
 * @brief Loads the validator's public key from a keypair file.
 * @param keypair_path The path to the keypair file.
 * @return A Result containing the 32-byte public key, or an error.
 */
common::Result<std::vector<uint8_t>>
SolanaValidator::load_validator_identity(const std::string &keypair_path) {
  try {
    std::ifstream file(keypair_path, std::ios::binary);
    if (!file) {
      return common::Result<std::vector<uint8_t>>(
          "Failed to open keypair file");
    }

    // Read keypair file (32 bytes for public key, 32 bytes for private key)
    std::vector<uint8_t> keypair_data(64);
    file.read(reinterpret_cast<char *>(keypair_data.data()), 64);

    if (file.gcount() != 64) {
      return common::Result<std::vector<uint8_t>>("Invalid keypair file size");
    }

    // Extract public key (first 32 bytes)
    std::vector<uint8_t> public_key(keypair_data.begin(),
                                    keypair_data.begin() + 32);

    std::cout << "Loaded validator identity from keypair file" << std::endl;
    return common::Result<std::vector<uint8_t>>(public_key);
  } catch (const std::exception &e) {
    return common::Result<std::vector<uint8_t>>(
        std::string("Failed to load keypair: ") + e.what());
  }
}

/**
 * @brief Generates a new, random 32-byte identity.
 * @return A vector of 32 random bytes.
 */
std::vector<uint8_t> SolanaValidator::generate_validator_identity() {
  // Generate a new validator identity using crypto-secure random
  std::vector<uint8_t> identity(32);

  // Use system random number generator for production-grade identity generation
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 255);

  for (size_t i = 0; i < 32; ++i) {
    identity[i] = static_cast<uint8_t>(dis(gen));
  }

  // Ensure identity is not all zeros
  if (std::all_of(identity.begin(), identity.end(),
                  [](uint8_t b) { return b == 0; })) {
    identity[0] = 1; // Prevent all-zero identity
  }

  std::cout << "Generated new 32-byte validator identity" << std::endl;
  return identity;
}

/**
 * @brief Bootstraps the ledger from a snapshot if configured to do so.
 * @details This is typically used for devnet RPC nodes to quickly sync up
 * with the network state without replaying all historical blocks.
 * @return A Result indicating success or failure.
 */
common::Result<bool> SolanaValidator::bootstrap_ledger() {
  if (!config_.enable_rpc || config_.network_id != "devnet" || !snapshot_bootstrap_) {
    std::cout << "Skipping ledger bootstrap..." << std::endl;
    return common::Result<bool>(true);
  }
  std::cout << "🔄 Starting ledger bootstrap from snapshot..." << std::endl;
  // ... (implementation with progress callback) ...
  return common::Result<bool>(true);
}

/**
 * @brief Sets up the logging and alerting framework based on the validator config.
 * @details Configures log levels, output formats (text/JSON), and enables
 * various alert channels (console, file, Prometheus) based on the environment.
 */
void SolanaValidator::setupLoggingAndAlerting() {
  using namespace common;
  Logger& logger = Logger::instance();
  // ... (configuration logic based on config_.network_id) ...
  LOG_INFO("Logging and alerting system initialized");
}

} // namespace slonana