# Secure Inter-Node Communication

This document describes the secure communication features implemented in slonana.cpp to protect inter-node messaging and ensure consensus integrity.

## Overview

The slonana.cpp validator implements comprehensive cryptographic protections for all inter-node communication, including:

- **Transport-level Security**: TLS 1.3 with mutual authentication for all network connections
- **Message-level Security**: AES-GCM encryption and Ed25519 signatures for application messages  
- **Replay Protection**: Nonce-based replay attack prevention with configurable time windows
- **Identity Verification**: Certificate-based node identity validation and trusted peer management

## Configuration

### Basic Security Settings

Add the following configuration to your validator config to enable secure communication:

```cpp
ValidatorConfig config;

// Enable secure messaging
config.enable_secure_messaging = true;
config.enable_tls = true;
config.require_mutual_tls = true;

// Certificate and key paths
config.tls_certificate_path = "/path/to/validator-cert.pem";
config.tls_private_key_path = "/path/to/validator-key.pem";
config.ca_certificate_path = "/path/to/ca-cert.pem";

// Message-level cryptography
config.node_signing_key_path = "/path/to/signing-key.pem";
config.peer_keys_directory = "/path/to/peer-keys/";
config.enable_message_encryption = true;
config.enable_replay_protection = true;

// Security tuning
config.message_ttl_seconds = 300;  // 5 minute message validity
config.tls_handshake_timeout_ms = 10000;  // 10 second TLS timeout
```

### Certificate Management

#### Generating TLS Certificates

For development and testing:

```bash
# Generate CA private key
openssl genrsa -out ca-key.pem 4096

# Generate CA certificate
openssl req -new -x509 -days 365 -key ca-key.pem -out ca-cert.pem

# Generate validator private key
openssl genrsa -out validator-key.pem 4096

# Generate certificate signing request
openssl req -new -key validator-key.pem -out validator.csr

# Sign validator certificate with CA
openssl x509 -req -days 365 -in validator.csr -CA ca-cert.pem -CAkey ca-key.pem -out validator-cert.pem
```

#### Generating Ed25519 Signing Keys

```bash
# Generate Ed25519 private key for message signing
openssl genpkey -algorithm Ed25519 -out signing-key.pem

# Extract public key for peer verification
openssl pkey -in signing-key.pem -pubout -out signing-pubkey.pem
```

### Network Component Integration

#### QUIC Client

The QUIC client automatically uses secure messaging when enabled:

```cpp
ValidatorConfig config;
config.enable_secure_messaging = true;
// ... other security config ...

QuicClient client(config);
client.initialize();

// Send encrypted and signed data
std::vector<uint8_t> data = {0x48, 0x65, 0x6C, 0x6C, 0x6F};
client.send_secure_data("peer_connection_id", stream_id, data, "consensus_message");

// Check security statistics
auto stats = client.get_security_stats();
std::cout << "Messages encrypted: " << stats.messages_encrypted << std::endl;
```

#### Gossip Protocol

The gossip protocol automatically encrypts and signs messages when secure messaging is enabled:

```cpp
ValidatorConfig config;
config.enable_secure_messaging = true;
// ... other security config ...

GossipProtocol gossip(config);
gossip.start();

// All gossip messages are automatically secured
NetworkMessage message;
message.type = MessageType::BLOCK_NOTIFICATION;
message.payload = block_data;

gossip.broadcast_message(message);  // Automatically encrypted and signed
```

## Security Features

### Transport Layer Security (TLS 1.3)

- **Mutual Authentication**: Both client and server verify each other's certificates
- **Perfect Forward Secrecy**: Session keys are ephemeral and not derived from long-term keys
- **Latest Protocol**: Uses TLS 1.3 for best security and performance
- **Certificate Validation**: Full certificate chain validation with configurable CA trust

### Message-Level Cryptography

- **AES-GCM Encryption**: Authenticated encryption for message confidentiality and integrity
- **Ed25519 Signatures**: Fast elliptic curve signatures for message authentication
- **Cryptographically Secure Nonces**: Uses OpenSSL RAND_bytes for unpredictable nonce generation
- **Nonce-based Replay Protection**: Prevents replay attacks with cryptographic nonces and TTL-based expiry
- **Timestamp Validation**: Rejects messages outside valid time windows

### Clock Synchronization Requirements

**Important**: The replay protection system relies on accurate timestamps and requires synchronized clocks across all validator nodes:

