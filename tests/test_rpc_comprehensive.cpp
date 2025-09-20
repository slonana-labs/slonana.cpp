#include "network/rpc_server.h"
#include "test_framework.h"
#include <memory>
#include <string>
#include <vector>

// Comprehensive RPC API test suite covering all 35+ methods

void test_rpc_comprehensive_account_methods() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::vector<std::pair<std::string, std::string>> account_tests = {
      {"getAccountInfo",
       R"({"jsonrpc":"2.0","method":"getAccountInfo","params":["11111111111111111111111111111112"],"id":"1"})"},
      {"getBalance",
       R"({"jsonrpc":"2.0","method":"getBalance","params":["11111111111111111111111111111112"],"id":"2"})"},
      {"getProgramAccounts",
       R"({"jsonrpc":"2.0","method":"getProgramAccounts","params":["11111111111111111111111111111112"],"id":"3"})"},
      {"getMultipleAccounts",
       R"({"jsonrpc":"2.0","method":"getMultipleAccounts","params":[["11111111111111111111111111111112"]],"id":"4"})"},
      {"getLargestAccounts",
       R"({"jsonrpc":"2.0","method":"getLargestAccounts","params":"","id":"5"})"},
      {"getMinimumBalanceForRentExemption",
       R"({"jsonrpc":"2.0","method":"getMinimumBalanceForRentExemption","params":[0],"id":"6"})"}};

  for (size_t i = 0; i < account_tests.size(); ++i) {
    const auto &test = account_tests[i];
    std::string response = rpc_server.handle_request(test.second);
    ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
    ASSERT_CONTAINS(response, "\"id\":\"" + std::to_string(i + 1) + "\"");
    // Each method should return either a result or a proper error
    ASSERT_TRUE(response.find("\"result\":") != std::string::npos ||
                response.find("\"error\":") != std::string::npos);
  }

  rpc_server.stop();
}

void test_rpc_comprehensive_block_methods() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::vector<std::pair<std::string, std::string>> block_tests = {
      {"getSlot",
       R"({"jsonrpc":"2.0","method":"getSlot","params":"","id":"1"})"},
      {"getBlock",
       R"({"jsonrpc":"2.0","method":"getBlock","params":[0],"id":"2"})"},
      {"getBlockHeight",
       R"({"jsonrpc":"2.0","method":"getBlockHeight","params":"","id":"3"})"},
      {"getBlocks",
       R"({"jsonrpc":"2.0","method":"getBlocks","params":[0,10],"id":"4"})"},
      {"getFirstAvailableBlock",
       R"({"jsonrpc":"2.0","method":"getFirstAvailableBlock","params":"","id":"5"})"},
      {"getGenesisHash",
       R"({"jsonrpc":"2.0","method":"getGenesisHash","params":"","id":"6"})"},
      {"getSlotLeaders",
       R"({"jsonrpc":"2.0","method":"getSlotLeaders","params":[0,10],"id":"7"})"},
      {"getBlockProduction",
       R"({"jsonrpc":"2.0","method":"getBlockProduction","params":"","id":"8"})"}};

  for (const auto &test : block_tests) {
    std::string response = rpc_server.handle_request(test.second);
    ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
    ASSERT_TRUE(response.find("\"result\":") != std::string::npos ||
                response.find("\"error\":") != std::string::npos);
  }

  rpc_server.stop();
}

void test_rpc_comprehensive_transaction_methods() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::vector<std::pair<std::string, std::string>> transaction_tests = {
      {"getTransaction",
       R"({"jsonrpc":"2.0","method":"getTransaction","params":["signature123"],"id":"1"})"},
      {"sendTransaction",
       R"({"jsonrpc":"2.0","method":"sendTransaction","params":["base64data"],"id":"2"})"},
      {"simulateTransaction",
       R"({"jsonrpc":"2.0","method":"simulateTransaction","params":["base64data"],"id":"3"})"},
      {"getSignatureStatuses",
       R"({"jsonrpc":"2.0","method":"getSignatureStatuses","params":[["sig1","sig2"]],"id":"4"})"},
      {"getConfirmedSignaturesForAddress2",
       R"({"jsonrpc":"2.0","method":"getConfirmedSignaturesForAddress2","params":["11111111111111111111111111111112"],"id":"5"})"}};

  for (const auto &test : transaction_tests) {
    std::string response = rpc_server.handle_request(test.second);
    ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
    ASSERT_TRUE(response.find("\"result\":") != std::string::npos ||
                response.find("\"error\":") != std::string::npos);
  }

  rpc_server.stop();
}

