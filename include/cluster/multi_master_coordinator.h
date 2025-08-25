#pragma once

#include "common/types.h"
#include "cluster/multi_master_manager.h"
#include "network/topology_manager.h"
#include "network/distributed_load_balancer.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <functional>

namespace slonana {
namespace cluster {

using namespace slonana::network;

// Multi-master coordination structures
struct MasterCoordinationEvent {
    std::string event_id;
    std::string event_type;  // "election", "failover", "promotion", "demotion", "sync"
    std::string source_master;
    std::string target_master;
    std::unordered_map<std::string, std::string> parameters;
    std::chrono::system_clock::time_point timestamp;
    bool is_processed;
};

struct GlobalConsensusState {
    std::string global_leader;
    std::unordered_map<std::string, MasterRole> master_assignments;
    std::unordered_map<std::string, std::string> region_leaders;
    std::unordered_map<uint32_t, std::string> shard_masters;
    uint64_t consensus_term;
    uint64_t state_version;
    std::chrono::system_clock::time_point last_update;
};

struct CrossMasterSyncRequest {
    std::string request_id;
    std::string source_master;
    std::string sync_type;  // "ledger", "state", "config", "full"
    uint64_t start_slot;
    uint64_t end_slot;
    std::vector<uint8_t> data_hash;
    bool is_urgent;
};

struct MasterPerformanceMetrics {
    std::string master_id;
    uint64_t transactions_processed;
    uint64_t rpc_requests_handled;
    uint64_t consensus_operations;
    std::chrono::milliseconds average_response_time;
    double cpu_utilization;
    double memory_utilization;
    uint64_t network_bandwidth_used;
    double error_rate;
    std::chrono::system_clock::time_point last_update;
};

struct MultiMasterConfiguration {
    uint32_t max_masters_per_region;
    uint32_t max_masters_per_shard;
    uint32_t min_masters_for_consensus;
    uint32_t master_election_timeout_ms;
    uint32_t heartbeat_interval_ms;
    uint32_t sync_interval_ms;
    uint32_t failover_timeout_ms;
    bool enable_automatic_failover;
    bool enable_cross_region_sync;
    bool enable_load_balancing;
    std::string global_coordination_strategy;  // "centralized", "distributed", "hybrid"
};

class MultiMasterCoordinator {
public:
    MultiMasterCoordinator(const std::string& coordinator_id, const ValidatorConfig& config);
    ~MultiMasterCoordinator();

    // Core management
    bool start();
    void stop();
    bool is_running() const { return running_.load(); }

    // Component integration
    void set_multi_master_manager(std::shared_ptr<MultiMasterManager> manager) { multi_master_manager_ = manager; }
    void set_topology_manager(std::shared_ptr<NetworkTopologyManager> manager) { topology_manager_ = manager; }
    void set_load_balancer(std::shared_ptr<DistributedLoadBalancer> balancer) { load_balancer_ = balancer; }

    // Global consensus management
    bool initiate_global_consensus();
    bool update_global_state(const GlobalConsensusState& state);
    GlobalConsensusState get_global_consensus_state() const;
    bool validate_global_state_consistency();

    // Master coordination
    bool coordinate_master_election(MasterRole role, const std::string& region = "");
    bool coordinate_master_promotion(const std::string& node_id, MasterRole role);
    bool coordinate_master_demotion(const std::string& master_id);
    bool coordinate_failover(const std::string& failed_master);

    // Cross-master synchronization
    bool initiate_cross_master_sync(const CrossMasterSyncRequest& request);
    bool handle_sync_request(const CrossMasterSyncRequest& request);
    std::vector<CrossMasterSyncRequest> get_pending_sync_requests() const;
    bool complete_sync_request(const std::string& request_id, bool success);

    // Load balancing coordination
    bool coordinate_load_rebalancing();
    bool redistribute_traffic(const std::string& source_master, const std::string& target_master);
    bool update_traffic_weights(const std::unordered_map<std::string, uint32_t>& weights);

    // Performance monitoring
    bool update_master_performance(const MasterPerformanceMetrics& metrics);
    std::vector<MasterPerformanceMetrics> get_master_performance_metrics() const;
    std::string identify_performance_bottleneck() const;
    bool optimize_master_allocation();

