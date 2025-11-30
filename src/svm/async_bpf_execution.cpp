#include "svm/async_bpf_execution.h"
#include <algorithm>
#include <cstring>

namespace slonana {
namespace svm {

// Error codes
constexpr uint64_t ASYNC_SUCCESS = 0;
constexpr uint64_t ASYNC_ERROR_NULL_POINTER = 1;
constexpr uint64_t ASYNC_ERROR_INVALID_TIMER = 2;
constexpr uint64_t ASYNC_ERROR_INVALID_WATCHER = 3;
constexpr uint64_t ASYNC_ERROR_MAX_TIMERS = 4;
constexpr uint64_t ASYNC_ERROR_MAX_WATCHERS = 5;
constexpr uint64_t ASYNC_ERROR_INVALID_SLOT = 6;
constexpr uint64_t ASYNC_ERROR_SHUTDOWN = 7;
constexpr uint64_t ASYNC_ERROR_INVALID_BUFFER = 8;
constexpr uint64_t ASYNC_ERROR_BUFFER_FULL = 9;
constexpr uint64_t ASYNC_ERROR_MAX_BUFFERS = 10;

// Cleanup configuration - completed/cancelled timers are removed at these intervals
constexpr uint64_t TIMER_CLEANUP_INTERVAL_SLOTS = 100;

// ============================================================================
// TimerManager Implementation
// ============================================================================

TimerManager::TimerManager() {}

TimerManager::~TimerManager() {}

uint64_t TimerManager::create_timer(
    const std::string& program_id,
    TimerMode mode,
    uint64_t trigger_slot,
    uint64_t period_slots,
    const std::vector<uint8_t>& callback_data,
    const std::vector<AccountInfo>& accounts,
    uint64_t compute_budget)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check limits
    auto it = program_timers_.find(program_id);
    if (it != program_timers_.end() && it->second.size() >= MAX_TIMERS_PER_PROGRAM) {
        return 0;
    }
    
    if (timers_.size() >= MAX_SCHEDULED_TASKS) {
        return 0;
    }
    
    // Validate timer parameters
    if (trigger_slot == 0 && mode != TimerMode::CONDITIONAL) {
        return 0;
    }
    
    if (mode == TimerMode::PERIODIC && period_slots < MIN_TIMER_SLOTS) {
        return 0;
    }
    
    uint64_t timer_id = next_timer_id_.fetch_add(1);
    
    BpfTimer timer;
    timer.timer_id = timer_id;
    timer.program_id = program_id;
    timer.mode = mode;
    timer.state = TimerState::PENDING;
    timer.trigger_slot = trigger_slot;
    timer.period_slots = period_slots;
    timer.callback_data = callback_data;
    timer.accounts = accounts;
    timer.compute_budget = compute_budget;
    
    timers_[timer_id] = std::move(timer);
    program_timers_[program_id].push_back(timer_id);
    
    return timer_id;
}

bool TimerManager::cancel_timer(uint64_t timer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = timers_.find(timer_id);
    if (it == timers_.end()) {
        return false;
    }
    
    it->second.state = TimerState::CANCELLED;
    return true;
}

const BpfTimer* TimerManager::get_timer(uint64_t timer_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = timers_.find(timer_id);
    if (it == timers_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<AsyncTask> TimerManager::check_timers(uint64_t current_slot) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<AsyncTask> tasks;
    
    for (auto& [id, timer] : timers_) {
        if (timer.state != TimerState::PENDING) {
            continue;
        }
        
        if (timer.trigger_slot <= current_slot) {
            // Timer is ready to fire
            tasks.push_back(create_timer_task(timer, current_slot));
            
            if (timer.mode == TimerMode::ONE_SHOT) {
                timer.state = TimerState::COMPLETED;
            } else if (timer.mode == TimerMode::PERIODIC) {
                reschedule_periodic_timer(timer, current_slot);
            }
            
            timer.execution_count++;
            timer.last_execution_slot = current_slot;
        }
        
        // Check deadline mode
        if (timer.mode == TimerMode::DEADLINE && 
            timer.deadline_slot > 0 && 
            timer.deadline_slot < current_slot) {
            timer.state = TimerState::EXPIRED;
        }
    }
    
    return tasks;
}

std::vector<const BpfTimer*> TimerManager::get_program_timers(
    const std::string& program_id) const 
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<const BpfTimer*> result;
    
    auto it = program_timers_.find(program_id);
    if (it != program_timers_.end()) {
        for (uint64_t timer_id : it->second) {
            auto timer_it = timers_.find(timer_id);
            if (timer_it != timers_.end()) {
                result.push_back(&timer_it->second);
            }
        }
    }
    
    return result;
}

void TimerManager::get_stats(AsyncExecutionStats& stats) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    stats.total_timers_created = next_timer_id_.load() - 1;
    stats.timers_currently_active = 0;
    stats.total_timer_executions = 0;
    
    for (const auto& [id, timer] : timers_) {
        if (timer.state == TimerState::PENDING) {
            stats.timers_currently_active++;
        }
        stats.total_timer_executions += timer.execution_count;
    }
}

void TimerManager::cleanup_expired(uint64_t current_slot) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<uint64_t> to_remove;
    
    for (const auto& [id, timer] : timers_) {
        if (timer.state == TimerState::COMPLETED ||
            timer.state == TimerState::CANCELLED ||
            timer.state == TimerState::EXPIRED) {
            to_remove.push_back(id);
        }
    }
    
    for (uint64_t id : to_remove) {
        auto it = timers_.find(id);
        if (it != timers_.end()) {
            // Remove from program_timers_
            auto& program_list = program_timers_[it->second.program_id];
            program_list.erase(
                std::remove(program_list.begin(), program_list.end(), id),
                program_list.end()
            );
            timers_.erase(it);
        }
    }
}