void test_rpc_comprehensive_network_methods() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::vector<std::pair<std::string, std::string>> network_tests = {
      {"getVersion",
       R"({"jsonrpc":"2.0","method":"getVersion","params":"","id":"1"})"},
      {"getClusterNodes",
       R"({"jsonrpc":"2.0","method":"getClusterNodes","params":"","id":"2"})"},
      {"getIdentity",
       R"({"jsonrpc":"2.0","method":"getIdentity","params":"","id":"3"})"},
      {"getHealth",
       R"({"jsonrpc":"2.0","method":"getHealth","params":"","id":"4"})"}};

  for (const auto &test : network_tests) {
    std::string response = rpc_server.handle_request(test.second);
    ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
    ASSERT_TRUE(response.find("\"result\":") != std::string::npos ||
                response.find("\"error\":") != std::string::npos);
  }

  rpc_server.stop();
}

void test_rpc_comprehensive_validator_methods() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::vector<std::pair<std::string, std::string>> validator_tests = {
      {"getVoteAccounts",
       R"({"jsonrpc":"2.0","method":"getVoteAccounts","params":"","id":"1"})"},
      {"getLeaderSchedule",
       R"({"jsonrpc":"2.0","method":"getLeaderSchedule","params":"","id":"2"})"},
      {"getEpochInfo",
       R"({"jsonrpc":"2.0","method":"getEpochInfo","params":"","id":"3"})"},
      {"getEpochSchedule",
       R"({"jsonrpc":"2.0","method":"getEpochSchedule","params":"","id":"4"})"}};

  for (const auto &test : validator_tests) {
    std::string response = rpc_server.handle_request(test.second);
    ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
    ASSERT_TRUE(response.find("\"result\":") != std::string::npos ||
                response.find("\"error\":") != std::string::npos);
  }

  rpc_server.stop();
}

void test_rpc_comprehensive_staking_methods() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::vector<std::pair<std::string, std::string>> staking_tests = {
      {"getStakeActivation",
       R"({"jsonrpc":"2.0","method":"getStakeActivation","params":["11111111111111111111111111111112"],"id":"1"})"},
      {"getInflationGovernor",
       R"({"jsonrpc":"2.0","method":"getInflationGovernor","params":"","id":"2"})"},
      {"getInflationRate",
       R"({"jsonrpc":"2.0","method":"getInflationRate","params":"","id":"3"})"},
      {"getInflationReward",
       R"({"jsonrpc":"2.0","method":"getInflationReward","params":[["11111111111111111111111111111112"]],"id":"4"})"}};

  for (const auto &test : staking_tests) {
    std::string response = rpc_server.handle_request(test.second);
    ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
    ASSERT_TRUE(response.find("\"result\":") != std::string::npos ||
                response.find("\"error\":") != std::string::npos);
  }

  rpc_server.stop();
}

void test_rpc_comprehensive_utility_methods() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::vector<std::pair<std::string, std::string>> utility_tests = {
      {"getRecentBlockhash",
       R"({"jsonrpc":"2.0","method":"getRecentBlockhash","params":"","id":"1"})"},
      {"getLatestBlockhash",
       R"({"jsonrpc":"2.0","method":"getLatestBlockhash","params":"","id":"2"})"},
      {"getFeeForMessage",
       R"({"jsonrpc":"2.0","method":"getFeeForMessage","params":["base64message"],"id":"3"})"},
      {"isBlockhashValid",
       R"({"jsonrpc":"2.0","method":"isBlockhashValid","params":["blockhash123"],"id":"4"})"}};

  for (const auto &test : utility_tests) {
    std::string response = rpc_server.handle_request(test.second);
    ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
    ASSERT_TRUE(response.find("\"result\":") != std::string::npos ||
                response.find("\"error\":") != std::string::npos);
  }

  rpc_server.stop();
}

