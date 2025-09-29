#include "performance/advanced_profiler.h"
#include <algorithm>
#include <fstream>
#include <numeric>
#include <sstream>
#include <cmath>

#ifdef __x86_64__
#include <x86intrin.h>
#endif

namespace slonana {
namespace performance {

AdvancedProfiler::AdvancedProfiler(const MonitorConfig& config) 
    : config_(config) {
#ifdef __linux__
    setup_hardware_counters();
#endif
}

AdvancedProfiler::~AdvancedProfiler() {
    should_stop_monitoring_.store(true);
    if (monitoring_thread_ && monitoring_thread_->joinable()) {
        monitoring_thread_->join();
    }
    
#ifdef __linux__
    cleanup_hardware_counters();
#endif
}

// MeasurementScope Implementation
AdvancedProfiler::MeasurementScope::MeasurementScope(
    AdvancedProfiler& profiler, 
    const std::string& operation_name)
    : profiler_(profiler), operation_name_(operation_name) {
    
    start_time_ = std::chrono::high_resolution_clock::now();
    start_cycles_ = get_cpu_cycles();
    start_cache_misses_ = 0;
    start_branch_misses_ = 0;
    
    // Read hardware counters if available
#ifdef __linux__
    if (!profiler_.perf_counters_.empty()) {
        // Simplified: just use first counter for cache misses
        start_cache_misses_ = profiler_.read_counter(profiler_.perf_counters_[0]);
    }
#endif
}

AdvancedProfiler::MeasurementScope::~MeasurementScope() {
    auto end_time = std::chrono::high_resolution_clock::now();
    uint64_t end_cycles = get_cpu_cycles();
    uint64_t end_cache_misses = start_cache_misses_;
    uint64_t end_branch_misses = start_branch_misses_;
    
    // Read end hardware counters
#ifdef __linux__
    if (!profiler_.perf_counters_.empty()) {
        end_cache_misses = profiler_.read_counter(profiler_.perf_counters_[0]);
    }
#endif
    
    // Calculate duration
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time_);
    
