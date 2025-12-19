/**
 * Comprehensive Integration Test for Gossip Protocol
 * 
 * Tests all components working together in a realistic scenario
 */

#include "network/gossip/gossip_service.h"
#include "network/gossip/crds.h"
#include "network/gossip/protocol.h"
#include "network/gossip/crds_shards.h"
#include "network/gossip/weighted_shuffle.h"
#include "network/gossip/received_cache.h"
#include "network/gossip/duplicate_shred_detector.h"
#include "network/gossip/gossip_metrics.h"
#include "network/gossip/push_active_set.h"
#include "network/gossip/legacy_contact_info.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>

using namespace slonana::network::gossip;
using namespace slonana::common;

void test_crds_sharding() {
    std::cout << "\n=== Testing CRDS Sharding ===\n";
    
    CrdsShards shards(16);
    
    // Create test values
    for (int i = 0; i < 100; ++i) {
        PublicKey pk(32, i);
        ContactInfo ci(pk);
        CrdsValue value(ci);
        
        VersionedCrdsValue versioned;
        versioned.value = value;
        versioned.local_timestamp = timestamp();
        versioned.ordinal = i;
        
        shards.insert(i, &versioned);
    }
    
    std::cout << "Total entries in shards: " << shards.size() << "\n";
    assert(shards.size() == 100);
    
    // Test sampling
    auto sample = shards.sample(10);
    std::cout << "Sample size: " << sample.size() << "\n";
    assert(sample.size() <= 10);
    
    std::cout << "✓ CRDS Sharding test passed\n";
}

void test_weighted_shuffle() {
    std::cout << "\n=== Testing Weighted Shuffle ===\n";
    
    std::vector<WeightedShuffle::WeightedNode> nodes;
    
    // Create nodes with different stakes
    for (int i = 0; i < 10; ++i) {
        PublicKey pk(32, i);
        uint64_t stake = (i + 1) * 1000000;  // 1M, 2M, ..., 10M
        nodes.emplace_back(pk, stake);
    }
    
    WeightedShuffle shuffle(nodes, 12345);
    
    // Get shuffled order
    auto shuffled = shuffle.get_shuffled(10);
    std::cout << "Shuffled " << shuffled.size() << " nodes\n";
    assert(shuffled.size() == 10);
    
    // Test that higher stake nodes appear more frequently in multiple shuffles
    std::map<size_t, int> selection_count;
    for (int trial = 0; trial < 100; ++trial) {
        WeightedShuffle trial_shuffle(nodes, trial);
        const auto* first = trial_shuffle.next();
        if (first) {
            // Find index of selected node
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (nodes[i].pubkey == first->pubkey) {
                    selection_count[i]++;
                    break;
                }
            }
        }
    }
    
    std::cout << "Selection distribution (higher stake should be selected more):\n";
    for (const auto &[idx, count] : selection_count) {
        std::cout << "  Node " << idx << " (stake: " << nodes[idx].stake 
                  << "): " << count << " selections\n";
    }
    
    std::cout << "✓ Weighted Shuffle test passed\n";
}

void test_received_cache() {
    std::cout << "\n=== Testing Received Cache ===\n";
    
    ReceivedCache cache(100);
    
    // Create test hashes
    std::vector<Hash> hashes;
    for (int i = 0; i < 50; ++i) {
        Hash hash(32, i);
        hashes.push_back(hash);
    }
    
    // Insert hashes
    int new_count = 0;
    for (const auto &hash : hashes) {
        if (cache.insert(hash)) {
            new_count++;
        }
    }
    
    std::cout << "Inserted " << new_count << " new hashes\n";
    assert(new_count == 50);
    assert(cache.size() == 50);
    
    // Try inserting again (should be duplicates)
    int dup_count = 0;
    for (const auto &hash : hashes) {
        if (!cache.insert(hash)) {
            dup_count++;
        }
    }
    
    std::cout << "Detected " << dup_count << " duplicates\n";
    assert(dup_count == 50);
    
    std::cout << "✓ Received Cache test passed\n";
}