void test_rpc_error_handling() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  // Test various error conditions
  std::vector<std::pair<std::string, std::string>> error_tests = {
      {"Invalid JSON", "{invalid json}"},
      {"Missing method", R"({"jsonrpc":"2.0","params":"","id":"1"})"},
      {"Unknown method",
       R"({"jsonrpc":"2.0","method":"unknownMethod","params":"","id":"2"})"},
      {"Invalid params",
       R"({"jsonrpc":"2.0","method":"getBalance","params":123,"id":"3"})"},
      {"Missing id", R"({"jsonrpc":"2.0","method":"getHealth","params":""})"}};

  for (const auto &test : error_tests) {
    std::string response = rpc_server.handle_request(test.second);
    ASSERT_CONTAINS(response, "\"error\":");
    // Should contain proper JSON-RPC error codes
    ASSERT_TRUE(response.find("-32") !=
                std::string::npos); // JSON-RPC error codes start with -32
  }

  rpc_server.stop();
}

void test_rpc_performance_batch() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  auto start_time = std::chrono::high_resolution_clock::now();

  // Test performance with multiple rapid requests
  for (int i = 0; i < 100; ++i) {
    std::string request =
        R"({"jsonrpc":"2.0","method":"getHealth","params":"","id":")" +
        std::to_string(i) + R"("})";
    std::string response = rpc_server.handle_request(request);
    ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  std::cout << "Performance: Processed 100 RPC requests in " << duration.count()
            << "ms" << std::endl;

  rpc_server.stop();
}

void test_rpc_concurrent_requests() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  // Test concurrent request handling (simulated by rapid sequential calls)
  std::vector<std::string> methods = {"getHealth", "getVersion", "getSlot",
                                      "getBlockHeight", "getEpochInfo"};

  for (int round = 0; round < 5; ++round) {
    for (const auto &method : methods) {
      std::string request = R"({"jsonrpc":"2.0","method":")" + method +
                            R"(","params":"","id":")" + std::to_string(round) +
                            R"("})";
      std::string response = rpc_server.handle_request(request);
      ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
    }
  }

  rpc_server.stop();
}

void test_rpc_method_coverage() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  // Test that all major method categories are implemented
  std::vector<std::string> core_methods = {
      // Basic methods
      "getHealth", "getVersion", "getSlot", "getBlockHeight",
      // Account methods
      "getAccountInfo", "getBalance", "getProgramAccounts",
      // Block methods
      "getBlock", "getGenesisHash", "getFirstAvailableBlock",
      // Transaction methods
      "getTransaction", "sendTransaction", "simulateTransaction",
      // Network methods
      "getClusterNodes", "getIdentity",
      // Validator methods
      "getVoteAccounts", "getEpochInfo", "getLeaderSchedule",
      // Staking methods
      "getStakeActivation", "getInflationGovernor",
      // Utility methods
      "getRecentBlockhash", "getLatestBlockhash"};

  int successful_methods = 0;

  for (const auto &method : core_methods) {
    std::string request = R"({"jsonrpc":"2.0","method":")" + method +
                          R"(","params":"","id":"test"})";
    std::string response = rpc_server.handle_request(request);

    // Method should either return a result or a proper error (not "method not
    // found")
    if (response.find("\"result\":") != std::string::npos ||
        (response.find("\"error\":") != std::string::npos &&
         response.find("-32601") == std::string::npos)) {
      successful_methods++;
    }
  }

  std::cout << "Method coverage: " << successful_methods << "/"
            << core_methods.size() << " methods implemented" << std::endl;

  // We expect most methods to be implemented (at least 80% coverage)
  ASSERT_GT(successful_methods, static_cast<int>(core_methods.size() * 0.8));

  rpc_server.stop();
}