    // Record measurement
    profiler_.record_measurement(
        operation_name_,
        static_cast<double>(duration.count()),
        end_cache_misses - start_cache_misses_,
        end_branch_misses - start_branch_misses_
    );
}

uint64_t AdvancedProfiler::start_measurement(const std::string& operation_name) {
    uint64_t measurement_id = measurement_counter_.fetch_add(1);
    
    MeasurementPoint point;
    point.name = operation_name;
    point.start_cycles = get_cpu_cycles();
    point.timestamp = std::chrono::high_resolution_clock::now();
    
    // Store active measurement
    {
        std::unique_lock<std::shared_mutex> lock(stats_mutex_);
        active_measurements_[measurement_id] = point;
    }
    
    return measurement_id;
}

void AdvancedProfiler::end_measurement(uint64_t measurement_id) {
    uint64_t end_cycles = get_cpu_cycles();
    
    std::unique_lock<std::shared_mutex> lock(stats_mutex_);
    
    auto it = active_measurements_.find(measurement_id);
    if (it != active_measurements_.end()) {
        MeasurementPoint& point = it->second;
        point.end_cycles = end_cycles;
        
        // Calculate duration and record
        double duration_ns = cycles_to_nanoseconds(point.end_cycles - point.start_cycles);
        
        // Add to measurements
        measurements_[point.name].push_back(point);
        
        // Remove from active measurements
        active_measurements_.erase(it);
        
        lock.unlock();
        
        // Record in simplified form
        record_measurement(point.name, duration_ns, point.cache_misses, point.branch_misses);
    }
}

void AdvancedProfiler::record_measurement(
    const std::string& operation_name,
    double latency_ns,
    uint64_t cache_misses,
    uint64_t branch_misses) {
    
    std::unique_lock<std::shared_mutex> lock(stats_mutex_);
    
    MeasurementPoint point;
    point.name = operation_name;
    point.start_cycles = 0;
    point.end_cycles = static_cast<uint64_t>(latency_ns / cycles_to_nanoseconds(1));
    point.cache_misses = cache_misses;
    point.branch_misses = branch_misses;
    point.timestamp = std::chrono::high_resolution_clock::now();
    
    measurements_[operation_name].push_back(point);
}

std::optional<AdvancedProfiler::PerformanceStats> 
AdvancedProfiler::get_stats(const std::string& operation_name) const {
    std::shared_lock<std::shared_mutex> lock(stats_mutex_);
    
    auto it = measurements_.find(operation_name);
    if (it == measurements_.end() || it->second.empty()) {
        return std::nullopt;
    }
    
    return calculate_stats(it->second);
}

std::map<std::string, AdvancedProfiler::PerformanceStats> 
AdvancedProfiler::get_all_stats() const {
    std::shared_lock<std::shared_mutex> lock(stats_mutex_);
    
    std::map<std::string, PerformanceStats> all_stats;
    
    for (const auto& [name, measurements] : measurements_) {
        if (!measurements.empty()) {
            all_stats[name] = calculate_stats(measurements);
        }
    }
    
    return all_stats;
}

void AdvancedProfiler::reset_stats() {
    std::unique_lock<std::shared_mutex> lock(stats_mutex_);
    measurements_.clear();
    active_measurements_.clear();
    measurement_counter_.store(0);
}

void AdvancedProfiler::set_monitoring_enabled(bool enabled) {
    bool was_enabled = monitoring_enabled_.exchange(enabled);
    
    if (enabled && !was_enabled) {
        // Start monitoring thread
        should_stop_monitoring_.store(false);
        monitoring_thread_ = std::make_unique<std::thread>(&AdvancedProfiler::monitoring_loop, this);
    } else if (!enabled && was_enabled) {
        // Stop monitoring thread
        should_stop_monitoring_.store(true);
        if (monitoring_thread_ && monitoring_thread_->joinable()) {
            monitoring_thread_->join();
        }
        monitoring_thread_.reset();
    }
}

bool AdvancedProfiler::export_to_json(const std::string& filename) const {
    std::shared_lock<std::shared_mutex> lock(stats_mutex_);
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    file << "{\n";
    file << "  \"performance_stats\": {\n";
    
    auto all_stats = get_all_stats();
    bool first = true;
    
    for (const auto& [name, stats] : all_stats) {
        if (!first) {
            file << ",\n";
        }
        first = false;
        
        file << "    \"" << name << "\": {\n";
        file << "      \"sample_count\": " << stats.sample_count << ",\n";
        file << "      \"mean_latency_ns\": " << stats.mean_latency_ns << ",\n";
        file << "      \"p50_latency_ns\": " << stats.p50_latency_ns << ",\n";
        file << "      \"p95_latency_ns\": " << stats.p95_latency_ns << ",\n";
        file << "      \"p99_latency_ns\": " << stats.p99_latency_ns << ",\n";
        file << "      \"p999_latency_ns\": " << stats.p999_latency_ns << ",\n";
        file << "      \"mean_throughput_ops_per_sec\": " << stats.mean_throughput_ops_per_sec << "\n";
        file << "    }";
    }
    
    file << "\n  }\n";
    file << "}\n";
    
    return true;
}

bool AdvancedProfiler::export_to_csv(const std::string& filename) const {
    std::shared_lock<std::shared_mutex> lock(stats_mutex_);
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    // CSV header
    file << "operation,sample_count,mean_latency_ns,p50_latency_ns,p95_latency_ns,p99_latency_ns,p999_latency_ns,throughput_ops_per_sec\n";
    
    auto all_stats = get_all_stats();
    for (const auto& [name, stats] : all_stats) {
        file << name << ","
             << stats.sample_count << ","
             << stats.mean_latency_ns << ","
             << stats.p50_latency_ns << ","
             << stats.p95_latency_ns << ","
             << stats.p99_latency_ns << ","
             << stats.p999_latency_ns << ","
             << stats.mean_throughput_ops_per_sec << "\n";
    }
    
    return true;
}

AdvancedProfiler::SystemMetrics AdvancedProfiler::get_system_metrics() const {
    SystemMetrics metrics{};
    
    // Read system metrics from /proc filesystem
    std::ifstream stat_file("/proc/stat");
    std::ifstream meminfo_file("/proc/meminfo");
    std::ifstream loadavg_file("/proc/loadavg");
    
    if (loadavg_file.is_open()) {
        loadavg_file >> metrics.load_average_1min;
    }
    
    // Simplified implementation - would read actual system metrics
    metrics.cpu_usage_percent = 50.0;  // Placeholder
    metrics.memory_usage_percent = 30.0;  // Placeholder
    metrics.memory_bandwidth_mbps = 10000.0;  // Placeholder
    metrics.context_switches_per_sec = 1000;  // Placeholder
    metrics.page_faults_per_sec = 100;  // Placeholder
    metrics.network_rx_mbps = 100;  // Placeholder
    metrics.network_tx_mbps = 100;  // Placeholder
    
    return metrics;
}

bool AdvancedProfiler::is_hardware_counters_available() {
#ifdef __linux__
    // Try to open a simple performance counter
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));
    pe.type = PERF_TYPE_HARDWARE;
    pe.config = PERF_COUNT_HW_CPU_CYCLES;
    pe.size = sizeof(pe);
    
    int fd = syscall(__NR_perf_event_open, &pe, 0, -1, -1, 0);
    if (fd >= 0) {
        close(fd);
        return true;
    }
