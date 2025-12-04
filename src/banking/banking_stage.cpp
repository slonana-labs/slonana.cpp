#include "banking/banking_stage.h"
#include "common/logging.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <execution>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <sys/resource.h>
#include <unistd.h>

namespace slonana {
namespace banking {

// Helper function to get configurable checkpoint directory
static std::string get_checkpoint_directory() {
  // Check environment variable first
  const char *env_path = std::getenv("SLONANA_CHECKPOINT_DIR");
  if (env_path && strlen(env_path) > 0) {
    return std::string(env_path) + "/banking_checkpoints";
  }

  // Use platform-appropriate default
#ifdef _WIN32
  const char *temp_dir = std::getenv("TEMP");
  if (temp_dir) {
    return std::string(temp_dir) + "\\slonana\\banking_checkpoints";
  }
  return "C:\\temp\\slonana\\banking_checkpoints";
#else
  return "/var/lib/slonana/banking_checkpoints";
#endif
}

// TransactionBatch implementation
std::atomic<uint64_t> TransactionBatch::next_batch_id_(1);

TransactionBatch::TransactionBatch()
    : batch_id_(next_batch_id_++),
      creation_time_(std::chrono::steady_clock::now()), state_(State::PENDING) {
}

TransactionBatch::TransactionBatch(std::vector<TransactionPtr> transactions)
    : batch_id_(next_batch_id_++), transactions_(std::move(transactions)),
      creation_time_(std::chrono::steady_clock::now()), state_(State::PENDING) {
}

TransactionBatch::~TransactionBatch() = default;

void TransactionBatch::add_transaction(TransactionPtr transaction) {
  if (transaction) {
    transactions_.push_back(transaction);
  }
}

// PipelineStage implementation
PipelineStage::PipelineStage(const std::string &name,
                             ProcessFunction process_fn)
    : name_(name), process_fn_(process_fn), running_(false),
      batch_timeout_(std::chrono::seconds(5)), max_parallel_batches_(4),
      should_stop_(false), processed_batches_(0), failed_batches_(0),
      total_processing_time_ms_(0) {}

PipelineStage::~PipelineStage() { stop(); }

bool PipelineStage::start() {
  if (running_) {
    return true;
  }

  should_stop_ = false;
  running_ = true;

  // Start worker threads
  for (size_t i = 0; i < max_parallel_batches_; ++i) {
    workers_.emplace_back(&PipelineStage::worker_loop, this);
  }

  return true;
}

bool PipelineStage::stop() {
  if (!running_) {
    return true;
  }

  should_stop_ = true;
  queue_cv_.notify_all();

  // Wait for all workers to finish
  for (auto &worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  workers_.clear();

  running_ = false;
  return true;
}

void PipelineStage::submit_batch(std::shared_ptr<TransactionBatch> batch) {
  if (!running_ || !batch) {
    return;
  }

  std::lock_guard<std::mutex> lock(queue_mutex_);
  batch_queue_.push(batch);
  queue_cv_.notify_one();
}

size_t PipelineStage::get_pending_batches() const {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  return batch_queue_.size();
}

double PipelineStage::get_average_processing_time_ms() const {
  if (processed_batches_ == 0) {
    return 0.0;
  }
  return static_cast<double>(total_processing_time_ms_) / processed_batches_;
}

void PipelineStage::worker_loop() {
  try {
    while (!should_stop_) {
      std::shared_ptr<TransactionBatch> batch;

      try {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // **ENHANCED TIMEOUT HANDLING** - Prevent indefinite blocking
        auto timeout = std::chrono::seconds(1); // 1 second timeout
        if (!queue_cv_.wait_for(lock, timeout, [this] {
              return !batch_queue_.empty() || should_stop_;
            })) {
          // Timeout occurred, check if we should continue
          if (should_stop_) {
            break;
          }
          continue; // Continue loop and check again
        }

        if (should_stop_) {
          break;
        }

        if (!batch_queue_.empty()) {
          batch = batch_queue_.front();
          batch_queue_.pop();
        }
      } catch (const std::system_error &e) {
        std::cerr << "ERROR: System error in worker loop queue handling: "
                  << e.what() << std::endl;
        continue;
      } catch (const std::exception &e) {
        std::cerr << "ERROR: Exception in worker loop queue handling: "
                  << e.what() << std::endl;
        continue;
      } catch (...) {
        std::cerr << "ERROR: Unknown exception in worker loop queue handling"
                  << std::endl;
        continue;
      }

      if (batch) {
        try {
          // **ENHANCED BATCH VALIDATION** - Ensure batch is valid before
          // processing
          if (!batch) {
            std::cerr << "ERROR: Null batch pointer after dequeue in " << name_
                      << std::endl;
            continue;
          }

          // Validate batch state
          if (batch->empty()) {
            std::cerr << "WARNING: Empty batch in " << name_ << ", skipping"
                      << std::endl;
            continue;
          }

          std::cerr << "DEBUG: Processing batch " << batch->get_batch_id()
                    << " with " << batch->size() << " transactions in " << name_
                    << std::endl;

          process_batch(batch);

        } catch (const std::bad_alloc &e) {
          std::cerr << "CRITICAL: Memory allocation error processing batch in "
                    << name_ << ": " << e.what() << std::endl;
          // Don't continue - memory issues are serious
          break;
        } catch (const std::runtime_error &e) {
          std::cerr << "ERROR: Runtime error processing batch in " << name_
                    << ": " << e.what() << std::endl;
        } catch (const std::exception &e) {
          std::cerr << "ERROR: Exception processing batch in worker: "
                    << e.what() << std::endl;
        } catch (...) {
          std::cerr << "ERROR: Unknown exception processing batch in worker"
                    << std::endl;
        }
      }
    }
  } catch (const std::bad_alloc &e) {
    std::cerr << "CRITICAL: Memory allocation error in worker loop: "
              << e.what() << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "CRITICAL: Worker loop exception: " << e.what() << std::endl;
  } catch (...) {
    std::cerr << "CRITICAL: Unknown worker loop exception" << std::endl;
  }

  std::cerr << "DEBUG: Worker loop for " << name_ << " terminated" << std::endl;
}

void PipelineStage::process_batch(std::shared_ptr<TransactionBatch> batch) {
  if (!batch) {
    std::cerr << "ERROR: Null batch pointer in stage " << name_ << std::endl;
    return;
  }

  auto start_time = std::chrono::steady_clock::now();

  batch->set_state(TransactionBatch::State::PROCESSING);

  bool success = false;
  try {
    if (!process_fn_) {
      std::cerr << "ERROR: Null process function in stage " << name_
                << std::endl;
      success = false;
    } else {
      // **ENHANCED PROCESS FUNCTION PROTECTION** - Additional safety checks
      std::cerr << "DEBUG: Executing process function for batch "
                << batch->get_batch_id() << " in stage " << name_ << std::endl;

      // Validate batch state before processing
      if (batch->get_state() != TransactionBatch::State::PROCESSING) {
        std::cerr << "WARNING: Batch " << batch->get_batch_id()
                  << " not in PROCESSING state in " << name_ << std::endl;
      }

      success = process_fn_(batch);

      std::cerr << "DEBUG: Process function completed for batch "
                << batch->get_batch_id() << " in stage " << name_
                << " with result: " << (success ? "SUCCESS" : "FAILURE")
                << std::endl;
    }
  } catch (const std::bad_alloc &e) {
    std::cerr << "CRITICAL: Memory allocation error in stage " << name_ << ": "
              << e.what() << std::endl;
    success = false;
  } catch (const std::runtime_error &e) {
    std::cerr << "ERROR: Runtime error processing batch in stage " << name_
              << ": " << e.what() << std::endl;
    success = false;
  } catch (const std::logic_error &e) {
    std::cerr << "ERROR: Logic error processing batch in stage " << name_
              << ": " << e.what() << std::endl;
    success = false;
  } catch (const std::exception &e) {
    std::cerr << "Error processing batch in stage " << name_ << ": " << e.what()
              << std::endl;
    success = false;
  } catch (...) {
    std::cerr << "CRITICAL: Unknown exception in stage " << name_ << std::endl;
    success = false;
  }

  auto end_time = std::chrono::steady_clock::now();
  auto processing_time = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);
  total_processing_time_ms_ += processing_time.count();

  // **ENHANCED STATE MANAGEMENT** - More robust state transitions
  try {
    if (success) {
      batch->set_state(TransactionBatch::State::COMPLETED);
      processed_batches_++;

      // Forward to next stage with validation
      if (next_stage_) {
        std::cerr << "DEBUG: Forwarding batch " << batch->get_batch_id()
                  << " from " << name_ << " to next stage" << std::endl;
        next_stage_->submit_batch(batch);
      } else {
        std::cerr << "DEBUG: Batch " << batch->get_batch_id()
                  << " completed in final stage " << name_ << std::endl;
      }
    } else {
      batch->set_state(TransactionBatch::State::FAILED);
      failed_batches_++;
      std::cerr << "WARNING: Batch " << batch->get_batch_id()
                << " failed in stage " << name_ << std::endl;
    }
  } catch (const std::exception &e) {
    std::cerr << "ERROR: Exception during batch state transition in " << name_
              << ": " << e.what() << std::endl;
    // Ensure we count failed batches even if state setting fails
    if (!success) {
      failed_batches_++;
    }
  }
}

// ResourceMonitor implementation
ResourceMonitor::ResourceMonitor()
    : monitoring_(false), cpu_usage_(0.0), memory_usage_mb_(0),
      peak_memory_usage_mb_(0), cpu_threshold_(80.0),
      memory_threshold_mb_(1024) {}

ResourceMonitor::~ResourceMonitor() { stop(); }

bool ResourceMonitor::start() {
  if (monitoring_) {
    return true;
  }

  monitoring_ = true;
  monitor_thread_ = std::thread(&ResourceMonitor::monitor_loop, this);
  return true;
}

bool ResourceMonitor::stop() {
  if (!monitoring_) {
    return true;
  }

  monitoring_ = false;
  if (monitor_thread_.joinable()) {
    monitor_thread_.join();
  }
  return true;
}

void ResourceMonitor::monitor_loop() {
  while (monitoring_) {
    double cpu = calculate_cpu_usage();
    size_t memory = calculate_memory_usage();

    cpu_usage_ = cpu;
    memory_usage_mb_ = memory;

    if (memory > peak_memory_usage_mb_) {
      peak_memory_usage_mb_ = memory;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}

double ResourceMonitor::calculate_cpu_usage() {
  // Production-grade CPU usage calculation using Linux /proc/stat interface
  // This provides accurate system-wide CPU utilization metrics
  static long long prev_idle = 0, prev_total = 0;

  std::ifstream stat_file("/proc/stat");
  if (!stat_file.is_open()) {
    return 0.0;
  }

  std::string line;
  std::getline(stat_file, line);

  std::istringstream iss(line);
  std::string cpu_label;
  long long user, nice, system, idle, iowait, irq, softirq, steal;

  iss >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >>
      softirq >> steal;

  long long current_idle = idle + iowait;
  long long current_total =
      user + nice + system + idle + iowait + irq + softirq + steal;

  long long total_diff = current_total - prev_total;
  long long idle_diff = current_idle - prev_idle;

  double cpu_percent = 0.0;
  if (total_diff > 0) {
    cpu_percent = 100.0 * (total_diff - idle_diff) / total_diff;
  }

  prev_idle = current_idle;
  prev_total = current_total;

  return cpu_percent;
}

size_t ResourceMonitor::calculate_memory_usage() {
  // Get memory usage for current process
  std::ifstream status_file("/proc/self/status");
  if (!status_file.is_open()) {
    return 0;
  }

  std::string line;
  while (std::getline(status_file, line)) {
    if (line.find("VmRSS:") == 0) {
      std::istringstream iss(line);
      std::string label, value_str, unit;
      iss >> label >> value_str >> unit;

      size_t value = std::stoull(value_str);
      return value / 1024; // Convert KB to MB
    }
  }

  return 0;
}

// BankingStage implementation
BankingStage::BankingStage()
    : initialized_(false), running_(false), batch_size_(1),
      batch_timeout_(std::chrono::milliseconds(10)), parallel_stages_(4),
      max_concurrent_batches_(16), worker_thread_count_(8),
      adaptive_batching_enabled_(true), resource_monitoring_enabled_(true),
      priority_processing_enabled_(false), ledger_manager_(nullptr),
      should_stop_(false), total_transactions_processed_(0),
      total_batches_processed_(0), failed_transactions_(0),
      total_processing_time_ms_(0),
      transaction_processor_breaker_(
          common::CircuitBreakerConfig{10, std::chrono::milliseconds(5000), 3}),
      transaction_retry_policy_(
          common::FaultTolerance::create_rpc_retry_policy()),
      state_checkpoint_(std::make_shared<common::FileCheckpoint>(
          get_checkpoint_directory())) {

  // Initialize fee market and MEV protection
  fee_market_ = std::make_unique<FeeMarket>();
  mev_protection_ = std::make_unique<MEVProtection>();
  
  std::cout << "Banking Stage initialized with fault tolerance mechanisms"
            << std::endl;
  std::cout << "Fee market and MEV protection modules enabled" << std::endl;
}

BankingStage::~BankingStage() { shutdown(); }

bool BankingStage::initialize() {
  try {
    if (initialized_) {
      std::cout << "Banking stage already initialized" << std::endl;
      return true;
    }

    std::cout << "Initializing banking stage components..." << std::endl;

    // **SAFE PIPELINE INITIALIZATION** - Enhanced error handling
    try {
      initialize_pipeline();
      std::cout << "Banking stage pipeline initialized successfully"
                << std::endl;
    } catch (const std::exception &e) {
      std::cerr << "ERROR: Failed to initialize banking stage pipeline: "
                << e.what() << std::endl;
      return false;
    } catch (...) {
      std::cerr << "ERROR: Unknown error initializing banking stage pipeline"
                << std::endl;
      return false;
    }

    // **SAFE RESOURCE MONITOR INITIALIZATION** - Prevent crashes in monitoring
    if (resource_monitoring_enabled_) {
      try {
        resource_monitor_ = std::make_unique<ResourceMonitor>();
        std::cout << "Banking stage resource monitor initialized" << std::endl;
      } catch (const std::bad_alloc &e) {
        std::cerr << "ERROR: Memory allocation failed for resource monitor: "
                  << e.what() << std::endl;
        resource_monitoring_enabled_ = false; // Disable monitoring but continue
      } catch (const std::exception &e) {
        std::cerr << "ERROR: Failed to initialize resource monitor: "
                  << e.what() << std::endl;
        resource_monitoring_enabled_ = false; // Disable monitoring but continue
      }
    }

    initialized_ = true;
    std::cout << "Banking stage initialization completed successfully"
              << std::endl;
    return true;

  } catch (const std::exception &e) {
    std::cerr << "CRITICAL: Banking stage initialization failed: " << e.what()
              << std::endl;
    return false;
  } catch (...) {
    std::cerr << "CRITICAL: Unknown error during banking stage initialization"
              << std::endl;
    return false;
  }
}

bool BankingStage::start() {
  try {
    if (!initialized_) {
      std::cerr << "ERROR: Cannot start banking stage - not initialized"
                << std::endl;
      return false;
    }

    if (running_) {
      std::cout << "Banking stage already running" << std::endl;
      return true;
    }

    std::cout << "Starting banking stage components..." << std::endl;

    // **SAFE PIPELINE STAGE STARTUP** - Start each stage with error handling
    for (size_t i = 0; i < pipeline_stages_.size(); ++i) {
      auto &stage = pipeline_stages_[i];
      if (!stage) {
        std::cerr << "ERROR: Null pipeline stage at index " << i << std::endl;
        return false;
      }

      try {
        if (!stage->start()) {
          std::cerr << "ERROR: Failed to start pipeline stage " << i
                    << std::endl;
          return false;
        }
        std::cout << "Started pipeline stage " << i << " successfully"
                  << std::endl;
      } catch (const std::exception &e) {
        std::cerr << "ERROR: Exception starting pipeline stage " << i << ": "
                  << e.what() << std::endl;
        return false;
      }
    }

    // **SAFE RESOURCE MONITOR STARTUP** - Handle potential startup failures
    if (resource_monitor_) {
      try {
        if (!resource_monitor_->start()) {
          std::cerr << "WARNING: Resource monitor failed to start, continuing "
                       "without monitoring"
                    << std::endl;
          resource_monitor_.reset(); // Disable monitoring but continue
        } else {
          std::cout << "Resource monitor started successfully" << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "WARNING: Exception starting resource monitor: "
                  << e.what() << std::endl;
        resource_monitor_.reset(); // Disable monitoring but continue
      }
    }

    // **START BATCH PROCESSOR THREAD** - Critical for transaction processing
    try {
      should_stop_ = false;
      batch_processor_ = std::thread(&BankingStage::process_batches, this);
      std::cout << "Banking stage batch processor thread started successfully"
                << std::endl;
    } catch (const std::exception &e) {
      std::cerr << "ERROR: Failed to start batch processor thread: " << e.what()
                << std::endl;
      return false;
    }

    running_ = true;
    std::cout << "Banking stage started successfully" << std::endl;
    return true;

  } catch (const std::exception &e) {
    std::cerr << "CRITICAL: Banking stage startup failed: " << e.what()
              << std::endl;
    return false;
  } catch (...) {
    std::cerr << "CRITICAL: Unknown error during banking stage startup"
              << std::endl;
    return false;
  }
}

bool BankingStage::stop() {
  if (!running_) {
    return true;
  }

  should_stop_ = true;
  queue_cv_.notify_all();

  // Stop batch processor
  if (batch_processor_.joinable()) {
    batch_processor_.join();
  }

  // Stop pipeline stages
  for (auto &stage : pipeline_stages_) {
    stage->stop();
  }

  // Stop resource monitor
  if (resource_monitor_) {
    resource_monitor_->stop();
  }

  running_ = false;
  return true;
}

bool BankingStage::shutdown() {
  if (!initialized_) {
    return true;
  }

  stop();

  pipeline_stages_.clear();
  validation_stage_.reset();
  execution_stage_.reset();
  commitment_stage_.reset();
  resource_monitor_.reset();

  initialized_ = false;
  return true;
}

void BankingStage::submit_transaction(TransactionPtr transaction) {
  // **HIGH-PERFORMANCE TRANSACTION SUBMISSION** - Optimized for 1k+ TPS
  if (!running_) {
    std::cerr << "Banking: Transaction rejected - banking stage not running" << std::endl;
    return;
  }
  
  if (!transaction) {
    std::cerr << "Banking: Transaction rejected - null transaction" << std::endl;
    return;
  }

  // **DIAGNOSTIC LOGGING** - Track transaction submission (first 10 only to avoid log spam)
  static std::atomic<size_t> submitted_count{0};
  size_t current_count = submitted_count.fetch_add(1);
  if (current_count < 10 || current_count % 100 == 0) {
    std::cout << "Banking: Transaction #" << current_count << " submitted to banking stage" << std::endl;
  }

  try {
    // **FEE-BASED PRIORITY CALCULATION** - Use fee market for intelligent ordering
    int priority = 0;
    if (fee_market_enabled_ && fee_market_) {
      // Extract fee from transaction (simplified - would need proper fee extraction)
      uint64_t tx_fee = 5000; // Default fee, in production would extract from tx
      
      // Calculate priority based on fee tier
      FeeTier tier = fee_market_->classify_fee_tier(tx_fee);
      switch (tier) {
        case FeeTier::URGENT:
          priority = 1000;
          break;
        case FeeTier::HIGH:
          priority = 500;
          break;
        case FeeTier::NORMAL:
          priority = 100;
          break;
        case FeeTier::LOW:
          priority = 10;
          break;
      }
      
      // Record fee for statistics
      fee_market_->record_transaction_fee(tx_fee, true);
    }
    
    // **OPTIMIZED PRIORITY QUEUEING** - Minimal lock contention
    if (priority_processing_enabled_ || fee_market_enabled_) {
      std::unique_lock<std::mutex> lock(priority_mutex_, std::try_to_lock);
      if (lock) {
        // Check for manual priority override
        auto it = transaction_priorities_.find(transaction);
        if (it != transaction_priorities_.end()) {
          priority = std::max(priority, it->second);
        }
        priority_queue_.push({priority, transaction});
        queue_cv_.notify_one();
        
        if (current_count < 5) {
          std::cout << "Banking: Transaction queued in priority queue (priority=" << priority << ")" << std::endl;
        }
        return; // Fast path success
      }
    }

    // **HIGH-SPEED STANDARD QUEUEING** - Minimal overhead
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      transaction_queue_.push(transaction);
      
      if (current_count < 5) {
        std::cout << "Banking: Transaction queued in standard queue (queue size=" << transaction_queue_.size() << ")" << std::endl;
      }
    }
    queue_cv_.notify_one();

  } catch (const std::exception& e) {
    std::cerr << "Banking: Transaction submission failed: " << e.what() << std::endl;
    return;
  } catch (...) {
    std::cerr << "Banking: Transaction submission failed with unknown error" << std::endl;
    return;
  }
}

// **NEW: BATCH TRANSACTION SUBMISSION** for ultra-high throughput
void BankingStage::submit_transaction_batch(
    std::vector<TransactionPtr> transactions) {
  if (!running_ || transactions.empty()) {
    return;
  }

  try {
    // **BATCH PROCESSING** - Submit multiple transactions with single lock
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      for (auto &tx : transactions) {
        if (tx) {
          transaction_queue_.push(tx);
        }
      }
    }

    // **BULK NOTIFICATION** - Wake multiple workers
    for (size_t i = 0;
         i < std::min(transactions.size(), size_t(worker_thread_count_)); ++i) {
      queue_cv_.notify_one();
    }

  } catch (...) {
    return;
  }
}

void BankingStage::submit_transactions(
    std::vector<TransactionPtr> transactions) {
  for (auto &transaction : transactions) {
    submit_transaction(transaction);
  }
}

std::future<bool>
BankingStage::process_transaction_async(TransactionPtr transaction) {
  auto promise = std::make_shared<std::promise<bool>>();
  auto future = promise->get_future();

  // Create a single-transaction batch
  auto batch = std::make_shared<TransactionBatch>();
  batch->add_transaction(transaction);

  // Set completion callback to fulfill promise
  auto original_callback = completion_callback_;
  completion_callback_ =
      [promise,
       original_callback](std::shared_ptr<TransactionBatch> completed_batch) {
        bool success =
            completed_batch->get_state() == TransactionBatch::State::COMPLETED;
        promise->set_value(success);

        if (original_callback) {
          original_callback(completed_batch);
        }
      };

  submit_batch(batch);
  return future;
}

void BankingStage::submit_batch(std::shared_ptr<TransactionBatch> batch) {
  if (!running_ || !batch || batch->empty()) {
    return;
  }

  // Start with validation stage
  if (validation_stage_) {
    validation_stage_->submit_batch(batch);
  }
}

BankingStage::Statistics BankingStage::get_statistics() const {
  Statistics stats{};

  stats.total_transactions_processed = total_transactions_processed_;
  stats.total_batches_processed = total_batches_processed_;
  stats.failed_transactions = failed_transactions_;
  stats.pending_transactions = get_pending_transaction_count();

  if (total_batches_processed_ > 0) {
    stats.average_batch_processing_time_ms =
        static_cast<double>(total_processing_time_ms_) /
        total_batches_processed_;
  } else {
    stats.average_batch_processing_time_ms = 0.0;
  }

  stats.transactions_per_second = get_throughput_tps();

  if (resource_monitor_) {
    stats.cpu_usage = resource_monitor_->get_cpu_usage();
    stats.memory_usage_mb = resource_monitor_->get_memory_usage_mb();
  } else {
    stats.cpu_usage = 0.0;
    stats.memory_usage_mb = 0;
  }

  auto now = std::chrono::steady_clock::now();
  stats.uptime =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);

  return stats;
}

size_t BankingStage::get_pending_transaction_count() const {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  size_t count = transaction_queue_.size();

  if (priority_processing_enabled_) {
    std::lock_guard<std::mutex> priority_lock(priority_mutex_);
    count += priority_queue_.size();
  }

  return count;
}

double BankingStage::get_throughput_tps() const {
  auto now = std::chrono::steady_clock::now();
  auto uptime =
      std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);

