# 🌐 RPC API Full Implementation Plan

**Created:** November 10, 2025  
**Issue:** #36 - Audit and Verify Missing Features for Agave Compatibility  
**Requested By:** @0xrinegade  
**Repository:** slonana-labs/slonana.cpp

## 📋 Executive Summary

This document provides a comprehensive, actionable plan to achieve **100% RPC API implementation** for full Agave compatibility. The RPC API consists of 6 method categories, with 2 fully complete and 4 requiring additional methods.

**Current Status:**
- ✅ **RPC Server Infrastructure** - 100% Complete
- ✅ **Account Methods** - 100% Complete (8/8)
- ⚠️ **Block Methods** - 83% Complete (10/12)
- ⚠️ **Transaction Methods** - 80% Complete (12/15)
- ⚠️ **Validator Methods** - 63% Complete (5/8)
- ⚠️ **Network Methods** - 71% Complete (5/7)

**Overall RPC Completion:** 83% → Target: 100%

---

## 🎯 Implementation Objectives

### Primary Goals
1. **Implement missing block methods** (2 methods)
2. **Implement missing transaction methods** (3 methods)
3. **Implement missing validator methods** (3 methods)
4. **Implement missing network methods** (2 methods)
5. **Validate** 100% compatibility with Agave RPC API

### Success Criteria
- [ ] All RPC methods implemented (47/47 = 100%)
- [ ] Response formats match Agave exactly
- [ ] Performance: <10ms p95 latency for all methods
- [ ] All methods tested and validated
- [ ] Full compatibility with Agave RPC clients

---

## 🏗️ Component-by-Component Implementation Plan

## 1. Block Methods Enhancement (83% → 100%)

### Current Status: 10/12 Methods Implemented ⚠️

**Implemented Methods:**
- ✅ `getBlock`
- ✅ `getBlockHeight`
- ✅ `getBlockCommitment`
- ✅ `getBlockProduction`
- ✅ `getBlocks`
- ✅ `getBlocksWithLimit`
- ✅ `getConfirmedBlock` (deprecated)
- ✅ `getFirstAvailableBlock`
- ✅ `getLatestBlockhash`
- ✅ `getRecentBlockhash` (deprecated)

**Missing Methods:**
- ❌ `getBlockTime` - Returns estimated production time for a block
- ❌ Enhanced `getBlocks` - Range query variant with better performance

### Implementation Roadmap (1 week)

#### Method 1: getBlockTime

**Objective:** Return the estimated production time of a block.

**Implementation:**

```cpp
// In src/network/rpc_server.cpp

std::string handle_getBlockTime(const std::string& params) {
  // Parse slot number from params
  uint64_t slot = parse_slot_param(params);
  
  // Get block from ledger
  auto block = ledger_manager_->get_block(slot);
  if (!block) {
    return create_error_response(-32009, "Block not available");
  }
  
  // Get block timestamp
  std::optional<int64_t> block_time = block->get_block_time();
  if (!block_time) {
    return create_null_response(); // Block time not available
  }
  
  // Return Unix timestamp
  return create_success_response(std::to_string(*block_time));
}
```

**Requirements:**
- Add timestamp tracking to block structure
- Store timestamp during block production
- Return Unix timestamp (seconds since epoch)
- Handle case where timestamp is unavailable

**Testing:**
- Test with recent blocks
- Test with old blocks (timestamp may be unavailable)
- Validate timestamp accuracy
- Cross-validate with Agave

**Deliverables:**
- Updated `src/network/rpc_server.cpp` (+50 lines)
- Updated block structure with timestamp (+20 lines)
- Test cases in `tests/test_rpc_methods.cpp` (+30 lines)

---

#### Method 2: Enhanced getBlocks (Range Query)

**Objective:** Optimize range query variant of getBlocks for better performance.

**Implementation:**

