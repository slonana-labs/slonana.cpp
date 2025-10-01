#include "common/alerting.h"
#include <fstream>
#include <iostream>
#include <iomanip>

namespace slonana {
namespace common {

// ConsoleAlertChannel implementation
void ConsoleAlertChannel::send_alert(const LogEntry& entry) {
    std::cerr << "🚨 CRITICAL ALERT 🚨" << std::endl;
    std::cerr << "Module: " << entry.module << std::endl;
    std::cerr << "Message: " << entry.message << std::endl;
    std::cerr << "Time: ";
    
    auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
    std::cerr << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << std::endl;
    
    if (!entry.error_code.empty()) {
        std::cerr << "Error Code: " << entry.error_code << std::endl;
    }
    
    if (!entry.context.empty()) {
        std::cerr << "Context:" << std::endl;
        for (const auto& [key, value] : entry.context) {
            std::cerr << "  " << key << ": " << value << std::endl;
        }
    }
    std::cerr << "========================" << std::endl;
}

// SlackAlertChannel implementation
SlackAlertChannel::SlackAlertChannel(const std::string& webhook_url, bool enabled)
    : webhook_url_(webhook_url), enabled_(enabled) {}

void SlackAlertChannel::send_alert(const LogEntry& entry) {
    // Note: This is a simplified implementation
    // In production, you'd use libcurl or similar HTTP client library
    // For now, we'll just log the alert that would be sent
    std::cout << "SLACK ALERT (webhook: " << webhook_url_ << "): " 
              << format_slack_message(entry) << std::endl;
}

std::string SlackAlertChannel::format_slack_message(const LogEntry& entry) const {
    std::ostringstream msg;
    msg << ":warning: *CRITICAL FAILURE DETECTED* :warning:\n"
        << "*Module:* " << entry.module << "\n"
        << "*Message:* " << entry.message << "\n";
    
    if (!entry.error_code.empty()) {
        msg << "*Error Code:* " << entry.error_code << "\n";
    }
    
    auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
    msg << "*Time:* " << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S UTC") << "\n";
    
    if (!entry.context.empty()) {
        msg << "*Context:*\n";
        for (const auto& [key, value] : entry.context) {
            msg << "• " << key << ": " << value << "\n";
        }
    }
    
    return msg.str();
}

// EmailAlertChannel implementation
EmailAlertChannel::EmailAlertChannel(const std::string& smtp_server, int port,
                                   const std::string& username, const std::string& password,
                                   const std::string& from_email, const std::string& to_email,
                                   bool enabled)
    : smtp_server_(smtp_server), port_(port), username_(username), password_(password),
      from_email_(from_email), to_email_(to_email), enabled_(enabled) {}

void EmailAlertChannel::send_alert(const LogEntry& entry) {
    // Note: This is a simplified implementation
    // In production, you'd use a proper SMTP library
    std::cout << "EMAIL ALERT (to: " << to_email_ << "): " << std::endl;
    std::cout << "Subject: " << format_email_subject(entry) << std::endl;
    std::cout << "Body: " << format_email_body(entry) << std::endl;
}

std::string EmailAlertChannel::format_email_subject(const LogEntry& entry) const {
    return "[CRITICAL] Slonana Validator Failure in " + entry.module;
}

std::string EmailAlertChannel::format_email_body(const LogEntry& entry) const {
    std::ostringstream body;
    body << "A critical failure has been detected in the Slonana validator.\n\n"
         << "Details:\n"
         << "Module: " << entry.module << "\n"
         << "Message: " << entry.message << "\n";
    
    auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
    body << "Time: " << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S UTC") << "\n";
    
    if (!entry.error_code.empty()) {
        body << "Error Code: " << entry.error_code << "\n";
    }
    
    body << "Thread: " << entry.thread_id << "\n";
    
    if (!entry.context.empty()) {
        body << "\nAdditional Context:\n";
        for (const auto& [key, value] : entry.context) {
            body << key << ": " << value << "\n";
        }
    }
    
    body << "\nThis alert requires immediate attention.\n";
    return body.str();
}

// FileAlertChannel implementation
FileAlertChannel::FileAlertChannel(const std::string& file_path, bool enabled)
    : file_path_(file_path), enabled_(enabled) {}

void FileAlertChannel::send_alert(const LogEntry& entry) {
    std::ofstream file(file_path_, std::ios::app);
    if (!file.is_open()) {
        // Fallback to stderr if file cannot be opened
        std::cerr << "ALERT: Failed to open alert file '" << file_path_ 
                  << "' - Module: " << entry.module 
                  << ", Message: " << entry.message << std::endl;
        return;
    }
    
    auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
    
    file << "[" << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S UTC") << "] "
         << "CRITICAL ALERT - Module: " << entry.module 
         << ", Message: " << entry.message;
    
    if (!entry.error_code.empty()) {
        file << ", Error: " << entry.error_code;
    }
    
    file << std::endl;
    
    // Check if write was successful
    if (file.fail()) {
        std::cerr << "ALERT: Failed to write to alert file '" << file_path_ 
                  << "' - Module: " << entry.module 
                  << ", Message: " << entry.message << std::endl;
    }
}

// PrometheusAlertChannel implementation
PrometheusAlertChannel::PrometheusAlertChannel(bool enabled) : enabled_(enabled) {}

void PrometheusAlertChannel::send_alert(const LogEntry& entry) {
    // In a real implementation, this would increment Prometheus metrics
    // that can trigger alerts in Alertmanager
    std::cout << "PROMETHEUS METRIC: critical_failures_total{module=\"" 
              << entry.module << "\"} +1" << std::endl;
}

// AlertChannelFactory implementation
std::unique_ptr<IAlertChannel> AlertChannelFactory::create_console_channel(bool enabled) {
    return std::make_unique<ConsoleAlertChannel>(enabled);
}

std::unique_ptr<IAlertChannel> AlertChannelFactory::create_slack_channel(
    const std::string& webhook_url, bool enabled) {
    return std::make_unique<SlackAlertChannel>(webhook_url, enabled);
}

std::unique_ptr<IAlertChannel> AlertChannelFactory::create_email_channel(
    const std::string& smtp_server, int port,
    const std::string& username, const std::string& password,
    const std::string& from_email, const std::string& to_email,
    bool enabled) {
    return std::make_unique<EmailAlertChannel>(
        smtp_server, port, username, password, from_email, to_email, enabled
    );
}

std::unique_ptr<IAlertChannel> AlertChannelFactory::create_file_channel(
    const std::string& file_path, bool enabled) {
    return std::make_unique<FileAlertChannel>(file_path, enabled);
}

std::unique_ptr<IAlertChannel> AlertChannelFactory::create_prometheus_channel(bool enabled) {
    return std::make_unique<PrometheusAlertChannel>(enabled);
}

} // namespace common
} // namespace slonana