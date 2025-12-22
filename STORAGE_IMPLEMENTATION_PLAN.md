# 💾 Storage & State Management Full Implementation Plan

**Created:** November 10, 2025  
**Issue:** #36 - Audit and Verify Missing Features for Agave Compatibility  
**Requested By:** @0xrinegade  
**Repository:** slonana-labs/slonana.cpp

## 📋 Executive Summary

This document provides a comprehensive, actionable plan to achieve **100% Storage & State Management implementation** for full Agave compatibility. The Storage layer consists of 4 major components, with 2 already complete and 2 requiring enhancement.

**Current Status:**
- ⚠️ **Accounts Database** - 50% Complete (needs versioning and tiers)
- ✅ **Blockstore** - 100% Complete
- ⚠️ **Bank State** - 60% Complete (needs fork management)
- ✅ **Snapshots** - 100% Complete

**Overall Storage Completion:** 78% → Target: 100%

---

## 🎯 Implementation Objectives

### Primary Goals
1. **Implement versioned account storage** with full history
2. **Add hot/cold storage tiers** for optimization
3. **Implement comprehensive fork state management**
4. **Add state rollback capabilities** for fork switching
5. **Enable cross-fork state queries** for consensus

### Success Criteria
- [ ] All Storage components at 100% implementation
- [ ] Versioned accounts with efficient history queries
- [ ] Hot/cold tiers with automatic migration
- [ ] Fork state management with rollback support
- [ ] Performance: <100μs account lookups, <10ms fork switches
- [ ] Full Agave compatibility for state management

---

## 🏗️ Component-by-Component Implementation Plan

## 1. Accounts Database Enhancement (50% → 100%)

### Current Status: 50% Complete ⚠️

**Implemented Features:**
- ✅ Basic account storage
- ✅ Account retrieval
- ✅ Simple indexing
- ✅ In-memory operations

**Missing Features:**
- ❌ Versioned account storage
- ❌ Account history tracking
- ❌ Background compaction
- ❌ Hot/cold storage tiers
- ❌ Efficient snapshot mechanism

### Implementation Roadmap

#### Phase 1: Versioned Account Storage (2 weeks)

**Objective:** Implement comprehensive versioned storage for account history.

**Week 1: Version Storage Infrastructure**

1. **Versioned Account Data Structure**
   ```cpp
   // In include/storage/accounts_db.h
   
   class VersionedAccountsDB {
   public:
     struct AccountVersion {
       uint64_t slot;              // Slot when modified
       uint64_t write_version;     // Global write version
       std::vector<uint8_t> data;  // Account data
       uint64_t lamports;          // Balance
       Hash owner;                 // Owner program
       bool executable;            // Executable flag
       uint64_t rent_epoch;        // Next rent payment epoch
     };
     
     struct AccountHistory {
       Hash pubkey;
       std::vector<AccountVersion> versions;
       size_t current_version_index;
     };
     
     // Versioned operations
     void store_account(
       const Hash& pubkey,
       const AccountData& data,
       uint64_t slot);
     
     std::optional<AccountData> load_account(
       const Hash& pubkey,
       uint64_t slot = LATEST_SLOT);
     
     std::vector<AccountVersion> get_account_history(
       const Hash& pubkey,
       uint64_t start_slot,
       uint64_t end_slot);
     
     // Version management
     void prune_old_versions(uint64_t before_slot);
     size_t get_version_count(const Hash& pubkey) const;
   };
   ```

2. **Write Version Tracking**
   - Global write version counter
   - Atomic increment on each write
   - Used for ordering within same slot

3. **Efficient Version Storage**
   - Delta compression for similar versions
   - Reference counting for unchanged data
   - Lazy deletion of old versions

**Week 2: History Queries & Integration**

4. **History Query API**
   ```cpp
   // Query account at specific slot
   auto account_at_slot = accounts_db->load_account(
     pubkey, target_slot);
   
   // Get all changes in range
   auto changes = accounts_db->get_account_history(
     pubkey, start_slot, end_slot);
   
   // Get version count
   size_t versions = accounts_db->get_version_count(pubkey);
   ```

5. **Integration Points**
   - Update SVM to use versioned loads
   - Update RPC methods to support historical queries
   - Update consensus to track write versions

**Deliverables:**
- `include/storage/versioned_accounts_db.h` - Interface (200+ lines)
- `src/storage/versioned_accounts_db.cpp` - Implementation (600+ lines)
- `tests/test_versioned_accounts.cpp` - Test suite (250+ lines)
- Performance benchmark report

