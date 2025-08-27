#include "test_framework.h"
#include "monitoring/metrics.h"
#include <memory>
#include <thread>
#include <chrono>

// Forward declare resource monitor tests
void run_resource_monitor_tests(TestRunner& runner);

using namespace slonana::monitoring;

void test_metrics_registry_creation() {
    auto registry = MonitoringFactory::create_registry();
    ASSERT_TRUE(registry.get() != nullptr);
}

void test_counter_creation_and_operation() {
    auto registry = MonitoringFactory::create_registry();
    
    auto counter = registry->counter("test_counter", "Test counter metric");
    ASSERT_TRUE(counter.get() != nullptr);
    ASSERT_EQ(std::string("test_counter"), counter->get_name());
    ASSERT_EQ(std::string("Test counter metric"), counter->get_help());
    ASSERT_EQ(static_cast<int>(MetricType::COUNTER), static_cast<int>(counter->get_type()));
    
    // Test initial value
    ASSERT_EQ(0.0, counter->get_value());
    
    // Test increment
    counter->increment();
    ASSERT_EQ(1.0, counter->get_value());
    
    counter->increment(5.0);
    ASSERT_EQ(6.0, counter->get_value());
    
    // Test negative increment throws exception
    ASSERT_THROWS(counter->increment(-1.0), std::invalid_argument);
}

void test_gauge_creation_and_operation() {
    auto registry = MonitoringFactory::create_registry();
    
    auto gauge = registry->gauge("test_gauge", "Test gauge metric");
    ASSERT_TRUE(gauge.get() != nullptr);
    ASSERT_EQ(std::string("test_gauge"), gauge->get_name());
    ASSERT_EQ(std::string("Test gauge metric"), gauge->get_help());
    ASSERT_EQ(static_cast<int>(MetricType::GAUGE), static_cast<int>(gauge->get_type()));
    
    // Test initial value
    ASSERT_EQ(0.0, gauge->get_value());
    
    // Test set value
    gauge->set(10.0);
    ASSERT_EQ(10.0, gauge->get_value());
    
    // Test add/subtract
    gauge->add(5.0);
    ASSERT_EQ(15.0, gauge->get_value());
    
    gauge->subtract(3.0);
    ASSERT_EQ(12.0, gauge->get_value());
}

void test_histogram_creation_and_operation() {
    auto registry = MonitoringFactory::create_registry();
    
    std::vector<double> buckets = {0.1, 0.5, 1.0, 2.0, 5.0};
    auto histogram = registry->histogram("test_histogram", "Test histogram metric", buckets);
    ASSERT_TRUE(histogram.get() != nullptr);
    ASSERT_EQ(std::string("test_histogram"), histogram->get_name());
    ASSERT_EQ(std::string("Test histogram metric"), histogram->get_help());
    ASSERT_EQ(static_cast<int>(MetricType::HISTOGRAM), static_cast<int>(histogram->get_type()));
    
    // Test observations
    histogram->observe(0.05);  // Falls in 0.1 bucket
    histogram->observe(0.3);   // Falls in 0.5 bucket
    histogram->observe(1.5);   // Falls in 2.0 bucket
    histogram->observe(10.0);  // Falls in +Inf bucket
    
    auto data = histogram->get_data();
    ASSERT_EQ(static_cast<uint64_t>(4), data.total_count);
    ASSERT_GT(data.sum, 0.0);
    ASSERT_GT(data.buckets.size(), static_cast<size_t>(0));
}

