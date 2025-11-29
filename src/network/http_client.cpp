#include "network/http_client.h"
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <curl/curl.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <regex>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace slonana {
namespace network {

// Constants for configuration
constexpr size_t PROGRESS_REPORT_INTERVAL = 102400;  // 100KB
constexpr long DEFAULT_DOWNLOAD_TIMEOUT = 300L;      // 5 minutes

// Thread-safe CURL initialization
static std::once_flag curl_init_flag;
static void init_curl_once() {
  curl_global_init(CURL_GLOBAL_DEFAULT);
}

// Callback function for CURL to write response data
static size_t curl_write_callback(void *contents, size_t size, size_t nmemb,
                                   void *userp) {
  std::string *buffer = static_cast<std::string *>(userp);
  size_t total_size = size * nmemb;
  buffer->append(static_cast<char *>(contents), total_size);
  return total_size;
}

// Callback function for CURL to write to file
static size_t curl_file_write_callback(void *contents, size_t size,
                                        size_t nmemb, void *userp) {
  std::ofstream *file = static_cast<std::ofstream *>(userp);
  size_t total_size = size * nmemb;
  file->write(static_cast<char *>(contents), total_size);
  return total_size;
}

// Progress callback structure for file downloads
struct ProgressCallbackData {
  std::function<void(size_t, size_t)> callback;
  size_t last_reported = 0;
};

static int curl_progress_callback(void *clientp, curl_off_t dltotal,
                                   curl_off_t dlnow,
                                   curl_off_t /* ultotal */,
                                   curl_off_t /* ulnow */) {
  ProgressCallbackData *data = static_cast<ProgressCallbackData *>(clientp);
  if (data && data->callback && dltotal > 0) {
    // Report progress at intervals to avoid excessive callbacks
    if (static_cast<size_t>(dlnow) - data->last_reported > PROGRESS_REPORT_INTERVAL) {
      data->callback(static_cast<size_t>(dlnow), static_cast<size_t>(dltotal));
      data->last_reported = static_cast<size_t>(dlnow);
    }
  }
  return 0;
}

HttpClient::HttpClient()
    : timeout_seconds_(30), user_agent_("slonana-cpp/1.0") {
  // Thread-safe initialization of CURL using std::call_once
  std::call_once(curl_init_flag, init_curl_once);
}

HttpClient::~HttpClient() {
  // Note: curl_global_cleanup() is intentionally not called here because
  // it's a global cleanup function that should only be called once when
  // all CURL usage is complete. In a long-running application with multiple
  // HttpClient instances, premature cleanup would break other instances.
  // The OS will clean up when the process exits.
}

HttpResponse
HttpClient::get(const std::string &url,
                const std::map<std::string, std::string> &headers) {
  return execute_request(url, "GET", "", headers);
}

HttpResponse
HttpClient::head(const std::string &url,
                 const std::map<std::string, std::string> &headers) {
  return execute_request(url, "HEAD", "", headers);
}

HttpResponse
HttpClient::post(const std::string &url, const std::string &data,
                 const std::map<std::string, std::string> &headers) {
  return execute_request(url, "POST", data, headers);
}

HttpResponse HttpClient::solana_rpc_call(const std::string &rpc_url,
                                         const std::string &method,
                                         const std::string &params) {
  std::ostringstream json_body;
  json_body << "{" << "\"jsonrpc\":\"2.0\"," << "\"id\":1," << "\"method\":\""
            << method << "\"," << "\"params\":" << params << "}";

  std::map<std::string, std::string> headers = {
      {"Content-Type", "application/json"}, {"Accept", "application/json"}};

  return post(rpc_url, json_body.str(), headers);
}

bool HttpClient::download_file(
    const std::string &url, const std::string &local_path,
    std::function<void(size_t, size_t)> progress_callback) {
  std::cout << "📥 Downloading file using CURL (HTTPS supported)" << std::endl;
  std::cout << "   URL: " << url << std::endl;
  std::cout << "   Destination: " << local_path << std::endl;

  CURL *curl = curl_easy_init();
  if (!curl) {
    std::cerr << "Failed to initialize CURL" << std::endl;
    return false;
  }

  // Open file for writing
  std::ofstream file(local_path, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Failed to open file for writing: " << local_path << std::endl;
    curl_easy_cleanup(curl);
    return false;
  }

  // Setup progress callback if provided
  ProgressCallbackData progress_data;
  progress_data.callback = progress_callback;

  // Configure CURL
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_file_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent_.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds_ > 0 ? static_cast<long>(timeout_seconds_) : DEFAULT_DOWNLOAD_TIMEOUT);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

  // Enable progress callback
  if (progress_callback) {
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_data);
  }

  // Perform the request
  CURLcode res = curl_easy_perform(curl);
  file.close();

  if (res != CURLE_OK) {
    std::cerr << "CURL download failed: " << curl_easy_strerror(res) << std::endl;
    curl_easy_cleanup(curl);
    // Remove partial download
    std::remove(local_path.c_str());
    return false;
  }

  // Check HTTP response code
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  
  if (http_code < 200 || http_code >= 300) {
    std::cerr << "HTTP error: " << http_code << std::endl;
    curl_easy_cleanup(curl);
    // Remove partial download
    std::remove(local_path.c_str());
    return false;
  }

  // Get download size info
  curl_off_t download_size = 0;
  curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &download_size);
  
  std::cout << "✅ Download completed successfully: " << download_size << " bytes" << std::endl;

  // Final progress callback
  if (progress_callback) {
    progress_callback(static_cast<size_t>(download_size), static_cast<size_t>(download_size));
  }

  curl_easy_cleanup(curl);
  return true;
}

