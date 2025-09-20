#include "security/key_manager.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>

#ifdef __linux__
#include <sys/mman.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/kdf.h>

namespace slonana {
namespace security {

// ============================================================================
// SecureBuffer Implementation
// ============================================================================

SecureBuffer::SecureBuffer(size_t size) : data_(size), is_locked_(false) {
    if (size > 0) {
        lock_memory();
    }
}

SecureBuffer::SecureBuffer() : data_(), is_locked_(false) {
    // Empty buffer constructor
}

SecureBuffer::SecureBuffer(const std::vector<uint8_t>& data) 
    : data_(data), is_locked_(false) {
    if (!data_.empty()) {
        lock_memory();
    }
}

SecureBuffer::SecureBuffer(SecureBuffer&& other) noexcept 
    : data_(std::move(other.data_)), is_locked_(other.is_locked_) {
    other.is_locked_ = false;
}

SecureBuffer& SecureBuffer::operator=(SecureBuffer&& other) noexcept {
    if (this != &other) {
        secure_wipe();
        if (is_locked_) {
            unlock_memory();
        }
        
        data_ = std::move(other.data_);
        is_locked_ = other.is_locked_;
        other.is_locked_ = false;
    }
    return *this;
}

SecureBuffer::~SecureBuffer() {
    secure_wipe();
    if (is_locked_) {
        unlock_memory();
    }
}

bool SecureBuffer::lock_memory() {
    if (data_.empty() || is_locked_) {
        return is_locked_;
    }

#ifdef __linux__
    if (mlock(data_.data(), data_.size()) == 0) {
        is_locked_ = true;
    }
#elif defined(_WIN32)
    if (VirtualLock(data_.data(), data_.size())) {
        is_locked_ = true;
    }
#endif
    
    return is_locked_;
}

bool SecureBuffer::unlock_memory() {
    if (!is_locked_ || data_.empty()) {
        return true;
    }

#ifdef __linux__
    if (munlock(data_.data(), data_.size()) == 0) {
        is_locked_ = false;
    }
#elif defined(_WIN32)
    if (VirtualUnlock(data_.data(), data_.size())) {
        is_locked_ = false;
    }
#endif
    
    return !is_locked_;
}

void SecureBuffer::secure_wipe() {
    if (!data_.empty()) {
        // Use OpenSSL's secure memory clearing
        OPENSSL_cleanse(data_.data(), data_.size());
        
        // Additional explicit zeroing as fallback
        volatile uint8_t* ptr = data_.data();
        for (size_t i = 0; i < data_.size(); ++i) {
            ptr[i] = 0;
        }
    }
}

bool SecureBuffer::compare(const SecureBuffer& other) const {
    if (data_.size() != other.data_.size()) {
        return false;
    }
    
    return key_utils::secure_compare(data_.data(), other.data_.data(), data_.size());
}

// ============================================================================
// EncryptedFileKeyStore Implementation
// ============================================================================

EncryptedFileKeyStore::EncryptedFileKeyStore(const std::string& storage_path)
    : storage_path_(storage_path) {
    // Generate a random master key - in production this should be derived from user input
    auto key_result = key_utils::generate_secure_random(32);
    if (key_result.is_ok()) {
        master_key_ = std::move(key_result.value());
    }
}

EncryptedFileKeyStore::EncryptedFileKeyStore(const std::string& storage_path, 
                                           const SecureBuffer& master_key)
    : storage_path_(storage_path), master_key_(master_key) {
}

EncryptedFileKeyStore::~EncryptedFileKeyStore() {
    // Secure cleanup is handled by SecureBuffer destructor
}

std::string EncryptedFileKeyStore::get_key_file_path(const std::string& key_id) const {
    return storage_path_ + "/" + key_id + ".key";
}

std::string EncryptedFileKeyStore::get_metadata_file_path(const std::string& key_id) const {
    return storage_path_ + "/" + key_id + ".meta";
}

Result<SecureBuffer> EncryptedFileKeyStore::encrypt_data(const SecureBuffer& plaintext) const {
    if (plaintext.empty() || master_key_.empty()) {
        return Result<SecureBuffer>("Invalid input for encryption");
    }

    // AES-256-GCM encryption
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return Result<SecureBuffer>("Failed to create cipher context");
    }

