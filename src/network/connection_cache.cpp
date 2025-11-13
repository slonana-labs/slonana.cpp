#include "network/connection_cache.h"
#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

// Advanced optimizations
#include <x86intrin.h> // For SIMD and prefetch

namespace slonana {
namespace network {

// Fast hash function for connection IDs - FNV-1a
static inline uint64_t fast_hash(const char* str, size_t len, uint16_t port) {
  uint64_t hash = 14695981039346656037ULL; // FNV offset basis
  for (size_t i = 0; i < len; ++i) {
    hash ^= static_cast<uint64_t>(str[i]);
    hash *= 1099511628211ULL; // FNV prime
  }
  hash ^= static_cast<uint64_t>(port);
  hash *= 1099511628211ULL;
  return hash;
}

// Connection key for faster lookups - uses uint64 hash instead of string
struct ConnectionKey {
  uint64_t hash;
  std::string address;
  uint16_t port;
  
  ConnectionKey(const std::string& addr, uint16_t p) 
    : hash(fast_hash(addr.c_str(), addr.size(), p)), address(addr), port(p) {}
  
  bool operator==(const ConnectionKey& other) const {
    return hash == other.hash && port == other.port && 
           __builtin_expect(address == other.address, 1); // Likely equal if hash matches
  }
};

} // namespace network
} // namespace slonana

// Hash function for ConnectionKey
namespace std {
template<>
struct hash<slonana::network::ConnectionKey> {
  size_t operator()(const slonana::network::ConnectionKey& k) const {
    return k.hash;
  }
};
}

namespace slonana {
namespace network {

ConnectionCache::ConnectionCache(const CacheConfig& config)
    : config_(config), running_(false), should_stop_(false) {}

ConnectionCache::~ConnectionCache() {
  shutdown();
}

bool ConnectionCache::initialize() {
  if (running_.load()) {
    return false;
  }

  running_.store(true);
  should_stop_.store(false);

  // Reserve capacity for hash map to reduce rehashing overhead
  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    connections_.reserve(config_.max_connections);
  }

  // Set default connection factory if not provided
  if (!connection_factory_) {
    connection_factory_ = [this](const std::string& addr, uint16_t port) {
      return default_connection_factory(addr, port);
    };
  }

  // Start background threads
  health_monitor_thread_ = std::thread(&ConnectionCache::health_monitor_loop, this);
  connection_reaper_thread_ = std::thread(&ConnectionCache::connection_reaper_loop, this);
  
  if (config_.enable_auto_reconnect) {
    reconnect_thread_ = std::thread(&ConnectionCache::reconnect_loop, this);
  }