HttpResponse
HttpClient::execute_request(const std::string &url, const std::string &method,
                            const std::string &data,
                            const std::map<std::string, std::string> &headers) {
  HttpResponse response;

  CURL *curl = curl_easy_init();
  if (!curl) {
    response.error_message = "Failed to initialize CURL";
    return response;
  }

  std::string response_body;

  // Configure CURL
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent_.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds_ > 0 ? timeout_seconds_ : 30L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

  // Set HTTP method
  if (method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.length());
  } else if (method == "HEAD") {
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
  }
  // GET is the default

  // Build custom headers
  struct curl_slist *header_list = nullptr;
  for (const auto &header : headers) {
    std::string header_line = header.first + ": " + header.second;
    header_list = curl_slist_append(header_list, header_line.c_str());
  }
  if (header_list) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
  }

  // Perform the request
  CURLcode res = curl_easy_perform(curl);

  if (res != CURLE_OK) {
    response.error_message = std::string("CURL error: ") + curl_easy_strerror(res);
    if (header_list) curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    return response;
  }

  // Get HTTP response code
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  
  response.status_code = static_cast<int>(http_code);
  response.body = response_body;
  response.success = (http_code >= 200 && http_code < 300);

  if (!response.success) {
    response.error_message = "HTTP error: " + std::to_string(http_code);
  }

  // Cleanup
  if (header_list) curl_slist_free_all(header_list);
  curl_easy_cleanup(curl);

  return response;
}

std::string HttpClient::escape_url(const std::string &url) {
  // Basic URL escaping - simplified implementation
  return url;
}

std::string HttpClient::build_header_string(
    const std::map<std::string, std::string> &headers) {
  std::ostringstream header_stream;
  for (const auto &header : headers) {
    header_stream << header.first << ": " << header.second << "\r\n";
  }
  return header_stream.str();
}

// RPC utilities implementation
namespace rpc_utils {

std::string extract_json_field(const std::string &json,
                               const std::string &field) {
  std::string search_key = "\"" + field + "\"";
  size_t key_pos = json.find(search_key);
  if (key_pos == std::string::npos)
    return "";

  size_t colon_pos = json.find(":", key_pos);
  if (colon_pos == std::string::npos)
    return "";

  // Skip whitespace
  size_t value_start = colon_pos + 1;
  while (value_start < json.length() && std::isspace(json[value_start])) {
    value_start++;
  }

  if (value_start >= json.length())
    return "";

  // Handle string values
  if (json[value_start] == '"') {
    size_t end_quote = json.find('"', value_start + 1);
    if (end_quote == std::string::npos)
      return "";
    return json.substr(value_start + 1, end_quote - value_start - 1);
  }

  // Handle object values
  if (json[value_start] == '{') {
    int brace_count = 1;
    size_t value_end = value_start + 1;
    while (value_end < json.length() && brace_count > 0) {
      if (json[value_end] == '{')
        brace_count++;
      else if (json[value_end] == '}')
        brace_count--;
      value_end++;
    }
    if (brace_count == 0) {
      return json.substr(value_start, value_end - value_start);
    }
    return "";
  }

  // Handle array values
  if (json[value_start] == '[') {
    int bracket_count = 1;
    size_t value_end = value_start + 1;
    while (value_end < json.length() && bracket_count > 0) {
      if (json[value_end] == '[')
        bracket_count++;
      else if (json[value_end] == ']')
        bracket_count--;
      value_end++;
    }
    if (bracket_count == 0) {
      return json.substr(value_start, value_end - value_start);
    }
    return "";
  }

  // Handle numeric and boolean values
  size_t value_end = value_start;
  while (value_end < json.length() && json[value_end] != ',' &&
         json[value_end] != '}' && json[value_end] != ']' &&
         json[value_end] != '\n' && json[value_end] != ' ' &&
         json[value_end] != '\t') {
    value_end++;
  }

  return json.substr(value_start, value_end - value_start);
}

uint64_t extract_slot_from_response(const std::string &response) {
  // Look for "result" field first
  std::string result = extract_json_field(response, "result");
  if (!result.empty()) {
    try {
      return std::stoull(result);
    } catch (const std::exception &e) {
      // Try to extract from result object
      std::string full_field = extract_json_field(result, "full");
      if (!full_field.empty()) {
        return std::stoull(full_field);
      }
    }
  }
  return 0;
}

std::string extract_error_message(const std::string &response) {
  std::string error_field = extract_json_field(response, "error");
  if (!error_field.empty()) {
    std::string message = extract_json_field(error_field, "message");
    return message.empty() ? error_field : message;
  }
  return "";
}

bool is_rpc_error(const std::string &response) {
  return response.find("\"error\"") != std::string::npos;
}

} // namespace rpc_utils

} // namespace network
} // namespace slonana