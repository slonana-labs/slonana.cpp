# 🔧 Agave Protocol Technical Specifications

**Date:** September 8, 2025  
**Related:** [AGAVE_COMPATIBILITY_AUDIT.md](./AGAVE_COMPATIBILITY_AUDIT.md) | [AGAVE_IMPLEMENTATION_PLAN.md](./AGAVE_IMPLEMENTATION_PLAN.md)  
**Repository:** slonana-labs/slonana.cpp  

## 📖 Overview

This document provides detailed technical specifications for implementing Agave protocol compatibility in slonana.cpp. Each section includes exact protocol definitions, data structures, and implementation requirements.

## 🏗️ Core Protocol Specifications

### **1. Tower BFT Consensus Protocol**

#### **1.1 Tower Structure**
```cpp
namespace slonana::consensus {

struct TowerSlot {
    uint64_t slot;
    uint64_t confirmation_count;
    std::chrono::steady_time timestamp;
};

class Tower {
private:
    std::vector<TowerSlot> tower_slots_;
    uint64_t root_slot_;
    uint64_t last_vote_slot_;
    
public:
    // Core Tower BFT operations
    bool can_vote_on_slot(uint64_t slot) const;
    uint64_t calculate_lockout_period(size_t confirmation_count) const;
    bool is_slot_locked_out(uint64_t slot) const;
    void record_vote(uint64_t slot);
    uint64_t get_vote_threshold(uint64_t slot) const;
};

// Lockout calculation: 2^confirmation_count ticks
constexpr uint64_t TOWER_BFT_MAX_LOCKOUT = 1ULL << 32;
constexpr size_t TOWER_BFT_THRESHOLD = 32; // 2/3 + 1 confirmation threshold
}
```

#### **1.2 Vote State Management**
```cpp
namespace slonana::consensus {

struct VoteState {
    std::vector<Lockout> votes;
    uint64_t root_slot;
    std::vector<uint8_t> authorized_voter; // 32-byte pubkey
    std::vector<uint8_t> node_pubkey;      // 32-byte pubkey
    uint64_t commission;
    
    // Agave compatibility methods
    bool is_valid_vote(uint64_t slot) const;
    uint64_t last_voted_slot() const;
    void process_vote(uint64_t slot, uint64_t timestamp);
    std::vector<uint64_t> slots_in_lockout(uint64_t current_slot) const;
};

struct Lockout {
    uint64_t slot;
    uint32_t confirmation_count;
    
    uint64_t lockout_period() const {
        return std::min(1ULL << confirmation_count, TOWER_BFT_MAX_LOCKOUT);
    }
    
    bool is_locked_out_at_slot(uint64_t other_slot) const {
        return other_slot > slot + lockout_period();
    }
};
}
```

### **2. Turbine Protocol Specification**

#### **2.1 Turbine Tree Construction**
```cpp
namespace slonana::network {

struct TurbineNode {
    std::vector<uint8_t> pubkey;
    std::string address;
    uint16_t port;
    uint32_t stake_weight;
};

class TurbineTree {
private:
    std::vector<TurbineNode> nodes_;
    uint32_t fanout_;
    uint32_t max_retransmits_;
    
public:
    // Agave-compatible tree construction
    void construct_tree(const std::vector<TurbineNode>& validators);
    std::vector<TurbineNode> get_children(const TurbineNode& node) const;
    std::vector<TurbineNode> get_retransmit_peers(const TurbineNode& node) const;
    
    // Tree parameters (match Agave)
    static constexpr uint32_t DATA_PLANE_FANOUT = 8;
    static constexpr uint32_t FORWARD_PLANE_FANOUT = 16;
    static constexpr uint32_t MAX_RETRANSMIT_PEERS = 4;
};
}
```