---

#### Phase 2: Hot/Cold Storage Tiers (1-2 weeks)

**Objective:** Implement tiered storage for optimization.

**Week 1: Tier Infrastructure**

1. **Storage Tier Manager**
   ```cpp
   // In include/storage/tier_manager.h
   
   class StorageTierManager {
   public:
     enum class Tier {
       HOT,      // In-memory, frequently accessed
       WARM,     // SSD, recently accessed
       COLD      // Disk, infrequently accessed
     };
     
     struct TierConfig {
       size_t hot_tier_max_accounts = 100000;
       size_t warm_tier_max_accounts = 1000000;
       std::chrono::minutes hot_ttl{30};
       std::chrono::hours warm_ttl{24};
     };
     
     // Tier operations
     void store_account(
       const Hash& pubkey,
       const AccountData& data,
       Tier preferred_tier = Tier::HOT);
     
     std::optional<AccountData> load_account(
       const Hash& pubkey);
     
     // Tier management
     void promote_to_hot(const Hash& pubkey);
     void demote_to_cold(const Hash& pubkey);
     void run_tier_migration();
     
     // Statistics
     size_t get_hot_count() const;
     size_t get_warm_count() const;
     size_t get_cold_count() const;
     double get_hot_hit_rate() const;
   };
   ```

2. **Access Pattern Tracking**
   - Track access frequency per account
   - Track last access time
   - Use LRU for hot tier eviction

3. **Automatic Tier Migration**
   - Background thread for migration
   - Move frequently accessed to hot tier
   - Move infrequently accessed to cold tier
   - Configurable migration policies

**Week 2: Integration & Optimization**

4. **Predictive Tier Management**
   - Analyze transaction patterns
   - Pre-load likely-needed accounts to hot tier
   - Batch migrations for efficiency

5. **Performance Optimization**
   - Async loading from cold tier
   - Read-ahead for sequential access
   - Cache warming strategies

**Deliverables:**
- `include/storage/tier_manager.h` - Interface (150+ lines)
- `src/storage/tier_manager.cpp` - Implementation (500+ lines)
- `tests/test_storage_tiers.cpp` - Test suite (200+ lines)
- Migration policy documentation

---

#### Phase 3: Background Compaction (1 week)

**Objective:** Implement efficient background compaction for version pruning.

**Implementation:**

1. **Compaction Manager**
   ```cpp
   // In include/storage/compaction_manager.h
   
   class CompactionManager {
   public:
     struct CompactionConfig {
       uint64_t retain_versions = 1000;  // Keep last N versions
       uint64_t retain_slots = 10000;    // Keep last N slots
       std::chrono::hours compaction_interval{4};
       bool enable_auto_compaction = true;
     };
     
     // Compaction operations
     void start_background_compaction();
     void stop_background_compaction();
     void compact_account(const Hash& pubkey);
     void compact_slot_range(uint64_t start, uint64_t end);
     
     // Statistics
     uint64_t get_space_reclaimed() const;
     size_t get_versions_pruned() const;
   };
   ```

2. **Compaction Strategies**
   - Prune versions older than threshold
   - Keep checkpoints at regular intervals
   - Merge similar versions with delta encoding
   - Incremental compaction to avoid blocking

3. **Safety Guarantees**
   - Never compact versions needed for consensus
   - Atomic compaction operations
   - Rollback on failure

**Deliverables:**
- `include/storage/compaction_manager.h` - Interface (100+ lines)
- `src/storage/compaction_manager.cpp` - Implementation (350+ lines)
- `tests/test_compaction.cpp` - Test suite (150+ lines)

---

## 2. Bank State Enhancement (60% → 100%)

### Current Status: 60% Complete ⚠️

**Implemented Features:**
- ✅ Basic state tracking
- ✅ Account state management
- ✅ Slot progression
- ✅ Single-fork state

**Missing Features:**
- ❌ Fork state management
- ❌ State rollback capabilities
- ❌ Cross-fork state queries
- ❌ Fork reconciliation
- ❌ State caching per fork

### Implementation Roadmap

#### Phase 1: Fork State Management (2 weeks)

**Objective:** Implement comprehensive multi-fork state tracking.

**Week 1: Fork State Infrastructure**

