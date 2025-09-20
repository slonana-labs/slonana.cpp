#include "common/logging.h"
#include <iomanip>
#include <sstream>

namespace slonana {
namespace common {

std::string Logger::get_thread_id() const {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

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

std::string Logger::format_json(const LogEntry& entry) const {
    std::ostringstream json;
    
    // Convert timestamp to ISO 8601 format
    auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        entry.timestamp.time_since_epoch()
    ) % 1000;
    
    json << "{"
         << "\"timestamp\":\"" << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S")
         << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z\","
         << "\"level\":\"" << level_to_string(entry.level) << "\","
         << "\"module\":\"" << entry.module << "\","
         << "\"thread_id\":\"" << entry.thread_id << "\","
         << "\"message\":\"" << entry.message << "\"";
    
    if (!entry.error_code.empty()) {
        json << ",\"error_code\":\"" << entry.error_code << "\"";
    }
    
    if (!entry.context.empty()) {
        json << ",\"context\":{";
        bool first = true;
        for (const auto& [key, value] : entry.context) {
            if (!first) json << ",";
            json << "\"" << key << "\":\"" << value << "\"";
            first = false;
        }
        json << "}";
    }
    
    json << "}";
    return json.str();
}

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

void Logger::start_async_worker() {
    if (async_enabled_.load()) return;
    
    worker_shutdown_.store(false);
    async_enabled_.store(true);
    worker_thread_ = std::thread(&Logger::worker_loop, this);
}

void Logger::stop_async_worker() {
    if (!async_enabled_.load()) return;
    
    worker_shutdown_.store(true);
    queue_cv_.notify_all();
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    async_enabled_.store(false);
    
    // Process remaining entries in queue
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!log_queue_.empty()) {
        output_log_entry(log_queue_.front());
        log_queue_.pop();
    }
}

void Logger::worker_loop() {
    while (!worker_shutdown_.load()) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        queue_cv_.wait(lock, [this] {
            return !log_queue_.empty() || worker_shutdown_.load();
        });
        
        while (!log_queue_.empty()) {
            auto entry = log_queue_.front();
            log_queue_.pop();
            lock.unlock();
            
            output_log_entry(entry);
            
            lock.lock();
        }
    }
}

} // namespace common
} // namespace slonana