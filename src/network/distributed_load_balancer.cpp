#include "network/distributed_load_balancer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>

namespace slonana {
namespace network {

DistributedLoadBalancer::DistributedLoadBalancer(const std::string &balancer_id,
                                                 const ValidatorConfig &config)
    : balancer_id_(balancer_id), config_(config), running_(false) {

  // Initialize default statistics
  std::memset(&current_stats_, 0, sizeof(current_stats_));

  // Initialize lock-free request queue if available
#if HAS_LOCKFREE_QUEUE
  lock_free_request_queue_ = std::make_unique<boost::lockfree::queue<ConnectionRequest*>>(1024);
  std::cout << "Lock-free request queue enabled with ownership tracking" << std::endl;
#else
  std::cout << "Using mutex-protected request queue with condition variable" << std::endl;
#endif

  // Create default load balancing rule
  LoadBalancingRule default_rule;
  default_rule.rule_name = "default";
  default_rule.service_pattern = ".*";
  default_rule.strategy = LoadBalancingStrategy::LEAST_CONNECTIONS;
  default_rule.health_check_interval_ms = 5000;
  default_rule.max_retries = 3;
  default_rule.enable_session_affinity = false;
  load_balancing_rules_["default"] = default_rule;

  std::cout << "Distributed load balancer initialized: " << balancer_id_
            << std::endl;
}

DistributedLoadBalancer::~DistributedLoadBalancer() { stop(); }

bool DistributedLoadBalancer::start() {
  if (running_.load())
    return false;

  running_.store(true);

  // Start background threads
  health_monitor_thread_ =
      std::thread(&DistributedLoadBalancer::health_monitor_loop, this);
  stats_collector_thread_ =
      std::thread(&DistributedLoadBalancer::stats_collector_loop, this);
  circuit_breaker_thread_ =
      std::thread(&DistributedLoadBalancer::circuit_breaker_loop, this);
  request_processor_thread_ =
      std::thread(&DistributedLoadBalancer::request_processor_loop, this);

  std::cout << "Distributed load balancer started: " << balancer_id_
            << std::endl;
  return true;
}

void DistributedLoadBalancer::stop() {
  if (!running_.load())
    return;

  running_.store(false);

  // Stop all threads
  if (health_monitor_thread_.joinable())
    health_monitor_thread_.join();
  if (stats_collector_thread_.joinable())
    stats_collector_thread_.join();
  if (circuit_breaker_thread_.joinable())
    circuit_breaker_thread_.join();
  if (request_processor_thread_.joinable())
    request_processor_thread_.join();

  std::cout << "Distributed load balancer stopped: " << balancer_id_
            << std::endl;
}

bool DistributedLoadBalancer::register_backend_server(
    const BackendServer &server) {
  if (!validate_backend_server(server)) {
    std::cerr << "Invalid backend server configuration" << std::endl;
    return false;
  }

  std::lock_guard<std::mutex> lock(servers_mutex_);

  BackendServer new_server = server;
  new_server.last_health_check = std::chrono::system_clock::now();
  new_server.health_score = 1.0;
  new_server.is_active = true;
  new_server.is_draining = false;

  backend_servers_[server.server_id] = new_server;

  notify_event("server_registered", server.server_id,
               "address=" + server.address + ":" + std::to_string(server.port));

  std::cout << "Registered backend server: " << server.server_id << " at "
            << server.address << ":" << server.port
            << " (region: " << server.region << ")" << std::endl;

  return true;
}

bool DistributedLoadBalancer::unregister_backend_server(
    const std::string &server_id) {
  std::lock_guard<std::mutex> lock(servers_mutex_);

  auto it = backend_servers_.find(server_id);
  if (it == backend_servers_.end()) {
    return false;
  }

  BackendServer server = it->second;
  backend_servers_.erase(it);

  // Remove from service mappings
  for (auto &service_pair : servers_by_service_) {
    auto &server_list = service_pair.second;
    server_list.erase(
        std::remove(server_list.begin(), server_list.end(), server_id),
        server_list.end());
  }

  notify_event("server_unregistered", server_id);

  std::cout << "Unregistered backend server: " << server_id << std::endl;
  return true;
}

bool DistributedLoadBalancer::update_server_status(const std::string &server_id,
                                                   bool is_active) {
  std::lock_guard<std::mutex> lock(servers_mutex_);

  auto it = backend_servers_.find(server_id);
  if (it == backend_servers_.end()) {
    return false;
  }

  it->second.is_active = is_active;
  it->second.last_health_check = std::chrono::system_clock::now();

  if (is_active) {
    it->second.health_score = std::min(1.0, it->second.health_score + 0.1);
  } else {
    it->second.health_score = std::max(0.0, it->second.health_score - 0.2);
  }

  notify_event(is_active ? "server_activated" : "server_deactivated",
               server_id);

  return true;
}

bool DistributedLoadBalancer::update_server_load(
    const std::string &server_id, uint32_t connections,
    std::chrono::milliseconds response_time) {
  std::lock_guard<std::mutex> lock(servers_mutex_);

  auto it = backend_servers_.find(server_id);
  if (it == backend_servers_.end()) {
    return false;
  }

  it->second.current_connections = connections;
  it->second.average_response_time = response_time;
  it->second.last_health_check = std::chrono::system_clock::now();

  // Update health score based on load and response time
  double load_ratio = static_cast<double>(connections) /
                      std::max(1u, it->second.max_connections);
  double response_factor =
      std::max(0.0, 1.0 - (response_time.count() /
                           1000.0)); // Penalize high response times
  double load_factor = std::max(0.0, 1.0 - load_ratio);

  it->second.health_score = (response_factor + load_factor) / 2.0;

  return true;
}

std::vector<BackendServer> DistributedLoadBalancer::get_backend_servers(
    const std::string &service_name) const {
  std::lock_guard<std::mutex> lock(servers_mutex_);

  std::vector<BackendServer> servers;

  if (service_name.empty()) {
    // Return all servers
    for (const auto &pair : backend_servers_) {
      servers.push_back(pair.second);
    }
  } else {
    // Return servers for specific service
    auto service_it = servers_by_service_.find(service_name);
    if (service_it != servers_by_service_.end()) {
      for (const auto &server_id : service_it->second) {
        auto server_it = backend_servers_.find(server_id);
        if (server_it != backend_servers_.end()) {
          servers.push_back(server_it->second);
        }
      }
    }
  }

  return servers;
}

bool DistributedLoadBalancer::add_load_balancing_rule(
    const LoadBalancingRule &rule) {
  std::lock_guard<std::mutex> lock(rules_mutex_);

  load_balancing_rules_[rule.rule_name] = rule;

  // Update server-to-service mappings
  std::lock_guard<std::mutex> servers_lock(servers_mutex_);
  for (const auto &server_id : rule.backend_servers) {
    servers_by_service_[rule.service_pattern].push_back(server_id);
  }

  std::cout << "Added load balancing rule: " << rule.rule_name
            << " for service pattern: " << rule.service_pattern
            << " with strategy: "
            << load_balancing_strategy_to_string(rule.strategy) << std::endl;

  return true;
}

bool DistributedLoadBalancer::remove_load_balancing_rule(
    const std::string &rule_name) {
  std::lock_guard<std::mutex> lock(rules_mutex_);

  auto it = load_balancing_rules_.find(rule_name);
  if (it == load_balancing_rules_.end() || rule_name == "default") {
    return false; // Cannot remove default rule
  }

  load_balancing_rules_.erase(it);

  std::cout << "Removed load balancing rule: " << rule_name << std::endl;
  return true;
}

bool DistributedLoadBalancer::update_load_balancing_rule(
    const LoadBalancingRule &rule) {
  std::lock_guard<std::mutex> lock(rules_mutex_);

  load_balancing_rules_[rule.rule_name] = rule;

  std::cout << "Updated load balancing rule: " << rule.rule_name << std::endl;
  return true;
}

std::vector<LoadBalancingRule>
DistributedLoadBalancer::get_load_balancing_rules() const {
  std::lock_guard<std::mutex> lock(rules_mutex_);

  std::vector<LoadBalancingRule> rules;
  for (const auto &pair : load_balancing_rules_) {
    rules.push_back(pair.second);
  }

  return rules;
}

ConnectionResponse
DistributedLoadBalancer::route_request(const ConnectionRequest &request) {
  ConnectionResponse response;
  response.request_id = request.request_id;
  response.success = false;

  auto start_time = std::chrono::steady_clock::now();

  // Find matching rule
  LoadBalancingRule *rule = find_matching_rule(request.service_name);
  if (!rule) {
    response.error_message = "No matching load balancing rule found";
    return response;
  }

  // Get available servers for the service
  auto available_servers =
      get_available_servers(request.service_name, request.target_region);
  if (available_servers.empty()) {
    response.error_message = "No available servers for service";
    return response;
  }

  // Filter healthy servers
  auto healthy_servers = filter_healthy_servers(available_servers);
  if (healthy_servers.empty()) {
    response.error_message = "No healthy servers available";
    return response;
  }

  // Check session affinity first
  if (rule->enable_session_affinity) {
    std::string session_id =
        request.request_id; // Simplified - could extract from headers
    std::string affinity_server = get_affinity_server(session_id);

    if (!affinity_server.empty() &&
        std::find(healthy_servers.begin(), healthy_servers.end(),
                  affinity_server) != healthy_servers.end()) {
      response.selected_server = affinity_server;
    }
  }

  // Select server using load balancing strategy
  if (response.selected_server.empty()) {
    switch (rule->strategy) {
    case LoadBalancingStrategy::ROUND_ROBIN:
      response.selected_server =
          select_server_round_robin(healthy_servers, request.service_name);
      break;
    case LoadBalancingStrategy::LEAST_CONNECTIONS:
      response.selected_server =
          select_server_least_connections(healthy_servers);
      break;
    case LoadBalancingStrategy::LEAST_RESPONSE_TIME:
      response.selected_server =
          select_server_least_response_time(healthy_servers);
      break;
    case LoadBalancingStrategy::WEIGHTED_ROUND_ROBIN:
      response.selected_server = select_server_weighted(healthy_servers, *rule);
      break;
    case LoadBalancingStrategy::IP_HASH:
      response.selected_server =
          select_server_ip_hash(healthy_servers, request.client_ip);
      break;
    case LoadBalancingStrategy::GEOGRAPHIC:
      response.selected_server =
          select_server_geographic(healthy_servers, request.target_region);
      break;
    case LoadBalancingStrategy::RESOURCE_BASED:
      response.selected_server = select_server_resource_based(healthy_servers);
      break;
    case LoadBalancingStrategy::ADAPTIVE:
      response.selected_server =
          select_server_adaptive(healthy_servers, request);
      break;
    }
  }

  if (response.selected_server.empty()) {
    response.error_message = "Failed to select server";
    return response;
  }

  // Get server details
  std::lock_guard<std::mutex> lock(servers_mutex_);
  auto server_it = backend_servers_.find(response.selected_server);
  if (server_it != backend_servers_.end()) {
    const BackendServer &server = server_it->second;
    response.server_address = server.address;
    response.server_port = server.port;
    response.success = true;

    // Create session affinity if enabled
    if (rule->enable_session_affinity) {
      create_session_affinity(request.request_id, response.selected_server);
    }
  } else {
    response.error_message = "Selected server not found";
    return response;
  }

  auto end_time = std::chrono::steady_clock::now();
  response.response_time =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_time -
                                                            start_time);

