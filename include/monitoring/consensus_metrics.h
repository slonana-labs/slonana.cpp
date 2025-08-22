#pragma once

#include "monitoring/metrics.h"
#include <memory>
#include <chrono>

namespace slonana {
namespace monitoring {

/**
 * @brief Consensus-specific metrics and timing instrumentation
 * 
 * This class provides comprehensive timing and performance metrics
 * for all consensus operations, enabling detailed tracing and
 * performance analysis of the validator's consensus participation.
 */
class ConsensusMetrics {
public:
    ConsensusMetrics();
    ~ConsensusMetrics() = default;

    // Block processing metrics
    void record_block_validation_time(double seconds);
    void record_block_processing_time(double seconds);
    void record_block_storage_time(double seconds);
    void increment_blocks_processed();
    void increment_blocks_rejected();

    // Vote processing metrics  
    void record_vote_processing_time(double seconds);
    void record_vote_verification_time(double seconds);
    void increment_votes_processed();
    void increment_votes_rejected();

    // Fork choice metrics
    void record_fork_choice_time(double seconds);
    void record_fork_weight_calculation_time(double seconds);
    void set_active_forks_count(int64_t count);
    void set_current_slot(int64_t slot);

    // Consensus performance metrics
    void record_consensus_round_time(double seconds);
    void record_leader_schedule_time(double seconds);
    void record_signature_verification_time(double seconds);
    void record_transaction_verification_time(double seconds);

    // Proof of History metrics
    void record_poh_tick_time(double seconds);
    void record_poh_verification_time(double seconds);
    void record_poh_hash_time(double seconds);
    void increment_poh_ticks_generated();
    void set_poh_sequence_number(int64_t sequence);
    void set_poh_current_slot(int64_t slot);

    // Network consensus metrics
    void increment_consensus_messages_sent();
    void increment_consensus_messages_received();
    void record_consensus_network_latency(double seconds);

    // Timing utility for automatic measurement
    class Timer {
    public:
        Timer(std::shared_ptr<monitoring::IHistogram> histogram);
        ~Timer();
        double stop();

    private:
        std::shared_ptr<monitoring::IHistogram> histogram_;
        std::chrono::steady_clock::time_point start_time_;
        bool stopped_;
    };

    // Convenience methods for creating timers
    Timer create_block_validation_timer();
    Timer create_vote_processing_timer();
    Timer create_fork_choice_timer();
    Timer create_consensus_round_timer();
    Timer create_poh_tick_timer();
    Timer create_poh_verification_timer();

    // Get specific metrics for external use
    std::shared_ptr<monitoring::IHistogram> get_block_validation_histogram();
    std::shared_ptr<monitoring::IHistogram> get_vote_processing_histogram();
    std::shared_ptr<monitoring::IHistogram> get_fork_choice_histogram();
    std::shared_ptr<monitoring::IHistogram> get_consensus_round_histogram();
    std::shared_ptr<monitoring::ICounter> get_blocks_processed_counter();

private:
    // Block processing metrics
    std::shared_ptr<monitoring::IHistogram> block_validation_time_;
    std::shared_ptr<monitoring::IHistogram> block_processing_time_;
    std::shared_ptr<monitoring::IHistogram> block_storage_time_;
    std::shared_ptr<monitoring::ICounter> blocks_processed_;
    std::shared_ptr<monitoring::ICounter> blocks_rejected_;

    // Vote processing metrics
    std::shared_ptr<monitoring::IHistogram> vote_processing_time_;
    std::shared_ptr<monitoring::IHistogram> vote_verification_time_;
    std::shared_ptr<monitoring::ICounter> votes_processed_;
    std::shared_ptr<monitoring::ICounter> votes_rejected_;

    // Fork choice metrics
    std::shared_ptr<monitoring::IHistogram> fork_choice_time_;
    std::shared_ptr<monitoring::IHistogram> fork_weight_calculation_time_;
    std::shared_ptr<monitoring::IGauge> active_forks_count_;
    std::shared_ptr<monitoring::IGauge> current_slot_;