    // Generate random IV
    std::vector<uint8_t> iv(12); // GCM standard IV size
    if (RAND_bytes(iv.data(), iv.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return Result<SecureBuffer>("Failed to generate IV");
    }

    // Initialize encryption
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, master_key_.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return Result<SecureBuffer>("Failed to initialize encryption");
    }

    // Prepare output buffer: IV (12) + ciphertext + tag (16)
    SecureBuffer ciphertext(iv.size() + plaintext.size() + 16);
    
    // Copy IV to beginning
    std::copy(iv.begin(), iv.end(), ciphertext.data());
    
    int len;
    int ciphertext_len = 0;
    
    // Encrypt data
    if (EVP_EncryptUpdate(ctx, ciphertext.data() + iv.size(), &len, 
                         plaintext.data(), plaintext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return Result<SecureBuffer>("Encryption failed");
    }
    ciphertext_len = len;
    
    // Finalize encryption
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + iv.size() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return Result<SecureBuffer>("Encryption finalization failed");
    }
    ciphertext_len += len;
    
    // Get authentication tag
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, 
                           ciphertext.data() + iv.size() + ciphertext_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return Result<SecureBuffer>("Failed to get authentication tag");
    }
    
    EVP_CIPHER_CTX_free(ctx);
    return Result<SecureBuffer>(std::move(ciphertext));
}

Result<SecureBuffer> EncryptedFileKeyStore::decrypt_data(const SecureBuffer& ciphertext) const {
    if (ciphertext.size() < 28 || master_key_.empty()) { // IV(12) + min_data(0) + tag(16)
        return Result<SecureBuffer>("Invalid ciphertext size");
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return Result<SecureBuffer>("Failed to create cipher context");
    }

    // Extract IV (first 12 bytes)
    const uint8_t* iv = ciphertext.data();
    const uint8_t* encrypted_data = ciphertext.data() + 12;
    size_t encrypted_size = ciphertext.size() - 28; // Total - IV - tag
    const uint8_t* tag = ciphertext.data() + ciphertext.size() - 16;

    // Initialize decryption
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, master_key_.data(), iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return Result<SecureBuffer>("Failed to initialize decryption");
    }

    SecureBuffer plaintext(encrypted_size);
    int len;
    int plaintext_len = 0;

    // Decrypt data
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, encrypted_data, encrypted_size) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return Result<SecureBuffer>("Decryption failed");
    }
    plaintext_len = len;

    // Set authentication tag
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void*)tag) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return Result<SecureBuffer>("Failed to set authentication tag");
    }

    // Finalize decryption (this verifies the tag)
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return Result<SecureBuffer>("Decryption verification failed");
    }
    plaintext_len += len;

    EVP_CIPHER_CTX_free(ctx);
    
    // Resize to actual plaintext size
    SecureBuffer result(plaintext_len);
    std::copy(plaintext.data(), plaintext.data() + plaintext_len, result.data());
    
    return Result<SecureBuffer>(std::move(result));
}

Result<bool> EncryptedFileKeyStore::store_key(const std::string& key_id, 
                                            const SecureBuffer& key_data,
                                            const KeyMetadata& metadata) {
    // Encrypt key data
    auto encrypted_result = encrypt_data(key_data);
    if (!encrypted_result.is_ok()) {
        return Result<bool>("Failed to encrypt key: " + encrypted_result.error());
    }

    // Write encrypted key to file
    std::string key_path = get_key_file_path(key_id);
    std::ofstream key_file(key_path, std::ios::binary);
    if (!key_file) {
        return Result<bool>("Failed to open key file for writing");
    }

    const auto& encrypted_data = encrypted_result.value();
    key_file.write(reinterpret_cast<const char*>(encrypted_data.data()), encrypted_data.size());
    key_file.close();

    // Write metadata (you would serialize KeyMetadata here)
    // For now, simplified implementation
    std::string meta_path = get_metadata_file_path(key_id);
    std::ofstream meta_file(meta_path);
    if (!meta_file) {
        return Result<bool>("Failed to open metadata file for writing");
    }

    // Simple serialization of metadata
    meta_file << "key_id=" << metadata.key_id << "\n";
    meta_file << "key_type=" << metadata.key_type << "\n";
    meta_file << "created_at=" << std::chrono::duration_cast<std::chrono::seconds>(
        metadata.created_at.time_since_epoch()).count() << "\n";
    meta_file << "expires_at=" << std::chrono::duration_cast<std::chrono::seconds>(
        metadata.expires_at.time_since_epoch()).count() << "\n";
    meta_file << "use_count=" << metadata.use_count << "\n";
    meta_file << "is_revoked=" << (metadata.is_revoked ? "true" : "false") << "\n";
    meta_file.close();

    return Result<bool>(true);
}