```cpp
// In src/network/rpc_server.cpp

std::string handle_getBlocks_range(const std::string& params) {
  // Parse start and end slots
  auto [start_slot, end_slot, commitment] = 
    parse_getBlocks_params(params);
  
  // Validate range
  if (end_slot < start_slot) {
    return create_error_response(-32602, "Invalid range");
  }
  
  size_t max_range = 500000; // Agave limit
  if (end_slot - start_slot > max_range) {
    return create_error_response(-32602, 
      "Range too large (max 500000)");
  }
  
  // Efficient range query using block index
  std::vector<uint64_t> slots;
  slots.reserve(end_slot - start_slot + 1);
  
  for (uint64_t slot = start_slot; slot <= end_slot; ++slot) {
    if (ledger_manager_->has_block(slot)) {
      slots.push_back(slot);
    }
  }
  
  return create_array_response(slots);
}
```

**Optimizations:**
- Use block index for fast existence checks
- Reserve vector capacity for efficiency
- Batch lookups where possible
- Add caching for recent ranges

**Deliverables:**
- Enhanced range query implementation (+80 lines)
- Block index optimization (+50 lines)
- Performance benchmarks
- Test cases (+40 lines)

---

## 2. Transaction Methods Enhancement (80% → 100%)

### Current Status: 12/15 Methods Implemented ⚠️

**Implemented Methods:**
- ✅ `getTransaction`
- ✅ `getSignaturesForAddress`
- ✅ `getSignatureStatuses`
- ✅ `sendTransaction`
- ✅ `getFeeForMessage`
- ✅ `getMinimumBalanceForRentExemption`
- ✅ `getRecentPrioritizationFees`
- ✅ `isBlockhashValid`
- ✅ `requestAirdrop`
- ✅ `getTransactionCount`
- ✅ `getMaxRetransmitSlot`
- ✅ `getRecentSignaturesForAddress` (deprecated)

**Missing Methods:**
- ❌ `simulateTransaction` - Dry-run transaction execution
- ❌ `sendBundle` - Bundle transaction submission (MEV)
- ❌ `getMaxShredInsertSlot` - Returns the max shred insert slot

### Implementation Roadmap (2 weeks)

#### Method 1: simulateTransaction

**Objective:** Simulate transaction execution without committing to blockchain.

**Implementation:**

```cpp
// In src/network/rpc_server.cpp

std::string handle_simulateTransaction(const std::string& params) {
  // Parse transaction and options
  auto [tx_data, options] = parse_simulate_params(params);
  
  // Decode transaction
  auto transaction = decode_transaction(tx_data);
  if (!transaction) {
    return create_error_response(-32602, "Invalid transaction");
  }
  
  // Create simulation context
  SimulationContext sim_ctx;
  sim_ctx.sig_verify = options.sig_verify;
  sim_ctx.replace_recent_blockhash = options.replace_recent_blockhash;
  sim_ctx.commitment = options.commitment;
  
  // Simulate in SVM
  auto result = svm_engine_->simulate_transaction(
    transaction, sim_ctx);
  
  // Build response
  nlohmann::json response;
  response["err"] = result.error ? 
    serialize_error(result.error) : nullptr;
  response["logs"] = result.logs;
  response["accounts"] = serialize_accounts(result.accounts);
  response["unitsConsumed"] = result.compute_units_consumed;
  response["returnData"] = result.return_data;
  
  return create_success_response(response);
}
```

**Requirements:**
- Integrate with SVM simulation mode
- Support signature verification bypass
- Handle blockhash replacement
- Return detailed execution results (logs, accounts, compute units)
- Don't modify actual state

**Testing:**
- Simulate successful transactions
- Simulate failing transactions
- Test with invalid transactions
- Verify no state changes
- Cross-validate with Agave

**Deliverables:**
- `simulateTransaction` implementation (+150 lines)
- SVM simulation mode enhancement (+100 lines)
- Comprehensive test suite (+100 lines)

---

#### Method 2: sendBundle

**Objective:** Submit bundle of transactions for MEV protection.

**Implementation:**