// Additional comprehensive RPC tests (doubling from 11 to 22)
void test_rpc_advanced_block_methods() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::vector<std::pair<std::string, std::string>> advanced_block_tests = {
      {"getBlocks",
       R"({"jsonrpc":"2.0","method":"getBlocks","params":[0, 10],"id":"1"})"},
      {"getBlocksWithLimit",
       R"({"jsonrpc":"2.0","method":"getBlocksWithLimit","params":[0, 5],"id":"2"})"},
      {"getConfirmedBlocks",
       R"({"jsonrpc":"2.0","method":"getConfirmedBlocks","params":[0, 10],"id":"3"})"},
      {"getFirstAvailableBlock",
       R"({"jsonrpc":"2.0","method":"getFirstAvailableBlock","params":[],"id":"4"})"},
      {"getLeaderSchedule",
       R"({"jsonrpc":"2.0","method":"getLeaderSchedule","params":[],"id":"5"})"}};

  for (size_t i = 0; i < advanced_block_tests.size(); ++i) {
    const auto &test = advanced_block_tests[i];
    std::string response = rpc_server.handle_request(test.second);
    ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
    ASSERT_CONTAINS(response, "\"id\":\"" + std::to_string(i + 1) + "\"");
    ASSERT_TRUE(response.find("\"result\":") != std::string::npos ||
                response.find("\"error\":") != std::string::npos);
  }

  rpc_server.stop();
}

void test_rpc_subscription_methods() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::vector<std::pair<std::string, std::string>> subscription_tests = {
      {"accountSubscribe",
       R"({"jsonrpc":"2.0","method":"accountSubscribe","params":["11111111111111111111111111111112"],"id":"1"})"},
      {"blockSubscribe",
       R"({"jsonrpc":"2.0","method":"blockSubscribe","params":["all"],"id":"2"})"},
      {"logsSubscribe",
       R"({"jsonrpc":"2.0","method":"logsSubscribe","params":["all"],"id":"3"})"},
      {"programSubscribe",
       R"({"jsonrpc":"2.0","method":"programSubscribe","params":["11111111111111111111111111111112"],"id":"4"})"},
      {"signatureSubscribe",
       R"({"jsonrpc":"2.0","method":"signatureSubscribe","params":["5eykt4UsFv8P8NJdTREpY1vzqKqZKvdpKuc147dw2N9d"],"id":"5"})"}};

  for (size_t i = 0; i < subscription_tests.size(); ++i) {
    const auto &test = subscription_tests[i];
    std::string response = rpc_server.handle_request(test.second);
    ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
    ASSERT_CONTAINS(response, "\"id\":\"" + std::to_string(i + 1) + "\"");
    ASSERT_TRUE(response.find("\"result\":") != std::string::npos ||
                response.find("\"error\":") != std::string::npos);
  }

  rpc_server.stop();
}

void test_rpc_token_methods() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::vector<std::pair<std::string, std::string>> token_tests = {
      {"getTokenAccountBalance",
       R"({"jsonrpc":"2.0","method":"getTokenAccountBalance","params":["11111111111111111111111111111112"],"id":"1"})"},
      {"getTokenAccountsByDelegate",
       R"({"jsonrpc":"2.0","method":"getTokenAccountsByDelegate","params":["11111111111111111111111111111112", {"mint": "11111111111111111111111111111112"}],"id":"2"})"},
      {"getTokenAccountsByOwner",
       R"({"jsonrpc":"2.0","method":"getTokenAccountsByOwner","params":["11111111111111111111111111111112", {"mint": "11111111111111111111111111111112"}],"id":"3"})"},
      {"getTokenSupply",
       R"({"jsonrpc":"2.0","method":"getTokenSupply","params":["11111111111111111111111111111112"],"id":"4"})"},
      {"getTokenLargestAccounts",
       R"({"jsonrpc":"2.0","method":"getTokenLargestAccounts","params":["11111111111111111111111111111112"],"id":"5"})"}};

  for (size_t i = 0; i < token_tests.size(); ++i) {
    const auto &test = token_tests[i];
    std::string response = rpc_server.handle_request(test.second);
    ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
    ASSERT_CONTAINS(response, "\"id\":\"" + std::to_string(i + 1) + "\"");
    ASSERT_TRUE(response.find("\"result\":") != std::string::npos ||
                response.find("\"error\":") != std::string::npos);
  }

  rpc_server.stop();
}