#### **2.2 Shred Processing**
```cpp
namespace slonana::network {

enum class ShredType : uint8_t {
    DATA = 0,
    CODING = 1,
};

struct ShredHeader {
    uint8_t signature[64];
    uint8_t variant;
    uint64_t slot;
    uint32_t index;
    uint16_t version;
    uint16_t fec_set_index;
} __attribute__((packed));

class Shred {
private:
    ShredHeader header_;
    std::vector<uint8_t> payload_;
    
public:
    // Agave-compatible shred processing
    bool verify_signature(const std::vector<uint8_t>& pubkey) const;
    ShredType get_type() const;
    uint64_t slot() const { return header_.slot; }
    uint32_t index() const { return header_.index; }
    
    // Serialization (match Agave format)
    std::vector<uint8_t> serialize() const;
    static std::optional<Shred> deserialize(const std::vector<uint8_t>& data);
    
    // Constants (must match Agave)
    static constexpr size_t MAX_SHRED_SIZE = 1280;
    static constexpr size_t SHRED_HEADER_SIZE = sizeof(ShredHeader);
};

class TurbineBroadcast {
public:
    void broadcast_shreds(const std::vector<Shred>& shreds);
    void handle_received_shred(const Shred& shred, const std::string& from);
    void retransmit_shred(const Shred& shred, const std::vector<TurbineNode>& peers);
};
}
```

### **3. QUIC Protocol Integration**

#### **3.1 QUIC Connection Management**
```cpp
namespace slonana::network {

class QuicConnection {
private:
    void* quic_connection_; // Platform-specific QUIC handle
    std::string peer_address_;
    uint16_t peer_port_;
    bool is_server_;
    
public:
    QuicConnection(const std::string& address, uint16_t port, bool server = false);
    ~QuicConnection();
    
    // Connection lifecycle
    bool connect(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));
    void disconnect();
    bool is_connected() const;
    
    // Stream operations
    uint64_t open_stream();
    bool send_data(uint64_t stream_id, const std::vector<uint8_t>& data);
    std::optional<std::vector<uint8_t>> receive_data(uint64_t stream_id);
    void close_stream(uint64_t stream_id);
    
    // Agave-compatible settings
    static constexpr uint32_t MAX_CONCURRENT_STREAMS = 100;
    static constexpr uint32_t MAX_STREAM_DATA = 1024 * 1024; // 1MB
    static constexpr uint32_t CONNECTION_IDLE_TIMEOUT = 30000; // 30s
};

class QuicServer {
private:
    void* quic_listener_;
    std::thread accept_thread_;
    std::function<void(std::unique_ptr<QuicConnection>)> connection_handler_;
    
public:
    bool start(const std::string& bind_address, uint16_t port);
    void stop();
    void set_connection_handler(std::function<void(std::unique_ptr<QuicConnection>)> handler);
};
}
```

#### **3.2 TLS Configuration**
```cpp
namespace slonana::network {

struct TlsConfig {
    std::string cert_file;
    std::string key_file;
    std::string ca_file;
    bool verify_peer;
    
    // Agave-compatible cipher suites
    std::vector<std::string> cipher_suites = {
        "TLS_AES_256_GCM_SHA384",
        "TLS_CHACHA20_POLY1305_SHA256",
        "TLS_AES_128_GCM_SHA256"
    };
};

class TlsManager {
public:
    static bool initialize_tls(const TlsConfig& config);
    static void cleanup_tls();
    static bool verify_certificate(const std::vector<uint8_t>& cert_data);
};
}
```

### **4. Enhanced Banking Stage**