```cpp
// In src/network/rpc_server.cpp

std::string handle_sendBundle(const std::string& params) {
  // Parse bundle transactions
  auto bundle = parse_bundle_params(params);
  
  // Validate bundle
  if (bundle.transactions.empty()) {
    return create_error_response(-32602, "Empty bundle");
  }
  
  if (bundle.transactions.size() > 5) { // Agave limit
    return create_error_response(-32602, "Bundle too large");
  }
  
  // Submit to banking stage as atomic bundle
  auto bundle_id = banking_stage_->submit_bundle(bundle);
  
  // Return bundle ID
  return create_success_response(bundle_id);
}
```

**Requirements:**
- Bundle validation
- Atomic execution guarantee
- Integration with MEV protection in banking stage
- Bundle tracking and status

**Deliverables:**
- `sendBundle` implementation (+100 lines)
- Bundle handling in banking stage (+150 lines)
- Test cases (+80 lines)

---

#### Method 3: getMaxShredInsertSlot

**Objective:** Return the maximum slot that has received a shred.

**Implementation:**

```cpp
// In src/network/rpc_server.cpp

std::string handle_getMaxShredInsertSlot(const std::string& params) {
  // Get from ledger manager
  uint64_t max_slot = ledger_manager_->get_max_shred_insert_slot();
  
  return create_success_response(std::to_string(max_slot));
}
```

**Requirements:**
- Track maximum shred insert slot in ledger
- Update on each shred insertion
- Simple getter method

**Deliverables:**
- Implementation (+30 lines)
- Ledger tracking (+20 lines)
- Test cases (+20 lines)

---

## 3. Validator Methods Enhancement (63% → 100%)

### Current Status: 5/8 Methods Implemented ⚠️

**Implemented Methods:**
- ✅ `getSlot`
- ✅ `getSlotLeader`
- ✅ `getSlotLeaders`
- ✅ `getEpochInfo`
- ✅ `getLeaderSchedule`

**Missing Methods:**
- ❌ `getVoteAccounts` - Returns all vote accounts
- ❌ `getValidatorInfo` - Returns validator info  
- ❌ `getStakeActivation` - Returns stake activation info

### Implementation Roadmap (1-2 weeks)

#### Method 1: getVoteAccounts

**Objective:** Return information about all vote accounts.

**Implementation:**

```cpp
// In src/network/rpc_server.cpp

std::string handle_getVoteAccounts(const std::string& params) {
  // Parse optional vote pubkey filter
  auto options = parse_vote_accounts_params(params);
  
  // Get vote accounts from staking manager
  auto vote_accounts = staking_manager_->get_vote_accounts(
    options.vote_pubkey);
  
  // Classify as current or delinquent
  nlohmann::json current_accounts = nlohmann::json::array();
  nlohmann::json delinquent_accounts = nlohmann::json::array();
  
  for (const auto& account : vote_accounts) {
    nlohmann::json vote_info;
    vote_info["votePubkey"] = account.vote_pubkey;
    vote_info["nodePubkey"] = account.node_pubkey;
    vote_info["activatedStake"] = account.activated_stake;
    vote_info["epochVoteAccount"] = account.epoch_vote_account;
    vote_info["commission"] = account.commission;
    vote_info["lastVote"] = account.last_vote;
    vote_info["epochCredits"] = account.epoch_credits;
    
    if (account.is_delinquent) {
      delinquent_accounts.push_back(vote_info);
    } else {
      current_accounts.push_back(vote_info);
    }
  }
  
  nlohmann::json response;
  response["current"] = current_accounts;
  response["delinquent"] = delinquent_accounts;
  
  return create_success_response(response);
}
```

**Requirements:**
- Integration with staking manager
- Track delinquency status
- Return comprehensive vote account info
- Support filtering by vote pubkey

**Deliverables:**
- Implementation (+120 lines)
- Staking manager integration (+80 lines)
- Test cases (+60 lines)

---

#### Method 2: getValidatorInfo

**Objective:** Return validator identity and configuration info.

**Implementation:**

