# Slonana Full Validator Implementation Plan

## Overview

This document outlines the comprehensive plan to implement a full standalone validator for Slonana, including networking, consensus, gossip protocol, RPC server, and banking components.

**Current Status:** The repository has a working SVM execution engine and BPF runtime with ML inference capabilities. This plan adds the remaining components needed for a complete validator.

**Total Estimated Effort:** 16-23 weeks

---

## Phase 0: Foundation (COMPLETE ✅)

### Components Already Implemented
- ✅ SVM execution engine (`src/svm/engine.cpp`)
- ✅ BPF interpreter and runtime
- ✅ ML inference syscalls (decision tree, neural network)
- ✅ Async BPF execution (timers, watchers, ring buffers)
- ✅ Economic opcodes (auctions, escrow, staking, reputation)
- ✅ Account management and state persistence
- ✅ Transaction processing pipeline
- ✅ Fixed-point arithmetic for BPF
- ✅ Base58 encoding/decoding

### Test Coverage
- 99+ unit tests passing
- Integration tests with SVM engine
- E2E tests for ML inference and async BPF

---

## Phase 1: Networking Layer (2-3 weeks)

### Goal
Build the foundational networking infrastructure for peer-to-peer communication, RPC serving, and cluster connectivity.

### Components to Implement

#### 1.1 Socket Infrastructure (`src/network/socket.cpp`)
```cpp
class TcpSocket {
    int listen(const std::string& address, uint16_t port);
    int accept();
    ssize_t send(const void* data, size_t len);
    ssize_t recv(void* data, size_t len);
    void set_nonblocking();
    void set_keepalive();
};

class UdpSocket {
    int bind(const std::string& address, uint16_t port);
    ssize_t sendto(const void* data, size_t len, 
                   const sockaddr* addr);
    ssize_t recvfrom(void* data, size_t len, 
                     sockaddr* addr);
};
```

**Tasks:**
- [ ] Implement TCP socket with async I/O (epoll/kqueue)
- [ ] Implement UDP socket for gossip
- [ ] Add connection timeout and retry logic
- [ ] Implement socket pooling for reuse
- [ ] Add TLS support for secure connections
- [ ] Error handling and logging

#### 1.2 Connection Pool (`src/network/connection_pool.cpp`)
```cpp
class ConnectionPool {
    Connection* get_connection(const NodeId& peer);
    void return_connection(Connection* conn);
    void close_idle_connections();
    size_t active_count();
    void set_max_connections(size_t max);
};
```

**Tasks:**
- [ ] Connection lifecycle management
- [ ] Idle connection cleanup
- [ ] Connection health monitoring
- [ ] Rate limiting per peer
- [ ] Metrics collection (connections, bytes sent/received)

#### 1.3 Protocol Serialization (`src/network/protocol.cpp`)
```cpp
class PacketSerializer {
    std::vector<uint8_t> serialize_transaction(const Transaction& tx);
    Transaction deserialize_transaction(const uint8_t* data, size_t len);
    std::vector<uint8_t> serialize_block(const Block& block);
    Block deserialize_block(const uint8_t* data, size_t len);
};
```

**Tasks:**
- [ ] Implement borsh serialization (Solana standard)
- [ ] Implement bincode as alternative
- [ ] Packet framing and length prefixes
- [ ] Compression support (zstd)
- [ ] Versioning for protocol upgrades

#### 1.4 Packet Handling (`src/network/packet_handler.cpp`)
```cpp
class PacketHandler {
    void handle_packet(const Packet& packet, const NodeId& sender);
    void route_to_handler(PacketType type, const Packet& packet);
    void register_handler(PacketType type, PacketCallback callback);
};
```

**Tasks:**
- [ ] Packet routing to appropriate handlers
- [ ] Request/response matching
- [ ] Packet validation and checksums
- [ ] DDoS protection (rate limiting, blacklisting)

**Dependencies:** None (foundational)

**Testing:**
- Unit tests for socket operations
- Integration tests for connection pooling
- Stress tests for high connection counts
- Network failure simulation

---

## Phase 2: RPC Server (2-3 weeks)

### Goal
Implement a JSON-RPC 2.0 server with WebSocket support for standard Solana RPC methods.

### Components to Implement