#### **4.1 Transaction Pipeline**
```cpp
namespace slonana::banking {

enum class TransactionStage {
    FETCH,
    SIGVERIFY,
    BANKING,
    EXECUTE,
    RECORD
};

struct TransactionBatch {
    std::vector<common::Transaction> transactions;
    std::vector<std::vector<uint8_t>> signatures;
    uint64_t batch_id;
    std::chrono::steady_time received_time;
};

class BankingStage {
private:
    std::vector<std::thread> worker_threads_;
    std::queue<TransactionBatch> pending_batches_;
    std::mutex batch_mutex_;
    std::condition_variable batch_cv_;
    
    // Stage-specific processors
    std::unique_ptr<SigVerifyStage> sigverify_stage_;
    std::unique_ptr<ExecuteStage> execute_stage_;
    std::unique_ptr<RecordStage> record_stage_;
    
public:
    bool initialize(const common::ValidatorConfig& config);
    void start();
    void stop();
    
    // Transaction processing pipeline
    void submit_batch(TransactionBatch&& batch);
    void process_batch(const TransactionBatch& batch);
    
    // Performance monitoring
    struct BankingMetrics {
        uint64_t transactions_processed;
        uint64_t transactions_failed;
        double average_processing_time_ms;
        uint64_t queue_depth;
    } metrics_;
    
    // Agave-compatible parameters
    static constexpr size_t MAX_BATCH_SIZE = 128;
    static constexpr size_t MAX_QUEUE_DEPTH = 1000;
    static constexpr std::chrono::milliseconds BATCH_TIMEOUT{10};
};
}
```

#### **4.2 Parallel Execution Engine**
```cpp
namespace slonana::banking {

class ParallelExecutor {
private:
    size_t thread_count_;
    std::vector<std::thread> execution_threads_;
    
public:
    struct ExecutionResult {
        bool success;
        uint64_t compute_units_consumed;
        std::string error_message;
        std::vector<uint8_t> return_data;
    };
    
    std::vector<ExecutionResult> execute_transactions_parallel(
        const std::vector<common::Transaction>& transactions,
        const svm::AccountLoader& account_loader
    );
    
    // Account conflict detection
    std::vector<std::vector<size_t>> detect_conflicts(
        const std::vector<common::Transaction>& transactions
    ) const;
    
    // Agave-compatible execution parameters
    static constexpr size_t MAX_PARALLEL_THREADS = 16;
    static constexpr uint64_t MAX_COMPUTE_UNITS_PER_BLOCK = 48_000_000;
};
}
```

## 🌐 Network Protocol Specifications

### **5. Gossip Protocol Enhancement**

#### **5.1 Cluster Info Structure**
```cpp
namespace slonana::network {

struct ContactInfo {
    std::vector<uint8_t> pubkey;           // 32 bytes
    std::vector<uint8_t> gossip_addr;      // IP:Port for gossip
    std::vector<uint8_t> tvu_addr;         // IP:Port for TVU
    std::vector<uint8_t> tpu_addr;         // IP:Port for TPU
    std::vector<uint8_t> rpc_addr;         // IP:Port for RPC
    uint64_t wallclock;                    // Timestamp
    uint64_t shred_version;                // Network version
};

struct Vote {
    std::vector<uint8_t> signature;        // 64 bytes
    std::vector<uint8_t> vote_account;     // 32 bytes
    std::vector<uint64_t> slots;           // Voted slots
    uint64_t timestamp;
    std::vector<uint8_t> hash;             // Vote hash
};

class GossipMessage {
public:
    enum Type {
        CONTACT_INFO = 0,
        VOTE = 1,
        VERSIONED_VALUE = 2,
        SNAPSHOT_HASHES = 3,
        ACCOUNTS_HASHES = 4,
        EPOCH_SLOTS = 5,
        LEGACY_VERSION = 6,
        VERSION = 7,
        NODE_INSTANCE = 8,
        DUPLICATE_SHRED = 9,
        INCREMENTAL_SNAPSHOT_HASHES = 10,
        LOWEST_SLOT = 11,
    };
    
    Type type;
    std::vector<uint8_t> data;
    uint64_t wallclock;
    std::vector<uint8_t> signature;
    
    // Serialization (must match Agave bincode format)
    std::vector<uint8_t> serialize() const;
    static std::optional<GossipMessage> deserialize(const std::vector<uint8_t>& data);
};
}
```

