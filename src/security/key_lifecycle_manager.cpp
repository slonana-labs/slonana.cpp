#include "security/key_manager.h"
#include <filesystem>
#include <thread>
#include <fstream>

namespace slonana {
namespace security {

// ============================================================================
// KeyRotationScheduler Implementation
// ============================================================================

KeyRotationScheduler::KeyRotationScheduler(std::shared_ptr<KeyStore> key_store)
    : key_store_(key_store), is_running_(false) {
    // Set default rotation policy
    policy_.rotation_interval = std::chrono::hours(24 * 30); // 30 days
    policy_.max_use_count = UINT64_MAX;
    policy_.rotate_on_suspicious_activity = true;
    policy_.automatic_rotation_enabled = true;
    policy_.grace_period = std::chrono::hours(24);
}

KeyRotationScheduler::~KeyRotationScheduler() {
    stop_scheduler();
}

void KeyRotationScheduler::set_default_policy(const KeyRotationPolicy& policy) {
    policy_ = policy;
}

void KeyRotationScheduler::set_key_policy(const std::string& key_id, const KeyRotationPolicy& policy) {
    key_policies_[key_id] = policy;
}

KeyRotationPolicy KeyRotationScheduler::get_policy(const std::string& key_id) const {
    auto it = key_policies_.find(key_id);
    if (it != key_policies_.end()) {
        return it->second;
    }
    return policy_; // Return default policy
}

Result<bool> KeyRotationScheduler::start_scheduler() {
    if (is_running_) {
        return Result<bool>("Scheduler already running");
    }
    
    is_running_ = true;
    last_check_ = std::chrono::steady_clock::now();
    
    // In a real implementation, you'd start a background thread here
    // For this minimal implementation, we'll rely on periodic manual checks
    
    return Result<bool>(true);
}

void KeyRotationScheduler::stop_scheduler() {
    is_running_ = false;
}

bool KeyRotationScheduler::should_rotate_key(const std::string& key_id, const KeyMetadata& metadata) const {
    auto policy = get_policy(key_id);
    
    if (!policy.automatic_rotation_enabled) {
        return false;
    }
    
    auto now = std::chrono::system_clock::now();
    
    // Check if key has expired
    if (now >= metadata.expires_at) {
        return true;
    }
    
    // Check rotation interval
    auto age = now - metadata.created_at;
    if (age >= policy.rotation_interval) {
        return true;
    }
    
    // Check use count
    if (metadata.use_count >= policy.max_use_count) {
        return true;
    }
    
    return false;
}

Result<SecureBuffer> KeyRotationScheduler::generate_new_key(const std::string& key_type) const {
    size_t key_size = 32; // Default to 256 bits
    
    if (key_type == "validator_identity") {
        key_size = 32; // Ed25519 private key size
    } else if (key_type == "session") {
        key_size = 32; // AES-256 key size
    }
    
    return key_utils::generate_secure_random(key_size);
}

Result<std::string> KeyRotationScheduler::rotate_key(const std::string& key_id) {
    // Get current key metadata
    auto metadata_result = key_store_->get_key_metadata(key_id);
    if (!metadata_result.is_ok()) {
        return Result<std::string>("Failed to get key metadata: " + metadata_result.error());
    }
    
    auto metadata = metadata_result.value();
    
    // Generate new key
    auto new_key_result = generate_new_key(metadata.key_type);
    if (!new_key_result.is_ok()) {
        return Result<std::string>("Failed to generate new key: " + new_key_result.error());
    }
    
    // Create new key metadata
    std::string new_key_id = key_utils::generate_key_id(metadata.key_type);
    KeyMetadata new_metadata;
    new_metadata.key_id = new_key_id;
    new_metadata.key_type = metadata.key_type;
    new_metadata.created_at = std::chrono::system_clock::now();
    new_metadata.expires_at = new_metadata.created_at + get_policy(key_id).rotation_interval;
    new_metadata.use_count = 0;
    new_metadata.is_revoked = false;
    new_metadata.authorized_operations = metadata.authorized_operations;
    
    // Store new key
    auto store_result = key_store_->store_key(new_key_id, new_key_result.value(), new_metadata);
    if (!store_result.is_ok()) {
        return Result<std::string>("Failed to store new key: " + store_result.error());
    }
    
    // Mark old key as revoked (but don't delete yet - grace period)
    metadata.is_revoked = true;
    metadata.revocation_reason = "Automatic rotation";
    auto update_result = key_store_->update_metadata(key_id, metadata);
    if (!update_result.is_ok()) {
        // Log warning but don't fail the rotation
    }
    
    return Result<std::string>(new_key_id);
}

Result<bool> KeyRotationScheduler::revoke_key(const std::string& key_id, const std::string& reason) {
    auto metadata_result = key_store_->get_key_metadata(key_id);
    if (!metadata_result.is_ok()) {
        return Result<bool>("Failed to get key metadata: " + metadata_result.error());
    }
    
    auto metadata = metadata_result.value();
    metadata.is_revoked = true;
    metadata.revocation_reason = reason;
    
    return key_store_->update_metadata(key_id, metadata);
}

Result<std::vector<std::string>> KeyRotationScheduler::check_expired_keys() {
    auto keys_result = key_store_->list_keys();
    if (!keys_result.is_ok()) {
        return Result<std::vector<std::string>>("Failed to list keys: " + keys_result.error());
    }
    
    std::vector<std::string> expired_keys;
    auto now = std::chrono::system_clock::now();
    
    for (const auto& key_id : keys_result.value()) {
        auto metadata_result = key_store_->get_key_metadata(key_id);
        if (metadata_result.is_ok()) {
            const auto& metadata = metadata_result.value();
            if (should_rotate_key(key_id, metadata)) {
                expired_keys.push_back(key_id);
            }
        }
    }
    
    return Result<std::vector<std::string>>(expired_keys);
}

void KeyRotationScheduler::check_rotations() {
    if (!is_running_) {
        return;
    }
    
    auto expired_keys_result = check_expired_keys();
    if (expired_keys_result.is_ok()) {
        for (const auto& key_id : expired_keys_result.value()) {
            auto rotation_result = rotate_key(key_id);
            if (rotation_result.is_ok()) {
                // Log successful rotation
            } else {
                // Log rotation failure
            }
        }
    }
    
    last_check_ = std::chrono::steady_clock::now();
}

Result<bool> KeyRotationScheduler::cleanup_expired_keys() {
    auto keys_result = key_store_->list_keys();
    if (!keys_result.is_ok()) {
        return Result<bool>("Failed to list keys: " + keys_result.error());
    }
    
    auto now = std::chrono::system_clock::now();
    bool all_cleaned = true;
    
    for (const auto& key_id : keys_result.value()) {
        auto metadata_result = key_store_->get_key_metadata(key_id);
        if (metadata_result.is_ok()) {
            const auto& metadata = metadata_result.value();
            
            // Clean up revoked keys after grace period
            if (metadata.is_revoked) {
                auto policy = get_policy(key_id);
                auto revocation_age = now - metadata.expires_at; // Simplified
                
                if (revocation_age >= policy.grace_period) {
                    auto wipe_result = key_store_->secure_wipe_key(key_id);
                    if (!wipe_result.is_ok()) {
                        all_cleaned = false;
                    }
                }
            }
        }
    }
    
    return Result<bool>(all_cleaned);
}

// ============================================================================
// CryptographicKeyManager Implementation
// ============================================================================

CryptographicKeyManager::CryptographicKeyManager(std::shared_ptr<KeyStore> key_store)
    : key_store_(key_store) {
    rotation_scheduler_ = std::make_unique<KeyRotationScheduler>(key_store);
    
    // Set default rotation policy
    default_policy_.rotation_interval = std::chrono::hours(24 * 30); // 30 days
    default_policy_.max_use_count = UINT64_MAX;
    default_policy_.rotate_on_suspicious_activity = true;
    default_policy_.automatic_rotation_enabled = true;
    default_policy_.grace_period = std::chrono::hours(24);
}

CryptographicKeyManager::~CryptographicKeyManager() {
    shutdown();
}

Result<bool> CryptographicKeyManager::initialize() {
    // Check entropy quality
    if (!key_utils::check_entropy_quality()) {
        return Result<bool>("Insufficient system entropy for secure key generation");
    }
    
    // Initialize key store if needed
    if (auto* file_store = dynamic_cast<EncryptedFileKeyStore*>(key_store_.get())) {
        auto init_result = file_store->initialize_storage();
        if (!init_result.is_ok()) {
            return Result<bool>("Failed to initialize key storage: " + init_result.error());
        }
    }
    
    // Start rotation scheduler
    rotation_scheduler_->set_default_policy(default_policy_);
    auto scheduler_result = rotation_scheduler_->start_scheduler();
    if (!scheduler_result.is_ok()) {
        return Result<bool>("Failed to start rotation scheduler: " + scheduler_result.error());
    }
    
    return Result<bool>(true);
}

Result<bool> CryptographicKeyManager::shutdown() {
    if (rotation_scheduler_) {
        rotation_scheduler_->stop_scheduler();
    }
    return Result<bool>(true);
}

Result<SecureBuffer> CryptographicKeyManager::generate_cryptographically_secure_key(size_t key_size) const {
    return key_utils::generate_secure_random(key_size);
}

std::string CryptographicKeyManager::generate_key_id(const std::string& key_type) const {
    return key_utils::generate_key_id(key_type);
}

Result<std::string> CryptographicKeyManager::generate_validator_identity_key() {
    // Generate Ed25519-compatible key (32 bytes)
    auto key_result = generate_cryptographically_secure_key(32);
    if (!key_result.is_ok()) {
        return Result<std::string>("Failed to generate validator key: " + key_result.error());
    }
    
    // Validate key strength
    if (!key_utils::validate_key_strength(key_result.value())) {
        return Result<std::string>("Generated key failed strength validation");
    }
    
    // Create metadata
    std::string key_id = generate_key_id("validator_identity");
    KeyMetadata metadata;
    metadata.key_id = key_id;
    metadata.key_type = "validator_identity";
    metadata.created_at = std::chrono::system_clock::now();
    metadata.expires_at = metadata.created_at + default_policy_.rotation_interval;
    metadata.use_count = 0;
    metadata.is_revoked = false;
    metadata.authorized_operations = {"sign_blocks", "sign_transactions", "consensus_voting"};
    
    // Store key
    auto store_result = key_store_->store_key(key_id, key_result.value(), metadata);
    if (!store_result.is_ok()) {
        return Result<std::string>("Failed to store validator key: " + store_result.error());
    }
    
    return Result<std::string>(key_id);
}

Result<std::string> CryptographicKeyManager::generate_session_key() {
    auto key_result = generate_cryptographically_secure_key(32);
    if (!key_result.is_ok()) {
        return Result<std::string>("Failed to generate session key: " + key_result.error());
    }
    
    std::string key_id = generate_key_id("session");
    KeyMetadata metadata;
    metadata.key_id = key_id;
    metadata.key_type = "session";
    metadata.created_at = std::chrono::system_clock::now();
    metadata.expires_at = metadata.created_at + std::chrono::hours(24); // Sessions expire daily
    metadata.use_count = 0;
    metadata.is_revoked = false;
    metadata.authorized_operations = {"encrypt_communications", "authenticate_requests"};
    
    auto store_result = key_store_->store_key(key_id, key_result.value(), metadata);
    if (!store_result.is_ok()) {
        return Result<std::string>("Failed to store session key: " + store_result.error());
    }
    
    return Result<std::string>(key_id);
}

Result<std::string> CryptographicKeyManager::generate_key(const std::string& key_type, size_t key_size) {
    auto key_result = generate_cryptographically_secure_key(key_size);
    if (!key_result.is_ok()) {
        return Result<std::string>("Failed to generate key: " + key_result.error());
    }
    
    std::string key_id = generate_key_id(key_type);
    KeyMetadata metadata;
    metadata.key_id = key_id;
    metadata.key_type = key_type;
    metadata.created_at = std::chrono::system_clock::now();
    metadata.expires_at = metadata.created_at + default_policy_.rotation_interval;
    metadata.use_count = 0;
    metadata.is_revoked = false;
    
    auto store_result = key_store_->store_key(key_id, key_result.value(), metadata);
    if (!store_result.is_ok()) {
        return Result<std::string>("Failed to store key: " + store_result.error());
    }
    
    return Result<std::string>(key_id);
}

Result<SecureBuffer> CryptographicKeyManager::get_key(const std::string& key_id) {
    // Check if key exists and is valid
    auto metadata_result = key_store_->get_key_metadata(key_id);
    if (!metadata_result.is_ok()) {
        return Result<SecureBuffer>("Key not found: " + metadata_result.error());
    }
    
    const auto& metadata = metadata_result.value();
    if (metadata.is_revoked) {
        return Result<SecureBuffer>("Key has been revoked: " + metadata.revocation_reason);
    }
    
    auto now = std::chrono::system_clock::now();
    if (now >= metadata.expires_at) {
        return Result<SecureBuffer>("Key has expired");
    }
    
    return key_store_->load_key(key_id);
}

Result<KeyMetadata> CryptographicKeyManager::get_key_info(const std::string& key_id) {
    return key_store_->get_key_metadata(key_id);
}

Result<bool> CryptographicKeyManager::use_key(const std::string& key_id) {
    auto metadata_result = key_store_->get_key_metadata(key_id);
    if (!metadata_result.is_ok()) {
        return Result<bool>("Key not found: " + metadata_result.error());
    }
    
    auto metadata = metadata_result.value();
    metadata.use_count++;
    metadata.last_used = std::chrono::system_clock::now();
    
    return key_store_->update_metadata(key_id, metadata);
}

Result<std::vector<std::string>> CryptographicKeyManager::list_keys() const {
    return key_store_->list_keys();
}

Result<std::string> CryptographicKeyManager::rotate_key(const std::string& key_id) {
    return rotation_scheduler_->rotate_key(key_id);
}

Result<bool> CryptographicKeyManager::revoke_key(const std::string& key_id, const std::string& reason) {
    return rotation_scheduler_->revoke_key(key_id, reason);
}

Result<bool> CryptographicKeyManager::extend_key_expiry(const std::string& key_id, 
                                                       std::chrono::seconds extension) {
    auto metadata_result = key_store_->get_key_metadata(key_id);
    if (!metadata_result.is_ok()) {
        return Result<bool>("Key not found: " + metadata_result.error());
    }
    
    auto metadata = metadata_result.value();
    metadata.expires_at += extension;
    
    return key_store_->update_metadata(key_id, metadata);
}

Result<std::string> CryptographicKeyManager::import_legacy_key(const std::string& file_path, 
                                                              const std::string& key_type) {
    // Read legacy key file
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        return Result<std::string>("Failed to open legacy key file");
    }
    
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    if (file_size != 64) { // Expect 64-byte Solana keypair format
        return Result<std::string>("Invalid legacy key file size");
    }
    