void TimerManager::reschedule_periodic_timer(BpfTimer& timer, uint64_t current_slot) {
    timer.trigger_slot = current_slot + timer.period_slots;
}

AsyncTask TimerManager::create_timer_task(const BpfTimer& timer, uint64_t current_slot) {
    AsyncTask task;
    task.task_id = 0; // Will be assigned by scheduler
    task.program_id = timer.program_id;
    task.priority = AsyncPriority::NORMAL;
    task.type = AsyncTask::Type::TIMER_CALLBACK;
    task.instruction_data = timer.callback_data;
    task.accounts = timer.accounts;
    task.compute_budget = timer.compute_budget;
    task.scheduled_slot = current_slot;
    task.created_at = std::chrono::steady_clock::now();
    
    return task;
}

// ============================================================================
// AccountWatcherManager Implementation
// ============================================================================

AccountWatcherManager::AccountWatcherManager() {}

AccountWatcherManager::~AccountWatcherManager() {}

uint64_t AccountWatcherManager::create_watcher(
    const std::string& program_id,
    const std::string& watched_account,
    WatcherTrigger trigger_type,
    const std::vector<uint8_t>& callback_data,
    const std::vector<AccountInfo>& additional_accounts,
    uint64_t compute_budget)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check limits
    auto it = program_watchers_.find(program_id);
    if (it != program_watchers_.end() && it->second.size() >= MAX_WATCHERS_PER_PROGRAM) {
        return 0;
    }
    
    if (watchers_.size() >= MAX_WATCHER_CALLBACKS) {
        return 0;
    }
    
    uint64_t watcher_id = next_watcher_id_.fetch_add(1);
    
    AccountWatcher watcher;
    watcher.watcher_id = watcher_id;
    watcher.program_id = program_id;
    watcher.watched_account = watched_account;
    watcher.trigger_type = trigger_type;
    watcher.is_active = true;
    watcher.callback_data = callback_data;
    watcher.additional_accounts = additional_accounts;
    watcher.compute_budget = compute_budget;
    
    watchers_[watcher_id] = std::move(watcher);
    account_watchers_[watched_account].push_back(watcher_id);
    program_watchers_[program_id].push_back(watcher_id);
    
    return watcher_id;
}

uint64_t AccountWatcherManager::create_threshold_watcher(
    const std::string& program_id,
    const std::string& watched_account,
    WatcherTrigger trigger_type,
    uint64_t threshold_offset,
    int64_t threshold_value,
    const std::vector<uint8_t>& callback_data,
    uint64_t compute_budget)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (trigger_type != WatcherTrigger::THRESHOLD_ABOVE &&
        trigger_type != WatcherTrigger::THRESHOLD_BELOW) {
        return 0;
    }
    
    // Check limits
    auto it = program_watchers_.find(program_id);
    if (it != program_watchers_.end() && it->second.size() >= MAX_WATCHERS_PER_PROGRAM) {
        return 0;
    }
    
    uint64_t watcher_id = next_watcher_id_.fetch_add(1);
    
    AccountWatcher watcher;
    watcher.watcher_id = watcher_id;
    watcher.program_id = program_id;
    watcher.watched_account = watched_account;
    watcher.trigger_type = trigger_type;
    watcher.is_active = true;
    watcher.threshold_offset = threshold_offset;
    watcher.threshold_value = threshold_value;
    watcher.callback_data = callback_data;
    watcher.compute_budget = compute_budget;
    
    watchers_[watcher_id] = std::move(watcher);
    account_watchers_[watched_account].push_back(watcher_id);
    program_watchers_[program_id].push_back(watcher_id);
    
    return watcher_id;
}

uint64_t AccountWatcherManager::create_pattern_watcher(
    const std::string& program_id,
    const std::string& watched_account,
    const std::vector<uint8_t>& pattern,
    uint64_t pattern_offset,
    const std::vector<uint8_t>& callback_data,
    uint64_t compute_budget)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (pattern.empty()) {
        return 0;
    }
    
    // Check limits
    auto it = program_watchers_.find(program_id);
    if (it != program_watchers_.end() && it->second.size() >= MAX_WATCHERS_PER_PROGRAM) {
        return 0;
    }
    
    uint64_t watcher_id = next_watcher_id_.fetch_add(1);
    
    AccountWatcher watcher;
    watcher.watcher_id = watcher_id;
    watcher.program_id = program_id;
    watcher.watched_account = watched_account;
    watcher.trigger_type = WatcherTrigger::PATTERN_MATCH;
    watcher.is_active = true;
    watcher.pattern = pattern;
    watcher.pattern_offset = pattern_offset;
    watcher.callback_data = callback_data;
    watcher.compute_budget = compute_budget;
    
    watchers_[watcher_id] = std::move(watcher);
    account_watchers_[watched_account].push_back(watcher_id);
    program_watchers_[program_id].push_back(watcher_id);
    
    return watcher_id;
}

