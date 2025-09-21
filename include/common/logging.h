#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

namespace slonana {
namespace common {

/**
 * @brief Logging levels for conditional debug output
 * 
 * Controls logging verbosity to avoid performance impact in production builds.
 * Debug logging should be disabled in release configurations for optimal performance.
 */
enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    CRITICAL = 5  // New critical level for system failures requiring immediate attention
};

/**
 * @brief Structured log entry for JSON logging
 */
struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string module;
    std::string thread_id;
    std::string message;
    std::string error_code;
    std::unordered_map<std::string, std::string> context;
};

/**
 * @brief Interface for alerting channels
 */
class IAlertChannel {
public:
    virtual ~IAlertChannel() = default;
    virtual void send_alert(const LogEntry& entry) = 0;
    virtual bool is_enabled() const = 0;
    virtual std::string get_name() const = 0;
};

/**
 * @brief Global logging configuration
 * 
 * Thread-safe logging configuration that can be adjusted at runtime.
 * Performance-critical paths should check enabled status before formatting strings.
 * Enhanced with structured logging and async processing for production use.
 */
class Logger {
public:
    /// Get the singleton logger instance
    static Logger& instance() {
        static Logger logger;
        return logger;
    }
    
    /// Set current logging level
    void set_level(LogLevel level) noexcept {
        current_level_.store(static_cast<int>(level), std::memory_order_relaxed);
    }
    
    /// Enable/disable structured JSON logging
    void set_json_format(bool enabled) noexcept {
        json_format_.store(enabled, std::memory_order_relaxed);
    }
    
    /// Enable/disable async logging for performance
    void set_async_logging(bool enabled) noexcept {
        if (enabled && !async_enabled_.load()) {
            start_async_worker();
        } else if (!enabled && async_enabled_.load()) {
            stop_async_worker();
        }
    }
    
    /// Add alerting channel for critical failures
    void add_alert_channel(std::unique_ptr<IAlertChannel> channel) {
        std::lock_guard<std::mutex> lock(alert_mutex_);
        alert_channels_.push_back(std::move(channel));
    }
    
    /// Check if debug logging is enabled
    bool is_debug_enabled() const noexcept {
        return current_level_.load(std::memory_order_relaxed) <= static_cast<int>(LogLevel::DEBUG);
    }
    
    /// Check if a specific level is enabled
    bool is_enabled(LogLevel level) const noexcept {
        return static_cast<int>(level) >= current_level_.load(std::memory_order_relaxed);
    }
    
    /// Log a simple message (backward compatibility) 
    /// Note: Uses "unknown" module - prefer log_structured() for better observability
    template<typename... Args>
    void log(LogLevel level, Args&&... args) {
        if (!is_enabled(level)) return;
        
        std::ostringstream oss;
        (oss << ... << args);
        
        // Try to infer module from call context
        std::string module = infer_module_from_context();
        
        LogEntry entry{
            std::chrono::system_clock::now(),
            level,
            module,
            get_thread_id(),
            oss.str(),
            "",
            {}
        };
        
        process_log_entry(entry);
    }
    
    /// Log with explicit module (preferred for better observability)
    template<typename... Args>
    void log(LogLevel level, const std::string& module, Args&&... args) {
        if (!is_enabled(level)) return;
        
        std::ostringstream oss;
        (oss << ... << args);
        
        LogEntry entry{
            std::chrono::system_clock::now(),
            level,
            module,
            get_thread_id(),
            oss.str(),
            "",
            {}
        };
        
        process_log_entry(entry);
    }
    
    /// Log a structured message with context
    void log_structured(LogLevel level, const std::string& module, 
                       const std::string& message, const std::string& error_code = "",
                       const std::unordered_map<std::string, std::string>& context = {}) {
        if (!is_enabled(level)) return;
        
        LogEntry entry{
            std::chrono::system_clock::now(),
            level,
            module,
            get_thread_id(),
            message,
            error_code,
            context
        };
        
        process_log_entry(entry);
    }
    
    /// Log critical failure with automatic alerting
    void log_critical_failure(const std::string& module, const std::string& message,
                              const std::string& error_code = "",
                              const std::unordered_map<std::string, std::string>& context = {}) {
        LogEntry entry{
            std::chrono::system_clock::now(),
            LogLevel::CRITICAL,
            module,
            get_thread_id(),
            message,
            error_code,
            context
        };
        
        process_log_entry(entry);
        trigger_alerts(entry);
    }

private:
    Logger() : current_level_(static_cast<int>(LogLevel::INFO)), 
               json_format_(false), async_enabled_(false), worker_shutdown_(false) {}
    
    ~Logger() {
        stop_async_worker();
    }
    
    std::atomic<int> current_level_;
    std::atomic<bool> json_format_;
    std::atomic<bool> async_enabled_;
    std::atomic<bool> worker_shutdown_;
    
    // Async logging support with bounded queue
    static const size_t MAX_QUEUE_SIZE = 10000;  // Prevent runaway memory usage
    std::queue<LogEntry> log_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread worker_thread_;
    
