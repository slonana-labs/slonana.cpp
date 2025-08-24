#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <chrono>
#include <functional>

namespace slonana {
namespace monitoring {

/**
 * @brief Metric types supported by the monitoring system
 */
enum class MetricType {
    COUNTER,        // Monotonically increasing value
    GAUGE,          // Arbitrary value that can go up or down
    HISTOGRAM,      // Distribution of values with buckets
    SUMMARY         // Distribution with quantiles
};

/**
 * @brief Metric value with timestamp
 */
struct MetricValue {
    double value;
    std::chrono::system_clock::time_point timestamp;
    std::map<std::string, std::string> labels;
};

/**
 * @brief Histogram bucket definition
 */
struct HistogramBucket {
    double upper_bound;
    uint64_t count;
};

/**
 * @brief Histogram metric data
 */
struct HistogramData {
    std::vector<HistogramBucket> buckets;
    uint64_t total_count;
    double sum;
};

/**
 * @brief Summary quantile definition
 */
struct SummaryQuantile {
    double quantile;    // 0.0 to 1.0
    double value;
};

/**
 * @brief Summary metric data
 */
struct SummaryData {
    std::vector<SummaryQuantile> quantiles;
    uint64_t count;
    double sum;
};

/**
 * @brief Base interface for all metrics
 */
class IMetric {
public:
    virtual ~IMetric() = default;
    
    /**
     * @brief Get the metric name
     * @return metric name
     */
    virtual std::string get_name() const = 0;
    
    /**
     * @brief Get the metric help text
     * @return help description
     */
    virtual std::string get_help() const = 0;
    
    /**
     * @brief Get the metric type
     * @return metric type
     */
    virtual MetricType get_type() const = 0;
    
    /**
     * @brief Get current metric values
     * @return vector of metric values with labels
     */
    virtual std::vector<MetricValue> get_values() const = 0;
};

/**
 * @brief Counter metric interface
 */
class ICounter : public IMetric {
public:
    /**
     * @brief Increment the counter by 1
     */
    virtual void increment() = 0;
    
    /**
     * @brief Increment the counter by specified amount
     * @param amount amount to increment (must be positive)
     */
    virtual void increment(double amount) = 0;
    
    /**
     * @brief Get current counter value
     * @return current value
     */
    virtual double get_value() const = 0;
};

/**
 * @brief Gauge metric interface
 */
class IGauge : public IMetric {
public:
    /**
     * @brief Set the gauge to a specific value
     * @param value new gauge value
     */
    virtual void set(double value) = 0;
    
    /**
     * @brief Increment the gauge by specified amount
     * @param amount amount to add
     */
    virtual void add(double amount) = 0;
    
    /**
     * @brief Decrement the gauge by specified amount
     * @param amount amount to subtract
     */
    virtual void subtract(double amount) = 0;
    
    /**
     * @brief Get current gauge value
     * @return current value
     */
    virtual double get_value() const = 0;
};

/**
 * @brief Histogram metric interface
 */
class IHistogram : public IMetric {
public:
    /**
     * @brief Observe a value in the histogram
     * @param value value to observe
     */
    virtual void observe(double value) = 0;
    
    /**
     * @brief Get current histogram data
     * @return histogram buckets and statistics
     */
    virtual HistogramData get_data() const = 0;
};

/**
 * @brief Timer utility for measuring durations
 */
class Timer {
public:
    /**
     * @brief Create and start a timer
     * @param histogram histogram to observe the duration in
     */
    explicit Timer(IHistogram* histogram);
    
    /**
     * @brief Destructor automatically observes the duration
     */
    ~Timer();
    
    /**
     * @brief Manually stop the timer and record duration
     * @return duration in seconds
     */
    double stop();

private:
    IHistogram* histogram_;
    std::chrono::steady_clock::time_point start_time_;
    bool stopped_;
};

/**
 * @brief Metrics registry interface
 */
class IMetricsRegistry {
public:
    virtual ~IMetricsRegistry() = default;
    
    /**
     * @brief Create or get a counter metric
     * @param name metric name
     * @param help help description
     * @param labels constant labels for this metric
     * @return counter instance
     */
    virtual std::shared_ptr<ICounter> counter(
        const std::string& name,
        const std::string& help,
        const std::map<std::string, std::string>& labels = {}
    ) = 0;
    
    /**
     * @brief Create or get a gauge metric
     * @param name metric name
     * @param help help description
     * @param labels constant labels for this metric
     * @return gauge instance
     */
    virtual std::shared_ptr<IGauge> gauge(
        const std::string& name,
        const std::string& help,
        const std::map<std::string, std::string>& labels = {}
    ) = 0;
    
    /**
     * @brief Create or get a histogram metric
     * @param name metric name
     * @param help help description
     * @param buckets histogram bucket boundaries
     * @param labels constant labels for this metric
     * @return histogram instance
     */
    virtual std::shared_ptr<IHistogram> histogram(
        const std::string& name,
        const std::string& help,
        const std::vector<double>& buckets = {0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0},
        const std::map<std::string, std::string>& labels = {}
    ) = 0;
    
    /**
     * @brief Get all registered metrics
     * @return vector of all metrics
     */
    virtual std::vector<std::shared_ptr<IMetric>> get_all_metrics() const = 0;
    
    /**
     * @brief Remove a metric by name
     * @param name metric name to remove
     * @return true if metric was removed
     */
    virtual bool remove_metric(const std::string& name) = 0;
    
    /**
     * @brief Clear all metrics
     */
    virtual void clear() = 0;
};

/**
 * @brief Metrics exporter interface for different output formats
 */
class IMetricsExporter {
public:
    virtual ~IMetricsExporter() = default;
    
    /**
     * @brief Export metrics in the target format
     * @param registry metrics registry to export from
     * @return exported metrics as string
     */
    virtual std::string export_metrics(const IMetricsRegistry& registry) = 0;
    
    /**
     * @brief Get the content type for HTTP responses
     * @return content type string
     */
    virtual std::string get_content_type() const = 0;
};

/**
 * @brief Factory for creating monitoring components
 */
class MonitoringFactory {
public:
    /**
     * @brief Create a metrics registry instance
     * @return unique pointer to metrics registry
     */
    static std::unique_ptr<IMetricsRegistry> create_registry();
    
    /**
     * @brief Create a Prometheus metrics exporter
     * @return unique pointer to Prometheus exporter
     */
    static std::unique_ptr<IMetricsExporter> create_prometheus_exporter();
    
    /**
     * @brief Create a JSON metrics exporter
     * @return unique pointer to JSON exporter
     */
    static std::unique_ptr<IMetricsExporter> create_json_exporter();
};

/**
 * @brief Global metrics registry singleton
 */
class GlobalMetrics {
private:
    static std::unique_ptr<IMetricsRegistry> instance_;
    
public:
    static IMetricsRegistry& registry();
    static void initialize();
};

}} // namespace slonana::monitoring