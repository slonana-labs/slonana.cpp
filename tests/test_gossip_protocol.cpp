/**
 * Test program for the Agave-compatible gossip protocol implementation
 * 
 * This demonstrates basic usage of the gossip service and CRDS table.
 */

#include "network/gossip/gossip_service.h"
#include "network/gossip/crds.h"
#include "network/gossip/protocol.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace slonana::network::gossip;
using namespace slonana::common;

void test_crds_basic() {
    std::cout << "\n=== Testing CRDS Basic Operations ===\n";
    
    Crds crds;
    
    // Create a test public key
    PublicKey pk1(32, 1);
    PublicKey pk2(32, 2);
    
    // Create contact infos
    ContactInfo ci1(pk1);
    ci1.shred_version = 12345;
    ci1.wallclock = timestamp();
    
    ContactInfo ci2(pk2);
    ci2.shred_version = 12345;
    ci2.wallclock = timestamp();
    
    // Insert into CRDS
    CrdsValue val1(ci1);
    CrdsValue val2(ci2);
    
    auto result1 = crds.insert(val1, timestamp(), GossipRoute::LocalMessage);
    auto result2 = crds.insert(val2, timestamp(), GossipRoute::LocalMessage);
    
    std::cout << "Insert node 1: " << (result1.is_ok() ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Insert node 2: " << (result2.is_ok() ? "SUCCESS" : "FAILED") << "\n";
    
    // Query CRDS
    std::cout << "Total entries: " << crds.len() << "\n";
    std::cout << "Contact infos: " << crds.num_nodes() << "\n";
    
    auto contact_infos = crds.get_contact_infos();
    std::cout << "Retrieved " << contact_infos.size() << " contact infos\n";
    
    // Test update with newer wallclock
    ContactInfo ci1_updated(pk1);
    ci1_updated.shred_version = 12345;
    ci1_updated.wallclock = timestamp() + 1000;
    CrdsValue val1_updated(ci1_updated);
    
    auto update_result = crds.insert(val1_updated, timestamp(), GossipRoute::PushMessage);
    std::cout << "Update node 1: " << (update_result.is_ok() ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Total entries after update: " << crds.len() << "\n";
}

void test_protocol_messages() {
    std::cout << "\n=== Testing Protocol Messages ===\n";
    
    PublicKey my_pk(32, 99);
    
    // Test Ping/Pong
    PingMessage ping(my_pk);
    ping.generate_token();
    std::cout << "Generated ping with token size: " << ping.token.size() << "\n";
    
    Signature fake_sig(64, 0);
    ping.sign(fake_sig);
    std::cout << "Ping verification: " << (ping.verify() ? "PASS" : "FAIL") << "\n";
    
    PongMessage pong(my_pk, ping.token);
    pong.sign(fake_sig);
    std::cout << "Pong verification: " << (pong.verify() ? "PASS" : "FAIL") << "\n";
    
    // Test Protocol message creation
    ContactInfo ci(my_pk);
    CrdsValue val(ci);
    
    std::vector<CrdsValue> values;
    values.push_back(val);
    
    auto push_msg = Protocol::create_push_message(my_pk, values);
    std::cout << "Created push message: " << (push_msg.is_valid() ? "VALID" : "INVALID") << "\n";
    
    auto pull_resp = Protocol::create_pull_response(my_pk, values);
    std::cout << "Created pull response: " << (pull_resp.is_valid() ? "VALID" : "INVALID") << "\n";
    
    // Test prune data
    std::vector<PublicKey> prune_list;
    prune_list.push_back(PublicKey(32, 10));
    prune_list.push_back(PublicKey(32, 11));
    
    PruneData prune(my_pk, prune_list, PublicKey(32, 20), timestamp());
    prune.sign(fake_sig);
    std::cout << "Prune data verification: " << (prune.verify() ? "PASS" : "FAIL") << "\n";
    std::cout << "Prune list size: " << prune_list.size() << " peers\n";
}

void test_bloom_filter() {
    std::cout << "\n=== Testing Bloom Filter ===\n";
    
    CrdsFilter filter(100);
    
    // Add some hashes
    Hash hash1(32, 1);
    Hash hash2(32, 2);
    Hash hash3(32, 3);
    
    filter.add(hash1);
    filter.add(hash2);
    
    std::cout << "Contains hash1: " << (filter.contains(hash1) ? "YES" : "NO") << "\n";
    std::cout << "Contains hash2: " << (filter.contains(hash2) ? "YES" : "NO") << "\n";
    std::cout << "Contains hash3: " << (filter.contains(hash3) ? "NO (expected)" : "YES (false positive)") << "\n";
}

void test_gossip_service() {
    std::cout << "\n=== Testing Gossip Service ===\n";
    
    // Create configuration
    GossipService::Config config;
    config.bind_address = "127.0.0.1";
    config.bind_port = 18001;  // Use non-standard port for testing
    config.node_pubkey = PublicKey(32, 99);
    config.shred_version = 12345;
    config.gossip_push_fanout = 3;
    config.gossip_pull_fanout = 2;
    config.push_interval_ms = 500;
    config.pull_interval_ms = 1000;
    
    // Create service
    GossipService gossip(config);
    
    // Register callbacks
    int contact_info_count = 0;
    gossip.register_contact_info_callback([&contact_info_count](const ContactInfo& ci) {
        contact_info_count++;
        std::cout << "  -> New contact info received (total: " << contact_info_count << ")\n";
    });
    
    int vote_count = 0;
    gossip.register_vote_callback([&vote_count](const Vote& vote) {
        vote_count++;
        std::cout << "  -> New vote received (total: " << vote_count << ")\n";
    });
    
    // Start service
    auto result = gossip.start();
    if (!result.is_ok()) {
        std::cout << "Failed to start gossip service: " << result.error() << "\n";
        return;
    }
    
    std::cout << "Gossip service started successfully\n";
    std::cout << "Running for 3 seconds...\n";
    
    // Run for a bit
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // Get stats
    auto stats = gossip.get_stats();
    std::cout << "\nGossip Statistics:\n";
    std::cout << "  Nodes: " << stats.num_nodes << "\n";
    std::cout << "  Votes: " << stats.num_votes << "\n";
    std::cout << "  Total entries: " << stats.num_entries << "\n";
    std::cout << "  Push messages sent: " << stats.push_messages_sent << "\n";
    std::cout << "  Pull requests sent: " << stats.pull_requests_sent << "\n";
    std::cout << "  Messages received: " << stats.messages_received << "\n";
    
    // Insert a local value
    ContactInfo my_ci(config.node_pubkey);
    my_ci.shred_version = config.shred_version;
    CrdsValue my_val(my_ci);
    
    auto insert_result = gossip.insert_local_value(my_val);
    std::cout << "\nInsert local value: " << (insert_result.is_ok() ? "SUCCESS" : "FAILED") << "\n";
    
    // Stop service
    gossip.stop();
    std::cout << "Gossip service stopped\n";
}

int main() {
    std::cout << "=================================================\n";
    std::cout << "  Agave-Compatible Gossip Protocol Test Suite\n";
    std::cout << "=================================================\n";
    
    try {
        test_crds_basic();
        test_protocol_messages();
        test_bloom_filter();
        test_gossip_service();
        
        std::cout << "\n=================================================\n";
        std::cout << "  All tests completed successfully!\n";
        std::cout << "=================================================\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nTest failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
