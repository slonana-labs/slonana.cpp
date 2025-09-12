#pragma once

#include "monitoring/metrics.h"
#include "monitoring/prometheus_exporter.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace slonana {
namespace monitoring {

struct PrometheusServerStats {
  uint64_t total_requests = 0;
  uint64_t metrics_requests = 0;
  uint64_t health_requests = 0;
  uint64_t error_requests = 0;
  uint64_t start_time = 0;
};

/**
 * @brief HTTP server for Prometheus metrics export
 *
 * Provides a lightweight HTTP server that serves Prometheus-formatted
 * metrics on /metrics endpoint and health checks on /health endpoint.
 */
class PrometheusServer {
private:
  uint16_t port_;
  std::shared_ptr<IMetricsRegistry> registry_;
  std::unique_ptr<PrometheusExporter> exporter_;

  // Server state
  std::atomic<bool> running_;
  std::thread server_thread_;
  int server_socket_ = -1;

  // Statistics
  PrometheusServerStats stats_;
  mutable std::mutex stats_mutex_;

  // Private methods
  void server_loop();
  void handle_client(int client_socket);
  void handle_metrics_request(int client_socket);
  void handle_health_request(int client_socket);
  void handle_not_found(int client_socket);
  void handle_internal_error(int client_socket);

public:
  PrometheusServer(uint16_t port, std::shared_ptr<IMetricsRegistry> registry);
  ~PrometheusServer();

  // Lifecycle management
  bool start();
  void stop();
  bool is_running() const { return running_.load(); }

  // Configuration
  uint16_t get_port() const { return port_; }

  // Statistics
  PrometheusServerStats get_stats() const;
  void reset_stats();
};

} // namespace monitoring
} // namespace slonana