#### 2.1 HTTP Server (`src/rpc/http_server.cpp`)
```cpp
class HttpServer {
    void start(const std::string& bind_address, uint16_t port);
    void stop();
    void register_endpoint(const std::string& path, 
                          HttpHandler handler);
    void enable_cors();
};
```

**Tasks:**
- [ ] HTTP/1.1 server implementation
- [ ] Request parsing and routing
- [ ] Response formatting
- [ ] CORS support for browser clients
- [ ] Keep-alive connections
- [ ] Request logging and metrics

#### 2.2 JSON-RPC Handler (`src/rpc/jsonrpc.cpp`)
```cpp
class JsonRpcHandler {
    json handle_request(const json& request);
    json create_response(int id, const json& result);
    json create_error(int id, int code, const std::string& message);
};
```

**Tasks:**
- [ ] JSON-RPC 2.0 specification compliance
- [ ] Batch request support
- [ ] Error code standardization
- [ ] Request ID tracking
- [ ] Method dispatch

#### 2.3 RPC Methods (`src/rpc/methods/`)

**Account Methods:**
```cpp
// src/rpc/methods/account.cpp
json getAccountInfo(const PublicKey& address);
json getBalance(const PublicKey& address);
json getProgramAccounts(const PublicKey& program_id, 
                       const AccountFilter& filters);
json getMultipleAccounts(const std::vector<PublicKey>& addresses);
```

**Transaction Methods:**
```cpp
// src/rpc/methods/transaction.cpp
json sendTransaction(const Transaction& tx, 
                    const SendOptions& options);
json simulateTransaction(const Transaction& tx);
json getTransaction(const Signature& sig);
json getRecentBlockhash();
json getFeeForMessage(const Message& msg);
```

**Block Methods:**
```cpp
// src/rpc/methods/block.cpp
json getBlock(uint64_t slot);
json getBlockHeight();
json getSlot();
json getBlockTime(uint64_t slot);
json getBlocks(uint64_t start_slot, uint64_t end_slot);
```

**Program Methods:**
```cpp
// src/rpc/methods/program.cpp
json getTokenAccountsByOwner(const PublicKey& owner);
json getTokenSupply(const PublicKey& mint);
json getTokenAccountBalance(const PublicKey& address);
```

**Tasks:**
- [ ] Implement all standard Solana RPC methods
- [ ] Add commitment level support (processed, confirmed, finalized)
- [ ] Implement subscription methods for WebSocket
- [ ] Add rate limiting per method
- [ ] Cache frequently accessed data
- [ ] Add custom methods for ML inference queries

#### 2.4 WebSocket Server (`src/rpc/websocket.cpp`)
```cpp
class WebSocketServer {
    void start(uint16_t port);
    void subscribe(const SubscriptionId& id, 
                  SubscriptionType type,
                  const SubscriptionFilter& filter);
    void unsubscribe(const SubscriptionId& id);
    void notify_subscribers(SubscriptionType type, const json& data);
};
```

**Tasks:**
- [ ] WebSocket protocol implementation
- [ ] Subscription management
- [ ] Account change notifications
- [ ] Transaction notifications
- [ ] Slot notifications
- [ ] Program log streaming

**Dependencies:** Phase 1 (Networking)

**Testing:**
- RPC method unit tests
- Integration tests with real transactions
- Load testing with concurrent requests
- WebSocket subscription tests
- Compatibility tests with Solana CLI tools

---

## Phase 3: Gossip Protocol (3-4 weeks)

### Goal
Implement peer discovery and cluster topology maintenance using the gossip protocol.

### Components to Implement

#### 3.1 CRDS (Cluster Replicated Data Store) (`src/gossip/crds.cpp`)
```cpp
class CRDS {
    void insert(const CrdsValue& value);
    std::optional<CrdsValue> get(const CrdsLabel& label);
    std::vector<CrdsValue> get_all();
    void prune_expired(uint64_t now);
    void update_timestamp(const CrdsLabel& label, uint64_t timestamp);
};

struct CrdsValue {
    CrdsLabel label;
    CrdsData data;
    uint64_t timestamp;
    Signature signature;
};
```

**Tasks:**
- [ ] Implement CRDS storage and retrieval
- [ ] Value versioning and conflict resolution
- [ ] Expiration and pruning
- [ ] Signature verification
- [ ] Memory management for large clusters

