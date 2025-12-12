#pragma once

#include "svm/engine.h"
#include "svm/ml_inference.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

namespace slonana {
namespace svm {

/**
 * Async BPF Execution Module
 * 
 * Implements Asynchronous Program Execution (APE) patterns for sBPF:
 * - Block-based timers for self-scheduling programs
 * - Account watchers for state-change triggers  
 * - Background execution for autonomous AI agents
 * - Event-driven execution without external triggers
 * 
 * This enables fully autonomous on-chain programs that can:
 * - Schedule themselves for future execution
 * - React to account state changes automatically
 * - Run ML inference in the background
 * - Make trading decisions without off-chain dependencies
 */

// ============================================================================
// Constants and Limits
// ============================================================================

constexpr uint64_t MAX_TIMERS_PER_PROGRAM = 16;
constexpr uint64_t MAX_WATCHERS_PER_PROGRAM = 32;
constexpr uint64_t MAX_SCHEDULED_TASKS = 1024;
constexpr uint64_t MAX_WATCHER_CALLBACKS = 256;
constexpr uint32_t DEFAULT_TIMER_SLOTS = 10;
constexpr uint64_t MAX_BACKGROUND_TASKS = 64;

// Timer precision in slots
constexpr uint64_t MIN_TIMER_SLOTS = 1;
constexpr uint64_t MAX_TIMER_SLOTS = 1000000; // ~5 days at 400ms slots

// Ring buffer limits
constexpr uint64_t MAX_RING_BUFFERS_PER_PROGRAM = 8;
constexpr uint64_t MAX_RING_BUFFER_SIZE = 1024 * 1024;  // 1MB max
constexpr uint64_t MIN_RING_BUFFER_SIZE = 64;           // 64 bytes min
constexpr uint64_t DEFAULT_RING_BUFFER_SIZE = 4096;     // 4KB default

// ============================================================================
// Ring Buffer Types (for async event communication)
// ============================================================================

/**
 * Ring buffer state
 */
enum class RingBufferState : uint8_t {
    ACTIVE = 0,       // Buffer is active and operational
    PAUSED = 1,       // Buffer operations paused
    CLOSED = 2,       // Buffer closed, no more operations allowed
    DRAINING = 3      // Buffer being drained before close
};

/**
 * Ring buffer entry header
 * 
 * NOTE: This struct is 24 bytes total (3x 8-byte aligned fields):
 * - sequence_number: 8 bytes
 * - timestamp_slot: 8 bytes  
 * - data_length + flags: 8 bytes combined
 * Naturally aligned for 64-bit platforms, no padding required.
 */
struct RingBufferEntry {
    uint64_t sequence_number;     // Monotonically increasing sequence
    uint64_t timestamp_slot;      // Slot when entry was written
    uint32_t data_length;         // Length of data following header
    uint32_t flags;               // Entry flags (priority, type, etc.)
    
    RingBufferEntry()
        : sequence_number(0), timestamp_slot(0), data_length(0), flags(0) {}
};

/**
 * Lock-free ring buffer for inter-program communication
 * Enables async event passing between programs and the runtime
 */
struct BpfRingBuffer {
    uint64_t buffer_id;
    std::string owner_program_id;
    RingBufferState state;
    
    // Buffer configuration
    uint64_t capacity;           // Total buffer size in bytes
    std::atomic<uint64_t> entry_count;  // Number of entries in buffer
    
    // Ring buffer pointers (atomic for lock-free operation)
    std::atomic<uint64_t> head;  // Write position
    std::atomic<uint64_t> tail;  // Read position
    std::atomic<uint64_t> sequence; // Next sequence number
    
    // Buffer storage
    std::vector<uint8_t> buffer;
    
    // Statistics
    uint64_t total_writes;
    uint64_t total_reads;
    uint64_t dropped_entries;    // Entries dropped due to full buffer
    uint64_t peak_usage;         // Peak bytes used
    
    // Created slot
    uint64_t created_at_slot;
    
    BpfRingBuffer()
        : buffer_id(0), state(RingBufferState::ACTIVE),
          capacity(DEFAULT_RING_BUFFER_SIZE), entry_count(0),
          head(0), tail(0), sequence(0),
          total_writes(0), total_reads(0), dropped_entries(0),
          peak_usage(0), created_at_slot(0) {}
    