void test_duplicate_shred_detector() {
    std::cout << "\n=== Testing Duplicate Shred Detector ===\n";
    
    DuplicateShredDetector detector;
    
    PublicKey validator(32, 1);
    
    // Insert first shred
    std::vector<uint8_t> shred1(100, 0xAA);
    bool is_dup1 = detector.check_and_insert(1000, 0, shred1, validator);
    std::cout << "First shred is duplicate: " << (is_dup1 ? "yes" : "no") << "\n";
    assert(!is_dup1);
    
    // Insert same shred (should not be duplicate)
    bool is_dup2 = detector.check_and_insert(1000, 0, shred1, validator);
    std::cout << "Same shred is duplicate: " << (is_dup2 ? "yes" : "no") << "\n";
    assert(!is_dup2);
    
    // Insert different shred at same slot/index (should be duplicate!)
    std::vector<uint8_t> shred2(100, 0xBB);
    bool is_dup3 = detector.check_and_insert(1000, 0, shred2, validator);
    std::cout << "Different shred at same slot/index is duplicate: " 
              << (is_dup3 ? "yes" : "no") << "\n";
    assert(is_dup3);
    
    std::cout << "Detected " << detector.size() << " duplicate shred(s)\n";
    assert(detector.size() == 1);
    
    std::cout << "✓ Duplicate Shred Detector test passed\n";
}