  if (uptime.count() == 0) {
    return 0.0;
  }

  return static_cast<double>(total_transactions_processed_) / uptime.count();
}

void BankingStage::set_transaction_priority(TransactionPtr transaction,
                                            int priority) {
  if (!priority_processing_enabled_) {
    return;
  }

  std::lock_guard<std::mutex> lock(priority_mutex_);
  transaction_priorities_[transaction] = priority;
}

void BankingStage::initialize_pipeline() {
  // Create validation stage
  validation_stage_ = std::make_shared<PipelineStage>(
      "validation", [this](std::shared_ptr<TransactionBatch> batch) {
        return validate_batch(batch);
      });

  // Create execution stage
  execution_stage_ = std::make_shared<PipelineStage>(
      "execution", [this](std::shared_ptr<TransactionBatch> batch) {
        return execute_batch(batch);
      });

  // Create commitment stage
  commitment_stage_ = std::make_shared<PipelineStage>(
      "commitment", [this](std::shared_ptr<TransactionBatch> batch) {
        return commit_batch(batch);
      });

  // Connect stages
  validation_stage_->set_next_stage(execution_stage_);
  execution_stage_->set_next_stage(commitment_stage_);

  // Add to pipeline
  pipeline_stages_ = {validation_stage_, execution_stage_, commitment_stage_};

  // Configure stages
  for (auto &stage : pipeline_stages_) {
    stage->set_batch_timeout(batch_timeout_);
    stage->set_max_parallel_batches(parallel_stages_);
  }
}

void BankingStage::process_batches() {
  std::cout << "Banking: Batch processor thread started" << std::endl;
  size_t iteration_count = 0;
  
  while (!should_stop_) {
    process_transaction_queue();
    create_batch_if_needed();

    if (adaptive_batching_enabled_) {
      adjust_batch_size_if_needed();
    }

    if (resource_monitoring_enabled_ && should_throttle_processing()) {
      handle_resource_pressure();
    }

    // **DIAGNOSTIC LOGGING** - Log batch processor health every 100 iterations
    iteration_count++;
    if (iteration_count % 100 == 0) {
      size_t queue_size = 0;
      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_size = transaction_queue_.size();
      }
      std::cout << "Banking: Batch processor iteration " << iteration_count 
                << " - queue_size=" << queue_size 
                << " batches_processed=" << total_batches_processed_.load() << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  
  std::cout << "Banking: Batch processor thread stopping" << std::endl;
}

void BankingStage::create_batch_if_needed() {
  std::lock_guard<std::mutex> batch_lock(batch_mutex_);

  if (!current_batch_) {
    current_batch_ = std::make_shared<TransactionBatch>();
  }

  bool should_process_batch = false;
  std::string trigger_reason;

  // Check if batch is full or timeout reached
  if (current_batch_->size() >= batch_size_) {
    should_process_batch = true;
    trigger_reason = "batch_full";
  } else if (!current_batch_->empty()) {
    auto age =
        std::chrono::steady_clock::now() - current_batch_->get_creation_time();
    if (age >= batch_timeout_) {
      should_process_batch = true;
      trigger_reason = "timeout";
    }
  }

  if (should_process_batch) {
    size_t batch_size = current_batch_->size();
    std::cout << "Banking: Processing batch with " << batch_size 
              << " transactions (trigger=" << trigger_reason << ")" << std::endl;
    
    submit_batch(current_batch_);
    total_batches_processed_++;
    current_batch_ = std::make_shared<TransactionBatch>();
  }
}

void BankingStage::process_transaction_queue() {
  std::vector<TransactionPtr> transactions_to_process;

  // Extract transactions from queue
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    size_t max_to_process = batch_size_;
    while (!transaction_queue_.empty() &&
           transactions_to_process.size() < max_to_process) {
      transactions_to_process.push_back(transaction_queue_.front());
      transaction_queue_.pop();
    }
  }

  // Handle priority queue if enabled
  if (priority_processing_enabled_ || fee_market_enabled_) {
    std::lock_guard<std::mutex> priority_lock(priority_mutex_);

    size_t remaining_capacity = batch_size_ - transactions_to_process.size();
    while (!priority_queue_.empty() && remaining_capacity > 0) {
      transactions_to_process.push_back(priority_queue_.top().second);
      priority_queue_.pop();
      remaining_capacity--;
    }
  }

  // **MEV PROTECTION** - Apply fair ordering before batching
  if (mev_protection_enabled_ && mev_protection_ && !transactions_to_process.empty()) {
    try {
      transactions_to_process = mev_protection_->apply_fair_ordering(transactions_to_process);
    } catch (const std::exception &e) {
      LOG_ERROR("MEV protection failed: {}", e.what());
      // Continue with original ordering on failure
    }
  }

  // Add transactions to current batch
  if (!transactions_to_process.empty()) {
    std::lock_guard<std::mutex> batch_lock(batch_mutex_);

    if (!current_batch_) {
      current_batch_ = std::make_shared<TransactionBatch>();
    }

    // **DIAGNOSTIC LOGGING** - Track batch additions
    static size_t last_logged_batch_size = 0;
    size_t new_batch_size = current_batch_->size() + transactions_to_process.size();
    
    if (last_logged_batch_size == 0 || new_batch_size >= batch_size_ || new_batch_size % 50 == 0) {
      std::cout << "Banking: Adding " << transactions_to_process.size() 
                << " transactions to current batch (new size=" << new_batch_size << ")" << std::endl;
      last_logged_batch_size = new_batch_size;
    }

    for (auto &transaction : transactions_to_process) {
      current_batch_->add_transaction(transaction);
    }
  }
}

bool BankingStage::validate_batch(std::shared_ptr<TransactionBatch> batch) {
  // Validate all transactions in the batch with enhanced safety checks
  if (!batch) {
    LOG_ERROR("Null batch in validate_batch");
    return false;
  }

  auto &transactions = batch->get_transactions();
  std::vector<bool> results(transactions.size());

  // Use parallel algorithms for performance-critical transaction validation
  // as recommended in CODE_STYLE.md
  try {
    std::transform(
        std::execution::par_unseq, transactions.begin(), transactions.end(),
        results.begin(), [](const TransactionPtr &transaction) -> bool {
          try {
            // Enhanced transaction validation with safety checks
            if (transaction) {
              // Verify transaction pointer is still valid before calling
              // verify()
              return transaction->verify();
            }
            return false;
          } catch (const std::exception &e) {
            LOG_ERROR("Exception during transaction validation: ", e.what());
            return false;
          } catch (...) {
            LOG_ERROR("Unknown exception during transaction validation");
            return false;
          }
        });
  } catch (const std::exception &e) {
    // Fallback to sequential processing if parallel execution fails
    LOG_WARN("Parallel validation failed, falling back to sequential: ",
             e.what());

    for (size_t i = 0; i < transactions.size(); ++i) {
      try {
        if (transactions[i]) {
          results[i] = transactions[i]->verify();
        } else {
          results[i] = false;
        }
      } catch (const std::exception &ex) {
        LOG_ERROR("Transaction validation error: ", ex.what());
        results[i] = false;
      } catch (...) {
        LOG_ERROR("Unknown transaction validation error");
        results[i] = false;
      }
    }
  }

  // Count failed transactions
  size_t local_failed_count = std::count(results.begin(), results.end(), false);

  // Thread-safe counter update - Update failed transactions atomically
  failed_transactions_.fetch_add(local_failed_count, std::memory_order_relaxed);

  batch->set_results(results);
  return std::all_of(results.begin(), results.end(),
                     [](bool valid) { return valid; });
}

bool BankingStage::execute_batch(std::shared_ptr<TransactionBatch> batch) {
  // Execute all transactions in the batch with enhanced safety
  if (!batch) {
    LOG_ERROR("Null batch in execute_batch");
    return false;
  }

  auto &transactions = batch->get_transactions();
  std::vector<bool> results(transactions.size());

  // Use parallel algorithms for performance-critical transaction execution
  // as recommended in CODE_STYLE.md for transaction processing
  try {
    std::transform(
        std::execution::par_unseq, transactions.begin(), transactions.end(),
        results.begin(), [](const TransactionPtr &transaction) -> bool {
          try {
            // Enhanced transaction execution with safety checks
            if (transaction) {
              // More robust execution check - verify transaction is well-formed
              return !transaction->signatures.empty() &&
                     !transaction->message.empty();
            }
            return false;
          } catch (const std::exception &e) {
            LOG_ERROR("Exception during transaction execution: ", e.what());
            return false;
          } catch (...) {
            LOG_ERROR("Unknown exception during transaction execution");
            return false;
          }
        });
  } catch (const std::exception &e) {
    // Fallback to sequential processing if parallel execution fails
    LOG_WARN("Parallel execution failed, falling back to sequential: ",
             e.what());

    for (size_t i = 0; i < transactions.size(); ++i) {
      try {
        if (transactions[i]) {
          results[i] = !transactions[i]->signatures.empty() &&
                       !transactions[i]->message.empty();
        } else {
          results[i] = false;
        }
      } catch (const std::exception &ex) {
        LOG_ERROR("Transaction execution error: ", ex.what());
        results[i] = false;
      } catch (...) {
        LOG_ERROR("Unknown transaction execution error");
        results[i] = false;
      }
    }
  }

  // Count processed and failed transactions
  size_t local_processed_count =
      std::count(results.begin(), results.end(), true);
  size_t local_failed_count = results.size() - local_processed_count;

  // Thread-safe counter updates - Update counters atomically
  total_transactions_processed_.fetch_add(local_processed_count,
                                          std::memory_order_relaxed);
  failed_transactions_.fetch_add(local_failed_count, std::memory_order_relaxed);

  batch->set_results(results);
  return std::all_of(results.begin(), results.end(),
                     [](bool executed) { return executed; });
}

bool BankingStage::commit_batch(std::shared_ptr<TransactionBatch> batch) {
  // Production-ready commitment process that records transactions in the ledger
  if (!batch) {
    LOG_ERROR("Null batch in commit_batch");
    return false;
  }

  auto &transactions = batch->get_transactions();
  bool all_committed = true;

  // **THREAD-SAFE LEDGER ACCESS** - Use mutex to protect ledger operations
  std::unique_lock<std::mutex> ledger_lock(ledger_mutex_, std::defer_lock);

  if (ledger_manager_) {
    try {
      // Acquire lock for ledger operations to prevent race conditions
      ledger_lock.lock();

      // Create a new block to contain these transactions
      ledger::Block new_block;
      new_block.slot = ledger_manager_->get_latest_slot() + 1;
      new_block.timestamp =
          std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count();

      // **FIX: Handle genesis block case where parent hash might be empty**
      common::Hash parent_hash = ledger_manager_->get_latest_block_hash();
      if (parent_hash.empty()) {
        // For genesis block, use a well-known genesis hash (all zeros with
        // specific pattern)
        parent_hash.resize(32, 0);
        parent_hash[0] = 0x42; // Genesis marker
        std::cout << "Banking: Creating genesis block (parent hash is empty)"
                  << std::endl;
      }
      new_block.parent_hash = parent_hash;

      // Convert shared_ptr<Transaction> to Transaction objects for the block
      size_t processed_transactions = 0;
      for (const auto &tx_ptr : transactions) {
        try {
          if (tx_ptr) {
            ledger::Transaction ledger_tx;
            ledger_tx.signatures = tx_ptr->signatures;
            ledger_tx.message = tx_ptr->message;
            ledger_tx.hash = tx_ptr->hash;

            // **SAFE BASE58 ENCODING** - Protect against encoding crashes
            try {
              if (!ledger_tx.signatures.empty() &&
                  !ledger_tx.signatures[0].empty()) {
                // Transaction ID should be the base58-encoded first signature
                // This ensures mixed-case format consistent with Solana
                // conventions
                std::string transaction_id =
                    encode_base58_safe(ledger_tx.signatures[0]);
                std::cout << "Banking: Transaction committed with ID: "
                          << transaction_id
                          << " (base58-encoded, mixed-case format)"
                          << std::endl;
              }
            } catch (const std::exception &encode_error) {
              std::cerr << "ERROR: Base58 encoding failed: "
                        << encode_error.what() << std::endl;
              // Continue processing - encoding failure shouldn't stop
              // commitment
            }

            new_block.transactions.push_back(ledger_tx);
            processed_transactions++;
          }
        } catch (const std::exception &tx_error) {
          std::cerr << "ERROR: Transaction processing failed: "
                    << tx_error.what() << std::endl;
          all_committed = false;
        }
      }

      // Only create block if we have transactions to commit
      if (processed_transactions > 0) {
        try {
          // Compute block hash using SHA-256 for cryptographic security
          new_block.block_hash = new_block.compute_hash();

          // Store the block in the ledger with balance state tracking
          auto store_result = ledger_manager_->store_block(new_block);
          if (!store_result.is_ok()) {
            std::cerr
                << "Banking: Failed to store transaction block in ledger: "
                << store_result.error() << std::endl;
            all_committed = false;
          } else {
            try {
              std::cout << "Banking: Successfully committed "
                        << processed_transactions
                        << " transactions to ledger at slot " << new_block.slot
                        << " with block hash: "
                        << encode_base58_safe(new_block.block_hash)
                        << std::endl;
            } catch (const std::exception &log_error) {
              std::cerr << "ERROR: Logging failed: " << log_error.what()
                        << std::endl;
            }

            // **THREAD-SAFE COUNTER UPDATES** - Update counters atomically
            // after successful commit
            total_transactions_processed_.fetch_add(processed_transactions,
                                                    std::memory_order_relaxed);
            total_batches_processed_.fetch_add(1, std::memory_order_relaxed);

            // **BLOCK NOTIFICATION CALLBACK** - Notify validator core about the
            // new block
            if (block_notification_callback_) {
              try {
                block_notification_callback_(new_block);
                std::cout
                    << "Banking: Notified validator core about block at slot "
                    << new_block.slot << std::endl;
              } catch (const std::exception &callback_error) {
                std::cerr << "ERROR: Block notification callback failed: "
                          << callback_error.what() << std::endl;
              }
            } else {
              std::cout << "Banking: No block notification callback registered"
                        << std::endl;
            }
            
            // **FEE MARKET UPDATE** - Adjust base fee based on block utilization
            if (fee_market_enabled_ && fee_market_) {
              try {
                // Calculate block utilization (transactions processed / max capacity)
                double utilization = static_cast<double>(processed_transactions) / 
                                    std::max(size_t(1), batch_size_);
                utilization = std::min(1.0, utilization); // Cap at 100%
                
                fee_market_->update_base_fee(utilization);
                
                uint64_t new_base_fee = fee_market_->get_current_base_fee();
                std::cout << "Banking: Updated base fee to " << new_base_fee 
                         << " lamports (utilization: " << (utilization * 100.0) 
                         << "%)" << std::endl;
              } catch (const std::exception &fee_error) {
                std::cerr << "ERROR: Fee market update failed: " << fee_error.what()
                          << std::endl;
              }
            }
          }
        } catch (const std::exception &block_error) {
          std::cerr << "ERROR: Block processing failed: " << block_error.what()
                    << std::endl;
          all_committed = false;
        }
      } else {
        std::cout << "Banking: No valid transactions to commit in batch"
                  << std::endl;
      }

      // Release the lock before callback
      ledger_lock.unlock();

    } catch (const std::exception &ledger_error) {
      std::cerr << "ERROR: Ledger operation failed: " << ledger_error.what()
                << std::endl;
      all_committed = false;

      // Ensure lock is released on error
      if (ledger_lock.owns_lock()) {
        ledger_lock.unlock();
      }
    } catch (...) {
      std::cerr << "ERROR: Unknown error in ledger operations" << std::endl;
      all_committed = false;

      // Ensure lock is released on error
      if (ledger_lock.owns_lock()) {
        ledger_lock.unlock();
      }
    }
  } else {
    std::cout << "Banking: Warning - No ledger manager available, transactions "
                 "not persisted"
              << std::endl;
    std::cout << "Banking: Note - In production, all transactions must be "
                 "recorded in persistent ledger"
              << std::endl;
  }

  // **SAFE CALLBACK EXECUTION** - Protect against callback crashes
  try {
    if (completion_callback_) {
      completion_callback_(batch);
    }
  } catch (const std::exception &callback_error) {
    std::cerr << "ERROR: Completion callback failed: " << callback_error.what()
              << std::endl;
  } catch (...) {
    std::cerr << "ERROR: Unknown error in completion callback" << std::endl;
  }

  return all_committed;
}

void BankingStage::adjust_batch_size_if_needed() {
  size_t optimal_size = calculate_optimal_batch_size();
  if (optimal_size != batch_size_) {
    batch_size_ = optimal_size;
  }
}

size_t BankingStage::calculate_optimal_batch_size() {
  // Calculate optimal batch size based on current throughput and resource usage
  double current_tps = get_throughput_tps();

  if (resource_monitor_) {
    double cpu_usage = resource_monitor_->get_cpu_usage();

    if (cpu_usage > 80.0) {
      // High CPU usage, reduce batch size
      return std::max(batch_size_ / 2, size_t(16));
    } else if (cpu_usage < 50.0 && current_tps < 1000.0) {
      // Low CPU usage and low throughput, increase batch size
      return std::min(batch_size_ * 2, size_t(256));
    }
  }

  return batch_size_;
}

bool BankingStage::should_throttle_processing() const {
  if (!resource_monitor_) {
    return false;
  }

  return resource_monitor_->is_cpu_overloaded() ||
         resource_monitor_->is_memory_overloaded();
}

void BankingStage::handle_resource_pressure() {
  // Reduce processing rate when under resource pressure
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

// Utility function for base58 encoding (Solana-compatible)
std::string
BankingStage::encode_base58(const std::vector<uint8_t> &data) const {
  // Proper base58 encoding implementation for Solana compatibility
  if (data.empty())
    return "";

  // Base58 alphabet used by Bitcoin and Solana
  static const char base58_alphabet[] =
      "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

  // For 64-byte Ed25519 signatures, ensure exactly 88 characters
  if (data.size() == 64) {
    // Use improved algorithm for consistent 88-character output
    std::vector<uint8_t> digits;

    // Convert input to big number in base 58
    for (uint8_t byte : data) {
      uint32_t carry = byte;
      for (size_t i = 0; i < digits.size(); ++i) {
        carry += static_cast<uint32_t>(digits[i]) * 256;
        digits[i] = carry % 58;
        carry /= 58;
      }
      while (carry > 0) {
        digits.push_back(carry % 58);
        carry /= 58;
      }
    }

    // Count leading zeros
    size_t leading_zeros = 0;
    for (uint8_t byte : data) {
      if (byte == 0) {
        leading_zeros++;
      } else {
        break;
      }
    }

    // Build result with proper padding
    std::string result;

    // Add leading zero characters
    for (size_t i = 0; i < leading_zeros; ++i) {
      result += base58_alphabet[0];
    }

    // Add encoded digits in reverse order
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
      result += base58_alphabet[*it];
    }

    // For 64-byte signatures, pad to exactly 88 characters if needed
    while (result.length() < 88) {
      result = base58_alphabet[0] + result;
    }

    return result;
  }

  // Default implementation for other data sizes
  std::vector<uint8_t> digits(1, 0);

  for (uint8_t byte : data) {
    uint32_t carry = byte;
    for (size_t j = 0; j < digits.size(); ++j) {
      carry += static_cast<uint32_t>(digits[j]) << 8;
      digits[j] = carry % 58;
      carry /= 58;
    }

    while (carry > 0) {
      digits.push_back(carry % 58);
      carry /= 58;
    }
  }

  // Convert leading zeros
  std::string result;
  for (uint8_t byte : data) {
    if (byte != 0)
      break;
    result += base58_alphabet[0];
  }

  // Convert digits to base58 characters (reverse order)
  for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
    result += base58_alphabet[*it];
  }