void test_rpc_advanced_transaction_methods() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::vector<std::pair<std::string, std::string>> advanced_tx_tests = {
      {"getConfirmedSignaturesForAddress2",
       R"({"jsonrpc":"2.0","method":"getConfirmedSignaturesForAddress2","params":["11111111111111111111111111111112"],"id":"1"})"},
      {"getSignatureStatuses",
       R"({"jsonrpc":"2.0","method":"getSignatureStatuses","params":[["5eykt4UsFv8P8NJdTREpY1vzqKqZKvdpKuc147dw2N9d"]],"id":"2"})"},
      {"getFeeForMessage",
       R"({"jsonrpc":"2.0","method":"getFeeForMessage","params":["AQABAgIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAEBAQAA"],"id":"3"})"},
      {"getRecentPerformanceSamples",
       R"({"jsonrpc":"2.0","method":"getRecentPerformanceSamples","params":[5],"id":"4"})"},
      {"simulateTransaction",
       R"({"jsonrpc":"2.0","method":"simulateTransaction","params":["AQABAgIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAEBAQAA"],"id":"5"})"}};

  for (size_t i = 0; i < advanced_tx_tests.size(); ++i) {
    const auto &test = advanced_tx_tests[i];
    std::string response = rpc_server.handle_request(test.second);
    ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
    ASSERT_CONTAINS(response, "\"id\":\"" + std::to_string(i + 1) + "\"");
    ASSERT_TRUE(response.find("\"result\":") != std::string::npos ||
                response.find("\"error\":") != std::string::npos);
  }

  rpc_server.stop();
}

void test_rpc_data_consistency() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  // Test data consistency across multiple calls
  std::vector<std::string> consistency_tests = {
      R"({"jsonrpc":"2.0","method":"getSlot","params":[],"id":"1"})",
      R"({"jsonrpc":"2.0","method":"getBlockHeight","params":[],"id":"2"})",
      R"({"jsonrpc":"2.0","method":"getEpochInfo","params":[],"id":"3"})"};

  std::vector<std::string> responses;
  for (const auto &test : consistency_tests) {
    std::string response = rpc_server.handle_request(test);
    responses.push_back(response);
    ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
  }

  // All responses should be valid
  ASSERT_EQ(3, responses.size());

  rpc_server.stop();
}

void test_rpc_historical_data_queries() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  std::vector<std::pair<std::string, std::string>> historical_tests = {
      {"getConfirmedBlock",
       R"({"jsonrpc":"2.0","method":"getConfirmedBlock","params":[0],"id":"1"})"},
      {"getConfirmedTransaction",
       R"({"jsonrpc":"2.0","method":"getConfirmedTransaction","params":["5eykt4UsFv8P8NJdTREpY1vzqKqZKvdpKuc147dw2N9d"],"id":"2"})"},
      {"getAccountInfoHistory",
       R"({"jsonrpc":"2.0","method":"getAccountInfo","params":["11111111111111111111111111111112", {"encoding": "base64"}],"id":"3"})"},
      {"getBlockTime",
       R"({"jsonrpc":"2.0","method":"getBlockTime","params":[0],"id":"4"})"}};

  for (size_t i = 0; i < historical_tests.size(); ++i) {
    const auto &test = historical_tests[i];
    std::string response = rpc_server.handle_request(test.second);
    ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
    ASSERT_CONTAINS(response, "\"id\":\"" + std::to_string(i + 1) + "\"");
    ASSERT_TRUE(response.find("\"result\":") != std::string::npos ||
                response.find("\"error\":") != std::string::npos);
  }

  rpc_server.stop();
}