  // Update statistics
  update_request_statistics(request, response);

  std::cout << "Routed request " << request.request_id
            << " to server: " << response.selected_server << " ("
            << response.server_address << ":" << response.server_port << ")"
            << std::endl;

  return response;
}

std::vector<std::string> DistributedLoadBalancer::get_available_servers(
    const std::string &service_name, const std::string &region) const {
  std::lock_guard<std::mutex> lock(servers_mutex_);

  std::vector<std::string> available_servers;

  for (const auto &pair : backend_servers_) {
    const BackendServer &server = pair.second;

    if (!server.is_active || server.is_draining) {
      continue;
    }

    // Check region filter
    if (!region.empty() && server.region != region) {
      continue;
    }

    // Check if server can handle the service (simplified check)
    available_servers.push_back(server.server_id);
  }

  return available_servers;
}

bool DistributedLoadBalancer::validate_server_capacity(
    const std::string &server_id) const {
  std::lock_guard<std::mutex> lock(servers_mutex_);

  auto it = backend_servers_.find(server_id);
  if (it == backend_servers_.end()) {
    return false;
  }

  const BackendServer &server = it->second;
  return server.current_connections < server.max_connections;
}

bool DistributedLoadBalancer::perform_health_check(
    const std::string &server_id) {
  std::lock_guard<std::mutex> lock(servers_mutex_);

  auto it = backend_servers_.find(server_id);
  if (it == backend_servers_.end()) {
    return false;
  }

  BackendServer &server = it->second;

  // Simulate health check (in real implementation, this would be a network
  // call)
  bool is_healthy = server.is_active && !is_circuit_breaker_open(server_id);

  server.last_health_check = std::chrono::system_clock::now();

  if (is_healthy) {
    server.health_score = std::min(1.0, server.health_score + 0.05);
    reset_circuit_breaker(server_id);
  } else {
    server.health_score = std::max(0.0, server.health_score - 0.1);
    trigger_failover(server_id);
  }

  return is_healthy;
}

bool DistributedLoadBalancer::set_server_draining(const std::string &server_id,
                                                  bool draining) {
  std::lock_guard<std::mutex> lock(servers_mutex_);

  auto it = backend_servers_.find(server_id);
  if (it == backend_servers_.end()) {
    return false;
  }

  it->second.is_draining = draining;

  notify_event(draining ? "server_draining_started" : "server_draining_stopped",
               server_id);

  std::cout << "Server " << server_id
            << " draining: " << (draining ? "enabled" : "disabled")
            << std::endl;
  return true;
}

std::vector<std::string>
DistributedLoadBalancer::get_unhealthy_servers() const {
  std::lock_guard<std::mutex> lock(servers_mutex_);

  std::vector<std::string> unhealthy_servers;
  for (const auto &pair : backend_servers_) {
    if (!pair.second.is_active || pair.second.health_score < 0.5) {
      unhealthy_servers.push_back(pair.first);
    }
  }

  return unhealthy_servers;
}

bool DistributedLoadBalancer::trigger_failover(
    const std::string &failed_server) {
  std::cout << "Triggering failover for server: " << failed_server << std::endl;

  update_server_status(failed_server, false);
  enable_circuit_breaker(failed_server);

  notify_event("server_failover", failed_server);

  return true;
}

bool DistributedLoadBalancer::create_session_affinity(
    const std::string &session_id, const std::string &server_id) {
  std::lock_guard<std::mutex> lock(affinity_mutex_);

  session_affinity_[session_id] = server_id;
  affinity_timestamps_[session_id] = std::chrono::system_clock::now();

  return true;
}

bool DistributedLoadBalancer::remove_session_affinity(
    const std::string &session_id) {
  std::lock_guard<std::mutex> lock(affinity_mutex_);

  session_affinity_.erase(session_id);
  affinity_timestamps_.erase(session_id);

  return true;
}

std::string DistributedLoadBalancer::get_affinity_server(
    const std::string &session_id) const {
  std::lock_guard<std::mutex> lock(affinity_mutex_);

  auto it = session_affinity_.find(session_id);
  if (it != session_affinity_.end()) {
    // Check if affinity has expired (simplified - 1 hour timeout)
    auto timestamp_it = affinity_timestamps_.find(session_id);
    if (timestamp_it != affinity_timestamps_.end()) {
      auto now = std::chrono::system_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::hours>(
          now - timestamp_it->second);
      if (duration.count() < 1) {
        return it->second;
      }
    }
  }

  return "";
}

bool DistributedLoadBalancer::enable_circuit_breaker(
    const std::string &server_id) {
  std::lock_guard<std::mutex> lock(circuit_breaker_mutex_);

  circuit_breakers_[server_id] = true;
  circuit_breaker_timestamps_[server_id] = std::chrono::system_clock::now();
  failure_counts_[server_id] = failure_counts_[server_id] + 1;

  notify_event("circuit_breaker_opened", server_id);

  std::cout << "Circuit breaker opened for server: " << server_id << std::endl;
  return true;
}

bool DistributedLoadBalancer::disable_circuit_breaker(
    const std::string &server_id) {
  std::lock_guard<std::mutex> lock(circuit_breaker_mutex_);

  circuit_breakers_[server_id] = false;
  failure_counts_[server_id] = 0;

  notify_event("circuit_breaker_closed", server_id);

  std::cout << "Circuit breaker closed for server: " << server_id << std::endl;
  return true;
}

bool DistributedLoadBalancer::is_circuit_breaker_open(
    const std::string &server_id) const {
  std::lock_guard<std::mutex> lock(circuit_breaker_mutex_);

  auto it = circuit_breakers_.find(server_id);
  return it != circuit_breakers_.end() && it->second;
}

LoadBalancerStats DistributedLoadBalancer::get_statistics() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  return current_stats_;
}

