#include "monitoring/prometheus_server.h"
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace slonana {
namespace monitoring {

PrometheusServer::PrometheusServer(uint16_t port,
                                   std::shared_ptr<IMetricsRegistry> registry)
    : port_(port), registry_(registry), running_(false),
      exporter_(std::make_unique<PrometheusExporter>()) {
  std::cout << "Prometheus server initialized on port " << port_ << std::endl;
}

PrometheusServer::~PrometheusServer() { stop(); }

bool PrometheusServer::start() {
  if (running_.load()) {
    return false;
  }

  // Create socket
  server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket_ < 0) {
    std::cerr << "Failed to create socket for Prometheus server" << std::endl;
    return false;
  }

  // Set socket options
  int opt = 1;
  setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  // Bind socket
  struct sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port_);

  if (bind(server_socket_, (struct sockaddr *)&address, sizeof(address)) < 0) {
    std::cerr << "Failed to bind socket for Prometheus server on port " << port_
              << std::endl;
    close(server_socket_);
    return false;
  }

  // Listen for connections
  if (listen(server_socket_, 10) < 0) {
    std::cerr << "Failed to listen on socket for Prometheus server"
              << std::endl;
    close(server_socket_);
    return false;
  }

  running_.store(true);
  server_thread_ = std::thread(&PrometheusServer::server_loop, this);

  std::cout << "Prometheus server started on port " << port_ << std::endl;
  return true;
}

void PrometheusServer::stop() {
  if (!running_.load()) {
    return;
  }

  running_.store(false);

  if (server_socket_ >= 0) {
    close(server_socket_);
    server_socket_ = -1;
  }

  if (server_thread_.joinable()) {
    server_thread_.join();
  }

  std::cout << "Prometheus server stopped" << std::endl;
}

void PrometheusServer::server_loop() {
  while (running_.load()) {
    struct sockaddr_in client_address;
    socklen_t client_len = sizeof(client_address);

    int client_socket =
        accept(server_socket_, (struct sockaddr *)&client_address, &client_len);
    if (client_socket < 0) {
      if (running_.load()) {
        std::cerr << "Failed to accept client connection" << std::endl;
      }
      continue;
    }

    // Handle client in separate thread
    std::thread client_thread(&PrometheusServer::handle_client, this,
                              client_socket);
    client_thread.detach();
  }
}

void PrometheusServer::handle_client(int client_socket) {
  char buffer[4096];
  ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

  if (bytes_read <= 0) {
    close(client_socket);
    return;
  }

  buffer[bytes_read] = '\0';
  std::string request(buffer);

  // Parse HTTP request
  if (request.find("GET /metrics") == 0 ||
      request.find("GET /metrics ") != std::string::npos) {
    handle_metrics_request(client_socket);
  } else if (request.find("GET /health") == 0 ||
             request.find("GET /health ") != std::string::npos) {
    handle_health_request(client_socket);
  } else {
    handle_not_found(client_socket);
  }

  close(client_socket);
}

void PrometheusServer::handle_metrics_request(int client_socket) {
  try {
    std::string metrics_data = exporter_->export_metrics(*registry_);
    std::string content_type = exporter_->get_content_type();

    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << metrics_data.length() << "\r\n";
    response << "Cache-Control: no-cache\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << metrics_data;

    std::string response_str = response.str();
    send(client_socket, response_str.c_str(), response_str.length(), 0);

    // Update statistics
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.total_requests++;
    stats_.metrics_requests++;

  } catch (const std::exception &e) {
    std::cerr << "Error handling metrics request: " << e.what() << std::endl;
    handle_internal_error(client_socket);

    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.error_requests++;
  }
}

void PrometheusServer::handle_health_request(int client_socket) {
  std::string health_data = R"({"status":"healthy","timestamp":)" +
                            std::to_string(std::time(nullptr)) + "}";

  std::ostringstream response;
  response << "HTTP/1.1 200 OK\r\n";
  response << "Content-Type: application/json\r\n";
  response << "Content-Length: " << health_data.length() << "\r\n";
  response << "Cache-Control: no-cache\r\n";
  response << "Connection: close\r\n";
  response << "\r\n";
  response << health_data;

  std::string response_str = response.str();
  send(client_socket, response_str.c_str(), response_str.length(), 0);

  // Update statistics
  std::lock_guard<std::mutex> lock(stats_mutex_);
  stats_.total_requests++;
  stats_.health_requests++;
}

void PrometheusServer::handle_not_found(int client_socket) {
  std::string error_data = "404 Not Found";

  std::ostringstream response;
  response << "HTTP/1.1 404 Not Found\r\n";
  response << "Content-Type: text/plain\r\n";
  response << "Content-Length: " << error_data.length() << "\r\n";
  response << "Connection: close\r\n";
  response << "\r\n";
  response << error_data;

  std::string response_str = response.str();
  send(client_socket, response_str.c_str(), response_str.length(), 0);

  // Update statistics
  std::lock_guard<std::mutex> lock(stats_mutex_);
  stats_.total_requests++;
  stats_.error_requests++;
}

void PrometheusServer::handle_internal_error(int client_socket) {
  std::string error_data = "500 Internal Server Error";

  std::ostringstream response;
  response << "HTTP/1.1 500 Internal Server Error\r\n";
  response << "Content-Type: text/plain\r\n";
  response << "Content-Length: " << error_data.length() << "\r\n";
  response << "Connection: close\r\n";
  response << "\r\n";
  response << error_data;

  std::string response_str = response.str();
  send(client_socket, response_str.c_str(), response_str.length(), 0);
}

PrometheusServerStats PrometheusServer::get_stats() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  return stats_;
}

void PrometheusServer::reset_stats() {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  stats_ = PrometheusServerStats{};
}

} // namespace monitoring
} // namespace slonana