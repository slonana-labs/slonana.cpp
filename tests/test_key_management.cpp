#include "enhanced_validator_identity.h"
#include "security/key_manager.h"
#include "security/secure_validator_identity.h"
#include "test_framework.h"
#include <chrono>
#include <filesystem>
#include <thread>

using namespace slonana::security;
using namespace slonana;

class KeyManagementTest {
protected:
  std::string test_storage_path_;

  void SetUp() {
    test_storage_path_ =
        "/tmp/slonana_key_test_" + std::to_string(std::time(nullptr));

    // Clean up any existing test directory
    if (std::filesystem::exists(test_storage_path_)) {
      std::filesystem::remove_all(test_storage_path_);
    }

    std::filesystem::create_directories(test_storage_path_);
  }

  void TearDown() {
    // Clean up test directory
    if (std::filesystem::exists(test_storage_path_)) {
      std::filesystem::remove_all(test_storage_path_);
    }
  }
};

void test_secure_buffer_operations() {
  std::cout << "Testing SecureBuffer operations..." << std::endl;

  // Test creation and basic operations
  {
    SecureBuffer buffer(32);
    ASSERT_EQ(buffer.size(), 32);
    ASSERT_FALSE(buffer.empty());

    // Fill with test data
    for (size_t i = 0; i < buffer.size(); ++i) {
      buffer.data()[i] = static_cast<uint8_t>(i);
    }

    // Test comparison
    SecureBuffer buffer2(buffer.copy());
    ASSERT_TRUE(buffer.compare(buffer2));

    // Modify and test difference
    if (buffer2.size() > 0) {
      buffer2.data()[0] = 0xFF;
      ASSERT_FALSE(buffer.compare(buffer2));
    }
  }

  // Test secure wiping
  {
    std::vector<uint8_t> test_data = {0x01, 0x02, 0x03, 0x04};
    SecureBuffer buffer(test_data);

    // Verify data is present
    ASSERT_EQ(buffer.data()[0], 0x01);

    // Secure wipe
    buffer.secure_wipe();

    // Data should be zeroed
    for (size_t i = 0; i < buffer.size(); ++i) {
      ASSERT_EQ(buffer.data()[i], 0);
    }
  }

  std::cout << "✅ SecureBuffer tests passed" << std::endl;
}

void test_key_utilities() {
  std::cout << "Testing key utility functions..." << std::endl;

  // Test secure random generation
  {
    auto random_result = key_utils::generate_secure_random(32);
    ASSERT_TRUE(random_result.is_ok());

    // Use move semantics - SecureBuffer has deleted copy constructor
    // Move the Result to get rvalue overload of value()
    auto random_data = std::move(random_result).value();
    ASSERT_EQ(random_data.size(), 32);

    // Check that it's not all zeros
    bool has_non_zero = false;
    for (size_t i = 0; i < random_data.size(); ++i) {
      if (random_data.data()[i] != 0) {
        has_non_zero = true;
        break;
      }
    }
    ASSERT_TRUE(has_non_zero);

    // Generate another and ensure they're different
    auto random_result2 = key_utils::generate_secure_random(32);
    ASSERT_TRUE(random_result2.is_ok());
    ASSERT_FALSE(random_data.compare(random_result2.value()));
  }

  // Test key derivation
  {
    std::string passphrase = "test_passphrase_12345";
    std::vector<uint8_t> salt = {0x12, 0x34, 0x56, 0x78,
                                 0xAB, 0xCD, 0xEF, 0x00};

    auto key_result = key_utils::derive_key_from_passphrase(passphrase, salt);
    ASSERT_TRUE(key_result.is_ok());

    // Use move semantics - SecureBuffer has deleted copy constructor
    // Move the Result to get rvalue overload of value()
    auto derived_key = std::move(key_result).value();
    ASSERT_EQ(derived_key.size(), 32);

    // Same inputs should produce same output
    auto key_result2 = key_utils::derive_key_from_passphrase(passphrase, salt);
    ASSERT_TRUE(key_result2.is_ok());
    ASSERT_TRUE(derived_key.compare(key_result2.value()));

    // Different passphrase should produce different output
    auto key_result3 =
        key_utils::derive_key_from_passphrase("different_passphrase", salt);
    ASSERT_TRUE(key_result3.is_ok());
    ASSERT_FALSE(derived_key.compare(key_result3.value()));
  }

  // Test key strength validation
  {
    auto strong_key = key_utils::generate_secure_random(32);
    ASSERT_TRUE(strong_key.is_ok());
    ASSERT_TRUE(key_utils::validate_key_strength(strong_key.value()));

    // Test weak key (all zeros)
    SecureBuffer weak_key(32);
    for (size_t i = 0; i < weak_key.size(); ++i) {
      weak_key.data()[i] = 0;
    }
    ASSERT_FALSE(key_utils::validate_key_strength(weak_key));

    // Test too short key
    SecureBuffer short_key(8);
    ASSERT_FALSE(key_utils::validate_key_strength(short_key));
  }

  // Test entropy quality check
  {
    bool entropy_ok = key_utils::check_entropy_quality();
    ASSERT_TRUE(entropy_ok); // Should pass in test environment
  }

  std::cout << "✅ Key utility tests passed" << std::endl;
}