#### 3.2 Gossip Messages (`src/gossip/messages.cpp`)
```cpp
struct ContactInfo {
    PublicKey id;
    std::string gossip_address;
    std::string tpu_address;
    std::string rpc_address;
    uint64_t wallclock;
    Signature signature;
};

struct Vote {
    PublicKey voter;
    uint64_t slot;
    Hash bank_hash;
    uint64_t timestamp;
};

struct EpochSlots {
    PublicKey from;
    std::vector<uint64_t> slots;
    uint64_t wallclock;
};
```

**Tasks:**
- [ ] Define all gossip message types
- [ ] Serialization/deserialization
- [ ] Message validation
- [ ] Signature creation and verification

#### 3.3 Gossip Protocol (`src/gossip/protocol.cpp`)
```cpp
class GossipService {
    void start(const ContactInfo& local_contact);
    void stop();
    void add_peer(const ContactInfo& peer);
    void remove_peer(const PublicKey& peer_id);
    void broadcast(const CrdsValue& value);
    std::vector<ContactInfo> get_active_peers();
};

class GossipPullHandler {
    std::vector<CrdsValue> handle_pull_request(
        const PublicKey& from,
        const BloomFilter& filter,
        const CrdsValue& caller_info);
};

class GossipPushHandler {
    void handle_push_message(
        const PublicKey& from,
        const std::vector<CrdsValue>& values);
};
```

**Tasks:**
- [ ] Implement pull protocol (periodic peer sampling)
- [ ] Implement push protocol (epidemic broadcast)
- [ ] Bloom filter for efficient data exchange
- [ ] Fan-out and push/pull scheduling
- [ ] Peer selection strategies
- [ ] Failure detection and timeout

#### 3.4 Cluster Discovery (`src/gossip/discovery.cpp`)
```cpp
class ClusterDiscovery {
    void discover_peers();
    void connect_to_entrypoint(const std::string& entrypoint);
    std::vector<ContactInfo> get_cluster_nodes();
    size_t cluster_size();
};
```

**Tasks:**
- [ ] Bootstrap from entrypoint nodes
- [ ] Maintain list of known validators
- [ ] Track stake-weighted voting power
- [ ] Identify leaders and voting nodes

**Dependencies:** Phase 1 (Networking)

**Testing:**
- Unit tests for CRDS operations
- Gossip message serialization tests
- Multi-node gossip simulation
- Network partition recovery tests
- Performance tests for large clusters (1000+ nodes)

---

## Phase 4: Consensus (4-6 weeks)

### Goal
Implement Tower BFT consensus mechanism for validator voting and fork choice.

### Components to Implement

#### 4.1 Tower BFT (`src/consensus/tower.cpp`)
```cpp
class Tower {
    void record_vote(uint64_t slot, const Hash& bank_hash);
    bool is_locked_out(uint64_t slot, const Hash& bank_hash);
    uint64_t threshold_depth();
    std::optional<uint64_t> root();
    void set_root(uint64_t slot);
};

struct Vote {
    uint64_t slot;
    Hash bank_hash;
    uint64_t timestamp;
};
```

**Tasks:**
- [ ] Implement lockout mechanism
- [ ] Vote tower storage and retrieval
- [ ] Threshold depth calculation
- [ ] Root advancement
- [ ] Vote rollback protection
- [ ] Slashing condition detection

#### 4.2 Leader Schedule (`src/consensus/leader_schedule.cpp`)
```cpp
class LeaderSchedule {
    PublicKey get_leader(uint64_t slot);
    void compute_schedule(uint64_t epoch, 
                         const std::map<PublicKey, uint64_t>& stakes);
    std::vector<PublicKey> get_epoch_leaders(uint64_t epoch);
};
```

**Tasks:**
- [ ] Stake-weighted leader selection
- [ ] Epoch boundary handling
- [ ] Leader rotation
- [ ] Cache computed schedules
- [ ] Handle stake changes