bool AccountWatcherManager::remove_watcher(uint64_t watcher_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = watchers_.find(watcher_id);
    if (it == watchers_.end()) {
        return false;
    }
    
    // Remove from account_watchers_
    auto& account_list = account_watchers_[it->second.watched_account];
    account_list.erase(
        std::remove(account_list.begin(), account_list.end(), watcher_id),
        account_list.end()
    );
    
    // Remove from program_watchers_
    auto& program_list = program_watchers_[it->second.program_id];
    program_list.erase(
        std::remove(program_list.begin(), program_list.end(), watcher_id),
        program_list.end()
    );
    
    watchers_.erase(it);
    return true;
}

bool AccountWatcherManager::set_watcher_active(uint64_t watcher_id, bool active) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = watchers_.find(watcher_id);
    if (it == watchers_.end()) {
        return false;
    }
    
    it->second.is_active = active;
    return true;
}

const AccountWatcher* AccountWatcherManager::get_watcher(uint64_t watcher_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = watchers_.find(watcher_id);
    if (it == watchers_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<AsyncTask> AccountWatcherManager::check_account_change(
    const std::string& account_pubkey,
    const std::vector<uint8_t>& old_data,
    const std::vector<uint8_t>& new_data,
    uint64_t old_lamports,
    uint64_t new_lamports,
    uint64_t current_slot)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<AsyncTask> tasks;
    
    auto it = account_watchers_.find(account_pubkey);
    if (it == account_watchers_.end()) {
        return tasks;
    }
    
    for (uint64_t watcher_id : it->second) {
        auto watcher_it = watchers_.find(watcher_id);
        if (watcher_it == watchers_.end() || !watcher_it->second.is_active) {
            continue;
        }
        
        AccountWatcher& watcher = watcher_it->second;
        
        // Check rate limiting
        if (current_slot - watcher.last_trigger_slot < watcher.min_slots_between_triggers) {
            continue;
        }
        
        if (should_trigger(watcher, old_data, new_data, old_lamports, new_lamports)) {
            tasks.push_back(create_watcher_task(watcher, current_slot));
            watcher.last_trigger_slot = current_slot;
            watcher.trigger_count++;
            watcher.last_known_state = new_data;
        }
    }
    
    return tasks;
}

std::vector<const AccountWatcher*> AccountWatcherManager::get_account_watchers(
    const std::string& account_pubkey) const 
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<const AccountWatcher*> result;
    
    auto it = account_watchers_.find(account_pubkey);
    if (it != account_watchers_.end()) {
        for (uint64_t watcher_id : it->second) {
            auto watcher_it = watchers_.find(watcher_id);
            if (watcher_it != watchers_.end()) {
                result.push_back(&watcher_it->second);
            }
        }
    }
    
    return result;
}

void AccountWatcherManager::get_stats(AsyncExecutionStats& stats) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    stats.total_watchers_created = next_watcher_id_.load() - 1;
    stats.watchers_currently_active = 0;
    stats.total_watcher_triggers = 0;
    
    for (const auto& [id, watcher] : watchers_) {
        if (watcher.is_active) {
            stats.watchers_currently_active++;
        }
        stats.total_watcher_triggers += watcher.trigger_count;
    }
}

bool AccountWatcherManager::should_trigger(
    const AccountWatcher& watcher,
    const std::vector<uint8_t>& old_data,
    const std::vector<uint8_t>& new_data,
    uint64_t old_lamports,
    uint64_t new_lamports) const
{
    switch (watcher.trigger_type) {
        case WatcherTrigger::ANY_CHANGE:
            return old_data != new_data || old_lamports != new_lamports;
            
        case WatcherTrigger::LAMPORT_CHANGE:
            return old_lamports != new_lamports;
            
        case WatcherTrigger::DATA_CHANGE:
            return old_data != new_data;
            
        case WatcherTrigger::THRESHOLD_ABOVE: {
            if (watcher.threshold_offset + sizeof(int64_t) > new_data.size()) {
                return false;
            }
            int64_t old_value = 0, new_value = 0;
            if (watcher.threshold_offset + sizeof(int64_t) <= old_data.size()) {
                std::memcpy(&old_value, old_data.data() + watcher.threshold_offset, sizeof(int64_t));
            }
            std::memcpy(&new_value, new_data.data() + watcher.threshold_offset, sizeof(int64_t));
            return old_value <= watcher.threshold_value && new_value > watcher.threshold_value;
        }
            
        case WatcherTrigger::THRESHOLD_BELOW: {
            if (watcher.threshold_offset + sizeof(int64_t) > new_data.size()) {
                return false;
            }
            int64_t old_value = 0, new_value = 0;
            if (watcher.threshold_offset + sizeof(int64_t) <= old_data.size()) {
                std::memcpy(&old_value, old_data.data() + watcher.threshold_offset, sizeof(int64_t));
            }
            std::memcpy(&new_value, new_data.data() + watcher.threshold_offset, sizeof(int64_t));
            return old_value >= watcher.threshold_value && new_value < watcher.threshold_value;
        }
            
        case WatcherTrigger::PATTERN_MATCH: {
            if (watcher.pattern_offset + watcher.pattern.size() > new_data.size()) {
                return false;
            }
            // Check if pattern was not matching before but matches now
            bool old_matches = false;
            if (watcher.pattern_offset + watcher.pattern.size() <= old_data.size()) {
                old_matches = std::equal(
                    watcher.pattern.begin(), watcher.pattern.end(),
                    old_data.begin() + watcher.pattern_offset
                );
            }
            bool new_matches = std::equal(
                watcher.pattern.begin(), watcher.pattern.end(),
                new_data.begin() + watcher.pattern_offset
            );
            return !old_matches && new_matches;
        }
            
        case WatcherTrigger::OWNER_CHANGE:
            // Would need owner info passed in - for now treat as data change
            return old_data != new_data;
            
        default:
            return false;
    }
}