```cpp
// In src/network/rpc_server.cpp

std::string handle_getValidatorInfo(const std::string& params) {
  // Get validator identity
  auto identity = validator_core_->get_identity();
  
  nlohmann::json info;
  info["identityPubkey"] = identity.pubkey;
  info["gossip"] = identity.gossip_addr;
  info["tpu"] = identity.tpu_addr;
  info["rpc"] = identity.rpc_addr;
  info["version"] = identity.version;
  info["featureSet"] = identity.feature_set;
  
  return create_success_response(info);
}
```

**Requirements:**
- Return validator identity and network addresses
- Include version and feature set info
- Match Agave response format

**Deliverables:**
- Implementation (+50 lines)
- Test cases (+30 lines)

---

#### Method 3: getStakeActivation

**Objective:** Return stake activation state for an account.

**Implementation:**

```cpp
// In src/network/rpc_server.cpp

std::string handle_getStakeActivation(const std::string& params) {
  // Parse stake account pubkey and options
  auto [stake_pubkey, options] = parse_stake_activation_params(params);
  
  // Get stake activation from staking manager
  auto activation = staking_manager_->get_stake_activation(
    stake_pubkey, options.epoch);
  
  nlohmann::json response;
  response["state"] = activation.state; // "active", "inactive", "activating", "deactivating"
  response["active"] = activation.active_stake;
  response["inactive"] = activation.inactive_stake;
  
  return create_success_response(response);
}
```

**Requirements:**
- Track stake activation state
- Support epoch parameter
- Return active/inactive stake amounts

**Deliverables:**
- Implementation (+80 lines)
- Staking manager enhancement (+60 lines)
- Test cases (+50 lines)

---

## 4. Network Methods Enhancement (71% → 100%)

### Current Status: 5/7 Methods Implemented ⚠️

**Implemented Methods:**
- ✅ `getHealth`
- ✅ `getVersion`
- ✅ `getClusterNodes`
- ✅ `getSupply`
- ✅ `getInflationRate`

**Missing Methods:**
- ❌ `getRecentPerformanceSamples` - Returns recent performance metrics
- ❌ `getInflationGovernor` - Returns inflation parameters

### Implementation Roadmap (1 week)

#### Method 1: getRecentPerformanceSamples

**Objective:** Return recent performance samples (TPS, slot duration).

**Implementation:**

```cpp
// In src/network/rpc_server.cpp

std::string handle_getRecentPerformanceSamples(const std::string& params) {
  // Parse optional limit
  size_t limit = parse_limit_param(params, 720); // Default: 720 samples
  
  // Get samples from performance monitor
  auto samples = performance_monitor_->get_recent_samples(limit);
  
  nlohmann::json samples_array = nlohmann::json::array();
  for (const auto& sample : samples) {
    nlohmann::json sample_json;
    sample_json["slot"] = sample.slot;
    sample_json["numTransactions"] = sample.num_transactions;
    sample_json["numSlots"] = sample.num_slots;
    sample_json["samplePeriodSecs"] = sample.sample_period_secs;
    samples_array.push_back(sample_json);
  }
  
  return create_success_response(samples_array);
}
```

**Requirements:**
- Performance monitoring infrastructure
- Store recent samples (last 720 by default)
- Track TPS and slot metrics
- Periodic sampling

**Deliverables:**
- Implementation (+100 lines)
- Performance monitor (+150 lines)
- Test cases (+50 lines)

---

#### Method 2: getInflationGovernor

**Objective:** Return inflation parameters.

**Implementation:**

```cpp
// In src/network/rpc_server.cpp

std::string handle_getInflationGovernor(const std::string& params) {
  // Get inflation parameters from validator
  auto inflation = validator_core_->get_inflation_governor();
  
  nlohmann::json response;
  response["initial"] = inflation.initial_rate;
  response["terminal"] = inflation.terminal_rate;
  response["taper"] = inflation.taper;
  response["foundation"] = inflation.foundation_rate;
  response["foundationTerm"] = inflation.foundation_term;
  
  return create_success_response(response);
}
```