1. **Fork State Manager**
   ```cpp
   // In include/validator/fork_state_manager.h
   
   class ForkStateManager {
   public:
     struct ForkState {
       uint64_t fork_id;
       uint64_t root_slot;
       uint64_t tip_slot;
       Hash parent_fork;
       std::unordered_map<Hash, AccountData> modified_accounts;
       std::unordered_set<Hash> deleted_accounts;
       uint64_t lamport_total;
       size_t transaction_count;
     };
     
     // Fork operations
     ForkId create_fork(uint64_t slot, ForkId parent);
     void delete_fork(ForkId fork_id);
     ForkState get_fork_state(ForkId fork_id);
     
     // Account operations per fork
     void store_account(
       ForkId fork_id,
       const Hash& pubkey,
       const AccountData& data);
     
     std::optional<AccountData> load_account(
       ForkId fork_id,
       const Hash& pubkey);
     
     // Fork queries
     std::vector<ForkId> get_active_forks();
     std::vector<ForkId> get_forks_at_slot(uint64_t slot);
     ForkId get_fork_ancestor(ForkId fork_id, uint64_t slot);
   };
   ```

2. **Fork Hierarchy Tracking**
   - Tree structure for fork relationships
   - Efficient ancestor queries
   - Fork depth tracking

3. **Copy-on-Write State**
   - Parent fork state as baseline
   - Store only modifications
   - Efficient memory usage

**Week 2: Fork Queries & Integration**

4. **Cross-Fork State Queries**
   ```cpp
   // Query account across forks
   auto account_on_fork = fork_manager->load_account(
     fork_id, pubkey);
   
   // Compare state between forks
   auto diff = fork_manager->compare_forks(
     fork_id1, fork_id2);
   
   // Get fork ancestry
   auto ancestors = fork_manager->get_ancestors(fork_id);
   ```

5. **Integration with Consensus**
   - Fork creation on new blocks
   - Fork deletion on finalization
   - Fork switching on leader change

**Deliverables:**
- `include/validator/fork_state_manager.h` - Interface (200+ lines)
- `src/validator/fork_state_manager.cpp` - Implementation (700+ lines)
- `tests/test_fork_state.cpp` - Test suite (300+ lines)

---

#### Phase 2: State Rollback Capabilities (1 week)

**Objective:** Implement efficient state rollback for fork switching.

**Implementation:**

1. **Rollback Manager**
   ```cpp
   // In include/validator/rollback_manager.h
   
   class RollbackManager {
   public:
     struct RollbackPoint {
       uint64_t slot;
       ForkId fork_id;
       Hash state_hash;
       size_t modified_accounts;
     };
     
     // Rollback operations
     void create_rollback_point(uint64_t slot);
     void rollback_to_slot(uint64_t target_slot);
     void rollback_to_fork(ForkId fork_id);
     
     // Rollback queries
     std::vector<RollbackPoint> get_rollback_points();
     bool can_rollback_to(uint64_t slot);
   };
   ```

2. **Efficient Rollback Strategy**
   - Store account deltas for rollback
   - Incremental rollback (not full replay)
   - Validate state after rollback

3. **Safety Guarantees**
   - Atomic rollback operations
   - State consistency validation
   - Recovery from partial rollback

**Deliverables:**
- `include/validator/rollback_manager.h` - Interface (120+ lines)
- `src/validator/rollback_manager.cpp` - Implementation (400+ lines)
- `tests/test_rollback.cpp` - Test suite (200+ lines)

---

#### Phase 3: State Caching & Optimization (3-4 days)

**Objective:** Optimize state access with intelligent caching.

**Implementation:**

1. **Fork State Cache**
   - Per-fork account caches
   - LRU eviction within fork
   - Cache warming on fork creation

2. **Optimizations**
   - Parallel state loading
   - Read-ahead for sequential access
   - Batch operations for efficiency

**Deliverables:**
- Enhanced fork state manager (+150 lines)
- Performance benchmarks
- Cache tuning guide

---

## 📊 Implementation Timeline

### Critical Path: Accounts DB & Bank State (5-6 weeks)

| Week | Tasks | Deliverables |
|------|-------|--------------|
| **Week 1-2** | Versioned account storage | Version tracking complete |
| **Week 3** | Hot/cold storage tiers | Tier manager functional |
| **Week 4** | Background compaction | Compaction working |
| **Week 5-6** | Fork state management | Multi-fork state complete |
| **Week 7** | State rollback | Rollback functional |

---

## 🧪 Testing Strategy

### Unit Tests
- [ ] Versioned account operations
- [ ] Tier migration logic
- [ ] Compaction algorithms
- [ ] Fork state tracking
- [ ] Rollback mechanisms