Result<SecureBuffer> EncryptedFileKeyStore::load_key(const std::string& key_id) {
    std::string key_path = get_key_file_path(key_id);
    std::ifstream key_file(key_path, std::ios::binary);
    if (!key_file) {
        return Result<SecureBuffer>("Key file not found");
    }

    // Read encrypted data
    key_file.seekg(0, std::ios::end);
    size_t file_size = key_file.tellg();
    key_file.seekg(0, std::ios::beg);

    SecureBuffer encrypted_data(file_size);
    key_file.read(reinterpret_cast<char*>(encrypted_data.data()), file_size);
    key_file.close();

    // Decrypt key data
    return decrypt_data(encrypted_data);
}

Result<KeyMetadata> EncryptedFileKeyStore::get_key_metadata(const std::string& key_id) {
    std::string meta_path = get_metadata_file_path(key_id);
    std::ifstream meta_file(meta_path);
    if (!meta_file) {
        return Result<KeyMetadata>("Metadata file not found");
    }

    KeyMetadata metadata;
    std::string line;
    while (std::getline(meta_file, line)) {
        size_t eq_pos = line.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = line.substr(0, eq_pos);
            std::string value = line.substr(eq_pos + 1);
            
            if (key == "key_id") {
                metadata.key_id = value;
            } else if (key == "key_type") {
                metadata.key_type = value;
            } else if (key == "created_at") {
                auto timestamp = std::chrono::seconds(std::stoull(value));
                metadata.created_at = std::chrono::system_clock::time_point(timestamp);
            } else if (key == "expires_at") {
                auto timestamp = std::chrono::seconds(std::stoull(value));
                metadata.expires_at = std::chrono::system_clock::time_point(timestamp);
            } else if (key == "use_count") {
                metadata.use_count = std::stoull(value);
            } else if (key == "is_revoked") {
                metadata.is_revoked = (value == "true");
            }
        }
    }

    return Result<KeyMetadata>(metadata);
}

Result<bool> EncryptedFileKeyStore::update_metadata(const std::string& key_id, 
                                                   const KeyMetadata& metadata) {
    // Reuse store logic for metadata update
    SecureBuffer dummy_key(1); // We don't actually update the key
    auto current_key = load_key(key_id);
    if (!current_key.is_ok()) {
        return Result<bool>("Key not found for metadata update");
    }
    
    return store_key(key_id, current_key.value(), metadata);
}

Result<bool> EncryptedFileKeyStore::delete_key(const std::string& key_id) {
    std::string key_path = get_key_file_path(key_id);
    std::string meta_path = get_metadata_file_path(key_id);
    
    bool key_deleted = secure_delete_file(key_path).is_ok();
    bool meta_deleted = secure_delete_file(meta_path).is_ok();
    
    return Result<bool>(key_deleted && meta_deleted);
}

Result<std::vector<std::string>> EncryptedFileKeyStore::list_keys() {
    // This is a simplified implementation
    // In production, you'd properly scan the directory
    std::vector<std::string> keys;
    return Result<std::vector<std::string>>(keys);
}

Result<bool> EncryptedFileKeyStore::secure_wipe_key(const std::string& key_id) {
    return delete_key(key_id); // Current implementation is the same
}

Result<bool> EncryptedFileKeyStore::initialize_storage() {
    // Create storage directory if it doesn't exist
    std::filesystem::path storage_dir(storage_path_);
    
    if (!std::filesystem::exists(storage_dir)) {
        std::error_code ec;
        if (!std::filesystem::create_directories(storage_dir, ec)) {
            return Result<bool>("Failed to create storage directory: " + ec.message());
        }
        
        // Set restrictive permissions (owner only)
        std::filesystem::permissions(storage_dir, 
                                   std::filesystem::perms::owner_read | 
                                   std::filesystem::perms::owner_write | 
                                   std::filesystem::perms::owner_exec,
                                   std::filesystem::perm_options::replace, ec);
        if (ec) {
            return Result<bool>("Failed to set directory permissions: " + ec.message());
        }
    }
    
    return Result<bool>(true);
}