    // Move constructor
    BpfRingBuffer(BpfRingBuffer&& other) noexcept
        : buffer_id(other.buffer_id),
          owner_program_id(std::move(other.owner_program_id)),
          state(other.state), capacity(other.capacity),
          entry_count(other.entry_count.load()),
          head(other.head.load()), tail(other.tail.load()),
          sequence(other.sequence.load()),
          buffer(std::move(other.buffer)),
          total_writes(other.total_writes), total_reads(other.total_reads),
          dropped_entries(other.dropped_entries), peak_usage(other.peak_usage),
          created_at_slot(other.created_at_slot) {}
    
    // Disable copy
    BpfRingBuffer(const BpfRingBuffer&) = delete;
    BpfRingBuffer& operator=(const BpfRingBuffer&) = delete;
};

// ============================================================================
// Timer Types
// ============================================================================

/**
 * Timer scheduling mode
 */
enum class TimerMode : uint8_t {
    ONE_SHOT = 0,     // Execute once at specified slot
    PERIODIC = 1,     // Execute every N slots
    CONDITIONAL = 2,  // Execute when condition met
    DEADLINE = 3      // Execute before deadline slot
};

/**
 * Timer state
 */
enum class TimerState : uint8_t {
    PENDING = 0,      // Waiting to fire
    EXECUTING = 1,    // Currently executing
    COMPLETED = 2,    // One-shot completed
    CANCELLED = 3,    // Manually cancelled
    EXPIRED = 4       // Deadline passed without execution
};

/**
 * Block-based timer for self-scheduling programs
 */
struct BpfTimer {
    uint64_t timer_id;
    std::string program_id;
    TimerMode mode;
    TimerState state;
    
    // Scheduling
    uint64_t trigger_slot;          // Slot when timer should fire
    uint64_t period_slots;          // For periodic timers
    uint64_t deadline_slot;         // For deadline mode
    uint64_t created_at_slot;
    
    // Execution context
    std::vector<uint8_t> callback_data;  // Data passed to callback
    std::vector<AccountInfo> accounts;   // Accounts to pass
    uint64_t compute_budget;             // Max CU for callback
    
    // Statistics
    uint64_t execution_count;
    uint64_t total_compute_used;
    uint64_t last_execution_slot;
    
    BpfTimer() 
        : timer_id(0), mode(TimerMode::ONE_SHOT), state(TimerState::PENDING),
          trigger_slot(0), period_slots(0), deadline_slot(0), created_at_slot(0),
          compute_budget(200000), execution_count(0), total_compute_used(0),
          last_execution_slot(0) {}
};

// ============================================================================
// Account Watcher Types
// ============================================================================

/**
 * Type of account change to watch for
 */
enum class WatcherTrigger : uint8_t {
    ANY_CHANGE = 0,       // Any modification to account data
    LAMPORT_CHANGE = 1,   // Balance change
    DATA_CHANGE = 2,      // Account data modification
    OWNER_CHANGE = 3,     // Owner field change
    THRESHOLD_ABOVE = 4,  // Value crosses threshold (above)
    THRESHOLD_BELOW = 5,  // Value crosses threshold (below)
    PATTERN_MATCH = 6     // Data matches specific pattern
};

/**
 * Account watcher for state-change triggers
 */
struct AccountWatcher {
    uint64_t watcher_id;
    std::string program_id;
    std::string watched_account;
    WatcherTrigger trigger_type;
    bool is_active;
    
    // Threshold configuration (for THRESHOLD_* triggers)
    uint64_t threshold_offset;   // Byte offset in account data
    int64_t threshold_value;     // Value to compare against
    
    // Pattern matching (for PATTERN_MATCH trigger)
    std::vector<uint8_t> pattern;
    uint64_t pattern_offset;
    
    // Callback configuration
    std::vector<uint8_t> callback_data;
    std::vector<AccountInfo> additional_accounts;
    uint64_t compute_budget;
    
    // State tracking
    std::vector<uint8_t> last_known_state;
    uint64_t last_trigger_slot;
    uint64_t trigger_count;
    