AsyncTask AccountWatcherManager::create_watcher_task(
    const AccountWatcher& watcher,
    uint64_t current_slot)
{
    AsyncTask task;
    task.task_id = 0; // Will be assigned by scheduler
    task.program_id = watcher.program_id;
    task.priority = AsyncPriority::HIGH; // Watchers are typically high priority
    task.type = AsyncTask::Type::WATCHER_CALLBACK;
    task.instruction_data = watcher.callback_data;
    task.accounts = watcher.additional_accounts;
    task.compute_budget = watcher.compute_budget;
    task.scheduled_slot = current_slot;
    task.created_at = std::chrono::steady_clock::now();
    
    return task;
}

// ============================================================================
// RingBufferManager Implementation
// ============================================================================

RingBufferManager::RingBufferManager() {}

RingBufferManager::~RingBufferManager() {}

uint64_t RingBufferManager::create_buffer(
    const std::string& program_id,
    uint64_t size)
{
    // Validate size
    if (size < MIN_RING_BUFFER_SIZE || size > MAX_RING_BUFFER_SIZE) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check per-program limit
    auto it = program_buffers_.find(program_id);
    if (it != program_buffers_.end() && it->second.size() >= MAX_RING_BUFFERS_PER_PROGRAM) {
        return 0;
    }
    
    // Create new buffer
    uint64_t buffer_id = next_buffer_id_++;
    auto buffer = std::make_unique<BpfRingBuffer>();
    buffer->buffer_id = buffer_id;
    buffer->owner_program_id = program_id;
    buffer->state = RingBufferState::ACTIVE;
    buffer->capacity = size;
    buffer->buffer.resize(size);
    
    // Add to maps
    program_buffers_[program_id].push_back(buffer_id);
    buffers_[buffer_id] = std::move(buffer);
    
    total_buffers_created_++;
    
    return buffer_id;
}

bool RingBufferManager::destroy_buffer(uint64_t buffer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = buffers_.find(buffer_id);
    if (it == buffers_.end()) {
        return false;
    }
    
    // Remove from program list
    const std::string& program_id = it->second->owner_program_id;
    auto prog_it = program_buffers_.find(program_id);
    if (prog_it != program_buffers_.end()) {
        auto& buffers = prog_it->second;
        buffers.erase(std::remove(buffers.begin(), buffers.end(), buffer_id), buffers.end());
        if (buffers.empty()) {
            program_buffers_.erase(prog_it);
        }
    }
    
    buffers_.erase(it);
    return true;
}

bool RingBufferManager::push(
    uint64_t buffer_id,
    const uint8_t* data,
    uint32_t data_len,
    uint64_t current_slot,
    uint32_t flags)
{
    if (!data || data_len == 0) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = buffers_.find(buffer_id);
    if (it == buffers_.end()) {
        return false;
    }
    
    BpfRingBuffer& buffer = *it->second;
    
    if (buffer.state != RingBufferState::ACTIVE) {
        return false;
    }
    
    // Calculate total entry size (header + data)
    uint32_t entry_size = sizeof(RingBufferEntry) + data_len;
    
    // Check if we have enough space
    uint64_t available = calculate_available_space(buffer);
    if (entry_size > available) {
        buffer.dropped_entries++;
        return false;
    }
    
    // Get write position
    uint64_t head = buffer.head.load();
    
    // Write header
    RingBufferEntry entry;
    entry.sequence_number = buffer.sequence++;
    entry.timestamp_slot = current_slot;
    entry.data_length = data_len;
    entry.flags = flags;
    
    // Write header bytes (wrap around if needed)
    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&entry);
    for (size_t i = 0; i < sizeof(RingBufferEntry); i++) {
        buffer.buffer[(head + i) % buffer.capacity] = header_bytes[i];
    }
    
    // Write data bytes
    for (size_t i = 0; i < data_len; i++) {
        buffer.buffer[(head + sizeof(RingBufferEntry) + i) % buffer.capacity] = data[i];
    }
    
    // Update head
    buffer.head.store((head + entry_size) % buffer.capacity);
    buffer.entry_count.fetch_add(1);
    buffer.total_writes++;
    total_bytes_written_ += data_len;
    
    // Update peak usage
    uint64_t used = buffer.capacity - calculate_available_space(buffer);
    if (used > buffer.peak_usage) {
        buffer.peak_usage = used;
    }
    
    return true;
}

std::vector<uint8_t> RingBufferManager::pop(uint64_t buffer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = buffers_.find(buffer_id);
    if (it == buffers_.end()) {
        return {};
    }
    
    BpfRingBuffer& buffer = *it->second;
    
    if (buffer.state == RingBufferState::CLOSED) {
        return {};
    }
    
    if (buffer.entry_count.load() == 0) {
        return {};
    }
    
    uint64_t tail = buffer.tail.load();
    uint64_t head = buffer.head.load();
    
    if (tail == head) {
        return {};
    }
    
    // Read header
    RingBufferEntry entry;
    uint8_t* header_bytes = reinterpret_cast<uint8_t*>(&entry);
    for (size_t i = 0; i < sizeof(RingBufferEntry); i++) {
        header_bytes[i] = buffer.buffer[(tail + i) % buffer.capacity];
    }
    
    // Read data
    std::vector<uint8_t> data(entry.data_length);
    for (size_t i = 0; i < entry.data_length; i++) {
        data[i] = buffer.buffer[(tail + sizeof(RingBufferEntry) + i) % buffer.capacity];
    }
    
    // Update tail
    uint32_t entry_size = sizeof(RingBufferEntry) + entry.data_length;
    buffer.tail.store((tail + entry_size) % buffer.capacity);
    buffer.entry_count.fetch_sub(1);
    buffer.total_reads++;
    total_bytes_read_ += entry.data_length;
    
    return data;
}