#### 4.3 Fork Choice (`src/consensus/fork_choice.cpp`)
```cpp
class ForkChoice {
    Hash select_fork(const std::vector<Hash>& candidates);
    void add_vote(const PublicKey& validator, 
                 uint64_t slot,
                 const Hash& bank_hash,
                 uint64_t stake);
    uint64_t get_vote_weight(uint64_t slot, const Hash& bank_hash);
    std::optional<Hash> get_supermajority_fork(uint64_t slot);
};
```

**Tasks:**
- [ ] Implement heaviest fork rule
- [ ] Vote aggregation by stake
- [ ] Supermajority detection (2/3+ stake)
- [ ] Fork pruning
- [ ] Optimistic confirmation

#### 4.4 Block Production (`src/consensus/block_producer.cpp`)
```cpp
class BlockProducer {
    Block produce_block(uint64_t slot, const Hash& parent_hash);
    void add_transaction_to_block(const Transaction& tx);
    void finalize_block();
    bool is_leader(uint64_t slot);
};
```

**Tasks:**
- [ ] Leader check before production
- [ ] Transaction selection from mempool
- [ ] PoH (Proof of History) tick generation
- [ ] Block signing
- [ ] Block broadcast to cluster

#### 4.5 Block Validation (`src/consensus/block_validator.cpp`)
```cpp
class BlockValidator {
    bool validate_block(const Block& block);
    bool verify_poh(const Block& block);
    bool verify_leader(const Block& block, uint64_t slot);
    bool execute_transactions(const Block& block, BankState& state);
};
```

**Tasks:**
- [ ] PoH verification
- [ ] Leader signature verification
- [ ] Transaction execution
- [ ] State hash computation
- [ ] Block replay for recovery

**Dependencies:** Phase 1 (Networking), Phase 3 (Gossip)

**Testing:**
- Tower BFT unit tests
- Fork choice simulation
- Leader schedule correctness
- Block production and validation
- Multi-validator consensus tests
- Byzantine behavior simulation

---

## Phase 5: Banking (3-4 weeks)

### Goal
Implement transaction processing pipeline with parallel execution and fee management.

### Components to Implement

#### 5.1 Transaction Queue (`src/banking/transaction_queue.cpp`)
```cpp
class TransactionQueue {
    void add_transaction(const Transaction& tx);
    std::vector<Transaction> get_batch(size_t max_count);
    void remove_transaction(const Signature& sig);
    size_t size();
    void prune_expired();
};
```

**Tasks:**
- [ ] Priority queue by fee
- [ ] Duplicate detection
- [ ] Expiration based on recent blockhash
- [ ] Memory limits and eviction
- [ ] Fair scheduling across accounts

#### 5.2 Parallel Execution (`src/banking/parallel_executor.cpp`)
```cpp
class ParallelExecutor {
    struct ExecutionResult {
        Signature signature;
        bool success;
        std::string error;
        uint64_t compute_units;
    };
    
    std::vector<ExecutionResult> execute_batch(
        const std::vector<Transaction>& txs,
        BankState& state);
    
    void detect_conflicts(const std::vector<Transaction>& txs);
};
```

**Tasks:**
- [ ] Account lock detection
- [ ] Parallel execution lanes (8+)
- [ ] Conflict resolution
- [ ] Transaction batching
- [ ] Rollback on error
- [ ] Integration with async BPF (timers, watchers)

#### 5.3 Fee Collection (`src/banking/fee_collector.cpp`)
```cpp
class FeeCollector {
    void collect_fee(const Signature& sig, uint64_t fee_lamports);
    uint64_t calculate_fee(const Transaction& tx);
    void distribute_fees(uint64_t slot);
    uint64_t get_total_fees(uint64_t slot);
};
```

**Tasks:**
- [ ] Fee calculation based on signatures and compute units
- [ ] Fee account management
- [ ] Fee distribution to validators
- [ ] Burn mechanism (50% burned in Solana)

#### 5.4 Bank State (`src/banking/bank.cpp`)
```cpp
class Bank {
    uint64_t slot;
    Hash parent_hash;
    std::map<PublicKey, Account> accounts;
    
    ProcessResult process_transaction(const Transaction& tx);
    Hash compute_state_hash();
    void commit();
    void rollback();
    Bank* fork(uint64_t new_slot);
};
```

**Tasks:**
- [ ] State management and forking
- [ ] Account state updates
- [ ] Hash computation for integrity
- [ ] Commit and rollback support
- [ ] Snapshot and restore