    // Rate limiting
    uint64_t min_slots_between_triggers;
    
    AccountWatcher()
        : watcher_id(0), trigger_type(WatcherTrigger::ANY_CHANGE),
          is_active(true), threshold_offset(0), threshold_value(0),
          pattern_offset(0), compute_budget(200000), last_trigger_slot(0),
          trigger_count(0), min_slots_between_triggers(1) {}
};

// ============================================================================
// Async Task Types
// ============================================================================

/**
 * Priority levels for async tasks
 */
enum class AsyncPriority : uint8_t {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    CRITICAL = 3
};

/**
 * Async execution result (different from SVM ExecutionResult enum)
 */
struct AsyncExecutionResult {
    bool success;
    uint64_t compute_units_consumed;
    std::string error_message;
    std::vector<uint8_t> return_data;
    
    AsyncExecutionResult() 
        : success(false), compute_units_consumed(0) {}
    
    AsyncExecutionResult(bool s, uint64_t cu, const std::string& err = "")
        : success(s), compute_units_consumed(cu), error_message(err) {}
};

/**
 * Async task for background execution
 */
struct AsyncTask {
    uint64_t task_id;
    std::string program_id;
    AsyncPriority priority;
    
    // Task type
    enum class Type {
        TIMER_CALLBACK,
        WATCHER_CALLBACK,
        ML_INFERENCE,
        SCHEDULED_EXECUTION,
        BACKGROUND_COMPUTE
    } type;
    
    // Execution context
    std::vector<uint8_t> bytecode;
    std::vector<uint8_t> instruction_data;
    std::vector<AccountInfo> accounts;
    uint64_t compute_budget;
    
    // Scheduling
    uint64_t scheduled_slot;
    uint64_t deadline_slot;
    std::chrono::steady_clock::time_point created_at;
    
    // Result handling
    std::promise<AsyncExecutionResult> result_promise;
    std::function<void(const AsyncExecutionResult&)> completion_callback;
    
    // Dependencies
    std::vector<uint64_t> depends_on_tasks;
    bool all_dependencies_complete;
    
    AsyncTask()
        : task_id(0), priority(AsyncPriority::NORMAL), type(Type::SCHEDULED_EXECUTION),
          compute_budget(200000), scheduled_slot(0), deadline_slot(0),
          all_dependencies_complete(true) {}
    
    // Move constructor for promise
    AsyncTask(AsyncTask&& other) noexcept
        : task_id(other.task_id), program_id(std::move(other.program_id)),
          priority(other.priority), type(other.type),
          bytecode(std::move(other.bytecode)),
          instruction_data(std::move(other.instruction_data)),
          accounts(std::move(other.accounts)),
          compute_budget(other.compute_budget),
          scheduled_slot(other.scheduled_slot),
          deadline_slot(other.deadline_slot),
          created_at(other.created_at),
          result_promise(std::move(other.result_promise)),
          completion_callback(std::move(other.completion_callback)),
          depends_on_tasks(std::move(other.depends_on_tasks)),
          all_dependencies_complete(other.all_dependencies_complete) {}
    
    AsyncTask& operator=(AsyncTask&& other) noexcept {
        if (this != &other) {
            task_id = other.task_id;
            program_id = std::move(other.program_id);
            priority = other.priority;
            type = other.type;
            bytecode = std::move(other.bytecode);
            instruction_data = std::move(other.instruction_data);
            accounts = std::move(other.accounts);
            compute_budget = other.compute_budget;
            scheduled_slot = other.scheduled_slot;
            deadline_slot = other.deadline_slot;
            created_at = other.created_at;
            result_promise = std::move(other.result_promise);
            completion_callback = std::move(other.completion_callback);
            depends_on_tasks = std::move(other.depends_on_tasks);
            all_dependencies_complete = other.all_dependencies_complete;
        }
        return *this;
    }
};

// ============================================================================
// Statistics and Monitoring
// ============================================================================

/**
 * Async execution statistics
 */
struct AsyncExecutionStats {
    // Timer stats
    uint64_t total_timers_created;
    uint64_t total_timer_executions;
    uint64_t timer_execution_failures;
    uint64_t timers_currently_active;
    