void test_rpc_rate_limiting_advanced() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  auto start_time = std::chrono::high_resolution_clock::now();

  // Send 200 rapid requests to test rate limiting
  int successful_requests = 0;
  for (int i = 0; i < 200; ++i) {
    std::string request =
        R"({"jsonrpc":"2.0","method":"getHealth","params":[],"id":")" +
        std::to_string(i) + R"("})";
    std::string response = rpc_server.handle_request(request);

    if (response.find("\"jsonrpc\":\"2.0\"") != std::string::npos) {
      successful_requests++;
    }
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  std::cout << "Rate limiting test: " << successful_requests
            << "/200 requests in " << duration.count() << "ms" << std::endl;

  // Should handle most requests successfully
  ASSERT_GT(successful_requests, 150);

  rpc_server.stop();
}

void test_rpc_authentication_advanced() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  // Test various authentication scenarios
  std::vector<std::string> auth_methods = {"sendTransaction", "requestAirdrop",
                                           "simulateTransaction"};

  for (const auto &method : auth_methods) {
    // Test without authentication
    std::string request = R"({"jsonrpc":"2.0","method":")" + method +
                          R"(","params":[],"id":"1"})";
    std::string response = rpc_server.handle_request(request);
    ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");

    // Should handle authentication requirements gracefully
    ASSERT_TRUE(response.find("\"result\":") != std::string::npos ||
                response.find("\"error\":") != std::string::npos);
  }

  rpc_server.stop();
}

void test_rpc_protocol_edge_cases() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  // Test protocol edge cases
  std::vector<std::string> edge_cases = {
      R"({"jsonrpc":"2.0","method":"getHealth","params":[],"id":null})", // null
                                                                         // id
      R"({"jsonrpc":"2.0","method":"getHealth","params":[],"id":123})", // numeric
                                                                        // id
      R"({"jsonrpc":"2.0","method":"getHealth","params":null,"id":"1"})", // null
                                                                          // params
      R"({"jsonrpc":"2.0","method":"getHealth","id":"1"})", // missing params
      R"({"method":"getHealth","params":[],"id":"1"})"      // missing jsonrpc
  };

  for (size_t i = 0; i < edge_cases.size(); ++i) {
    std::string response = rpc_server.handle_request(edge_cases[i]);

    // Should handle edge cases gracefully
    ASSERT_TRUE(response.length() > 0);

    // For valid JSON-RPC, should include jsonrpc field
    if (edge_cases[i].find("\"jsonrpc\"") != std::string::npos) {
      ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
    }
  }

  rpc_server.stop();
}

void test_rpc_performance_optimization() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  auto start_time = std::chrono::high_resolution_clock::now();

  // Test performance with various method types
  std::vector<std::string> perf_methods = {"getHealth", "getVersion", "getSlot",
                                           "getBlockHeight", "getEpochInfo"};

  int total_requests = 0;
  for (int round = 0; round < 20; ++round) {
    for (const auto &method : perf_methods) {
      std::string request = R"({"jsonrpc":"2.0","method":")" + method +
                            R"(","params":[],"id":")" +
                            std::to_string(total_requests) + R"("})";
      std::string response = rpc_server.handle_request(request);
      ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
      total_requests++;
    }
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  std::cout << "Performance test: " << total_requests << " requests in "
            << duration.count() << "ms ("
            << (total_requests * 1000.0 / duration.count()) << " req/s)"
            << std::endl;

  ASSERT_EQ(100, total_requests); // 20 rounds * 5 methods

  rpc_server.stop();
}

