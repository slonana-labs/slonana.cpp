#include "network/rpc_server.h"
#include <iostream>
#include <string>
#include <vector>
#include <iomanip>

class SolanaRpcTester {
private:
    slonana::network::SolanaRpcServer* rpc_server_;
    
public:
    SolanaRpcTester(slonana::network::SolanaRpcServer* server) : rpc_server_(server) {}
    
    void test_method(const std::string& method_name, const std::string& request, const std::string& expected_fields = "") {
        std::cout << "\n🔹 Testing " << method_name << "\n";
        std::cout << "Request: " << request << "\n";
        
        std::string response = rpc_server_->handle_request(request);
        std::cout << "Response: " << response << "\n";
        
        // Basic validation
        bool valid = true;
        std::vector<std::string> errors;
        
        if (response.find("\"jsonrpc\":\"2.0\"") == std::string::npos) {
            valid = false;
            errors.push_back("Missing JSON-RPC 2.0 field");
        }
        
        if (response.find("\"id\":") == std::string::npos) {
            valid = false;
            errors.push_back("Missing ID field");
        }
        
        bool has_result = response.find("\"result\":") != std::string::npos;
        bool has_error = response.find("\"error\":") != std::string::npos;
        
        if (!has_result && !has_error) {
            valid = false;
            errors.push_back("Missing both result and error fields");
        }
        
        if (has_result && has_error) {
            valid = false;
            errors.push_back("Both result and error fields present");
        }
        
        // Check expected fields if provided
        if (!expected_fields.empty() && has_result) {
            if (response.find(expected_fields) == std::string::npos) {
                valid = false;
                errors.push_back("Missing expected field: " + expected_fields);
            }
        }
        
        if (valid) {
            std::cout << "✅ PASS\n";
        } else {
            std::cout << "❌ FAIL: ";
            for (const auto& error : errors) {
                std::cout << error << "; ";
            }
            std::cout << "\n";
        }
    }
    
