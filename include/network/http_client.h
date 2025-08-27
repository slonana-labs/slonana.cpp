#pragma once

#include "common/types.h"
#include <string>
#include <map>
#include <memory>

namespace slonana {
namespace network {

/**
 * HTTP Response structure
 */
struct HttpResponse {
    int status_code = 0;
    std::string body;
    std::map<std::string, std::string> headers;
    bool success = false;
    std::string error_message;
};

/**
 * Simple HTTP client for snapshot downloads and RPC calls
 */
class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    // Basic HTTP operations
    HttpResponse get(const std::string& url, const std::map<std::string, std::string>& headers = {});
    HttpResponse post(const std::string& url, const std::string& data, 
                     const std::map<std::string, std::string>& headers = {});

    // Specialized methods for Solana operations
    HttpResponse solana_rpc_call(const std::string& rpc_url, const std::string& method, 
                                const std::string& params = "[]");
    
    // Download file with progress tracking
    bool download_file(const std::string& url, const std::string& local_path, 
                      std::function<void(size_t, size_t)> progress_callback = nullptr);

    // Configuration
    void set_timeout(int seconds) { timeout_seconds_ = seconds; }
    void set_user_agent(const std::string& user_agent) { user_agent_ = user_agent; }

private:
    int timeout_seconds_;
    std::string user_agent_;
    
    // Helper methods
    HttpResponse execute_request(const std::string& url, const std::string& method, 
                               const std::string& data = "", 
                               const std::map<std::string, std::string>& headers = {});
    std::string escape_url(const std::string& url);
    std::string build_header_string(const std::map<std::string, std::string>& headers);
};

/**
 * Solana RPC response parsing utilities
 */
namespace rpc_utils {
    std::string extract_json_field(const std::string& json, const std::string& field);
    uint64_t extract_slot_from_response(const std::string& response);
    std::string extract_error_message(const std::string& response);
    bool is_rpc_error(const std::string& response);
}

} // namespace network
} // namespace slonana