    // Watcher stats
    uint64_t total_watchers_created;
    uint64_t total_watcher_triggers;
    uint64_t watcher_false_positives;
    uint64_t watchers_currently_active;
    
    // Task stats
    uint64_t total_tasks_queued;
    uint64_t total_tasks_completed;
    uint64_t total_tasks_failed;
    uint64_t tasks_currently_pending;
    
    // Performance
    uint64_t total_compute_used;
    double average_task_latency_us;
    double average_timer_accuracy_slots;
    uint64_t peak_concurrent_tasks;
    
    // Resource usage
    size_t memory_used_bytes;
    double cpu_utilization_percent;
    
    AsyncExecutionStats()
        : total_timers_created(0), total_timer_executions(0),
          timer_execution_failures(0), timers_currently_active(0),
          total_watchers_created(0), total_watcher_triggers(0),
          watcher_false_positives(0), watchers_currently_active(0),
          total_tasks_queued(0), total_tasks_completed(0),
          total_tasks_failed(0), tasks_currently_pending(0),
          total_compute_used(0), average_task_latency_us(0),
          average_timer_accuracy_slots(0), peak_concurrent_tasks(0),
          memory_used_bytes(0), cpu_utilization_percent(0) {}
};

// ============================================================================
// Timer Manager
// ============================================================================

/**
 * Manages block-based timers for autonomous program scheduling
 */
class TimerManager {
public:
    TimerManager();
    ~TimerManager();
    
    /**
     * Create a new timer
     * @return Timer ID or 0 on failure
     */
    uint64_t create_timer(
        const std::string& program_id,
        TimerMode mode,
        uint64_t trigger_slot,
        uint64_t period_slots = 0,
        const std::vector<uint8_t>& callback_data = {},
        const std::vector<AccountInfo>& accounts = {},
        uint64_t compute_budget = 200000
    );
    
    /**
     * Cancel a timer
     */
    bool cancel_timer(uint64_t timer_id);
    
    /**
     * Get timer info
     */
    const BpfTimer* get_timer(uint64_t timer_id) const;
    
    /**
     * Check and fire ready timers for current slot
     * @return List of tasks to execute
     */
    std::vector<AsyncTask> check_timers(uint64_t current_slot);
    
    /**
     * Get all timers for a program
     */
    std::vector<const BpfTimer*> get_program_timers(const std::string& program_id) const;
    
    /**
     * Get statistics
     */
    void get_stats(AsyncExecutionStats& stats) const;
    
    /**
     * Clear expired timers
     */
    void cleanup_expired(uint64_t current_slot);
    
private:
    std::unordered_map<uint64_t, BpfTimer> timers_;
    std::unordered_map<std::string, std::vector<uint64_t>> program_timers_;
    mutable std::mutex mutex_;
    std::atomic<uint64_t> next_timer_id_{1};
    
    void reschedule_periodic_timer(BpfTimer& timer, uint64_t current_slot);
    AsyncTask create_timer_task(const BpfTimer& timer, uint64_t current_slot);
};

// ============================================================================
// Account Watcher Manager
// ============================================================================

/**
 * Manages account watchers for state-change triggers
 */
class AccountWatcherManager {
public:
    AccountWatcherManager();
    ~AccountWatcherManager();
    
    /**
     * Create a new account watcher
     * @return Watcher ID or 0 on failure
     */
    uint64_t create_watcher(
        const std::string& program_id,
        const std::string& watched_account,
        WatcherTrigger trigger_type,
        const std::vector<uint8_t>& callback_data = {},
        const std::vector<AccountInfo>& additional_accounts = {},
        uint64_t compute_budget = 200000
    );
    
    /**
     * Create a threshold watcher
     */
    uint64_t create_threshold_watcher(
        const std::string& program_id,
        const std::string& watched_account,
        WatcherTrigger trigger_type,
        uint64_t threshold_offset,
        int64_t threshold_value,
        const std::vector<uint8_t>& callback_data = {},
        uint64_t compute_budget = 200000
    );
    
