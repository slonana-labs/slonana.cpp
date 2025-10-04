#include "security/key_manager.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <filesystem>
#include <cmath>
#include <nlohmann/json.hpp>
#include <sodium.h>
#include <unistd.h>
#include <termios.h>
#include <cstdlib>

using json = nlohmann::json;

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

// JSON serialization for KeyMetadata
std::string KeyMetadata::to_json() const {
    json j;
    j["version"] = version;
    j["key_id"] = key_id;
    j["key_type"] = key_type;
    j["created_at"] = std::chrono::duration_cast<std::chrono::seconds>(
        created_at.time_since_epoch()).count();
    j["expires_at"] = std::chrono::duration_cast<std::chrono::seconds>(
        expires_at.time_since_epoch()).count();
    j["last_used"] = std::chrono::duration_cast<std::chrono::seconds>(
        last_used.time_since_epoch()).count();
    j["use_count"] = use_count;
    j["is_revoked"] = is_revoked;
    j["revocation_reason"] = revocation_reason;
    j["authorized_operations"] = authorized_operations;
    
    return j.dump(2); // Pretty print with 2-space indentation
}

Result<KeyMetadata> KeyMetadata::from_json(const std::string& json_str) {
    try {
        json j = json::parse(json_str);
        
        KeyMetadata metadata;
        
        // Check version compatibility
        if (j.contains("version")) {
            std::string file_version = j["version"];
            if (file_version != METADATA_VERSION) {
                // For now, we'll try to read old versions but warn
                // In production, implement proper migration logic
            }
        }
        
        metadata.version = j.value("version", METADATA_VERSION);
        metadata.key_id = j.value("key_id", "");
        metadata.key_type = j.value("key_type", "");
        
        // Parse timestamps
        if (j.contains("created_at")) {
            auto seconds = j["created_at"].get<int64_t>();
            metadata.created_at = std::chrono::system_clock::from_time_t(seconds);
        }
        if (j.contains("expires_at")) {
            auto seconds = j["expires_at"].get<int64_t>();
            metadata.expires_at = std::chrono::system_clock::from_time_t(seconds);
        }
        if (j.contains("last_used")) {
            auto seconds = j["last_used"].get<int64_t>();
            metadata.last_used = std::chrono::system_clock::from_time_t(seconds);
        }
        
        metadata.use_count = j.value("use_count", 0UL);
        metadata.is_revoked = j.value("is_revoked", false);
        metadata.revocation_reason = j.value("revocation_reason", "");
        metadata.authorized_operations = j.value("authorized_operations", std::vector<std::string>{});
        
        if (!metadata.validate_schema()) {
            return Result<KeyMetadata>("Invalid metadata schema");
        }
        
        return Result<KeyMetadata>(std::move(metadata), success_tag{});
    } catch (const json::exception& e) {
        return Result<KeyMetadata>("JSON parsing error: " + std::string(e.what()));
    }
}

bool KeyMetadata::validate_schema() const {
    // Basic schema validation
    if (key_id.empty() || key_type.empty()) {
        return false;
    }
    
    // Validate timestamps
    if (created_at > expires_at) {
        return false;
    }
    
    // Validate key type
    const std::vector<std::string> valid_types = {
        "validator_identity", "session", "backup", "temporary"
    };
    if (std::find(valid_types.begin(), valid_types.end(), key_type) == valid_types.end()) {
        return false;
    }
    
    return true;
}

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