std::unordered_map<std::string, double>
DistributedLoadBalancer::get_server_health_scores() const {
  std::lock_guard<std::mutex> lock(servers_mutex_);

  std::unordered_map<std::string, double> health_scores;
  for (const auto &pair : backend_servers_) {
    health_scores[pair.first] = calculate_server_health_score(pair.second);
  }

  return health_scores;
}

std::vector<std::pair<std::string, uint32_t>>
DistributedLoadBalancer::get_server_loads() const {
  std::lock_guard<std::mutex> lock(servers_mutex_);

  std::vector<std::pair<std::string, uint32_t>> server_loads;
  for (const auto &pair : backend_servers_) {
    server_loads.emplace_back(pair.first, pair.second.current_connections);
  }

  return server_loads;
}

bool DistributedLoadBalancer::update_configuration(
    const std::unordered_map<std::string, std::string> &config) {
  for (const auto &pair : config) {
    std::cout << "Updated load balancer config: " << pair.first << " = "
              << pair.second << std::endl;
  }

  return true;
}

std::unordered_map<std::string, std::string>
DistributedLoadBalancer::get_configuration() const {
  std::unordered_map<std::string, std::string> config;

  std::lock_guard<std::mutex> servers_lock(servers_mutex_);
  std::lock_guard<std::mutex> rules_lock(rules_mutex_);

  config["balancer_id"] = balancer_id_;
  config["total_servers"] = std::to_string(backend_servers_.size());
  config["total_rules"] = std::to_string(load_balancing_rules_.size());

  return config;
}

