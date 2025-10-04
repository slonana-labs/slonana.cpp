/**
 * @file logging.cpp
 * @brief Implements the backend for the Slonana logging framework.
 *
 * This file contains the implementation of the Logger class methods, including
 * log entry formatting, asynchronous worker management, and helper utilities.
 */
#include "common/logging.h"
#include <iomanip>
#include <sstream>

namespace slonana {
namespace common {

/**
 * @brief Gets the current thread's ID as a string.
 * @return A string representation of the thread ID.
 */
std::string Logger::get_thread_id() const {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

/**
 * @brief Converts a LogLevel enum to its string representation.
 * @param level The log level to convert.
 * @return The string name of the log level (e.g., "INFO", "ERROR").
 */
std::string Logger::level_to_string(LogLevel level) const {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Escapes special characters in a string for JSON compatibility.
 * @param input The string to escape.
 * @return A new string with characters like quotes, backslashes, and control
 * characters properly escaped.
 */
std::string Logger::escape_json_string(const std::string& input) const {
    std::ostringstream escaped;
    for (char c : input) {
        switch (c) {
            case '"':  escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:
                if (c >= 0 && c < 32) {
                    escaped << "\\u" << std::hex << std::setfill('0') << std::setw(4) << static_cast<int>(c);
                } else {
                    escaped << c;
                }
                break;
        }
    }
    return escaped.str();
}

/**
 * @brief Infers the module name from the execution context.
 * @details This is a placeholder for more advanced context inference, such as
 * stack trace analysis. Currently returns a default value.
 * @return The inferred module name, currently "inferred".
 */
std::string Logger::infer_module_from_context() const {
    return "inferred";
}

/**
 * @brief Formats a LogEntry into a JSON string.
 * @param entry The log entry to format.
 * @return A single-line JSON string representing the log entry.
 */
std::string Logger::format_json(const LogEntry& entry) const {
    std::ostringstream json;
    
    auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        entry.timestamp.time_since_epoch()
    ) % 1000;
    
    json << "{"
         << "\"timestamp\":\"" << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S")
         << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z\","
         << "\"level\":\"" << level_to_string(entry.level) << "\","
         << "\"module\":\"" << escape_json_string(entry.module) << "\","
         << "\"thread_id\":\"" << escape_json_string(entry.thread_id) << "\","
         << "\"message\":\"" << escape_json_string(entry.message) << "\"";
    
    if (!entry.error_code.empty()) {
        json << ",\"error_code\":\"" << escape_json_string(entry.error_code) << "\"";
    }
    
    if (!entry.context.empty()) {
        json << ",\"context\":{";
        bool first = true;
        for (const auto& [key, value] : entry.context) {
            if (!first) json << ",";
            json << "\"" << escape_json_string(key) << "\":\"" << escape_json_string(value) << "\"";
            first = false;
        }
        json << "}";
    }
    
    json << "}";
    return json.str();
}

/**
 * @brief Formats a LogEntry into a human-readable plain text string.
 * @param entry The log entry to format.
 * @return A single-line text string representing the log entry.
 */
std::string Logger::format_text(const LogEntry& entry) const {
    std::ostringstream text;
    
    auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
    
    text << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << "] "
         << "[" << level_to_string(entry.level) << "] "
         << "[" << entry.module << "] "
         << entry.message;
    
    if (!entry.error_code.empty()) {
        text << " (error: " << entry.error_code << ")";
    }
    
    if (!entry.context.empty()) {
        text << " {";
        bool first = true;
        for (const auto& [key, value] : entry.context) {
            if (!first) text << ", ";
            text << key << "=" << value;
            first = false;
        }
        text << "}";
    }
    
    return text.str();
}

/**
 * @brief Starts the asynchronous logging worker thread.
 * @details If async logging is already enabled, this function does nothing.
 * Otherwise, it launches a new thread to process the log queue.
 */
void Logger::start_async_worker() {
    if (async_enabled_.load()) return;
    
    worker_shutdown_.store(false);
    async_enabled_.store(true);
    worker_thread_ = std::thread(&Logger::worker_loop, this);
}

/**
 * @brief Stops the asynchronous logging worker thread.
 * @details Signals the worker thread to shut down, waits for it to join, and
 * then processes any remaining entries in the log queue synchronously.
 */
void Logger::stop_async_worker() {
    if (!async_enabled_.load()) return;
    
    worker_shutdown_.store(true);
    queue_cv_.notify_all();
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    async_enabled_.store(false);
    
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!log_queue_.empty()) {
        output_log_entry(log_queue_.front());
        log_queue_.pop();
    }
}

/**
 * @brief The main loop for the asynchronous worker thread.
 * @details Waits for log entries to appear in the queue and processes them
 * until a shutdown is signaled.
 */
void Logger::worker_loop() {
    while (!worker_shutdown_.load()) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        queue_cv_.wait(lock, [this] {
            return !log_queue_.empty() || worker_shutdown_.load();
        });
        
        while (!log_queue_.empty()) {
            LogEntry entry = std::move(log_queue_.front());
            log_queue_.pop();
            lock.unlock();
            
            output_log_entry(entry);
            
            lock.lock();
        }
    }
}

} // namespace common
} // namespace slonana