std::vector<uint8_t> RingBufferManager::peek(uint64_t buffer_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = buffers_.find(buffer_id);
    if (it == buffers_.end()) {
        return {};
    }
    
    const BpfRingBuffer& buffer = *it->second;
    
    if (buffer.entry_count.load() == 0) {
        return {};
    }
    
    uint64_t tail = buffer.tail.load();
    
    // Read header
    RingBufferEntry entry;
    uint8_t* header_bytes = reinterpret_cast<uint8_t*>(&entry);
    for (size_t i = 0; i < sizeof(RingBufferEntry); i++) {
        header_bytes[i] = buffer.buffer[(tail + i) % buffer.capacity];
    }
    
    // Read data (without modifying tail)
    std::vector<uint8_t> data(entry.data_length);
    for (size_t i = 0; i < entry.data_length; i++) {
        data[i] = buffer.buffer[(tail + sizeof(RingBufferEntry) + i) % buffer.capacity];
    }
    
    return data;
}

const BpfRingBuffer* RingBufferManager::get_buffer(uint64_t buffer_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(buffer_id);
    return it != buffers_.end() ? it->second.get() : nullptr;
}

bool RingBufferManager::has_data(uint64_t buffer_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(buffer_id);
    return it != buffers_.end() && it->second->entry_count.load() > 0;
}

uint64_t RingBufferManager::get_entry_count(uint64_t buffer_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(buffer_id);
    return it != buffers_.end() ? it->second->entry_count.load() : 0;
}

double RingBufferManager::get_usage(uint64_t buffer_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(buffer_id);
    if (it == buffers_.end()) {
        return 0.0;
    }
    
    const BpfRingBuffer& buffer = *it->second;
    uint64_t used = buffer.capacity - calculate_available_space(buffer);
    return static_cast<double>(used) / static_cast<double>(buffer.capacity);
}

void RingBufferManager::get_stats(AsyncExecutionStats& stats) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Add ring buffer stats to memory usage
    size_t total_memory = 0;
    for (const auto& pair : buffers_) {
        total_memory += pair.second->capacity;
    }
    stats.memory_used_bytes += total_memory;
}

bool RingBufferManager::set_buffer_state(uint64_t buffer_id, RingBufferState state) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(buffer_id);
    if (it == buffers_.end()) {
        return false;
    }
    it->second->state = state;
    return true;
}

std::vector<uint64_t> RingBufferManager::get_program_buffers(const std::string& program_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = program_buffers_.find(program_id);
    return it != program_buffers_.end() ? it->second : std::vector<uint64_t>{};
}

uint64_t RingBufferManager::calculate_available_space(const BpfRingBuffer& buffer) const {
    uint64_t head = buffer.head.load();
    uint64_t tail = buffer.tail.load();
    
    if (head >= tail) {
        return buffer.capacity - (head - tail) - 1;  // -1 to distinguish full from empty
    } else {
        return tail - head - 1;
    }
}

// ============================================================================
// AsyncTaskScheduler Implementation
// ============================================================================

AsyncTaskScheduler::AsyncTaskScheduler(size_t num_workers)
    : num_workers_(num_workers),
      task_queue_(task_priority_compare)
{}

AsyncTaskScheduler::~AsyncTaskScheduler() {
    shutdown();
}

bool AsyncTaskScheduler::initialize() {
    if (running_.load()) {
        return false;
    }
    
    running_.store(true);
    
    workers_.reserve(num_workers_);
    for (size_t i = 0; i < num_workers_; i++) {
        workers_.emplace_back(&AsyncTaskScheduler::worker_loop, this);
    }
    
    return true;
}

void AsyncTaskScheduler::shutdown() {
    running_.store(false);
    queue_cv_.notify_all();
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

std::future<AsyncExecutionResult> AsyncTaskScheduler::submit_task(AsyncTask task) {
    std::future<AsyncExecutionResult> future = task.result_promise.get_future();
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task.task_id = next_task_id_.fetch_add(1);
        
        auto task_ptr = std::make_unique<AsyncTask>(std::move(task));
        task_queue_.push(task_ptr.get());
        task_storage_.push_back(std::move(task_ptr));
    }
    
    queue_cv_.notify_one();
    return future;
}

std::vector<std::future<AsyncExecutionResult>> AsyncTaskScheduler::submit_tasks(
    std::vector<AsyncTask> tasks)
{
    std::vector<std::future<AsyncExecutionResult>> futures;
    futures.reserve(tasks.size());
    
    for (auto& task : tasks) {
        futures.push_back(submit_task(std::move(task)));
    }
    
    return futures;
}

void AsyncTaskScheduler::set_current_slot(uint64_t slot) {
    current_slot_.store(slot);
}

uint64_t AsyncTaskScheduler::get_current_slot() const {
    return current_slot_.load();
}

size_t AsyncTaskScheduler::get_pending_count() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return task_queue_.size();
}

