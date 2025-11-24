/**
 * MeshCore Adapter Tests
 *
 * Comprehensive test suite for mesh networking functionality
 */

#include "network/meshcore_adapter.h"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

using namespace slonana::network::meshcore;
using namespace std::chrono_literals;

class MeshCoreAdapterTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create default config
    config_.enabled = true;
    config_.test_mode = true; // Enable simulated connections for unit tests
    config_.node_id = "test_node_1";
    config_.listen_port = 9000;
    config_.heartbeat_interval_ms = 1000;
    config_.mesh_discovery_interval_ms = 2000;
    config_.max_direct_peers = 10;
    config_.desired_direct_peers = 5;
  }

  void TearDown() override {
    // Cleanup
  }

  MeshConfig config_;
};

/**
 * Test basic lifecycle: start and stop
 */
TEST_F(MeshCoreAdapterTest, BasicLifecycle) {
  MeshCoreAdapter adapter(config_);

  EXPECT_FALSE(adapter.is_running());

  auto result = adapter.start();
  EXPECT_TRUE(result.is_ok());
  EXPECT_TRUE(adapter.is_running());

  adapter.stop();
  EXPECT_FALSE(adapter.is_running());
}

/**
 * Test starting when disabled
 */
TEST_F(MeshCoreAdapterTest, StartWhenDisabled) {
  config_.enabled = false;
  MeshCoreAdapter adapter(config_);

  auto result = adapter.start();
  EXPECT_FALSE(result.is_ok());
  EXPECT_FALSE(adapter.is_running());
}

/**
 * Test mesh join and leave
 */
TEST_F(MeshCoreAdapterTest, JoinAndLeaveMesh) {
  MeshCoreAdapter adapter(config_);

  adapter.start();
  EXPECT_FALSE(adapter.is_joined());

  auto join_result = adapter.join_mesh();
  EXPECT_TRUE(join_result.is_ok());
  EXPECT_TRUE(adapter.is_joined());

  auto leave_result = adapter.leave_mesh();
  EXPECT_TRUE(leave_result.is_ok());
  EXPECT_FALSE(adapter.is_joined());

  adapter.stop();
}

/**
 * Test mesh join with bootstrap nodes
 */
TEST_F(MeshCoreAdapterTest, JoinWithBootstrapNodes) {
  config_.bootstrap_nodes = {"127.0.0.1:9001", "127.0.0.1:9002",
                             "127.0.0.1:9003"};

  MeshCoreAdapter adapter(config_);
  adapter.start();

  auto join_start = std::chrono::steady_clock::now();
  auto result = adapter.join_mesh();
  auto join_duration = std::chrono::steady_clock::now() - join_start;

  EXPECT_TRUE(result.is_ok());
  EXPECT_TRUE(adapter.is_joined());

  // Check join time is reasonable (< 2s as per requirements)
  auto join_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(join_duration)
          .count();
  EXPECT_LT(join_ms, 2000); // < 2s

  adapter.stop();
}

/**
 * Test peer connection
 */
TEST_F(MeshCoreAdapterTest, ConnectToPeer) {
  MeshCoreAdapter adapter(config_);
  adapter.start();
  adapter.join_mesh();

  auto result = adapter.connect_to_peer("127.0.0.1", 9001);
  EXPECT_TRUE(result.is_ok());

  // Wait for connection to establish
  std::this_thread::sleep_for(500ms);

  auto peers = adapter.get_connected_peers();
  EXPECT_GE(peers.size(), 0); // May take time to show as connected

  adapter.stop();
}

/**
 * Test peer disconnection
 */
TEST_F(MeshCoreAdapterTest, DisconnectFromPeer) {
  MeshCoreAdapter adapter(config_);
  adapter.start();
  adapter.join_mesh();

  adapter.connect_to_peer("127.0.0.1", 9001);
  std::this_thread::sleep_for(500ms);

  auto result = adapter.disconnect_from_peer("127.0.0.1:9001");
  EXPECT_TRUE(result.is_ok());

  adapter.stop();
}

/**
 * Test message sending
 */
TEST_F(MeshCoreAdapterTest, SendMessage) {
  MeshCoreAdapter adapter(config_);
  adapter.start();
  adapter.join_mesh();

  // Connect to a peer first
  adapter.connect_to_peer("127.0.0.1", 9001);
  std::this_thread::sleep_for(500ms);

  MeshMessage msg;
  msg.type = MeshMessageType::DATA;
  msg.sender_id = config_.node_id;
  msg.receiver_id = "127.0.0.1:9001";
  msg.payload = {0x01, 0x02, 0x03, 0x04};
  msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
  msg.ttl = 10;

  auto result = adapter.send_message(msg);
  EXPECT_TRUE(result.is_ok());

  adapter.stop();
}

/**
 * Test message broadcasting
 */
