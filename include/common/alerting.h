/**
 * @file alerting.h
 * @brief Defines interfaces and implementations for various alert channels.
 *
 * This file contains the definitions for different alert channels such as console,
 * Slack, email, file, and Prometheus. It also includes a factory for creating
 * instances of these channels. These channels are used to send notifications
 * for important system events.
 */

#pragma once

#include "common/logging.h"
#include <memory>
#include <string>

namespace slonana {
namespace common {

/**
 * @brief An alert channel that prints messages to the console.
 * @details This channel is primarily used for development and testing purposes,
 * allowing developers to see alerts directly in their terminal output.
 */
class ConsoleAlertChannel : public IAlertChannel {
public:
    /**
     * @brief Constructs a new ConsoleAlertChannel.
     * @param enabled Whether the channel is initially enabled.
     */
    ConsoleAlertChannel(bool enabled = true) : enabled_(enabled) {}

    /**
     * @brief Sends an alert to the console.
     * @param entry The log entry containing the alert information.
     */
    void send_alert(const LogEntry& entry) override;

    /**
     * @brief Checks if the alert channel is enabled.
     * @return True if enabled, false otherwise.
     */
    bool is_enabled() const override { return enabled_; }

    /**
     * @brief Gets the name of the alert channel.
     * @return The string "console".
     */
    std::string get_name() const override { return "console"; }

    /**
     * @brief Enables or disables the alert channel.
     * @param enabled The new enabled state.
     */
    void set_enabled(bool enabled) { enabled_ = enabled; }

private:
    bool enabled_;
};

/**
 * @brief An alert channel that sends messages to a Slack webhook.
 * @details This channel formats alerts as Slack messages and sends them to a
 * pre-configured webhook URL, allowing for real-time notifications in a Slack
 * channel.
 */
class SlackAlertChannel : public IAlertChannel {
public:
    /**
     * @brief Constructs a new SlackAlertChannel.
     * @param webhook_url The URL of the Slack webhook.
     * @param enabled Whether the channel is initially enabled.
     */
    SlackAlertChannel(const std::string& webhook_url, bool enabled = true);

    /**
     * @brief Sends an alert to the configured Slack webhook.
     * @param entry The log entry containing the alert information.
     */
    void send_alert(const LogEntry& entry) override;

    /**
     * @brief Checks if the alert channel is enabled.
     * @return True if enabled and a webhook URL is configured, false otherwise.
     */
    bool is_enabled() const override { return enabled_ && !webhook_url_.empty(); }

    /**
     * @brief Gets the name of the alert channel.
     * @return The string "slack".
     */
    std::string get_name() const override { return "slack"; }

    /**
     * @brief Enables or disables the alert channel.
     * @param enabled The new enabled state.
     */
    void set_enabled(bool enabled) { enabled_ = enabled; }

private:
    std::string webhook_url_;
    bool enabled_;

    /**
     * @brief Formats a log entry into a Slack message payload.
     * @param entry The log entry to format.
     * @return A JSON string representing the Slack message.
     */
    std::string format_slack_message(const LogEntry& entry) const;
};

/**
 * @brief An alert channel that sends messages via email using SMTP.
 * @details This channel connects to an SMTP server to send alert emails to a
 * specified recipient. It supports authentication.
 */
class EmailAlertChannel : public IAlertChannel {
public:
    /**
     * @brief Constructs a new EmailAlertChannel.
     * @param smtp_server The address of the SMTP server.
     * @param port The port of the SMTP server.
     * @param username The username for SMTP authentication.
     * @param password The password for SMTP authentication.
     * @param from_email The sender's email address.
     * @param to_email The recipient's email address.
     * @param enabled Whether the channel is initially enabled.
     */
    EmailAlertChannel(const std::string& smtp_server, int port,
                     const std::string& username, const std::string& password,
                     const std::string& from_email, const std::string& to_email,
                     bool enabled = true);

    /**
     * @brief Sends an alert via email.
     * @param entry The log entry containing the alert information.
     */
    void send_alert(const LogEntry& entry) override;

    /**
     * @brief Checks if the alert channel is enabled.
     * @return True if enabled and a recipient email is set, false otherwise.
     */
    bool is_enabled() const override { return enabled_ && !to_email_.empty(); }

    /**
     * @brief Gets the name of the alert channel.
     * @return The string "email".
     */
    std::string get_name() const override { return "email"; }

    /**
     * @brief Enables or disables the alert channel.
     * @param enabled The new enabled state.
     */
    void set_enabled(bool enabled) { enabled_ = enabled; }

private:
    std::string smtp_server_;
    int port_;
    std::string username_;
    std::string password_;
    std::string from_email_;
    std::string to_email_;
    bool enabled_;

