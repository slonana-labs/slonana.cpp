#pragma once

#include <iostream>
#include <sstream>
#include <string>

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
    ERROR = 4
};

/**
 * @brief Global logging configuration
 * 
 * Thread-safe logging configuration that can be adjusted at runtime.
 * Performance-critical paths should check enabled status before formatting strings.
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
        current_level_ = level;
    }
    
    /// Check if debug logging is enabled
    bool is_debug_enabled() const noexcept {
        return current_level_ <= LogLevel::DEBUG;
    }
    
    /// Check if a specific level is enabled
    bool is_enabled(LogLevel level) const noexcept {
        return level >= current_level_;
    }
    
    /// Log a message if the level is enabled
    template<typename... Args>
    void log(LogLevel level, Args&&... args) {
        if (is_enabled(level)) {
            std::ostringstream oss;
            (oss << ... << args);
            std::cout << oss.str() << std::endl;
        }
    }

private:
    Logger() = default;
    LogLevel current_level_ = LogLevel::INFO;
};

} // namespace common
} // namespace slonana

/**
 * @brief Performance-conscious logging macros
 * 
 * These macros avoid string formatting overhead when logging is disabled.
 * Use LOG_DEBUG for performance-critical paths that should be silent in release builds.
 */
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
