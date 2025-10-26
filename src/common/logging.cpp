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
  case LogLevel::TRACE:
    return "TRACE";
  case LogLevel::DEBUG:
    return "DEBUG";
  case LogLevel::INFO:
    return "INFO";
  case LogLevel::WARN:
    return "WARN";
  case LogLevel::ERROR:
    return "ERROR";
  case LogLevel::CRITICAL:
    return "CRITICAL";
  default:
    return "UNKNOWN";
  }
}

std::string Logger::escape_json_string(const std::string &input) const {
  std::ostringstream escaped;
  for (char c : input) {
    switch (c) {
    case '"':
      escaped << "\\\"";
      break;
    case '\\':
      escaped << "\\\\";
      break;
    case '\b':
      escaped << "\\b";
      break;
    case '\f':
      escaped << "\\f";
      break;
    case '\n':
      escaped << "\\n";
      break;
    case '\r':
      escaped << "\\r";
      break;
    case '\t':
      escaped << "\\t";
      break;
    default:
      if (c >= 0 && c < 32) {
        // Control characters - escape as unicode
        escaped << "\\u" << std::hex << std::setfill('0') << std::setw(4)
                << static_cast<int>(c);
      } else {
        escaped << c;
      }
      break;
    }
  }
  return escaped.str();
}

std::string Logger::infer_module_from_context() const {
  // Simple heuristic: try to infer module from thread name or other context
  // For now, return "inferred" - could be enhanced with stack trace analysis
  return "inferred";
}

std::string Logger::format_json(const LogEntry &entry) const {
  std::ostringstream json;

  // Convert timestamp to ISO 8601 format
  auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                entry.timestamp.time_since_epoch()) %
            1000;

  json << "{" << "\"timestamp\":\""
       << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S") << "."
       << std::setfill('0') << std::setw(3) << ms.count() << "Z\","
       << "\"level\":\"" << level_to_string(entry.level) << "\","
       << "\"module\":\"" << escape_json_string(entry.module) << "\","
       << "\"thread_id\":\"" << escape_json_string(entry.thread_id) << "\","
       << "\"message\":\"" << escape_json_string(entry.message) << "\"";

  if (!entry.error_code.empty()) {
    json << ",\"error_code\":\"" << escape_json_string(entry.error_code)
         << "\"";
  }

  if (!entry.context.empty()) {
    json << ",\"context\":{";
    bool first = true;
    for (const auto &[key, value] : entry.context) {
      if (!first)
        json << ",";
      json << "\"" << escape_json_string(key) << "\":\""
           << escape_json_string(value) << "\"";
      first = false;
    }
    json << "}";
  }

  json << "}";
  return json.str();
}

std::string Logger::format_text(const LogEntry &entry) const {
  std::ostringstream text;

  auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);

  text << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
       << "] " << "[" << level_to_string(entry.level) << "] " << "["
       << entry.module << "] " << entry.message;

  if (!entry.error_code.empty()) {
    text << " (error: " << entry.error_code << ")";
  }

  if (!entry.context.empty()) {
    text << " {";
    bool first = true;
    for (const auto &[key, value] : entry.context) {
      if (!first)
        text << ", ";
      text << key << "=" << value;
      first = false;
    }
    text << "}";
  }

  return text.str();
}

void Logger::start_async_worker() {
  if (async_enabled_.load())
    return;

  worker_shutdown_.store(false);
  async_enabled_.store(true);
  worker_thread_ = std::thread(&Logger::worker_loop, this);
}

void Logger::stop_async_worker() {
  if (!async_enabled_.load())
    return;

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