### Integration Tests
- [ ] Versioned accounts with SVM
- [ ] Tier performance under load
- [ ] Fork state with consensus
- [ ] Rollback during fork switch
- [ ] End-to-end state management

### Performance Tests
- [ ] Account lookup latency (target: <100μs)
- [ ] Version query performance
- [ ] Tier migration overhead
- [ ] Fork switch time (target: <10ms)
- [ ] Memory usage optimization

### Stress Tests
- [ ] Million+ accounts
- [ ] Deep fork trees (100+ forks)
- [ ] Rapid fork switching
- [ ] Concurrent access patterns
- [ ] Compaction under load

---

## 📝 Code Structure

### New Files to Create

```
include/storage/
  ├── versioned_accounts_db.h        (200+ lines)
  ├── tier_manager.h                 (150+ lines)
  ├── compaction_manager.h           (100+ lines)

include/validator/
  ├── fork_state_manager.h           (200+ lines)
  ├── rollback_manager.h             (120+ lines)

src/storage/
  ├── versioned_accounts_db.cpp      (600+ lines)
  ├── tier_manager.cpp               (500+ lines)
  ├── compaction_manager.cpp         (350+ lines)

src/validator/
  ├── fork_state_manager.cpp         (700+ lines)
  ├── rollback_manager.cpp           (400+ lines)

tests/
  ├── test_versioned_accounts.cpp    (250+ lines)
  ├── test_storage_tiers.cpp         (200+ lines)
  ├── test_compaction.cpp            (150+ lines)
  ├── test_fork_state.cpp            (300+ lines)
  ├── test_rollback.cpp              (200+ lines)
```

### Files to Modify

```
src/storage/accounts_db.cpp          (+100 lines)
src/validator/core.cpp               (+150 lines)
include/storage/accounts_db.h        (+50 lines)
```

**Total New Code:** ~3,620 lines  
**Total Modified Code:** ~300 lines  
**Test Code:** ~1,100 lines

---

## 🎯 Success Metrics

### Functional Metrics
- [ ] **Versioned Storage**: Full history tracking
- [ ] **Tier Management**: Automatic hot/cold migration
- [ ] **Fork State**: Multi-fork state tracking
- [ ] **Rollback**: Efficient state rollback (<10ms)

### Performance Metrics
- [ ] **Account Lookup**: <100μs average
- [ ] **Version Query**: <1ms for history
- [ ] **Fork Switch**: <10ms average
- [ ] **Memory Usage**: <1GB for 1M accounts

### Quality Metrics
- [ ] **Test Coverage**: >90% code coverage
- [ ] **Test Pass Rate**: 100% passing
- [ ] **Agave Compatibility**: State behavior matches

---

## 🚀 Getting Started

### Step 1: Versioned Storage Foundation (Week 1)
```bash
# Create versioned accounts module
touch include/storage/versioned_accounts_db.h
touch src/storage/versioned_accounts_db.cpp
touch tests/test_versioned_accounts.cpp

# Add to CMakeLists.txt
# Build and verify
make build && make test
```

### Step 2: Iterative Development
- Implement one feature at a time
- Test thoroughly after each change
- Benchmark performance continuously
- Document as you go

---

## ✅ Completion Checklist

### Accounts Database
- [ ] Versioned storage implemented
- [ ] Hot/cold tiers functional
- [ ] Background compaction working
- [ ] Performance targets met
- [ ] All tests passing

### Bank State
- [ ] Fork state management complete
- [ ] State rollback functional
- [ ] Cross-fork queries working
- [ ] Performance targets met
- [ ] All tests passing

### Documentation
- [ ] API documentation complete
- [ ] Configuration guide written
- [ ] Performance tuning guide
- [ ] Migration strategies documented

---

## 🎉 Conclusion

This comprehensive plan provides a clear path to achieving **100% Storage & State Management implementation**. The critical work focuses on:

1. **Versioned Account Storage** - Full history tracking with efficient queries
2. **Hot/Cold Storage Tiers** - Automatic optimization with tier migration
3. **Comprehensive Fork State Management** - Multi-fork state with rollback
4. **Background Compaction** - Efficient version pruning

**Estimated Timeline:** 5-7 weeks for all features

**Success Criteria:** All storage components at 100%, full Agave compatibility, performance targets met

---

**Document Version:** 1.0  
**Last Updated:** November 10, 2025  
**Status:** Ready for Implementation
