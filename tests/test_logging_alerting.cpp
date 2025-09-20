#include "common/logging.h"
#include "common/alerting.h"
#include "test_framework.h"
#include <fstream>
#include <thread>
#include <chrono>

using namespace slonana::common;

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
    std::string temp_file = "/tmp/test_alerts.log";
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
    std::remove(temp_file.c_str());
    
    std::cout << "✅ File alert channel test passed" << std::endl;
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
    logger.add_alert_channel(AlertChannelFactory::create_file_channel("/tmp/test_logger_alerts.log", true));
    
    // Test critical failure that should trigger alerts
    logger.log_critical_failure("test_integration", "Integration test critical failure", "INT_001");
    
    // Check that file alert was written
    std::ifstream file("/tmp/test_logger_alerts.log");
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
    std::remove("/tmp/test_logger_alerts.log");
    
    std::cout << "✅ Logger with alert channels test passed" << std::endl;
}

int main() {
    try {
        std::cout << "Starting Logging and Alerting Tests..." << std::endl;
        
        test_basic_logging_levels();
        test_structured_logging();
        test_critical_failure_logging();
        test_json_formatting();
        test_async_logging();
        test_macro_usage();
        test_console_alert_channel();
        test_file_alert_channel();
        test_slack_alert_channel();
        test_logger_with_alert_channels();
        
        std::cout << "🎉 All logging and alerting tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed: " << e.what() << std::endl;
        return 1;
    }
}