    void run_comprehensive_test() {
        std::cout << "🚀 Solana RPC API Comprehensive Test Suite\n";
        std::cout << "==========================================\n";
        
        // Account Methods
        std::cout << "\n📁 Account Methods\n";
        test_method("getAccountInfo", 
            R"({"jsonrpc":"2.0","method":"getAccountInfo","params":["11111111111111111111111111111112"],"id":"1"})",
            "\"context\"");
            
        test_method("getBalance", 
            R"({"jsonrpc":"2.0","method":"getBalance","params":["11111111111111111111111111111112"],"id":"2"})",
            "\"value\"");
            
        test_method("getProgramAccounts", 
            R"({"jsonrpc":"2.0","method":"getProgramAccounts","params":["11111111111111111111111111111112"],"id":"3"})",
            "");
            
        test_method("getMultipleAccounts", 
            R"({"jsonrpc":"2.0","method":"getMultipleAccounts","params":[["11111111111111111111111111111112","11111111111111111111111111111113"]],"id":"4"})",
            "");
            
        test_method("getLargestAccounts", 
            R"({"jsonrpc":"2.0","method":"getLargestAccounts","params":[],"id":"5"})",
            "");
            
        test_method("getMinimumBalanceForRentExemption", 
            R"({"jsonrpc":"2.0","method":"getMinimumBalanceForRentExemption","params":[0],"id":"6"})",
            "");
        
        // Block Methods
        std::cout << "\n🔗 Block Methods\n";
        test_method("getSlot", 
            R"({"jsonrpc":"2.0","method":"getSlot","params":[],"id":"7"})",
            "");
            
        test_method("getBlock", 
            R"({"jsonrpc":"2.0","method":"getBlock","params":[1],"id":"8"})",
            "\"blockHash\"");
            
        test_method("getBlockHeight", 
            R"({"jsonrpc":"2.0","method":"getBlockHeight","params":[],"id":"9"})",
            "");
            
        test_method("getBlocks", 
            R"({"jsonrpc":"2.0","method":"getBlocks","params":[0,10],"id":"10"})",
            "");
            
        test_method("getFirstAvailableBlock", 
            R"({"jsonrpc":"2.0","method":"getFirstAvailableBlock","params":[],"id":"11"})",
            "");
            
        test_method("getGenesisHash", 
            R"({"jsonrpc":"2.0","method":"getGenesisHash","params":[],"id":"12"})",
            "");
            
        test_method("getSlotLeaders", 
            R"({"jsonrpc":"2.0","method":"getSlotLeaders","params":[0,10],"id":"13"})",
            "");
            
        test_method("getBlockProduction", 
            R"({"jsonrpc":"2.0","method":"getBlockProduction","params":[],"id":"14"})",
            "");
        
        // Transaction Methods
        std::cout << "\n💸 Transaction Methods\n";
        test_method("getTransaction", 
            R"({"jsonrpc":"2.0","method":"getTransaction","params":["signature123"],"id":"15"})",
            "");
            
        test_method("sendTransaction", 
            R"({"jsonrpc":"2.0","method":"sendTransaction","params":["base64encodedtransaction"],"id":"16"})",
            "");
            
        test_method("simulateTransaction", 
            R"({"jsonrpc":"2.0","method":"simulateTransaction","params":["base64encodedtransaction"],"id":"17"})",
            "");
            
        test_method("getSignatureStatuses", 
            R"({"jsonrpc":"2.0","method":"getSignatureStatuses","params":[["signature1","signature2"]],"id":"18"})",
            "");
            
        test_method("getConfirmedSignaturesForAddress2", 
            R"({"jsonrpc":"2.0","method":"getConfirmedSignaturesForAddress2","params":["11111111111111111111111111111112"],"id":"19"})",
            "");
        
        // Network Methods
        std::cout << "\n🌐 Network Methods\n";
        test_method("getClusterNodes", 
            R"({"jsonrpc":"2.0","method":"getClusterNodes","params":[],"id":"20"})",
            "");
            
        test_method("getVersion", 
            R"({"jsonrpc":"2.0","method":"getVersion","params":[],"id":"21"})",
            "\"solana-core\"");
            
        test_method("getHealth", 
            R"({"jsonrpc":"2.0","method":"getHealth","params":[],"id":"22"})",
            "\"ok\"");
            
        test_method("getIdentity", 
            R"({"jsonrpc":"2.0","method":"getIdentity","params":[],"id":"23"})",
            "");
        
        // Validator Methods
        std::cout << "\n🏛️ Validator Methods\n";
        test_method("getVoteAccounts", 
            R"({"jsonrpc":"2.0","method":"getVoteAccounts","params":[],"id":"24"})",
            "");
            
        test_method("getLeaderSchedule", 
            R"({"jsonrpc":"2.0","method":"getLeaderSchedule","params":[],"id":"25"})",
            "");
            
        test_method("getEpochInfo", 
            R"({"jsonrpc":"2.0","method":"getEpochInfo","params":[],"id":"26"})",
            "");
            
        test_method("getEpochSchedule", 
            R"({"jsonrpc":"2.0","method":"getEpochSchedule","params":[],"id":"27"})",
            "");
        
        // Staking Methods
        std::cout << "\n💰 Staking Methods\n";
        test_method("getStakeActivation", 
            R"({"jsonrpc":"2.0","method":"getStakeActivation","params":["11111111111111111111111111111112"],"id":"28"})",
            "");
            
        test_method("getInflationGovernor", 
            R"({"jsonrpc":"2.0","method":"getInflationGovernor","params":[],"id":"29"})",
            "");
            
        test_method("getInflationRate", 
            R"({"jsonrpc":"2.0","method":"getInflationRate","params":[],"id":"30"})",
            "");
            
        test_method("getInflationReward", 
            R"({"jsonrpc":"2.0","method":"getInflationReward","params":[["11111111111111111111111111111112"]],"id":"31"})",
            "");
        
        // Utility Methods
        std::cout << "\n🔧 Utility Methods\n";
        test_method("getRecentBlockhash", 
            R"({"jsonrpc":"2.0","method":"getRecentBlockhash","params":[],"id":"32"})",
            "");
            
        test_method("getFeeForMessage", 
            R"({"jsonrpc":"2.0","method":"getFeeForMessage","params":["base64message"],"id":"33"})",
            "");
            
        test_method("getLatestBlockhash", 
            R"({"jsonrpc":"2.0","method":"getLatestBlockhash","params":[],"id":"34"})",
            "\"blockhash\"");
            
        test_method("isBlockhashValid", 
            R"({"jsonrpc":"2.0","method":"isBlockhashValid","params":["11111111111111111111111111111112"],"id":"35"})",
            "");
        
        // Error Cases
        std::cout << "\n❗ Error Cases\n";
        test_method("unknown_method", 
            R"({"jsonrpc":"2.0","method":"unknown_method","params":[],"id":"36"})",
            "");
            
        test_method("invalid_json", 
            R"({"invalid":"json})",
            "");
        
        std::cout << "\n🏁 Test Suite Complete!\n";
    }
};

int main() {
    slonana::common::ValidatorConfig config;
    config.rpc_bind_address = "127.0.0.1:18899";
    
    slonana::network::SolanaRpcServer rpc_server(config);
    rpc_server.start();
    
    SolanaRpcTester tester(&rpc_server);
    tester.run_comprehensive_test();
    
    rpc_server.stop();
    return 0;
}