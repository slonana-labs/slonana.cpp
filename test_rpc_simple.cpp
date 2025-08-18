#include "network/rpc_server.h"
#include <iostream>
#include <string>

int main() {
    slonana::common::ValidatorConfig config;
    config.rpc_bind_address = "127.0.0.1:18899";
    
    slonana::network::SolanaRpcServer rpc_server(config);
    rpc_server.start();
    
    // Test basic RPC methods
    std::vector<std::string> test_requests = {
        R"({"jsonrpc":"2.0","method":"getHealth","params":"","id":"1"})",
        R"({"jsonrpc":"2.0","method":"getSlot","params":"","id":"2"})",
        R"({"jsonrpc":"2.0","method":"getVersion","params":"","id":"3"})",
        R"({"jsonrpc":"2.0","method":"getAccountInfo","params":["11111111111111111111111111111112"],"id":"4"})",
        R"({"jsonrpc":"2.0","method":"getBalance","params":["11111111111111111111111111111112"],"id":"5"})",
        R"({"jsonrpc":"2.0","method":"getBlock","params":[1],"id":"6"})",
        R"({"jsonrpc":"2.0","method":"getTransaction","params":["signature123"],"id":"7"})",
        R"({"jsonrpc":"2.0","method":"unknown_method","params":"","id":"8"})"
    };
    
    std::cout << "Testing Solana RPC API Implementation:\n\n";
    
    for (const auto& request : test_requests) {
        std::cout << "Request: " << request << "\n";
        std::string response = rpc_server.handle_request(request);
        std::cout << "Response: " << response << "\n\n";
    }
    
    rpc_server.stop();
    return 0;
}