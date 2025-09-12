#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace slonana {
namespace monitoring {

/**
 * @brief Health check status levels
 */
enum class HealthStatus {
  HEALTHY,   // Component is functioning normally
  DEGRADED,  // Component has issues but is functional
  UNHEALTHY, // Component is not functioning properly
  UNKNOWN    // Status cannot be determined
};

/**
 * @brief Health check result
 */
struct HealthCheckResult {
  HealthStatus status;
  std::string component_name;
  std::string message;
  std::chrono::system_clock::time_point timestamp;
  std::chrono::milliseconds duration;
  std::map<std::string, std::string> metadata;
};

/**
 * @brief Overall system health summary
 */
struct SystemHealth {
  HealthStatus overall_status;
  std::chrono::system_clock::time_point timestamp;
  std::vector<HealthCheckResult> component_results;
  std::string summary_message;
};

/**
 * @brief Health check interface for individual components
 */
class IHealthCheck {
public:
  virtual ~IHealthCheck() = default;

  /**
   * @brief Get the name of this health check
   * @return component name
   */
  virtual std::string get_name() const = 0;

  /**
   * @brief Perform the health check
   * @return health check result
   */
  virtual HealthCheckResult check() = 0;

  /**
   * @brief Get the check interval for this component
   * @return recommended check interval
   */
  virtual std::chrono::milliseconds get_check_interval() const = 0;

  /**
   * @brief Get the timeout for this health check
   * @return maximum duration before timing out
   */
  virtual std::chrono::milliseconds get_timeout() const = 0;
};

/**
 * @brief Health monitoring system configuration
 */
struct HealthMonitorConfig {
  std::chrono::milliseconds default_check_interval{30000};
  std::chrono::milliseconds default_timeout{5000};
  bool enable_auto_checks = true;
  bool enable_http_endpoint = true;
  uint16_t http_port = 8080;
  std::string http_path = "/health";
  size_t max_result_history = 100;
};

/**
 * @brief System health monitoring coordinator
 */
class HealthMonitor {
public:
  explicit HealthMonitor(
      const HealthMonitorConfig &config = HealthMonitorConfig{});
  ~HealthMonitor();

  // Non-copyable, non-movable
  HealthMonitor(const HealthMonitor &) = delete;
  HealthMonitor &operator=(const HealthMonitor &) = delete;

  /**
   * @brief Initialize the health monitoring system
   * @return true if initialization successful
   */
  bool initialize();

  /**
   * @brief Shutdown the health monitoring system
   */
  void shutdown();

  /**
   * @brief Register a health check component
   * @param health_check health check implementation
   */
  void register_health_check(std::shared_ptr<IHealthCheck> health_check);

  /**
   * @brief Unregister a health check component
   * @param name component name to remove
   * @return true if component was removed
   */
  bool unregister_health_check(const std::string &name);

  /**
   * @brief Start automatic health checking
   */
  void start_monitoring();

  /**
   * @brief Stop automatic health checking
   */
  void stop_monitoring();

  /**
   * @brief Perform health checks for all registered components
   * @return system health summary
   */
  SystemHealth check_all();

  /**
   * @brief Perform health check for a specific component
   * @param component_name name of component to check
   * @return health check result, or nullopt if component not found
   */
  std::optional<HealthCheckResult>
  check_component(const std::string &component_name);

  /**
   * @brief Get the last known system health status
   * @return cached system health
   */
  SystemHealth get_last_health() const;

  /**
   * @brief Get health check history for a component
   * @param component_name name of component
   * @param limit maximum number of results to return
   * @return vector of historical results
   */
  std::vector<HealthCheckResult>
  get_component_history(const std::string &component_name,
                        size_t limit = 10) const;

  /**
   * @brief Register callback for health status changes
   * @param callback function to call when health status changes
   */
  void register_status_change_callback(
      std::function<void(const SystemHealth &)> callback);

  /**
   * @brief Get current monitoring configuration
   * @return configuration structure
   */
  const HealthMonitorConfig &get_config() const { return config_; }

private:
  void monitoring_thread_func();
  void schedule_next_check(const std::string &component_name);
  void update_system_health(const SystemHealth &health);
  HealthStatus
  calculate_overall_status(const std::vector<HealthCheckResult> &results);

