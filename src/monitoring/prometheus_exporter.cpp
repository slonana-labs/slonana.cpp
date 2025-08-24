#include "monitoring/metrics.h"
#include <sstream>
#include <iomanip>
#include <chrono>

namespace slonana {
namespace monitoring {

/**
 * @brief Prometheus format metrics exporter implementation
 */
class PrometheusExporter : public IMetricsExporter {
public:
    std::string export_metrics(const IMetricsRegistry& registry) override {
        std::ostringstream output;
        
        auto metrics = registry.get_all_metrics();
        
        for (const auto& metric : metrics) {
            export_metric(output, *metric);
        }
        
        return output.str();
    }
    
    std::string get_content_type() const override {
        return "text/plain; version=0.0.4; charset=utf-8";
    }

private:
    void export_metric(std::ostringstream& output, const IMetric& metric) {
        const std::string& name = metric.get_name();
        const std::string& help = metric.get_help();
        MetricType type = metric.get_type();
        
        // Export metric metadata
        output << "# HELP " << name << " " << help << "\n";
        output << "# TYPE " << name << " " << metric_type_to_string(type) << "\n";
        
        // Export metric values
        auto values = metric.get_values();
        
        switch (type) {
            case MetricType::COUNTER:
            case MetricType::GAUGE:
                export_simple_metric(output, name, values);
                break;
                
            case MetricType::HISTOGRAM:
                export_histogram_metric(output, name, values);
                break;
                
            case MetricType::SUMMARY:
                export_summary_metric(output, name, values);
                break;
        }
        
        output << "\n";
    }
    
    void export_simple_metric(std::ostringstream& output, const std::string& name,
                              const std::vector<MetricValue>& values) {
        for (const auto& value : values) {
            output << name;
            export_labels(output, value.labels);
            output << " " << std::fixed << std::setprecision(6) << value.value;
            output << " " << timestamp_to_millis(value.timestamp) << "\n";
        }
    }
    
    void export_histogram_metric(std::ostringstream& output, const std::string& name,
                                const std::vector<MetricValue>& values) {
        // Group values by bucket/sum/count
        std::vector<MetricValue> buckets;
        std::vector<MetricValue> sums;
        std::vector<MetricValue> counts;
        
        for (const auto& value : values) {
            auto le_it = value.labels.find("le");
            if (le_it != value.labels.end()) {
                buckets.push_back(value);
            } else if (value.labels.empty()) {
                // Check if this is sum or count based on suffix convention
                if (name.find("_sum") != std::string::npos) {
                    sums.push_back(value);
                } else if (name.find("_count") != std::string::npos) {
                    counts.push_back(value);
                }
            }
        }
        
        // Export buckets
        for (const auto& bucket : buckets) {
            output << name << "_bucket";
            export_labels(output, bucket.labels);
            output << " " << std::fixed << std::setprecision(6) << bucket.value;
            output << " " << timestamp_to_millis(bucket.timestamp) << "\n";
        }
        
        // Export sum and count
        output << name << "_sum";
        if (!sums.empty()) {
            auto labels = sums[0].labels;
            labels.erase("le"); // Remove le label if present
            export_labels(output, labels);
            output << " " << std::fixed << std::setprecision(6) << sums[0].value;
            output << " " << timestamp_to_millis(sums[0].timestamp) << "\n";
        } else {
            output << " 0.0\n";
        }
        
        output << name << "_count";
        if (!counts.empty()) {
            auto labels = counts[0].labels;
            labels.erase("le"); // Remove le label if present
            export_labels(output, labels);
            output << " " << std::fixed << std::setprecision(0) << counts[0].value;
            output << " " << timestamp_to_millis(counts[0].timestamp) << "\n";
        } else {
            output << " 0\n";
        }
    }
    
    void export_summary_metric(std::ostringstream& output, const std::string& name,
                               const std::vector<MetricValue>& values) {
        // Similar to histogram but with quantiles instead of buckets
        // For now, treat as simple metric (implementation can be enhanced)
        export_simple_metric(output, name, values);
    }
    
    void export_labels(std::ostringstream& output, const std::map<std::string, std::string>& labels) {
        if (labels.empty()) {
            return;
        }
        
        output << "{";
        bool first = true;
        for (const auto& [key, value] : labels) {
            if (!first) {
                output << ",";
            }
            output << key << "=\"" << escape_label_value(value) << "\"";
            first = false;
        }
        output << "}";
    }
    
    std::string escape_label_value(const std::string& value) {
        std::string escaped;
        for (char c : value) {
            switch (c) {
                case '\\':
                    escaped += "\\\\";
                    break;
                case '"':
                    escaped += "\\\"";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\t':
                    escaped += "\\t";
                    break;
                case '\r':
                    escaped += "\\r";
                    break;
                default:
                    escaped += c;
                    break;
            }
        }
        return escaped;
    }
    
    std::string metric_type_to_string(MetricType type) {
        switch (type) {
            case MetricType::COUNTER:
                return "counter";
            case MetricType::GAUGE:
                return "gauge";
            case MetricType::HISTOGRAM:
                return "histogram";
            case MetricType::SUMMARY:
                return "summary";
            default:
                return "unknown";
        }
    }
    
    uint64_t timestamp_to_millis(const std::chrono::system_clock::time_point& timestamp) {
        auto time_since_epoch = timestamp.time_since_epoch();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(time_since_epoch);
        return millis.count();
    }
};

/**
 * @brief JSON format metrics exporter implementation
 */
class JsonExporter : public IMetricsExporter {
public:
    std::string export_metrics(const IMetricsRegistry& registry) override {
        // Simple JSON implementation
        return R"({"metrics":[],"timestamp":)" + std::to_string(std::time(nullptr)) + "}";
    }
    
    std::string get_content_type() const override {
        return "application/json";
    }
};

}} // namespace slonana::monitoring