    // Consensus performance metrics
    std::shared_ptr<monitoring::IHistogram> consensus_round_time_;
    std::shared_ptr<monitoring::IHistogram> leader_schedule_time_;
    std::shared_ptr<monitoring::IHistogram> signature_verification_time_;
    std::shared_ptr<monitoring::IHistogram> transaction_verification_time_;

    // Network consensus metrics
    std::shared_ptr<monitoring::ICounter> consensus_messages_sent_;
    std::shared_ptr<monitoring::ICounter> consensus_messages_received_;
    std::shared_ptr<monitoring::IHistogram> consensus_network_latency_;

    // Proof of History metrics
    std::shared_ptr<monitoring::IHistogram> poh_tick_time_;
    std::shared_ptr<monitoring::IHistogram> poh_verification_time_;
    std::shared_ptr<monitoring::IHistogram> poh_hash_time_;
    std::shared_ptr<monitoring::ICounter> poh_ticks_generated_;
    std::shared_ptr<monitoring::IGauge> poh_sequence_number_;
    std::shared_ptr<monitoring::IGauge> poh_current_slot_;
};

/**
 * @brief Global consensus metrics instance for easy access
 */
class GlobalConsensusMetrics {
public:
    static ConsensusMetrics& instance();
    static void initialize();
    static void shutdown();

private:
    static std::unique_ptr<ConsensusMetrics> instance_;
    static std::mutex instance_mutex_;
};

/**
 * @brief Macros for convenient timing measurement
 */
#define CONSENSUS_TIMER_BLOCK_VALIDATION() \
    auto _timer = slonana::monitoring::GlobalConsensusMetrics::instance().create_block_validation_timer()

#define CONSENSUS_TIMER_VOTE_PROCESSING() \
    auto _timer = slonana::monitoring::GlobalConsensusMetrics::instance().create_vote_processing_timer()

#define CONSENSUS_TIMER_FORK_CHOICE() \
    auto _timer = slonana::monitoring::GlobalConsensusMetrics::instance().create_fork_choice_timer()

#define CONSENSUS_TIMER_ROUND() \
    auto _timer = slonana::monitoring::GlobalConsensusMetrics::instance().create_consensus_round_timer()

#define CONSENSUS_TIMER_POH_TICK() \
    auto _timer = slonana::monitoring::GlobalConsensusMetrics::instance().create_poh_tick_timer()

#define CONSENSUS_TIMER_POH_VERIFICATION() \
    auto _timer = slonana::monitoring::GlobalConsensusMetrics::instance().create_poh_verification_timer()

/**
 * @brief Consensus performance analyzer for detailed timing analysis
 */
class ConsensusPerformanceAnalyzer {
public:
    struct PerformanceReport {
        double avg_block_validation_time_ms;
        double avg_vote_processing_time_ms;
        double avg_fork_choice_time_ms;
        double avg_consensus_round_time_ms;
        double avg_poh_tick_time_ms;
        double avg_poh_verification_time_ms;
        
        int64_t total_blocks_processed;
        int64_t total_votes_processed;
        int64_t total_poh_ticks_generated;
        double blocks_per_second;
        double votes_per_second;
        double poh_ticks_per_second;
        
        std::chrono::system_clock::time_point report_timestamp;
        std::chrono::milliseconds analysis_period;
    };

    ConsensusPerformanceAnalyzer();
    
    /**
     * @brief Generate performance report from current metrics
     * @param analysis_period time period for rate calculations
     * @return performance analysis report
     */
    PerformanceReport generate_report(std::chrono::milliseconds analysis_period = std::chrono::minutes(5));

    /**
     * @brief Export performance report as JSON
     * @param report performance report to export
     * @return JSON string representation
     */
    std::string export_report_json(const PerformanceReport& report);

    /**
     * @brief Check if consensus performance meets targets
     * @param report performance report to evaluate
     * @return true if performance is within acceptable ranges
     */
    bool validate_performance_targets(const PerformanceReport& report);

private:
    std::chrono::system_clock::time_point last_report_time_;
    int64_t last_blocks_count_;
    int64_t last_votes_count_;
};

} // namespace monitoring
} // namespace slonana