    // Alerting support with rate limiting
    std::vector<std::unique_ptr<IAlertChannel>> alert_channels_;
    std::mutex alert_mutex_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_alert_time_;
    static constexpr std::chrono::seconds ALERT_RATE_LIMIT_INTERVAL{60};  // 1 minute between same alerts
    
    void process_log_entry(const LogEntry& entry) {
        if (async_enabled_.load()) {
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                // Drop oldest entries if queue is full to prevent memory exhaustion
                if (log_queue_.size() >= MAX_QUEUE_SIZE) {
                    log_queue_.pop();  // Drop oldest entry
                }
                log_queue_.push(entry);
            }
            queue_cv_.notify_one();
        } else {
            output_log_entry(entry);
        }
    }
    
    void output_log_entry(const LogEntry& entry) {
        if (json_format_.load()) {
            std::cout << format_json(entry) << std::endl;
        } else {
            std::cout << format_text(entry) << std::endl;
        }
    }
    
    void trigger_alerts(const LogEntry& entry) {
        std::lock_guard<std::mutex> lock(alert_mutex_);
        
        // Create rate limiting key from module and error code
        std::string rate_key = entry.module + ":" + entry.error_code;
        auto now = std::chrono::steady_clock::now();
        
        // Check rate limiting
        auto it = last_alert_time_.find(rate_key);
        if (it != last_alert_time_.end()) {
            if (now - it->second < ALERT_RATE_LIMIT_INTERVAL) {
                return;  // Skip alert due to rate limiting
            }
        }
        last_alert_time_[rate_key] = now;
        
        for (auto& channel : alert_channels_) {
            if (channel->is_enabled()) {
                try {
                    channel->send_alert(entry);
                } catch (const std::exception& e) {
                    // Don't let alerting failures crash the logger
                    // Use fallback to stderr instead of std::cout to avoid circular logging
                    std::cerr << "Alert channel '" << channel->get_name() 
                              << "' failed: " << e.what() << std::endl;
                }
            }
        }
    }
    
    std::string get_thread_id() const;
    std::string format_json(const LogEntry& entry) const;
    std::string format_text(const LogEntry& entry) const;
    std::string level_to_string(LogLevel level) const;
    std::string infer_module_from_context() const;
    std::string escape_json_string(const std::string& input) const;
    
    void start_async_worker();
    void stop_async_worker();
    void worker_loop();
};

} // namespace common
} // namespace slonana

/**
 * @brief Performance-conscious logging macros
 * 
 * These macros avoid string formatting overhead when logging is disabled.
 * Use LOG_DEBUG for performance-critical paths that should be silent in release builds.
 */
#define LOG_TRACE(...) \
    do { \
        if (slonana::common::Logger::instance().is_enabled(slonana::common::LogLevel::TRACE)) { \
            slonana::common::Logger::instance().log(slonana::common::LogLevel::TRACE, __VA_ARGS__); \
        } \
    } while(0)

#define LOG_DEBUG(...) \
    do { \
        if (slonana::common::Logger::instance().is_debug_enabled()) { \
            slonana::common::Logger::instance().log(slonana::common::LogLevel::DEBUG, __VA_ARGS__); \
        } \
    } while(0)

#define LOG_INFO(...) \
    slonana::common::Logger::instance().log(slonana::common::LogLevel::INFO, __VA_ARGS__)

#define LOG_WARN(...) \
    slonana::common::Logger::instance().log(slonana::common::LogLevel::WARN, __VA_ARGS__)

#define LOG_ERROR(...) \
    slonana::common::Logger::instance().log(slonana::common::LogLevel::ERROR, __VA_ARGS__)

#define LOG_CRITICAL(...) \
    slonana::common::Logger::instance().log(slonana::common::LogLevel::CRITICAL, __VA_ARGS__)

/**
 * @brief Structured logging macros for better observability
 */
#define LOG_STRUCTURED(level, module, message, ...) \
    slonana::common::Logger::instance().log_structured(level, module, message, ##__VA_ARGS__)

#define LOG_CRITICAL_FAILURE(module, message, ...) \
    slonana::common::Logger::instance().log_critical_failure(module, message, ##__VA_ARGS__)

/**
 * @brief Module-specific logging macros
 */
#define LOG_NETWORK_ERROR(message, ...) \
    LOG_CRITICAL_FAILURE("network", message, ##__VA_ARGS__)

#define LOG_CONSENSUS_ERROR(message, ...) \
    LOG_CRITICAL_FAILURE("consensus", message, ##__VA_ARGS__)

#define LOG_LEDGER_ERROR(message, ...) \
    LOG_CRITICAL_FAILURE("ledger", message, ##__VA_ARGS__)

#define LOG_SVM_ERROR(message, ...) \
    LOG_CRITICAL_FAILURE("svm", message, ##__VA_ARGS__)

#define LOG_VALIDATOR_ERROR(message, ...) \
    LOG_CRITICAL_FAILURE("validator", message, ##__VA_ARGS__)