    std::vector<uint8_t> file_data(file_size);
    file.read(reinterpret_cast<char*>(file_data.data()), file_size);
    file.close();
    
    // Extract private key (first 32 bytes in Solana format)
    std::vector<uint8_t> private_key_data(file_data.begin(), file_data.begin() + 32);
    SecureBuffer private_key(private_key_data);
    
    // Create key ID and metadata
    std::string key_id = generate_key_id(key_type + "_legacy");
    KeyMetadata metadata;
    metadata.key_id = key_id;
    metadata.key_type = key_type;
    metadata.created_at = std::chrono::system_clock::now();
    metadata.expires_at = metadata.created_at + default_policy_.rotation_interval;
    metadata.use_count = 0;
    metadata.is_revoked = false;
    
    // Store the key
    auto store_result = key_store_->store_key(key_id, private_key, metadata);
    if (!store_result.is_ok()) {
        return Result<std::string>("Failed to store imported key: " + store_result.error());
    }
    
    return Result<std::string>(key_id);
}

Result<bool> CryptographicKeyManager::export_key_for_backup(const std::string& key_id, 
                                                           const std::string& backup_path) {
    auto key_result = key_store_->load_key(key_id);
    if (!key_result.is_ok()) {
        return Result<bool>("Failed to load key: " + key_result.error());
    }
    
    // For security, we'll encrypt the backup
    auto encrypted_result = static_cast<EncryptedFileKeyStore*>(key_store_.get())->encrypt_data_for_backup(key_result.value());
    if (!encrypted_result.is_ok()) {
        return Result<bool>("Failed to encrypt backup: " + encrypted_result.error());
    }
    
    std::ofstream backup_file(backup_path, std::ios::binary);
    if (!backup_file) {
        return Result<bool>("Failed to create backup file");
    }
    
    const auto& encrypted_data = encrypted_result.value();
    backup_file.write(reinterpret_cast<const char*>(encrypted_data.data()), encrypted_data.size());
    backup_file.close();
    
    return Result<bool>(true);
}

void CryptographicKeyManager::set_rotation_policy(const KeyRotationPolicy& policy) {
    default_policy_ = policy;
    rotation_scheduler_->set_default_policy(policy);
}

Result<bool> CryptographicKeyManager::set_primary_validator_key(const std::string& key_id) {
    // Verify the key exists and is a validator identity key
    auto metadata_result = key_store_->get_key_metadata(key_id);
    if (!metadata_result.is_ok()) {
        return Result<bool>("Key not found: " + metadata_result.error());
    }
    
    const auto& metadata = metadata_result.value();
    if (metadata.key_type != "validator_identity") {
        return Result<bool>("Key is not a validator identity key");
    }
    
    if (metadata.is_revoked) {
        return Result<bool>("Cannot set revoked key as primary");
    }
    
    primary_validator_key_id_ = key_id;
    return Result<bool>(true);
}

Result<SecureBuffer> CryptographicKeyManager::get_primary_validator_key() {
    if (primary_validator_key_id_.empty()) {
        return Result<SecureBuffer>("No primary validator key set");
    }
    
    return get_key(primary_validator_key_id_);
}

CryptographicKeyManager::KeyManagerStats CryptographicKeyManager::get_stats() const {
    KeyManagerStats stats = {};
    
    auto keys_result = key_store_->list_keys();
    if (keys_result.is_ok()) {
        stats.total_keys = keys_result.value().size();
        
        for (const auto& key_id : keys_result.value()) {
            auto metadata_result = key_store_->get_key_metadata(key_id);
            if (metadata_result.is_ok()) {
                const auto& metadata = metadata_result.value();
                auto now = std::chrono::system_clock::now();
                
                if (metadata.is_revoked) {
                    stats.revoked_keys++;
                } else if (now >= metadata.expires_at) {
                    stats.expired_keys++;
                } else {
                    stats.active_keys++;
                }
            }
        }
    }
    
    return stats;
}

Result<bool> CryptographicKeyManager::secure_wipe_all_revoked_keys() {
    return rotation_scheduler_->cleanup_expired_keys();
}

Result<bool> CryptographicKeyManager::emergency_revoke_all_keys(const std::string& reason) {
    auto keys_result = key_store_->list_keys();
    if (!keys_result.is_ok()) {
        return Result<bool>("Failed to list keys: " + keys_result.error());
    }
    
    bool all_revoked = true;
    for (const auto& key_id : keys_result.value()) {
        auto revoke_result = rotation_scheduler_->revoke_key(key_id, reason);
        if (!revoke_result.is_ok()) {
            all_revoked = false;
        }
    }
    
    return Result<bool>(all_revoked);
}

Result<std::vector<std::string>> CryptographicKeyManager::audit_key_usage() {
    auto keys_result = key_store_->list_keys();
    if (!keys_result.is_ok()) {
        return Result<std::vector<std::string>>("Failed to list keys: " + keys_result.error());
    }
    
    std::vector<std::string> audit_log;
    
    for (const auto& key_id : keys_result.value()) {
        auto metadata_result = key_store_->get_key_metadata(key_id);
        if (metadata_result.is_ok()) {
            const auto& metadata = metadata_result.value();
            
            std::stringstream entry;
            entry << "Key: " << key_id 
                  << ", Type: " << metadata.key_type
                  << ", Uses: " << metadata.use_count
                  << ", Revoked: " << (metadata.is_revoked ? "yes" : "no");
            
            audit_log.push_back(entry.str());
        }
    }
    
    return Result<std::vector<std::string>>(audit_log);
}

} // namespace security
} // namespace slonana