  return true;
}

void ConnectionCache::shutdown() {
  if (!running_.load()) {
    return;
  }

  should_stop_.store(true);
  running_.store(false);

  // Join threads
  if (health_monitor_thread_.joinable()) {
    health_monitor_thread_.join();
  }
  if (connection_reaper_thread_.joinable()) {
    connection_reaper_thread_.join();
  }
  if (reconnect_thread_.joinable()) {
    reconnect_thread_.join();
  }

  // Close all connections
  clear();
}

std::string ConnectionCache::generate_connection_id(const std::string& address, uint16_t port) {
  // Optimized: pre-allocate and use direct string concatenation instead of ostringstream
  std::string conn_id;
  conn_id.reserve(address.size() + 1 + 5); // address + ':' + max 5 digits for port
  conn_id = address;
  conn_id += ':';
  conn_id += std::to_string(port);
  return conn_id;
}

std::shared_ptr<ConnectionCache::ConnectionInfo> 
ConnectionCache::get_or_create(const std::string& address, uint16_t port) {
  std::string conn_id = generate_connection_id(address, port);
  
  // Prefetch the hash map data to reduce cache miss penalty
  __builtin_prefetch(&connections_, 0, 3);
  
  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    stats_.total_lookups++;
    
    // Check if connection exists (fast path - no timing here)
    auto it = connections_.find(conn_id);
    
    // Branch prediction hint: cache hits are more likely
    if (__builtin_expect(it != connections_.end(), 1)) {
      stats_.cache_hits++;
      it->second->last_used = std::chrono::steady_clock::now();
      
      // Prefetch connection data for immediate use
      __builtin_prefetch(it->second.get(), 0, 3);
      
      return it->second;
    }
    
    stats_.cache_misses++;
    
    // Check connection limit
    if (__builtin_expect(connections_.size() >= config_.max_connections, 0)) {
      // Evict oldest stale connection
      for (auto it = connections_.begin(); it != connections_.end(); ++it) {
        if (is_connection_stale(*it->second)) {
          evict_connection(it->first);
          break;
        }
      }
    }
    
    // Create new connection
    auto conn = std::make_shared<ConnectionInfo>();
    conn->connection_id = std::move(conn_id);
    conn->remote_address = address;
    conn->remote_port = port;
    conn->state = ConnectionState::CONNECTING;
    
    // Attempt to establish connection
    int socket_fd = connection_factory_(address, port);
    if (socket_fd >= 0) {
      conn->socket_fd = socket_fd;
      conn->state = ConnectionState::CONNECTED;
      stats_.active_connections++;
    } else {
      conn->state = ConnectionState::FAILED;
    }
    
    stats_.total_connections++;
    connections_[conn->connection_id] = conn;
    
    if (__builtin_expect(event_callback_ != nullptr, 0)) {
      event_callback_(conn->connection_id, conn->state);
    }
    
    return conn;
  }
}

std::shared_ptr<ConnectionCache::ConnectionInfo> 
ConnectionCache::get(const std::string& connection_id) {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  
  auto it = connections_.find(connection_id);
  if (it != connections_.end()) {
    it->second->last_used = std::chrono::steady_clock::now();
    return it->second;
  }
  
  return nullptr;
}

bool ConnectionCache::remove(const std::string& connection_id) {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  
  auto it = connections_.find(connection_id);
  if (it != connections_.end()) {
    // Close socket
    if (it->second->socket_fd >= 0) {
      close(it->second->socket_fd);
      it->second->socket_fd = -1;
    }
    
    if (it->second->state == ConnectionState::CONNECTED) {
      stats_.active_connections--;
    }
    
    connections_.erase(it);
    return true;
  }
  
  return false;
}

void ConnectionCache::clear() {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  
  for (auto& [conn_id, conn] : connections_) {
    if (conn->socket_fd >= 0) {
      close(conn->socket_fd);
      conn->socket_fd = -1;
    }
  }
  
  connections_.clear();
  stats_.active_connections.store(0);
}

void ConnectionCache::mark_send_success(const std::string& connection_id, 
                                        std::chrono::milliseconds latency) {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  
  auto it = connections_.find(connection_id);
  if (it != connections_.end()) {
    auto& conn = it->second;
    conn->successful_sends++;
    conn->last_successful_send = std::chrono::system_clock::now().time_since_epoch().count();
    conn->last_used = std::chrono::steady_clock::now();
    
    // Update average latency (simple moving average)
    if (conn->avg_latency.count() == 0) {
      conn->avg_latency = latency;
    } else {
      conn->avg_latency = std::chrono::milliseconds(
        (conn->avg_latency.count() * 9 + latency.count()) / 10
      );
    }
    
    // If connection was unhealthy, mark as healthy now
    if (conn->state == ConnectionState::UNHEALTHY && conn->is_healthy()) {
      update_connection_state(connection_id, ConnectionState::CONNECTED);
    }
  }
}

void ConnectionCache::mark_send_failure(const std::string& connection_id) {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  
  auto it = connections_.find(connection_id);
  if (it != connections_.end()) {
    auto& conn = it->second;
    conn->failed_sends++;
    conn->last_failed_send = std::chrono::system_clock::now().time_since_epoch().count();
    
    // Mark as unhealthy if success rate drops
    if (conn->get_success_rate() < config_.unhealthy_threshold) {
      update_connection_state(connection_id, ConnectionState::UNHEALTHY);
    }
  }
}

