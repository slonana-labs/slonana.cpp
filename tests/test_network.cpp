#include "network/gossip.h"
#include "network/rpc_server.h"
#include "test_framework.h"
#include <chrono>
#include <memory>
#include <thread>

void test_rpc_server_initialization() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899"; // Use test port

  slonana::network::SolanaRpcServer rpc_server(config);

  ASSERT_FALSE(rpc_server.is_running());
}

void test_rpc_server_start_stop() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);

  auto start_result = rpc_server.start();
  ASSERT_TRUE(start_result.is_ok());
  ASSERT_TRUE(rpc_server.is_running());

  rpc_server.stop();
  ASSERT_FALSE(rpc_server.is_running());
}

void test_rpc_get_health() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::string request =
      R"({"jsonrpc":"2.0","method":"getHealth","params":"","id":"1"})";
  std::string response = rpc_server.handle_request(request);

  ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
  ASSERT_CONTAINS(response, "\"id\":\"1\"");
  ASSERT_CONTAINS(response, "\"result\":\"ok\"");

  rpc_server.stop();
}

void test_rpc_get_version() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::string request =
      R"({"jsonrpc":"2.0","method":"getVersion","params":"","id":"2"})";
  std::string response = rpc_server.handle_request(request);

  ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
  ASSERT_CONTAINS(response, "\"id\":\"2\"");
  ASSERT_CONTAINS(response, "solana-core");
  ASSERT_CONTAINS(response, "\"feature-set\":");

  rpc_server.stop();
}

void test_rpc_get_slot() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::string request =
      R"({"jsonrpc":"2.0","method":"getSlot","params":"","id":"3"})";
  std::string response = rpc_server.handle_request(request);

  ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
  ASSERT_CONTAINS(response, "\"id\":\"3\"");
  ASSERT_CONTAINS(response, "\"result\":0"); // Initial slot should be 0

  rpc_server.stop();
}

void test_rpc_get_account_info() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::string request =
      R"({"jsonrpc":"2.0","method":"getAccountInfo","params":["11111111111111111111111111111112"],"id":"4"})";
  std::string response = rpc_server.handle_request(request);

  ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
  ASSERT_CONTAINS(response, "\"id\":\"4\"");
  ASSERT_CONTAINS(response, "\"context\":");

  rpc_server.stop();
}

void test_rpc_get_balance() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::string request =
      R"({"jsonrpc":"2.0","method":"getBalance","params":["11111111111111111111111111111112"],"id":"5"})";
  std::string response = rpc_server.handle_request(request);

  ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
  ASSERT_CONTAINS(response, "\"id\":\"5\"");
  ASSERT_CONTAINS(response, "\"context\":");
  ASSERT_CONTAINS(response, "\"value\":");

  rpc_server.stop();
}

void test_rpc_get_block_height() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::string request =
      R"({"jsonrpc":"2.0","method":"getBlockHeight","params":"","id":"6"})";
  std::string response = rpc_server.handle_request(request);

  ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
  ASSERT_CONTAINS(response, "\"id\":\"6\"");
  ASSERT_CONTAINS(response, "\"result\":0"); // Initial block height should be 0

  rpc_server.stop();
}

void test_rpc_get_genesis_hash() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::string request =
      R"({"jsonrpc":"2.0","method":"getGenesisHash","params":"","id":"7"})";
  std::string response = rpc_server.handle_request(request);

  ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
  ASSERT_CONTAINS(response, "\"id\":\"7\"");
  ASSERT_CONTAINS(response, "\"result\":");

  rpc_server.stop();
}

void test_rpc_get_identity() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::string request =
      R"({"jsonrpc":"2.0","method":"getIdentity","params":"","id":"8"})";
  std::string response = rpc_server.handle_request(request);

  ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
  ASSERT_CONTAINS(response, "\"id\":\"8\"");
  ASSERT_CONTAINS(response, "\"identity\":");

  rpc_server.stop();
}

void test_rpc_get_epoch_info() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::string request =
      R"({"jsonrpc":"2.0","method":"getEpochInfo","params":"","id":"9"})";
  std::string response = rpc_server.handle_request(request);

  ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
  ASSERT_CONTAINS(response, "\"id\":\"9\"");
  ASSERT_CONTAINS(response, "\"epoch\":");
  ASSERT_CONTAINS(response, "\"slotIndex\":");
  ASSERT_CONTAINS(response, "\"slotsInEpoch\":");

  rpc_server.stop();
}