#### **5.2 Pull/Push Protocol**
```cpp
namespace slonana::network {

class GossipProtocol {
private:
    std::map<std::vector<uint8_t>, ContactInfo> cluster_info_;
    std::map<std::vector<uint8_t>, Vote> recent_votes_;
    
public:
    // Pull request handling
    struct PullRequest {
        std::vector<uint8_t> filter;       // Bloom filter
        std::vector<uint8_t> value_hash;   // Hash of known values
        uint64_t wallclock;
    };
    
    struct PullResponse {
        std::vector<GossipMessage> messages;
    };
    
    PullResponse handle_pull_request(const PullRequest& request);
    void handle_pull_response(const PullResponse& response);
    
    // Push message handling
    void push_message(const GossipMessage& message);
    std::vector<GossipMessage> generate_push_messages();
    
    // Agave-compatible parameters
    static constexpr size_t PULL_REQUEST_INTERVAL_MS = 200;
    static constexpr size_t PUSH_MESSAGE_INTERVAL_MS = 100;
    static constexpr size_t MAX_PULL_REQUESTS = 20;
    static constexpr size_t CRDS_UNIQUE_PUBKEY_CAPACITY = 8192;
};
}
```

### **6. RPC API Complete Specification**

#### **6.1 Missing Critical Methods**
```cpp
namespace slonana::rpc {

class RpcServer {
public:
    // Block-related methods
    json getBlockTime(uint64_t slot);
    json getBlocks(uint64_t start_slot, std::optional<uint64_t> end_slot);
    json getBlocksWithLimit(uint64_t start_slot, uint64_t limit);
    
    // Transaction methods
    json simulateTransaction(const std::string& transaction_base64,
                           const json& config = json::object());
    json sendBundle(const std::vector<std::string>& transactions);
    
    // Validator methods
    json getVoteAccounts(const json& config = json::object());
    json getValidatorInfo();
    json getVersion();
    
    // Network methods
    json getClusterNodes();
    json getRecentPerformanceSamples(std::optional<size_t> limit);
    json getSupply(const json& config = json::object());
    
    // Subscription methods (WebSocket)
    void accountSubscribe(const std::string& account_pubkey, const json& config);
    void slotSubscribe();
    void voteSubscribe();
    void rootSubscribe();
    
private:
    // Helper methods for complex operations
    json format_account_info(const svm::Account& account, uint64_t slot);
    json format_transaction_status(const common::Transaction& tx, uint64_t slot);
    std::vector<uint8_t> simulate_transaction_execution(
        const common::Transaction& tx,
        const json& config
    );
};

// Response format structures (must match Agave exactly)
struct RpcResponse {
    json jsonrpc = "2.0";
    json id;
    json result;
    std::optional<json> error;
    
    json to_json() const;
};

struct RpcError {
    int code;
    std::string message;
    std::optional<json> data;
};

// Standard RPC error codes (match Agave)
constexpr int RPC_PARSE_ERROR = -32700;
constexpr int RPC_INVALID_REQUEST = -32600;
constexpr int RPC_METHOD_NOT_FOUND = -32601;
constexpr int RPC_INVALID_PARAMS = -32602;
constexpr int RPC_INTERNAL_ERROR = -32603;
constexpr int RPC_SLOT_SKIPPED = -32007;
constexpr int RPC_NO_LEADER_SCHEDULE = -32008;
constexpr int RPC_KEY_EXCLUDED_FROM_SECONDARY_INDEX = -32009;
}
```

## 💾 Storage & State Management

### **7. Enhanced Accounts Database**

