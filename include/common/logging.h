/**
 * @file logging.h
 * @brief Defines a high-performance, structured, and thread-safe logging framework.
 *
 * This file contains the core components for logging, including a singleton
 * Logger class, log levels, structured log entries, and an interface for
 * alert channels. It supports synchronous and asynchronous logging, JSON and
 * text formats, and provides a set of macros for efficient, compile-time
 * log level checking.
 */
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
 * @brief Defines the severity levels for log messages.
 * @details Controls logging verbosity. In production builds, lower levels
 * like TRACE and DEBUG can be compiled out to improve performance.
 */
enum class LogLevel {
    /// @brief Detailed diagnostic information, typically for tracing code execution.
    TRACE = 0,
    /// @brief Information useful for debugging.
    DEBUG = 1,
    /// @brief General informational messages about system state.
    INFO = 2,
    /// @brief Indicates a potential problem or an unexpected event.
    WARN = 3,
    /// @brief A serious error that prevents a specific operation from completing.
    ERROR = 4,
    /// @brief A critical failure that may affect overall system stability and requires immediate attention.
    CRITICAL = 5
};

/**
 * @brief Represents a structured log entry.
 * @details Contains all the information for a single log event, designed for
 * easy serialization to formats like JSON.
 */
struct LogEntry {
    /// @brief The timestamp when the log event occurred.
    std::chrono::system_clock::time_point timestamp;
    /// @brief The severity level of the log message.
    LogLevel level;
    /// @brief The system module where the log originated (e.g., "network", "consensus").
    std::string module;
    /// @brief The ID of the thread that generated the log.
    std::string thread_id;
    /// @brief The main log message.
    std::string message;
    /// @brief An optional error code associated with the event.
    std::string error_code;
    /// @brief A map of key-value pairs for additional structured context.
    std::unordered_map<std::string, std::string> context;
};

/**
 * @brief Interface for alert channels that handle critical log messages.
 * @details Defines a contract for sending alerts to external systems like
 * Slack, PagerDuty, or email.
 */
class IAlertChannel {
public:
    virtual ~IAlertChannel() = default;
    /**
     * @brief Sends an alert based on a log entry.
     * @param entry The log entry that triggered the alert.
     */
    virtual void send_alert(const LogEntry& entry) = 0;
    /**
     * @brief Checks if the alert channel is currently enabled.
     * @return True if the channel is enabled, false otherwise.
     */
    virtual bool is_enabled() const = 0;
    /**
     * @brief Gets the name of the alert channel.
     * @return A string identifier for the channel (e.g., "slack", "email").
     */
    virtual std::string get_name() const = 0;
};

/**
 * @brief A thread-safe, singleton logger with structured and asynchronous logging capabilities.
 * @details Provides a central point for all logging within the application. It can
 * be configured at runtime and supports multiple output formats and alert channels.
 */
class Logger {
public:
    /**
     * @brief Gets the singleton instance of the Logger.
     * @return A reference to the global Logger instance.
     */
    static Logger& instance() {
        static Logger logger;
        return logger;
    }
    
    /**
     * @brief Sets the minimum log level to be processed.
     * @param level The new minimum log level. Messages below this level will be ignored.
     */
    void set_level(LogLevel level) noexcept {
        current_level_.store(static_cast<int>(level), std::memory_order_relaxed);
    }
    
    /**
     * @brief Enables or disables structured JSON output format.
     * @param enabled If true, logs will be formatted as JSON; otherwise, as plain text.
     */
    void set_json_format(bool enabled) noexcept {
        json_format_.store(enabled, std::memory_order_relaxed);
    }
    
    /**
     * @brief Enables or disables asynchronous logging.
     * @details When enabled, log entries are pushed to a queue and processed by a
     * background worker thread to minimize I/O latency on the calling thread.
     * @param enabled If true, enables async logging; otherwise, logging is synchronous.
     */
    void set_async_logging(bool enabled) noexcept {
        if (enabled && !async_enabled_.load()) {
            start_async_worker();
        } else if (!enabled && async_enabled_.load()) {
            stop_async_worker();
        }
    }
    
    /**
     * @brief Adds an alert channel to the logger.
     * @details The logger will send critical messages to this channel.
     * @param channel A unique pointer to an object implementing the IAlertChannel interface.
     */
    void add_alert_channel(std::unique_ptr<IAlertChannel> channel) {
        std::lock_guard<std::mutex> lock(alert_mutex_);
        alert_channels_.push_back(std::move(channel));
    }
    
    /**
     * @brief Checks if the DEBUG log level is currently enabled.
     * @return True if DEBUG or lower levels are enabled, false otherwise.
     */
    bool is_debug_enabled() const noexcept {
        return current_level_.load(std::memory_order_relaxed) <= static_cast<int>(LogLevel::DEBUG);
    }
    