    /**
     * Create a pattern watcher
     */
    uint64_t create_pattern_watcher(
        const std::string& program_id,
        const std::string& watched_account,
        const std::vector<uint8_t>& pattern,
        uint64_t pattern_offset,
        const std::vector<uint8_t>& callback_data = {},
        uint64_t compute_budget = 200000
    );
    
    /**
     * Remove a watcher
     */
    bool remove_watcher(uint64_t watcher_id);
    
    /**
     * Pause/resume a watcher
     */
    bool set_watcher_active(uint64_t watcher_id, bool active);
    
    /**
     * Get watcher info
     */
    const AccountWatcher* get_watcher(uint64_t watcher_id) const;
    
    /**
     * Check account change and generate tasks if triggered
     * @param account_pubkey Account that changed
     * @param old_data Previous account data
     * @param new_data New account data
     * @param current_slot Current slot number
     * @return Tasks to execute for triggered watchers
     */
    std::vector<AsyncTask> check_account_change(
        const std::string& account_pubkey,
        const std::vector<uint8_t>& old_data,
        const std::vector<uint8_t>& new_data,
        uint64_t old_lamports,
        uint64_t new_lamports,
        uint64_t current_slot
    );
    
    /**
     * Get all watchers for an account
     */
    std::vector<const AccountWatcher*> get_account_watchers(
        const std::string& account_pubkey) const;
    
    /**
     * Get statistics
     */
    void get_stats(AsyncExecutionStats& stats) const;
    
private:
    std::unordered_map<uint64_t, AccountWatcher> watchers_;
    std::unordered_map<std::string, std::vector<uint64_t>> account_watchers_;
    std::unordered_map<std::string, std::vector<uint64_t>> program_watchers_;
    mutable std::mutex mutex_;
    std::atomic<uint64_t> next_watcher_id_{1};
    
    bool should_trigger(
        const AccountWatcher& watcher,
        const std::vector<uint8_t>& old_data,
        const std::vector<uint8_t>& new_data,
        uint64_t old_lamports,
        uint64_t new_lamports
    ) const;
    
    AsyncTask create_watcher_task(
        const AccountWatcher& watcher,
        uint64_t current_slot
    );
};

// ============================================================================
// Ring Buffer Manager
// ============================================================================

/**
 * Manages ring buffers for async event communication
 */
class RingBufferManager {
public:
    RingBufferManager();
    ~RingBufferManager();
    
    /**
     * Create a new ring buffer (sol_ring_buffer_create)
     * @param program_id Program creating the buffer
     * @param size Buffer size in bytes
     * @return Buffer ID or 0 on failure
     */
    uint64_t create_buffer(
        const std::string& program_id,
        uint64_t size = DEFAULT_RING_BUFFER_SIZE
    );
    
    /**
     * Destroy a ring buffer (sol_ring_buffer_destroy)
     */
    bool destroy_buffer(uint64_t buffer_id);
    
    /**
     * Push data to ring buffer (sol_ring_buffer_push)
     * @return true on success, false if buffer full or invalid
     */
    bool push(
        uint64_t buffer_id,
        const uint8_t* data,
        uint32_t data_len,
        uint64_t current_slot,
        uint32_t flags = 0
    );
    
    /**
     * Pop data from ring buffer (sol_ring_buffer_pop)
     * @return Data vector (empty if no data available)
     */
    std::vector<uint8_t> pop(uint64_t buffer_id);
    
    /**
     * Peek at next entry without removing (sol_ring_buffer_peek)
     */
    std::vector<uint8_t> peek(uint64_t buffer_id) const;
    
    /**
     * Get buffer info
     */
    const BpfRingBuffer* get_buffer(uint64_t buffer_id) const;
    
    /**
     * Check if buffer has data
     */
    bool has_data(uint64_t buffer_id) const;
    
    /**
     * Get number of entries in buffer
     */
    uint64_t get_entry_count(uint64_t buffer_id) const;
    
    /**
     * Get buffer capacity usage (0.0 to 1.0)
     */
    double get_usage(uint64_t buffer_id) const;
    
    /**
     * Get statistics
     */
    void get_stats(AsyncExecutionStats& stats) const;
    
    /**
     * Pause/resume buffer
     */
    bool set_buffer_state(uint64_t buffer_id, RingBufferState state);
    