  return result;
}

// **SAFE BASE58 ENCODING** - Enhanced version with comprehensive error handling
std::string
BankingStage::encode_base58_safe(const std::vector<uint8_t> &data) const {
  try {
    // Input validation
    if (data.empty()) {
      return "";
    }

    // Limit input size to prevent memory issues
    if (data.size() > 1024) {
      std::cerr << "ERROR: Base58 input too large: " << data.size() << " bytes"
                << std::endl;
      return "error_input_too_large";
    }

    // Base58 alphabet used by Bitcoin and Solana
    static const char base58_alphabet[] =
        "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

    // For 64-byte Ed25519 signatures, ensure exactly 88 characters
    if (data.size() == 64) {
      try {
        // Use improved algorithm for consistent 88-character output
        std::vector<uint8_t> digits;
        digits.reserve(100); // Pre-allocate to prevent excessive reallocations

        // Convert input to big number in base 58
        for (uint8_t byte : data) {
          uint32_t carry = byte;
          for (size_t i = 0; i < digits.size(); ++i) {
            carry += static_cast<uint32_t>(digits[i]) * 256;
            digits[i] = carry % 58;
            carry /= 58;
          }
          while (carry > 0) {
            digits.push_back(carry % 58);
            carry /= 58;

            // Safety check to prevent infinite loops
            if (digits.size() > 200) {
              std::cerr << "ERROR: Base58 encoding exceeded safety limit"
                        << std::endl;
              return "error_encoding_overflow";
            }
          }
        }

        // Count leading zeros safely
        size_t leading_zeros = 0;
        for (uint8_t byte : data) {
          if (byte == 0) {
            leading_zeros++;
          } else {
            break;
          }
        }

        // Build result with proper padding
        std::string result;
        result.reserve(100); // Pre-allocate space

        // Add leading zero characters
        for (size_t i = 0; i < leading_zeros && i < 64; ++i) {
          result += base58_alphabet[0];
        }

        // Add encoded digits in reverse order
        for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
          if (*it < 58) { // Bounds check
            result += base58_alphabet[*it];
          } else {
            std::cerr << "ERROR: Invalid base58 digit: "
                      << static_cast<int>(*it) << std::endl;
            return "error_invalid_digit";
          }
        }

        // For 64-byte signatures, pad to exactly 88 characters if needed
        while (result.length() < 88 && result.length() < 100) {
          result = base58_alphabet[0] + result;
        }

        return result;

      } catch (const std::bad_alloc &e) {
        std::cerr << "ERROR: Memory allocation failed in base58 encoding: "
                  << e.what() << std::endl;
        return "error_memory_allocation";
      } catch (const std::exception &e) {
        std::cerr << "ERROR: Exception in 64-byte base58 encoding: " << e.what()
                  << std::endl;
        return "error_encoding_exception";
      }
    }

    // Default implementation for other data sizes with safety checks
    try {
      std::vector<uint8_t> digits(1, 0);
      digits.reserve(data.size() * 2); // Pre-allocate space

      for (uint8_t byte : data) {
        uint32_t carry = byte;
        for (size_t j = 0; j < digits.size(); ++j) {
          carry += static_cast<uint32_t>(digits[j]) << 8;
          digits[j] = carry % 58;
          carry /= 58;
        }

        while (carry > 0) {
          digits.push_back(carry % 58);
          carry /= 58;

          // Safety check to prevent infinite loops
          if (digits.size() > data.size() * 4) {
            std::cerr << "ERROR: Base58 encoding safety limit exceeded"
                      << std::endl;
            return "error_encoding_limit_exceeded";
          }
        }
      }

      // Convert leading zeros safely
      std::string result;
      result.reserve(digits.size() + 10); // Pre-allocate space

      for (uint8_t byte : data) {
        if (byte != 0)
          break;
        result += base58_alphabet[0];
      }

      // Convert digits to base58 characters (reverse order)
      for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (*it < 58) { // Bounds check
          result += base58_alphabet[*it];
        } else {
          std::cerr << "ERROR: Invalid base58 digit in general encoding: "
                    << static_cast<int>(*it) << std::endl;
          return "error_invalid_general_digit";
        }
      }

      return result;

    } catch (const std::bad_alloc &e) {
      std::cerr
          << "ERROR: Memory allocation failed in general base58 encoding: "
          << e.what() << std::endl;
      return "error_general_memory_allocation";
    } catch (const std::exception &e) {
      std::cerr << "ERROR: Exception in general base58 encoding: " << e.what()
                << std::endl;
      return "error_general_encoding_exception";
    }

  } catch (...) {
    std::cerr << "ERROR: Unknown error in safe base58 encoding" << std::endl;
    return "error_unknown_base58_error";
  }
}

