/**
 * Lock-Free Queue Push Failure Handling Example
 * 
 * This example demonstrates the proper protocol for handling push failures
 * in the lock-free request queue, including memory management and backpressure.
 */

#include "network/distributed_load_balancer.h"
#include "common/types.h"
#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>

using namespace slonana::network;
using namespace slonana::common;

// Example backpressure policies
enum class BackpressurePolicy {
    DROP_OLDEST,        // Drop oldest item in queue
    DROP_NEWEST,        // Drop newest item (reject submission)
    BLOCK_UNTIL_SPACE,  // Block producer until space available
    RATE_LIMIT          // Apply rate limiting
};

// Demonstrate backpressure handling
void demonstrate_backpressure_handling() {
    std::cout << "\n📚 Lock-Free Queue Push Failure Handling Examples\n";
    std::cout << "==================================================\n\n";

    ValidatorConfig config;
    config.identity_keypair_path = "/tmp/test_keypair";

    // Example 1: Small queue to demonstrate backpressure
    std::cout << "Example 1: Small Queue Capacity (Backpressure Scenario)\n";
    std::cout << "--------------------------------------------------------\n";
    {
        size_t small_capacity = 8;  // Intentionally small for demo
        DistributedLoadBalancer balancer("backpressure_demo", config, small_capacity);
        
        std::cout << "✓ Created balancer with capacity: " << small_capacity << "\n";
        std::cout << "✓ This demonstrates what happens when queue fills up\n\n";

        // Show initial metrics
        auto metrics = balancer.get_queue_metrics();
        std::cout << "Initial state:\n";
        std::cout << "  - Capacity: " << metrics.capacity << "\n";
        std::cout << "  - Allocated: " << metrics.allocated_count << "\n";
        std::cout << "  - Utilization: " << metrics.utilization_percent << "%\n\n";
    }

    // Example 2: Proper push failure handling pattern
    std::cout << "\nExample 2: Proper Push Failure Handling Code Pattern\n";
    std::cout << "-----------------------------------------------------\n";
    std::cout << R"(
// ✅ CORRECT: Always handle push failure
ConnectionRequest* req = new ConnectionRequest(...);
if (!queue->push(req)) {
    // 1. MANDATORY: Delete to prevent memory leak
    delete req;
    
    // 2. Track failure for monitoring/alerting
    push_failure_count_.fetch_add(1, std::memory_order_relaxed);
    
    // 3. Implement backpressure policy (choose one):
    
    // Option A: Return error to client
    return ConnectionResponse{
        .success = false,
        .error_message = "Service overloaded, please retry later"
    };
    
    // Option B: Drop with logging for monitoring
    LOG_WARNING("Request dropped due to queue full");
    dropped_requests_metric.increment();
    
    // Option C: Retry with exponential backoff
    std::this_thread::sleep_for(backoff_delay);
    backoff_delay *= 2;  // exponential backoff
    // ... retry logic ...
    
    // Option D: Apply rate limiting
    rate_limiter.throttle(client_id);
} else {
    // Success: Queue takes ownership, track allocation
    queue_allocated_count_.fetch_add(1, std::memory_order_relaxed);
}
)";

    // Example 3: Monitoring queue health
    std::cout << "\nExample 3: Monitoring Queue Health in Production\n";
    std::cout << "------------------------------------------------\n";
    std::cout << R"(
// Regular monitoring loop (e.g., every 60 seconds)
auto metrics = balancer.get_queue_metrics();

// Alert if utilization is high (>80%)
if (metrics.utilization_percent > 80.0) {
    alert("Queue utilization high: " + 
          std::to_string(metrics.utilization_percent) + "%");
}

// Alert if seeing push failures
if (metrics.push_failure_count > 0) {
    alert("Queue backpressure detected: " + 
          std::to_string(metrics.push_failure_count) + " failures");
}

// Capacity planning: if consistently high, increase capacity
if (metrics.utilization_percent > 70.0) {
    consider_increasing_queue_capacity();
}
)";

    std::cout << "\n✅ Examples completed!\n\n";
}

int main() {
    std::cout << "\n╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Lock-Free Queue Push Failure Handling Example           ║\n";
    std::cout << "║  Demonstrates proper memory management and backpressure   ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n";

    try {
        demonstrate_backpressure_handling();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Error: " << e.what() << "\n\n";
        return 1;
    }
}