    /**
     * Get all buffers for a program
     */
    std::vector<uint64_t> get_program_buffers(const std::string& program_id) const;
    
private:
    std::unordered_map<uint64_t, std::unique_ptr<BpfRingBuffer>> buffers_;
    std::unordered_map<std::string, std::vector<uint64_t>> program_buffers_;
    mutable std::mutex mutex_;
    std::atomic<uint64_t> next_buffer_id_{1};
    
    // Statistics
    std::atomic<uint64_t> total_buffers_created_{0};
    std::atomic<uint64_t> total_bytes_written_{0};
    std::atomic<uint64_t> total_bytes_read_{0};
    
    uint64_t calculate_available_space(const BpfRingBuffer& buffer) const;
};

// ============================================================================
// Async Task Scheduler
// ============================================================================

/**
 * Schedules and executes async tasks
 */
class AsyncTaskScheduler {
public:
    AsyncTaskScheduler(size_t num_workers = 4);
    ~AsyncTaskScheduler();
    
    /**
     * Initialize the scheduler
     */
    bool initialize();
    
    /**
     * Shutdown the scheduler
     */
    void shutdown();
    
    /**
     * Submit a task for execution
     * @return Future for the result
     */
    std::future<AsyncExecutionResult> submit_task(AsyncTask task);
    
    /**
     * Submit multiple tasks
     */
    std::vector<std::future<AsyncExecutionResult>> submit_tasks(
        std::vector<AsyncTask> tasks
    );
    
    /**
     * Set current slot (for timer/scheduling purposes)
     */
    void set_current_slot(uint64_t slot);
    
    /**
     * Get current slot
     */
    uint64_t get_current_slot() const;
    
    /**
     * Get pending task count
     */
    size_t get_pending_count() const;
    
    /**
     * Get statistics
     */
    void get_stats(AsyncExecutionStats& stats) const;
    
    /**
     * Clear completed tasks
     */
    void cleanup();
    
private:
    std::vector<std::thread> workers_;
    std::priority_queue<
        AsyncTask*,
        std::vector<AsyncTask*>,
        std::function<bool(AsyncTask*, AsyncTask*)>
    > task_queue_;
    std::vector<std::unique_ptr<AsyncTask>> task_storage_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> current_slot_{0};
    std::atomic<uint64_t> next_task_id_{1};
    size_t num_workers_;
    
    // Statistics
    std::atomic<uint64_t> tasks_completed_{0};
    std::atomic<uint64_t> tasks_failed_{0};
    std::atomic<uint64_t> total_latency_us_{0};
    
    void worker_loop();
    AsyncExecutionResult execute_task(AsyncTask& task);
    static bool task_priority_compare(AsyncTask* a, AsyncTask* b);
};

// ============================================================================
// Async BPF Execution Engine
// ============================================================================

/**
 * Main async BPF execution engine
 * Coordinates timers, watchers, and task scheduling for autonomous programs
 */
class AsyncBpfExecutionEngine {
public:
    AsyncBpfExecutionEngine(size_t num_workers = 4);
    ~AsyncBpfExecutionEngine();
    
    /**
     * Initialize the engine
     */
    bool initialize();
    
    /**
     * Shutdown the engine
     */
    void shutdown();
    
    /**
     * Check if engine is running
     */
    bool is_running() const { return running_.load(); }
    
    // ========================================================================
    // Timer API (sol_timer_* syscalls)
    // ========================================================================
    
    /**
     * Create a one-shot timer (sol_timer_create)
     * @param program_id Program creating the timer
     * @param trigger_slot Slot when timer should fire
     * @param callback_data Data to pass to callback
     * @param accounts Accounts to include in callback
     * @param compute_budget Compute budget for callback
     * @return Timer ID or 0 on failure
     */
    uint64_t sol_timer_create(
        const std::string& program_id,
        uint64_t trigger_slot,
        const std::vector<uint8_t>& callback_data,
        const std::vector<AccountInfo>& accounts,
        uint64_t compute_budget
    );
    
    /**
     * Create a periodic timer (sol_timer_create_periodic)
     */
    uint64_t sol_timer_create_periodic(
        const std::string& program_id,
        uint64_t start_slot,
        uint64_t period_slots,
        const std::vector<uint8_t>& callback_data,
        const std::vector<AccountInfo>& accounts,
        uint64_t compute_budget
    );
    