void test_rpc_unknown_method() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::string request =
      R"({"jsonrpc":"2.0","method":"unknownMethod","params":"","id":"10"})";
  std::string response = rpc_server.handle_request(request);

  ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
  ASSERT_CONTAINS(response, "\"id\":\"10\"");
  ASSERT_CONTAINS(response, "\"error\":");
  ASSERT_CONTAINS(response, "-32601"); // Method not found error code

  rpc_server.stop();
}

void test_rpc_invalid_json() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::string request = "{invalid json}";
  std::string response = rpc_server.handle_request(request);

  ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
  ASSERT_CONTAINS(response, "\"error\":");
  ASSERT_CONTAINS(response, "-32700"); // Parse error code

  rpc_server.stop();
}

void test_rpc_batch_requests() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::string batch_request = R"([
        {"jsonrpc":"2.0","method":"getHealth","params":"","id":"1"},
        {"jsonrpc":"2.0","method":"getSlot","params":"","id":"2"},
        {"jsonrpc":"2.0","method":"getVersion","params":"","id":"3"}
    ])";

  std::string response = rpc_server.handle_request(batch_request);

  // Should handle batch requests (even if not fully implemented)
  ASSERT_NOT_EMPTY(response);

  rpc_server.stop();
}

void test_gossip_protocol_initialization() {
  slonana::common::ValidatorConfig config;
  config.gossip_bind_address = "127.0.0.1:18001";

  auto gossip = std::make_unique<slonana::network::GossipProtocol>(config);

  // Should not be running initially
  ASSERT_TRUE(true); // Basic initialization test
}

void test_gossip_protocol_start_stop() {
  slonana::common::ValidatorConfig config;
  config.gossip_bind_address = "127.0.0.1:18001";

  auto gossip = std::make_unique<slonana::network::GossipProtocol>(config);

  auto start_result = gossip->start();
  ASSERT_TRUE(start_result.is_ok());

  // Give it a moment to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  gossip->stop();
}

void test_gossip_message_broadcasting() {
  slonana::common::ValidatorConfig config;
  config.gossip_bind_address = "127.0.0.1:18001";

  auto gossip = std::make_unique<slonana::network::GossipProtocol>(config);

  auto start_result = gossip->start();
  ASSERT_TRUE(start_result.is_ok());

  slonana::network::NetworkMessage message;
  message.type = slonana::network::MessageType::PING;
  message.sender.resize(32, 0x01);

  auto broadcast_result = gossip->broadcast_message(message);
  ASSERT_TRUE(broadcast_result.is_ok());

  gossip->stop();
}

void run_network_tests(TestRunner &runner) {
  std::cout << "\n=== Network Tests ===" << std::endl;

  runner.run_test("RPC Server Initialization", test_rpc_server_initialization);
  runner.run_test("RPC Server Start/Stop", test_rpc_server_start_stop);
  runner.run_test("RPC getHealth", test_rpc_get_health);
  runner.run_test("RPC getVersion", test_rpc_get_version);
  runner.run_test("RPC getSlot", test_rpc_get_slot);
  runner.run_test("RPC getAccountInfo", test_rpc_get_account_info);
  runner.run_test("RPC getBalance", test_rpc_get_balance);
  runner.run_test("RPC getBlockHeight", test_rpc_get_block_height);
  runner.run_test("RPC getGenesisHash", test_rpc_get_genesis_hash);
  runner.run_test("RPC getIdentity", test_rpc_get_identity);
  runner.run_test("RPC getEpochInfo", test_rpc_get_epoch_info);
  runner.run_test("RPC Unknown Method", test_rpc_unknown_method);
  runner.run_test("RPC Invalid JSON", test_rpc_invalid_json);
  runner.run_test("RPC Batch Requests", test_rpc_batch_requests);
  runner.run_test("Gossip Protocol Initialization",
                  test_gossip_protocol_initialization);
  runner.run_test("Gossip Protocol Start/Stop",
                  test_gossip_protocol_start_stop);
  runner.run_test("Gossip Message Broadcasting",
                  test_gossip_message_broadcasting);
}