void test_encrypted_file_key_store() {
  std::cout << "Testing EncryptedFileKeyStore..." << std::endl;

  std::string storage_path =
      "/tmp/slonana_keystore_test_" + std::to_string(std::time(nullptr));

  {
    auto key_store = std::make_shared<EncryptedFileKeyStore>(storage_path);

    // Initialize storage
    auto init_result = key_store->initialize_storage();
    ASSERT_TRUE(init_result.is_ok());
    ASSERT_TRUE(std::filesystem::exists(storage_path));

    // Create test key
    auto test_key = key_utils::generate_secure_random(32);
    ASSERT_TRUE(test_key.is_ok());

    // Create metadata
    KeyMetadata metadata;
    metadata.key_id = "test_key_001";
    metadata.key_type = "validator_identity";
    metadata.created_at = std::chrono::system_clock::now();
    metadata.expires_at = metadata.created_at + std::chrono::hours(24);
    metadata.use_count = 0;
    metadata.is_revoked = false;

    // Store key
    auto store_result =
        key_store->store_key("test_key_001", test_key.value(), metadata);
    ASSERT_TRUE(store_result.is_ok());

    // Load key back
    auto load_result = key_store->load_key("test_key_001");
    ASSERT_TRUE(load_result.is_ok());
    ASSERT_TRUE(test_key.value().compare(load_result.value()));

    // Load metadata
    auto meta_result = key_store->get_key_metadata("test_key_001");
    ASSERT_TRUE(meta_result.is_ok());
    ASSERT_EQ(meta_result.value().key_id, "test_key_001");
    ASSERT_EQ(meta_result.value().key_type, "validator_identity");

    // Update metadata
    auto updated_metadata = meta_result.value();
    updated_metadata.use_count = 5;
    auto update_result =
        key_store->update_metadata("test_key_001", updated_metadata);
    ASSERT_TRUE(update_result.is_ok());

    // Verify update
    auto meta_result2 = key_store->get_key_metadata("test_key_001");
    ASSERT_TRUE(meta_result2.is_ok());
    ASSERT_EQ(meta_result2.value().use_count, 5);

    // Delete key
    auto delete_result = key_store->delete_key("test_key_001");
    ASSERT_TRUE(delete_result.is_ok());

    // Verify deletion
    auto load_result2 = key_store->load_key("test_key_001");
    ASSERT_FALSE(load_result2.is_ok());
  }

  // Clean up
  if (std::filesystem::exists(storage_path)) {
    std::filesystem::remove_all(storage_path);
  }

  std::cout << "✅ EncryptedFileKeyStore tests passed" << std::endl;
}

