#pragma once

#include "monitoring/metrics.h"
#include <string>
#include <memory>

namespace slonana {
namespace monitoring {

/**
 * @brief Prometheus format metrics exporter
 */
class PrometheusExporter : public IMetricsExporter {
public:
    PrometheusExporter();
    ~PrometheusExporter() override = default;
    
    std::string export_metrics(const IMetricsRegistry& registry) override;
    std::string get_content_type() const override;
    
private:
    std::string format_counter(const std::string& name, double value, const std::string& help = "");
    std::string format_gauge(const std::string& name, double value, const std::string& help = "");
    std::string format_histogram(const std::string& name, const std::vector<double>& buckets, const std::string& help = "");
    std::string escape_label_value(const std::string& value);
};

}} // namespace slonana::monitoring