**Dependencies:** Phase 0 (SVM Engine)

**Testing:**
- Transaction queue unit tests
- Parallel execution tests
- Fee calculation verification
- Bank state forking tests
- Throughput benchmarks

---

## Phase 6: Integration (2-3 weeks)

### Goal
Integrate all components into a full validator binary with configuration and monitoring.

### Components to Implement

#### 6.1 Validator Binary (`src/validator/main.cpp`)
```cpp
class Validator {
    void start(const ValidatorConfig& config);
    void stop();
    void run_event_loop();
    
    // Component references
    NetworkLayer network;
    RpcServer rpc;
    GossipService gossip;
    ConsensusEngine consensus;
    BankingStage banking;
    SVMEngine svm;
};
```

**Tasks:**
- [ ] Component initialization and startup
- [ ] Graceful shutdown
- [ ] Event loop coordination
- [ ] Thread pool management
- [ ] Signal handling (SIGTERM, SIGINT)

#### 6.2 Configuration (`src/validator/config.cpp`)
```cpp
struct ValidatorConfig {
    std::string identity_keypair_path;
    std::string ledger_path;
    std::vector<std::string> entrypoints;
    
    NetworkConfig network;
    RpcConfig rpc;
    GossipConfig gossip;
    ConsensusConfig consensus;
};
```

**Tasks:**
- [ ] Configuration file parsing (TOML/JSON)
- [ ] Environment variable overrides
- [ ] Validation and defaults
- [ ] Hot reload support

#### 6.3 CLI (`src/validator/cli.cpp`)
```cpp
class ValidatorCLI {
    void parse_args(int argc, char** argv);
    void print_help();
    void print_version();
    ValidatorConfig build_config();
};
```

**Tasks:**
- [ ] Command-line argument parsing
- [ ] Subcommands (start, status, stop)
- [ ] Help text and examples
- [ ] Version information

#### 6.4 Monitoring (`src/validator/metrics.cpp`)
```cpp
class MetricsCollector {
    void record_transaction(bool success, uint64_t compute_units);
    void record_block_produced(uint64_t slot);
    void record_vote(uint64_t slot);
    json get_metrics();
    void expose_prometheus_endpoint();
};
```

**Tasks:**
- [ ] Prometheus metrics export
- [ ] Health check endpoint
- [ ] Performance counters
- [ ] Resource usage tracking (CPU, memory, network)
- [ ] Dashboard integration

#### 6.5 Logging (`src/validator/logging.cpp`)
```cpp
class Logger {
    void set_level(LogLevel level);
    void log(LogLevel level, const std::string& message);
    void enable_file_logging(const std::string& path);
    void enable_syslog();
};
```

**Tasks:**
- [ ] Structured logging (JSON)
- [ ] Log rotation
- [ ] Log levels (DEBUG, INFO, WARN, ERROR)
- [ ] Context tracking (request IDs)

**Dependencies:** All previous phases

**Testing:**
- End-to-end validator tests
- Multi-validator cluster tests
- Failover and recovery tests
- Performance benchmarks
- Documentation review

---

## Implementation Timeline

### Week-by-Week Breakdown

**Weeks 1-3:** Phase 1 - Networking Layer
- Week 1: Socket infrastructure and connection pooling
- Week 2: Protocol serialization and packet handling
- Week 3: Testing and optimization

**Weeks 4-6:** Phase 2 - RPC Server
- Week 4: HTTP server and JSON-RPC handler
- Week 5: Core RPC methods (account, transaction)
- Week 6: WebSocket subscriptions and remaining methods

**Weeks 7-10:** Phase 3 - Gossip Protocol
- Week 7: CRDS implementation
- Week 8: Gossip messages and pull protocol
- Week 9: Push protocol and peer discovery
- Week 10: Testing and cluster simulation

**Weeks 11-16:** Phase 4 - Consensus
- Week 11-12: Tower BFT and leader schedule
- Week 13-14: Fork choice and vote aggregation
- Week 15: Block production and validation
- Week 16: Multi-validator testing

**Weeks 17-20:** Phase 5 - Banking
- Week 17: Transaction queue and parallel executor
- Week 18: Fee collection and bank state
- Week 19: Integration with async BPF
- Week 20: Performance optimization