    /**
     * @brief Formats the subject line for an alert email.
     * @param entry The log entry to format.
     * @return A string for the email subject.
     */
    std::string format_email_subject(const LogEntry& entry) const;

    /**
     * @brief Formats the body for an alert email.
     * @param entry The log entry to format.
     * @return A string for the email body.
     */
    std::string format_email_body(const LogEntry& entry) const;
};

/**
 * @brief An alert channel that writes messages to a file.
 * @details This channel provides persistent alerting by appending alert
 * messages to a specified log file.
 */
class FileAlertChannel : public IAlertChannel {
public:
    /**
     * @brief Constructs a new FileAlertChannel.
     * @param file_path The path to the file where alerts will be written.
     * @param enabled Whether the channel is initially enabled.
     */
    FileAlertChannel(const std::string& file_path, bool enabled = true);

    /**
     * @brief Appends an alert to the configured file.
     * @param entry The log entry containing the alert information.
     */
    void send_alert(const LogEntry& entry) override;

    /**
     * @brief Checks if the alert channel is enabled.
     * @return True if enabled and a file path is set, false otherwise.
     */
    bool is_enabled() const override { return enabled_ && !file_path_.empty(); }

    /**
     * @brief Gets the name of the alert channel.
     * @return The string "file".
     */
    std::string get_name() const override { return "file"; }

    /**
     * @brief Enables or disables the alert channel.
     * @param enabled The new enabled state.
     */
    void set_enabled(bool enabled) { enabled_ = enabled; }

private:
    std::string file_path_;
    bool enabled_;
};

/**
 * @brief An alert channel that integrates with Prometheus.
 * @details This channel increments Prometheus counters for alerts, allowing
 * monitoring systems to track the number and type of alerts that occur.
 */
class PrometheusAlertChannel : public IAlertChannel {
public:
    /**
     * @brief Constructs a new PrometheusAlertChannel.
     * @param enabled Whether the channel is initially enabled.
     */
    PrometheusAlertChannel(bool enabled = true);

    /**
     * @brief Sends an alert to Prometheus by incrementing a counter.
     * @param entry The log entry containing the alert information.
     */
    void send_alert(const LogEntry& entry) override;

    /**
     * @brief Checks if the alert channel is enabled.
     * @return True if enabled, false otherwise.
     */
    bool is_enabled() const override { return enabled_; }

    /**
     * @brief Gets the name of the alert channel.
     * @return The string "prometheus".
     */
    std::string get_name() const override { return "prometheus"; }

    /**
     * @brief Enables or disables the alert channel.
     * @param enabled The new enabled state.
     */
    void set_enabled(bool enabled) { enabled_ = enabled; }

private:
    bool enabled_;
};

/**
 * @brief A factory for creating instances of alert channels.
 * @details This class provides a set of static methods to easily construct
 * various types of alert channels, simplifying configuration and setup.
 */
class AlertChannelFactory {
public:
    /**
     * @brief Creates a console alert channel.
     * @param enabled Whether the channel should be enabled.
     * @return A unique pointer to a new ConsoleAlertChannel.
     */
    static std::unique_ptr<IAlertChannel> create_console_channel(bool enabled = true);

    /**
     * @brief Creates a Slack alert channel.
     * @param webhook_url The URL for the Slack webhook.
     * @param enabled Whether the channel should be enabled.
     * @return A unique pointer to a new SlackAlertChannel.
     */
    static std::unique_ptr<IAlertChannel> create_slack_channel(const std::string& webhook_url, bool enabled = true);

    /**
     * @brief Creates an email alert channel.
     * @param smtp_server The address of the SMTP server.
     * @param port The port of the SMTP server.
     * @param username The username for SMTP authentication.
     * @param password The password for SMTP authentication.
     * @param from_email The sender's email address.
     * @param to_email The recipient's email address.
     * @param enabled Whether the channel should be enabled.
     * @return A unique pointer to a new EmailAlertChannel.
     */
    static std::unique_ptr<IAlertChannel> create_email_channel(
        const std::string& smtp_server, int port,
        const std::string& username, const std::string& password,
        const std::string& from_email, const std::string& to_email,
        bool enabled = true
    );

    /**
     * @brief Creates a file-based alert channel.
     * @param file_path The path to the alert log file.
     * @param enabled Whether the channel should be enabled.
     * @return A unique pointer to a new FileAlertChannel.
     */
    static std::unique_ptr<IAlertChannel> create_file_channel(const std::string& file_path, bool enabled = true);

    /**
     * @brief Creates a Prometheus alert channel.
     * @param enabled Whether the channel should be enabled.
     * @return A unique pointer to a new PrometheusAlertChannel.
     */
    static std::unique_ptr<IAlertChannel> create_prometheus_channel(bool enabled = true);
};

} // namespace common
} // namespace slonana