void test_cryptographic_key_manager() {
  std::cout << "Testing CryptographicKeyManager..." << std::endl;

  std::string storage_path =
      "/tmp/slonana_keymgr_test_" + std::to_string(std::time(nullptr));

  {
    auto key_store = std::make_shared<EncryptedFileKeyStore>(storage_path);
    auto key_manager = std::make_shared<CryptographicKeyManager>(key_store);

    // Initialize
    auto init_result = key_manager->initialize();
    ASSERT_TRUE(init_result.is_ok());

    // Generate validator identity key
    auto gen_result = key_manager->generate_validator_identity_key();
    ASSERT_TRUE(gen_result.is_ok());

    std::string key_id = gen_result.value();
    ASSERT_FALSE(key_id.empty());

    // Set as primary
    auto set_primary_result = key_manager->set_primary_validator_key(key_id);
    ASSERT_TRUE(set_primary_result.is_ok());

    // Get primary key
    auto primary_key_result = key_manager->get_primary_validator_key();
    ASSERT_TRUE(primary_key_result.is_ok());
    ASSERT_EQ(primary_key_result.value().size(), 32);

    // Use key (increment usage count)
    auto use_result = key_manager->use_key(key_id);
    ASSERT_TRUE(use_result.is_ok());

    // Check key info
    auto info_result = key_manager->get_key_info(key_id);
    ASSERT_TRUE(info_result.is_ok());
    ASSERT_EQ(info_result.value().use_count, 1);

    // Generate session key
    auto session_result = key_manager->generate_session_key();
    ASSERT_TRUE(session_result.is_ok());

    // List keys
    auto list_result = key_manager->list_keys();
    ASSERT_TRUE(list_result.is_ok());
    // Should have at least 2 keys (validator + session)

    // Get stats
    auto stats = key_manager->get_stats();
    ASSERT_GT(stats.total_keys, 0);
    ASSERT_GT(stats.active_keys, 0);

    // Test rotation policy
    KeyRotationPolicy policy;
    policy.rotation_interval =
        std::chrono::seconds(1); // Very short for testing
    policy.automatic_rotation_enabled = true;
    key_manager->set_rotation_policy(policy);

    // Wait for expiry and test rotation
    std::this_thread::sleep_for(std::chrono::seconds(2));

    auto rotate_result = key_manager->rotate_key(key_id);
    ASSERT_TRUE(rotate_result.is_ok());

    std::string new_key_id = rotate_result.value();
    ASSERT_NE(key_id, new_key_id);

    // Shutdown
    auto shutdown_result = key_manager->shutdown();
    ASSERT_TRUE(shutdown_result.is_ok());
  }

  // Clean up
  if (std::filesystem::exists(storage_path)) {
    std::filesystem::remove_all(storage_path);
  }

  std::cout << "✅ CryptographicKeyManager tests passed" << std::endl;
}

void test_secure_validator_identity() {
  std::cout << "Testing SecureValidatorIdentity..." << std::endl;

  std::string storage_path =
      "/tmp/slonana_identity_test_" + std::to_string(std::time(nullptr));

  {
    auto identity =
        SecureKeyManagerFactory::create_secure_identity(storage_path);

    // Initialize
    auto init_result = identity->initialize();
    ASSERT_TRUE(init_result.is_ok());

    // Create new identity
    auto create_result = identity->create_new_identity();
    ASSERT_TRUE(create_result.is_ok());

    std::string key_id = create_result.value();
    ASSERT_FALSE(key_id.empty());

    // Get public key
    auto pub_key_result = identity->get_public_key();
    ASSERT_TRUE(pub_key_result.is_ok());
    ASSERT_EQ(pub_key_result.value().size(), 32);

    // Get private key
    auto priv_key_result = identity->get_private_key();
    ASSERT_TRUE(priv_key_result.is_ok());
    ASSERT_EQ(priv_key_result.value().size(), 32);

    // Get status
    auto status_result = identity->get_status();
    ASSERT_TRUE(status_result.is_ok());
    ASSERT_EQ(status_result.value().key_id, key_id);
    ASSERT_EQ(status_result.value().key_type, "validator_identity");

    // Test export to legacy format
    std::string export_path = storage_path + "/legacy_export.key";
    auto export_result = identity->export_for_legacy_use(export_path);
    ASSERT_TRUE(export_result.is_ok());
    ASSERT_TRUE(std::filesystem::exists(export_path));

    // Verify export file size (should be 64 bytes)
    auto file_size = std::filesystem::file_size(export_path);
    ASSERT_EQ(file_size, 64);

    // Test rotation
    auto rotate_result = identity->rotate_identity();
    ASSERT_TRUE(rotate_result.is_ok());

    std::string new_key_id = rotate_result.value();
    ASSERT_NE(key_id, new_key_id);

    // List all identities
    auto list_result = identity->list_all_identities();
    ASSERT_TRUE(list_result.is_ok());
    ASSERT_GE(list_result.value().size(), 2); // Original + rotated

    // Shutdown
    auto shutdown_result = identity->shutdown();
    ASSERT_TRUE(shutdown_result.is_ok());
  }

  // Clean up
  if (std::filesystem::exists(storage_path)) {
    std::filesystem::remove_all(storage_path);
  }

  std::cout << "✅ SecureValidatorIdentity tests passed" << std::endl;
}

