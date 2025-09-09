#include "network/shred_distribution.h"
#include "network/turbine.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <vector>

using namespace slonana::network;

bool test_turbine_node() {
  std::cout << "Testing TurbineNode functionality..." << std::endl;

  std::vector<uint8_t> pubkey(32, 0x01);
  TurbineNode node1(pubkey, "127.0.0.1", 8080, 1000);

  assert(node1.pubkey.size() == 32);
  assert(node1.address == "127.0.0.1");
  assert(node1.port == 8080);
  assert(node1.stake_weight == 1000);
  assert(node1.to_string() == "127.0.0.1:8080");

  // Test equality
  TurbineNode node2(pubkey, "127.0.0.1", 8080, 1000);
  assert(node1 == node2);

  // Test inequality
  TurbineNode node3(pubkey, "127.0.0.1", 8081, 1000);
  assert(!(node1 == node3));

  std::cout << "✅ TurbineNode functionality test passed" << std::endl;
  return true;
}

bool test_turbine_tree_construction() {
  std::cout << "Testing TurbineTree construction..." << std::endl;

  // Create self node
  std::vector<uint8_t> self_pubkey(32, 0x01);
  TurbineNode self_node(self_pubkey, "127.0.0.1", 8000, 2000);

  TurbineTree tree(self_node, 4); // Fanout of 4

  // Create validator nodes
  std::vector<TurbineNode> validators;
  for (int i = 0; i < 10; ++i) {
    std::vector<uint8_t> pubkey(32, static_cast<uint8_t>(i + 2));
    validators.emplace_back(pubkey, "127.0.0." + std::to_string(i + 2),
                            8000 + i, 1000 + i * 100);
  }

  tree.construct_tree(validators);

  auto stats = tree.get_stats();
  assert(stats.total_nodes >= 10); // At least the validators
  assert(stats.fanout == 4);
  assert(stats.tree_height > 0);

  // Test tree validation
  assert(tree.validate_tree() == true);

  // Test getting all nodes
  auto all_nodes = tree.get_all_nodes();
  assert(!all_nodes.empty());

  // Test root node
  auto root = tree.get_root();
  assert(!root.pubkey.empty());

  std::cout << "✅ TurbineTree construction test passed" << std::endl;
  return true;
}

bool test_turbine_tree_relationships() {
  std::cout << "Testing TurbineTree relationships..." << std::endl;

  std::vector<uint8_t> self_pubkey(32, 0x01);
  TurbineNode self_node(self_pubkey, "127.0.0.1", 8000, 2000);

  TurbineTree tree(self_node, 2); // Fanout of 2

  // Create a small tree
  std::vector<TurbineNode> validators;
  for (int i = 0; i < 5; ++i) {
    std::vector<uint8_t> pubkey(32, static_cast<uint8_t>(i + 2));
    validators.emplace_back(pubkey, "127.0.0." + std::to_string(i + 2),
                            8000 + i, 1000);
  }

  tree.construct_tree(validators);

  auto all_nodes = tree.get_all_nodes();
  assert(all_nodes.size() >= 5);

  // Test children relationships
  auto root = tree.get_root();
  auto children = tree.get_children(root);
  assert(!children.empty());

  // Test parent relationships
  if (!children.empty()) {
    auto parent = tree.get_parent(children[0]);
    assert(parent.has_value());
    assert(parent.value() == root);
  }

  // Test retransmit peers
  auto retransmit_peers = tree.get_retransmit_peers(root);
  // Should have some peers, but number depends on algorithm

  std::cout << "✅ TurbineTree relationships test passed" << std::endl;
  return true;
}