#endif
    return false;
}

std::vector<std::string> AdvancedProfiler::get_available_counter_types() {
    std::vector<std::string> counters;
    
#ifdef __linux__
    counters = {
        "cpu-cycles",
        "cache-misses",
        "branch-misses",
        "instructions",
        "cache-references"
    };
#endif
    
    return counters;
}

uint64_t AdvancedProfiler::get_cpu_cycles() {
#ifdef __x86_64__
    return __rdtsc();
#else
    // Fallback for other architectures
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
#endif
}

double AdvancedProfiler::cycles_to_nanoseconds(uint64_t cycles) {
    // Assume 3.0 GHz CPU for conversion (would be detected dynamically in real implementation)
    const double cpu_freq_ghz = 3.0;
    return static_cast<double>(cycles) / cpu_freq_ghz;
}

AdvancedProfiler::PerformanceStats 
AdvancedProfiler::calculate_stats(const std::vector<MeasurementPoint>& measurements) const {
    PerformanceStats stats{};
    
    if (measurements.empty()) {
        return stats;
    }
    
    stats.operation_name = measurements[0].name;
    stats.sample_count = measurements.size();
    
    // Extract latencies in nanoseconds
    std::vector<double> latencies;
    latencies.reserve(measurements.size());
    
    for (const auto& point : measurements) {
        double latency_ns = cycles_to_nanoseconds(point.end_cycles - point.start_cycles);
        latencies.push_back(latency_ns);
    }
    
    // Sort for percentile calculation
    std::sort(latencies.begin(), latencies.end());
    
    // Calculate basic statistics
    stats.min_latency_ns = latencies.front();
    stats.max_latency_ns = latencies.back();
    stats.mean_latency_ns = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
    
    // Calculate percentiles
    auto percentiles = calculate_percentiles(latencies, {0.5, 0.95, 0.99, 0.999});
    stats.p50_latency_ns = percentiles[0];
    stats.p95_latency_ns = percentiles[1];
    stats.p99_latency_ns = percentiles[2];
    stats.p999_latency_ns = percentiles[3];
    
    // Calculate standard deviation
    double variance = 0.0;
    for (double latency : latencies) {
        variance += std::pow(latency - stats.mean_latency_ns, 2);
    }
    stats.stddev_latency_ns = std::sqrt(variance / latencies.size());
    
    // Calculate throughput
    if (stats.mean_latency_ns > 0) {
        stats.mean_throughput_ops_per_sec = 1e9 / stats.mean_latency_ns;
        stats.peak_throughput_ops_per_sec = 1e9 / stats.min_latency_ns;
    }
    
    // Calculate hardware counter statistics
    if (!measurements.empty()) {
        uint64_t total_cache_misses = 0;
        uint64_t total_branch_misses = 0;
        
        for (const auto& point : measurements) {
            total_cache_misses += point.cache_misses;
            total_branch_misses += point.branch_misses;
        }
        
        stats.mean_cache_misses_per_op = static_cast<double>(total_cache_misses) / measurements.size();
        stats.mean_branch_misses_per_op = static_cast<double>(total_branch_misses) / measurements.size();
        stats.instructions_per_cycle = 2.5; // Placeholder - would calculate from actual counters
    }
    
    // Estimate resource utilization
    stats.cpu_utilization_percent = 75.0; // Placeholder
    stats.memory_bandwidth_mbps = 8000.0; // Placeholder
    
    return stats;
}