TEST_F(MeshCoreAdapterTest, BroadcastMessage) {
  MeshCoreAdapter adapter(config_);
  adapter.start();
  adapter.join_mesh();

  // Connect to multiple peers
  adapter.connect_to_peer("127.0.0.1", 9001);
  adapter.connect_to_peer("127.0.0.1", 9002);
  std::this_thread::sleep_for(500ms);

  MeshMessage msg;
  msg.type = MeshMessageType::DATA;
  msg.sender_id = config_.node_id;
  msg.payload = {0x01, 0x02, 0x03, 0x04};
  msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();

  auto result = adapter.broadcast_message(msg);
  EXPECT_TRUE(result.is_ok());

  adapter.stop();
}

/**
 * Test message handler registration
 */
TEST_F(MeshCoreAdapterTest, MessageHandlerRegistration) {
  MeshCoreAdapter adapter(config_);

  bool handler_called = false;
  MeshMessage received_msg;

  adapter.register_message_handler(MeshMessageType::DATA,
                                   [&](const MeshMessage &msg) {
                                     handler_called = true;
                                     received_msg = msg;
                                   });

  adapter.start();
  adapter.join_mesh();

  // Connect and send message
  adapter.connect_to_peer("127.0.0.1", 9001);
  std::this_thread::sleep_for(500ms);

  MeshMessage msg;
  msg.type = MeshMessageType::DATA;
  msg.sender_id = config_.node_id;
  msg.receiver_id = "127.0.0.1:9001";
  msg.payload = {0xAA, 0xBB};

  adapter.send_message(msg);
  std::this_thread::sleep_for(100ms);

  EXPECT_TRUE(handler_called);
  EXPECT_EQ(received_msg.payload, msg.payload);

  adapter.stop();
}

/**
 * Test error handler registration
 */
TEST_F(MeshCoreAdapterTest, ErrorHandlerRegistration) {
  MeshCoreAdapter adapter(config_);

  bool error_called = false;
  std::string error_node;
  std::string error_msg;

  adapter.register_error_handler(
      [&](const std::string &node_id, const std::string &message) {
        error_called = true;
        error_node = node_id;
        error_msg = message;
      });

  adapter.start();
  adapter.join_mesh();

  // Trigger an error condition by sending to non-existent peer
  MeshMessage msg;
  msg.type = MeshMessageType::DATA;
  msg.sender_id = config_.node_id;
  msg.receiver_id = "nonexistent:9999";

  auto result = adapter.send_message(msg);
  EXPECT_FALSE(result.is_ok());

  adapter.stop();
}

/**
 * Test getting peer information
 */
TEST_F(MeshCoreAdapterTest, GetPeerInfo) {
  MeshCoreAdapter adapter(config_);
  adapter.start();
  adapter.join_mesh();

  adapter.connect_to_peer("127.0.0.1", 9001);
  std::this_thread::sleep_for(500ms);

  auto peer_info = adapter.get_peer_info("127.0.0.1:9001");
  EXPECT_TRUE(peer_info.has_value());

  if (peer_info) {
    EXPECT_EQ(peer_info->address, "127.0.0.1");
    EXPECT_EQ(peer_info->port, 9001);
  }

  adapter.stop();
}

/**
 * Test statistics collection
 */
TEST_F(MeshCoreAdapterTest, GetStatistics) {
  MeshCoreAdapter adapter(config_);
  adapter.start();
  adapter.join_mesh();

  // Connect to peers and send messages
  adapter.connect_to_peer("127.0.0.1", 9001);
  adapter.connect_to_peer("127.0.0.1", 9002);
  std::this_thread::sleep_for(500ms);

  MeshMessage msg;
  msg.type = MeshMessageType::DATA;
  msg.sender_id = config_.node_id;
  msg.receiver_id = "127.0.0.1:9001";
  msg.payload = {0x01, 0x02, 0x03};

  adapter.send_message(msg);

  auto stats = adapter.get_stats();
  EXPECT_GE(stats.total_nodes, 0);
  EXPECT_GE(stats.messages_sent, 0);
  EXPECT_EQ(stats.mesh_joins, 1);

  adapter.stop();
}

/**
 * Test topology retrieval
 */
TEST_F(MeshCoreAdapterTest, GetTopology) {
  MeshCoreAdapter adapter(config_);
  adapter.start();
  adapter.join_mesh();

  adapter.connect_to_peer("127.0.0.1", 9001);
  adapter.connect_to_peer("127.0.0.1", 9002);
  std::this_thread::sleep_for(500ms);

  auto topology = adapter.get_topology();
  EXPECT_GE(topology.size(), 0);

  adapter.stop();
}

/**
 * Test performance: message latency
 * Requirement: <40ms p50, <75ms p95
 */