void AsyncTaskScheduler::get_stats(AsyncExecutionStats& stats) const {
    stats.total_tasks_completed = tasks_completed_.load();
    stats.total_tasks_failed = tasks_failed_.load();
    stats.tasks_currently_pending = get_pending_count();
    
    uint64_t completed = tasks_completed_.load();
    if (completed > 0) {
        stats.average_task_latency_us = 
            static_cast<double>(total_latency_us_.load()) / completed;
    }
}

void AsyncTaskScheduler::cleanup() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    // Remove completed tasks from storage
    task_storage_.erase(
        std::remove_if(
            task_storage_.begin(),
            task_storage_.end(),
            [](const std::unique_ptr<AsyncTask>& t) {
                return t == nullptr;
            }
        ),
        task_storage_.end()
    );
}

void AsyncTaskScheduler::worker_loop() {
    while (running_.load()) {
        AsyncTask* task = nullptr;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !running_.load() || !task_queue_.empty();
            });
            
            if (!running_.load() && task_queue_.empty()) {
                break;
            }
            
            if (!task_queue_.empty()) {
                task = task_queue_.top();
                task_queue_.pop();
            }
        }
        
        if (task) {
            auto start = std::chrono::steady_clock::now();
            AsyncExecutionResult result = execute_task(*task);
            auto end = std::chrono::steady_clock::now();
            
            auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            total_latency_us_.fetch_add(latency.count());
            
            if (result.success) {
                tasks_completed_.fetch_add(1);
            } else {
                tasks_failed_.fetch_add(1);
            }
            
            // Set result
            try {
                task->result_promise.set_value(result);
            } catch (...) {
                // Promise already satisfied
            }
            
            // Call completion callback if set
            if (task->completion_callback) {
                task->completion_callback(result);
            }
        }
    }
}

AsyncExecutionResult AsyncTaskScheduler::execute_task(AsyncTask& task) {
    AsyncExecutionResult result;
    result.success = true;
    result.compute_units_consumed = 0;
    
    // Execute based on task type
    switch (task.type) {
        case AsyncTask::Type::TIMER_CALLBACK:
        case AsyncTask::Type::WATCHER_CALLBACK:
        case AsyncTask::Type::SCHEDULED_EXECUTION:
        case AsyncTask::Type::BACKGROUND_COMPUTE:
            // These would invoke the actual BPF program
            // For now, simulate successful execution
            result.compute_units_consumed = task.compute_budget / 10; // Placeholder
            break;
            
        case AsyncTask::Type::ML_INFERENCE:
            // ML inference tasks are handled specially
            result.compute_units_consumed = task.compute_budget / 5;
            break;
    }
    
    return result;
}

bool AsyncTaskScheduler::task_priority_compare(AsyncTask* a, AsyncTask* b) {
    // Higher priority value = higher priority
    if (static_cast<uint8_t>(a->priority) != static_cast<uint8_t>(b->priority)) {
        return static_cast<uint8_t>(a->priority) < static_cast<uint8_t>(b->priority);
    }
    // Earlier deadline = higher priority
    if (a->deadline_slot != b->deadline_slot) {
        return a->deadline_slot > b->deadline_slot;
    }
    // Earlier creation = higher priority
    return a->created_at > b->created_at;
}

// ============================================================================
// AsyncBpfExecutionEngine Implementation
// ============================================================================

AsyncBpfExecutionEngine::AsyncBpfExecutionEngine(size_t num_workers)
    : timer_manager_(std::make_unique<TimerManager>()),
      watcher_manager_(std::make_unique<AccountWatcherManager>()),
      task_scheduler_(std::make_unique<AsyncTaskScheduler>(num_workers)),
      ring_buffer_manager_(std::make_unique<RingBufferManager>())
{}

AsyncBpfExecutionEngine::~AsyncBpfExecutionEngine() {
    shutdown();
}

bool AsyncBpfExecutionEngine::initialize() {
    if (running_.load()) {
        return false;
    }
    
    if (!task_scheduler_->initialize()) {
        return false;
    }
    
    running_.store(true);
    
    slot_processor_thread_ = std::thread(&AsyncBpfExecutionEngine::slot_processor_loop, this);
    
    return true;
}

void AsyncBpfExecutionEngine::shutdown() {
    running_.store(false);
    slot_cv_.notify_all();
    
    if (slot_processor_thread_.joinable()) {
        slot_processor_thread_.join();
    }
    
    task_scheduler_->shutdown();
}

uint64_t AsyncBpfExecutionEngine::sol_timer_create(
    const std::string& program_id,
    uint64_t trigger_slot,
    const std::vector<uint8_t>& callback_data,
    const std::vector<AccountInfo>& accounts,
    uint64_t compute_budget)
{
    return timer_manager_->create_timer(
        program_id,
        TimerMode::ONE_SHOT,
        trigger_slot,
        0,
        callback_data,
        accounts,
        compute_budget
    );
}

uint64_t AsyncBpfExecutionEngine::sol_timer_create_periodic(
    const std::string& program_id,
    uint64_t start_slot,
    uint64_t period_slots,
    const std::vector<uint8_t>& callback_data,
    const std::vector<AccountInfo>& accounts,
    uint64_t compute_budget)
{
    return timer_manager_->create_timer(
        program_id,
        TimerMode::PERIODIC,
        start_slot,
        period_slots,
        callback_data,
        accounts,
        compute_budget
    );
}

bool AsyncBpfExecutionEngine::sol_timer_cancel(uint64_t timer_id) {
    return timer_manager_->cancel_timer(timer_id);
}