Result<bool> EncryptedFileKeyStore::change_master_key(const SecureBuffer& new_master_key) {
    if (new_master_key.size() != 32) {
        return Result<bool>("Master key must be 32 bytes");
    }
    
    // In a production implementation, you would:
    // 1. Re-encrypt all existing keys with the new master key
    // 2. Securely wipe the old master key
    // For this minimal implementation, we'll just update the key
    
    SecureBuffer old_master_key = std::move(master_key_);
    master_key_ = new_master_key;
    
    return Result<bool>(true);
}

Result<bool> EncryptedFileKeyStore::secure_delete_file(const std::string& file_path) const {
    // First, overwrite the file with random data
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file) {
        return Result<bool>(true); // File doesn't exist, consider it deleted
    }
    
    size_t file_size = file.tellg();
    file.close();
    
    if (file_size > 0) {
        std::ofstream out_file(file_path, std::ios::binary);
        if (out_file) {
            std::vector<uint8_t> random_data(file_size);
            RAND_bytes(random_data.data(), file_size);
            out_file.write(reinterpret_cast<const char*>(random_data.data()), file_size);
            out_file.close();
        }
    }
    
    // Now delete the file
    if (std::remove(file_path.c_str()) == 0) {
        return Result<bool>(true);
    }
    
    return Result<bool>("Failed to delete file");
}

// ============================================================================
// Key Utility Functions
// ============================================================================

namespace key_utils {

Result<SecureBuffer> generate_secure_random(size_t size) {
    if (size == 0) {
        return Result<SecureBuffer>("Invalid size for random generation");
    }

    SecureBuffer buffer(size);
    
    // Use OpenSSL's cryptographically secure random number generator
    if (RAND_bytes(buffer.data(), size) != 1) {
        return Result<SecureBuffer>("Failed to generate secure random bytes");
    }
    
    return Result<SecureBuffer>(std::move(buffer));
}

Result<SecureBuffer> derive_key_from_passphrase(const std::string& passphrase, 
                                               const std::vector<uint8_t>& salt,
                                               uint32_t iterations) {
    if (passphrase.empty() || salt.empty()) {
        return Result<SecureBuffer>("Invalid passphrase or salt");
    }

    SecureBuffer derived_key(32); // 256 bits
    
    if (PKCS5_PBKDF2_HMAC(passphrase.c_str(), passphrase.length(),
                          salt.data(), salt.size(),
                          iterations,
                          EVP_sha256(),
                          derived_key.size(),
                          derived_key.data()) != 1) {
        return Result<SecureBuffer>("Key derivation failed");
    }
    
    return Result<SecureBuffer>(std::move(derived_key));
}

bool secure_compare(const uint8_t* a, const uint8_t* b, size_t size) {
    // Use OpenSSL's constant-time comparison
    return CRYPTO_memcmp(a, b, size) == 0;
}

std::string generate_key_id(const std::string& prefix) {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    // Generate some random bytes for uniqueness
    std::vector<uint8_t> random_bytes(8);
    RAND_bytes(random_bytes.data(), random_bytes.size());
    
    std::stringstream ss;
    ss << prefix << "_" << timestamp << "_";
    for (auto byte : random_bytes) {
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte);
    }
    
    return ss.str();
}

bool validate_key_strength(const SecureBuffer& key) {
    if (key.size() < 16) { // Minimum 128 bits
        return false;
    }
    
    // Check for all zeros
    bool all_zeros = true;
    for (size_t i = 0; i < key.size(); ++i) {
        if (key.data()[i] != 0) {
            all_zeros = false;
            break;
        }
    }
    
    return !all_zeros;
}

bool check_entropy_quality() {
    // Simple entropy check - in production you'd want more sophisticated checks
    std::vector<uint8_t> test_data(64);
    if (RAND_bytes(test_data.data(), test_data.size()) != 1) {
        return false;
    }
    
    // Basic entropy test - check for patterns
    std::unordered_map<uint8_t, int> frequency;
    for (auto byte : test_data) {
        frequency[byte]++;
    }
    
    // If any byte appears too frequently, entropy might be low
    for (const auto& pair : frequency) {
        if (pair.second > 8) { // More than 12.5% frequency
            return false;
        }
    }
    
    return true;
}

} // namespace key_utils

} // namespace security
} // namespace slonana