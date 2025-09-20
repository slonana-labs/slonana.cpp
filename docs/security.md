# Slonana.cpp Security Guide

## Overview

Security is a fundamental concern for blockchain validators. This guide covers the comprehensive security features implemented in Slonana.cpp, including the new cryptographic key lifecycle management system.

## 🔐 Cryptographic Key Lifecycle Management

### New in Version 1.1.0

Slonana.cpp now includes a comprehensive key lifecycle management system that addresses critical security vulnerabilities in cryptographic key handling. See [Key Management Guide](./key_management.md) for detailed documentation.

#### Key Features

- **Cryptographically Secure Key Generation**: Uses OpenSSL's secure random number generator
- **Encrypted Key Storage**: AES-256-GCM encryption for all stored keys
- **Automated Key Rotation**: Configurable policies for automatic key rotation
- **Secure Memory Management**: Protected memory buffers with automatic wiping
- **Comprehensive Auditing**: Complete audit trails for all key operations
- **Legacy Compatibility**: Seamless integration with existing keypair formats

#### Quick Start

```bash
# Generate a new secure validator identity
slonana-keygen generate

# Check identity status
slonana-keygen status

# Enable automatic rotation in production
slonana-keygen rotate  # Manual rotation
# Automatic rotation is configured via policies
```

## 🛡️ Network Security

### Connection Security

- **TLS Encryption**: All network communications use TLS 1.3
- **Peer Authentication**: Cryptographic peer identity verification
- **Message Integrity**: HMAC-based message authentication

### Gossip Protocol Security

```cpp
// Configure secure gossip
config.enable_tls = true;
config.require_validator_identity = true;
config.minimum_validator_stake = 1000000; // Minimum stake for participation
```

### RPC Security

```bash
# Enable RPC authentication
export SLONANA_RPC_AUTH_TOKEN="your-secure-token"

# Configure authenticated RPC
slonana_validator --enable-rpc-auth --rpc-auth-token-file /secure/path/token.txt
```

## 🔑 Traditional Key Management (Legacy)

### Validator Identity Keys

```bash
# Generate new validator identity (legacy format)
solana-keygen new --no-bip39-passphrase --outfile validator-keypair.json

# Secure the keypair file
chmod 600 validator-keypair.json
chown slonana:slonana validator-keypair.json

# Use with validator
slonana_validator --identity validator-keypair.json
```

### Key Security Best Practices

1. **File Permissions**
   ```bash
   # Restrict access to validator keys
   chmod 600 /etc/slonana/validator-keypair.json
   chown slonana:slonana /etc/slonana/validator-keypair.json
   ```

2. **Storage Location**
   ```bash
   # Use secure, encrypted filesystem
   /etc/slonana/           # System-wide
   ~/.slonana/             # User-specific
   /var/lib/slonana/       # Service account
   ```

3. **Backup and Recovery**
   ```bash
   # Create encrypted backup
   gpg --cipher-algo AES256 --compress-algo 1 --s2k-cipher-algo AES256 \
       --s2k-digest-algo SHA512 --s2k-mode 3 --s2k-count 65536 \
       --symmetric --output validator-keypair.json.gpg validator-keypair.json
   
   # Store backup securely offline
   ```

## 🏗️ System Hardening

### Operating System Security

```bash
# Disable unnecessary services
sudo systemctl disable apache2 nginx mysql

# Update system regularly
sudo apt update && sudo apt upgrade -y

# Enable automatic security updates
sudo apt install unattended-upgrades
sudo dpkg-reconfigure -plow unattended-upgrades

# Configure firewall
sudo ufw enable
sudo ufw allow 8001/tcp  # Gossip
sudo ufw allow 8899/tcp  # RPC (if needed)
sudo ufw deny 22/tcp     # SSH (use alternative port)
```

### SSH Security

```bash
# Edit /etc/ssh/sshd_config
PermitRootLogin no
PasswordAuthentication no
PubkeyAuthentication yes
Port 2222  # Non-standard port
AllowUsers slonana-admin

# Restart SSH service
sudo systemctl restart ssh
```

### Process Isolation

```bash
# Create dedicated user for validator
sudo useradd -r -s /bin/false -d /var/lib/slonana slonana

# Run validator with limited privileges
sudo -u slonana slonana_validator --config /etc/slonana/validator.conf
```

## 🔍 Security Monitoring

### Key Management Monitoring

```bash
# Monitor key lifecycle events
slonana-keygen audit

# Check for rotation needs
slonana-keygen status

# Automated monitoring script
#!/bin/bash
STATUS=$(slonana-keygen status --json)
NEEDS_ROTATION=$(echo $STATUS | jq -r '.needs_rotation')

if [ "$NEEDS_ROTATION" = "true" ]; then
    echo "ALERT: Validator key rotation needed"
    # Send alert to monitoring system
fi
```

