#pragma once

#include "common/types.h"
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace slonana {
namespace security {

using namespace slonana::common;

// Key size constants
constexpr size_t ED25519_PRIVATE_KEY_SIZE = 32;
constexpr size_t ED25519_PUBLIC_KEY_SIZE = 32;
constexpr size_t ED25519_SIGNATURE_SIZE = 64;
constexpr size_t SOLANA_KEYPAIR_SIZE = 64; // 32 private + 32 public
constexpr size_t AES_256_KEY_SIZE = 32;
constexpr size_t DEFAULT_KEY_SIZE = 32;

// Rotation intervals
constexpr auto DEFAULT_ROTATION_INTERVAL = std::chrono::hours(24 * 30); // 30 days
constexpr auto DEVELOPMENT_ROTATION_INTERVAL = std::chrono::hours(24 * 7); // 7 days  
constexpr auto HIGH_SECURITY_ROTATION_INTERVAL = std::chrono::hours(24 * 7); // 7 days
constexpr auto DEFAULT_GRACE_PERIOD = std::chrono::hours(24); // 24 hours

// Use count limits
constexpr uint64_t DEFAULT_MAX_USE_COUNT = 1000000;
constexpr uint64_t DEVELOPMENT_MAX_USE_COUNT = 100000;
constexpr uint64_t HIGH_SECURITY_MAX_USE_COUNT = 50000;

// Thread timing
constexpr auto ROTATION_CHECK_INTERVAL = std::chrono::hours(1);
constexpr auto ROTATION_CHECK_JITTER_MAX = std::chrono::minutes(30);

// Storage constants
constexpr const char* METADATA_VERSION = "1.0";
constexpr const char* DEFAULT_STORAGE_SUBDIR = ".slonana/keys";

namespace slonana {
namespace security {

using ::slonana::common::Result;

// Forward declarations
class SecureBuffer;
class KeyStore;
class KeyRotationScheduler;

/**
 * Secure memory buffer for cryptographic material
 * Ensures memory is securely wiped on destruction
 */
class SecureBuffer {
private:
    std::vector<uint8_t> data_;
    bool is_locked_;

public:
    explicit SecureBuffer(size_t size);
    SecureBuffer(); // Default constructor
    SecureBuffer(const std::vector<uint8_t>& data);
    SecureBuffer(const SecureBuffer&) = delete; // No copy
    SecureBuffer& operator=(const SecureBuffer&) = delete; // No copy assignment
    SecureBuffer(SecureBuffer&& other) noexcept;
    SecureBuffer& operator=(SecureBuffer&& other) noexcept;
    ~SecureBuffer();

    // Data access
    uint8_t* data() { return data_.data(); }
    const uint8_t* data() const { return data_.data(); }
    size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }

    // Memory protection
    bool lock_memory();
    bool unlock_memory();
    void secure_wipe();

    // Utility
    std::vector<uint8_t> copy() const { return data_; }
    bool compare(const SecureBuffer& other) const;
    
    // Efficient access methods to reduce copying
    const std::vector<uint8_t>& get_data_ref() const { return data_; }
    std::vector<uint8_t>&& move_data() && { return std::move(data_); }
    
    // Hash function for use in containers
    size_t hash() const;
};

/**
 * Key metadata for lifecycle management with JSON serialization
 */
struct KeyMetadata {
    std::string key_id;
    std::string key_type; // "validator_identity", "session", etc.
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point expires_at;
    std::chrono::system_clock::time_point last_used;
    uint64_t use_count;
    bool is_revoked;
    std::string revocation_reason;
    std::vector<std::string> authorized_operations;
    std::string version = METADATA_VERSION; // Schema version
    
    // JSON serialization methods
    std::string to_json() const;
    static Result<KeyMetadata> from_json(const std::string& json_str);
    bool validate_schema() const;
};

/**
 * Key rotation policy configuration
 */
struct KeyRotationPolicy {
    std::chrono::seconds rotation_interval = DEFAULT_ROTATION_INTERVAL;
    uint64_t max_use_count = DEFAULT_MAX_USE_COUNT;
    bool rotate_on_suspicious_activity = true;
    bool automatic_rotation_enabled = true;
    std::chrono::seconds grace_period = DEFAULT_GRACE_PERIOD;
    bool add_jitter = true; // Add random jitter to avoid thundering herd
    std::chrono::seconds check_interval = ROTATION_CHECK_INTERVAL; // Configurable check interval
};

/**
 * Abstract interface for key storage backends
 * 
 * THREAD SAFETY: Implementations are responsible for thread safety.
 * EncryptedFileKeyStore is currently NOT thread-safe and requires external
 * synchronization if accessed from multiple threads.
 * 
 * Future HSM implementations should provide their own thread safety
 * guarantees as appropriate for the hardware.
 */
class KeyStore {
public:
    virtual ~KeyStore() = default;

    // Key operations
    virtual Result<bool> store_key(const std::string& key_id, 
                                  const SecureBuffer& key_data,
                                  const KeyMetadata& metadata) = 0;
    virtual Result<SecureBuffer> load_key(const std::string& key_id) = 0;
    virtual Result<KeyMetadata> get_key_metadata(const std::string& key_id) = 0;
    virtual Result<bool> update_metadata(const std::string& key_id, 
                                        const KeyMetadata& metadata) = 0;
    virtual Result<bool> delete_key(const std::string& key_id) = 0;
    virtual Result<std::vector<std::string>> list_keys() = 0;

    // Secure operations
    virtual Result<bool> secure_wipe_key(const std::string& key_id) = 0;
    virtual bool supports_hardware_security() const = 0;
    virtual std::string get_backend_name() const = 0;
};

/**
 * Encrypted file-based key storage implementation
 */
class EncryptedFileKeyStore : public KeyStore {
private:
    std::string storage_path_;
    SecureBuffer master_key_;
    
    std::string get_key_file_path(const std::string& key_id) const;
    std::string get_metadata_file_path(const std::string& key_id) const;
    Result<SecureBuffer> encrypt_data(const SecureBuffer& plaintext) const;
    Result<SecureBuffer> decrypt_data(const SecureBuffer& ciphertext) const;
    Result<bool> secure_delete_file(const std::string& file_path) const;

public:
    explicit EncryptedFileKeyStore(const std::string& storage_path);
    explicit EncryptedFileKeyStore(const std::string& storage_path, 
                                  const SecureBuffer& master_key);
    ~EncryptedFileKeyStore() override;

    // KeyStore interface implementation
    Result<bool> store_key(const std::string& key_id, 
                          const SecureBuffer& key_data,
                          const KeyMetadata& metadata) override;
    Result<SecureBuffer> load_key(const std::string& key_id) override;
    Result<KeyMetadata> get_key_metadata(const std::string& key_id) override;
    Result<bool> update_metadata(const std::string& key_id, 
                                const KeyMetadata& metadata) override;
    Result<bool> delete_key(const std::string& key_id) override;
    Result<std::vector<std::string>> list_keys() override;
    Result<bool> secure_wipe_key(const std::string& key_id) override;
    
    bool supports_hardware_security() const override { return false; }
    std::string get_backend_name() const override { return "EncryptedFile"; }

    // Configuration
    Result<bool> initialize_storage();
    Result<bool> change_master_key(const SecureBuffer& new_master_key);
    
    // Master key management
    Result<bool> initialize_with_passphrase(const std::string& passphrase);
    Result<bool> prompt_for_passphrase_if_needed();
    static std::string get_passphrase_from_user(const std::string& prompt);
    
    // Public encryption interface for backup operations
    Result<SecureBuffer> encrypt_data_for_backup(const SecureBuffer& plaintext) const {
        return encrypt_data(plaintext);
    }
};

/**
 * Key rotation scheduler for automated lifecycle management
 */
class KeyRotationScheduler {
private:
    std::shared_ptr<KeyStore> key_store_;
    KeyRotationPolicy policy_;
    std::unordered_map<std::string, KeyRotationPolicy> key_policies_;
    bool is_running_;
    std::chrono::steady_clock::time_point last_check_;
    std::unique_ptr<std::thread> rotation_thread_;
    std::mutex scheduler_mutex_;
    std::condition_variable scheduler_cv_;

    bool should_rotate_key(const std::string& key_id, const KeyMetadata& metadata) const;
    Result<SecureBuffer> generate_new_key(const std::string& key_type) const;
    void rotation_worker_thread(); // Background rotation worker

public:
    explicit KeyRotationScheduler(std::shared_ptr<KeyStore> key_store);
    ~KeyRotationScheduler();

    // Configuration
    void set_default_policy(const KeyRotationPolicy& policy);
    void set_key_policy(const std::string& key_id, const KeyRotationPolicy& policy);
    KeyRotationPolicy get_policy(const std::string& key_id) const;

    // Operations
    Result<bool> start_scheduler();
    void stop_scheduler();
    bool is_running() const { return is_running_; }

    // Manual operations
    Result<std::string> rotate_key(const std::string& key_id);
    Result<bool> revoke_key(const std::string& key_id, const std::string& reason);
    Result<std::vector<std::string>> check_expired_keys();
    
    // Maintenance
    void check_rotations();
    Result<bool> cleanup_expired_keys();
};

/**
 * Main cryptographic key lifecycle manager
 */
class CryptographicKeyManager {
private:
    std::shared_ptr<KeyStore> key_store_;
    std::unique_ptr<KeyRotationScheduler> rotation_scheduler_;
    KeyRotationPolicy default_policy_;
    std::string primary_validator_key_id_;
    
    Result<SecureBuffer> generate_cryptographically_secure_key(size_t key_size) const;
    std::string generate_key_id(const std::string& key_type) const;

public:
    explicit CryptographicKeyManager(std::shared_ptr<KeyStore> key_store);
    ~CryptographicKeyManager();

    // Initialization
    Result<bool> initialize();
    Result<bool> shutdown();

    // Key generation
    Result<std::string> generate_validator_identity_key();
    Result<std::string> generate_session_key();
    Result<std::string> generate_key(const std::string& key_type, size_t key_size = 32);

    // Key operations
    Result<SecureBuffer> get_key(const std::string& key_id);
    Result<KeyMetadata> get_key_info(const std::string& key_id);
    Result<bool> use_key(const std::string& key_id); // Updates usage tracking
    Result<std::vector<std::string>> list_keys() const;

    // Lifecycle management
    Result<std::string> rotate_key(const std::string& key_id);
    Result<bool> revoke_key(const std::string& key_id, const std::string& reason);
    Result<bool> extend_key_expiry(const std::string& key_id, 
                                  std::chrono::seconds extension);

    // Legacy compatibility
    Result<std::string> import_legacy_key(const std::string& file_path, 
                                         const std::string& key_type);
    Result<bool> export_key_for_backup(const std::string& key_id, 
                                      const std::string& backup_path);

    // Configuration
    void set_rotation_policy(const KeyRotationPolicy& policy);
    KeyRotationPolicy get_rotation_policy() const { return default_policy_; }

    // Primary validator key management
    Result<bool> set_primary_validator_key(const std::string& key_id);
    Result<SecureBuffer> get_primary_validator_key();
    std::string get_primary_validator_key_id() const { return primary_validator_key_id_; }

    // Status and monitoring
    struct KeyManagerStats {
        uint64_t total_keys;
        uint64_t active_keys;
        uint64_t expired_keys;
        uint64_t revoked_keys;
        uint64_t rotations_performed;
        std::chrono::system_clock::time_point last_rotation;
        std::chrono::system_clock::time_point next_scheduled_rotation;
    };
    KeyManagerStats get_stats() const;

    // Security operations
    Result<bool> secure_wipe_all_revoked_keys();
    Result<bool> emergency_revoke_all_keys(const std::string& reason);
    Result<std::vector<std::string>> audit_key_usage();
};

// Utility functions
namespace key_utils {
    /**
     * Generate cryptographically secure random bytes
     */
    Result<SecureBuffer> generate_secure_random(size_t size);
    
    /**
     * Derive key from passphrase using PBKDF2
     */
    Result<SecureBuffer> derive_key_from_passphrase(const std::string& passphrase, 
                                                   const std::vector<uint8_t>& salt,
                                                   uint32_t iterations = 100000);
    
    /**
     * Secure memory comparison
     */
    bool secure_compare(const uint8_t* a, const uint8_t* b, size_t size);
    
    /**
     * Generate unique key identifier
     */
    std::string generate_key_id(const std::string& prefix = "key");
    
    /**
     * Validate key strength and format
     */
    bool validate_key_strength(const SecureBuffer& key);
    
    /**
     * Get system entropy quality
     */
    bool check_entropy_quality();
}

} // namespace security
} // namespace slonana