#pragma once

#include "common/logging.h"
#include <memory>
#include <string>

namespace slonana {
namespace common {

/**
 * @brief Console alert channel for development/testing
 */
class ConsoleAlertChannel : public IAlertChannel {
public:
    ConsoleAlertChannel(bool enabled = true) : enabled_(enabled) {}
    
    void send_alert(const LogEntry& entry) override;
    bool is_enabled() const override { return enabled_; }
    std::string get_name() const override { return "console"; }
    
    void set_enabled(bool enabled) { enabled_ = enabled; }

private:
    bool enabled_;
};

/**
 * @brief Slack webhook alert channel
 */
class SlackAlertChannel : public IAlertChannel {
public:
    SlackAlertChannel(const std::string& webhook_url, bool enabled = true);
    
    void send_alert(const LogEntry& entry) override;
    bool is_enabled() const override { return enabled_ && !webhook_url_.empty(); }
    std::string get_name() const override { return "slack"; }
    
    void set_enabled(bool enabled) { enabled_ = enabled; }

private:
    std::string webhook_url_;
    bool enabled_;
    
    std::string format_slack_message(const LogEntry& entry) const;
};

/**
 * @brief Email alert channel (SMTP)
 */
class EmailAlertChannel : public IAlertChannel {
public:
    EmailAlertChannel(const std::string& smtp_server, int port,
                     const std::string& username, const std::string& password,
                     const std::string& from_email, const std::string& to_email,
                     bool enabled = true);
    
    void send_alert(const LogEntry& entry) override;
    bool is_enabled() const override { return enabled_ && !to_email_.empty(); }
    std::string get_name() const override { return "email"; }
    
    void set_enabled(bool enabled) { enabled_ = enabled; }

private:
    std::string smtp_server_;
    int port_;
    std::string username_;
    std::string password_;
    std::string from_email_;
    std::string to_email_;
    bool enabled_;
    
    std::string format_email_subject(const LogEntry& entry) const;
    std::string format_email_body(const LogEntry& entry) const;
};

/**
 * @brief File-based alert channel for persistent alerting
 */
class FileAlertChannel : public IAlertChannel {
public:
    FileAlertChannel(const std::string& file_path, bool enabled = true);
    
    void send_alert(const LogEntry& entry) override;
    bool is_enabled() const override { return enabled_ && !file_path_.empty(); }
    std::string get_name() const override { return "file"; }
    
    void set_enabled(bool enabled) { enabled_ = enabled; }

private:
    std::string file_path_;
    bool enabled_;
};

/**
 * @brief Prometheus metrics alert channel
 * Increments alert counters for monitoring systems
 */
class PrometheusAlertChannel : public IAlertChannel {
public:
    PrometheusAlertChannel(bool enabled = true);
    
    void send_alert(const LogEntry& entry) override;
    bool is_enabled() const override { return enabled_; }
    std::string get_name() const override { return "prometheus"; }
    
    void set_enabled(bool enabled) { enabled_ = enabled; }

private:
    bool enabled_;
};

/**
 * @brief Alert channel factory for easy configuration
 */
class AlertChannelFactory {
public:
    static std::unique_ptr<IAlertChannel> create_console_channel(bool enabled = true);
    static std::unique_ptr<IAlertChannel> create_slack_channel(const std::string& webhook_url, bool enabled = true);
    static std::unique_ptr<IAlertChannel> create_email_channel(
        const std::string& smtp_server, int port,
        const std::string& username, const std::string& password,
        const std::string& from_email, const std::string& to_email,
        bool enabled = true
    );
    static std::unique_ptr<IAlertChannel> create_file_channel(const std::string& file_path, bool enabled = true);
    static std::unique_ptr<IAlertChannel> create_prometheus_channel(bool enabled = true);
};

} // namespace common
} // namespace slonana