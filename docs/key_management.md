# Cryptographic Key Lifecycle Management

## Overview

The Slonana validator now includes a comprehensive cryptographic key lifecycle management system that addresses critical security requirements for blockchain validators. This system provides automated key rotation, secure storage, and proper key destruction while maintaining backward compatibility with existing keypair formats.

## 🔐 Security Improvements

### Critical Issues Addressed

- **Weak Random Number Generation**: Replaced `std::mt19937` with OpenSSL's cryptographically secure random number generator
- **Plaintext Key Storage**: Implemented AES-256-GCM encrypted storage for all cryptographic material
- **No Secure Memory Management**: Added secure memory buffers with automatic wiping and optional memory locking
- **Missing Key Rotation**: Implemented automated key rotation with configurable policies
- **No Key Destruction**: Added secure key wiping and destruction procedures
- **No HSM Abstraction**: Created pluggable key storage backend architecture

### New Security Features

1. **Cryptographically Secure Key Generation**
   - Uses OpenSSL's `RAND_bytes` for entropy
   - Validates key strength and entropy quality
   - Supports Ed25519-compatible key generation

2. **Encrypted Key Storage**
   - AES-256-GCM encryption for all stored keys
   - Key metadata encryption and integrity protection
   - Secure file deletion with random overwrites

3. **Automated Key Rotation**
   - Configurable rotation intervals and use count limits
   - Automatic rotation on suspicious activity detection
   - Grace period for smooth key transitions

4. **Secure Memory Management**
   - `SecureBuffer` class with automatic secure wiping
   - Memory locking on supported platforms
   - Constant-time memory comparisons

5. **Comprehensive Audit and Monitoring**
   - Key usage tracking and audit logs
   - Lifecycle event monitoring
   - Security metrics and alerting

## 🛠️ Architecture

### Core Components

```
┌─────────────────────────────────────────┐
│            ValidatorIdentity            │
│  (Maintains backward compatibility)     │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│      EnhancedValidatorIdentity          │
│  (Bridges legacy and secure systems)   │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│       SecureValidatorIdentity           │
│    (High-level secure operations)      │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│      CryptographicKeyManager            │
│   (Core key lifecycle management)      │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│            KeyStore Interface           │
│      (Pluggable storage backends)      │
└─────────────────┬───────────────────────┘
                  │
        ┌─────────┴─────────┐
        ▼                   ▼
┌───────────────┐  ┌──────────────────┐
│ EncryptedFile │  │    Future HSM    │
│   KeyStore    │  │   Integration    │
└───────────────┘  └──────────────────┘
```

### Key Storage Abstraction

The system uses a pluggable architecture allowing for different storage backends:

- **EncryptedFileKeyStore**: Default encrypted file-based storage
- **Future HSM Integration**: Ready for hardware security module integration
- **Cloud Key Management**: Extensible for cloud-based key services

## 📋 Usage

### CLI Tool - `slonana-keygen`

The new CLI tool provides comprehensive key management operations:

```bash
# Generate a new secure validator identity
slonana-keygen generate

# Import existing legacy keypair
slonana-keygen import /path/to/validator-keypair.json

# Check identity status
slonana-keygen status

# Rotate identity key
slonana-keygen rotate

# List all identities
slonana-keygen list

# Export to legacy format
slonana-keygen export /path/to/output.json

# Audit key usage
slonana-keygen audit

# Clean up expired keys
slonana-keygen cleanup
```

### Programmatic Usage

```cpp
#include "security/secure_validator_identity.h"

// Create secure identity manager
auto identity = SecureKeyManagerFactory::create_secure_identity("/secure/key/storage");

// Initialize
auto init_result = identity->initialize();
if (!init_result.is_ok()) {
    // Handle error
}

// Generate new validator identity
auto create_result = identity->create_new_identity();
if (create_result.is_ok()) {
    std::string key_id = create_result.value();
    std::cout << "Created secure identity: " << key_id << std::endl;
}

// Get current public key
auto pub_key_result = identity->get_public_key();
if (pub_key_result.is_ok()) {
    auto pub_key = pub_key_result.value();
    // Use public key for validator operations
}

// Enable automatic rotation
auto key_manager = identity->get_key_manager();
if (key_manager) {
    auto policy = SecureKeyManagerFactory::create_production_policy();
    key_manager->set_rotation_policy(policy);
}
```

### Integration with Existing Validator

The enhanced validator identity maintains full backward compatibility:

```cpp
// In slonana_validator.cpp - minimal changes required
EnhancedValidatorIdentity enhanced_identity(storage_path, true); // Enable secure management

// Existing code continues to work
if (!config_.identity_keypair_path.empty()) {
    auto identity_result = enhanced_identity.load_validator_identity(config_.identity_keypair_path);
    // ... existing logic
} else {
    validator_identity_ = enhanced_identity.generate_validator_identity();
    // ... existing logic
}

// New secure operations available
auto rotation_result = enhanced_identity.rotate_identity_if_needed();
auto rotation_enabled = enhanced_identity.enable_automatic_rotation();
```

## 🔑 Key Rotation Policies

### Pre-configured Policies

1. **Production Policy**
   - Rotation interval: 30 days
   - Max use count: 1,000,000
   - Automatic rotation: Enabled
   - Grace period: 24 hours

2. **Development Policy**
   - Rotation interval: 7 days
   - Max use count: 100,000
   - Automatic rotation: Disabled (manual)
   - Grace period: 1 hour

3. **High Security Policy**
   - Rotation interval: 7 days
   - Max use count: 50,000
   - Automatic rotation: Enabled
   - Grace period: 2 hours

### Custom Policy Configuration

