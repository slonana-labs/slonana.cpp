#pragma once

#include <string>
#include <chrono>
#include <atomic>
#include <memory>
#include <functional>
#include <thread>
#include <map>

namespace slonana {
namespace monitoring {

/**
 * @brief Resource usage information
 */
struct ResourceUsage {
    uint64_t total_memory_bytes = 0;
    uint64_t available_memory_bytes = 0;
    uint64_t used_memory_bytes = 0;
    double memory_usage_ratio = 0.0;
    double cpu_usage_percent = 0.0;
    uint64_t total_disk_bytes = 0;
    uint64_t available_disk_bytes = 0;
    double disk_usage_ratio = 0.0;
    std::chrono::system_clock::time_point timestamp;
};

/**
 * @brief Resource monitoring configuration
 */
struct ResourceMonitorConfig {
    double memory_warning_threshold = 0.8;    // 80%
    double memory_critical_threshold = 0.95;  // 95%
    uint64_t memory_headroom_mb = 512;         // 512MB minimum headroom
    double cpu_warning_threshold = 80.0;      // 80%
    double cpu_critical_threshold = 95.0;     // 95%
    double disk_warning_threshold = 0.85;     // 85%
    double disk_critical_threshold = 0.95;    // 95%
    std::chrono::milliseconds check_interval{30000}; // 30 seconds
    bool enable_automatic_logging = true;
    std::string log_file_path = "";
};

/**
 * @brief Resource exhaustion callback type
 */
using ResourceExhaustionCallback = std::function<void(const ResourceUsage&, const std::string&)>;

/**
 * @brief Advanced resource monitoring for preventing SIGTERM from resource exhaustion
 * 
 * This class provides comprehensive resource monitoring to detect and prevent
 * system-level kills due to resource exhaustion. It monitors memory, CPU, and
 * disk usage and provides early warnings and graceful degradation capabilities.
 */
class ResourceMonitor {
public:
    /**
     * @brief Construct resource monitor with configuration
     * @param config Resource monitoring configuration
     */
    explicit ResourceMonitor(const ResourceMonitorConfig& config = {});
    
    /**
     * @brief Destructor
     */
    ~ResourceMonitor();
    
    /**
     * @brief Start resource monitoring
     * @return true if started successfully
     */
    bool start();
    
    /**
     * @brief Stop resource monitoring
     */
    void stop();
    
    /**
     * @brief Get current resource usage
     * @return Current resource usage information
     */
    ResourceUsage get_current_usage();
    
    /**
     * @brief Check if there is sufficient memory headroom
     * @param required_mb Required memory headroom in MB
     * @return true if sufficient headroom is available
     */
    bool ensure_memory_headroom(uint64_t required_mb = 0);
    
    /**
     * @brief Check if system is experiencing resource pressure
     * @return true if resource pressure is detected
     */
    bool is_resource_pressure();
    
    /**
     * @brief Register callback for resource exhaustion events
     * @param callback Function to call when resource exhaustion is detected
     */
    void register_exhaustion_callback(ResourceExhaustionCallback callback);
    
    /**
     * @brief Log current resource usage to file or stdout
     * @param message Optional message to include in log
     */
    void log_resource_usage(const std::string& message = "");
    
    /**
     * @brief Get resource usage as formatted string
     * @param usage Resource usage data
     * @return Formatted string representation
     */
    static std::string format_resource_usage(const ResourceUsage& usage);
    
    /**
     * @brief Get system memory information (static utility)
     * @param total_bytes Output for total memory in bytes
     * @param available_bytes Output for available memory in bytes
     * @return true if successfully retrieved
     */
    static bool get_memory_info(uint64_t& total_bytes, uint64_t& available_bytes);
    
    /**
     * @brief Get system CPU usage (static utility)
     * @return CPU usage percentage (0-100)
     */
    static double get_cpu_usage();
    
    /**
     * @brief Get disk usage for path (static utility)
     * @param path Filesystem path to check
     * @param total_bytes Output for total disk space in bytes
     * @param available_bytes Output for available disk space in bytes
     * @return true if successfully retrieved
     */
    static bool get_disk_usage(const std::string& path, uint64_t& total_bytes, uint64_t& available_bytes);

private:
    ResourceMonitorConfig config_;
    std::atomic<bool> running_;
    std::atomic<bool> resource_pressure_;
    std::unique_ptr<std::thread> monitor_thread_;
    ResourceExhaustionCallback exhaustion_callback_;
    
    /**
     * @brief Main monitoring loop
     */
    void monitor_loop();
    
    /**
     * @brief Check resource thresholds and trigger callbacks if needed
     * @param usage Current resource usage
     */
    void check_thresholds(const ResourceUsage& usage);
};

} // namespace monitoring
} // namespace slonana