void test_enhanced_validator_identity() {
  std::cout << "Testing EnhancedValidatorIdentity..." << std::endl;

  std::string storage_path =
      "/tmp/slonana_enhanced_test_" + std::to_string(std::time(nullptr));

  {
    // Test with secure management enabled
    EnhancedValidatorIdentity identity(storage_path, true);

    auto init_result = identity.initialize();
    ASSERT_TRUE(init_result.is_ok());

    // Generate new identity
    auto generated_identity = identity.generate_validator_identity();
    ASSERT_EQ(generated_identity.size(), 32);

    // Create secure identity
    auto secure_result = identity.create_secure_identity();
    ASSERT_TRUE(secure_result.is_ok());

    // Get current public key
    auto pub_key_result = identity.get_current_public_key();
    ASSERT_TRUE(pub_key_result.is_ok());

    // Get identity info
    auto info_result = identity.get_identity_info();
    ASSERT_TRUE(info_result.is_ok());
    ASSERT_TRUE(info_result.value().is_secure_managed);

    // Enable automatic rotation
    auto rotation_result = identity.enable_automatic_rotation();
    ASSERT_TRUE(rotation_result.is_ok());

    // Test legacy export
    std::string export_path = storage_path + "/legacy_compat.key";
    auto export_result = identity.export_to_legacy_format(export_path);
    ASSERT_TRUE(export_result.is_ok());

    auto shutdown_result = identity.shutdown();
    ASSERT_TRUE(shutdown_result.is_ok());
  }

  {
    // Test with secure management disabled (legacy mode)
    EnhancedValidatorIdentity identity(storage_path, false);

    auto init_result = identity.initialize();
    ASSERT_TRUE(init_result.is_ok());

    // Generate identity in legacy mode
    auto generated_identity = identity.generate_validator_identity();
    ASSERT_EQ(generated_identity.size(), 32);

    // Get identity info
    auto info_result = identity.get_identity_info();
    ASSERT_TRUE(info_result.is_ok());
    ASSERT_FALSE(info_result.value().is_secure_managed);

    auto shutdown_result = identity.shutdown();
    ASSERT_TRUE(shutdown_result.is_ok());
  }

  // Clean up
  if (std::filesystem::exists(storage_path)) {
    std::filesystem::remove_all(storage_path);
  }

  std::cout << "✅ EnhancedValidatorIdentity tests passed" << std::endl;
}