**Weeks 21-23:** Phase 6 - Integration
- Week 21: Validator binary and configuration
- Week 22: Monitoring and logging
- Week 23: Documentation and final testing

---

## Testing Strategy

### Unit Tests
- Test each component in isolation
- Mock external dependencies
- Achieve 80%+ code coverage

### Integration Tests
- Test component interactions
- Verify protocol compliance
- Test error handling and edge cases

### End-to-End Tests
- Full validator cluster (3-5 nodes)
- Transaction submission and finalization
- Leader rotation and block production
- Failover and recovery

### Performance Tests
- Transaction throughput (target: 50K+ TPS)
- Latency (target: <400ms to finalization)
- Resource usage (CPU, memory, network)
- Scalability (100+ node cluster)

### Security Tests
- Byzantine behavior simulation
- DDoS protection
- Cryptographic verification
- Permission and access control

---

## Dependencies and Libraries

### External Libraries Needed
- **Networking:** libuv or Boost.Asio for async I/O
- **HTTP:** cpp-httplib or libmicrohttpd
- **WebSocket:** websocketpp or uWebSockets
- **JSON:** nlohmann/json
- **Serialization:** borsh-cpp (custom), msgpack
- **Cryptography:** libsodium (Ed25519, SHA-256)
- **Compression:** zstd
- **Metrics:** prometheus-cpp
- **Logging:** spdlog
- **Testing:** Google Test, Google Mock

### Build System Updates
- Update CMakeLists.txt to include new targets
- Add dependency management (vcpkg or conan)
- Configure CI/CD for automated testing

---

## Risks and Mitigations

### Technical Risks
1. **Performance:** May not achieve 50K+ TPS initially
   - *Mitigation:* Profile and optimize hotspots, consider parallelization

2. **Consensus bugs:** Tower BFT is complex and error-prone
   - *Mitigation:* Extensive testing, formal verification if possible

3. **Network reliability:** Gossip may not converge in large clusters
   - *Mitigation:* Test with realistic network conditions, add fallback mechanisms

4. **Memory usage:** CRDS may grow unbounded
   - *Mitigation:* Implement pruning and memory limits

### Project Risks
1. **Timeline:** 16-23 weeks is aggressive
   - *Mitigation:* Prioritize MVP features, defer optimizations

2. **Testing complexity:** Multi-node testing is challenging
   - *Mitigation:* Build simulation framework early

3. **Integration issues:** Components may not work together smoothly
   - *Mitigation:* Incremental integration, continuous testing

---

## Success Criteria

### MVP (Minimum Viable Product)
- [ ] Validator can join a cluster via gossip
- [ ] Validator can receive transactions via RPC
- [ ] Validator can produce blocks when leader
- [ ] Validator can vote on blocks
- [ ] Validator can serve basic RPC queries

### Full Feature Set
- [ ] All standard Solana RPC methods implemented
- [ ] WebSocket subscriptions working
- [ ] Parallel transaction execution
- [ ] ML inference syscalls accessible via RPC
- [ ] Async BPF features (timers, watchers) functional
- [ ] Economic opcodes operational
- [ ] Prometheus metrics and monitoring
- [ ] Documentation complete

### Performance Targets
- [ ] 10K+ TPS sustained
- [ ] <1 second confirmation time
- [ ] <100MB memory per validator
- [ ] Support 100+ node clusters

---

## Next Steps

1. **Review and approval:** Get stakeholder feedback on this plan
2. **Environment setup:** Set up development environment with required libraries
3. **Phase 1 kickoff:** Begin networking layer implementation
4. **Continuous integration:** Set up CI/CD for automated testing
5. **Documentation:** Maintain technical documentation throughout

---

## Conclusion

This plan provides a comprehensive roadmap to transform the current SVM execution engine and BPF runtime into a full standalone validator. The phased approach allows for incremental progress and testing, while the detailed task breakdown ensures clear accountability.

The existing ML inference and async BPF capabilities will be integrated throughout, making this a unique blockchain validator with built-in AI capabilities for autonomous agents.

**Current Status:** Phase 0 complete, ready to begin Phase 1.

**Estimated Completion:** 16-23 weeks from Phase 1 start date.