    /**
     * Cancel a timer (sol_timer_cancel)
     */
    bool sol_timer_cancel(uint64_t timer_id);
    
    /**
     * Get timer info (sol_timer_get_info)
     */
    const BpfTimer* sol_timer_get_info(uint64_t timer_id) const;
    
    // ========================================================================
    // Account Watcher API (sol_watcher_* syscalls)
    // ========================================================================
    
    /**
     * Create an account watcher (sol_watcher_create)
     */
    uint64_t sol_watcher_create(
        const std::string& program_id,
        const std::string& watched_account,
        WatcherTrigger trigger_type,
        const std::vector<uint8_t>& callback_data,
        const std::vector<AccountInfo>& additional_accounts,
        uint64_t compute_budget
    );
    
    /**
     * Create threshold watcher (sol_watcher_create_threshold)
     */
    uint64_t sol_watcher_create_threshold(
        const std::string& program_id,
        const std::string& watched_account,
        WatcherTrigger trigger_type,
        uint64_t offset,
        int64_t threshold,
        const std::vector<uint8_t>& callback_data,
        uint64_t compute_budget
    );
    
    /**
     * Remove a watcher (sol_watcher_remove)
     */
    bool sol_watcher_remove(uint64_t watcher_id);
    
    /**
     * Get watcher info (sol_watcher_get_info)
     */
    const AccountWatcher* sol_watcher_get_info(uint64_t watcher_id) const;
    
    // ========================================================================
    // Ring Buffer API (sol_ring_buffer_* syscalls)
    // ========================================================================
    
    /**
     * Create a ring buffer (sol_ring_buffer_create)
     * @param program_id Program creating the buffer
     * @param size Buffer size in bytes
     * @return Buffer ID or 0 on failure
     */
    uint64_t sol_ring_buffer_create(
        const std::string& program_id,
        uint64_t size = DEFAULT_RING_BUFFER_SIZE
    );
    
    /**
     * Push data to ring buffer (sol_ring_buffer_push)
     * @param buffer_id Buffer to push to
     * @param data Data to push
     * @param data_len Length of data
     * @param flags Entry flags
     * @return true on success
     */
    bool sol_ring_buffer_push(
        uint64_t buffer_id,
        const uint8_t* data,
        uint32_t data_len,
        uint32_t flags = 0
    );
    
    /**
     * Pop data from ring buffer (sol_ring_buffer_pop)
     * @param buffer_id Buffer to pop from
     * @return Data vector (empty if no data)
     */
    std::vector<uint8_t> sol_ring_buffer_pop(uint64_t buffer_id);
    
    /**
     * Destroy a ring buffer (sol_ring_buffer_destroy)
     */
    bool sol_ring_buffer_destroy(uint64_t buffer_id);
    
    /**
     * Get ring buffer info
     */
    const BpfRingBuffer* sol_ring_buffer_get_info(uint64_t buffer_id) const;
    
    // ========================================================================
    // Slot Processing
    // ========================================================================
    
    /**
     * Process a new slot
     * Checks timers and executes ready callbacks
     */
    void process_slot(uint64_t slot);
    
    /**
     * Notify account change
     * Triggers relevant watchers
     */
    void notify_account_change(
        const std::string& account_pubkey,
        const std::vector<uint8_t>& old_data,
        const std::vector<uint8_t>& new_data,
        uint64_t old_lamports,
        uint64_t new_lamports
    );
    
    // ========================================================================
    // Statistics and Monitoring
    // ========================================================================
    
    /**
     * Get comprehensive statistics
     */
    AsyncExecutionStats get_stats() const;
    
    /**
     * Reset statistics
     */
    void reset_stats();
    
    /**
     * Get current slot
     */
    uint64_t get_current_slot() const { return current_slot_.load(); }
    
private:
    std::unique_ptr<TimerManager> timer_manager_;
    std::unique_ptr<AccountWatcherManager> watcher_manager_;
    std::unique_ptr<AsyncTaskScheduler> task_scheduler_;
    std::unique_ptr<RingBufferManager> ring_buffer_manager_;
    
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> current_slot_{0};
    
