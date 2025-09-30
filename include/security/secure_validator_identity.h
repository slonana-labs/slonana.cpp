#pragma once

#include "security/key_manager.h"
#include "common/types.h"
#include <memory>
#include <string>

namespace slonana {
namespace security {

/**
 * Secure validator identity manager that integrates with the new key lifecycle system
 */
class SecureValidatorIdentity {
private:
    std::shared_ptr<CryptographicKeyManager> key_manager_;
    std::string primary_identity_key_id_;
    std::string storage_path_;
    
public:
    explicit SecureValidatorIdentity(const std::string& storage_path);
    ~SecureValidatorIdentity();

    // Initialization
    Result<bool> initialize();
    Result<bool> shutdown();

    // Key operations
    Result<std::string> create_new_identity();
    Result<std::string> import_legacy_identity(const std::string& keypair_path);
    Result<bool> set_primary_identity(const std::string& key_id);
    
    // Identity access
    Result<common::PublicKey> get_public_key();
    Result<SecureBuffer> get_private_key();
    std::string get_primary_key_id() const { return primary_identity_key_id_; }
    
    // Lifecycle management
    Result<std::string> rotate_identity();
    Result<bool> revoke_identity(const std::string& reason);
    
    // Legacy compatibility
    Result<common::PublicKey> generate_legacy_compatible_identity();
    Result<bool> export_for_legacy_use(const std::string& output_path);
    
    // Status
    struct IdentityStatus {
        std::string key_id;
        std::string key_type;
        std::chrono::system_clock::time_point created_at;
        std::chrono::system_clock::time_point expires_at;
        uint64_t use_count;
        bool is_revoked;
        bool needs_rotation;
    };
    
    Result<IdentityStatus> get_status();
    Result<std::vector<IdentityStatus>> list_all_identities();
    
    // Access key manager for advanced operations
    std::shared_ptr<CryptographicKeyManager> get_key_manager() const { 
        return key_manager_; 
    }
};

/**
 * Factory functions for creating secure key management components
 */
class SecureKeyManagerFactory {
public:
    static std::shared_ptr<KeyStore> create_encrypted_file_store(const std::string& storage_path);
    static std::shared_ptr<KeyStore> create_encrypted_file_store(const std::string& storage_path, 
                                                               const std::string& passphrase);
    static std::shared_ptr<CryptographicKeyManager> create_key_manager(std::shared_ptr<KeyStore> store);
    static std::unique_ptr<SecureValidatorIdentity> create_secure_identity(const std::string& storage_path);
    
    // Configuration helpers
    static KeyRotationPolicy create_production_policy();
    static KeyRotationPolicy create_development_policy();
    static KeyRotationPolicy create_high_security_policy();
};

} // namespace security
} // namespace slonana