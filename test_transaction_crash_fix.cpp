#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

// Test to validate the transaction crash fix
int main() {
    std::cout << "=== Transaction Crash Prevention Test ===" << std::endl;
    
    // Start the validator in a separate process
    std::cout << "Starting slonana validator..." << std::endl;
    
    pid_t validator_pid = fork();
    if (validator_pid == 0) {
        // Child process - start validator
        execl("./slonana_validator", "slonana_validator", 
              "--ledger-path", "/tmp/test_ledger", 
              "--rpc-bind-address", "127.0.0.1:18899",
              "--gossip-bind-address", "127.0.0.1:18001",
              "--log-level", "info",
              nullptr);
        exit(1); // If exec fails
    } else if (validator_pid < 0) {
        std::cerr << "Failed to fork validator process" << std::endl;
        return 1;
    }
    
    // Give validator time to start
    std::cout << "Waiting for validator to start..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    // Test transaction sending with various scenarios that previously caused crashes
    std::vector<std::string> test_transactions = {
        // Valid-looking base64 transaction
        "AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABAAECIQAAIGAAAAAA",
        
        // Short transaction that might cause buffer overflow
        "AQABAg==",
        
        // Long transaction with potential parsing issues
        "AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABAAECIQAAIGAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        
        // Empty transaction
        "",
        
        // Invalid characters that might cause parsing errors
        "INVALID_TRANSACTION_DATA!@#$%^&*()",
        
        // JSON-like data instead of base64
        "{\"invalidTransaction\": true}",
        
        // Binary-looking data
        "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"
    };
    
    bool all_tests_passed = true;
    
    for (size_t i = 0; i < test_transactions.size(); ++i) {
        std::cout << "Testing transaction " << (i + 1) << "/" << test_transactions.size() << "..." << std::endl;
        
        // Create JSON-RPC request
        std::string json_request = R"({
            "jsonrpc": "2.0",
            "id": )" + std::to_string(i + 1) + R"(,
            "method": "sendTransaction",
            "params": [")" + test_transactions[i] + R"("]
        })";
        
        // Send HTTP request to validator
        CURL* curl = curl_easy_init();
        if (curl) {
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            
            curl_easy_setopt(curl, CURLOPT_URL, "http://127.0.0.1:18899");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_request.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
            
            std::string response_data;
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, [](char* data, size_t size, size_t nmemb, std::string* response) {
                response->append(data, size * nmemb);
                return size * nmemb;
            });
            
            CURLcode res = curl_easy_perform(curl);
            
            // Check if validator is still running after transaction
            int validator_status;
            pid_t result = waitpid(validator_pid, &validator_status, WNOHANG);
            
            if (result == validator_pid) {
                std::cout << "❌ FAILED: Validator crashed after transaction " << (i + 1) << std::endl;
                all_tests_passed = false;
                break;
            } else if (result == 0) {
                std::cout << "✅ PASSED: Validator still running after transaction " << (i + 1) << std::endl;
            } else {
                std::cout << "⚠️  WARNING: Could not check validator status for transaction " << (i + 1) << std::endl;
            }
            
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }
        
        // Small delay between requests
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Final check - send a valid RPC request to ensure validator is responsive
    std::cout << "Final responsiveness test..." << std::endl;
    
    CURL* curl = curl_easy_init();
    if (curl) {
        std::string health_request = R"({"jsonrpc":"2.0","id":999,"method":"getHealth"})";
        
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        curl_easy_setopt(curl, CURLOPT_URL, "http://127.0.0.1:18899");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, health_request.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        
        std::string response_data;
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, [](char* data, size_t size, size_t nmemb, std::string* response) {
            response->append(data, size * nmemb);
            return size * nmemb;
        });
        
        CURLcode res = curl_easy_perform(curl);
        
        if (res == CURLE_OK && !response_data.empty()) {
            std::cout << "✅ PASSED: Validator responsive after all transaction tests" << std::endl;
        } else {
            std::cout << "❌ FAILED: Validator not responsive after transaction tests" << std::endl;
            all_tests_passed = false;
        }
        
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    
    // Cleanup - stop validator
    std::cout << "Stopping validator..." << std::endl;
    kill(validator_pid, SIGTERM);
    
    // Wait for validator to stop
    int validator_status;
    waitpid(validator_pid, &validator_status, 0);
    
    std::cout << "=== Test Results ===" << std::endl;
    if (all_tests_passed) {
        std::cout << "🎉 ALL TESTS PASSED: Transaction crash prevention is working!" << std::endl;
        std::cout << "The validator did not crash when processing problematic transactions." << std::endl;
        return 0;
    } else {
        std::cout << "❌ SOME TESTS FAILED: Transaction crash prevention needs more work." << std::endl;
        return 1;
    }
}