    // Regional coordination
    bool setup_regional_coordination(const std::string& region);
    bool coordinate_cross_region_operations(const std::string& source_region, const std::string& target_region);
    std::vector<std::string> get_regional_leaders() const;

    // Event handling
    bool process_coordination_event(const MasterCoordinationEvent& event);
    std::vector<MasterCoordinationEvent> get_pending_events() const;
    bool broadcast_coordination_event(const MasterCoordinationEvent& event);

    // Configuration and management
    bool update_configuration(const MultiMasterConfiguration& config);
    MultiMasterConfiguration get_configuration() const;
    bool validate_configuration() const;

    // Health and status
    bool perform_health_check();
    bool is_coordination_healthy() const;
    std::unordered_map<std::string, std::string> get_coordination_status() const;

    // Statistics and monitoring
    struct CoordinatorStats {
        uint32_t total_masters;
        uint32_t active_masters;
        uint32_t masters_by_region;
        uint64_t total_coordination_events;
        uint64_t successful_failovers;
        uint64_t sync_operations_completed;
        double average_consensus_time_ms;
        double coordination_efficiency;
        std::chrono::system_clock::time_point last_global_consensus;
    };

    CoordinatorStats get_statistics() const;

    // Event callbacks
    using CoordinationEventCallback = std::function<void(const std::string& event_type, const std::string& details)>;
    void set_event_callback(CoordinationEventCallback callback) { event_callback_ = callback; }

private:
    std::string coordinator_id_;
    ValidatorConfig config_;
    std::atomic<bool> running_;

    // Component managers
    std::shared_ptr<MultiMasterManager> multi_master_manager_;
    std::shared_ptr<NetworkTopologyManager> topology_manager_;
    std::shared_ptr<DistributedLoadBalancer> load_balancer_;

    // Global state
    mutable std::mutex global_state_mutex_;
    GlobalConsensusState global_consensus_state_;
    MultiMasterConfiguration coordination_config_;

    // Event processing
    mutable std::mutex events_mutex_;
    std::vector<MasterCoordinationEvent> pending_events_;
    std::unordered_map<std::string, bool> processed_events_;

    // Synchronization
    mutable std::mutex sync_mutex_;
    std::vector<CrossMasterSyncRequest> pending_sync_requests_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> sync_timeouts_;

    // Performance tracking
    mutable std::mutex performance_mutex_;
    std::unordered_map<std::string, MasterPerformanceMetrics> performance_metrics_;

    // Statistics
    mutable std::mutex stats_mutex_;
    CoordinatorStats current_stats_;

    // Background threads
    std::thread global_consensus_thread_;
    std::thread coordination_thread_;
    std::thread sync_coordinator_thread_;
    std::thread performance_monitor_thread_;

    // Event callback
    CoordinationEventCallback event_callback_;

    // Private methods
    void global_consensus_loop();
    void coordination_loop();
    void sync_coordinator_loop();
    void performance_monitor_loop();

    bool elect_global_leader();
    bool update_master_assignments();
    bool reconcile_regional_leaders();
    bool balance_shard_assignments();

    std::string select_best_master_candidate(MasterRole role, const std::string& region = "") const;
    bool validate_master_capacity(const std::string& master_id, MasterRole role) const;
    double calculate_master_fitness(const std::string& master_id, MasterRole role) const;

    bool synchronize_masters(const std::vector<std::string>& masters);
    bool validate_sync_consistency(const std::vector<std::string>& masters);
    bool resolve_sync_conflicts(const std::vector<std::string>& masters);

    void update_coordination_statistics();
    void cleanup_expired_events();
    void cleanup_expired_sync_requests();

    std::string generate_event_id() const;
    std::string generate_sync_request_id() const;

    void notify_coordination_event(const std::string& event_type, const std::string& details = "");
};

// Utility functions
const char* master_coordination_event_type_to_string(const std::string& event_type);
std::string create_coordination_event_details(const std::unordered_map<std::string, std::string>& parameters);

} // namespace cluster
} // namespace slonana