std::vector<double> AdvancedProfiler::calculate_percentiles(
    std::vector<double> values, 
    const std::vector<double>& percentiles) {
    
    if (values.empty()) {
        return std::vector<double>(percentiles.size(), 0.0);
    }
    
    std::sort(values.begin(), values.end());
    std::vector<double> results;
    
    for (double p : percentiles) {
        double index = p * (values.size() - 1);
        size_t lower = static_cast<size_t>(std::floor(index));
        size_t upper = static_cast<size_t>(std::ceil(index));
        
        if (lower == upper) {
            results.push_back(values[lower]);
        } else {
            double weight = index - lower;
            results.push_back(values[lower] * (1 - weight) + values[upper] * weight);
        }
    }
    
    return results;
}

void AdvancedProfiler::monitoring_loop() {
    while (!should_stop_monitoring_.load()) {
        auto stats = get_all_stats();
        
        if (config_.enable_real_time_alerts) {
            check_performance_alerts(stats);
        }
        
        std::this_thread::sleep_for(config_.sampling_interval);
    }
}

void AdvancedProfiler::check_performance_alerts(
    const std::map<std::string, PerformanceStats>& stats) {
    
    for (const auto& [name, stat] : stats) {
        // Check latency alerts
        if (stat.p95_latency_ns > config_.latency_alert_threshold_ms * 1e6) {
            std::cout << "ALERT: High latency detected for " << name 
                      << " - P95: " << (stat.p95_latency_ns / 1e6) << "ms" << std::endl;
        }
        
        // Check throughput alerts
        if (stat.mean_throughput_ops_per_sec < config_.throughput_alert_threshold_ops_per_sec) {
            std::cout << "ALERT: Low throughput detected for " << name
                      << " - " << stat.mean_throughput_ops_per_sec << " ops/sec" << std::endl;
        }
    }
}

#ifdef __linux__
bool AdvancedProfiler::setup_hardware_counters() {
    // Setup basic hardware performance counters
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));
    pe.size = sizeof(pe);
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;
    
    // CPU cycles counter
    pe.type = PERF_TYPE_HARDWARE;
    pe.config = PERF_COUNT_HW_CACHE_MISSES;
    
    int fd = syscall(__NR_perf_event_open, &pe, 0, -1, -1, 0);
    if (fd >= 0) {
        PerfCounter counter;
        counter.fd = fd;
        counter.id = PERF_COUNT_HW_CACHE_MISSES;
        counter.name = "cache-misses";
        perf_counters_.push_back(counter);
        return true;
    }
    
    return false;
}

void AdvancedProfiler::cleanup_hardware_counters() {
    for (auto& counter : perf_counters_) {
        if (counter.fd >= 0) {
            close(counter.fd);
        }
    }
    perf_counters_.clear();
}

uint64_t AdvancedProfiler::read_counter(const PerfCounter& counter) const {
    uint64_t value = 0;
    if (counter.fd >= 0) {
        read(counter.fd, &value, sizeof(value));
    }
    return value;
}
#endif

// BenchmarkRunner Implementation
BenchmarkRunner::BenchmarkRunner(const BenchmarkConfig& config) 
    : config_(config) {}

} // namespace performance
} // namespace slonana