  HealthMonitorConfig config_;
  std::atomic<bool> monitoring_active_{false};
  std::atomic<bool> shutting_down_{false};

  mutable std::mutex health_checks_mutex_;
  std::map<std::string, std::shared_ptr<IHealthCheck>> health_checks_;

  mutable std::mutex results_mutex_;
  std::map<std::string, std::vector<HealthCheckResult>> component_history_;
  SystemHealth last_system_health_;

  std::vector<std::function<void(const SystemHealth &)>> status_callbacks_;
  mutable std::mutex callbacks_mutex_;

  std::thread monitoring_thread_;
};

/**
 * @brief Common health check implementations
 */
namespace health_checks {

/**
 * @brief Database connection health check
 */
class DatabaseHealthCheck : public IHealthCheck {
public:
  explicit DatabaseHealthCheck(const std::string &connection_string);
  std::string get_name() const override { return "database"; }
  HealthCheckResult check() override;
  std::chrono::milliseconds get_check_interval() const override {
    return std::chrono::milliseconds{30000};
  }
  std::chrono::milliseconds get_timeout() const override {
    return std::chrono::milliseconds{5000};
  }

private:
  std::string connection_string_;
};

/**
 * @brief Network connectivity health check
 */
class NetworkHealthCheck : public IHealthCheck {
public:
  explicit NetworkHealthCheck(const std::vector<std::string> &test_endpoints);
  std::string get_name() const override { return "network"; }
  HealthCheckResult check() override;
  std::chrono::milliseconds get_check_interval() const override {
    return std::chrono::milliseconds{15000};
  }
  std::chrono::milliseconds get_timeout() const override {
    return std::chrono::milliseconds{3000};
  }

private:
  std::vector<std::string> test_endpoints_;
};

/**
 * @brief Disk space health check
 */
class DiskSpaceHealthCheck : public IHealthCheck {
public:
  explicit DiskSpaceHealthCheck(const std::string &path,
                                double warning_threshold = 0.8,
                                double critical_threshold = 0.95);
  std::string get_name() const override { return "disk_space"; }
  HealthCheckResult check() override;
  std::chrono::milliseconds get_check_interval() const override {
    return std::chrono::milliseconds{60000};
  }
  std::chrono::milliseconds get_timeout() const override {
    return std::chrono::milliseconds{1000};
  }

private:
  std::string path_;
  double warning_threshold_;
  double critical_threshold_;
};

/**
 * @brief Memory usage health check
 */
class MemoryHealthCheck : public IHealthCheck {
public:
  explicit MemoryHealthCheck(double warning_threshold = 0.8,
                             double critical_threshold = 0.95);
  std::string get_name() const override { return "memory"; }
  HealthCheckResult check() override;
  std::chrono::milliseconds get_check_interval() const override {
    return std::chrono::milliseconds{30000};
  }
  std::chrono::milliseconds get_timeout() const override {
    return std::chrono::milliseconds{1000};
  }

private:
  double warning_threshold_;
  double critical_threshold_;
};

/**
 * @brief Validator-specific health check
 */
class ValidatorHealthCheck : public IHealthCheck {
public:
  ValidatorHealthCheck();
  std::string get_name() const override { return "validator"; }
  HealthCheckResult check() override;
  std::chrono::milliseconds get_check_interval() const override {
    return std::chrono::milliseconds{10000};
  }
  std::chrono::milliseconds get_timeout() const override {
    return std::chrono::milliseconds{2000};
  }
};

} // namespace health_checks

/**
 * @brief Global health monitor instance
 */
class GlobalHealthMonitor {
public:
  static HealthMonitor &instance();
  static bool initialize(const HealthMonitorConfig &config = {});
  static void shutdown();

private:
  static std::unique_ptr<HealthMonitor> instance_;
  static std::mutex instance_mutex_;
};

/**
 * @brief Utility functions for health status
 */
std::string health_status_to_string(HealthStatus status);
HealthStatus health_status_from_string(const std::string &status_str);

} // namespace monitoring
} // namespace slonana