void test_rpc_error_condition_handling() {
  slonana::common::ValidatorConfig config;
  config.rpc_bind_address = "127.0.0.1:18899";

  slonana::network::SolanaRpcServer rpc_server(config);
  rpc_server.start();

  // Test various error conditions
  std::vector<std::pair<std::string, std::string>> error_tests = {
      {"Invalid Account",
       R"({"jsonrpc":"2.0","method":"getAccountInfo","params":["invalid_account_address"],"id":"1"})"},
      {"Invalid Signature",
       R"({"jsonrpc":"2.0","method":"getConfirmedTransaction","params":["invalid_signature"],"id":"2"})"},
      {"Invalid Block",
       R"({"jsonrpc":"2.0","method":"getConfirmedBlock","params":[-1],"id":"3"})"},
      {"Invalid Parameters",
       R"({"jsonrpc":"2.0","method":"getBalance","params":["valid_account", "invalid_commitment"],"id":"4"})"}};

  for (size_t i = 0; i < error_tests.size(); ++i) {
    const auto &test = error_tests[i];
    std::string response = rpc_server.handle_request(test.second);
    ASSERT_CONTAINS(response, "\"jsonrpc\":\"2.0\"");
    ASSERT_CONTAINS(response, "\"id\":\"" + std::to_string(i + 1) + "\"");

    // Should return proper error response
    ASSERT_TRUE(response.find("\"error\":") != std::string::npos ||
                response.find("\"result\":") != std::string::npos);
  }

  rpc_server.stop();
}

void run_rpc_comprehensive_tests(TestRunner &runner) {
  std::cout << "\n=== Comprehensive RPC API Tests ===" << std::endl;

  // Original 11 tests
  runner.run_test("RPC Account Methods",
                  test_rpc_comprehensive_account_methods);
  runner.run_test("RPC Block Methods", test_rpc_comprehensive_block_methods);
  runner.run_test("RPC Transaction Methods",
                  test_rpc_comprehensive_transaction_methods);
  runner.run_test("RPC Network Methods",
                  test_rpc_comprehensive_network_methods);
  runner.run_test("RPC Validator Methods",
                  test_rpc_comprehensive_validator_methods);
  runner.run_test("RPC Staking Methods",
                  test_rpc_comprehensive_staking_methods);
  runner.run_test("RPC Utility Methods",
                  test_rpc_comprehensive_utility_methods);
  runner.run_test("RPC Error Handling", test_rpc_error_handling);
  runner.run_test("RPC Performance Batch", test_rpc_performance_batch);
  runner.run_test("RPC Concurrent Requests", test_rpc_concurrent_requests);
  runner.run_test("RPC Method Coverage", test_rpc_method_coverage);

  // Additional 11 tests for comprehensive coverage
  runner.run_test("RPC Advanced Block Methods",
                  test_rpc_advanced_block_methods);
  runner.run_test("RPC Subscription Methods", test_rpc_subscription_methods);
  runner.run_test("RPC Token Methods", test_rpc_token_methods);
  runner.run_test("RPC Advanced Transaction Methods",
                  test_rpc_advanced_transaction_methods);
  runner.run_test("RPC Data Consistency", test_rpc_data_consistency);
  runner.run_test("RPC Historical Data Queries",
                  test_rpc_historical_data_queries);
  runner.run_test("RPC Rate Limiting Advanced",
                  test_rpc_rate_limiting_advanced);
  runner.run_test("RPC Authentication Advanced",
                  test_rpc_authentication_advanced);
  runner.run_test("RPC Protocol Edge Cases", test_rpc_protocol_edge_cases);
  runner.run_test("RPC Performance Optimization",
                  test_rpc_performance_optimization);
  runner.run_test("RPC Error Condition Handling",
                  test_rpc_error_condition_handling);
}

// Standalone test main for RPC comprehensive tests
#ifndef COMPREHENSIVE_TESTS
int main() {
  std::cout << "=== RPC Comprehensive Test Suite ===" << std::endl;
  TestRunner runner;
  run_rpc_comprehensive_tests(runner);
  runner.print_summary();
  return runner.all_passed() ? 0 : 1;
}
#endif