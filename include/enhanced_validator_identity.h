#pragma once

#include "security/secure_validator_identity.h"
#include "common/types.h"
#include <memory>

namespace slonana {

/**
 * Enhanced validator identity manager that integrates secure key lifecycle management
 * while maintaining backward compatibility with existing validator code
 */
class EnhancedValidatorIdentity {
private:
    std::unique_ptr<security::SecureValidatorIdentity> secure_identity_;
    std::string legacy_keypair_path_;
    bool use_secure_management_;
    
    // Legacy compatibility
    std::vector<uint8_t> cached_legacy_identity_;
    bool legacy_identity_loaded_;

public:
    explicit EnhancedValidatorIdentity(const std::string& storage_path, 
                                      bool enable_secure_management = true);
    ~EnhancedValidatorIdentity();

    // Initialization
    common::Result<bool> initialize();
    common::Result<bool> shutdown();

    // Legacy compatibility methods (maintain existing interface)
    common::Result<std::vector<uint8_t>> load_validator_identity(const std::string& keypair_path);
    std::vector<uint8_t> generate_validator_identity();

    // Enhanced secure methods
    common::Result<std::string> create_secure_identity();
    common::Result<std::string> rotate_identity_if_needed();
    common::Result<bool> enable_automatic_rotation();
    
    // Identity access
    common::Result<common::PublicKey> get_current_public_key();
    common::Result<security::SecureBuffer> get_current_private_key();
    
    // Legacy export for compatibility
    common::Result<bool> export_to_legacy_format(const std::string& output_path);
    
    // Status and monitoring
    struct IdentityInfo {
        std::string key_id;
        bool is_secure_managed;
        bool needs_rotation;
        uint64_t use_count;
        std::chrono::system_clock::time_point expires_at;
    };
    
    common::Result<IdentityInfo> get_identity_info();
    
    // Configuration
    void set_legacy_keypair_path(const std::string& path) { legacy_keypair_path_ = path; }
    void enable_secure_management(bool enable) { use_secure_management_ = enable; }
    bool is_secure_management_enabled() const { return use_secure_management_; }
    
    // Access to underlying secure identity manager
    security::SecureValidatorIdentity* get_secure_identity() const { 
        return secure_identity_.get(); 
    }
};

} // namespace slonana