#### **7.1 Account Storage Structure**
```cpp
namespace slonana::storage {

struct AccountMetadata {
    uint64_t lamports;
    std::vector<uint8_t> owner;            // 32-byte pubkey
    bool executable;
    uint64_t rent_epoch;
    std::vector<uint8_t> data;
};

struct AccountVersion {
    AccountMetadata account;
    uint64_t slot;
    uint64_t write_version;
    std::chrono::steady_time timestamp;
};

class AccountsDb {
private:
    std::map<std::vector<uint8_t>, std::vector<AccountVersion>> account_versions_;
    std::map<uint64_t, std::set<std::vector<uint8_t>>> slot_accounts_;
    uint64_t write_version_counter_;
    
public:
    // Account operations
    std::optional<AccountMetadata> load_account(
        const std::vector<uint8_t>& pubkey,
        uint64_t slot
    ) const;
    
    void store_account(
        const std::vector<uint8_t>& pubkey,
        const AccountMetadata& account,
        uint64_t slot
    );
    
    void remove_account(const std::vector<uint8_t>& pubkey, uint64_t slot);
    
    // Garbage collection
    void clean_accounts(uint64_t max_clean_root);
    void shrink_account_storage();
    
    // Snapshot support
    std::vector<uint8_t> create_accounts_hash(uint64_t slot) const;
    bool verify_accounts_hash(uint64_t slot, const std::vector<uint8_t>& expected_hash) const;
    
    // Performance optimization
    struct AccountsDbMetrics {
        uint64_t total_accounts;
        uint64_t total_storage_size;
        uint64_t cache_hit_rate_percent;
        uint64_t gc_runs;
    } metrics_;
    
    // Agave-compatible parameters
    static constexpr size_t ACCOUNT_CACHE_SIZE = 1000000;
    static constexpr size_t MAX_ACCOUNT_DATA_SIZE = 10 * 1024 * 1024; // 10MB
};
}
```

### **8. Advanced Fork Choice Algorithm**

#### **8.1 Weighted Fork Choice**
```cpp
namespace slonana::consensus {

struct ForkWeight {
    uint64_t slot;
    uint64_t stake_weight;
    uint64_t vote_count;
    std::chrono::steady_time last_vote_time;
};

class ForkChoice {
private:
    std::map<uint64_t, ForkWeight> fork_weights_;
    std::map<uint64_t, std::vector<uint64_t>> fork_children_;
    uint64_t root_slot_;
    
public:
    // Fork choice algorithm (matches Agave behavior)
    uint64_t select_best_fork(uint64_t current_slot) const;
    void add_vote(uint64_t slot, uint64_t stake_weight);
    void set_root(uint64_t new_root);
    
    // Optimistic confirmation
    bool is_optimistically_confirmed(uint64_t slot, uint64_t current_slot) const;
    std::optional<uint64_t> get_optimistically_confirmed_slot() const;
    
    // Fork management
    void add_fork(uint64_t slot, uint64_t parent_slot);
    std::vector<uint64_t> get_descendants(uint64_t slot) const;
    std::vector<uint64_t> get_ancestors(uint64_t slot) const;
    
    // Agave-compatible thresholds
    static constexpr double OPTIMISTIC_CONFIRMATION_THRESHOLD = 0.67; // 2/3
    static constexpr size_t SWITCH_FORK_THRESHOLD = 38; // Slots to switch
    static constexpr size_t DUPLICATE_THRESHOLD = 20;  // Duplicate detection
};
}
```

## 🔒 Security & Validation Specifications

### **9. Input Validation Framework**

#### **9.1 Transaction Validation**
```cpp
namespace slonana::validation {

class TransactionValidator {
public:
    enum class ValidationError {
        INVALID_SIGNATURE = 1,
        INSUFFICIENT_FUNDS = 2,
        INVALID_ACCOUNT_DATA = 3,
        PROGRAM_ERROR = 4,
        INVALID_INSTRUCTION = 5,
        DUPLICATE_SIGNATURE = 6,
        ACCOUNT_LOAD_ERROR = 7,
        INVALID_COMPUTE_BUDGET = 8,
        INVALID_RENT_EXEMPT = 9,
        INVALID_ACCOUNT_OWNER = 10
    };
    
    struct ValidationResult {
        bool is_valid;
        std::vector<ValidationError> errors;
        uint64_t compute_units_required;
    };
    
    ValidationResult validate_transaction(const common::Transaction& tx) const;
    
private:
    bool validate_signatures(const common::Transaction& tx) const;
    bool validate_account_access(const common::Transaction& tx) const;
    bool validate_program_instructions(const common::Transaction& tx) const;
    bool validate_compute_budget(const common::Transaction& tx) const;
};
}
```