    /**
     * @brief Checks if a specific log level is currently enabled.
     * @param level The log level to check.
     * @return True if messages at this level will be processed, false otherwise.
     */
    bool is_enabled(LogLevel level) const noexcept {
        return static_cast<int>(level) >= current_level_.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Logs a simple message with a variable number of arguments.
     * @tparam Args The types of the arguments to log.
     * @param level The severity level of the message.
     * @param args The arguments to be concatenated into the log message.
     * @note This version is for backward compatibility. Prefer the version with an explicit module.
     */
    template<typename... Args>
    void log(LogLevel level, Args&&... args) {
        if (!is_enabled(level)) return;
        
        std::ostringstream oss;
        (oss << ... << args);
        
        std::string module = infer_module_from_context();
        
        LogEntry entry{
            std::chrono::system_clock::now(), level, module, get_thread_id(),
            oss.str(), "", {}
        };
        
        process_log_entry(entry);
    }
    
    /**
     * @brief Logs a simple message with an explicit module name.
     * @tparam Args The types of the arguments to log.
     * @param level The severity level of the message.
     * @param module The name of the module generating the log.
     * @param args The arguments to be concatenated into the log message.
     */
    template<typename... Args>
    void log(LogLevel level, const std::string& module, Args&&... args) {
        if (!is_enabled(level)) return;
        
        std::ostringstream oss;
        (oss << ... << args);
        
        LogEntry entry{
            std::chrono::system_clock::now(), level, module, get_thread_id(),
            oss.str(), "", {}
        };
        
        process_log_entry(entry);
    }
    
    /**
     * @brief Logs a structured message with detailed context.
     * @param level The severity level of the message.
     * @param module The name of the module generating the log.
     * @param message The main log message.
     * @param error_code An optional error code.
     * @param context A map of key-value pairs for additional context.
     */
    void log_structured(LogLevel level, const std::string& module, 
                       const std::string& message, const std::string& error_code = "",
                       const std::unordered_map<std::string, std::string>& context = {}) {
        if (!is_enabled(level)) return;
        
        LogEntry entry{
            std::chrono::system_clock::now(), level, module, get_thread_id(),
            message, error_code, context
        };
        
        process_log_entry(entry);
    }
    
    /**
     * @brief Logs a critical failure and triggers all configured alert channels.
     * @param module The name of the module where the failure occurred.
     * @param message A description of the failure.
     * @param error_code An optional error code associated with the failure.
     * @param context A map of key-value pairs for additional context.
     */
    void log_critical_failure(const std::string& module, const std::string& message,
                              const std::string& error_code = "",
                              const std::unordered_map<std::string, std::string>& context = {}) {
        LogEntry entry{
            std::chrono::system_clock::now(), LogLevel::CRITICAL, module,
            get_thread_id(), message, error_code, context
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
    
    static const size_t MAX_QUEUE_SIZE = 10000;
    std::queue<LogEntry> log_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread worker_thread_;
    
    std::vector<std::unique_ptr<IAlertChannel>> alert_channels_;
    std::mutex alert_mutex_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_alert_time_;
    static constexpr std::chrono::seconds ALERT_RATE_LIMIT_INTERVAL{60};
    
    void process_log_entry(const LogEntry& entry);
    void output_log_entry(const LogEntry& entry);
    void trigger_alerts(const LogEntry& entry);
    
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
 * @defgroup logging_macros Logging Macros
 * @brief A set of macros for convenient and performant logging.
 * @details These macros check the log level before evaluating their arguments,
 * which avoids the overhead of string formatting and function calls when the
 * log level is disabled.
 * @{
 */

/**
 * @brief Logs a message at the TRACE level.
 * @param ... Arguments to be passed to the logger. Can be a module name followed by message parts.
 */
#define LOG_TRACE(...) \
    do { \
        if (slonana::common::Logger::instance().is_enabled(slonana::common::LogLevel::TRACE)) { \
            slonana::common::Logger::instance().log(slonana::common::LogLevel::TRACE, __VA_ARGS__); \
        } \
    } while(0)

/**
 * @brief Logs a message at the DEBUG level.
 * @param ... Arguments to be passed to the logger.
 */
#define LOG_DEBUG(...) \
    do { \
        if (slonana::common::Logger::instance().is_debug_enabled()) { \
            slonana::common::Logger::instance().log(slonana::common::LogLevel::DEBUG, __VA_ARGS__); \
        } \
    } while(0)

/**
 * @brief Logs a message at the INFO level.
 * @param ... Arguments to be passed to the logger.
 */
#define LOG_INFO(...) \
    slonana::common::Logger::instance().log(slonana::common::LogLevel::INFO, __VA_ARGS__)

/**
 * @brief Logs a message at the WARN level.
 * @param ... Arguments to be passed to the logger.
 */
#define LOG_WARN(...) \
    slonana::common::Logger::instance().log(slonana::common::LogLevel::WARN, __VA_ARGS__)

/**
 * @brief Logs a message at the ERROR level.
 * @param ... Arguments to be passed to the logger.
 */
#define LOG_ERROR(...) \
    slonana::common::Logger::instance().log(slonana::common::LogLevel::ERROR, __VA_ARGS__)

/**
 * @brief Logs a message at the CRITICAL level.
 * @param ... Arguments to be passed to the logger.
 */
#define LOG_CRITICAL(...) \
    slonana::common::Logger::instance().log(slonana::common::LogLevel::CRITICAL, __VA_ARGS__)

/**
 * @brief Logs a structured message.
 * @param level The LogLevel for the message.
 * @param module The name of the originating module.
 * @param message The main log message.
 * @param ... Optional error code and context map.
 */
#define LOG_STRUCTURED(level, module, message, ...) \
    slonana::common::Logger::instance().log_structured(level, module, message, ##__VA_ARGS__)

/**
 * @brief Logs a critical failure and triggers alerts.
 * @param module The name of the originating module.
 * @param message The main log message.
 * @param ... Optional error code and context map.
 */
#define LOG_CRITICAL_FAILURE(module, message, ...) \
    slonana::common::Logger::instance().log_critical_failure(module, message, ##__VA_ARGS__)

/**
 * @}
 */

/**
 * @defgroup module_macros Module-Specific Logging Macros
 * @brief Convenience macros for logging critical errors from specific system modules.
 * @{
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
/**
 * @}
 */
