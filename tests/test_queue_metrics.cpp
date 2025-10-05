/**
 * Simple Queue Metrics Test
 * Tests the new queue configuration and metrics APIs
 */

#include "network/distributed_load_balancer.h"
#include "common/types.h"
#include <iostream>

using namespace slonana::network;
using namespace slonana::common;

int main() {
    std::cout << "\n🧪 Queue Metrics and Configuration Test\n";
    std::cout << "=========================================\n\n";

    ValidatorConfig config;
    config.identity_keypair_path = "/tmp/test_keypair";
    
    // Test 1: Default capacity
    {
        std::cout << "Test 1: Default capacity\n";
        DistributedLoadBalancer balancer1("test1", config);
        std::cout << "  ✓ Default capacity: " << balancer1.get_queue_capacity() << "\n";
        if (balancer1.get_queue_capacity() != 1024) {
            std::cerr << "  ❌ Expected default capacity 1024\n";
            return 1;
        }
    }

    // Test 2: Custom capacity
    {
        std::cout << "\nTest 2: Custom capacity\n";
        size_t custom_capacity = 512;
        DistributedLoadBalancer balancer2("test2", config, custom_capacity);
        std::cout << "  ✓ Custom capacity: " << balancer2.get_queue_capacity() << "\n";
        if (balancer2.get_queue_capacity() != custom_capacity) {
            std::cerr << "  ❌ Expected custom capacity " << custom_capacity << "\n";
            return 1;
        }
    }

    // Test 3: Queue metrics
    {
        std::cout << "\nTest 3: Queue metrics\n";
        DistributedLoadBalancer balancer3("test3", config, 2048);
        
        auto metrics = balancer3.get_queue_metrics();
        std::cout << "  ✓ Initial metrics:\n";
        std::cout << "    - Capacity: " << metrics.capacity << "\n";
        std::cout << "    - Allocated: " << metrics.allocated_count << "\n";
        std::cout << "    - Push failures: " << metrics.push_failure_count << "\n";
        std::cout << "    - Utilization: " << metrics.utilization_percent << "%\n";
        
        // Note: If lock-free queue is not available (HAS_LOCKFREE_QUEUE=0),
        // metrics will show 0 capacity - this is expected fallback behavior
        if (metrics.capacity == 0) {
            std::cout << "  ℹ Lock-free queue not available, using mutex fallback\n";
        } else if (metrics.capacity != 2048) {
            std::cerr << "  ❌ Wrong capacity in metrics\n";
            return 1;
        }
        
        if (metrics.allocated_count != 0) {
            std::cerr << "  ❌ Queue should be empty initially\n";
            return 1;
        }
        
        if (metrics.push_failure_count != 0) {
            std::cerr << "  ❌ Should have no push failures initially\n";
            return 1;
        }
    }

    // Test 4: Set capacity after construction
    {
        std::cout << "\nTest 4: Set capacity before start\n";
        DistributedLoadBalancer balancer4("test4", config);
        balancer4.set_queue_capacity(4096);
        std::cout << "  ✓ Capacity after set: " << balancer4.get_queue_capacity() << "\n";
        if (balancer4.get_queue_capacity() != 4096) {
            std::cerr << "  ❌ Set capacity failed\n";
            return 1;
        }
    }

    std::cout << "\n✅ All queue metrics tests passed!\n\n";
    return 0;
}
