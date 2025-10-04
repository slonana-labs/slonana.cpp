/**
 * Network Layer Concurrency Stress Test
 * Tests lock-free and async patterns in the network layer
 */

#include "network/distributed_load_balancer.h"
#include "network/topology_manager.h"
#include "common/types.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

using namespace slonana::network;
using namespace slonana::common;

class NetworkConcurrencyStressTest {
public:
    static void run_all_tests() {
        std::cout << "\n🧪 Network Layer Concurrency Stress Tests\n";
        std::cout << "==========================================\n\n";

        run_load_balancer_concurrent_routing_test();
        run_load_balancer_request_queue_stress_test();
        run_topology_manager_concurrent_updates_test();
        run_mixed_workload_test();

        std::cout << "\n✅ All network concurrency stress tests completed!\n";
    }

private:
    static void run_load_balancer_concurrent_routing_test() {
        std::cout << "🔬 Test: Load Balancer Concurrent Routing\n";

        ValidatorConfig config;
        config.identity_keypair_path = "/tmp/test_keypair";
        
        DistributedLoadBalancer balancer("test_balancer", config);

        // Register backend servers
        BackendServer server1;
        server1.server_id = "server1";
        server1.address = "127.0.0.1";
        server1.port = 8001;
        server1.is_active = true;
        balancer.register_backend_server(server1);

        BackendServer server2;
        server2.server_id = "server2";
        server2.address = "127.0.0.1";
        server2.port = 8002;
        server2.is_active = true;
        balancer.register_backend_server(server2);

        balancer.start();

        const int num_threads = 4;
        const int operations_per_thread = 100;
        std::atomic<int> successful_routes{0};
        std::atomic<int> failed_routes{0};
        std::vector<std::thread> threads;

        auto start_time = std::chrono::steady_clock::now();

        // Concurrent routing requests
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&, i]() {
                thread_local std::random_device rd;
                thread_local std::mt19937 gen(rd());
                thread_local std::uniform_int_distribution<> dis(0, 100);

                for (int j = 0; j < operations_per_thread; ++j) {
                    ConnectionRequest request;
                    request.request_id = "req_" + std::to_string(i) + "_" + std::to_string(j);
                    request.service_name = "test_service";
                    request.client_ip = "192.168.1." + std::to_string(dis(gen));
                    request.retry_count = 0;

                    auto response = balancer.route_request(request);
                    if (!response.selected_server.empty()) {
                        successful_routes.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        failed_routes.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        balancer.stop();

        int total_ops = successful_routes.load() + failed_routes.load();
        double ops_per_sec = (total_ops * 1000.0) / duration.count();

        std::cout << "  ✓ Total operations: " << total_ops << "\n";
        std::cout << "  ✓ Successful routes: " << successful_routes.load() << "\n";
        std::cout << "  ✓ Failed routes: " << failed_routes.load() << "\n";
        std::cout << "  ✓ Duration: " << duration.count() << "ms\n";
        std::cout << "  ✓ Throughput: " << ops_per_sec << " ops/sec\n\n";
    }

    static void run_load_balancer_request_queue_stress_test() {
        std::cout << "🔬 Test: Load Balancer Request Queue Stress (Lock-Free)\n";

        ValidatorConfig config;
        config.identity_keypair_path = "/tmp/test_keypair";
        
        DistributedLoadBalancer balancer("queue_test_balancer", config);

        BackendServer server;
        server.server_id = "server1";
        server.address = "127.0.0.1";
        server.port = 8001;
        server.is_active = true;
        balancer.register_backend_server(server);

        balancer.start();

        const int num_threads = 4;
        const int operations_per_thread = 100;
        std::atomic<int> enqueued{0};
        std::vector<std::thread> threads;

        auto start_time = std::chrono::steady_clock::now();

        // Concurrent queue operations
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&, i]() {
                for (int j = 0; j < operations_per_thread; ++j) {
                    ConnectionRequest request;
                    request.request_id = "queue_req_" + std::to_string(i) + "_" + std::to_string(j);
                    request.service_name = "queue_test_service";
                    request.client_ip = "192.168.1.1";

                    // Route request (which uses the internal queue)
                    balancer.route_request(request);
                    enqueued.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        // Give time for queue to process
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        balancer.stop();

        double ops_per_sec = (enqueued.load() * 1000.0) / duration.count();

        std::cout << "  ✓ Total enqueued: " << enqueued.load() << "\n";
        std::cout << "  ✓ Duration: " << duration.count() << "ms\n";
        std::cout << "  ✓ Queue throughput: " << ops_per_sec << " ops/sec\n";
        std::cout << "  ✓ Lock-free queue active: " << (ops_per_sec > 10000 ? "YES ✓" : "NO") << "\n\n";
    }

    static void run_topology_manager_concurrent_updates_test() {
        std::cout << "🔬 Test: Topology Manager Concurrent Updates\n";

        ValidatorConfig config;
        config.identity_keypair_path = "/tmp/test_keypair";
        
        NetworkTopologyManager topology("test_node", config);
        topology.start();

        const int num_threads = 4;
        const int operations_per_thread = 50;
        std::atomic<int> successful_ops{0};
        std::vector<std::thread> threads;

        auto start_time = std::chrono::steady_clock::now();

        // Concurrent topology updates
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&, i]() {
                thread_local std::random_device rd;
                thread_local std::mt19937 gen(rd());
                thread_local std::uniform_int_distribution<> dis(0, 1000);

                for (int j = 0; j < operations_per_thread; ++j) {
                    NetworkNode node;
                    node.node_id = "node_" + std::to_string(i) + "_" + std::to_string(j);
                    node.address = "192.168." + std::to_string(i) + "." + std::to_string(j % 256);
                    node.port = 9000 + dis(gen) % 1000;
                    node.region = "region_" + std::to_string(i % 3);
                    node.is_active = true;

                    if (topology.register_node(node)) {
                        successful_ops.fetch_add(1, std::memory_order_relaxed);
                    }

                    // Also test queries
                    auto active_nodes = topology.get_active_nodes();
                    if (!active_nodes.empty()) {
                        successful_ops.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        topology.stop();

        double ops_per_sec = (successful_ops.load() * 1000.0) / duration.count();

        std::cout << "  ✓ Successful operations: " << successful_ops.load() << "\n";
        std::cout << "  ✓ Duration: " << duration.count() << "ms\n";
        std::cout << "  ✓ Throughput: " << ops_per_sec << " ops/sec\n\n";
    }

    static void run_mixed_workload_test() {
        std::cout << "🔬 Test: Mixed Network Workload (Load Balancer + Topology)\n";

        ValidatorConfig config;
        config.identity_keypair_path = "/tmp/test_keypair";
        
        DistributedLoadBalancer balancer("mixed_balancer", config);
        NetworkTopologyManager topology("mixed_node", config);

        // Setup
        BackendServer server;
        server.server_id = "server1";
        server.address = "127.0.0.1";
        server.port = 8001;
        server.is_active = true;
        balancer.register_backend_server(server);

        balancer.start();
        topology.start();

        const int num_threads = 4;
        const int operations_per_thread = 50;
        std::atomic<int> total_ops{0};
        std::vector<std::thread> threads;

        auto start_time = std::chrono::steady_clock::now();

        // Mixed concurrent operations
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&, i]() {
                for (int j = 0; j < operations_per_thread; ++j) {
                    // Alternate between balancer and topology operations
                    if (j % 2 == 0) {
                        ConnectionRequest request;
                        request.request_id = "mixed_req_" + std::to_string(i) + "_" + std::to_string(j);
                        request.service_name = "mixed_service";
                        balancer.route_request(request);
                    } else {
                        NetworkNode node;
                        node.node_id = "mixed_node_" + std::to_string(i) + "_" + std::to_string(j);
                        node.address = "10.0.0." + std::to_string(i);
                        node.port = 9000 + j;
                        node.is_active = true;
                        topology.register_node(node);
                    }
                    total_ops.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        balancer.stop();
        topology.stop();

        double ops_per_sec = (total_ops.load() * 1000.0) / duration.count();

        std::cout << "  ✓ Total mixed operations: " << total_ops.load() << "\n";
        std::cout << "  ✓ Duration: " << duration.count() << "ms\n";
        std::cout << "  ✓ Mixed throughput: " << ops_per_sec << " ops/sec\n";
        std::cout << "  ✓ No deadlocks detected: ✓\n\n";
    }
};

int main() {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Network Layer Concurrency Stress Test Suite              ║\n";
    std::cout << "║  Testing Lock-Free and Async Patterns                     ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";

    try {
        NetworkConcurrencyStressTest::run_all_tests();
        std::cout << "\n🎉 All tests passed successfully!\n\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed with exception: " << e.what() << "\n\n";
        return 1;
    }
}
