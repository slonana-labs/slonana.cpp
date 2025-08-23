#include "monitoring/consensus_metrics.h"
#include "monitoring/metrics.h"
#include <sstream>
#include <iomanip>

namespace slonana {
namespace monitoring {

ConsensusMetrics::ConsensusMetrics() {
    auto& registry = GlobalMetrics::registry();

    // Block processing metrics
    block_validation_time_ = registry.histogram(
        "consensus_block_validation_duration_seconds",
        "Time spent validating blocks",
        {0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0}
    );

    block_processing_time_ = registry.histogram(
        "consensus_block_processing_duration_seconds", 
        "Time spent processing blocks end-to-end",
        {0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0}
    );

    block_storage_time_ = registry.histogram(
        "consensus_block_storage_duration_seconds",
        "Time spent storing blocks in ledger", 
        {0.0001, 0.0005, 0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5}
    );

    blocks_processed_ = registry.counter(
        "consensus_blocks_processed_total",
        "Total number of blocks processed by consensus"
    );

    blocks_rejected_ = registry.counter(
        "consensus_blocks_rejected_total", 
        "Total number of blocks rejected during validation"
    );

    // Vote processing metrics
    vote_processing_time_ = registry.histogram(
        "consensus_vote_processing_duration_seconds",
        "Time spent processing consensus votes",
        {0.0001, 0.0005, 0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5}
    );

    vote_verification_time_ = registry.histogram(
        "consensus_vote_verification_duration_seconds", 
        "Time spent verifying vote signatures and validity",
        {0.0001, 0.0005, 0.001, 0.005, 0.01, 0.025, 0.05, 0.1}
    );

    votes_processed_ = registry.counter(
        "consensus_votes_processed_total",
        "Total number of consensus votes processed"
    );

    votes_rejected_ = registry.counter(
        "consensus_votes_rejected_total",
        "Total number of consensus votes rejected"
    );

    // Fork choice metrics
    fork_choice_time_ = registry.histogram(
        "consensus_fork_choice_duration_seconds",
        "Time spent in fork choice algorithm",
        {0.0001, 0.0005, 0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5}
    );

    fork_weight_calculation_time_ = registry.histogram(
        "consensus_fork_weight_calculation_duration_seconds",
        "Time spent calculating fork weights",
        {0.0001, 0.0005, 0.001, 0.005, 0.01, 0.025, 0.05, 0.1}
    );

    active_forks_count_ = registry.gauge(
        "consensus_active_forks_count",
        "Number of active forks being tracked"
    );

    current_slot_ = registry.gauge(
        "consensus_current_slot",
        "Current consensus slot number"
    );

    // Consensus performance metrics
    consensus_round_time_ = registry.histogram(
        "consensus_round_duration_seconds",
        "Time for complete consensus round (proposal to finalization)",
        {0.1, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0}
    );

    leader_schedule_time_ = registry.histogram(
        "consensus_leader_schedule_duration_seconds",
        "Time spent calculating leader schedule",
        {0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5}
    );

    signature_verification_time_ = registry.histogram(
        "consensus_signature_verification_duration_seconds",
        "Time spent verifying cryptographic signatures",
        {0.0001, 0.0005, 0.001, 0.005, 0.01, 0.025, 0.05, 0.1}
    );

    transaction_verification_time_ = registry.histogram(
        "consensus_transaction_verification_duration_seconds",
        "Time spent verifying transactions in blocks",
        {0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0}
    );

    // Network consensus metrics  
    consensus_messages_sent_ = registry.counter(
        "consensus_messages_sent_total",
        "Total consensus messages sent to peers"
    );

    consensus_messages_received_ = registry.counter(
        "consensus_messages_received_total", 
        "Total consensus messages received from peers"
    );

    consensus_network_latency_ = registry.histogram(
        "consensus_network_latency_seconds",
        "Network latency for consensus message propagation",
        {0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0}
    );

    // Proof of History metrics
    poh_tick_time_ = registry.histogram(
        "consensus_poh_tick_duration_seconds",
        "Time spent generating PoH ticks",
        {0.0001, 0.0002, 0.0004, 0.0008, 0.001, 0.002, 0.004, 0.008, 0.01, 0.02}
    );

    poh_verification_time_ = registry.histogram(
        "consensus_poh_verification_duration_seconds",
        "Time spent verifying PoH sequences",
        {0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0}
    );

    poh_hash_time_ = registry.histogram(
        "consensus_poh_hash_duration_seconds",
        "Time spent computing individual PoH hashes",
        {0.00001, 0.00005, 0.0001, 0.0005, 0.001, 0.005, 0.01, 0.05, 0.1}
    );

    poh_ticks_generated_ = registry.counter(
        "consensus_poh_ticks_generated_total",
        "Total number of PoH ticks generated"
    );

    poh_sequence_number_ = registry.gauge(
        "consensus_poh_sequence_number",
        "Current PoH sequence number"
    );

    poh_current_slot_ = registry.gauge(
        "consensus_poh_current_slot",
        "Current slot number from PoH"
    );
}

// Block processing metrics
void ConsensusMetrics::record_block_validation_time(double seconds) {
    block_validation_time_->observe(seconds);
}

void ConsensusMetrics::record_block_processing_time(double seconds) {
    block_processing_time_->observe(seconds);
}

void ConsensusMetrics::record_block_storage_time(double seconds) {
    block_storage_time_->observe(seconds);
}

void ConsensusMetrics::increment_blocks_processed() {
    blocks_processed_->increment();
}

void ConsensusMetrics::increment_blocks_rejected() {
    blocks_rejected_->increment();
}

// Vote processing metrics
void ConsensusMetrics::record_vote_processing_time(double seconds) {
    vote_processing_time_->observe(seconds);
}

void ConsensusMetrics::record_vote_verification_time(double seconds) {
    vote_verification_time_->observe(seconds);
}

void ConsensusMetrics::increment_votes_processed() {
    votes_processed_->increment();
}

void ConsensusMetrics::increment_votes_rejected() {
    votes_rejected_->increment();
}

// Fork choice metrics
void ConsensusMetrics::record_fork_choice_time(double seconds) {
    fork_choice_time_->observe(seconds);
}

void ConsensusMetrics::record_fork_weight_calculation_time(double seconds) {
    fork_weight_calculation_time_->observe(seconds);
}

void ConsensusMetrics::set_active_forks_count(int64_t count) {
    active_forks_count_->set(static_cast<double>(count));
}

void ConsensusMetrics::set_current_slot(int64_t slot) {
    current_slot_->set(static_cast<double>(slot));
}

// Consensus performance metrics
void ConsensusMetrics::record_consensus_round_time(double seconds) {
    consensus_round_time_->observe(seconds);
}

void ConsensusMetrics::record_leader_schedule_time(double seconds) {
    leader_schedule_time_->observe(seconds);
}

void ConsensusMetrics::record_signature_verification_time(double seconds) {
    signature_verification_time_->observe(seconds);
}

void ConsensusMetrics::record_transaction_verification_time(double seconds) {
    transaction_verification_time_->observe(seconds);
}

// Proof of History metrics
void ConsensusMetrics::record_poh_tick_time(double seconds) {
    poh_tick_time_->observe(seconds);
}

void ConsensusMetrics::record_poh_verification_time(double seconds) {
    poh_verification_time_->observe(seconds);
}

void ConsensusMetrics::record_poh_hash_time(double seconds) {
    poh_hash_time_->observe(seconds);
}

void ConsensusMetrics::increment_poh_ticks_generated() {
    poh_ticks_generated_->increment();
}

void ConsensusMetrics::set_poh_sequence_number(int64_t sequence) {
    poh_sequence_number_->set(static_cast<double>(sequence));
}

void ConsensusMetrics::set_poh_current_slot(int64_t slot) {
    poh_current_slot_->set(static_cast<double>(slot));
}

// Network consensus metrics
void ConsensusMetrics::increment_consensus_messages_sent() {
    consensus_messages_sent_->increment();
}

void ConsensusMetrics::increment_consensus_messages_received() {
    consensus_messages_received_->increment();
}

void ConsensusMetrics::record_consensus_network_latency(double seconds) {
    consensus_network_latency_->observe(seconds);
}

// Timer implementation
ConsensusMetrics::Timer::Timer(std::shared_ptr<monitoring::IHistogram> histogram)
    : histogram_(histogram), start_time_(std::chrono::steady_clock::now()), stopped_(false) {
}

ConsensusMetrics::Timer::~Timer() {
    if (!stopped_ && histogram_) {
        stop();
    }
}

double ConsensusMetrics::Timer::stop() {
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

// Timer factory methods
ConsensusMetrics::Timer ConsensusMetrics::create_block_validation_timer() {
    return Timer(block_validation_time_);
}

ConsensusMetrics::Timer ConsensusMetrics::create_vote_processing_timer() {
    return Timer(vote_processing_time_);
}

ConsensusMetrics::Timer ConsensusMetrics::create_fork_choice_timer() {
    return Timer(fork_choice_time_);
}

ConsensusMetrics::Timer ConsensusMetrics::create_consensus_round_timer() {
    return Timer(consensus_round_time_);
}

ConsensusMetrics::Timer ConsensusMetrics::create_poh_tick_timer() {
    return Timer(poh_tick_time_);
}

ConsensusMetrics::Timer ConsensusMetrics::create_poh_verification_timer() {
    return Timer(poh_verification_time_);
}

// Getter methods
std::shared_ptr<monitoring::IHistogram> ConsensusMetrics::get_block_validation_histogram() {
    return block_validation_time_;
}

std::shared_ptr<monitoring::IHistogram> ConsensusMetrics::get_vote_processing_histogram() {
    return vote_processing_time_;
}

std::shared_ptr<monitoring::IHistogram> ConsensusMetrics::get_fork_choice_histogram() {
    return fork_choice_time_;
}

std::shared_ptr<monitoring::ICounter> ConsensusMetrics::get_blocks_processed_counter() {
    return blocks_processed_;
}

std::shared_ptr<monitoring::IHistogram> ConsensusMetrics::get_consensus_round_histogram() {
    return consensus_round_time_;
}

// Global consensus metrics
std::unique_ptr<ConsensusMetrics> GlobalConsensusMetrics::instance_ = nullptr;
std::mutex GlobalConsensusMetrics::instance_mutex_;

ConsensusMetrics& GlobalConsensusMetrics::instance() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (!instance_) {
        initialize();
    }
    return *instance_;
}

void GlobalConsensusMetrics::initialize() {
    if (!instance_) {
        instance_ = std::make_unique<ConsensusMetrics>();
    }
}

void GlobalConsensusMetrics::shutdown() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    instance_.reset();
}

// Performance analyzer implementation
ConsensusPerformanceAnalyzer::ConsensusPerformanceAnalyzer()
    : last_report_time_(std::chrono::system_clock::now())
    , last_blocks_count_(0)
    , last_votes_count_(0) {
}

ConsensusPerformanceAnalyzer::PerformanceReport 
ConsensusPerformanceAnalyzer::generate_report(std::chrono::milliseconds analysis_period) {
    PerformanceReport report;
    report.report_timestamp = std::chrono::system_clock::now();
    report.analysis_period = analysis_period;

    auto& metrics = GlobalConsensusMetrics::instance();

    // Get histogram data for timing analysis
    auto block_validation_data = metrics.get_block_validation_histogram()->get_data();
    auto vote_processing_data = metrics.get_vote_processing_histogram()->get_data();
    auto fork_choice_data = metrics.get_fork_choice_histogram()->get_data();
    auto consensus_round_data = metrics.get_consensus_round_histogram()->get_data();

    // Calculate average times (convert to milliseconds)
    report.avg_block_validation_time_ms = (block_validation_data.total_count > 0) 
        ? (block_validation_data.sum / block_validation_data.total_count) * 1000.0 
        : 0.0;
    
    report.avg_vote_processing_time_ms = (vote_processing_data.total_count > 0)
        ? (vote_processing_data.sum / vote_processing_data.total_count) * 1000.0
        : 0.0;
    
    report.avg_fork_choice_time_ms = (fork_choice_data.total_count > 0)
        ? (fork_choice_data.sum / fork_choice_data.total_count) * 1000.0
        : 0.0;
    
    report.avg_consensus_round_time_ms = (consensus_round_data.total_count > 0)
        ? (consensus_round_data.sum / consensus_round_data.total_count) * 1000.0
        : 0.0;

    // Get total counts
    report.total_blocks_processed = static_cast<int64_t>(metrics.get_blocks_processed_counter()->get_value());
    // Note: Would need to add votes counter getter for this
    report.total_votes_processed = 0; // Placeholder

    // Calculate rates 
    auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(
        report.report_timestamp - last_report_time_
    );
    
    if (time_diff.count() > 0) {
        double seconds = time_diff.count() / 1000.0;
        report.blocks_per_second = (report.total_blocks_processed - last_blocks_count_) / seconds;
        report.votes_per_second = (report.total_votes_processed - last_votes_count_) / seconds;
    } else {
        report.blocks_per_second = 0.0;
        report.votes_per_second = 0.0;
    }

    // Update for next report
    last_report_time_ = report.report_timestamp;
    last_blocks_count_ = report.total_blocks_processed;
    last_votes_count_ = report.total_votes_processed;

    return report;
}

std::string ConsensusPerformanceAnalyzer::export_report_json(const PerformanceReport& report) {
    std::ostringstream json;
    
    json << "{\n";
    json << "  \"consensus_performance_report\": {\n";
    json << "    \"timestamp\": " << std::chrono::duration_cast<std::chrono::milliseconds>(
        report.report_timestamp.time_since_epoch()
    ).count() << ",\n";
    json << "    \"analysis_period_ms\": " << report.analysis_period.count() << ",\n";
    json << "    \"timing_metrics\": {\n";
    json << "      \"avg_block_validation_time_ms\": " << std::fixed << std::setprecision(3) 
         << report.avg_block_validation_time_ms << ",\n";
    json << "      \"avg_vote_processing_time_ms\": " << std::fixed << std::setprecision(3)
         << report.avg_vote_processing_time_ms << ",\n";
    json << "      \"avg_fork_choice_time_ms\": " << std::fixed << std::setprecision(3)
         << report.avg_fork_choice_time_ms << ",\n";
    json << "      \"avg_consensus_round_time_ms\": " << std::fixed << std::setprecision(3)
         << report.avg_consensus_round_time_ms << "\n";
    json << "    },\n";
    json << "    \"throughput_metrics\": {\n";
    json << "      \"total_blocks_processed\": " << report.total_blocks_processed << ",\n";
    json << "      \"total_votes_processed\": " << report.total_votes_processed << ",\n";
    json << "      \"blocks_per_second\": " << std::fixed << std::setprecision(2) 
         << report.blocks_per_second << ",\n";
    json << "      \"votes_per_second\": " << std::fixed << std::setprecision(2)
         << report.votes_per_second << "\n";
    json << "    }\n";
    json << "  }\n";
    json << "}\n";
    
    return json.str();
}

bool ConsensusPerformanceAnalyzer::validate_performance_targets(const PerformanceReport& report) {
    // Define performance targets for consensus operations
    const double MAX_BLOCK_VALIDATION_TIME_MS = 100.0;     // 100ms max for block validation
    const double MAX_VOTE_PROCESSING_TIME_MS = 10.0;       // 10ms max for vote processing  
    const double MAX_FORK_CHOICE_TIME_MS = 50.0;           // 50ms max for fork choice
    const double MAX_CONSENSUS_ROUND_TIME_MS = 2000.0;     // 2s max for full consensus round
    const double MIN_BLOCKS_PER_SECOND = 0.5;              // Minimum 0.5 blocks/second
    
    bool performance_ok = true;
    
    if (report.avg_block_validation_time_ms > MAX_BLOCK_VALIDATION_TIME_MS) {
        performance_ok = false;
    }
    
    if (report.avg_vote_processing_time_ms > MAX_VOTE_PROCESSING_TIME_MS) {
        performance_ok = false;
    }
    
    if (report.avg_fork_choice_time_ms > MAX_FORK_CHOICE_TIME_MS) {
        performance_ok = false;
    }
    
    if (report.avg_consensus_round_time_ms > MAX_CONSENSUS_ROUND_TIME_MS) {
        performance_ok = false;
    }
    
    if (report.blocks_per_second < MIN_BLOCKS_PER_SECOND && report.total_blocks_processed > 0) {
        performance_ok = false;
    }
    
    return performance_ok;
}

} // namespace monitoring  
} // namespace slonana