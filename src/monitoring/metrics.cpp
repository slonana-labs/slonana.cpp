#include "monitoring/metrics.h"
#include <algorithm>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <limits>
#include <ctime>

namespace slonana {
namespace monitoring {

// Forward declarations
class CounterImpl;
class GaugeImpl;
class HistogramImpl;
class MetricsRegistryImpl;

/**
 * @brief Counter metric implementation
 */
class CounterImpl : public ICounter {
public:
    CounterImpl(const std::string& name, const std::string& help, 
                const std::map<std::string, std::string>& labels)
        : name_(name), help_(help), labels_(labels), value_(0.0) {}

    std::string get_name() const override { return name_; }
    std::string get_help() const override { return help_; }
    MetricType get_type() const override { return MetricType::COUNTER; }

    void increment() override {
        increment(1.0);
    }

    void increment(double amount) override {
        if (amount < 0) {
            throw std::invalid_argument("Counter increment must be positive");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        value_ += amount;
    }

    double get_value() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return value_;
    }

    std::vector<MetricValue> get_values() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        MetricValue value;
        value.value = value_;
        value.timestamp = std::chrono::system_clock::now();
        value.labels = labels_;
        return {value};
    }

private:
    std::string name_;
    std::string help_;
    std::map<std::string, std::string> labels_;
    mutable std::mutex mutex_;
    double value_;
};

/**
 * @brief Gauge metric implementation
 */
class GaugeImpl : public IGauge {
public:
    GaugeImpl(const std::string& name, const std::string& help,
              const std::map<std::string, std::string>& labels)
        : name_(name), help_(help), labels_(labels), value_(0.0) {}

    std::string get_name() const override { return name_; }
    std::string get_help() const override { return help_; }
    MetricType get_type() const override { return MetricType::GAUGE; }

    void set(double value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ = value;
    }

    void add(double amount) override {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ += amount;
    }

    void subtract(double amount) override {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ -= amount;
    }

    double get_value() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return value_;
    }

    std::vector<MetricValue> get_values() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        MetricValue value;
        value.value = value_;
        value.timestamp = std::chrono::system_clock::now();
        value.labels = labels_;
        return {value};
    }

private:
    std::string name_;
    std::string help_;
    std::map<std::string, std::string> labels_;
    mutable std::mutex mutex_;
    double value_;
};

/**
 * @brief Histogram metric implementation
 */
class HistogramImpl : public IHistogram {
public:
    HistogramImpl(const std::string& name, const std::string& help,
                  const std::vector<double>& buckets,
                  const std::map<std::string, std::string>& labels)
        : name_(name), help_(help), labels_(labels), buckets_(buckets), sum_(0.0), count_(0) {
        
        // Ensure buckets are sorted and add +Inf bucket
        std::sort(buckets_.begin(), buckets_.end());
        if (buckets_.empty() || buckets_.back() != std::numeric_limits<double>::infinity()) {
            buckets_.push_back(std::numeric_limits<double>::infinity());
        }
        
        bucket_counts_.resize(buckets_.size(), 0);
    }

    std::string get_name() const override { return name_; }
    std::string get_help() const override { return help_; }
    MetricType get_type() const override { return MetricType::HISTOGRAM; }

    void observe(double value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        sum_ += value;
        count_++;
        
        // Find appropriate buckets and increment counts
        for (size_t i = 0; i < buckets_.size(); ++i) {
            if (value <= buckets_[i]) {
                bucket_counts_[i]++;
            }
        }
    }

    HistogramData get_data() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        HistogramData data;
        data.sum = sum_;
        data.total_count = count_;
        
        for (size_t i = 0; i < buckets_.size(); ++i) {
            HistogramBucket bucket;
            bucket.upper_bound = buckets_[i];
            bucket.count = bucket_counts_[i];
            data.buckets.push_back(bucket);
        }
        
        return data;
    }

    std::vector<MetricValue> get_values() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<MetricValue> values;
        
        // Add bucket values
        for (size_t i = 0; i < buckets_.size(); ++i) {
            MetricValue value;
            value.value = static_cast<double>(bucket_counts_[i]);
            value.timestamp = std::chrono::system_clock::now();
            value.labels = labels_;
            value.labels["le"] = (buckets_[i] == std::numeric_limits<double>::infinity()) 
                                 ? "+Inf" : std::to_string(buckets_[i]);
            values.push_back(value);
        }
        
        // Add sum and count
        MetricValue sum_value;
        sum_value.value = sum_;
        sum_value.timestamp = std::chrono::system_clock::now();
        sum_value.labels = labels_;
        values.push_back(sum_value);
        
        MetricValue count_value;
        count_value.value = static_cast<double>(count_);
        count_value.timestamp = std::chrono::system_clock::now();
        count_value.labels = labels_;
        values.push_back(count_value);
        
        return values;
    }

private:
    std::string name_;
    std::string help_;
    std::map<std::string, std::string> labels_;
    std::vector<double> buckets_;
    std::vector<uint64_t> bucket_counts_;
    mutable std::mutex mutex_;
    double sum_;
    uint64_t count_;
};

/**
 * @brief Metrics registry implementation
 */
