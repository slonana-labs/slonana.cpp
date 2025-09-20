#include "enhanced_validator_identity.h"
#include <fstream>
#include <random>
#include <algorithm>
#include <iostream>
#include <openssl/rand.h>

namespace slonana {

EnhancedValidatorIdentity::EnhancedValidatorIdentity(const std::string& storage_path, 
                                                   bool enable_secure_management)
    : use_secure_management_(enable_secure_management), legacy_identity_loaded_(false) {
    
    if (use_secure_management_) {
        secure_identity_ = security::SecureKeyManagerFactory::create_secure_identity(storage_path);
    }
}

EnhancedValidatorIdentity::~EnhancedValidatorIdentity() {
    shutdown();
}

common::Result<bool> EnhancedValidatorIdentity::initialize() {
    if (use_secure_management_ && secure_identity_) {
        auto init_result = secure_identity_->initialize();
        if (!init_result.is_ok()) {
            // Fallback to legacy mode if secure initialization fails
            std::cout << "⚠️  Secure key management initialization failed, falling back to legacy mode: " 
                      << init_result.error() << "\n";
            use_secure_management_ = false;
            return common::Result<bool>(true);
        }
        
        std::cout << "✅ Secure key lifecycle management initialized\n";
        return common::Result<bool>(true);
    }
    
    return common::Result<bool>(true);
}

common::Result<bool> EnhancedValidatorIdentity::shutdown() {
    if (secure_identity_) {
        return secure_identity_->shutdown();
    }
    return common::Result<bool>(true);
}

common::Result<std::vector<uint8_t>> EnhancedValidatorIdentity::load_validator_identity(const std::string& keypair_path) {
    legacy_keypair_path_ = keypair_path;
    
    if (use_secure_management_ && secure_identity_) {
        // Try to import the legacy key into secure management
        auto import_result = secure_identity_->import_legacy_identity(keypair_path);
        if (import_result.is_ok()) {
            std::cout << "✅ Legacy keypair imported into secure key management: " 
                      << import_result.value() << "\
";
            
            // Return public key in legacy format
            auto pub_key_result = secure_identity_->get_public_key();
            if (pub_key_result.is_ok()) {
                cached_legacy_identity_ = pub_key_result.value();
                legacy_identity_loaded_ = true;
                return common::Result<std::vector<uint8_t>>(cached_legacy_identity_);
            }
        } else {
            std::cout << "⚠️  Failed to import legacy key into secure management: " 
                      << import_result.error() << "\
";
        }
    }
    
    // Fallback to original legacy implementation
    try {
        std::ifstream file(keypair_path, std::ios::binary);
        if (!file) {
            return common::Result<std::vector<uint8_t>>("Failed to open keypair file");
        }

        // Read keypair file (32 bytes for public key, 32 bytes for private key)
        std::vector<uint8_t> keypair_data(64);
        file.read(reinterpret_cast<char *>(keypair_data.data()), 64);

        if (file.gcount() != 64) {
            return common::Result<std::vector<uint8_t>>("Invalid keypair file size");
        }

        // Extract public key (first 32 bytes)
        std::vector<uint8_t> public_key(keypair_data.begin(), keypair_data.begin() + 32);
        
        cached_legacy_identity_ = public_key;
        legacy_identity_loaded_ = true;
        
        std::cout << "Loaded validator identity from keypair file (legacy mode)" << "\
";
        return common::Result<std::vector<uint8_t>>(public_key);

    } catch (const std::exception &e) {
        return common::Result<std::vector<uint8_t>>(
            std::string("Failed to load keypair: ") + e.what());
    }
}

std::vector<uint8_t> EnhancedValidatorIdentity::generate_validator_identity() {
    if (use_secure_management_ && secure_identity_) {
        // Use secure key generation
        auto create_result = secure_identity_->create_new_identity();
        if (create_result.is_ok()) {
            std::cout << "✅ Generated secure validator identity: " << create_result.value() << "\
";
            
            auto pub_key_result = secure_identity_->get_public_key();
            if (pub_key_result.is_ok()) {
                cached_legacy_identity_ = pub_key_result.value();
                legacy_identity_loaded_ = true;
                return cached_legacy_identity_;
            }
        } else {
            std::cout << "⚠️  Secure identity generation failed: " << create_result.error() << "\
";
        }
    }
    
    // Fallback to original implementation (but with better randomness)
    std::vector<uint8_t> identity(32);

    // Use OpenSSL for better randomness even in legacy mode
    if (RAND_bytes(identity.data(), identity.size()) != 1) {
        // Fallback to std::random_device if OpenSSL fails
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);

        for (size_t i = 0; i < 32; ++i) {
            identity[i] = static_cast<uint8_t>(dis(gen));
        }
    }

    // Ensure identity is not all zeros
    if (std::all_of(identity.begin(), identity.end(), [](uint8_t b) { return b == 0; })) {
        identity[0] = 1; // Prevent all-zero identity
    }