#### **9.2 Network Message Validation**
```cpp
namespace slonana::validation {

class NetworkValidator {
public:
    // Gossip message validation
    bool validate_gossip_message(const network::GossipMessage& msg) const;
    bool validate_cluster_info(const network::ContactInfo& info) const;
    bool validate_vote_message(const network::Vote& vote) const;
    
    // Shred validation
    bool validate_shred(const network::Shred& shred) const;
    bool validate_shred_signature(const network::Shred& shred, 
                                 const std::vector<uint8_t>& expected_pubkey) const;
    
    // Rate limiting
    bool check_rate_limit(const std::string& peer_address, 
                         network::GossipMessage::Type msg_type) const;
    
private:
    std::map<std::string, std::map<network::GossipMessage::Type, uint64_t>> rate_limits_;
    
    // Rate limit thresholds (match Agave)
    static constexpr uint64_t GOSSIP_RATE_LIMIT_PER_SECOND = 100;
    static constexpr uint64_t VOTE_RATE_LIMIT_PER_SECOND = 1000;
    static constexpr uint64_t SHRED_RATE_LIMIT_PER_SECOND = 10000;
};
}
```

## 🎯 Implementation Priorities

### **Critical Path Items (Week 1-4)**
1. **Tower BFT Consensus** - Essential for network participation
2. **Turbine Protocol** - Required for efficient shred distribution
3. **QUIC Integration** - Necessary for optimal performance
4. **Basic Banking Stage** - Minimum transaction processing

### **High Priority Items (Week 5-8)**
1. **Enhanced RPC API** - Complete API compatibility
2. **Accounts Database** - Advanced storage capabilities
3. **Fork Choice Algorithm** - Sophisticated fork selection
4. **Security Validation** - Comprehensive input validation

### **Medium Priority Items (Week 9-12)**
1. **Performance Optimization** - Achieve target benchmarks
2. **Program Cache** - Improved SVM performance
3. **Advanced Monitoring** - Production-ready metrics
4. **Documentation** - Complete technical documentation

## 📋 Validation Checklist

### **Protocol Compatibility**
- [ ] Tower BFT matches Agave behavior exactly
- [ ] Turbine protocol compatible with Agave validators
- [ ] QUIC connections work with Agave peers
- [ ] Gossip messages use identical format
- [ ] RPC responses match Agave format exactly

### **Performance Requirements**
- [ ] Transaction throughput ≥ 4,000 TPS
- [ ] RPC latency < 4ms average
- [ ] Memory usage < 100MB
- [ ] CPU usage < 10%
- [ ] Network efficiency improved by 20%

### **Security Standards**
- [ ] All inputs validated against Agave specifications
- [ ] Buffer overflows prevented
- [ ] Memory safety verified
- [ ] Cryptographic operations secure
- [ ] Rate limiting implemented

---

## 🎉 Conclusion

This technical specification provides the detailed implementation requirements for achieving full Agave compatibility. Each component is designed to match Agave's exact behavior while maintaining slonana.cpp's performance advantages.

**Implementation Success Factors:**
1. **Exact Protocol Compliance** - Follow specifications precisely
2. **Performance Focus** - Maintain efficiency advantages
3. **Thorough Testing** - Validate against Agave behavior
4. **Security First** - Implement robust validation

With these specifications, slonana.cpp will achieve seamless Agave network compatibility while setting new performance standards in the Solana ecosystem.

---

*This document serves as the definitive technical reference for Agave compatibility implementation.*