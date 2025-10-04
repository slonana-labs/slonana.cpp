/**
 * @file alerting.cpp
 * @brief Implements various alert channels for system notifications.
 *
 * This file provides the concrete implementations for the alert channels
 * defined in `alerting.h`. This includes channels for console output,
 * Slack messages, email notifications, file logging, and Prometheus metrics.
 * It also contains the implementation of the alert channel factory.
 */

#include "common/alerting.h"
#include <fstream>
#include <iostream>
#include <iomanip>

namespace slonana {
namespace common {

// ConsoleAlertChannel implementation
void ConsoleAlertChannel::send_alert(const LogEntry& entry) {
    if (!enabled_) return;
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
    if (!is_enabled()) return;
    // Note: This is a simplified implementation for demonstration.
    // In a production environment, this would use a library like libcurl to send
    // an HTTP POST request to the Slack webhook URL.
    std::cout << "SLACK ALERT (webhook: " << webhook_url_ << "): "
              << format_slack_message(entry) << std::endl;
}

std::string SlackAlertChannel::format_slack_message(const LogEntry& entry) const {
    std::ostringstream msg;
    msg << "{\"text\":\":warning: *CRITICAL FAILURE DETECTED* :warning:\","
        << "\"attachments\":[{\"color\":\"#ff0000\",\"fields\":["
        << "{\"title\":\"Module\",\"value\":\"" << entry.module << "\",\"short\":true},"
        << "{\"title\":\"Message\",\"value\":\"" << entry.message << "\",\"short\":false},";

    if (!entry.error_code.empty()) {
        msg << "{\"title\":\"Error Code\",\"value\":\"" << entry.error_code << "\",\"short\":true},";
    }

    auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
    char time_buf[100];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S UTC", std::gmtime(&time_t));
    msg << "{\"title\":\"Time\",\"value\":\"" << time_buf << "\",\"short\":true}";

    if (!entry.context.empty()) {
        msg << ",{\"title\":\"Context\",\"value\":\"";
        for (const auto& [key, value] : entry.context) {
            msg << "• " << key << ": " << value << "\\n";
        }
        msg << "\",\"short\":false}";
    }

    msg << "]}]}";
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
    if (!is_enabled()) return;
    // Note: This is a simplified implementation for demonstration.
    // In a production environment, this would use a proper SMTP client library
    // to send an email.
    std::cout << "EMAIL ALERT (to: " << to_email_ << "): " << std::endl;
    std::cout << "Subject: " << format_email_subject(entry) << std::endl;
    std::cout << "Body: \n" << format_email_body(entry) << std::endl;
}

std::string EmailAlertChannel::format_email_subject(const LogEntry& entry) const {
    return "[CRITICAL] Slonana Validator Failure in " + entry.module;
}

std::string EmailAlertChannel::format_email_body(const LogEntry& entry) const {
    std::ostringstream body;
    body << "A critical failure has been detected in the Slonana validator.\n\n"
         << "Details:\n"
         << "------------------------------------\n"
         << "Module:    " << entry.module << "\n"
         << "Message:   " << entry.message << "\n";

    auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
    char time_buf[100];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S UTC", std::gmtime(&time_t));
    body << "Time:      " << time_buf << "\n";

    if (!entry.error_code.empty()) {
        body << "Error Code: " << entry.error_code << "\n";
    }

    body << "Thread ID: " << entry.thread_id << "\n";

    if (!entry.context.empty()) {
        body << "\nAdditional Context:\n";
        for (const auto& [key, value] : entry.context) {
            body << "  - " << key << ": " << value << "\n";
        }
    }

    body << "\n------------------------------------\n"
         << "This alert requires immediate attention.\n";
    return body.str();
}

// FileAlertChannel implementation
FileAlertChannel::FileAlertChannel(const std::string& file_path, bool enabled)
    : file_path_(file_path), enabled_(enabled) {}

void FileAlertChannel::send_alert(const LogEntry& entry) {
    if (!is_enabled()) return;
    std::ofstream file(file_path_, std::ios::app);
    if (!file.is_open()) {
        // Fallback to stderr if the file cannot be opened
        std::cerr << "ALERT: Failed to open alert file '" << file_path_
                  << "' - Module: " << entry.module
                  << ", Message: " << entry.message << std::endl;
        return;
    }

    auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
    char time_buf[100];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S UTC", std::gmtime(&time_t));

    file << "[" << time_buf << "] "
         << "CRITICAL ALERT - Module: " << entry.module
         << ", Message: " << entry.message;

    if (!entry.error_code.empty()) {
        file << ", Error: " << entry.error_code;
    }

    file << std::endl;

    if (file.fail()) {
        std::cerr << "ALERT: Failed to write to alert file '" << file_path_
                  << "' - Module: " << entry.module
                  << ", Message: " << entry.message << std::endl;
    }
}

// PrometheusAlertChannel implementation
PrometheusAlertChannel::PrometheusAlertChannel(bool enabled) : enabled_(enabled) {}

void PrometheusAlertChannel::send_alert(const LogEntry& entry) {
    if (!enabled_) return;
    // In a real implementation, this would use a Prometheus client library
    // to increment a metric. This is a placeholder for demonstration.
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