    std::thread slot_processor_thread_;
    mutable std::mutex slot_mutex_;
    std::condition_variable slot_cv_;
    
    void slot_processor_loop();
};

// ============================================================================
// Extern "C" Syscall Wrappers
// ============================================================================

extern "C" {

/**
 * Create a one-shot timer
 * @param trigger_slot Slot when timer should fire
 * @param callback_data Data to pass to callback
 * @param callback_data_len Length of callback data
 * @param compute_budget Compute budget for callback
 * @return Timer ID or 0 on failure
 */
uint64_t sol_timer_create(
    uint64_t trigger_slot,
    const uint8_t* callback_data,
    uint64_t callback_data_len,
    uint64_t compute_budget
);

/**
 * Create a periodic timer
 * @param start_slot First slot to trigger
 * @param period_slots Slots between triggers
 * @param callback_data Data to pass to callback
 * @param callback_data_len Length of callback data
 * @param compute_budget Compute budget for callback
 * @return Timer ID or 0 on failure
 */
uint64_t sol_timer_create_periodic(
    uint64_t start_slot,
    uint64_t period_slots,
    const uint8_t* callback_data,
    uint64_t callback_data_len,
    uint64_t compute_budget
);

/**
 * Cancel a timer
 * @param timer_id Timer to cancel
 * @return 0 on success, error code on failure
 */
uint64_t sol_timer_cancel(uint64_t timer_id);

/**
 * Create an account watcher
 * @param account_pubkey Account to watch (32 bytes)
 * @param trigger_type Type of change to watch for
 * @param callback_data Data to pass to callback
 * @param callback_data_len Length of callback data
 * @param compute_budget Compute budget for callback
 * @return Watcher ID or 0 on failure
 */
uint64_t sol_watcher_create(
    const uint8_t* account_pubkey,
    uint8_t trigger_type,
    const uint8_t* callback_data,
    uint64_t callback_data_len,
    uint64_t compute_budget
);

/**
 * Create a threshold watcher
 * @param account_pubkey Account to watch (32 bytes)
 * @param trigger_type THRESHOLD_ABOVE or THRESHOLD_BELOW
 * @param offset Byte offset in account data
 * @param threshold Value to compare against
 * @param callback_data Data to pass to callback
 * @param callback_data_len Length of callback data
 * @param compute_budget Compute budget for callback
 * @return Watcher ID or 0 on failure
 */
uint64_t sol_watcher_create_threshold(
    const uint8_t* account_pubkey,
    uint8_t trigger_type,
    uint64_t offset,
    int64_t threshold,
    const uint8_t* callback_data,
    uint64_t callback_data_len,
    uint64_t compute_budget
);

/**
 * Remove an account watcher
 * @param watcher_id Watcher to remove
 * @return 0 on success, error code on failure
 */
uint64_t sol_watcher_remove(uint64_t watcher_id);

/**
 * Create a ring buffer
 * @param size Buffer size in bytes
 * @return Buffer ID or 0 on failure
 */
uint64_t sol_ring_buffer_create(uint64_t size);

/**
 * Push data to ring buffer
 * @param buffer_id Buffer to push to
 * @param data Data to push
 * @param data_len Length of data
 * @return 0 on success, error code on failure
 */
uint64_t sol_ring_buffer_push(
    uint64_t buffer_id,
    const uint8_t* data,
    uint64_t data_len
);

/**
 * Pop data from ring buffer
 * @param buffer_id Buffer to pop from
 * @param output Buffer to write data to
 * @param output_len Maximum bytes to read
 * @return Number of bytes read, 0 if empty
 */
uint64_t sol_ring_buffer_pop(
    uint64_t buffer_id,
    uint8_t* output,
    uint64_t output_len
);

/**
 * Destroy a ring buffer
 * @param buffer_id Buffer to destroy
 * @return 0 on success, error code on failure
 */
uint64_t sol_ring_buffer_destroy(uint64_t buffer_id);

/**
 * Get current slot
 * @return Current slot number
 */
uint64_t sol_get_slot();

} // extern "C"

} // namespace svm
} // namespace slonana