- **Time Sync Protocol**: All validator nodes must use NTP or equivalent time synchronization
- **Clock Skew Tolerance**: Messages with timestamps more than `message_ttl_seconds` old/future are rejected
- **Default TTL**: 300 seconds (5 minutes) provides reasonable tolerance for clock skew
- **Monitoring**: Monitor system clock synchronization status and NTP daemon health
- **Deployment**: Ensure NTP is configured before enabling secure messaging

**NTP Configuration Example**:
```bash
# Install and configure NTP
sudo apt install ntp
sudo systemctl enable ntp
sudo systemctl start ntp

# Verify time synchronization
timedatectl status
ntpq -p
```

If clocks become significantly desynchronized, valid messages may be rejected or replay attacks may succeed. Monitor NTP synchronization status in production deployments.

### Security Monitoring

The system provides detailed security statistics:

```cpp
auto stats = secure_messaging.get_security_stats();

std::cout << "Security Statistics:" << std::endl;
std::cout << "Messages encrypted: " << stats.messages_encrypted << std::endl;
std::cout << "Messages decrypted: " << stats.messages_decrypted << std::endl;
std::cout << "Signature verifications: " << stats.signature_verifications << std::endl;
std::cout << "TLS handshakes: " << stats.tls_handshakes_completed << std::endl;
std::cout << "Replay attacks blocked: " << stats.replay_attacks_blocked << std::endl;
std::cout << "Invalid signatures: " << stats.invalid_signatures_rejected << std::endl;
```

## Deployment Considerations

### Performance Impact

The cryptographic protections add minimal overhead:

- **TLS 1.3**: ~1-2ms additional handshake time, negligible ongoing overhead
- **Message Encryption**: ~0.1ms per message for AES-GCM operations
- **Message Signing**: ~0.5ms per message for Ed25519 operations
- **Memory Overhead**: ~10KB per connection for TLS state

### Backward Compatibility

The system supports graceful fallback during network upgrades:

- Secure messaging can be enabled incrementally across the network
- Nodes without security enabled can still participate (with warnings)
- Gradual migration path allows rolling upgrades without disruption

### Operational Security

Best practices for production deployment:

1. **Certificate Management**: Use proper CA infrastructure and certificate rotation
2. **Key Storage**: Store private keys with appropriate file system permissions (600)
3. **Monitoring**: Monitor security statistics for anomalies and attacks
4. **Updates**: Keep cryptographic libraries (OpenSSL) updated
5. **Backup**: Secure backup of certificates and keys with offline storage

## Troubleshooting

### Common Issues

#### TLS Handshake Failures

```
Error: TLS handshake failed
```

**Solutions**:
- Verify certificate and key files exist and are readable
- Check certificate validity dates
- Ensure CA certificate is trusted by peer
- Verify network connectivity and DNS resolution

#### Message Verification Failures

```
Error: Message signature verification failed
```

**Solutions**:
- Check peer public keys are correctly installed
- Verify message timestamps are within valid window
- Check for clock synchronization between nodes
- Monitor for replay attack attempts

#### Certificate Verification Errors

```
Error: Peer certificate verification failed
```

**Solutions**:
- Verify certificate chain is complete
- Check CA certificate is correctly configured
- Ensure certificates have not expired
- Validate certificate common names match expected peer identities

### Debug Configuration

For troubleshooting, enable additional logging:

```cpp
config.log_level = "debug";
config.enable_debug_logging = true;
```

This will provide detailed logs of:
- TLS handshake progress
- Certificate validation steps
- Message encryption/decryption operations
- Security policy enforcement decisions

## Security Considerations

### Threat Model

The secure messaging system protects against:

- **Network Eavesdropping**: All traffic is encrypted end-to-end
- **Man-in-the-Middle**: Mutual TLS authentication prevents MITM attacks
- **Message Tampering**: Cryptographic signatures detect any modifications
- **Replay Attacks**: Nonce-based protection prevents message replay
- **Identity Spoofing**: Certificate-based authentication ensures node identity

### Limitations

Current limitations and future improvements:

- **Key Rotation**: Manual certificate and key rotation (automated rotation planned)
- **Quantum Resistance**: Classical cryptography (post-quantum migration planned)
- **Hardware Security**: Software-based key storage (HSM support planned)

### Security Audit

The cryptographic implementation follows industry best practices:

- Uses well-vetted OpenSSL library for all cryptographic operations
- Implements current security standards (TLS 1.3, AES-GCM, Ed25519)
- Includes comprehensive security testing and validation
- Designed for formal security audit and certification

For security-critical deployments, consider:
- Professional security audit of the implementation
- Penetration testing of the deployed network
- Compliance validation against relevant standards (FIPS 140-2, Common Criteria)