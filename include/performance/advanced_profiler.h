#pragma once

#include <chrono>
#include <vector>
#include <map>
#include <atomic>
#include <thread>
#include <memory>
#include <string>
#include <fstream>

#ifdef __linux__
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace slonana {
namespace performance {

/**
 * Advanced Performance Profiler with Hardware Counter Integration
 * 
 * Provides cycle-accurate performance measurement using hardware performance
 * counters for precise bottleneck identification in ultra-high-throughput scenarios.
 * 
 * Key Features:
 * - CPU cycle-level timing precision (sub-nanosecond accuracy)
 * - Hardware performance counter integration (cache misses, branch mispredictions)
 * - Statistical analysis with latency distribution (P50, P95, P99, P99.9)
 * - Real-time performance monitoring and alerting
 * - Lock-free measurement to minimize measurement overhead
 */
class AdvancedProfiler {
public:
    /**
     * Performance measurement point
     */
    struct MeasurementPoint {
        std::string name;
        uint64_t start_cycles;
        uint64_t end_cycles;
        uint64_t cache_misses;
        uint64_t branch_misses;
        std::chrono::high_resolution_clock::time_point timestamp;
    };
    
    /**
     * Performance statistics for a measured operation
     */
    struct PerformanceStats {
        std::string operation_name;
        size_t sample_count;
        
        // Timing statistics (in nanoseconds)
        double mean_latency_ns;
        double min_latency_ns;
        double max_latency_ns;
        double p50_latency_ns;
        double p95_latency_ns;
        double p99_latency_ns;
        double p999_latency_ns;
        double stddev_latency_ns;
        
        // Throughput statistics
        double mean_throughput_ops_per_sec;
        double peak_throughput_ops_per_sec;
        
        // Hardware performance counters
        double mean_cache_misses_per_op;
        double mean_branch_misses_per_op;
        double instructions_per_cycle;
        
        // Resource utilization
        double cpu_utilization_percent;
        double memory_bandwidth_mbps;
    };
    
    /**
     * Real-time performance monitor configuration
     */
    struct MonitorConfig {
        std::chrono::milliseconds sampling_interval{100};
        double latency_alert_threshold_ms = 10.0;
        double throughput_alert_threshold_ops_per_sec = 1000.0;
        bool enable_hardware_counters = true;
        bool enable_real_time_alerts = false;
        std::string log_file_path;
    };
    
    explicit AdvancedProfiler(const MonitorConfig& config = MonitorConfig{});
    ~AdvancedProfiler();
    
    /**
     * RAII measurement scope for automatic timing
     */
    class MeasurementScope {
    public:
        MeasurementScope(AdvancedProfiler& profiler, const std::string& operation_name);
        ~MeasurementScope();
        
        // No copying/moving to avoid measurement corruption
        MeasurementScope(const MeasurementScope&) = delete;
        MeasurementScope& operator=(const MeasurementScope&) = delete;
        MeasurementScope(MeasurementScope&&) = delete;
        MeasurementScope& operator=(MeasurementScope&&) = delete;
        
    private:
        AdvancedProfiler& profiler_;
        std::string operation_name_;
        uint64_t start_cycles_;
        uint64_t start_cache_misses_;
        uint64_t start_branch_misses_;
        std::chrono::high_resolution_clock::time_point start_time_;
    };
    
    /**
     * Start measuring an operation
     * @param operation_name Name of the operation being measured
     * @return Measurement ID for ending the measurement
     */
    uint64_t start_measurement(const std::string& operation_name);
    
    /**
     * End a measurement
     * @param measurement_id ID returned from start_measurement
     */
    void end_measurement(uint64_t measurement_id);
    
    /**
     * Record a completed measurement
     * @param operation_name Name of the operation
     * @param latency_ns Latency in nanoseconds
     * @param cache_misses Number of cache misses
     * @param branch_misses Number of branch mispredictions
     */
    void record_measurement(
        const std::string& operation_name,
        double latency_ns,
        uint64_t cache_misses = 0,
        uint64_t branch_misses = 0
    );
    
    /**
     * Get performance statistics for an operation
     * @param operation_name Name of the operation
     * @return Performance statistics, or nullopt if no data available
     */
    std::optional<PerformanceStats> get_stats(const std::string& operation_name) const;
    
    /**
     * Get performance statistics for all measured operations
     * @return Map of operation name to performance statistics
     */
    std::map<std::string, PerformanceStats> get_all_stats() const;
    
    /**
     * Reset all performance statistics
     */
    void reset_stats();
    
    /**
     * Enable/disable real-time performance monitoring
     * @param enabled Whether to enable monitoring
     */
    void set_monitoring_enabled(bool enabled);
    
    /**
     * Export performance data to JSON format
     * @param filename Output file path
     * @return True if export successful, false otherwise
     */
    bool export_to_json(const std::string& filename) const;
    
    /**
     * Export performance data to CSV format
     * @param filename Output file path
     * @return True if export successful, false otherwise
     */
    bool export_to_csv(const std::string& filename) const;
    
    /**
     * Get current system performance metrics
     */
    struct SystemMetrics {
        double cpu_usage_percent;
        double memory_usage_percent;
        double memory_bandwidth_mbps;
        uint64_t context_switches_per_sec;
        uint64_t page_faults_per_sec;
        double load_average_1min;
        uint64_t network_rx_mbps;
        uint64_t network_tx_mbps;
    };
    
    SystemMetrics get_system_metrics() const;
    
    /**
     * Hardware performance counter utilities
     */
    static bool is_hardware_counters_available();
    static std::vector<std::string> get_available_counter_types();

private:
    MonitorConfig config_;
    mutable std::shared_mutex stats_mutex_;
    
    // Per-operation performance data
    std::map<std::string, std::vector<MeasurementPoint>> measurements_;
    
    // Active measurements
    std::atomic<uint64_t> measurement_counter_{0};
    std::map<uint64_t, MeasurementPoint> active_measurements_;
    
    // Real-time monitoring
    std::atomic<bool> monitoring_enabled_{false};
    std::unique_ptr<std::thread> monitoring_thread_;
    std::atomic<bool> should_stop_monitoring_{false};
    
    // Hardware performance counters
#ifdef __linux__
    struct PerfCounter {
        int fd;
        uint64_t id;
        std::string name;
    };
    
    std::vector<PerfCounter> perf_counters_;
    bool setup_hardware_counters();
    void cleanup_hardware_counters();
    uint64_t read_counter(const PerfCounter& counter) const;
#endif
    
    // Utility functions
    static uint64_t get_cpu_cycles();
    static double cycles_to_nanoseconds(uint64_t cycles);
    
    // Statistical analysis
    PerformanceStats calculate_stats(const std::vector<MeasurementPoint>& measurements) const;
    static std::vector<double> calculate_percentiles(std::vector<double> values, 
                                                   const std::vector<double>& percentiles);
    
    // Real-time monitoring implementation
    void monitoring_loop();
    void check_performance_alerts(const std::map<std::string, PerformanceStats>& stats);
};

/**
 * Macro for convenient RAII measurement
 */
#define PROFILE_SCOPE(profiler, name) \
    ::slonana::performance::AdvancedProfiler::MeasurementScope profile_scope_##__LINE__(profiler, name)

/**
 * Benchmark Runner for Systematic Performance Testing
 * 
 * Provides standardized benchmarking infrastructure for consistent
 * performance measurement across different system configurations.
 */
class BenchmarkRunner {
public:
    /**
     * Benchmark configuration
     */
    struct BenchmarkConfig {
        size_t warmup_iterations = 100;
        size_t measurement_iterations = 1000;
        std::chrono::milliseconds max_duration{60000}; // 60 seconds
        double acceptable_variance_percent = 5.0;
        bool enable_outlier_detection = true;
        double outlier_threshold_stddev = 3.0;
    };
    
    /**
     * Benchmark result
     */
    struct BenchmarkResult {
        std::string benchmark_name;
        size_t iterations_completed;
        std::chrono::milliseconds total_duration;
        
        // Performance metrics
        double mean_latency_ns;
        double median_latency_ns;
        double p95_latency_ns;
        double p99_latency_ns;
        double min_latency_ns;
        double max_latency_ns;
        double stddev_latency_ns;
        double variance_percent;
        
        // Throughput metrics
        double operations_per_second;
        double peak_ops_per_second;
        
        // Quality indicators
        bool measurement_stable;
        size_t outliers_detected;
        double confidence_interval_95_percent;
    };
    
    explicit BenchmarkRunner(const BenchmarkConfig& config = BenchmarkConfig{});
    
    /**
     * Run a benchmark function
     * @param name Benchmark name
     * @param benchmark_func Function to benchmark (should return operation count)
     * @return Benchmark results
     */
    template<typename Func>
    BenchmarkResult run_benchmark(const std::string& name, Func&& benchmark_func);
    
    /**
     * Run multiple benchmarks and generate comparison report
     * @param benchmarks Map of benchmark name to benchmark function
     * @return Comparison results
     */
    template<typename BenchmarkMap>
    std::vector<BenchmarkResult> run_benchmark_suite(const BenchmarkMap& benchmarks);
    
    /**
     * Compare two benchmark results
     * @param baseline Baseline benchmark result
     * @param comparison Comparison benchmark result
     * @return Performance improvement factor (>1.0 means improvement)
     */
    static double compare_benchmarks(const BenchmarkResult& baseline, 
                                   const BenchmarkResult& comparison);

private:
    BenchmarkConfig config_;
    AdvancedProfiler profiler_;
    
    // Statistical analysis
    static bool is_measurement_stable(const std::vector<double>& measurements, double acceptable_variance);
    static std::vector<double> remove_outliers(std::vector<double> measurements, double threshold_stddev);
    static double calculate_confidence_interval(const std::vector<double>& measurements, double confidence_level);
};

} // namespace performance
} // namespace slonana