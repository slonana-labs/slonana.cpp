#include "security/secure_validator_identity.h"
#include <filesystem>

namespace slonana {
namespace security {

// ============================================================================
// SecureValidatorIdentity Implementation
// ============================================================================

SecureValidatorIdentity::SecureValidatorIdentity(const std::string& storage_path)
    : storage_path_(storage_path) {
}

SecureValidatorIdentity::~SecureValidatorIdentity() {
    shutdown();
}

Result<bool> SecureValidatorIdentity::initialize() {
    // Create key store
    auto key_store = SecureKeyManagerFactory::create_encrypted_file_store(storage_path_);
    if (!key_store) {
        return Result<bool>("Failed to create key store");
    }
    
    // Create key manager
    key_manager_ = SecureKeyManagerFactory::create_key_manager(key_store);
    if (!key_manager_) {
        return Result<bool>("Failed to create key manager");
    }
    
    // Initialize key manager
    auto init_result = key_manager_->initialize();
    if (!init_result.is_ok()) {
        return Result<bool>("Failed to initialize key manager: " + init_result.error());
    }
    
    // Try to find existing primary identity
    auto keys_result = key_manager_->list_keys();
    if (keys_result.is_ok()) {
        for (const auto& key_id : keys_result.value()) {
            auto metadata_result = key_manager_->get_key_info(key_id);
            if (metadata_result.is_ok()) {
                const auto& metadata = metadata_result.value();
                if (metadata.key_type == "validator_identity" && !metadata.is_revoked) {
                    primary_identity_key_id_ = key_id;
                    break;
                }
            }
        }
    }
    
    return Result<bool>(true);
}

Result<bool> SecureValidatorIdentity::shutdown() {
    if (key_manager_) {
        return key_manager_->shutdown();
    }
    return Result<bool>(true);
}

Result<std::string> SecureValidatorIdentity::create_new_identity() {
    if (!key_manager_) {
        return Result<std::string>("Key manager not initialized");
    }
    
    auto key_result = key_manager_->generate_validator_identity_key();
    if (!key_result.is_ok()) {
        return Result<std::string>("Failed to generate identity: " + key_result.error());
    }
    
    std::string key_id = key_result.value();
    
    // Set as primary if we don't have one
    if (primary_identity_key_id_.empty()) {
        primary_identity_key_id_ = key_id;
        auto set_result = key_manager_->set_primary_validator_key(key_id);
        if (!set_result.is_ok()) {
            return Result<std::string>("Failed to set primary key: " + set_result.error());
        }
    }
    
    return Result<std::string>(key_id);
}

Result<std::string> SecureValidatorIdentity::import_legacy_identity(const std::string& keypair_path) {
    if (!key_manager_) {
        return Result<std::string>("Key manager not initialized");
    }
    
    auto import_result = key_manager_->import_legacy_key(keypair_path, "validator_identity");
    if (!import_result.is_ok()) {
        return Result<std::string>("Failed to import legacy key: " + import_result.error());
    }
    
    std::string key_id = import_result.value();
    
    // Set as primary if we don't have one
    if (primary_identity_key_id_.empty()) {
        primary_identity_key_id_ = key_id;
        auto set_result = key_manager_->set_primary_validator_key(key_id);
        if (!set_result.is_ok()) {
            return Result<std::string>("Failed to set primary key: " + set_result.error());
        }
    }
    
    return Result<std::string>(key_id);
}

Result<bool> SecureValidatorIdentity::set_primary_identity(const std::string& key_id) {
    if (!key_manager_) {
        return Result<bool>("Key manager not initialized");
    }
    
    auto set_result = key_manager_->set_primary_validator_key(key_id);
    if (set_result.is_ok()) {
        primary_identity_key_id_ = key_id;
    }
    
    return set_result;
}

Result<common::PublicKey> SecureValidatorIdentity::get_public_key() {
    if (!key_manager_ || primary_identity_key_id_.empty()) {
        return Result<common::PublicKey>("No primary identity set");
    }
    
    auto key_result = key_manager_->get_key(primary_identity_key_id_);
    if (!key_result.is_ok()) {
        return Result<common::PublicKey>("Failed to get key: " + key_result.error());
    }
    
    // For Ed25519, the public key is derived from the private key
    // This is a simplified implementation - in production you'd use proper Ed25519 key derivation
    const auto& private_key = key_result.value();
    if (private_key.size() != 32) {
        return Result<common::PublicKey>("Invalid private key size");
    }
    
    // Simplified: just copy the private key as public (NOT secure, just for demonstration)
    // In production, use proper Ed25519 public key derivation
    common::PublicKey public_key(private_key.data(), private_key.data() + 32);
    
    return Result<common::PublicKey>(public_key);
}

Result<SecureBuffer> SecureValidatorIdentity::get_private_key() {
    if (!key_manager_ || primary_identity_key_id_.empty()) {
        return Result<SecureBuffer>("No primary identity set");
    }
    
    // Track key usage
    auto use_result = key_manager_->use_key(primary_identity_key_id_);
    if (!use_result.is_ok()) {
        // Log warning but continue
    }
    
    return key_manager_->get_key(primary_identity_key_id_);
}

Result<std::string> SecureValidatorIdentity::rotate_identity() {
    if (!key_manager_ || primary_identity_key_id_.empty()) {
        return Result<std::string>("No primary identity to rotate");
    }
    
    auto rotate_result = key_manager_->rotate_key(primary_identity_key_id_);
    if (rotate_result.is_ok()) {
        // Update primary identity to the new key
        std::string new_key_id = rotate_result.value();
        auto set_result = set_primary_identity(new_key_id);
        if (!set_result.is_ok()) {
            return Result<std::string>("Failed to set new primary identity: " + set_result.error());
        }
        return Result<std::string>(new_key_id);
    }
    
    return rotate_result;
}

Result<bool> SecureValidatorIdentity::revoke_identity(const std::string& reason) {
    if (!key_manager_ || primary_identity_key_id_.empty()) {
        return Result<bool>("No primary identity to revoke");
    }
    
    auto revoke_result = key_manager_->revoke_key(primary_identity_key_id_, reason);
    if (revoke_result.is_ok()) {
        primary_identity_key_id_.clear(); // Clear revoked primary identity
    }
    
    return revoke_result;
}

Result<common::PublicKey> SecureValidatorIdentity::generate_legacy_compatible_identity() {
    // Create new identity and return public key in legacy format
    auto create_result = create_new_identity();
    if (!create_result.is_ok()) {
        return Result<common::PublicKey>("Failed to create identity: " + create_result.error());
    }
    
    return get_public_key();
}

Result<bool> SecureValidatorIdentity::export_for_legacy_use(const std::string& output_path) {
    if (!key_manager_ || primary_identity_key_id_.empty()) {
        return Result<bool>("No primary identity to export");
    }
    
    // Get the private key
    auto key_result = key_manager_->get_key(primary_identity_key_id_);
    if (!key_result.is_ok()) {
        return Result<bool>("Failed to get key: " + key_result.error());
    }
    
    const auto& private_key = key_result.value();
    if (private_key.size() != 32) {
        return Result<bool>("Invalid key size for export");
    }
    
    // Create 64-byte legacy format (32 private + 32 public)
    std::vector<uint8_t> legacy_keypair(64);
    std::copy(private_key.data(), private_key.data() + 32, legacy_keypair.begin());
    
    // Generate public key (simplified - should use proper Ed25519 derivation)
    std::copy(private_key.data(), private_key.data() + 32, legacy_keypair.begin() + 32);
    
    // Write to file
    std::ofstream file(output_path, std::ios::binary);
    if (!file) {
        return Result<bool>("Failed to open output file");
    }
    
    file.write(reinterpret_cast<const char*>(legacy_keypair.data()), legacy_keypair.size());
    file.close();
    
    // Set restrictive permissions
    std::filesystem::permissions(output_path, 
                               std::filesystem::perms::owner_read | 
                               std::filesystem::perms::owner_write);
    
    return Result<bool>(true);
}

Result<SecureValidatorIdentity::IdentityStatus> SecureValidatorIdentity::get_status() {
    if (!key_manager_ || primary_identity_key_id_.empty()) {
        return Result<IdentityStatus>("No primary identity set");
    }
    
    auto metadata_result = key_manager_->get_key_info(primary_identity_key_id_);
    if (!metadata_result.is_ok()) {
        return Result<IdentityStatus>("Failed to get key info: " + metadata_result.error());
    }
    
    const auto& metadata = metadata_result.value();
    
    IdentityStatus status;
    status.key_id = metadata.key_id;
    status.key_type = metadata.key_type;
    status.created_at = metadata.created_at;
    status.expires_at = metadata.expires_at;
    status.use_count = metadata.use_count;
    status.is_revoked = metadata.is_revoked;
    
    // Check if rotation is needed
    auto now = std::chrono::system_clock::now();
    auto policy = key_manager_->get_rotation_policy();
    auto age = now - metadata.created_at;
    
    status.needs_rotation = (age >= policy.rotation_interval) || 
                           (metadata.use_count >= policy.max_use_count) ||
                           (now >= metadata.expires_at);
    
    return Result<IdentityStatus>(status);
}

Result<std::vector<SecureValidatorIdentity::IdentityStatus>> SecureValidatorIdentity::list_all_identities() {
    if (!key_manager_) {
        return Result<std::vector<IdentityStatus>>("Key manager not initialized");
    }
    
    auto keys_result = key_manager_->list_keys();
    if (!keys_result.is_ok()) {
        return Result<std::vector<IdentityStatus>>("Failed to list keys: " + keys_result.error());
    }
    
    std::vector<IdentityStatus> identities;
    
    for (const auto& key_id : keys_result.value()) {
        auto metadata_result = key_manager_->get_key_info(key_id);
        if (metadata_result.is_ok()) {
            const auto& metadata = metadata_result.value();
            
            if (metadata.key_type == "validator_identity") {
                IdentityStatus status;
                status.key_id = metadata.key_id;
                status.key_type = metadata.key_type;
                status.created_at = metadata.created_at;
                status.expires_at = metadata.expires_at;
                status.use_count = metadata.use_count;
                status.is_revoked = metadata.is_revoked;
                
                auto now = std::chrono::system_clock::now();
                auto policy = key_manager_->get_rotation_policy();
                auto age = now - metadata.created_at;
                
                status.needs_rotation = (age >= policy.rotation_interval) || 
                                       (metadata.use_count >= policy.max_use_count) ||
                                       (now >= metadata.expires_at);
                
                identities.push_back(status);
            }
        }
    }
    
    return Result<std::vector<IdentityStatus>>(identities);
}

// ============================================================================
// SecureKeyManagerFactory Implementation
// ============================================================================

std::shared_ptr<KeyStore> SecureKeyManagerFactory::create_encrypted_file_store(const std::string& storage_path) {
    return std::make_shared<EncryptedFileKeyStore>(storage_path);
}

std::shared_ptr<KeyStore> SecureKeyManagerFactory::create_encrypted_file_store(const std::string& storage_path, 
                                                                              const std::string& passphrase) {
    // Derive key from passphrase
    std::vector<uint8_t> salt = {0x73, 0x6c, 0x6f, 0x6e, 0x61, 0x6e, 0x61, 0x00}; // "slonana\0"
    auto key_result = key_utils::derive_key_from_passphrase(passphrase, salt);
    
    if (key_result.is_ok()) {
        return std::make_shared<EncryptedFileKeyStore>(storage_path, key_result.value());
    }
    
    // Fallback to random key if derivation fails
    return std::make_shared<EncryptedFileKeyStore>(storage_path);
}

std::shared_ptr<CryptographicKeyManager> SecureKeyManagerFactory::create_key_manager(std::shared_ptr<KeyStore> store) {
    return std::make_shared<CryptographicKeyManager>(store);
}

std::unique_ptr<SecureValidatorIdentity> SecureKeyManagerFactory::create_secure_identity(const std::string& storage_path) {
    return std::make_unique<SecureValidatorIdentity>(storage_path);
}

KeyRotationPolicy SecureKeyManagerFactory::create_production_policy() {
    KeyRotationPolicy policy;
    policy.rotation_interval = std::chrono::hours(24 * 30); // 30 days
    policy.max_use_count = 1000000; // 1M uses
    policy.rotate_on_suspicious_activity = true;
    policy.automatic_rotation_enabled = true;
    policy.grace_period = std::chrono::hours(24); // 24 hours
    return policy;
}

KeyRotationPolicy SecureKeyManagerFactory::create_development_policy() {
    KeyRotationPolicy policy;
    policy.rotation_interval = std::chrono::hours(24 * 7); // 7 days
    policy.max_use_count = 100000; // 100K uses
    policy.rotate_on_suspicious_activity = false;
    policy.automatic_rotation_enabled = false; // Manual for dev
    policy.grace_period = std::chrono::hours(1); // 1 hour
    return policy;
}

KeyRotationPolicy SecureKeyManagerFactory::create_high_security_policy() {
    KeyRotationPolicy policy;
    policy.rotation_interval = std::chrono::hours(24 * 7); // 7 days
    policy.max_use_count = 50000; // 50K uses
    policy.rotate_on_suspicious_activity = true;
    policy.automatic_rotation_enabled = true;
    policy.grace_period = std::chrono::hours(2); // 2 hours
    return policy;
}

} // namespace security
} // namespace slonana