// Private methods
void DistributedLoadBalancer::health_monitor_loop() {
  while (running_.load()) {
    auto servers = get_backend_servers();

    for (const auto &server : servers) {
      perform_health_check(server.server_id);
    }

    cleanup_expired_affinities();

    // Reduced sleep time for faster health detection
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
}

void DistributedLoadBalancer::stats_collector_loop() {
  while (running_.load()) {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    std::lock_guard<std::mutex> servers_lock(servers_mutex_);

    // Update statistics
    current_stats_.total_backends = backend_servers_.size();
    current_stats_.active_backends = 0;

    double total_response_time = 0.0;
    uint32_t response_time_count = 0;

    for (const auto &pair : backend_servers_) {
      if (pair.second.is_active && !pair.second.is_draining) {
        current_stats_.active_backends++;
      }

      if (pair.second.average_response_time.count() > 0) {
        total_response_time += pair.second.average_response_time.count();
        response_time_count++;
      }
    }

    current_stats_.average_response_time_ms =
        response_time_count > 0 ? total_response_time / response_time_count
                                : 0.0;

    // Calculate requests per second (simplified)
    static auto last_time = std::chrono::steady_clock::now();
    static uint64_t last_requests = current_stats_.total_requests;

    auto now = std::chrono::steady_clock::now();
    auto time_diff =
        std::chrono::duration_cast<std::chrono::seconds>(now - last_time);

    if (time_diff.count() > 0) {
      current_stats_.requests_per_second =
          static_cast<double>(current_stats_.total_requests - last_requests) /
          time_diff.count();
      last_time = now;
      last_requests = current_stats_.total_requests;
    }

    // Reduced sleep time for more frequent stat updates
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
}

void DistributedLoadBalancer::circuit_breaker_loop() {
  while (running_.load()) {
    std::lock_guard<std::mutex> lock(circuit_breaker_mutex_);

    auto now = std::chrono::system_clock::now();

    // Check if circuit breakers should be reset (30 second timeout)
    for (auto &pair : circuit_breakers_) {
      if (pair.second) { // Circuit breaker is open
        auto timestamp_it = circuit_breaker_timestamps_.find(pair.first);
        if (timestamp_it != circuit_breaker_timestamps_.end()) {
          auto duration = std::chrono::duration_cast<std::chrono::seconds>(
              now - timestamp_it->second);

          if (duration.count() > 30) {
            // Try to close circuit breaker
            if (perform_health_check(pair.first)) {
              disable_circuit_breaker(pair.first);
            } else {
              // Reset timestamp for next attempt
              circuit_breaker_timestamps_[pair.first] = now;
            }
          }
        }
      }
    }

    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
}

void DistributedLoadBalancer::request_processor_loop() {
  while (running_.load()) {
    std::vector<ConnectionRequest> requests_to_process;

#if HAS_LOCKFREE_QUEUE
    // Lock-free path with RAII-style ownership management
    ConnectionRequest* req_ptr = nullptr;
    for (int i = 0; i < 10; ++i) {
      if (lock_free_request_queue_->pop(req_ptr)) {
        // Use unique_ptr for automatic cleanup (RAII)
        std::unique_ptr<ConnectionRequest> req_guard(req_ptr);
        requests_to_process.push_back(*req_guard);
        queue_allocated_count_.fetch_sub(1, std::memory_order_relaxed);
        // unique_ptr automatically deletes when going out of scope
      } else {
        break; // Queue is empty
      }
    }
    
    // If no requests, sleep briefly (still using minimal sleep for lock-free)
    if (requests_to_process.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
#else
    // Fallback mutex-protected path with event-driven wakeup
    {
      std::unique_lock<std::mutex> lock(request_queue_mutex_);
      
      // Wait for requests with condition variable (event-driven)
      request_queue_cv_.wait_for(lock, std::chrono::milliseconds(10), [this] {
        return !request_queue_.empty() || !running_.load();
      });
      
      if (!running_.load()) {
        break;
      }
      
      // Process up to 10 requests at a time
      for (int i = 0; i < 10 && !request_queue_.empty(); ++i) {
        requests_to_process.push_back(request_queue_.front());
        request_queue_.pop();
      }
    }
#endif

    // Process requests asynchronously
    for (const auto &request : requests_to_process) {
      auto response = route_request(request);
      // Response would be sent back to client in real implementation
    }
  }
}

std::string DistributedLoadBalancer::select_server_round_robin(
    const std::vector<std::string> &servers, const std::string &service_name) {
  if (servers.empty())
    return "";

  // Try shared lock first for read-only access (concurrent reads allowed)
  {
    std::shared_lock<std::shared_mutex> lock(round_robin_mutex_);
    auto it = round_robin_counters_.find(service_name);
    if (it != round_robin_counters_.end()) {
      // Counter exists, use atomic fetch_add (lock-free)
      uint32_t counter = it->second.fetch_add(1, std::memory_order_relaxed);
      return servers[counter % servers.size()];
    }
  }
  
  // Counter doesn't exist, need exclusive lock to insert
  {
    std::unique_lock<std::shared_mutex> lock(round_robin_mutex_);
    // Double-check after acquiring exclusive lock
    auto it = round_robin_counters_.find(service_name);
    if (it == round_robin_counters_.end()) {
      round_robin_counters_.emplace(service_name, 0);
      it = round_robin_counters_.find(service_name);
    }
    // Use atomic fetch_add for the increment
    uint32_t counter = it->second.fetch_add(1, std::memory_order_relaxed);
    return servers[counter % servers.size()];
  }
}

std::string DistributedLoadBalancer::select_server_least_connections(
    const std::vector<std::string> &servers) {
  if (servers.empty())
    return "";

  std::lock_guard<std::mutex> lock(servers_mutex_);

  std::string best_server = servers[0];
  uint32_t min_connections = UINT32_MAX;

  for (const auto &server_id : servers) {
    auto it = backend_servers_.find(server_id);
    if (it != backend_servers_.end()) {
      if (it->second.current_connections < min_connections) {
        min_connections = it->second.current_connections;
        best_server = server_id;
      }
    }
  }

  return best_server;
}

std::string DistributedLoadBalancer::select_server_least_response_time(
    const std::vector<std::string> &servers) {
  if (servers.empty())
    return "";

  std::lock_guard<std::mutex> lock(servers_mutex_);

  std::string best_server = servers[0];
  std::chrono::milliseconds min_response_time =
      std::chrono::milliseconds::max();

  for (const auto &server_id : servers) {
    auto it = backend_servers_.find(server_id);
    if (it != backend_servers_.end()) {
      if (it->second.average_response_time < min_response_time) {
        min_response_time = it->second.average_response_time;
        best_server = server_id;
      }
    }
  }

  return best_server;
}

std::string DistributedLoadBalancer::select_server_weighted(
    const std::vector<std::string> &servers, const LoadBalancingRule &rule) {
  if (servers.empty())
    return "";

  std::vector<std::pair<std::string, uint32_t>> weighted_servers;
  uint32_t total_weight = 0;

  for (const auto &server_id : servers) {
    auto weight_it = rule.server_weights.find(server_id);
    uint32_t weight =
        weight_it != rule.server_weights.end() ? weight_it->second : 1;
    weighted_servers.emplace_back(server_id, weight);
    total_weight += weight;
  }

  if (total_weight == 0)
    return servers[0];

  uint32_t random_value = rand() % total_weight;
  uint32_t current_weight = 0;

  for (const auto &weighted_server : weighted_servers) {
    current_weight += weighted_server.second;
    if (random_value < current_weight) {
      return weighted_server.first;
    }
  }

  return servers[0];
}

std::string DistributedLoadBalancer::select_server_ip_hash(
    const std::vector<std::string> &servers, const std::string &client_ip) {
  if (servers.empty())
    return "";

  std::hash<std::string> hasher;
  size_t hash_value = hasher(client_ip);

  return servers[hash_value % servers.size()];
}

std::string DistributedLoadBalancer::select_server_geographic(
    const std::vector<std::string> &servers, const std::string &target_region) {
  if (servers.empty())
    return "";

  std::lock_guard<std::mutex> lock(servers_mutex_);

  // Prefer servers in the target region
  if (!target_region.empty()) {
    for (const auto &server_id : servers) {
      auto it = backend_servers_.find(server_id);
      if (it != backend_servers_.end() && it->second.region == target_region) {
        return server_id;
      }
    }
  }

  // Fallback to least connections
  return select_server_least_connections(servers);
}

std::string DistributedLoadBalancer::select_server_resource_based(
    const std::vector<std::string> &servers) {
  if (servers.empty())
    return "";

  std::lock_guard<std::mutex> lock(servers_mutex_);

  std::string best_server = servers[0];
  double best_score = 0.0;

  for (const auto &server_id : servers) {
    auto it = backend_servers_.find(server_id);
    if (it != backend_servers_.end()) {
      double score = calculate_server_health_score(it->second);
      if (score > best_score) {
        best_score = score;
        best_server = server_id;
      }
    }
  }

  return best_server;
}

std::string DistributedLoadBalancer::select_server_adaptive(
    const std::vector<std::string> &servers, const ConnectionRequest &request) {
  // Adaptive algorithm that considers multiple factors
  if (servers.empty())
    return "";

  std::lock_guard<std::mutex> lock(servers_mutex_);

  std::string best_server = servers[0];
  double best_score = 0.0;

  for (const auto &server_id : servers) {
    auto it = backend_servers_.find(server_id);
    if (it != backend_servers_.end()) {
      const BackendServer &server = it->second;

      // Calculate composite score
      double health_score = calculate_server_health_score(server);
      double load_score =
          1.0 - (static_cast<double>(server.current_connections) /
                 std::max(1u, server.max_connections));
      double response_time_score =
          std::max(0.0, 1.0 - (server.average_response_time.count() / 1000.0));
      double region_score = (!request.target_region.empty() &&
                             server.region == request.target_region)
                                ? 1.0
                                : 0.5;

      double composite_score = (health_score * 0.3) + (load_score * 0.3) +
                               (response_time_score * 0.2) +
                               (region_score * 0.2);

      if (composite_score > best_score) {
        best_score = composite_score;
        best_server = server_id;
      }
    }
  }

  return best_server;
}

bool DistributedLoadBalancer::validate_backend_server(
    const BackendServer &server) const {
  return !server.server_id.empty() && !server.address.empty() &&
         server.port > 0;
}

LoadBalancingRule *
DistributedLoadBalancer::find_matching_rule(const std::string &service_name) {
  std::lock_guard<std::mutex> lock(rules_mutex_);

  // Find specific rule for service
  for (auto &rule_pair : load_balancing_rules_) {
    const std::string &pattern = rule_pair.second.service_pattern;

    try {
      std::regex pattern_regex(pattern);
      if (std::regex_match(service_name, pattern_regex)) {
        return &rule_pair.second;
      }
    } catch (const std::regex_error &) {
      // Simple string match fallback
      if (pattern == service_name || pattern == ".*") {
        return &rule_pair.second;
      }
    }
  }

  // Fallback to default rule
  auto default_it = load_balancing_rules_.find("default");
  return default_it != load_balancing_rules_.end() ? &default_it->second
                                                   : nullptr;
}

std::vector<std::string> DistributedLoadBalancer::filter_healthy_servers(
    const std::vector<std::string> &servers) const {
  std::lock_guard<std::mutex> servers_lock(servers_mutex_);
  std::lock_guard<std::mutex> circuit_lock(circuit_breaker_mutex_);

  std::vector<std::string> healthy_servers;

  for (const auto &server_id : servers) {
    auto server_it = backend_servers_.find(server_id);
    if (server_it != backend_servers_.end()) {
      const BackendServer &server = server_it->second;

      if (server.is_active && !server.is_draining &&
          server.health_score > 0.5 && !is_circuit_breaker_open(server_id)) {
        healthy_servers.push_back(server_id);
      }
    }
  }

  return healthy_servers;
}

double DistributedLoadBalancer::calculate_server_health_score(
    const BackendServer &server) const {
  if (!server.is_active)
    return 0.0;

  double base_score = server.health_score;
  double load_factor = 1.0 - (static_cast<double>(server.current_connections) /
                              std::max(1u, server.max_connections));
  double response_factor =
      std::max(0.0, 1.0 - (server.average_response_time.count() / 1000.0));

  return (base_score + load_factor + response_factor) / 3.0;
}

void DistributedLoadBalancer::update_request_statistics(
    const ConnectionRequest &request, const ConnectionResponse &response) {
  std::lock_guard<std::mutex> lock(stats_mutex_);

  current_stats_.total_requests++;

  if (response.success) {
    current_stats_.successful_requests++;
    current_stats_.requests_by_backend[response.selected_server]++;
  } else {
    current_stats_.failed_requests++;
  }

  if (request.retry_count > 0) {
    current_stats_.retried_requests++;
  }

  if (!request.target_region.empty()) {
    current_stats_.requests_by_region[request.target_region]++;
  }
}

void DistributedLoadBalancer::cleanup_expired_affinities() {
  std::lock_guard<std::mutex> lock(affinity_mutex_);

  auto now = std::chrono::system_clock::now();
  auto it = affinity_timestamps_.begin();

  while (it != affinity_timestamps_.end()) {
    auto duration =
        std::chrono::duration_cast<std::chrono::hours>(now - it->second);
    if (duration.count() >= 1) { // 1 hour expiry
      session_affinity_.erase(it->first);
      it = affinity_timestamps_.erase(it);
    } else {
      ++it;
    }
  }
}

void DistributedLoadBalancer::reset_circuit_breaker(
    const std::string &server_id) {
  std::lock_guard<std::mutex> lock(circuit_breaker_mutex_);

  auto failure_it = failure_counts_.find(server_id);
  if (failure_it != failure_counts_.end() && failure_it->second > 0) {
    failure_counts_[server_id] = std::max(0u, failure_it->second - 1);

    if (failure_counts_[server_id] == 0) {
      circuit_breakers_[server_id] = false;
    }
  }
}

void DistributedLoadBalancer::notify_event(const std::string &event,
                                           const std::string &server_id,
                                           const std::string &details) {
  if (event_callback_) {
    event_callback_(event, server_id, details);
  }
}

// Utility functions
const char *load_balancing_strategy_to_string(LoadBalancingStrategy strategy) {
  switch (strategy) {
  case LoadBalancingStrategy::ROUND_ROBIN:
    return "round_robin";
  case LoadBalancingStrategy::LEAST_CONNECTIONS:
    return "least_connections";
  case LoadBalancingStrategy::LEAST_RESPONSE_TIME:
    return "least_response_time";
  case LoadBalancingStrategy::WEIGHTED_ROUND_ROBIN:
    return "weighted_round_robin";
  case LoadBalancingStrategy::IP_HASH:
    return "ip_hash";
  case LoadBalancingStrategy::GEOGRAPHIC:
    return "geographic";
  case LoadBalancingStrategy::RESOURCE_BASED:
    return "resource_based";
  case LoadBalancingStrategy::ADAPTIVE:
    return "adaptive";
  default:
    return "unknown";
  }
}

LoadBalancingStrategy
string_to_load_balancing_strategy(const std::string &strategy_str) {
  if (strategy_str == "round_robin")
    return LoadBalancingStrategy::ROUND_ROBIN;
  if (strategy_str == "least_connections")
    return LoadBalancingStrategy::LEAST_CONNECTIONS;
  if (strategy_str == "least_response_time")
    return LoadBalancingStrategy::LEAST_RESPONSE_TIME;
  if (strategy_str == "weighted_round_robin")
    return LoadBalancingStrategy::WEIGHTED_ROUND_ROBIN;
  if (strategy_str == "ip_hash")
    return LoadBalancingStrategy::IP_HASH;
  if (strategy_str == "geographic")
    return LoadBalancingStrategy::GEOGRAPHIC;
  if (strategy_str == "resource_based")
    return LoadBalancingStrategy::RESOURCE_BASED;
  if (strategy_str == "adaptive")
    return LoadBalancingStrategy::ADAPTIVE;
  return LoadBalancingStrategy::LEAST_CONNECTIONS;
}

std::string generate_request_id() {
  static std::atomic<uint64_t> counter{0};
  return "req_" + std::to_string(counter.fetch_add(1)) + "_" +
         std::to_string(std::time(nullptr));
}

} // namespace network
} // namespace slonana