    cached_legacy_identity_ = identity;
    legacy_identity_loaded_ = true;

    std::cout << "Generated new 32-byte validator identity (enhanced legacy mode)" << "\
";
    return identity;
}

common::Result<std::string> EnhancedValidatorIdentity::create_secure_identity() {
    if (!use_secure_management_ || !secure_identity_) {
        return common::Result<std::string>("Not enabled");
    }
    
    auto result = secure_identity_->create_new_identity();
    if (result.is_ok()) {
        // Update cached legacy identity for compatibility
        auto pub_key_result = secure_identity_->get_public_key();
        if (pub_key_result.is_ok()) {
            cached_legacy_identity_ = pub_key_result.value();
            legacy_identity_loaded_ = true;
        }
    }
    
    return result;
}

common::Result<std::string> EnhancedValidatorIdentity::rotate_identity_if_needed() {
    if (!use_secure_management_ || !secure_identity_) {
        return common::Result<std::string>("Not enabled");
    }
    
    // Check if rotation is needed
    auto status_result = secure_identity_->get_status();
    if (!status_result.is_ok()) {
        return common::Result<std::string>("Check failed");
    }
    
    if (!status_result.value().needs_rotation) {
        std::string msg = "No rotation needed";
        return common::Result<std::string>(std::move(msg), common::success_tag{});
    }
    
    std::cout << "🔄 Identity rotation needed, performing automatic rotation..." << "\
";
    
    auto rotate_result = secure_identity_->rotate_identity();
    if (rotate_result.is_ok()) {
        // Update cached legacy identity
        auto pub_key_result = secure_identity_->get_public_key();
        if (pub_key_result.is_ok()) {
            cached_legacy_identity_ = pub_key_result.value();
            legacy_identity_loaded_ = true;
        }
        
        std::cout << "✅ Identity rotated successfully: " << rotate_result.value() << "\
";
    }
    
    return rotate_result;
}

common::Result<bool> EnhancedValidatorIdentity::enable_automatic_rotation() {
    if (!use_secure_management_ || !secure_identity_) {
        return common::Result<bool>("Secure key management not enabled");
    }
    
    // Set up production rotation policy
    auto key_manager = secure_identity_->get_key_manager();
    if (key_manager) {
        auto policy = security::SecureKeyManagerFactory::create_production_policy();
        key_manager->set_rotation_policy(policy);
        
        std::cout << "✅ Automatic key rotation enabled with production policy" << "\
";
        return common::Result<bool>(true);
    }
    
    return common::Result<bool>("Failed to access key manager");
}

common::Result<common::PublicKey> EnhancedValidatorIdentity::get_current_public_key() {
    if (use_secure_management_ && secure_identity_) {
        return secure_identity_->get_public_key();
    }
    
    // Legacy mode
    if (legacy_identity_loaded_) {
        return common::Result<common::PublicKey>(cached_legacy_identity_);
    }
    
    return common::Result<common::PublicKey>("No identity loaded");
}

common::Result<security::SecureBuffer> EnhancedValidatorIdentity::get_current_private_key() {
    if (use_secure_management_ && secure_identity_) {
        return secure_identity_->get_private_key();
    }
    
    return common::Result<security::SecureBuffer>("Private key access requires secure management mode");
}

common::Result<bool> EnhancedValidatorIdentity::export_to_legacy_format(const std::string& output_path) {
    if (use_secure_management_ && secure_identity_) {
        return secure_identity_->export_for_legacy_use(output_path);
    }
    
    // Legacy mode export
    if (!legacy_identity_loaded_ || legacy_keypair_path_.empty()) {
        return common::Result<bool>("No identity to export in legacy mode");
    }
    
    // Simply copy the original file if available
    std::ifstream src(legacy_keypair_path_, std::ios::binary);
    std::ofstream dst(output_path, std::ios::binary);
    
    if (!src || !dst) {
        return common::Result<bool>("Failed to open files for export");
    }
    
    dst << src.rdbuf();
    return common::Result<bool>(true);
}

common::Result<EnhancedValidatorIdentity::IdentityInfo> EnhancedValidatorIdentity::get_identity_info() {
    IdentityInfo info = {};
    info.is_secure_managed = use_secure_management_;
    
    if (use_secure_management_ && secure_identity_) {
        auto status_result = secure_identity_->get_status();
        if (status_result.is_ok()) {
            const auto& status = status_result.value();
            info.key_id = status.key_id;
            info.needs_rotation = status.needs_rotation;
            info.use_count = status.use_count;
            info.expires_at = status.expires_at;
        } else {
            return common::Result<IdentityInfo>("Failed to get secure identity status: " + status_result.error());
        }
    } else {
        info.key_id = "legacy_identity";
        info.needs_rotation = false; // Legacy doesn't support rotation
        info.use_count = 0;
        info.expires_at = std::chrono::system_clock::time_point::max();
    }
    
    return common::Result<IdentityInfo>(info);
}

} // namespace slonana