#include "network/http_client.h"
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <regex>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace slonana {
namespace network {

HttpClient::HttpClient()
    : timeout_seconds_(30), user_agent_("slonana-cpp/1.0") {}

HttpClient::~HttpClient() {}

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
  std::cout << "Downloading snapshot from: " << url << std::endl;
  std::cout << "Saving to: " << local_path << std::endl;

  auto response = get(url);
  if (!response.success) {
    std::cerr << "Failed to download file: " << response.error_message
              << std::endl;
    return false;
  }

  // Save to file
  std::ofstream file(local_path, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Failed to open file for writing: " << local_path << std::endl;
    return false;
  }

  file.write(response.body.data(), response.body.size());
  file.close();

  if (progress_callback) {
    progress_callback(response.body.size(), response.body.size());
  }

  std::cout << "Download completed: " << response.body.size() << " bytes"
            << std::endl;
  return true;
}

HttpResponse
HttpClient::execute_request(const std::string &url, const std::string &method,
                            const std::string &data,
                            const std::map<std::string, std::string> &headers) {
  HttpResponse response;

  // Parse URL - simplified implementation for basic URLs
  std::regex url_regex(R"(^https?://([^/]+)(/.*)?)");
  std::smatch matches;

  if (!std::regex_match(url, matches, url_regex)) {
    response.error_message = "Invalid URL format";
    return response;
  }

  std::string host = matches[1].str();
  std::string path = matches.size() > 2 ? matches[2].str() : "/";
  if (path.empty())
    path = "/";

  // Extract port if specified
  size_t port_pos = host.find(':');
  int port = 80;
  if (port_pos != std::string::npos) {
    port = std::stoi(host.substr(port_pos + 1));
    host = host.substr(0, port_pos);
  }

  // Create socket
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    response.error_message = "Failed to create socket";
    return response;
  }

  // Set timeout
  struct timeval timeout;
  timeout.tv_sec = timeout_seconds_;
  timeout.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  // Resolve hostname
  struct hostent *server = gethostbyname(host.c_str());
  if (!server) {
    close(sock);
    response.error_message = "Failed to resolve hostname: " + host;
    return response;
  }

  // Connect to server
  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);
  memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    close(sock);
    response.error_message = "Failed to connect to server";
    return response;
  }

  // Build HTTP request
  std::ostringstream request;
  request << method << " " << path << " HTTP/1.1\r\n";
  request << "Host: " << host << "\r\n";
  request << "User-Agent: " << user_agent_ << "\r\n";
  request << "Connection: close\r\n";

  // Add custom headers
  for (const auto &header : headers) {
    request << header.first << ": " << header.second << "\r\n";
  }

  // Add content for POST requests
  if (method == "POST" && !data.empty()) {
    request << "Content-Length: " << data.length() << "\r\n";
  }

  request << "\r\n";

  // Add body for POST requests
  if (method == "POST" && !data.empty()) {
    request << data;
  }

  std::string request_str = request.str();

  // Send request
  if (send(sock, request_str.c_str(), request_str.length(), 0) < 0) {
    close(sock);
    response.error_message = "Failed to send request";
    return response;
  }

  // Read response
  std::ostringstream response_stream;
  char buffer[4096];
  ssize_t bytes_received;

  while ((bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
    buffer[bytes_received] = '\0';
    response_stream << buffer;
  }

  close(sock);

  std::string response_str = response_stream.str();
  if (response_str.empty()) {
    response.error_message = "Empty response received";
    return response;
  }

  // Parse HTTP response
  size_t header_end = response_str.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    response.error_message = "Invalid HTTP response format";
    return response;
  }

  std::string header_part = response_str.substr(0, header_end);
  response.body = response_str.substr(header_end + 4);

  // Extract status code
  std::regex status_regex(R"(HTTP/1\.[01] (\d+))");
  std::smatch status_match;
  if (std::regex_search(header_part, status_match, status_regex)) {
    response.status_code = std::stoi(status_match[1].str());
    response.success =
        (response.status_code >= 200 && response.status_code < 300);
  } else {
    response.error_message = "Failed to parse status code";
    return response;
  }

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