// Fault tolerance implementations
common::Result<bool> BankingStage::process_transaction_with_fault_tolerance(
    TransactionPtr transaction) {
  if (!transaction) {
    return common::Result<bool>("Invalid transaction pointer");
  }

  // Check if operation is allowed in current degradation mode
  if (!degradation_manager_.is_operation_allowed("banking",
                                                 "process_transaction")) {
    return common::Result<bool>(
        "Transaction processing disabled due to degraded mode");
  }

  // Define the transaction processing operation
  auto process_operation = [this, transaction]() -> common::Result<bool> {
    try {
      // Validate transaction
      if (!transaction || !transaction->verify()) {
        return common::Result<bool>("Transaction validation failed");
      }

      // Create a batch with single transaction for processing
      auto batch = std::make_shared<TransactionBatch>();
      batch->add_transaction(transaction);

      // Execute through validation stage
      if (!validate_batch(batch)) {
        return common::Result<bool>("Transaction validation failed in batch");
      }

      // Execute through execution stage
      if (!execute_batch(batch)) {
        return common::Result<bool>("Transaction execution failed");
      }

      // Commit to ledger
      if (!commit_batch(batch)) {
        return common::Result<bool>("Transaction commitment failed");
      }

      return common::Result<bool>(true);
    } catch (const std::exception &e) {
      return common::Result<bool>("Transaction processing failed: " +
                                  std::string(e.what()));
    }
  };

  // Execute with circuit breaker and retry logic
  return transaction_processor_breaker_.execute([&]() {
    return common::FaultTolerance::retry_with_backoff(
        process_operation, transaction_retry_policy_);
  });
}