void test_timer_functionality() {
    auto registry = MonitoringFactory::create_registry();
    auto histogram = registry->histogram("test_timer", "Test timer metric");
    
    {
        Timer timer(histogram.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        double duration = timer.stop();
        ASSERT_GT(duration, 0.001); // At least 1ms
    }
    
    auto data = histogram->get_data();
    ASSERT_EQ(static_cast<uint64_t>(1), data.total_count);
}

void test_metrics_with_labels() {
    auto registry = MonitoringFactory::create_registry();
    
    std::map<std::string, std::string> labels = {
        {"service", "validator"},
        {"environment", "test"}
    };
    
    auto counter = registry->counter("test_labeled_counter", "Test counter with labels", labels);
    ASSERT_TRUE(counter.get() != nullptr);
    
    auto values = counter->get_values();
    ASSERT_EQ(static_cast<size_t>(1), values.size());
    ASSERT_EQ(std::string("validator"), values[0].labels.at("service"));
    ASSERT_EQ(std::string("test"), values[0].labels.at("environment"));
}

void test_registry_metric_management() {
    auto registry = MonitoringFactory::create_registry();
    
    // Create some metrics
    auto counter1 = registry->counter("counter1", "First counter");
    auto counter2 = registry->counter("counter2", "Second counter");
    auto gauge1 = registry->gauge("gauge1", "First gauge");
    
    // Check all metrics are registered
    auto all_metrics = registry->get_all_metrics();
    ASSERT_EQ(static_cast<size_t>(3), all_metrics.size());
    
    // Remove a metric
    ASSERT_TRUE(registry->remove_metric("counter1"));
    ASSERT_FALSE(registry->remove_metric("non_existent"));
    
    all_metrics = registry->get_all_metrics();
    ASSERT_EQ(static_cast<size_t>(2), all_metrics.size());
    
    // Clear all metrics
    registry->clear();
    all_metrics = registry->get_all_metrics();
    ASSERT_EQ(static_cast<size_t>(0), all_metrics.size());
}

void test_same_metric_name_returns_same_instance() {
    auto registry = MonitoringFactory::create_registry();
    
    auto counter1 = registry->counter("shared_counter", "Shared counter");
    auto counter2 = registry->counter("shared_counter", "Shared counter");
    
    // Should return the same instance
    ASSERT_EQ(counter1.get(), counter2.get());
    
    counter1->increment();
    ASSERT_EQ(1.0, counter2->get_value());
}

void test_global_metrics_registry() {
    GlobalMetrics::initialize();
    
    auto& registry1 = GlobalMetrics::registry();
    auto& registry2 = GlobalMetrics::registry();
    
    // Should be the same instance
    ASSERT_EQ(&registry1, &registry2);
    
    // Test using global registry
    auto counter = registry1.counter("global_test_counter", "Global test counter");
    counter->increment();
    ASSERT_EQ(1.0, counter->get_value());
    
    GlobalMetrics::shutdown();
}

void test_metric_values_timestamp() {
    auto registry = MonitoringFactory::create_registry();
    auto counter = registry->counter("timestamp_test", "Timestamp test");
    
    auto before = std::chrono::system_clock::now();
    counter->increment();
    auto values = counter->get_values();
    auto after = std::chrono::system_clock::now();
    
    ASSERT_EQ(static_cast<size_t>(1), values.size());
    ASSERT_GE(values[0].timestamp, before);
    ASSERT_LE(values[0].timestamp, after);
}

// Test runner function
void run_monitoring_tests() {
    TestRunner runner;
    
    runner.run_test("metrics_registry_creation", test_metrics_registry_creation);
    runner.run_test("counter_creation_and_operation", test_counter_creation_and_operation);
    runner.run_test("gauge_creation_and_operation", test_gauge_creation_and_operation);
    runner.run_test("histogram_creation_and_operation", test_histogram_creation_and_operation);
    runner.run_test("timer_functionality", test_timer_functionality);
    runner.run_test("metrics_with_labels", test_metrics_with_labels);
    runner.run_test("registry_metric_management", test_registry_metric_management);
    runner.run_test("same_metric_name_returns_same_instance", test_same_metric_name_returns_same_instance);
    runner.run_test("global_metrics_registry", test_global_metrics_registry);
    runner.run_test("metric_values_timestamp", test_metric_values_timestamp);
    
    // Run resource monitor tests
    std::cout << "\n=== Resource Monitor Tests ===" << std::endl;
    run_resource_monitor_tests(runner);
    
    runner.print_summary();
}

#ifdef STANDALONE_MONITORING_TESTS
// Main function for standalone test execution
int main() {
    run_monitoring_tests();
    return 0;
}
#endif