```cpp
KeyRotationPolicy custom_policy;
custom_policy.rotation_interval = std::chrono::hours(24 * 14); // 14 days
custom_policy.max_use_count = 500000;
custom_policy.rotate_on_suspicious_activity = true;
custom_policy.automatic_rotation_enabled = true;
custom_policy.grace_period = std::chrono::hours(6);

key_manager->set_rotation_policy(custom_policy);
```

## 🛡️ Security Best Practices

### Storage Security

1. **Key Storage Location**
   ```bash
   # Recommended secure locations
   /etc/slonana/keys/           # System-wide installation
   ~/.slonana/keys/             # User installation
   /var/lib/slonana/keys/       # Service installation
   ```

2. **File Permissions**
   ```bash
   # Set restrictive permissions
   chmod 700 /path/to/key/storage
   chown slonana:slonana /path/to/key/storage
   ```

3. **Network Isolation**
   - Store keys on encrypted filesystems
   - Use network-isolated storage for production
   - Consider hardware security modules for high-value validators

### Operational Security

1. **Regular Audits**
   ```bash
   # Weekly audit reports
   slonana-keygen audit > weekly_audit_$(date +%Y%m%d).log
   
   # Monitor for rotation needs
   slonana-keygen status
   ```

2. **Backup Procedures**
   ```bash
   # Secure backup (encrypted)
   slonana-keygen export /secure/backup/location/validator-backup-$(date +%Y%m%d).key
   
   # Verify backup integrity
   slonana-keygen import /secure/backup/location/validator-backup-*.key
   ```

3. **Emergency Procedures**
   ```bash
   # Emergency key revocation
   slonana-keygen revoke "Security incident - $(date)"
   
   # Generate new identity immediately
   slonana-keygen generate
   ```

## 🔍 Monitoring and Alerting

### Key Events to Monitor

1. **Key Rotation Events**
   - Automatic rotations
   - Failed rotation attempts
   - Manual rotations

2. **Security Events**
   - Key revocations
   - Emergency procedures
   - Failed key access attempts

3. **Lifecycle Events**
   - Key expiration warnings
   - High usage count alerts
   - Storage integrity checks

### Integration with Monitoring Systems

```cpp
// Example monitoring integration
auto stats = key_manager->get_stats();

if (stats.expired_keys > 0) {
    // Alert: Expired keys need cleanup
}

if (stats.rotations_performed == 0 && uptime > 30_days) {
    // Alert: No rotations performed in 30 days
}

auto audit_log = key_manager->audit_key_usage();
// Send audit log to security monitoring system
```

## 🧪 Testing

### Comprehensive Test Suite

The implementation includes extensive tests covering:

- Cryptographic key generation and validation
- Secure memory management
- Encrypted storage operations
- Key rotation scenarios
- Legacy compatibility
- Emergency procedures

```bash
# Run key management tests
cd build
./slonana_key_management_tests

# Run full test suite
make test
```

### Security Testing

- Memory sanitizer validation
- Entropy quality verification
- Encryption/decryption round-trip tests
- Key strength validation
- Secure deletion verification

## 🔄 Migration Guide

### Migrating from Legacy Keys

1. **Automatic Migration**
   ```cpp
   // Enable secure management in existing validator
   EnhancedValidatorIdentity identity(storage_path, true);
   
   // Import existing keypair
   auto import_result = identity.import_legacy_identity(legacy_keypair_path);
   ```

2. **Manual Migration**
   ```bash
   # Import existing keypair
   slonana-keygen import /path/to/existing/validator-keypair.json
   
   # Verify import
   slonana-keygen status
   
   # Export new secure backup
   slonana-keygen export /secure/backup/new-secure-keypair.json
   ```

3. **Gradual Rollout**
   - Phase 1: Install with secure management disabled
   - Phase 2: Enable secure management and import existing keys
   - Phase 3: Enable automatic rotation
   - Phase 4: Transition to secure-only operations

## 📚 API Reference

### Core Classes

- `SecureBuffer`: Secure memory management
- `KeyStore`: Abstract storage interface
- `EncryptedFileKeyStore`: Encrypted file storage implementation
- `CryptographicKeyManager`: Core key lifecycle management
- `SecureValidatorIdentity`: High-level secure operations
- `EnhancedValidatorIdentity`: Backward-compatible wrapper

### Key Utility Functions

- `key_utils::generate_secure_random()`: Cryptographically secure random generation
- `key_utils::derive_key_from_passphrase()`: PBKDF2 key derivation
- `key_utils::validate_key_strength()`: Key strength validation
- `key_utils::secure_compare()`: Constant-time memory comparison

## 🚀 Future Enhancements

### Planned Features

1. **Hardware Security Module Integration**
   - PKCS#11 interface support
   - Cloud HSM integration
   - Hardware key attestation

2. **Advanced Monitoring**
   - Real-time security metrics
   - Anomaly detection
   - Automated threat response

3. **Multi-Party Key Management**
   - Threshold signatures
   - Multi-signature support
   - Distributed key generation

4. **Compliance Features**
   - FIPS 140-2 compliance
   - Common Criteria certification
   - Regulatory audit support

## 🤝 Contributing

When contributing to the key management system:

1. Follow secure coding practices
2. Add comprehensive tests for new features
3. Update documentation for any API changes
4. Consider backward compatibility implications
5. Review security implications of changes

## 📞 Support

For security-related issues:
- **Security vulnerabilities**: Use private security reporting
- **Feature requests**: Open GitHub issues
- **Documentation**: Update this guide with improvements

---

**⚠️ Security Notice**: This key management system significantly improves the security posture of the Slonana validator. However, security is a shared responsibility. Operators must follow best practices for deployment, monitoring, and maintenance.