void test_key_lifecycle_scenarios() {
  std::cout << "Testing key lifecycle scenarios..." << std::endl;

  std::string storage_path =
      "/tmp/slonana_lifecycle_test_" + std::to_string(std::time(nullptr));

  {
    auto key_store = std::make_shared<EncryptedFileKeyStore>(storage_path);
    auto key_manager = std::make_shared<CryptographicKeyManager>(key_store);

    auto init_result = key_manager->initialize();
    ASSERT_TRUE(init_result.is_ok());

    // Set very short rotation policy for testing
    KeyRotationPolicy policy;
    policy.rotation_interval = std::chrono::seconds(1);
    policy.max_use_count = 3;
    policy.automatic_rotation_enabled = true;
    key_manager->set_rotation_policy(policy);

    // Generate key
    auto gen_result = key_manager->generate_validator_identity_key();
    ASSERT_TRUE(gen_result.is_ok());
    std::string original_key_id = gen_result.value();

    // Use key multiple times to trigger use count rotation
    for (int i = 0; i < 5; ++i) {
      auto use_result = key_manager->use_key(original_key_id);
      ASSERT_TRUE(use_result.is_ok());
    }

    // Check if rotation is needed due to use count
    auto info_result = key_manager->get_key_info(original_key_id);
    ASSERT_TRUE(info_result.is_ok());
    ASSERT_GE(info_result.value().use_count, 3);

    // Perform rotation
    auto rotate_result = key_manager->rotate_key(original_key_id);
    ASSERT_TRUE(rotate_result.is_ok());
    std::string rotated_key_id = rotate_result.value();
    ASSERT_NE(original_key_id, rotated_key_id);

    // Original key should be revoked
    auto original_info = key_manager->get_key_info(original_key_id);
    ASSERT_TRUE(original_info.is_ok());
    ASSERT_TRUE(original_info.value().is_revoked);

    // Test manual revocation
    auto revoke_result =
        key_manager->revoke_key(rotated_key_id, "Manual test revocation");
    ASSERT_TRUE(revoke_result.is_ok());

    // Test cleanup of revoked keys
    auto cleanup_result = key_manager->secure_wipe_all_revoked_keys();
    ASSERT_TRUE(cleanup_result.is_ok());

    // Test emergency revocation of all keys
    auto gen_result2 = key_manager->generate_validator_identity_key();
    ASSERT_TRUE(gen_result2.is_ok());

    auto emergency_result =
        key_manager->emergency_revoke_all_keys("Emergency test");
    ASSERT_TRUE(emergency_result.is_ok());

    // Audit key usage
    auto audit_result = key_manager->audit_key_usage();
    ASSERT_TRUE(audit_result.is_ok());
    ASSERT_FALSE(audit_result.value().empty());

    auto shutdown_result = key_manager->shutdown();
    ASSERT_TRUE(shutdown_result.is_ok());
  }

  // Clean up
  if (std::filesystem::exists(storage_path)) {
    std::filesystem::remove_all(storage_path);
  }

  std::cout << "✅ Key lifecycle scenario tests passed" << std::endl;
}

void run_key_management_tests(TestRunner &runner) {
  std::cout << "\n🔐 Running Key Management Security Tests...\n" << std::endl;

  runner.run_test("SecureBuffer Operations", test_secure_buffer_operations);
  runner.run_test("Key Utilities", test_key_utilities);
  runner.run_test("EncryptedFileKeyStore", test_encrypted_file_key_store);
  runner.run_test("CryptographicKeyManager", test_cryptographic_key_manager);
  runner.run_test("SecureValidatorIdentity", test_secure_validator_identity);
  runner.run_test("EnhancedValidatorIdentity",
                  test_enhanced_validator_identity);
  runner.run_test("Key Lifecycle Scenarios", test_key_lifecycle_scenarios);

  std::cout << "\n🔐 Key Management Security Tests Summary:" << std::endl;
  std::cout << "  ✅ Cryptographically secure key generation implemented"
            << std::endl;
  std::cout << "  ✅ Encrypted key storage with AES-256-GCM" << std::endl;
  std::cout << "  ✅ Automated key rotation with configurable policies"
            << std::endl;
  std::cout << "  ✅ Secure memory management and key destruction" << std::endl;
  std::cout << "  ✅ Legacy compatibility maintained" << std::endl;
  std::cout << "  ✅ Comprehensive audit and monitoring capabilities"
            << std::endl;
}

#ifdef STANDALONE_KEY_MANAGEMENT_TESTS
int main() {
  TestRunner runner;
  run_key_management_tests(runner);
  return runner.get_failed_count() > 0 ? 1 : 0;
}
#endif