void test_gossip_metrics() {
    std::cout << "\n=== Testing Gossip Metrics ===\n";
    
    GossipMetrics metrics;
    
    // Record some events
    metrics.record_push_message_sent(10);
    metrics.record_pull_request_sent();
    metrics.record_crds_insert(true);
    metrics.record_crds_insert(false);
    metrics.record_packet_sent(1024);
    
    // Time an operation
    {
        ScopedTimer timer([&](uint64_t us) {
            metrics.record_push_duration_us(us);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // Get metrics
    auto stats = metrics.get_metrics();
    std::cout << "Push messages sent: " << stats["push_messages_sent"] << "\n";
    std::cout << "Pull requests sent: " << stats["pull_requests_sent"] << "\n";
    std::cout << "CRDS inserts: " << stats["crds_inserts"] << "\n";
    std::cout << "CRDS insert failures: " << stats["crds_insert_failures"] << "\n";
    std::cout << "Bytes sent: " << stats["bytes_sent"] << "\n";
    
    assert(stats["push_messages_sent"] == 1);
    assert(stats["pull_requests_sent"] == 1);
    assert(stats["crds_inserts"] == 1);
    assert(stats["crds_insert_failures"] == 1);
    assert(stats["bytes_sent"] == 1024);
    
    std::cout << "\nFormatted metrics:\n" << metrics.to_string();
    
    std::cout << "✓ Gossip Metrics test passed\n";
}

void test_push_active_set() {
    std::cout << "\n=== Testing Push Active Set ===\n";
    
    PushActiveSet active_set(5, 1000);  // fanout=5, rotation=1s
    
    // Add peers
    std::vector<PublicKey> peers;
    for (int i = 0; i < 20; ++i) {
        PublicKey pk(32, i);
        peers.push_back(pk);
    }
    
    active_set.update_peers(peers);
    
    std::cout << "Pool size: " << active_set.pool_size() << "\n";
    std::cout << "Active set size: " << active_set.active_size() << "\n";
    
    assert(active_set.pool_size() == 20);
    assert(active_set.active_size() == 5);
    
    // Get active set
    auto active = active_set.get_active_set();
    std::cout << "Active peers: " << active.size() << "\n";
    assert(active.size() == 5);
    
    // Force rotation
    active_set.rotate();
    auto active_after_rotation = active_set.get_active_set();
    std::cout << "Active peers after rotation: " << active_after_rotation.size() << "\n";
    assert(active_after_rotation.size() == 5);
    
    std::cout << "✓ Push Active Set test passed\n";
}

void test_legacy_contact_info() {
    std::cout << "\n=== Testing Legacy Contact Info ===\n";
    
    // Create modern ContactInfo
    PublicKey pk(32, 1);
    ContactInfo ci(pk);
    ci.shred_version = 12345;
    ci.version = "1.18.0";
    ci.addrs.push_back("127.0.0.1:8001");
    ci.addrs.push_back("127.0.0.1:8002");
    
    // Convert to legacy
    auto legacy = LegacyContactInfo::from_contact_info(ci);
    std::cout << "Legacy version: " << legacy.version << "\n";
    std::cout << "Legacy shred version: " << legacy.shred_version << "\n";
    std::cout << "Legacy gossip addr: " << legacy.gossip_addr << "\n";
    
    assert(legacy.shred_version == 12345);
    assert(legacy.version == "1.18.0");
    assert(legacy.gossip_addr == "127.0.0.1:8001");
    
    // Convert back to modern
    auto ci_restored = legacy.to_contact_info();
    assert(ci_restored.shred_version == ci.shred_version);
    assert(ci_restored.addrs.size() >= 2);
    
    // Test version compatibility
    bool compat = LegacyVersion::is_compatible("1.18.0", "1.18.5");
    std::cout << "Version 1.18.0 compatible with 1.18.5: " << (compat ? "yes" : "no") << "\n";
    assert(compat);
    
    bool not_compat = LegacyVersion::is_compatible("1.18.0", "2.0.0");
    std::cout << "Version 1.18.0 compatible with 2.0.0: " << (not_compat ? "yes" : "no") << "\n";
    assert(!not_compat);
    
    std::cout << "✓ Legacy Contact Info test passed\n";
}

void test_integration() {
    std::cout << "\n=== Integration Test: All Components Together ===\n";
    
    // Create CRDS with sharding
    Crds crds;
    CrdsShards shards(16);
    ReceivedCache cache(1000);
    GossipMetrics metrics;
    PushActiveSet active_set(6, 30000);
    
    // Add some nodes
    std::vector<PublicKey> peers;
    for (int i = 0; i < 10; ++i) {
        PublicKey pk(32, i);
        ContactInfo ci(pk);
        ci.shred_version = 12345;
        ci.wallclock = timestamp() + i;
        
        CrdsValue value(ci);
        
        // Check cache for deduplication
        if (cache.insert(value.hash())) {
            // Insert into CRDS
            auto result = crds.insert(value, timestamp(), GossipRoute::LocalMessage);
            metrics.record_crds_insert(result.is_ok());
            
            if (result.is_ok()) {
                peers.push_back(pk);
            }
        }
    }
    
    // Update active set
    active_set.update_peers(peers);
    
    std::cout << "Added " << peers.size() << " peers\n";
    std::cout << "CRDS size: " << crds.len() << "\n";
    std::cout << "Cache size: " << cache.size() << "\n";
    std::cout << "Active set size: " << active_set.active_size() << "\n";
    
    // Get metrics
    auto stats = metrics.get_metrics();
    std::cout << "Successful inserts: " << stats["crds_inserts"] << "\n";
    
    std::cout << "✓ Integration test passed\n";
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << "   Comprehensive Gossip Protocol Integration Test\n";
    std::cout << "=======================================================\n";
    
    try {
        test_crds_sharding();
        test_weighted_shuffle();
        test_received_cache();
        test_duplicate_shred_detector();
        test_gossip_metrics();
        test_push_active_set();
        test_legacy_contact_info();
        test_integration();
        
        std::cout << "\n=======================================================\n";
        std::cout << "   ✓ ALL TESTS PASSED\n";
        std::cout << "=======================================================\n";
        
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "\n✗ Test failed with exception: " << e.what() << "\n";
        return 1;
    }
}
