#include "test_framework.h"
#include "monitoring/resource_monitor.h"
#include <chrono>
#include <thread>

using namespace slonana::monitoring;

void test_resource_monitor_basic() {
    std::cout << "Testing resource monitor basic functionality..." << std::endl;
    
    ResourceMonitorConfig config;
    config.memory_headroom_mb = 256;
    config.check_interval = std::chrono::milliseconds(100);
    config.enable_automatic_logging = false; // Disable to avoid spam
    
    ResourceMonitor monitor(config);
    
    // Test getting current usage
    auto usage = monitor.get_current_usage();
    ASSERT_GT(usage.total_memory_bytes, 0);
    ASSERT_GT(usage.available_memory_bytes, 0);
    ASSERT_GE(usage.memory_usage_ratio, 0.0);
    ASSERT_LE(usage.memory_usage_ratio, 1.0);
    
    std::cout << "  Memory usage: " << (usage.memory_usage_ratio * 100) << "%" << std::endl;
    std::cout << "  Available memory: " << (usage.available_memory_bytes / (1024 * 1024)) << "MB" << std::endl;
    
    // Test memory headroom check
    bool headroom_ok = monitor.ensure_memory_headroom(100); // 100MB should be available
    std::cout << "  Memory headroom check (100MB): " << (headroom_ok ? "OK" : "FAILED") << std::endl;
    
    // Test starting/stopping
    ASSERT_TRUE(monitor.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    monitor.stop();
    
    std::cout << "✅ Resource monitor basic test passed" << std::endl;
}

void test_resource_monitor_format() {
    std::cout << "Testing resource monitor formatting..." << std::endl;
    
    ResourceUsage usage;
    usage.total_memory_bytes = 16ULL * 1024 * 1024 * 1024; // 16GB
    usage.available_memory_bytes = 8ULL * 1024 * 1024 * 1024; // 8GB
    usage.used_memory_bytes = 8ULL * 1024 * 1024 * 1024; // 8GB
    usage.memory_usage_ratio = 0.5; // 50%
    usage.cpu_usage_percent = 25.0;
    usage.total_disk_bytes = 1000ULL * 1024 * 1024 * 1024; // 1TB
    usage.available_disk_bytes = 500ULL * 1024 * 1024 * 1024; // 500GB
    usage.disk_usage_ratio = 0.5; // 50%
    usage.timestamp = std::chrono::system_clock::now();
    
    std::string formatted = ResourceMonitor::format_resource_usage(usage);
    std::cout << "  Formatted: " << formatted << std::endl;
    
    // Check that the formatted string contains expected values
    ASSERT_NE(formatted.find("8192.0/16384.0MB"), std::string::npos);
    ASSERT_NE(formatted.find("50.0%"), std::string::npos);
    ASSERT_NE(formatted.find("25.0%"), std::string::npos);
    
    std::cout << "✅ Resource monitor formatting test passed" << std::endl;
}

void test_resource_monitor_callback() {
    std::cout << "Testing resource monitor callback..." << std::endl;
    
    ResourceMonitorConfig config;
    config.memory_warning_threshold = 0.01; // Very low threshold to trigger callback
    config.cpu_warning_threshold = 0.0; // Very low threshold
    config.check_interval = std::chrono::milliseconds(50);
    config.enable_automatic_logging = false;
    
    ResourceMonitor monitor(config);
    
    bool callback_triggered = false;
    std::string callback_message;
    
    monitor.register_exhaustion_callback([&](const ResourceUsage& usage, const std::string& message) {
        callback_triggered = true;
        callback_message = message;
        std::cout << "  Callback triggered: " << message << std::endl;
    });
    
    // Start monitoring - should trigger callback due to low thresholds
    monitor.start();
    
    // Wait for callback to be triggered
    for (int i = 0; i < 20 && !callback_triggered; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    monitor.stop();
    
    ASSERT_TRUE(callback_triggered);
    ASSERT_FALSE(callback_message.empty());
    
    std::cout << "✅ Resource monitor callback test passed" << std::endl;
}

void test_static_utilities() {
    std::cout << "Testing static utility functions..." << std::endl;
    
    // Test memory info
    uint64_t total_memory, available_memory;
    bool memory_ok = ResourceMonitor::get_memory_info(total_memory, available_memory);
    ASSERT_TRUE(memory_ok);
    ASSERT_GT(total_memory, 0);
    ASSERT_GT(available_memory, 0);
    ASSERT_LE(available_memory, total_memory);
    
    std::cout << "  Total memory: " << (total_memory / (1024 * 1024)) << "MB" << std::endl;
    std::cout << "  Available memory: " << (available_memory / (1024 * 1024)) << "MB" << std::endl;
    
    // Test CPU usage
    double cpu_usage = ResourceMonitor::get_cpu_usage();
    ASSERT_GE(cpu_usage, 0.0);
    ASSERT_LE(cpu_usage, 100.0);
    std::cout << "  CPU usage: " << cpu_usage << "%" << std::endl;
    
    // Test disk usage
    uint64_t total_disk, available_disk;
    bool disk_ok = ResourceMonitor::get_disk_usage(".", total_disk, available_disk);
    ASSERT_TRUE(disk_ok);
    ASSERT_GT(total_disk, 0);
    ASSERT_GT(available_disk, 0);
    ASSERT_LE(available_disk, total_disk);
    
    std::cout << "  Total disk: " << (total_disk / (1024 * 1024 * 1024)) << "GB" << std::endl;
    std::cout << "  Available disk: " << (available_disk / (1024 * 1024 * 1024)) << "GB" << std::endl;
    
    std::cout << "✅ Static utilities test passed" << std::endl;
}

void run_resource_monitor_tests(TestRunner& runner) {
    runner.run_test("Resource Monitor Basic", test_resource_monitor_basic);
    runner.run_test("Resource Monitor Format", test_resource_monitor_format);
    runner.run_test("Resource Monitor Callback", test_resource_monitor_callback);
    runner.run_test("Static Utilities", test_static_utilities);
}

#ifdef STANDALONE_RESOURCE_MONITOR_TESTS
int main() {
    TestRunner runner;
    std::cout << "=== Resource Monitor Test Suite ===" << std::endl;
    
    run_resource_monitor_tests(runner);
    
    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Tests run: " << runner.get_total_tests() << std::endl;
    std::cout << "Tests passed: " << runner.get_passed_tests() << std::endl;
    std::cout << "Tests failed: " << runner.get_failed_tests() << std::endl;
    
    if (runner.get_failed_tests() == 0) {
        std::cout << "All tests PASSED!" << std::endl;
        return 0;
    } else {
        std::cout << "Some tests FAILED!" << std::endl;
        return 1;
    }
}
#endif