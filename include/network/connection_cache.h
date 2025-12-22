#pragma once

#include "common/types.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace slonana {
namespace network {

/**
 * Connection cache with health monitoring and auto-reconnection
 * Target: sub-millisecond lookup times
 */
class ConnectionCache {
public:
  enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    UNHEALTHY,
    RECONNECTING,
    FAILED
  };

  struct ConnectionInfo {
    std::string connection_id;
    std::string remote_address;
    uint16_t remote_port;
    ConnectionState state;
    int socket_fd;
    
    // Health metrics
    uint64_t successful_sends;
    uint64_t failed_sends;
    uint64_t last_successful_send;
    uint64_t last_failed_send;
    std::chrono::milliseconds avg_latency;
    std::chrono::steady_clock::time_point last_health_check;
    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point last_used;
    
    // Reconnection tracking
    uint32_t reconnect_attempts;
    std::chrono::steady_clock::time_point next_reconnect_time;
    
    ConnectionInfo() 
        : remote_port(0), state(ConnectionState::DISCONNECTED),
          socket_fd(-1), successful_sends(0), failed_sends(0),
          last_successful_send(0), last_failed_send(0),
          avg_latency(0), reconnect_attempts(0) {
      created_at = std::chrono::steady_clock::now();
      last_used = created_at;
      last_health_check = created_at;
    }
    
    double get_success_rate() const {
      auto total = successful_sends + failed_sends;
      return total > 0 ? static_cast<double>(successful_sends) / total : 0.0;
    }
    
    bool is_healthy() const {
      return state == ConnectionState::CONNECTED && get_success_rate() > 0.95;
    }
  };

  struct CacheConfig {
    size_t max_connections;
    std::chrono::seconds connection_ttl;
    std::chrono::seconds health_check_interval;
    std::chrono::seconds reconnect_base_delay;
    uint32_t max_reconnect_attempts;
    bool enable_auto_reconnect;
    bool enable_connection_pooling;
    double unhealthy_threshold;
    
    CacheConfig()
        : max_connections(10000),
          connection_ttl(300),
          health_check_interval(10),
          reconnect_base_delay(1),
          max_reconnect_attempts(5),
          enable_auto_reconnect(true),
          enable_connection_pooling(true),
          unhealthy_threshold(0.8) {}
  };

  struct CacheStats {
    std::atomic<uint64_t> total_connections{0};
    std::atomic<uint64_t> active_connections{0};
    std::atomic<uint64_t> cache_hits{0};
    std::atomic<uint64_t> cache_misses{0};
    std::atomic<uint64_t> evictions{0};
    std::atomic<uint64_t> reconnections{0};
    std::atomic<uint64_t> health_check_failures{0};
    std::atomic<uint64_t> total_lookups{0};
    
    // Delete copy constructor and assignment to prevent atomic copying
    CacheStats() = default;
    CacheStats(const CacheStats&) = delete;
    CacheStats& operator=(const CacheStats&) = delete;
    
    double get_hit_rate() const {
      auto total = total_lookups.load();
      return total > 0 ? static_cast<double>(cache_hits.load()) / total : 0.0;
    }
    
    std::chrono::microseconds avg_lookup_time{0}; // Target: < 1ms
  };

  // Connection factory callback for creating new connections
  using ConnectionFactory = std::function<int(const std::string&, uint16_t)>;
  
  // Health check callback for custom health checks
  using HealthCheckCallback = std::function<bool(const ConnectionInfo&)>;
  
  // Event callbacks
  using ConnectionEventCallback = std::function<void(const std::string&, ConnectionState)>;

  explicit ConnectionCache(const CacheConfig& config = CacheConfig());
  ~ConnectionCache();

  // Lifecycle
  bool initialize();
  void shutdown();
  bool is_running() const { return running_.load(); }

  // Connection management
  std::shared_ptr<ConnectionInfo> get_or_create(const std::string& address, uint16_t port);
  std::shared_ptr<ConnectionInfo> get(const std::string& connection_id);
  bool remove(const std::string& connection_id);
  void clear();

  // Health monitoring
  void mark_send_success(const std::string& connection_id, std::chrono::milliseconds latency);
  void mark_send_failure(const std::string& connection_id);
  bool is_connection_healthy(const std::string& connection_id);
  void force_health_check(const std::string& connection_id);

  // Statistics
  const CacheStats& get_stats() const { return stats_; }
  void reset_stats();
  std::vector<std::shared_ptr<ConnectionInfo>> get_all_connections() const;
  size_t get_connection_count() const;

  // Configuration
  void set_connection_factory(ConnectionFactory factory) { connection_factory_ = factory; }
  void set_health_check_callback(HealthCheckCallback callback) { health_check_callback_ = callback; }
  void set_connection_event_callback(ConnectionEventCallback callback) { event_callback_ = callback; }

private:
  CacheConfig config_;
  std::atomic<bool> running_;
  CacheStats stats_;

  // Connection storage with fast lookup
  std::unordered_map<std::string, std::shared_ptr<ConnectionInfo>> connections_;
  mutable std::mutex cache_mutex_;

  // Background threads
  std::thread health_monitor_thread_;
  std::thread connection_reaper_thread_;
  std::thread reconnect_thread_;
  std::atomic<bool> should_stop_;

  // Callbacks
  ConnectionFactory connection_factory_;
  HealthCheckCallback health_check_callback_;
  ConnectionEventCallback event_callback_;

  // Internal methods
  void health_monitor_loop();
  void connection_reaper_loop();
  void reconnect_loop();
  
  bool perform_health_check(ConnectionInfo& conn);
  bool attempt_reconnect(ConnectionInfo& conn);
  void update_connection_state(const std::string& conn_id, ConnectionState new_state);
  void evict_connection(const std::string& conn_id);
  
  std::string generate_connection_id(const std::string& address, uint16_t port);
  bool is_connection_stale(const ConnectionInfo& conn) const;
  std::chrono::milliseconds calculate_reconnect_delay(uint32_t attempts) const;

  // Default connection factory
  int default_connection_factory(const std::string& address, uint16_t port);
};

} // namespace network
} // namespace slonana