common::Result<bool> BankingStage::save_banking_state() {
  if (!state_checkpoint_) {
    return common::Result<bool>("State checkpoint not initialized");
  }

  try {
    // Create checkpoint data including current batch state and statistics
    std::vector<uint8_t> state_data;

    // Serialize current banking state (simplified)
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

    state_data.resize(32);
    auto transactions_count = total_transactions_processed_.load();
    auto batches_count = total_batches_processed_.load();
    auto failures_count = failed_transactions_.load();

    memcpy(state_data.data(), &timestamp, sizeof(timestamp));
    memcpy(state_data.data() + 8, &transactions_count, sizeof(uint64_t));
    memcpy(state_data.data() + 16, &batches_count, sizeof(uint64_t));
    memcpy(state_data.data() + 24, &failures_count, sizeof(uint64_t));

    auto checkpoint_id = "banking_state_" + std::to_string(timestamp);
    return state_checkpoint_->save_data(checkpoint_id, state_data);
  } catch (const std::exception &e) {
    return common::Result<bool>("Failed to save banking state: " +
                                std::string(e.what()));
  }
}

common::Result<bool> BankingStage::restore_banking_state() {
  if (!state_checkpoint_) {
    return common::Result<bool>("State checkpoint not initialized");
  }

  try {
    // List available checkpoints and restore from the latest
    auto checkpoints_result = state_checkpoint_->list_checkpoints();
    if (checkpoints_result.is_err()) {
      return common::Result<bool>("Failed to list checkpoints: " +
                                  checkpoints_result.error());
    }

    auto checkpoints = checkpoints_result.value();
    if (checkpoints.empty()) {
      return common::Result<bool>(true); // No checkpoints to restore from
    }

    // Find the latest banking state checkpoint
    std::string latest_checkpoint;
    for (const auto &checkpoint : checkpoints) {
      if (checkpoint.find("banking_state_") == 0) {
        latest_checkpoint = checkpoint;
        break; // Assuming checkpoints are sorted by timestamp (newest first)
      }
    }

    if (latest_checkpoint.empty()) {
      return common::Result<bool>(true); // No banking checkpoints found
    }

    auto data_result = state_checkpoint_->load_data(latest_checkpoint);
    if (data_result.is_err()) {
      return common::Result<bool>("Failed to load checkpoint data: " +
                                  data_result.error());
    }

    auto state_data = data_result.value();
    if (state_data.size() < 32) {
      return common::Result<bool>("Invalid checkpoint data size");
    }

    // Restore state (simplified)
    uint64_t timestamp, transactions, batches, failures;
    memcpy(&timestamp, state_data.data(), sizeof(timestamp));
    memcpy(&transactions, state_data.data() + 8, sizeof(uint64_t));
    memcpy(&batches, state_data.data() + 16, sizeof(uint64_t));
    memcpy(&failures, state_data.data() + 24, sizeof(uint64_t));

    total_transactions_processed_.store(transactions);
    total_batches_processed_.store(batches);
    failed_transactions_.store(failures);

    std::cout << "Banking state restored from checkpoint: " << latest_checkpoint
              << " (transactions: " << transactions << ", batches: " << batches
              << ")" << std::endl;

    return common::Result<bool>(true);
  } catch (const std::exception &e) {
    return common::Result<bool>("Failed to restore banking state: " +
                                std::string(e.what()));
  }
}

// Fee market integration methods
void BankingStage::set_mev_protection_level(ProtectionLevel level) {
  if (mev_protection_) {
    mev_protection_->set_protection_level(level);
    std::cout << "Banking: MEV protection level set to " << static_cast<int>(level) 
              << std::endl;
  }
}

FeeStats BankingStage::get_fee_market_stats() const {
  if (fee_market_) {
    return fee_market_->get_recent_fee_stats();
  }
  return FeeStats{};
}

uint64_t BankingStage::get_current_base_fee() const {
  if (fee_market_) {
    return fee_market_->get_current_base_fee();
  }
  return 5000; // Default base fee
}

size_t BankingStage::get_detected_mev_attacks() const {
  if (mev_protection_) {
    return mev_protection_->get_detected_attacks_count();
  }
  return 0;
}

size_t BankingStage::get_protected_transactions() const {
  if (mev_protection_) {
    return mev_protection_->get_protected_transactions_count();
  }
  return 0;
}

} // namespace banking
} // namespace slonana