const BpfTimer* AsyncBpfExecutionEngine::sol_timer_get_info(uint64_t timer_id) const {
    return timer_manager_->get_timer(timer_id);
}

uint64_t AsyncBpfExecutionEngine::sol_watcher_create(
    const std::string& program_id,
    const std::string& watched_account,
    WatcherTrigger trigger_type,
    const std::vector<uint8_t>& callback_data,
    const std::vector<AccountInfo>& additional_accounts,
    uint64_t compute_budget)
{
    return watcher_manager_->create_watcher(
        program_id,
        watched_account,
        trigger_type,
        callback_data,
        additional_accounts,
        compute_budget
    );
}

uint64_t AsyncBpfExecutionEngine::sol_watcher_create_threshold(
    const std::string& program_id,
    const std::string& watched_account,
    WatcherTrigger trigger_type,
    uint64_t offset,
    int64_t threshold,
    const std::vector<uint8_t>& callback_data,
    uint64_t compute_budget)
{
    return watcher_manager_->create_threshold_watcher(
        program_id,
        watched_account,
        trigger_type,
        offset,
        threshold,
        callback_data,
        compute_budget
    );
}

bool AsyncBpfExecutionEngine::sol_watcher_remove(uint64_t watcher_id) {
    return watcher_manager_->remove_watcher(watcher_id);
}

const AccountWatcher* AsyncBpfExecutionEngine::sol_watcher_get_info(uint64_t watcher_id) const {
    return watcher_manager_->get_watcher(watcher_id);
}

// Ring buffer methods
uint64_t AsyncBpfExecutionEngine::sol_ring_buffer_create(
    const std::string& program_id,
    uint64_t size)
{
    return ring_buffer_manager_->create_buffer(program_id, size);
}

bool AsyncBpfExecutionEngine::sol_ring_buffer_push(
    uint64_t buffer_id,
    const uint8_t* data,
    uint32_t data_len,
    uint32_t flags)
{
    return ring_buffer_manager_->push(buffer_id, data, data_len, current_slot_.load(), flags);
}

std::vector<uint8_t> AsyncBpfExecutionEngine::sol_ring_buffer_pop(uint64_t buffer_id) {
    return ring_buffer_manager_->pop(buffer_id);
}

bool AsyncBpfExecutionEngine::sol_ring_buffer_destroy(uint64_t buffer_id) {
    return ring_buffer_manager_->destroy_buffer(buffer_id);
}

const BpfRingBuffer* AsyncBpfExecutionEngine::sol_ring_buffer_get_info(uint64_t buffer_id) const {
    return ring_buffer_manager_->get_buffer(buffer_id);
}

void AsyncBpfExecutionEngine::process_slot(uint64_t slot) {
    current_slot_.store(slot);
    task_scheduler_->set_current_slot(slot);
    
    // Check and fire ready timers
    auto timer_tasks = timer_manager_->check_timers(slot);
    for (auto& task : timer_tasks) {
        task_scheduler_->submit_task(std::move(task));
    }
    
    // Cleanup expired timers periodically (at TIMER_CLEANUP_INTERVAL_SLOTS intervals)
    if (slot % TIMER_CLEANUP_INTERVAL_SLOTS == 0) {
        timer_manager_->cleanup_expired(slot);
    }
}

void AsyncBpfExecutionEngine::notify_account_change(
    const std::string& account_pubkey,
    const std::vector<uint8_t>& old_data,
    const std::vector<uint8_t>& new_data,
    uint64_t old_lamports,
    uint64_t new_lamports)
{
    uint64_t slot = current_slot_.load();
    
    auto watcher_tasks = watcher_manager_->check_account_change(
        account_pubkey, old_data, new_data, old_lamports, new_lamports, slot
    );
    
    for (auto& task : watcher_tasks) {
        task_scheduler_->submit_task(std::move(task));
    }
}

AsyncExecutionStats AsyncBpfExecutionEngine::get_stats() const {
    AsyncExecutionStats stats;
    timer_manager_->get_stats(stats);
    watcher_manager_->get_stats(stats);
    task_scheduler_->get_stats(stats);
    ring_buffer_manager_->get_stats(stats);
    return stats;
}

void AsyncBpfExecutionEngine::reset_stats() {
    // Stats are cumulative, would need to add reset methods to managers
}

void AsyncBpfExecutionEngine::slot_processor_loop() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(slot_mutex_);
        slot_cv_.wait_for(lock, std::chrono::milliseconds(100));
        
        if (!running_.load()) {
            break;
        }
        
        // Process current slot
        uint64_t slot = current_slot_.load();
        if (slot > 0) {
            // Check timers
            auto timer_tasks = timer_manager_->check_timers(slot);
            for (auto& task : timer_tasks) {
                task_scheduler_->submit_task(std::move(task));
            }
        }
    }
}

// ============================================================================
// Global Engine Instance (for C API)
// ============================================================================

static std::unique_ptr<AsyncBpfExecutionEngine> g_async_engine;
static std::mutex g_engine_mutex;
static std::string g_current_program_id;

static AsyncBpfExecutionEngine* get_engine() {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (!g_async_engine) {
        g_async_engine = std::make_unique<AsyncBpfExecutionEngine>();
        g_async_engine->initialize();
    }
    return g_async_engine.get();
}

} // namespace svm
} // namespace slonana

// ============================================================================
// Extern "C" Syscall Implementations
// ============================================================================

