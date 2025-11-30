#include <iostream>
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>
#include <string>

#include "svm/async_bpf_execution.h"

using namespace slonana::svm;

// Simple test assertion macro
#define ASSERT_TRUE(condition) \
    if (!(condition)) { \
        throw std::runtime_error(std::string("Assertion failed: ") + #condition); \
    }

#define ASSERT_FALSE(condition) \
    if (condition) { \
        throw std::runtime_error(std::string("Assertion failed: !") + #condition); \
    }

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { \
        throw std::runtime_error(std::string("Assertion failed: ") + #a + " == " + #b); \
    }

#define ASSERT_NE(a, b) \
    if ((a) == (b)) { \
        throw std::runtime_error(std::string("Assertion failed: ") + #a + " != " + #b); \
    }

#define ASSERT_GT(a, b) \
    if (!((a) > (b))) { \
        throw std::runtime_error(std::string("Assertion failed: ") + #a + " > " + #b); \
    }

namespace {

// ============================================================================
// Timer Manager Tests
// ============================================================================

void test_timer_create_one_shot() {
    std::cout << "Testing timer creation (one-shot)...\n";
    
    TimerManager manager;
    
    uint64_t timer_id = manager.create_timer(
        "test_program",
        TimerMode::ONE_SHOT,
        100,  // trigger at slot 100
        0,    // no period
        {1, 2, 3},  // callback data
        {},   // no accounts
        200000
    );
    
    ASSERT_NE(timer_id, 0u);
    
    const BpfTimer* timer = manager.get_timer(timer_id);
    ASSERT_TRUE(timer != nullptr);
    ASSERT_EQ(timer->program_id, "test_program");
    ASSERT_EQ(timer->mode, TimerMode::ONE_SHOT);
    ASSERT_EQ(timer->trigger_slot, 100u);
    ASSERT_EQ(timer->state, TimerState::PENDING);
    
    std::cout << "  ✓ One-shot timer creation tests passed\n";
}

void test_timer_create_periodic() {
    std::cout << "Testing timer creation (periodic)...\n";
    
    TimerManager manager;
    
    uint64_t timer_id = manager.create_timer(
        "test_program",
        TimerMode::PERIODIC,
        100,  // start at slot 100
        10,   // fire every 10 slots
        {},
        {},
        200000
    );
    
    ASSERT_NE(timer_id, 0u);
    
    const BpfTimer* timer = manager.get_timer(timer_id);
    ASSERT_TRUE(timer != nullptr);
    ASSERT_EQ(timer->mode, TimerMode::PERIODIC);
    ASSERT_EQ(timer->period_slots, 10u);
    
    std::cout << "  ✓ Periodic timer creation tests passed\n";
}

void test_timer_cancel() {
    std::cout << "Testing timer cancellation...\n";
    
    TimerManager manager;
    
    uint64_t timer_id = manager.create_timer(
        "test_program",
        TimerMode::ONE_SHOT,
        100,
        0,
        {},
        {},
        200000
    );
    
    ASSERT_TRUE(manager.cancel_timer(timer_id));
    
    const BpfTimer* timer = manager.get_timer(timer_id);
    ASSERT_TRUE(timer != nullptr);
    ASSERT_EQ(timer->state, TimerState::CANCELLED);
    
    // Cancel non-existent timer
    ASSERT_FALSE(manager.cancel_timer(99999));
    
    std::cout << "  ✓ Timer cancellation tests passed\n";
}

void test_timer_check_fires() {
    std::cout << "Testing timer fires at correct slot...\n";
    
    TimerManager manager;
    
    uint64_t timer_id = manager.create_timer(
        "test_program",
        TimerMode::ONE_SHOT,
        100,
        0,
        {42},
        {},
        200000
    );
    
    // Check before trigger slot - should not fire
    auto tasks = manager.check_timers(50);
    ASSERT_EQ(tasks.size(), 0u);
    
    // Check at trigger slot - should fire
    tasks = manager.check_timers(100);
    ASSERT_EQ(tasks.size(), 1u);
    ASSERT_EQ(tasks[0].type, AsyncTask::Type::TIMER_CALLBACK);
    ASSERT_EQ(tasks[0].instruction_data.size(), 1u);
    ASSERT_EQ(tasks[0].instruction_data[0], 42);
    
    // Timer should be completed now
    const BpfTimer* timer = manager.get_timer(timer_id);
    ASSERT_EQ(timer->state, TimerState::COMPLETED);
    
    // Should not fire again
    tasks = manager.check_timers(101);
    ASSERT_EQ(tasks.size(), 0u);
    
    std::cout << "  ✓ Timer firing tests passed\n";
}

void test_timer_periodic_rescheduling() {
    std::cout << "Testing periodic timer rescheduling...\n";
    
    TimerManager manager;
    
    uint64_t timer_id = manager.create_timer(
        "test_program",
        TimerMode::PERIODIC,
        100,  // start at slot 100
        10,   // fire every 10 slots
        {},
        {},
        200000
    );
    
    // Fire at slot 100
    auto tasks = manager.check_timers(100);
    ASSERT_EQ(tasks.size(), 1u);
    
    const BpfTimer* timer = manager.get_timer(timer_id);
    ASSERT_EQ(timer->state, TimerState::PENDING);  // Should still be pending
    ASSERT_EQ(timer->trigger_slot, 110u);  // Rescheduled to slot 110
    
    // Fire at slot 110
    tasks = manager.check_timers(110);
    ASSERT_EQ(tasks.size(), 1u);
    
    timer = manager.get_timer(timer_id);
    ASSERT_EQ(timer->trigger_slot, 120u);  // Rescheduled to slot 120
    ASSERT_EQ(timer->execution_count, 2u);
    
    std::cout << "  ✓ Periodic timer rescheduling tests passed\n";
}

void test_timer_limits() {
    std::cout << "Testing timer limits...\n";
    
    TimerManager manager;
    
    // Create maximum number of timers for one program
    for (uint64_t i = 0; i < MAX_TIMERS_PER_PROGRAM; i++) {
        uint64_t timer_id = manager.create_timer(
            "test_program",
            TimerMode::ONE_SHOT,
            100 + i,
            0,
            {},
            {},
            200000
        );
        ASSERT_NE(timer_id, 0u);
    }
    
    // Should fail to create more
    uint64_t extra_timer = manager.create_timer(
        "test_program",
        TimerMode::ONE_SHOT,
        200,
        0,
        {},
        {},
        200000
    );
    ASSERT_EQ(extra_timer, 0u);
    
    // But different program can still create timers
    uint64_t other_program_timer = manager.create_timer(
        "other_program",
        TimerMode::ONE_SHOT,
        200,
        0,
        {},
        {},
        200000
    );
    ASSERT_NE(other_program_timer, 0u);
    
    std::cout << "  ✓ Timer limits tests passed\n";
}

// ============================================================================
// Account Watcher Tests
// ============================================================================

void test_watcher_create() {
    std::cout << "Testing watcher creation...\n";
    
    AccountWatcherManager manager;
    
    uint64_t watcher_id = manager.create_watcher(
        "test_program",
        "account_pubkey_123",
        WatcherTrigger::ANY_CHANGE,
        {1, 2, 3},
        {},
        200000
    );
    
    ASSERT_NE(watcher_id, 0u);
    
    const AccountWatcher* watcher = manager.get_watcher(watcher_id);
    ASSERT_TRUE(watcher != nullptr);
    ASSERT_EQ(watcher->program_id, "test_program");
    ASSERT_EQ(watcher->watched_account, "account_pubkey_123");
    ASSERT_EQ(watcher->trigger_type, WatcherTrigger::ANY_CHANGE);
    ASSERT_TRUE(watcher->is_active);
    
    std::cout << "  ✓ Watcher creation tests passed\n";
}

void test_watcher_threshold() {
    std::cout << "Testing threshold watcher...\n";
    
    AccountWatcherManager manager;
    
    uint64_t watcher_id = manager.create_threshold_watcher(
        "test_program",
        "account_pubkey_123",
        WatcherTrigger::THRESHOLD_ABOVE,
        0,     // offset
        1000,  // threshold
        {42},
        200000
    );
    
    ASSERT_NE(watcher_id, 0u);
    
    const AccountWatcher* watcher = manager.get_watcher(watcher_id);
    ASSERT_TRUE(watcher != nullptr);
    ASSERT_EQ(watcher->trigger_type, WatcherTrigger::THRESHOLD_ABOVE);
    ASSERT_EQ(watcher->threshold_offset, 0u);
    ASSERT_EQ(watcher->threshold_value, 1000);
    
    std::cout << "  ✓ Threshold watcher creation tests passed\n";
}

void test_watcher_pattern() {
    std::cout << "Testing pattern watcher...\n";
    
    AccountWatcherManager manager;
    
    std::vector<uint8_t> pattern = {0xDE, 0xAD, 0xBE, 0xEF};
    
    uint64_t watcher_id = manager.create_pattern_watcher(
        "test_program",
        "account_pubkey_123",
        pattern,
        10,  // offset
        {},
        200000
    );
    
    ASSERT_NE(watcher_id, 0u);
    
    const AccountWatcher* watcher = manager.get_watcher(watcher_id);
    ASSERT_TRUE(watcher != nullptr);
    ASSERT_EQ(watcher->trigger_type, WatcherTrigger::PATTERN_MATCH);
    ASSERT_EQ(watcher->pattern, pattern);
    ASSERT_EQ(watcher->pattern_offset, 10u);
    
    std::cout << "  ✓ Pattern watcher creation tests passed\n";
}

void test_watcher_triggers_on_change() {
    std::cout << "Testing watcher triggers on account change...\n";
    
    AccountWatcherManager manager;
    
    manager.create_watcher(
        "test_program",
        "account_123",
        WatcherTrigger::ANY_CHANGE,
        {},
        {},
        200000
    );
    
    std::vector<uint8_t> old_data = {1, 2, 3, 4};
    std::vector<uint8_t> new_data = {1, 2, 3, 5};  // Changed
    
    auto tasks = manager.check_account_change(
        "account_123", old_data, new_data, 1000, 1000, 100
    );
    
    ASSERT_EQ(tasks.size(), 1u);
    ASSERT_EQ(tasks[0].type, AsyncTask::Type::WATCHER_CALLBACK);
    
    std::cout << "  ✓ Watcher trigger tests passed\n";
}

void test_watcher_no_trigger_same_data() {
    std::cout << "Testing watcher does not trigger on same data...\n";
    
    AccountWatcherManager manager;
    
    manager.create_watcher(
        "test_program",
        "account_123",
        WatcherTrigger::DATA_CHANGE,
        {},
        {},
        200000
    );
    
    std::vector<uint8_t> data = {1, 2, 3, 4};
    
    // Same data, different lamports - DATA_CHANGE should not trigger
    auto tasks = manager.check_account_change(
        "account_123", data, data, 1000, 2000, 100
    );
    
    ASSERT_EQ(tasks.size(), 0u);
    
    std::cout << "  ✓ Watcher no-trigger tests passed\n";
}

void test_watcher_lamport_change() {
    std::cout << "Testing lamport change watcher...\n";
    
    AccountWatcherManager manager;
    
    manager.create_watcher(
        "test_program",
        "account_123",
        WatcherTrigger::LAMPORT_CHANGE,
        {},
        {},
        200000
    );
    
    std::vector<uint8_t> data = {1, 2, 3, 4};
    
    // Same data, different lamports - LAMPORT_CHANGE should trigger
    auto tasks = manager.check_account_change(
        "account_123", data, data, 1000, 2000, 100
    );
    
    ASSERT_EQ(tasks.size(), 1u);
    
    std::cout << "  ✓ Lamport change watcher tests passed\n";
}

void test_watcher_threshold_trigger() {
    std::cout << "Testing threshold watcher trigger...\n";
    
    AccountWatcherManager manager;
    
    manager.create_threshold_watcher(
        "test_program",
        "account_123",
        WatcherTrigger::THRESHOLD_ABOVE,
        0,     // offset
        1000,  // threshold
        {},
        200000
    );
    
    // Create old data with value below threshold
    int64_t old_value = 500;
    std::vector<uint8_t> old_data(sizeof(int64_t));
    std::memcpy(old_data.data(), &old_value, sizeof(int64_t));
    
    // Create new data with value above threshold
    int64_t new_value = 1500;
    std::vector<uint8_t> new_data(sizeof(int64_t));
    std::memcpy(new_data.data(), &new_value, sizeof(int64_t));
    
    // Should trigger because crossed threshold
    auto tasks = manager.check_account_change(
        "account_123", old_data, new_data, 1000, 1000, 100
    );
    
    ASSERT_EQ(tasks.size(), 1u);
    
    std::cout << "  ✓ Threshold trigger tests passed\n";
}

void test_watcher_remove() {
    std::cout << "Testing watcher removal...\n";
    
    AccountWatcherManager manager;
    
    uint64_t watcher_id = manager.create_watcher(
        "test_program",
        "account_123",
        WatcherTrigger::ANY_CHANGE,
        {},
        {},
        200000
    );
    
    ASSERT_TRUE(manager.remove_watcher(watcher_id));
    ASSERT_TRUE(manager.get_watcher(watcher_id) == nullptr);
    
    // Removing again should fail
    ASSERT_FALSE(manager.remove_watcher(watcher_id));
    
    std::cout << "  ✓ Watcher removal tests passed\n";
}

void test_watcher_pause_resume() {
    std::cout << "Testing watcher pause/resume...\n";
    
    AccountWatcherManager manager;
    
    uint64_t watcher_id = manager.create_watcher(
        "test_program",
        "account_123",
        WatcherTrigger::ANY_CHANGE,
        {},
        {},
        200000
    );
    
    // Pause watcher
    ASSERT_TRUE(manager.set_watcher_active(watcher_id, false));
    
    std::vector<uint8_t> old_data = {1, 2, 3};
    std::vector<uint8_t> new_data = {4, 5, 6};
    
    // Should not trigger while paused
    auto tasks = manager.check_account_change(
        "account_123", old_data, new_data, 1000, 1000, 100
    );
    ASSERT_EQ(tasks.size(), 0u);
    
    // Resume watcher
    ASSERT_TRUE(manager.set_watcher_active(watcher_id, true));
    
    // Should trigger now
    tasks = manager.check_account_change(
        "account_123", old_data, new_data, 1000, 1000, 101
    );
    ASSERT_EQ(tasks.size(), 1u);
    
    std::cout << "  ✓ Watcher pause/resume tests passed\n";
}

// ============================================================================
// Task Scheduler Tests
// ============================================================================

void test_scheduler_submit_task() {
    std::cout << "Testing task scheduler submission...\n";
    
    AsyncTaskScheduler scheduler(2);
    ASSERT_TRUE(scheduler.initialize());
    
    AsyncTask task;
    task.program_id = "test_program";
    task.priority = AsyncPriority::NORMAL;
    task.type = AsyncTask::Type::SCHEDULED_EXECUTION;
    task.compute_budget = 200000;
    
    auto future = scheduler.submit_task(std::move(task));
    
    // Wait for result
    auto result = future.get();
    ASSERT_TRUE(result.success);
    
    scheduler.shutdown();
    
    std::cout << "  ✓ Task submission tests passed\n";
}

void test_scheduler_priority() {
    std::cout << "Testing task scheduler priority...\n";
    
    AsyncTaskScheduler scheduler(1);  // Single worker to test ordering
    ASSERT_TRUE(scheduler.initialize());
    
    std::vector<std::future<AsyncExecutionResult>> futures;
    
    // Submit low priority first
    AsyncTask low_task;
    low_task.program_id = "low";
    low_task.priority = AsyncPriority::LOW;
    low_task.type = AsyncTask::Type::SCHEDULED_EXECUTION;
    low_task.compute_budget = 200000;
    futures.push_back(scheduler.submit_task(std::move(low_task)));
    
    // Submit high priority second
    AsyncTask high_task;
    high_task.program_id = "high";
    high_task.priority = AsyncPriority::HIGH;
    high_task.type = AsyncTask::Type::SCHEDULED_EXECUTION;
    high_task.compute_budget = 200000;
    futures.push_back(scheduler.submit_task(std::move(high_task)));
    
    // Wait for all tasks
    for (auto& f : futures) {
        f.get();
    }
    
    scheduler.shutdown();
    
    std::cout << "  ✓ Task priority tests passed\n";
}

void test_scheduler_batch() {
    std::cout << "Testing batch task submission...\n";
    
    AsyncTaskScheduler scheduler(4);
    ASSERT_TRUE(scheduler.initialize());
    
    std::vector<AsyncTask> tasks;
    for (int i = 0; i < 10; i++) {
        AsyncTask task;
        task.program_id = "batch_program_" + std::to_string(i);
        task.priority = AsyncPriority::NORMAL;
        task.type = AsyncTask::Type::SCHEDULED_EXECUTION;
        task.compute_budget = 200000;
        tasks.push_back(std::move(task));
    }
    
    auto futures = scheduler.submit_tasks(std::move(tasks));
    ASSERT_EQ(futures.size(), 10u);
    
    // Wait for all results
    int success_count = 0;
    for (auto& f : futures) {
        if (f.get().success) {
            success_count++;
        }
    }
    
    ASSERT_EQ(success_count, 10);
    
    scheduler.shutdown();
    
    std::cout << "  ✓ Batch task submission tests passed\n";
}

// ============================================================================
// Async Execution Engine Tests
// ============================================================================

void test_engine_initialize_shutdown() {
    std::cout << "Testing engine initialize/shutdown...\n";
    
    AsyncBpfExecutionEngine engine;
    
    ASSERT_TRUE(engine.initialize());
    ASSERT_TRUE(engine.is_running());
    
    engine.shutdown();
    ASSERT_FALSE(engine.is_running());
    
    std::cout << "  ✓ Engine initialize/shutdown tests passed\n";
}

void test_engine_timer_integration() {
    std::cout << "Testing engine timer integration...\n";
    
    AsyncBpfExecutionEngine engine;
    ASSERT_TRUE(engine.initialize());
    
    // Create a timer (use slot 101 to avoid cleanup at slot % 100 == 0)
    uint64_t timer_id = engine.sol_timer_create(
        "test_program",
        101,
        {1, 2, 3},
        {},
        200000
    );
    ASSERT_NE(timer_id, 0u);
    
    // Get timer info
    const BpfTimer* timer = engine.sol_timer_get_info(timer_id);
    ASSERT_TRUE(timer != nullptr);
    ASSERT_EQ(timer->trigger_slot, 101u);
    
    // Process slot to fire timer (slot 101 doesn't trigger cleanup)
    engine.process_slot(101);
    
    // Wait a bit for async processing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Timer should be executed (state set to COMPLETED by check_timers)
    timer = engine.sol_timer_get_info(timer_id);
    ASSERT_TRUE(timer != nullptr);
    ASSERT_EQ(timer->state, TimerState::COMPLETED);
    
    engine.shutdown();
    
    std::cout << "  ✓ Engine timer integration tests passed\n";
}

void test_engine_watcher_integration() {
    std::cout << "Testing engine watcher integration...\n";
    
    AsyncBpfExecutionEngine engine;
    ASSERT_TRUE(engine.initialize());
    
    // Create a watcher
    uint64_t watcher_id = engine.sol_watcher_create(
        "test_program",
        "account_123",
        WatcherTrigger::ANY_CHANGE,
        {42},
        {},
        200000
    );
    ASSERT_NE(watcher_id, 0u);
    
    // Get watcher info
    const AccountWatcher* watcher = engine.sol_watcher_get_info(watcher_id);
    ASSERT_TRUE(watcher != nullptr);
    ASSERT_EQ(watcher->watched_account, "account_123");
    
    // Notify account change
    engine.process_slot(1);
    engine.notify_account_change(
        "account_123",
        {1, 2, 3},
        {4, 5, 6},
        1000,
        2000
    );
    
    // Give time for async processing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Watcher should have triggered
    watcher = engine.sol_watcher_get_info(watcher_id);
    ASSERT_GT(watcher->trigger_count, 0u);
    
    engine.shutdown();
    
    std::cout << "  ✓ Engine watcher integration tests passed\n";
}

void test_engine_periodic_timer() {
    std::cout << "Testing engine periodic timer...\n";
    
    AsyncBpfExecutionEngine engine;
    ASSERT_TRUE(engine.initialize());
    
    // Create periodic timer
    uint64_t timer_id = engine.sol_timer_create_periodic(
        "test_program",
        100,   // start slot
        10,    // period
        {},
        {},
        200000
    );
    ASSERT_NE(timer_id, 0u);
    
    // Process multiple slots
    engine.process_slot(100);
    engine.process_slot(110);
    engine.process_slot(120);
    
    const BpfTimer* timer = engine.sol_timer_get_info(timer_id);
    ASSERT_EQ(timer->execution_count, 3u);
    
    engine.shutdown();
    
    std::cout << "  ✓ Engine periodic timer tests passed\n";
}

void test_engine_stats() {
    std::cout << "Testing engine statistics...\n";
    
    AsyncBpfExecutionEngine engine;
    ASSERT_TRUE(engine.initialize());
    
    // Create timers and watchers
    engine.sol_timer_create("prog1", 100, {}, {}, 200000);
    engine.sol_timer_create_periodic("prog2", 100, 10, {}, {}, 200000);
    engine.sol_watcher_create("prog3", "acc1", WatcherTrigger::ANY_CHANGE, {}, {}, 200000);
    
    auto stats = engine.get_stats();
    
    ASSERT_EQ(stats.total_timers_created, 2u);
    ASSERT_EQ(stats.total_watchers_created, 1u);
    ASSERT_EQ(stats.timers_currently_active, 2u);
    ASSERT_EQ(stats.watchers_currently_active, 1u);
    
    engine.shutdown();
    
    std::cout << "  ✓ Engine statistics tests passed\n";
}

// ============================================================================
// Performance Benchmarks
// ============================================================================

void benchmark_timer_creation() {
    std::cout << "Benchmarking timer creation...\n";
    
    TimerManager manager;
    
    const int iterations = 10000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        manager.create_timer(
            "program_" + std::to_string(i % 100),
            TimerMode::ONE_SHOT,
            100 + i,
            0,
            {},
            {},
            200000
        );
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double avg_us = static_cast<double>(duration.count()) / iterations;
    
    std::cout << "  Timer creation: " << avg_us << " μs/timer\n";
    std::cout << "  ✓ Timer creation benchmark complete\n";
}

void benchmark_watcher_trigger_check() {
    std::cout << "Benchmarking watcher trigger check...\n";
    
    AccountWatcherManager manager;
    
    // Create 100 watchers on same account
    for (int i = 0; i < 100; i++) {
        manager.create_watcher(
            "program_" + std::to_string(i),
            "account_123",
            WatcherTrigger::ANY_CHANGE,
            {},
            {},
            200000
        );
    }
    
    std::vector<uint8_t> old_data(1024, 0);
    std::vector<uint8_t> new_data(1024, 1);
    
    const int iterations = 10000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        manager.check_account_change(
            "account_123", old_data, new_data, 1000, 2000, 100 + i
        );
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double avg_us = static_cast<double>(duration.count()) / iterations;
    
    std::cout << "  Watcher check (100 watchers): " << avg_us << " μs/check\n";
    std::cout << "  ✓ Watcher trigger benchmark complete\n";
}

void benchmark_task_throughput() {
    std::cout << "Benchmarking task throughput...\n";
    
    AsyncTaskScheduler scheduler(4);
    scheduler.initialize();
    
    const int num_tasks = 10000;
    std::vector<std::future<AsyncExecutionResult>> futures;
    futures.reserve(num_tasks);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_tasks; i++) {
        AsyncTask task;
        task.program_id = "benchmark_program";
        task.priority = AsyncPriority::NORMAL;
        task.type = AsyncTask::Type::SCHEDULED_EXECUTION;
        task.compute_budget = 200000;
        futures.push_back(scheduler.submit_task(std::move(task)));
    }
    
    // Wait for all tasks
    for (auto& f : futures) {
        f.get();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    double tasks_per_sec = static_cast<double>(num_tasks) / (duration.count() / 1000.0);
    
    std::cout << "  Task throughput: " << tasks_per_sec << " tasks/sec\n";
    std::cout << "  Total time: " << duration.count() << " ms for " << num_tasks << " tasks\n";
    std::cout << "  ✓ Task throughput benchmark complete\n";
    
    scheduler.shutdown();
}

} // anonymous namespace

// ============================================================================
// Main Test Runner
// ============================================================================

int main() {
    int total_tests = 0;
    int passed_tests = 0;
    
    auto run_test = [&total_tests, &passed_tests](const char* name, void (*test_fn)()) {
        total_tests++;
        try {
            test_fn();
            passed_tests++;
        } catch (const std::exception& e) {
            std::cerr << "FAILED: " << name << " - " << e.what() << "\n";
        }
    };
    
    std::cout << "=== Async BPF Execution Test Suite ===\n\n";
    
    std::cout << "--- Timer Manager Tests ---\n";
    run_test("Timer Create One-Shot", test_timer_create_one_shot);
    run_test("Timer Create Periodic", test_timer_create_periodic);
    run_test("Timer Cancel", test_timer_cancel);
    run_test("Timer Check Fires", test_timer_check_fires);
    run_test("Timer Periodic Rescheduling", test_timer_periodic_rescheduling);
    run_test("Timer Limits", test_timer_limits);
    
    std::cout << "\n--- Account Watcher Tests ---\n";
    run_test("Watcher Create", test_watcher_create);
    run_test("Watcher Threshold", test_watcher_threshold);
    run_test("Watcher Pattern", test_watcher_pattern);
    run_test("Watcher Triggers On Change", test_watcher_triggers_on_change);
    run_test("Watcher No Trigger Same Data", test_watcher_no_trigger_same_data);
    run_test("Watcher Lamport Change", test_watcher_lamport_change);
    run_test("Watcher Threshold Trigger", test_watcher_threshold_trigger);
    run_test("Watcher Remove", test_watcher_remove);
    run_test("Watcher Pause Resume", test_watcher_pause_resume);
    
    std::cout << "\n--- Task Scheduler Tests ---\n";
    run_test("Scheduler Submit Task", test_scheduler_submit_task);
    run_test("Scheduler Priority", test_scheduler_priority);
    run_test("Scheduler Batch", test_scheduler_batch);
    
    std::cout << "\n--- Engine Integration Tests ---\n";
    run_test("Engine Initialize Shutdown", test_engine_initialize_shutdown);
    run_test("Engine Timer Integration", test_engine_timer_integration);
    run_test("Engine Watcher Integration", test_engine_watcher_integration);
    run_test("Engine Periodic Timer", test_engine_periodic_timer);
    run_test("Engine Stats", test_engine_stats);
    
    std::cout << "\n--- Performance Benchmarks ---\n";
    run_test("Benchmark Timer Creation", benchmark_timer_creation);
    run_test("Benchmark Watcher Trigger Check", benchmark_watcher_trigger_check);
    run_test("Benchmark Task Throughput", benchmark_task_throughput);
    
    std::cout << "\n====================================\n";
    std::cout << "Test Results: " << passed_tests << "/" << total_tests << " passed\n";
    std::cout << "====================================\n";
    
    std::cout << "=== Async BPF Execution Tests Complete ===\n";
    
    return (passed_tests == total_tests) ? 0 : 1;
}
