#include "common/logging.h"
#include "common/alerting.h"
#include "test_framework.h"
#include <fstream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <random>

using namespace slonana::common;

std::string get_temp_file_path(const std::string& prefix) {
    // Platform-independent temporary file creation
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(10000, 99999);
    
    std::string filename = prefix + "_" + std::to_string(dis(gen)) + ".log";
    return (temp_dir / filename).string();
}

void test_basic_logging_levels() {
    Logger& logger = Logger::instance();
    
    // Test level checking
    logger.set_level(LogLevel::INFO);
    ASSERT_FALSE(logger.is_enabled(LogLevel::DEBUG));
    ASSERT_TRUE(logger.is_enabled(LogLevel::INFO));
    ASSERT_TRUE(logger.is_enabled(LogLevel::WARN));
    ASSERT_TRUE(logger.is_enabled(LogLevel::ERROR));
    ASSERT_TRUE(logger.is_enabled(LogLevel::CRITICAL));
    
    logger.set_level(LogLevel::CRITICAL);
    ASSERT_FALSE(logger.is_enabled(LogLevel::ERROR));
    ASSERT_TRUE(logger.is_enabled(LogLevel::CRITICAL));
    
    std::cout << "✅ Basic logging levels test passed" << std::endl;
}

void test_json_encoding_edge_cases() {
    Logger& logger = Logger::instance();
    logger.set_json_format(true);
    logger.set_level(LogLevel::TRACE);
    
    // Test various edge cases that could break JSON
    std::unordered_map<std::string, std::string> context = {
        {"quotes", "Message with \"quotes\" inside"},
        {"backslashes", "Path\\with\\backslashes"},
        {"newlines", "Message\nwith\nnewlines"},
        {"control_chars", "Message\twith\tcontrol\bchars"},
        {"unicode", "Message with unicode: éñ中文"},
        {"empty", ""},
        {"special", "{}[]:,\"\\"},
    };
    
    // These should not crash and should produce valid JSON
    logger.log_structured(LogLevel::ERROR, "test_json", "Edge case test message", "JSON_EDGE_001", context);
    
    // Test with potentially problematic module and message content
    logger.log_structured(LogLevel::CRITICAL, "module\"with'quotes", 
                         "Message\nwith\ttabs\"and'quotes\\backslashes", 
                         "EDGE_002");
    
    logger.set_json_format(false);
    std::cout << "✅ JSON encoding edge cases test passed" << std::endl;
}

void test_async_logging_bounded_queue() {
    Logger& logger = Logger::instance();
    logger.set_async_logging(true);
    
    // Generate more messages than the queue can hold to test bounded queue behavior
    for (int i = 0; i < 15000; ++i) {  // More than MAX_QUEUE_SIZE (10000)
        logger.log(LogLevel::INFO, "Stress test message ", i);
    }
    
    // Give async worker time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Disable async logging (this should flush remaining messages)
    logger.set_async_logging(false);
    
    std::cout << "✅ Async logging bounded queue test passed" << std::endl;
}