bool test_shred_creation() {
  std::cout << "Testing Shred creation and serialization..." << std::endl;

  // Test data shred creation
  std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  auto data_shred = Shred::create_data_shred(100, 5, data);

  assert(data_shred.slot() == 100);
  assert(data_shred.index() == 5);
  assert(data_shred.get_type() == ShredType::DATA);
  assert(data_shred.payload() == data);
  assert(data_shred.is_valid() == true);

  // Test coding shred creation
  std::vector<uint8_t> coding_data = {0xAA, 0xBB, 0xCC, 0xDD};
  auto coding_shred = Shred::create_coding_shred(100, 10, 2, coding_data);

  assert(coding_shred.slot() == 100);
  assert(coding_shred.index() == 10);
  assert(coding_shred.fec_set_index() == 2);
  assert(coding_shred.get_type() == ShredType::CODING);
  assert(coding_shred.payload() == coding_data);

  // Test serialization
  auto serialized = data_shred.serialize();
  assert(!serialized.empty());
  assert(serialized.size() >= Shred::header_size());

  // Test deserialization
  auto deserialized = Shred::deserialize(serialized);
  assert(deserialized.has_value());
  assert(deserialized->slot() == data_shred.slot());
  assert(deserialized->index() == data_shred.index());
  assert(deserialized->payload() == data_shred.payload());

  std::cout << "✅ Shred creation and serialization test passed" << std::endl;
  return true;
}

bool test_turbine_broadcast() {
  std::cout << "Testing TurbineBroadcast functionality..." << std::endl;

  std::vector<uint8_t> self_pubkey(32, 0x01);
  TurbineNode self_node(self_pubkey, "127.0.0.1", 8000, 2000);

  TurbineBroadcast broadcast(self_node);

  // Create a tree
  auto tree = std::make_unique<TurbineTree>(self_node, 3);
  std::vector<TurbineNode> validators;
  for (int i = 0; i < 8; ++i) {
    std::vector<uint8_t> pubkey(32, static_cast<uint8_t>(i + 2));
    validators.emplace_back(pubkey, "127.0.0." + std::to_string(i + 2),
                            8000 + i, 1000);
  }
  tree->construct_tree(validators);

  broadcast.initialize(std::move(tree));

  // Test callbacks
  bool send_called = false;
  bool receive_called = false;

  broadcast.set_send_callback(
      [&](const Shred &shred, const std::vector<TurbineNode> &targets) {
        send_called = true;
        assert(!targets.empty());
      });

  broadcast.set_receive_callback(
      [&](const Shred &shred, const std::string &from) {
        receive_called = true;
        assert(!from.empty());
      });

  // Test broadcasting
  std::vector<uint8_t> data = {1, 2, 3, 4, 5};
  auto shred = Shred::create_data_shred(200, 1, data);
  std::vector<Shred> shreds = {shred};

  broadcast.broadcast_shreds(shreds);
  assert(send_called == true);

  // Test receiving
  broadcast.handle_received_shred(shred, "peer1");
  // receive_called might be true depending on tree structure

  // Test statistics
  auto stats = broadcast.get_stats();
  assert(stats.shreds_sent > 0);

  std::cout << "✅ TurbineBroadcast functionality test passed" << std::endl;
  return true;
}

bool test_shred_utilities() {
  std::cout << "Testing shred utility functions..." << std::endl;

  // Test data splitting
  std::vector<uint8_t> large_data(5000, 0xAB); // Large data
  auto shreds = shred_utils::split_data_into_shreds(large_data, 300, 0);

  assert(!shreds.empty());
  assert(shreds.size() > 1); // Should be split into multiple shreds

  // Verify all shreds have same slot
  for (const auto &shred : shreds) {
    assert(shred.slot() == 300);
    assert(shred.get_type() == ShredType::DATA);
  }

  // Test data reconstruction
  auto reconstructed = shred_utils::reconstruct_data_from_shreds(shreds);
  assert(reconstructed == large_data);

  // Test shred validation
  assert(ShredValidator::validate_shred(shreds[0]) == true);
  assert(ShredValidator::validate_slot_progression(shreds[0], 299) == true);
  assert(ShredValidator::validate_slot_progression(shreds[0], 301) == false);

  // Test hash calculation
  auto hash1 = shred_utils::calculate_shred_hash(shreds[0]);
  auto hash2 = shred_utils::calculate_shred_hash(shreds[0]);
  assert(hash1 == hash2); // Same shred should produce same hash

  if (shreds.size() > 1) {
    auto hash3 = shred_utils::calculate_shred_hash(shreds[1]);
    assert(hash1 != hash3); // Different shreds should have different hashes
  }

  std::cout << "✅ Shred utility functions test passed" << std::endl;
  return true;
}