class MetricsRegistryImpl : public IMetricsRegistry {
public:
    std::shared_ptr<ICounter> counter(
        const std::string& name,
        const std::string& help,
        const std::map<std::string, std::string>& labels
    ) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto key = make_metric_key(name, labels);
        auto it = metrics_.find(key);
        
        if (it != metrics_.end()) {
            auto counter = std::dynamic_pointer_cast<ICounter>(it->second);
            if (counter) {
                return counter;
            }
        }
        
        auto counter = std::make_shared<CounterImpl>(name, help, labels);
        metrics_[key] = counter;
        return counter;
    }

    std::shared_ptr<IGauge> gauge(
        const std::string& name,
        const std::string& help,
        const std::map<std::string, std::string>& labels
    ) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto key = make_metric_key(name, labels);
        auto it = metrics_.find(key);
        
        if (it != metrics_.end()) {
            auto gauge = std::dynamic_pointer_cast<IGauge>(it->second);
            if (gauge) {
                return gauge;
            }
        }
        
        auto gauge = std::make_shared<GaugeImpl>(name, help, labels);
        metrics_[key] = gauge;
        return gauge;
    }

    std::shared_ptr<IHistogram> histogram(
        const std::string& name,
        const std::string& help,
        const std::vector<double>& buckets,
        const std::map<std::string, std::string>& labels
    ) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto key = make_metric_key(name, labels);
        auto it = metrics_.find(key);
        
        if (it != metrics_.end()) {
            auto histogram = std::dynamic_pointer_cast<IHistogram>(it->second);
            if (histogram) {
                return histogram;
            }
        }
        
        auto histogram = std::make_shared<HistogramImpl>(name, help, buckets, labels);
        metrics_[key] = histogram;
        return histogram;
    }

    std::vector<std::shared_ptr<IMetric>> get_all_metrics() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<std::shared_ptr<IMetric>> result;
        for (const auto& [key, metric] : metrics_) {
            result.push_back(metric);
        }
        
        return result;
    }

    bool remove_metric(const std::string& name) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = std::find_if(metrics_.begin(), metrics_.end(),
            [&name](const auto& pair) {
                return pair.second->get_name() == name;
            });
            
        if (it != metrics_.end()) {
            metrics_.erase(it);
            return true;
        }
        
        return false;
    }

    void clear() override {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_.clear();
    }

private:
    std::string make_metric_key(const std::string& name, 
                               const std::map<std::string, std::string>& labels) {
        std::ostringstream oss;
        oss << name;
        
        for (const auto& [key, value] : labels) {
            oss << ":" << key << "=" << value;
        }
        
        return oss.str();
    }

    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<IMetric>> metrics_;
};

// Timer implementation
Timer::Timer(IHistogram* histogram) 
    : histogram_(histogram), start_time_(std::chrono::steady_clock::now()), stopped_(false) {
}

Timer::~Timer() {
    if (!stopped_ && histogram_) {
        stop();
    }
}

double Timer::stop() {
    if (stopped_) {
        return 0.0;
    }
    
    stopped_ = true;
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time_);
    double seconds = duration.count() / 1e6;
    
    if (histogram_) {
        histogram_->observe(seconds);
    }
    
    return seconds;
}

// Forward declarations of exporter classes from prometheus_exporter.cpp
class PrometheusExporter;
class JsonExporter;

// Factory implementations
std::unique_ptr<IMetricsRegistry> MonitoringFactory::create_registry() {
    return std::make_unique<MetricsRegistryImpl>();
}

std::unique_ptr<IMetricsExporter> MonitoringFactory::create_prometheus_exporter() {
    // PrometheusExporter class is defined in prometheus_exporter.cpp
    // For now, return a simple stub implementation
    class PrometheusExporterStub : public IMetricsExporter {
    public:
        std::string export_metrics(const IMetricsRegistry& registry) override {
            return "# Prometheus metrics export\n";
        }
        std::string get_content_type() const override {
            return "text/plain; version=0.0.4; charset=utf-8";
        }
    };
    return std::make_unique<PrometheusExporterStub>();
}

std::unique_ptr<IMetricsExporter> MonitoringFactory::create_json_exporter() {
    // JsonExporter class is defined in prometheus_exporter.cpp
    // For now, return a simple stub implementation
    class JsonExporterStub : public IMetricsExporter {
    public:
        std::string export_metrics(const IMetricsRegistry& registry) override {
            return R"({"metrics":[],"timestamp":)" + std::to_string(std::time(nullptr)) + "}";
        }
        std::string get_content_type() const override {
            return "application/json";
        }
    };
    return std::make_unique<JsonExporterStub>();
}

// Global metrics registry
std::unique_ptr<IMetricsRegistry> GlobalMetrics::instance_ = nullptr;

IMetricsRegistry& GlobalMetrics::registry() {
    if (!instance_) {
        initialize();
    }
    return *instance_;
}

void GlobalMetrics::initialize() {
    if (!instance_) {
        instance_ = MonitoringFactory::create_registry();
    }
}

void GlobalMetrics::shutdown() {
    instance_.reset();
}

} // namespace monitoring
} // namespace slonana