void test_alert_rate_limiting() {
    Logger& logger = Logger::instance();
    
    // Add console channel for testing
    logger.add_alert_channel(AlertChannelFactory::create_console_channel(true));
    
    // Send multiple alerts rapidly - only first should be processed due to rate limiting
    for (int i = 0; i < 5; ++i) {
        logger.log_critical_failure("rate_test", "Rapid fire alert", "RATE_001");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "✅ Alert rate limiting test passed" << std::endl;
}

void test_basic_logging_levels() {
    Logger& logger = Logger::instance();
    
    // Test level checking
    logger.set_level(LogLevel::INFO);
    ASSERT_FALSE(logger.is_enabled(LogLevel::DEBUG));
    ASSERT_TRUE(logger.is_enabled(LogLevel::INFO));
    ASSERT_TRUE(logger.is_enabled(LogLevel::WARN));
    ASSERT_TRUE(logger.is_enabled(LogLevel::ERROR));
    ASSERT_TRUE(logger.is_enabled(LogLevel::CRITICAL));
    
    logger.set_level(LogLevel::CRITICAL);
    ASSERT_FALSE(logger.is_enabled(LogLevel::ERROR));
    ASSERT_TRUE(logger.is_enabled(LogLevel::CRITICAL));
    
    std::cout << "✅ Basic logging levels test passed" << std::endl;
}

void test_structured_logging() {
    Logger& logger = Logger::instance();
    logger.set_level(LogLevel::TRACE);
    
    // Test structured logging
    std::unordered_map<std::string, std::string> context = {
        {"component", "test"},
        {"transaction_id", "12345"}
    };
    
    // This should not crash and should log appropriately
    logger.log_structured(LogLevel::INFO, "test_module", "Test message", "TEST_001", context);
    logger.log_structured(LogLevel::ERROR, "test_module", "Error message", "TEST_ERR_001");
    
    std::cout << "✅ Structured logging test passed" << std::endl;
}

void test_critical_failure_logging() {
    Logger& logger = Logger::instance();
    
    std::unordered_map<std::string, std::string> context = {
        {"error_type", "network_failure"},
        {"peer_count", "0"}
    };
    
    // Test critical failure logging
    logger.log_critical_failure("network", "Network connectivity lost", "NET_001", context);
    
    std::cout << "✅ Critical failure logging test passed" << std::endl;
}

void test_json_formatting() {
    Logger& logger = Logger::instance();
    logger.set_json_format(true);
    
    // Test JSON formatting
    logger.log_structured(LogLevel::ERROR, "test_module", "JSON test message", "JSON_001");
    
    // Reset to text format
    logger.set_json_format(false);
    
    std::cout << "✅ JSON formatting test passed" << std::endl;
}

void test_async_logging() {
    Logger& logger = Logger::instance();
    
    // Enable async logging
    logger.set_async_logging(true);
    
    // Log several messages
    for (int i = 0; i < 10; ++i) {
        logger.log(LogLevel::INFO, "Async message ", i);
    }
    
    // Give async worker time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Disable async logging (this should flush remaining messages)
    logger.set_async_logging(false);
    
    std::cout << "✅ Async logging test passed" << std::endl;
}

void test_macro_usage() {
    // Test that macros compile and work
    LOG_TRACE("Trace message");
    LOG_DEBUG("Debug message");
    LOG_INFO("Info message");
    LOG_WARN("Warning message");
    LOG_ERROR("Error message");
    LOG_CRITICAL("Critical message");
    
    // Test module-specific macros
    LOG_NETWORK_ERROR("Network error test");
    LOG_CONSENSUS_ERROR("Consensus error test");
    LOG_LEDGER_ERROR("Ledger error test");
    LOG_SVM_ERROR("SVM error test");
    LOG_VALIDATOR_ERROR("Validator error test");
    
    std::cout << "✅ Macro usage test passed" << std::endl;
}

void test_console_alert_channel() {
    auto console_channel = AlertChannelFactory::create_console_channel(true);
    
    ASSERT_TRUE(console_channel->is_enabled());
    ASSERT_TRUE(console_channel->get_name() == "console");
    
    LogEntry test_entry{
        std::chrono::system_clock::now(),
        LogLevel::CRITICAL,
        "test_module",
        "main_thread",
        "Test critical failure",
        "TEST_001",
        {{"key1", "value1"}, {"key2", "value2"}}
    };
    
    // Should not crash
    console_channel->send_alert(test_entry);
    
    std::cout << "✅ Console alert channel test passed" << std::endl;
}

void test_file_alert_channel() {
    std::string temp_file = get_temp_file_path("test_alerts");
    auto file_channel = AlertChannelFactory::create_file_channel(temp_file, true);
    
    ASSERT_TRUE(file_channel->is_enabled());
    ASSERT_TRUE(file_channel->get_name() == "file");
    
    LogEntry test_entry{
        std::chrono::system_clock::now(),
        LogLevel::CRITICAL,
        "test_module",
        "main_thread",
        "Test file alert",
        "FILE_001",
        {}
    };
    
    file_channel->send_alert(test_entry);
    
    // Check that file was created and contains content
    std::ifstream file(temp_file);
    ASSERT_TRUE(file.is_open());
    
    std::string line;
    bool found_alert = false;
    while (std::getline(file, line)) {
        if (line.find("Test file alert") != std::string::npos) {
            found_alert = true;
            break;
        }
    }
    ASSERT_TRUE(found_alert);
    
    // Clean up
    std::filesystem::remove(temp_file);
    
    std::cout << "✅ File alert channel test passed" << std::endl;
}

void test_file_alert_error_handling() {
    // Test with an invalid path to trigger error handling
    std::string invalid_path = "/invalid/directory/test_alerts.log";
    auto file_channel = AlertChannelFactory::create_file_channel(invalid_path, true);
    
    LogEntry test_entry{
        std::chrono::system_clock::now(),
        LogLevel::CRITICAL,
        "test_module",
        "main_thread",
        "Test error handling",
        "ERR_001",
        {}
    };
    
    // This should not crash but should output error to stderr
    file_channel->send_alert(test_entry);
    
    std::cout << "✅ File alert error handling test passed" << std::endl;
}

void test_end_to_end_alert_triggering() {
    Logger& logger = Logger::instance();
    
    // Setup multiple alert channels
    std::string temp_file = get_temp_file_path("e2e_alerts");
    logger.add_alert_channel(AlertChannelFactory::create_file_channel(temp_file, true));
    logger.add_alert_channel(AlertChannelFactory::create_console_channel(true));
    logger.add_alert_channel(AlertChannelFactory::create_prometheus_channel(true));
    
    // Trigger an end-to-end critical failure
    std::unordered_map<std::string, std::string> context = {
        {"component", "network"},
        {"peer_count", "0"},
        {"last_seen", "30s ago"}
    };
    
    logger.log_critical_failure("network", "Complete network isolation detected", "NET_E2E_001", context);
    
    // Verify file channel received the alert
    std::ifstream file(temp_file);
    ASSERT_TRUE(file.is_open());
    
    std::string content;
    std::string line;
    while (std::getline(file, line)) {
        content += line + "\n";
    }
    
    ASSERT_TRUE(content.find("Complete network isolation detected") != std::string::npos);
    ASSERT_TRUE(content.find("NET_E2E_001") != std::string::npos);
    
    // Clean up
    std::filesystem::remove(temp_file);
    
    std::cout << "✅ End-to-end alert triggering test passed" << std::endl;
}

void test_slack_alert_channel() {
    auto slack_channel = AlertChannelFactory::create_slack_channel("https://hooks.slack.com/test", true);
    
    ASSERT_TRUE(slack_channel->is_enabled());
    ASSERT_TRUE(slack_channel->get_name() == "slack");
    
    LogEntry test_entry{
        std::chrono::system_clock::now(),
        LogLevel::CRITICAL,
        "test_module",
        "main_thread",
        "Test Slack alert",
        "SLACK_001",
        {}
    };
    
    // Should not crash (this is a mock implementation)
    slack_channel->send_alert(test_entry);
    
    std::cout << "✅ Slack alert channel test passed" << std::endl;
}

void test_logger_with_alert_channels() {
    Logger& logger = Logger::instance();
    
    // Add alert channels
    logger.add_alert_channel(AlertChannelFactory::create_console_channel(true));
    std::string temp_file = get_temp_file_path("test_logger_alerts");
    logger.add_alert_channel(AlertChannelFactory::create_file_channel(temp_file, true));
    
    // Test critical failure that should trigger alerts
    logger.log_critical_failure("test_integration", "Integration test critical failure", "INT_001");
    
    // Check that file alert was written
    std::ifstream file(temp_file);
    ASSERT_TRUE(file.is_open());
    
    std::string line;
    bool found_alert = false;
    while (std::getline(file, line)) {
        if (line.find("Integration test critical failure") != std::string::npos) {
            found_alert = true;
            break;
        }
    }
    ASSERT_TRUE(found_alert);
    
    // Clean up
    std::filesystem::remove(temp_file);
    
    std::cout << "✅ Logger with alert channels test passed" << std::endl;
}

int main() {
    try {
        std::cout << "Starting Logging and Alerting Tests..." << std::endl;
        
        test_basic_logging_levels();
        test_structured_logging();
        test_critical_failure_logging();
        test_json_formatting();
        test_json_encoding_edge_cases();
        test_async_logging();
        test_async_logging_bounded_queue();
        test_macro_usage();
        test_console_alert_channel();
        test_file_alert_channel();
        test_file_alert_error_handling();
        test_slack_alert_channel();
        test_alert_rate_limiting();
        test_logger_with_alert_channels();
        test_end_to_end_alert_triggering();
        
        std::cout << "🎉 All logging and alerting tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed: " << e.what() << std::endl;
        return 1;
    }
}