### System Monitoring

```bash
# Monitor validator process
systemctl status slonana-validator

# Check system resources
htop
iostat
netstat -tulpn

# Monitor log files
tail -f /var/log/slonana/validator.log
```

### Security Event Logging

```bash
# Configure rsyslog for security events
echo "local0.*    /var/log/slonana/security.log" >> /etc/rsyslog.d/50-slonana.conf
systemctl restart rsyslog

# Monitor security log
tail -f /var/log/slonana/security.log
```

## 🚨 Incident Response

### Key Compromise Response

```bash
# Immediate response steps:

# 1. Revoke compromised key
slonana-keygen revoke "Security incident - $(date)"

# 2. Generate new identity
slonana-keygen generate

# 3. Update validator configuration
# 4. Restart validator with new identity
# 5. Notify network operators if necessary
```

### Security Incident Checklist

1. **Immediate Actions**
   - [ ] Stop validator process
   - [ ] Revoke compromised keys
   - [ ] Isolate affected systems
   - [ ] Generate new secure keys

2. **Investigation**
   - [ ] Audit key usage logs
   - [ ] Check system logs for intrusion
   - [ ] Verify backup integrity
   - [ ] Document timeline of events

3. **Recovery**
   - [ ] Deploy new secure keys
   - [ ] Restart validator operations
   - [ ] Monitor for continued threats
   - [ ] Update security procedures

4. **Post-Incident**
   - [ ] Conduct security review
   - [ ] Update incident response procedures
   - [ ] Share lessons learned (if appropriate)
   - [ ] Implement additional safeguards

## 🔒 Advanced Security Features

### Hardware Security Module (HSM) Integration

*Future Enhancement*

The key management system is designed to support HSM integration:

```cpp
// Future HSM support
auto hsm_store = std::make_shared<HSMKeyStore>(hsm_config);
auto key_manager = std::make_shared<CryptographicKeyManager>(hsm_store);
```

### Multi-Signature Support

*Future Enhancement*

Planned support for multi-signature validator operations:

```cpp
// Future multi-sig support
MultiSigConfig multi_sig_config;
multi_sig_config.threshold = 2;
multi_sig_config.total_signers = 3;
```

### Zero-Knowledge Proofs

*Future Enhancement*

Integration with zero-knowledge proof systems for enhanced privacy:

```cpp
// Future ZK integration
ZKProofSystem zk_system;
auto proof = zk_system.generate_validator_proof(validator_identity);
```

## 📋 Security Checklist

### Pre-Deployment

- [ ] Generated secure validator identity using `slonana-keygen`
- [ ] Configured automatic key rotation policy
- [ ] Set up encrypted key storage
- [ ] Hardened operating system
- [ ] Configured firewall rules
- [ ] Set up security monitoring
- [ ] Created incident response plan
- [ ] Tested backup and recovery procedures

### Ongoing Operations

- [ ] Monitor key lifecycle status weekly
- [ ] Review security logs daily
- [ ] Update system packages monthly
- [ ] Test incident response procedures quarterly
- [ ] Audit key usage monthly
- [ ] Verify backup integrity monthly
- [ ] Review security configurations quarterly

### Emergency Procedures

- [ ] Document key revocation process
- [ ] Test emergency key rotation
- [ ] Prepare incident communication plan
- [ ] Maintain offline recovery procedures
- [ ] Document escalation procedures

## 🤝 Security Community

### Reporting Security Issues

- **Vulnerabilities**: security@slonana.org (PGP encrypted)
- **General Issues**: GitHub security tab
- **Questions**: Discord #security channel

### Security Advisories

Subscribe to security advisories:
- GitHub security advisories
- Mailing list: security-announce@slonana.org
- RSS feed: https://github.com/slonana-labs/slonana.cpp/security/advisories

### Contributing to Security

1. **Security Reviews**: Participate in code security reviews
2. **Penetration Testing**: Report findings through responsible disclosure
3. **Documentation**: Improve security documentation
4. **Tools**: Contribute security tools and scripts

## 📚 Additional Resources

- [Key Management Detailed Guide](./key_management.md)
- [Deployment Guide](./DEPLOYMENT.md)
- [Monitoring Guide](./monitoring.md)
- [NIST Cybersecurity Framework](https://www.nist.gov/cyberframework)
- [OWASP Security Guidelines](https://owasp.org/)

---

**⚠️ Security Reminder**: Security is an ongoing process, not a one-time configuration. Regular monitoring, updates, and reviews are essential for maintaining a secure validator environment.