bool test_turbine_utils() {
  std::cout << "Testing turbine utility functions..." << std::endl;

  // Test optimal fanout calculation
  assert(turbine_utils::calculate_optimal_fanout(5) == 2);
  assert(turbine_utils::calculate_optimal_fanout(50) == 4);
  assert(turbine_utils::calculate_optimal_fanout(500) == 8);
  assert(turbine_utils::calculate_optimal_fanout(5000) == 16);

  // Test tree height estimation
  assert(turbine_utils::estimate_tree_height(1, 2) == 1);
  assert(turbine_utils::estimate_tree_height(8, 2) >= 3);
  assert(turbine_utils::estimate_tree_height(16, 4) >= 2);

  // Test node sorting
  std::vector<TurbineNode> nodes;
  for (int i = 0; i < 5; ++i) {
    std::vector<uint8_t> pubkey(32, static_cast<uint8_t>(i));
    nodes.emplace_back(pubkey, "127.0.0.1", 8000 + i, 1000 + i * 100);
  }

  auto sorted = turbine_utils::sort_by_stake(nodes);
  assert(sorted.size() == nodes.size());

  // Should be sorted by stake (highest first)
  for (size_t i = 1; i < sorted.size(); ++i) {
    assert(sorted[i - 1].stake_weight >= sorted[i].stake_weight);
  }

  // Test node validation
  std::vector<uint8_t> valid_pubkey(32, 0x01);
  TurbineNode valid_node(valid_pubkey, "127.0.0.1", 8080, 1000);
  assert(turbine_utils::validate_node(valid_node) == true);

  TurbineNode invalid_node(valid_pubkey, "", 8080, 1000); // Empty address
  assert(turbine_utils::validate_node(invalid_node) == false);

  std::cout << "✅ Turbine utility functions test passed" << std::endl;
  return true;
}

bool test_tree_serialization() {
  std::cout << "Testing TurbineTree serialization..." << std::endl;

  std::vector<uint8_t> self_pubkey(32, 0x01);
  TurbineNode self_node(self_pubkey, "127.0.0.1", 8000, 2000);

  TurbineTree tree(self_node, 3);

  // Create some nodes
  std::vector<TurbineNode> validators;
  for (int i = 0; i < 5; ++i) {
    std::vector<uint8_t> pubkey(32, static_cast<uint8_t>(i + 2));
    validators.emplace_back(pubkey, "127.0.0." + std::to_string(i + 2),
                            8000 + i, 1000 + i * 100);
  }

  tree.construct_tree(validators);

  // Test serialization
  auto serialized = tree.serialize();
  assert(!serialized.empty());

  // Test deserialization
  TurbineTree new_tree(self_node, 1); // Different initial fanout
  assert(new_tree.deserialize(serialized) == true);

  // Verify deserialized tree
  assert(new_tree.get_fanout() == tree.get_fanout());

  auto original_nodes = tree.get_all_nodes();
  auto deserialized_nodes = new_tree.get_all_nodes();
  assert(original_nodes.size() == deserialized_nodes.size());

  std::cout << "✅ TurbineTree serialization test passed" << std::endl;
  return true;
}

int main() {
  std::cout << "=== Turbine Protocol Test Suite ===" << std::endl;

  try {
    assert(test_turbine_node());
    assert(test_turbine_tree_construction());
    assert(test_turbine_tree_relationships());
    assert(test_shred_creation());
    assert(test_turbine_broadcast());
    assert(test_shred_utilities());
    assert(test_turbine_utils());
    assert(test_tree_serialization());

    std::cout << "\n🎉 All Turbine Protocol tests passed!" << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "❌ Test failed with unknown exception" << std::endl;
    return 1;
  }
}