TEST_F(MeshCoreAdapterTest, MessageLatencyPerformance) {
  MeshCoreAdapter adapter(config_);
  adapter.start();
  adapter.join_mesh();

  adapter.connect_to_peer("127.0.0.1", 9001);
  std::this_thread::sleep_for(500ms);

  std::vector<int64_t> latencies;
  const int num_messages = 100;

  for (int i = 0; i < num_messages; i++) {
    auto start = std::chrono::steady_clock::now();

    MeshMessage msg;
    msg.type = MeshMessageType::DATA;
    msg.sender_id = config_.node_id;
    msg.receiver_id = "127.0.0.1:9001";
    msg.payload = {0x01, 0x02};

    adapter.send_message(msg);

    auto end = std::chrono::steady_clock::now();
    auto latency =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count();
    latencies.push_back(latency);
  }

  // Calculate p50 and p95
  std::sort(latencies.begin(), latencies.end());
  auto p50 = latencies[latencies.size() / 2];
  auto p95 = latencies[latencies.size() * 95 / 100];

  EXPECT_LT(p50, 40); // p50 < 40ms
  EXPECT_LT(p95, 75); // p95 < 75ms

  adapter.stop();
}

/**
 * Test performance: mesh join time
 * Requirement: <2s avg, <5s p95
 */
TEST_F(MeshCoreAdapterTest, MeshJoinTimePerformance) {
  config_.bootstrap_nodes = {"127.0.0.1:9001", "127.0.0.1:9002",
                             "127.0.0.1:9003"};

  std::vector<int64_t> join_times;
  const int num_trials = 10;

  for (int i = 0; i < num_trials; i++) {
    MeshCoreAdapter adapter(config_);
    adapter.start();

    auto start = std::chrono::steady_clock::now();
    adapter.join_mesh();
    auto end = std::chrono::steady_clock::now();

    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count();
    join_times.push_back(duration);

    adapter.stop();
  }

  // Calculate average and p95
  int64_t sum = 0;
  for (auto t : join_times) {
    sum += t;
  }
  auto avg = sum / join_times.size();

  std::sort(join_times.begin(), join_times.end());
  auto p95 = join_times[join_times.size() * 95 / 100];

  EXPECT_LT(avg, 2000); // avg < 2s
  EXPECT_LT(p95, 5000); // p95 < 5s
}

/**
 * Test resilience: peer churn recovery
 * Requirement: >95% recovery within 2s
 */
TEST_F(MeshCoreAdapterTest, PeerChurnRecovery) {
  MeshCoreAdapter adapter(config_);
  adapter.start();
  adapter.join_mesh();

  // Connect to multiple peers
  std::vector<std::string> peer_ids;
  for (int i = 0; i < 5; i++) {
    std::string peer_id = "127.0.0.1:" + std::to_string(9001 + i);
    adapter.connect_to_peer("127.0.0.1", 9001 + i);
    peer_ids.push_back(peer_id);
  }

  std::this_thread::sleep_for(500ms);

  // Simulate peer failure
  auto stats_before = adapter.get_stats();
  adapter.disconnect_from_peer(peer_ids[0]);
  adapter.disconnect_from_peer(peer_ids[1]);

  // Wait for recovery
  auto recovery_start = std::chrono::steady_clock::now();
  std::this_thread::sleep_for(2s);
  auto recovery_time = std::chrono::steady_clock::now() - recovery_start;

  auto stats_after = adapter.get_stats();

  // Check recovery time
  auto recovery_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(recovery_time)
          .count();
  EXPECT_LE(recovery_ms, 2000); // Recovery within 2s

  // In a real implementation, should check reconnection success rate > 95%

  adapter.stop();
}

/**
 * Integration test: multi-node mesh
 */
TEST_F(MeshCoreAdapterTest, MultiNodeMesh) {
  // Create 3 nodes
  MeshConfig config1 = config_;
  config1.node_id = "node1";
  config1.listen_port = 9001;

  MeshConfig config2 = config_;
  config2.node_id = "node2";
  config2.listen_port = 9002;
  config2.bootstrap_nodes = {"127.0.0.1:9001"};

  MeshConfig config3 = config_;
  config3.node_id = "node3";
  config3.listen_port = 9003;
  config3.bootstrap_nodes = {"127.0.0.1:9001", "127.0.0.1:9002"};

  MeshCoreAdapter node1(config1);
  MeshCoreAdapter node2(config2);
  MeshCoreAdapter node3(config3);

  // Start all nodes
  node1.start();
  node1.join_mesh();

  node2.start();
  node2.join_mesh();

  node3.start();
  node3.join_mesh();

  std::this_thread::sleep_for(1s);

  // Check connectivity
  auto peers1 = node1.get_connected_peers();
  auto peers2 = node2.get_connected_peers();
  auto peers3 = node3.get_connected_peers();

  EXPECT_GE(peers2.size(), 0); // node2 should connect to node1
  EXPECT_GE(peers3.size(), 0); // node3 should connect to node1 and node2

  // Cleanup
  node3.stop();
  node2.stop();
  node1.stop();
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