extern "C" {

using namespace slonana::svm;

uint64_t sol_timer_create(
    uint64_t trigger_slot,
    const uint8_t* callback_data,
    uint64_t callback_data_len,
    uint64_t compute_budget)
{
    if (!callback_data && callback_data_len > 0) {
        return 0;
    }
    
    auto* engine = get_engine();
    std::vector<uint8_t> data;
    if (callback_data && callback_data_len > 0) {
        data.assign(callback_data, callback_data + callback_data_len);
    }
    
    return engine->sol_timer_create(
        g_current_program_id,
        trigger_slot,
        data,
        {},
        compute_budget
    );
}

uint64_t sol_timer_create_periodic(
    uint64_t start_slot,
    uint64_t period_slots,
    const uint8_t* callback_data,
    uint64_t callback_data_len,
    uint64_t compute_budget)
{
    if (!callback_data && callback_data_len > 0) {
        return 0;
    }
    
    auto* engine = get_engine();
    std::vector<uint8_t> data;
    if (callback_data && callback_data_len > 0) {
        data.assign(callback_data, callback_data + callback_data_len);
    }
    
    return engine->sol_timer_create_periodic(
        g_current_program_id,
        start_slot,
        period_slots,
        data,
        {},
        compute_budget
    );
}

uint64_t sol_timer_cancel(uint64_t timer_id) {
    auto* engine = get_engine();
    return engine->sol_timer_cancel(timer_id) ? ASYNC_SUCCESS : ASYNC_ERROR_INVALID_TIMER;
}

uint64_t sol_watcher_create(
    const uint8_t* account_pubkey,
    uint8_t trigger_type,
    const uint8_t* callback_data,
    uint64_t callback_data_len,
    uint64_t compute_budget)
{
    if (!account_pubkey) {
        return 0;
    }
    
    if (!callback_data && callback_data_len > 0) {
        return 0;
    }
    
    auto* engine = get_engine();
    // Safely convert binary pubkey to string - use hex encoding for safety
    std::string account;
    account.reserve(64);
    static const char hex_chars[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        account.push_back(hex_chars[(account_pubkey[i] >> 4) & 0xF]);
        account.push_back(hex_chars[account_pubkey[i] & 0xF]);
    }
    std::vector<uint8_t> data;
    if (callback_data && callback_data_len > 0) {
        data.assign(callback_data, callback_data + callback_data_len);
    }
    
    return engine->sol_watcher_create(
        g_current_program_id,
        account,
        static_cast<WatcherTrigger>(trigger_type),
        data,
        {},
        compute_budget
    );
}

uint64_t sol_watcher_create_threshold(
    const uint8_t* account_pubkey,
    uint8_t trigger_type,
    uint64_t offset,
    int64_t threshold,
    const uint8_t* callback_data,
    uint64_t callback_data_len,
    uint64_t compute_budget)
{
    if (!account_pubkey) {
        return 0;
    }
    
    if (!callback_data && callback_data_len > 0) {
        return 0;
    }
    
    auto* engine = get_engine();
    // Safely convert binary pubkey to string - use hex encoding for safety
    std::string account;
    account.reserve(64);
    static const char hex_chars[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        account.push_back(hex_chars[(account_pubkey[i] >> 4) & 0xF]);
        account.push_back(hex_chars[account_pubkey[i] & 0xF]);
    }
    std::vector<uint8_t> data;
    if (callback_data && callback_data_len > 0) {
        data.assign(callback_data, callback_data + callback_data_len);
    }
    
    return engine->sol_watcher_create_threshold(
        g_current_program_id,
        account,
        static_cast<WatcherTrigger>(trigger_type),
        offset,
        threshold,
        data,
        compute_budget
    );
}

uint64_t sol_watcher_remove(uint64_t watcher_id) {
    auto* engine = get_engine();
    return engine->sol_watcher_remove(watcher_id) ? ASYNC_SUCCESS : ASYNC_ERROR_INVALID_WATCHER;
}

uint64_t sol_ring_buffer_create(uint64_t size) {
    auto* engine = get_engine();
    return engine->sol_ring_buffer_create(g_current_program_id, size);
}

uint64_t sol_ring_buffer_push(
    uint64_t buffer_id,
    const uint8_t* data,
    uint64_t data_len)
{
    if (!data && data_len > 0) {
        return ASYNC_ERROR_NULL_POINTER;
    }
    
    auto* engine = get_engine();
    return engine->sol_ring_buffer_push(buffer_id, data, static_cast<uint32_t>(data_len), 0)
        ? ASYNC_SUCCESS : ASYNC_ERROR_BUFFER_FULL;
}

uint64_t sol_ring_buffer_pop(
    uint64_t buffer_id,
    uint8_t* output,
    uint64_t output_len)
{
    if (!output || output_len == 0) {
        return 0;
    }
    
    auto* engine = get_engine();
    auto data = engine->sol_ring_buffer_pop(buffer_id);
    
    if (data.empty()) {
        return 0;
    }
    
    size_t copy_len = std::min(static_cast<size_t>(output_len), data.size());
    std::memcpy(output, data.data(), copy_len);
    return copy_len;
}

uint64_t sol_ring_buffer_destroy(uint64_t buffer_id) {
    auto* engine = get_engine();
    return engine->sol_ring_buffer_destroy(buffer_id) ? ASYNC_SUCCESS : ASYNC_ERROR_INVALID_BUFFER;
}

uint64_t sol_get_slot() {
    auto* engine = get_engine();
    return engine->get_current_slot();
}

} // extern "C"