**Requirements:**
- Store inflation parameters
- Match Agave inflation model
- Support epoch-based queries

**Deliverables:**
- Implementation (+50 lines)
- Inflation parameters storage (+30 lines)
- Test cases (+30 lines)

---

## 📊 Implementation Timeline

### Phased Implementation (2-3 weeks)

| Week | Tasks | Deliverables |
|------|-------|--------------|
| **Week 1** | Block methods + Transaction methods (getMaxShredInsertSlot) | 3 methods complete |
| **Week 2** | simulateTransaction + sendBundle | 2 critical methods |
| **Week 3** | Validator methods + Network methods | 5 methods complete |

---

## 🧪 Testing Strategy

### Unit Tests
- [ ] Each RPC method individually
- [ ] Parameter parsing and validation
- [ ] Error handling
- [ ] Response format validation

### Integration Tests
- [ ] End-to-end RPC calls
- [ ] Cross-validation with Agave
- [ ] Performance testing
- [ ] Concurrent request handling

### Compatibility Tests
- [ ] Use Agave RPC clients
- [ ] Verify response formats match exactly
- [ ] Test all parameter combinations
- [ ] Edge case handling

---

## 📝 Code Structure

### Files to Modify

```
src/network/rpc_server.cpp           (+800 lines)
src/banking/banking_stage.cpp        (+150 lines)
src/staking/manager.cpp              (+140 lines)
src/monitoring/performance_monitor.cpp (+150 lines)
include/banking/banking_stage.h      (+30 lines)
include/staking/manager.h            (+40 lines)
```

### New Files to Create

```
include/monitoring/performance_monitor.h  (100+ lines)
src/monitoring/performance_monitor.cpp    (200+ lines)
tests/test_rpc_complete.cpp              (300+ lines)
```

**Total Modified Code:** ~1,270 lines  
**Total New Code:** ~600 lines  
**Test Code:** ~600 lines

---

## 🎯 Success Metrics

### Functional Metrics
- [ ] **All Methods**: 47/47 implemented (100%)
- [ ] **Response Format**: Exact match with Agave
- [ ] **Parameter Support**: All options supported

### Performance Metrics
- [ ] **Latency p95**: <10ms for all methods
- [ ] **Throughput**: >1,000 requests/sec
- [ ] **Concurrent Requests**: Handle 100+ simultaneous

### Quality Metrics
- [ ] **Test Coverage**: >95% for RPC code
- [ ] **Test Pass Rate**: 100% passing
- [ ] **Client Compatibility**: Works with all Agave clients

---

## ✅ Completion Checklist

### Block Methods
- [ ] getBlockTime implemented
- [ ] Enhanced getBlocks range query
- [ ] All tests passing

### Transaction Methods
- [ ] simulateTransaction working
- [ ] sendBundle functional
- [ ] getMaxShredInsertSlot implemented
- [ ] All tests passing

### Validator Methods
- [ ] getVoteAccounts complete
- [ ] getValidatorInfo working
- [ ] getStakeActivation functional
- [ ] All tests passing

### Network Methods
- [ ] getRecentPerformanceSamples working
- [ ] getInflationGovernor implemented
- [ ] All tests passing

---

## 🎉 Conclusion

This comprehensive plan provides a clear path to achieving **100% RPC API implementation**. The work focuses on:

1. **Block Methods** - 2 missing methods (1 week)
2. **Transaction Methods** - 3 missing methods, including critical `simulateTransaction` (2 weeks)
3. **Validator Methods** - 3 missing methods for validator info (1-2 weeks)
4. **Network Methods** - 2 missing methods for monitoring (1 week)

**Estimated Timeline:** 2-3 weeks for all methods

**Success Criteria:** All 47 RPC methods at 100%, full Agave compatibility

---

**Document Version:** 1.0  
**Last Updated:** November 10, 2025  
**Status:** Ready for Implementation
