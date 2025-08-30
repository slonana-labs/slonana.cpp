#pragma once

#include "common/types.h"
#include "network/http_client.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace slonana {
namespace validator {

/**
 * RPC node information for snapshot discovery
 */
struct RpcNodeInfo {
    std::string url;
    std::string version;
    uint64_t slot = 0;
    uint64_t snapshot_slot = 0;
    std::string snapshot_hash;
    double latency_ms = 0.0;
    double download_speed_mbps = 0.0;
    bool healthy = false;
    std::chrono::steady_clock::time_point last_checked;
};

/**
 * Snapshot discovery configuration
 */
struct SnapshotFinderConfig {
    // Discovery parameters
    int threads_count = 100;
    std::string rpc_address = "https://api.mainnet-beta.solana.com";
    
    // Snapshot criteria
    uint64_t target_slot = 0;  // 0 = latest
    std::string version_filter;
    std::string wildcard_version;
    uint64_t max_snapshot_age = 1300;  // slots
    
    // Quality thresholds
    double min_download_speed = 60.0;   // MB/s
    double max_download_speed = 0.0;    // MB/s, 0 = unlimited
    double max_latency = 100.0;         // ms
    
    // Network selection
    std::string network = "mainnet-beta";
    
    // Output options
    bool json_output = false;
    bool with_private_rpc = false;
    bool measurement_mode = false;
};

/**
 * Snapshot quality metrics
 */
struct SnapshotQuality {
    double download_speed_mbps = 0.0;
    double latency_ms = 0.0;
    uint64_t age_slots = 0;
    double quality_score = 0.0;  // Combined quality metric
    std::string rpc_url;
    std::string download_url;
};

/**
 * Advanced multi-threaded snapshot finder
 * 
 * Discovers optimal snapshots from multiple RPC endpoints using
 * concurrent testing of latency, download speed, and snapshot quality.
 */
class SnapshotFinder {
public:
    explicit SnapshotFinder(const SnapshotFinderConfig& config = {});
    ~SnapshotFinder();

    // Main discovery operations
    common::Result<std::vector<RpcNodeInfo>> discover_rpc_nodes();
    common::Result<std::vector<SnapshotQuality>> find_best_snapshots();
    common::Result<SnapshotQuality> find_single_best_snapshot();
    
    // Download operations
    common::Result<bool> download_snapshot_from_best_source(
        const std::string& output_directory,
        std::string& output_path_out,
        std::function<void(const std::string&, uint64_t, uint64_t)> progress_callback = nullptr);
    
    // RPC testing
    common::Result<RpcNodeInfo> test_rpc_node(const std::string& rpc_url);
    double measure_rpc_latency(const std::string& rpc_url);
    double measure_download_speed(const std::string& snapshot_url, size_t test_bytes = 1024 * 1024);
    
    // Configuration
    void set_config(const SnapshotFinderConfig& config) { config_ = config; }
    const SnapshotFinderConfig& get_config() const { return config_; }
    
    // Progress callbacks
    void set_discovery_progress_callback(std::function<void(int, int)> callback) {
        discovery_progress_callback_ = std::move(callback);
    }
    
    // Static utilities
    static std::vector<std::string> get_default_rpc_endpoints(const std::string& network = "mainnet-beta");
    static std::vector<std::string> get_snapshot_mirror_urls(const std::string& network = "mainnet-beta");
    static bool is_valid_snapshot_slot(uint64_t slot, uint64_t current_slot, uint64_t max_age);
    static double calculate_quality_score(const SnapshotQuality& quality);

private:
    SnapshotFinderConfig config_;
    std::unique_ptr<network::HttpClient> http_client_;
    
    // Threading
    std::vector<std::thread> worker_threads_;
    std::mutex results_mutex_;
    std::condition_variable cv_;
    std::atomic<bool> shutdown_requested_{false};
    std::atomic<int> completed_tests_{0};
    std::atomic<int> total_tests_{0};
    
    // Results storage
    std::vector<RpcNodeInfo> discovered_nodes_;
    std::vector<SnapshotQuality> snapshot_qualities_;
    
    // Progress tracking
    std::function<void(int, int)> discovery_progress_callback_;
    
    // Helper methods
    void worker_thread_function(
        const std::vector<std::string>& rpc_urls,
        size_t start_index,
        size_t end_index);
    
    common::Result<std::vector<std::string>> discover_cluster_nodes(const std::vector<std::string>& seed_endpoints);
    std::vector<std::string> extract_rpc_endpoints_from_cluster_response(const std::string& response);
    
    common::Result<uint64_t> get_current_slot(const std::string& rpc_url);
    common::Result<uint64_t> get_snapshot_slot(const std::string& rpc_url);
    std::string build_snapshot_download_url(const std::string& rpc_url, uint64_t slot);
    
    bool meets_version_criteria(const std::string& version) const;
    bool meets_quality_criteria(const SnapshotQuality& quality) const;
    
    void report_discovery_progress();
};

/**
 * CLI integration for snapshot finder
 */
class SnapshotFinderCli {
public:
    static int run_find_command(int argc, char* argv[]);
    static int run_download_command(int argc, char* argv[]);
    static int run_test_rpc_command(int argc, char* argv[]);
    
    static void print_find_usage();
    static void print_download_usage();
    static void print_test_rpc_usage();
    
private:
    static SnapshotFinderConfig parse_config_from_args(int argc, char* argv[]);
    static void print_rpc_nodes(const std::vector<RpcNodeInfo>& nodes, bool json_format = false);
    static void print_snapshot_qualities(const std::vector<SnapshotQuality>& qualities, bool json_format = false);
};

} // namespace validator
} // namespace slonana