bool ConnectionCache::is_connection_healthy(const std::string& connection_id) {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  
  auto it = connections_.find(connection_id);
  if (it != connections_.end()) {
    return it->second->is_healthy();
  }
  
  return false;
}

void ConnectionCache::force_health_check(const std::string& connection_id) {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  
  auto it = connections_.find(connection_id);
  if (it != connections_.end()) {
    perform_health_check(*it->second);
  }
}

void ConnectionCache::health_monitor_loop() {
  while (!should_stop_.load()) {
    std::vector<std::string> connections_to_check;
    
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      auto now = std::chrono::steady_clock::now();
      
      for (auto& [conn_id, conn] : connections_) {
        auto time_since_check = std::chrono::duration_cast<std::chrono::seconds>(
          now - conn->last_health_check
        );
        
        if (time_since_check >= config_.health_check_interval) {
          connections_to_check.push_back(conn_id);
        }
      }
    }
    
    // Perform health checks outside lock
    for (const auto& conn_id : connections_to_check) {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      auto it = connections_.find(conn_id);
      if (it != connections_.end()) {
        if (!perform_health_check(*it->second)) {
          stats_.health_check_failures++;
        }
      }
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

void ConnectionCache::connection_reaper_loop() {
  while (!should_stop_.load()) {
    std::vector<std::string> connections_to_evict;
    
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      
      for (auto& [conn_id, conn] : connections_) {
        if (is_connection_stale(*conn)) {
          connections_to_evict.push_back(conn_id);
        }
      }
    }
    
    // Evict stale connections
    for (const auto& conn_id : connections_to_evict) {
      evict_connection(conn_id);
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(10));
  }
}

void ConnectionCache::reconnect_loop() {
  while (!should_stop_.load()) {
    std::vector<std::string> connections_to_reconnect;
    
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      auto now = std::chrono::steady_clock::now();
      
      for (auto& [conn_id, conn] : connections_) {
        if ((conn->state == ConnectionState::UNHEALTHY || 
             conn->state == ConnectionState::FAILED) &&
            conn->reconnect_attempts < config_.max_reconnect_attempts &&
            now >= conn->next_reconnect_time) {
          connections_to_reconnect.push_back(conn_id);
        }
      }
    }
    
    // Attempt reconnections
    for (const auto& conn_id : connections_to_reconnect) {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      auto it = connections_.find(conn_id);
      if (it != connections_.end()) {
        attempt_reconnect(*it->second);
      }
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}

bool ConnectionCache::perform_health_check(ConnectionInfo& conn) {
  conn.last_health_check = std::chrono::steady_clock::now();
  
  // Custom health check if provided
  if (health_check_callback_) {
    bool is_healthy = health_check_callback_(conn);
    if (!is_healthy && conn.state == ConnectionState::CONNECTED) {
      conn.state = ConnectionState::UNHEALTHY;
      return false;
    }
    return is_healthy;
  }
  
  // Default health check: verify socket is still valid
  if (conn.socket_fd < 0) {
    conn.state = ConnectionState::FAILED;
    return false;
  }
  
  // Check if socket is still open
  int error = 0;
  socklen_t len = sizeof(error);
  int result = getsockopt(conn.socket_fd, SOL_SOCKET, SO_ERROR, &error, &len);
  
  if (result != 0 || error != 0) {
    conn.state = ConnectionState::UNHEALTHY;
    return false;
  }
  
  // Check success rate
  if (!conn.is_healthy()) {
    conn.state = ConnectionState::UNHEALTHY;
    return false;
  }
  
  return true;
}

bool ConnectionCache::attempt_reconnect(ConnectionInfo& conn) {
  conn.state = ConnectionState::RECONNECTING;
  conn.reconnect_attempts++;
  
  // Close old socket
  if (conn.socket_fd >= 0) {
    close(conn.socket_fd);
    conn.socket_fd = -1;
  }
  
  // Attempt new connection
  int new_socket = connection_factory_(conn.remote_address, conn.remote_port);
  
  if (new_socket >= 0) {
    conn.socket_fd = new_socket;
    conn.state = ConnectionState::CONNECTED;
    conn.reconnect_attempts = 0;
    conn.successful_sends = 0;
    conn.failed_sends = 0;
    stats_.reconnections++;
    
    if (event_callback_) {
      event_callback_(conn.connection_id, ConnectionState::CONNECTED);
    }
    
    return true;
  } else {
    conn.state = ConnectionState::FAILED;
    conn.next_reconnect_time = std::chrono::steady_clock::now() + 
                               calculate_reconnect_delay(conn.reconnect_attempts);
    return false;
  }
}

void ConnectionCache::update_connection_state(const std::string& conn_id, 
                                              ConnectionState new_state) {
  auto it = connections_.find(conn_id);
  if (it != connections_.end()) {
    auto old_state = it->second->state;
    it->second->state = new_state;
    
    if (old_state == ConnectionState::CONNECTED && new_state != ConnectionState::CONNECTED) {
      stats_.active_connections--;
    } else if (old_state != ConnectionState::CONNECTED && new_state == ConnectionState::CONNECTED) {
      stats_.active_connections++;
    }
    
    if (event_callback_) {
      event_callback_(conn_id, new_state);
    }
  }
}

void ConnectionCache::evict_connection(const std::string& conn_id) {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  
  auto it = connections_.find(conn_id);
  if (it != connections_.end()) {
    if (it->second->socket_fd >= 0) {
      close(it->second->socket_fd);
    }
    
    if (it->second->state == ConnectionState::CONNECTED) {
      stats_.active_connections--;
    }
    
    connections_.erase(it);
    stats_.evictions++;
  }
}

bool ConnectionCache::is_connection_stale(const ConnectionInfo& conn) const {
  auto now = std::chrono::steady_clock::now();
  auto age = std::chrono::duration_cast<std::chrono::seconds>(now - conn.last_used);
  
  return age >= config_.connection_ttl || 
         conn.state == ConnectionState::FAILED;
}

std::chrono::milliseconds ConnectionCache::calculate_reconnect_delay(uint32_t attempts) const {
  // Exponential backoff: base_delay * 2^attempts
  auto delay_ms = config_.reconnect_base_delay.count() * 1000 * (1 << std::min(attempts, 5u));
  return std::chrono::milliseconds(delay_ms);
}

int ConnectionCache::default_connection_factory(const std::string& address, uint16_t port) {
  // Create UDP socket
  int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock_fd < 0) {
    return -1;
  }
  
  // Set non-blocking
  int flags = fcntl(sock_fd, F_GETFL, 0);
  if (flags < 0 || fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    close(sock_fd);
    return -1;
  }
  
  // Connect to remote address (for UDP, this just sets default destination)
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  
  if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) <= 0) {
    close(sock_fd);
    return -1;
  }
  
  if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    // For UDP, connect() failure is not critical
    // Just log and continue
  }
  
  return sock_fd;
}

void ConnectionCache::reset_stats() {
  stats_.total_connections.store(0);
  stats_.cache_hits.store(0);
  stats_.cache_misses.store(0);
  stats_.evictions.store(0);
  stats_.reconnections.store(0);
  stats_.health_check_failures.store(0);
  stats_.total_lookups.store(0);
  stats_.avg_lookup_time = std::chrono::microseconds(0);
}

std::vector<std::shared_ptr<ConnectionCache::ConnectionInfo>> 
ConnectionCache::get_all_connections() const {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  
  std::vector<std::shared_ptr<ConnectionInfo>> result;
  result.reserve(connections_.size());
  
  for (const auto& [_, conn] : connections_) {
    result.push_back(conn);
  }
  
  return result;
}

size_t ConnectionCache::get_connection_count() const {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  return connections_.size();
}

} // namespace network
} // namespace slonana