size_t SecureBuffer::hash() const {
    size_t seed = data_.size();
    for (auto byte : data_) {
        seed ^= byte + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    return seed;
}

// ============================================================================
// EncryptedFileKeyStore Implementation
// ============================================================================

EncryptedFileKeyStore::EncryptedFileKeyStore(const std::string& storage_path)
    : storage_path_(storage_path), master_key_(AES_256_KEY_SIZE) { // Initialize with proper size
    
    // Try to prompt for passphrase if in interactive mode
    if (prompt_for_passphrase_if_needed().is_ok()) {
        // Successfully initialized with passphrase
        return;
    }
    
    // Fallback: Generate a random master key with user warning
    auto key_result = key_utils::generate_secure_random(AES_256_KEY_SIZE);
    if (key_result.is_ok()) {
        auto key_data = std::move(key_result).value();
        std::copy(key_data.data(), key_data.data() + key_data.size(), master_key_.data());
        
        // Warn user about random key generation
        std::cerr << "⚠️  WARNING: Generated random master key. Keys may be unrecoverable if storage is lost!" << std::endl;
        std::cerr << "    Consider using initialize_with_passphrase() for production use." << std::endl;
    }
}

EncryptedFileKeyStore::EncryptedFileKeyStore(const std::string& storage_path, 
                                           const SecureBuffer& master_key)
    : storage_path_(storage_path), master_key_(master_key.copy()) { // Use copy method
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

    // Read the entire JSON file
    std::ostringstream json_stream;
    json_stream << meta_file.rdbuf();
    std::string json_content = json_stream.str();
    meta_file.close();

    // Parse JSON metadata
    return KeyMetadata::from_json(json_content);
}

Result<bool> EncryptedFileKeyStore::update_metadata(const std::string& key_id, 
                                                   const KeyMetadata& metadata) {
    // Validate metadata before writing
    if (!metadata.validate_schema()) {
        return Result<bool>("Invalid metadata schema");
    }
    
    // Write JSON metadata directly without touching the key file
    std::string meta_path = get_metadata_file_path(key_id);
    std::ofstream meta_file(meta_path);
    if (!meta_file) {
        return Result<bool>("Failed to open metadata file for writing");
    }

    // Serialize to JSON
    std::string json_content = metadata.to_json();
    meta_file << json_content;
    meta_file.close();

    return Result<bool>(true);
}

Result<bool> EncryptedFileKeyStore::delete_key(const std::string& key_id) {
    std::string key_path = get_key_file_path(key_id);
    std::string meta_path = get_metadata_file_path(key_id);
    
    bool key_deleted = secure_delete_file(key_path).is_ok();
    bool meta_deleted = secure_delete_file(meta_path).is_ok();
    
    return Result<bool>(key_deleted && meta_deleted);
}

Result<std::vector<std::string>> EncryptedFileKeyStore::list_keys() {
    std::vector<std::string> keys;
    
    try {
        if (!std::filesystem::exists(storage_path_)) {
            return Result<std::vector<std::string>>(keys); // Return empty list if directory doesn't exist
        }
        
        // Scan directory for .key files and extract key IDs
        for (const auto& entry : std::filesystem::directory_iterator(storage_path_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".key") {
                std::string filename = entry.path().stem().string();
                
                // Verify corresponding metadata file exists
                std::string meta_path = get_metadata_file_path(filename);
                if (std::filesystem::exists(meta_path)) {
                    keys.push_back(filename);
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        return Result<std::vector<std::string>>("Failed to scan key directory: " + std::string(e.what()));
    }
    
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
    
    // Clear the current master key and copy the new one
    master_key_.secure_wipe();
    auto new_data = new_master_key.copy();
    std::copy(new_data.begin(), new_data.end(), master_key_.data());
    
    return Result<bool>(true);
}

Result<bool> EncryptedFileKeyStore::secure_delete_file(const std::string& file_path) const {
    try {
        std::ifstream file(file_path, std::ios::binary | std::ios::ate);
        if (!file) {
            return Result<bool>(true); // File doesn't exist, consider it deleted
        }
        
        size_t file_size = file.tellg();
        file.close();
        
        if (file_size > 0) {
            // DOD 5220.22-M standard: 3-pass secure deletion
            std::ofstream out_file(file_path, std::ios::binary);
            if (!out_file) {
                return Result<bool>("Failed to open file for secure deletion");
            }
            
            // Pass 1: All bits set to 1 (0xFF)
            std::vector<uint8_t> pass1_data(file_size, 0xFF);
            out_file.write(reinterpret_cast<const char*>(pass1_data.data()), file_size);
            out_file.flush();
            out_file.seekp(0);
            
            // Pass 2: All bits set to 0 (0x00)
            std::vector<uint8_t> pass2_data(file_size, 0x00);
            out_file.write(reinterpret_cast<const char*>(pass2_data.data()), file_size);
            out_file.flush();
            out_file.seekp(0);
            
            // Pass 3: Random data
            std::vector<uint8_t> pass3_data(file_size);
            if (RAND_bytes(pass3_data.data(), file_size) == 1) {
                out_file.write(reinterpret_cast<const char*>(pass3_data.data()), file_size);
                out_file.flush();
            }
            
            out_file.close();
        }
        
        // Now delete the file
        if (std::remove(file_path.c_str()) == 0) {
            return Result<bool>(true);
        }
        
        return Result<bool>("Failed to delete file after secure wiping");
    } catch (const std::exception& e) {
        return Result<bool>("Secure deletion failed: " + std::string(e.what()));
    }
}

Result<bool> EncryptedFileKeyStore::initialize_with_passphrase(const std::string& passphrase) {
    if (passphrase.empty()) {
        return Result<bool>("Empty passphrase not allowed");
    }
    
    // Generate salt for key derivation  
    std::vector<uint8_t> salt(16); // 128-bit salt
    if (RAND_bytes(salt.data(), salt.size()) != 1) {
        return Result<bool>("Failed to generate salt");
    }
    
    // Derive key from passphrase using PBKDF2
    auto derived_key_result = key_utils::derive_key_from_passphrase(passphrase, salt);
    if (!derived_key_result.is_ok()) {
        return Result<bool>("Failed to derive key from passphrase: " + derived_key_result.error());
    }
    
    // Clear current master key and set new one
    master_key_.secure_wipe();
    auto derived_key_data = derived_key_result.value().copy();
    std::copy(derived_key_data.begin(), derived_key_data.end(), master_key_.data());
    
    return Result<bool>(true);
}

Result<bool> EncryptedFileKeyStore::prompt_for_passphrase_if_needed() {
    // Check if we're in an interactive environment
    if (!isatty(STDIN_FILENO)) {
        return Result<bool>("Non-interactive environment, cannot prompt for passphrase");
    }
    
    // Check for environment variable override
    const char* skip_prompt = std::getenv("SLONANA_SKIP_PASSPHRASE_PROMPT");
    if (skip_prompt && std::string(skip_prompt) == "1") {
        return Result<bool>("Passphrase prompt disabled by environment variable");
    }
    
    std::string passphrase = get_passphrase_from_user(
        "Enter passphrase for key storage (leave empty for random key): ");
    
    if (passphrase.empty()) {
        return Result<bool>("User chose random key generation");
    }
    
    return initialize_with_passphrase(passphrase);
}

std::string EncryptedFileKeyStore::get_passphrase_from_user(const std::string& prompt) {
    std::cout << prompt << std::flush;
    
    // Disable echo for password input on POSIX systems
#ifdef _WIN32
    // Windows implementation would go here
    std::string passphrase;
    std::getline(std::cin, passphrase);
    return passphrase;
#else
    // POSIX implementation
    struct termios old_termios, new_termios;
    tcgetattr(STDIN_FILENO, &old_termios);
    new_termios = old_termios;
    new_termios.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
    
    std::string passphrase;
    std::getline(std::cin, passphrase);
    
    // Restore echo
    tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
    std::cout << std::endl; // Add newline after hidden input
    
    return passphrase;
#endif
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
    // Enhanced entropy check with multiple statistical tests
    const size_t test_size = 1024; // Larger sample for better statistics
    std::vector<uint8_t> test_data(test_size);
    
    if (RAND_bytes(test_data.data(), test_data.size()) != 1) {
        return false;
    }
    
    // Test 1: Frequency analysis (chi-square test approximation)
    std::unordered_map<uint8_t, int> frequency;
    for (auto byte : test_data) {
        frequency[byte]++;
    }
    
    // Expected frequency for uniform distribution
    double expected_freq = static_cast<double>(test_size) / 256.0;
    double chi_square = 0.0;
    
    for (int i = 0; i < 256; ++i) {
        int observed = frequency[static_cast<uint8_t>(i)];
        double diff = observed - expected_freq;
        chi_square += (diff * diff) / expected_freq;
    }
    
    // Chi-square critical value for 255 degrees of freedom at 95% confidence
    // This is a simplified check - in practice you'd use proper chi-square tables
    if (chi_square > 400.0) { // Very rough approximation
        return false;
    }
    
    // Test 2: Run test (consecutive identical bits)
    int runs = 0;
    bool current_bit = (test_data[0] & 1);
    for (size_t i = 1; i < test_size; ++i) {
        bool next_bit = (test_data[i] & 1);
        if (current_bit != next_bit) {
            runs++;
            current_bit = next_bit;
        }
    }
    
    // For good entropy, runs should be approximately test_size/2
    double expected_runs = static_cast<double>(test_size) / 2.0;
    if (std::abs(runs - expected_runs) > expected_runs * 0.2) { // 20% tolerance
        return false;
    }
    
    // Test 3: Check for repeating patterns
    for (size_t i = 0; i < test_size - 3; ++i) {
        bool pattern_found = false;
        for (size_t j = i + 1; j < test_size - 3; ++j) {
            if (test_data[i] == test_data[j] && 
                test_data[i+1] == test_data[j+1] && 
                test_data[i+2] == test_data[j+2] && 
                test_data[i+3] == test_data[j+3]) {
                pattern_found = true;
                break;
            }
        }
        if (pattern_found) {
            return false; // 4-byte pattern found, suspicious
        }
    }
    
    return true;
}

} // namespace key_utils

} // namespace security
} // namespace slonana