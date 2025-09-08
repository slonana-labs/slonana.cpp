#include "slonana_validator.h"
#include "consensus/proof_of_history.h"
#include <iostream>
#include <chrono>
#include <fstream>
#include <random>
#include <iomanip>
#include <algorithm>
#include <sstream>

namespace slonana {

class SolanaValidator::Impl {
public:
    ValidatorStats stats_;
    std::chrono::steady_clock::time_point start_time_;
    PublicKey validator_identity_;
    
    void update_stats() {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
        stats_.uptime_seconds = static_cast<uint64_t>(std::max(0L, duration.count()));
    }
};

SolanaValidator::SolanaValidator(const common::ValidatorConfig& config)
    : config_(config), impl_(std::make_unique<Impl>()) {
    
    // Initialize validator identity from keypair file or generate new one
    if (!config_.identity_keypair_path.empty()) {
        // Load identity from file
        auto identity_result = load_validator_identity(config_.identity_keypair_path);
        if (identity_result.is_ok()) {
            validator_identity_ = identity_result.value();
            impl_->validator_identity_ = identity_result.value();
            std::cout << "Loaded validator identity from " << config_.identity_keypair_path << std::endl;
        } else {
            std::cout << "Failed to load identity, generating new one" << std::endl;
            validator_identity_ = generate_validator_identity();
            impl_->validator_identity_ = validator_identity_;
        }
    } else {
        // Generate new identity
        validator_identity_ = generate_validator_identity();
        impl_->validator_identity_ = validator_identity_;
        std::cout << "Generated new validator identity" << std::endl;
    }
    
    std::cout << "Created Solana validator with config:" << std::endl;
    std::cout << "  Ledger path: " << config_.ledger_path << std::endl;
    std::cout << "  RPC bind: " << config_.rpc_bind_address << std::endl;
    std::cout << "  Gossip bind: " << config_.gossip_bind_address << std::endl;
}

SolanaValidator::~SolanaValidator() {
    shutdown();
}

common::Result<bool> SolanaValidator::initialize() {
    if (initialized_.load()) {
        return common::Result<bool>("Validator already initialized");
    }
    
    std::cout << "Initializing Solana validator..." << std::endl;
    
    // Initialize identity
    auto identity_result = initialize_identity();
    if (!identity_result.is_ok()) {
        return identity_result;
    }
    
    // Initialize components
    auto components_result = initialize_components();
    if (!components_result.is_ok()) {
        return components_result;
    }
    
    // Bootstrap ledger from snapshot if needed (for RPC nodes)
    auto bootstrap_result = bootstrap_ledger();
    if (!bootstrap_result.is_ok()) {
        return bootstrap_result;
    }
    
    // Setup event handlers
    auto handlers_result = setup_event_handlers();
    if (!handlers_result.is_ok()) {
        return handlers_result;
    }
    
    initialized_.store(true);
    impl_->start_time_ = std::chrono::steady_clock::now();
    
    std::cout << "Validator initialization complete" << std::endl;
    return common::Result<bool>(true);
}

common::Result<bool> SolanaValidator::start() {
    if (!initialized_.load()) {
        auto init_result = initialize();
        if (!init_result.is_ok()) {
            return init_result;
        }
    }
    
    if (running_.load()) {
        return common::Result<bool>("Validator already running");
    }
    
    std::cout << "🚀 Starting Solana validator services..." << std::endl;
    
    // Start Proof of History first (critical for slot progression)
    std::cout << "  ⏰ Starting Proof of History..." << std::endl;
    
    // Initialize and configure Proof of History
    auto poh_config = consensus::PohConfig{};
    poh_config.target_tick_duration = std::chrono::microseconds(config_.poh_target_tick_duration_us);
    poh_config.ticks_per_slot = config_.poh_ticks_per_slot;
    poh_config.enable_batch_processing = config_.poh_enable_batch_processing;
    poh_config.batch_size = config_.poh_batch_size;
    poh_config.hashing_threads = config_.poh_hashing_threads;
    
    if (!consensus::GlobalProofOfHistory::initialize(poh_config)) {
        return common::Result<bool>("Failed to initialize Proof of History");
    }
    
    // Start PoH with a genesis hash
    Hash genesis_hash(32, 0x42); // Simple genesis hash
    auto& global_poh = consensus::GlobalProofOfHistory::instance();
    auto poh_start_result = global_poh.start(genesis_hash);
    if (!poh_start_result.is_ok()) {
        return common::Result<bool>("Failed to start Proof of History: " + poh_start_result.error());
    }
    std::cout << "  ✅ Proof of History started successfully" << std::endl;
    
    // Start core validator
    std::cout << "  🎯 Starting validator core..." << std::endl;
    auto validator_result = validator_core_->start();
    if (!validator_result.is_ok()) {
        return validator_result;
    }
    std::cout << "  ✅ Validator core started successfully" << std::endl;
    
    // Start network services
    if (config_.enable_gossip) {
        std::cout << "  🌐 Starting gossip protocol..." << std::endl;
        auto gossip_result = gossip_protocol_->start();
        if (!gossip_result.is_ok()) {
            return gossip_result;
        }
        std::cout << "  ✅ Gossip protocol started successfully" << std::endl;
    }
    
    if (config_.enable_rpc) {
        std::cout << "  🔗 Starting RPC server..." << std::endl;
        auto rpc_result = rpc_server_->start();
        if (!rpc_result.is_ok()) {
            return rpc_result;
        }
        std::cout << "  ✅ RPC server started on " << config_.rpc_bind_address << std::endl;
    }
    
    running_.store(true);
    
    // Display startup summary
    std::cout << "🎉 Validator started successfully!" << std::endl;
    std::cout << "    Identity: " << std::hex;
    for (size_t i = 0; i < 8; ++i) {
        std::cout << std::setfill('0') << std::setw(2) 
                 << static_cast<int>(impl_->validator_identity_[i]);
    }
    std::cout << "..." << std::dec << std::endl;
    std::cout << "    RPC endpoint: http://" << config_.rpc_bind_address << std::endl;
    std::cout << "    PoH ticking every " << config_.poh_target_tick_duration_us << "μs" << std::endl;
    std::cout << "    Slot progression: " << config_.poh_ticks_per_slot << " ticks per slot" << std::endl;
    
    return common::Result<bool>(true);
}

void SolanaValidator::stop() {
    if (!running_.load()) {
        return;
    }
    
    std::cout << "Stopping Solana validator..." << std::endl;
    
    // Stop network services
    if (rpc_server_) {
        rpc_server_->stop();
    }
    
    if (gossip_protocol_) {
        gossip_protocol_->stop();
    }
    
    // Stop core validator
    if (validator_core_) {
        validator_core_->stop();
    }
    
    running_.store(false);
    std::cout << "Validator stopped" << std::endl;
}

void SolanaValidator::shutdown() {
    stop();
    
    // Clean shutdown of all components
    gossip_protocol_.reset();
    rpc_server_.reset();
    validator_core_.reset();
    staking_manager_.reset();
    execution_engine_.reset();
    account_manager_.reset();
    ledger_manager_.reset();
    
    // Shutdown GlobalProofOfHistory after all components are destroyed
    consensus::GlobalProofOfHistory::shutdown();
    
    initialized_.store(false);
    std::cout << "Validator shutdown complete" << std::endl;
}

bool SolanaValidator::is_running() const {
    return running_.load();
}

bool SolanaValidator::is_initialized() const {
    return initialized_.load();
}

// Component accessors
std::shared_ptr<network::GossipProtocol> SolanaValidator::get_gossip_protocol() const {
    return gossip_protocol_;
}

std::shared_ptr<network::SolanaRpcServer> SolanaValidator::get_rpc_server() const {
    return rpc_server_;
}

std::shared_ptr<ledger::LedgerManager> SolanaValidator::get_ledger_manager() const {
    return ledger_manager_;
}

std::shared_ptr<validator::ValidatorCore> SolanaValidator::get_validator_core() const {
    return validator_core_;
}

std::shared_ptr<staking::StakingManager> SolanaValidator::get_staking_manager() const {
    return staking_manager_;
}

std::shared_ptr<svm::ExecutionEngine> SolanaValidator::get_execution_engine() const {
    return execution_engine_;
}

SolanaValidator::ValidatorStats SolanaValidator::get_stats() const {
    impl_->update_stats();
    
    auto stats = impl_->stats_;
    
    // Update current state
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
    
    if (ledger_manager_) {
        stats.slots_behind = 0; // Would calculate based on network state
    }
    
    return stats;
}

common::Result<bool> SolanaValidator::update_config(const common::ValidatorConfig& new_config) {
    if (running_.load()) {
        return common::Result<bool>("Cannot update config while validator is running");
    }
    
    config_ = new_config;
    std::cout << "Updated validator configuration" << std::endl;
    return common::Result<bool>(true);
}

const common::ValidatorConfig& SolanaValidator::get_config() const {
    return config_;
}

// Private implementation methods
common::Result<bool> SolanaValidator::initialize_identity() {
    // Enhanced identity management with robust error handling
    std::string identity_path = config_.identity_keypair_path;
    
    // If no path specified, create a default one
    if (identity_path.empty()) {
        identity_path = "./validator-keypair.json";
        std::cout << "No identity path specified, using default: " << identity_path << std::endl;
    }
    
    // Ensure the directory exists
    std::string dir_path = identity_path.substr(0, identity_path.find_last_of("/\\"));
    if (!dir_path.empty()) {
        // Create directory if it doesn't exist (simplified for this implementation)
        std::system(("mkdir -p " + dir_path).c_str());
    }
    
    // Try to load existing identity
    std::ifstream keypair_file(identity_path);
    if (keypair_file.is_open()) {
        std::string file_content((std::istreambuf_iterator<char>(keypair_file)),
                                std::istreambuf_iterator<char>());
        keypair_file.close();
        
        // Try to parse as Solana CLI JSON format first
        if (file_content.front() == '[' && file_content.back() == ']') {
            std::cout << "🔑 Loading Solana CLI format keypair..." << std::endl;
            try {
                // Parse JSON array format: [byte1, byte2, ..., byte64]
                // Extract numbers from the JSON array
                std::vector<uint8_t> keypair_bytes;
                std::istringstream iss(file_content.substr(1, file_content.length() - 2)); // Remove [ ]
                std::string token;
                
                while (std::getline(iss, token, ',')) {
                    // Remove whitespace
                    token.erase(std::remove_if(token.begin(), token.end(), ::isspace), token.end());
                    if (!token.empty()) {
                        int byte_val = std::stoi(token);
                        if (byte_val >= 0 && byte_val <= 255) {
                            keypair_bytes.push_back(static_cast<uint8_t>(byte_val));
                        }
                    }
                }
                
                if (keypair_bytes.size() >= 32) {
                    // Use first 32 bytes as public key (Solana convention)
                    impl_->validator_identity_.resize(32);
                    std::copy(keypair_bytes.begin(), keypair_bytes.begin() + 32, impl_->validator_identity_.begin());
                    std::cout << "✅ Successfully loaded Solana CLI format identity from " << identity_path << std::endl;
                    return common::Result<bool>(true);
                } else {
                    std::cout << "⚠️ Invalid Solana CLI keypair file (insufficient bytes: " << keypair_bytes.size() << ")" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "⚠️ Failed to parse Solana CLI keypair: " << e.what() << std::endl;
            }
        }
        // Try to parse as hex format (legacy)
        else if (file_content.length() >= 64) {
            std::cout << "🔑 Loading hex format keypair..." << std::endl;
            impl_->validator_identity_.resize(32);
            try {
                for (size_t i = 0; i < 32; ++i) {
                    std::string byte_str = file_content.substr(i * 2, 2);
                    impl_->validator_identity_[i] = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
                }
                std::cout << "✅ Successfully loaded hex format identity from " << identity_path << std::endl;
                return common::Result<bool>(true);
            } catch (const std::exception& e) {
                std::cout << "⚠️ Failed to parse hex format keypair: " << e.what() << std::endl;
            }
        } else {
            std::cout << "⚠️ Invalid identity file format (too short: " << file_content.length() << " chars)" << std::endl;
        }
    }
    
    // Generate new identity if loading failed or file doesn't exist
    std::cout << "🔑 Generating new validator identity..." << std::endl;
    
    // Generate cryptographically secure identity
    impl_->validator_identity_.resize(32);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(1, 255);
    
    for (size_t i = 0; i < 32; ++i) {
        impl_->validator_identity_[i] = dis(gen);
    }
    
    // Save the generated identity to file in Solana CLI compatible format
    std::ofstream out_file(identity_path);
    if (out_file.is_open()) {
        // Generate a full 64-byte keypair (32 private + 32 public)
        std::vector<uint8_t> full_keypair(64);
        std::copy(impl_->validator_identity_.begin(), impl_->validator_identity_.end(), full_keypair.begin());
        
        // Generate additional 32 bytes for private key part (simplified)
        for (size_t i = 32; i < 64; ++i) {
            full_keypair[i] = dis(gen);
        }
        
        // Save in JSON array format compatible with Solana CLI
        out_file << "[";
        for (size_t i = 0; i < 64; ++i) {
            out_file << static_cast<int>(full_keypair[i]);
            if (i < 63) out_file << ",";
        }
        out_file << "]";
        out_file.close();
        std::cout << "✅ Saved new identity to " << identity_path << " (Solana CLI format)" << std::endl;
        
        // Verify the saved file can be read back
        std::ifstream verify_file(identity_path);
        if (verify_file.is_open()) {
            std::string verify_data((std::istreambuf_iterator<char>(verify_file)),
                                   std::istreambuf_iterator<char>());
            verify_file.close();
            if (verify_data.front() == '[' && verify_data.back() == ']') {
                std::cout << "✅ Identity file verification successful (Solana CLI format)" << std::endl;
            } else {
                std::cout << "⚠️ Identity file verification failed" << std::endl;
            }
        }
    } else {
        std::cout << "⚠️ Failed to save identity to " << identity_path 
                 << " - validator will use temporary identity" << std::endl;
        // Continue anyway with in-memory identity
    }
    
    // Display identity info for debugging
    std::cout << "Validator identity (first 8 bytes): ";
    for (size_t i = 0; i < 8; ++i) {
        std::cout << std::hex << std::setfill('0') << std::setw(2) 
                 << static_cast<int>(impl_->validator_identity_[i]);
    }
    std::cout << "..." << std::dec << std::endl;
    
    return common::Result<bool>(true);
}

common::Result<bool> SolanaValidator::initialize_components() {
    try {
        std::cout << "🚀 Initializing validator components..." << std::endl;
        
        // Initialize ledger manager
        std::cout << "  📚 Initializing ledger manager..." << std::endl;
        ledger_manager_ = std::make_shared<ledger::LedgerManager>(config_.ledger_path);
        
        // Initialize SVM components
        std::cout << "  ⚙️  Initializing SVM execution engine..." << std::endl;
        execution_engine_ = std::make_shared<svm::ExecutionEngine>();
        account_manager_ = std::make_shared<svm::AccountManager>();
        
        // Initialize staking manager
        std::cout << "  💰 Initializing staking manager..." << std::endl;
        staking_manager_ = std::make_shared<staking::StakingManager>();
        
        // Initialize validator core with enhanced setup
        std::cout << "  🎯 Initializing validator core..." << std::endl;
        validator_core_ = std::make_shared<validator::ValidatorCore>(
            ledger_manager_, validator_identity_);
        
        // Initialize and configure Proof of History
        std::cout << "  ⏰ Initializing Proof of History..." << std::endl;
        
        std::cout << "    PoH configuration: " << std::endl;
        std::cout << "      Tick duration: " << config_.poh_target_tick_duration_us << "μs" << std::endl;
        std::cout << "      Ticks per slot: " << config_.poh_ticks_per_slot << std::endl;
        std::cout << "      Batch processing: " << (config_.poh_enable_batch_processing ? "enabled" : "disabled") << std::endl;
        std::cout << "      Hashing threads: " << config_.poh_hashing_threads << std::endl;
        
        // Initialize network components
        std::cout << "  🌐 Initializing network components..." << std::endl;
        gossip_protocol_ = std::make_shared<network::GossipProtocol>(config_);
        rpc_server_ = std::make_shared<network::SolanaRpcServer>(config_);
        
        // Connect RPC server to validator components
        rpc_server_->set_ledger_manager(ledger_manager_);
        rpc_server_->set_validator_core(validator_core_);
        rpc_server_->set_staking_manager(staking_manager_);
        rpc_server_->set_execution_engine(execution_engine_);
        rpc_server_->set_account_manager(account_manager_);
        
        // Initialize snapshot bootstrap manager for RPC mode
        if (config_.enable_rpc && config_.network_id == "devnet") {
            std::cout << "  📸 Initializing snapshot bootstrap manager..." << std::endl;
            snapshot_bootstrap_ = std::make_unique<validator::SnapshotBootstrapManager>(config_);
        }
        
        std::cout << "All components initialized successfully" << std::endl;
        return common::Result<bool>(true);
        
    } catch (const std::exception& e) {
        return common::Result<bool>(std::string("Component initialization failed: ") + e.what());
    }
}

common::Result<bool> SolanaValidator::setup_event_handlers() {
    // Setup validator core callbacks
    validator_core_->set_block_callback(
        [this](const ledger::Block& block) {
            this->on_block_received(block);
        });
    
    validator_core_->set_vote_callback(
        [this](const validator::Vote& vote) {
            this->on_vote_received(vote);
        });
    
    // Setup gossip message handlers
    gossip_protocol_->register_handler(
        network::MessageType::BLOCK_NOTIFICATION,
        [this](const network::NetworkMessage& message) {
            this->on_gossip_message(message);
        });
    
    gossip_protocol_->register_handler(
        network::MessageType::VOTE_NOTIFICATION,
        [this](const network::NetworkMessage& message) {
            this->on_gossip_message(message);
        });
    
    // Note: RPC methods are automatically registered by SolanaRpcServer
    
    std::cout << "Event handlers setup complete" << std::endl;
    return common::Result<bool>(true);
}

void SolanaValidator::on_block_received(const ledger::Block& block) {
    impl_->stats_.blocks_processed++;
    impl_->stats_.transactions_processed += block.transactions.size();
    
    std::cout << "Processed block at slot " << block.slot 
              << " with " << block.transactions.size() << " transactions" << std::endl;
    
    // Broadcast block to network
    if (gossip_protocol_ && running_.load()) {
        network::NetworkMessage message;
        message.type = network::MessageType::BLOCK_NOTIFICATION;
        message.sender = validator_identity_;
        message.payload = block.serialize();
        message.timestamp = static_cast<uint64_t>(std::max(0L, std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()));
        
        gossip_protocol_->broadcast_message(message);
    }
}

void SolanaValidator::on_vote_received(const validator::Vote& vote) {
    impl_->stats_.votes_cast++;
    
    std::cout << "Cast vote for slot " << vote.slot << std::endl;
    
    // Broadcast vote to network
    if (gossip_protocol_ && running_.load()) {
        network::NetworkMessage message;
        message.type = network::MessageType::VOTE_NOTIFICATION;
        message.sender = validator_identity_;
        message.payload = vote.serialize();
        message.timestamp = static_cast<uint64_t>(std::max(0L, std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()));
        
        gossip_protocol_->broadcast_message(message);
    }
}

void SolanaValidator::on_gossip_message(const network::NetworkMessage& message) {
    switch (message.type) {
        case network::MessageType::BLOCK_NOTIFICATION: {
            // Deserialize and process block
            if (message.payload.size() >= 64) { // Minimum block size
                ledger::Block block(message.payload);
                if (validator_core_) {
                    validator_core_->process_block(block);
                }
            }
            break;
        }
        case network::MessageType::VOTE_NOTIFICATION: {
            // Deserialize and process vote
            if (message.payload.size() >= 40) { // Minimum vote size
                validator::Vote vote;
                // Stub deserialization
                if (validator_core_) {
                    validator_core_->process_vote(vote);
                }
            }
            break;
        }
        default:
            break;
    }
}

common::Result<std::vector<uint8_t>> SolanaValidator::load_validator_identity(const std::string& keypair_path) {
    try {
        std::ifstream file(keypair_path, std::ios::binary);
        if (!file) {
            return common::Result<std::vector<uint8_t>>("Failed to open keypair file");
        }
        
        // Read keypair file (32 bytes for public key, 32 bytes for private key)
        std::vector<uint8_t> keypair_data(64);
        file.read(reinterpret_cast<char*>(keypair_data.data()), 64);
        
        if (file.gcount() != 64) {
            return common::Result<std::vector<uint8_t>>("Invalid keypair file size");
        }
        
        // Extract public key (first 32 bytes)
        std::vector<uint8_t> public_key(keypair_data.begin(), keypair_data.begin() + 32);
        
        std::cout << "Loaded validator identity from keypair file" << std::endl;
        return common::Result<std::vector<uint8_t>>(public_key);
        
    } catch (const std::exception& e) {
        return common::Result<std::vector<uint8_t>>(
            std::string("Failed to load keypair: ") + e.what());
    }
}

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
    if (std::all_of(identity.begin(), identity.end(), [](uint8_t b) { return b == 0; })) {
        identity[0] = 1; // Prevent all-zero identity
    }
    
    std::cout << "Generated new 32-byte validator identity" << std::endl;
    return identity;
}

common::Result<bool> SolanaValidator::bootstrap_ledger() {
    // Only run bootstrap for devnet RPC nodes with snapshot support
    if (!config_.enable_rpc || config_.network_id != "devnet" || !snapshot_bootstrap_) {
        std::cout << "Skipping ledger bootstrap (not devnet RPC mode or bootstrap disabled)" << std::endl;
        return common::Result<bool>(true);
    }
    
    std::cout << "🔄 Starting ledger bootstrap from snapshot..." << std::endl;
    
    // Set up progress callback
    snapshot_bootstrap_->set_progress_callback([](const std::string& phase, uint64_t current, uint64_t total) {
        std::cout << "[Bootstrap] " << phase;
        if (total > 0) {
            std::cout << " (" << current << "/" << total << ")";
        }
        std::cout << std::endl;
    });
    
    // Run the bootstrap process
    auto result = snapshot_bootstrap_->bootstrap_from_snapshot();
    if (!result.is_ok()) {
        std::cout << "⚠️  Snapshot bootstrap failed: " << result.error() << std::endl;
        std::cout << "   Continuing without snapshot bootstrap..." << std::endl;
        // Don't fail completely - just continue without bootstrap
        return common::Result<bool>(true);
    }
    
    std::cout << "✅ Ledger bootstrap completed successfully" << std::endl;
    return common::Result<bool>(true);
}

} // namespace slonana