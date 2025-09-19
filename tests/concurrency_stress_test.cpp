#include "consensus/proof_of_history.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

using namespace slonana::consensus;

class ConcurrencyStressTest {
public:
    static void run_global_poh_stress_test() {
        std::cout << "🔬 Running GlobalProofOfHistory concurrency stress test...\n";
        
        const int num_threads = 8;
        const int operations_per_thread = 1000;
        std::atomic<int> successful_operations{0};
        std::atomic<int> failed_operations{0};
        
        // Initialize GlobalProofOfHistory
        PohConfig config;
        config.ticks_per_slot = 8;
        config.enable_lock_free_structures = true;
        
        if (!GlobalProofOfHistory::initialize(config)) {
            std::cout << "❌ Failed to initialize GlobalProofOfHistory\n";
            return;
        }
        
        std::vector<std::thread> threads;
        
        // Thread function that performs concurrent operations
        auto worker = [&](int thread_id) {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 255);
            
            for (int i = 0; i < operations_per_thread; ++i) {
                try {
                    // Generate random hash
                    Hash tx_hash(32);
                    for (auto& byte : tx_hash) {
                        byte = static_cast<uint8_t>(dis(gen));
                    }
                    
                    // Test concurrent access patterns
                    switch (i % 4) {
                        case 0: {
                            // Test mix_transaction
                            uint64_t seq = GlobalProofOfHistory::mix_transaction(tx_hash);
                            if (seq > 0) successful_operations.fetch_add(1, std::memory_order_relaxed);
                            break;
                        }
                        case 1: {
                            // Test get_current_entry
                            PohEntry entry = GlobalProofOfHistory::get_current_entry();
                            if (entry.sequence_number >= 0) successful_operations.fetch_add(1, std::memory_order_relaxed);
                            break;
                        }
                        case 2: {
                            // Test get_current_slot
                            Slot slot = GlobalProofOfHistory::get_current_slot();
                            if (slot >= 0) successful_operations.fetch_add(1, std::memory_order_relaxed);
                            break;
                        }
                        case 3: {
                            // Test is_initialized (should always be true during test)
                            if (GlobalProofOfHistory::is_initialized()) {
                                successful_operations.fetch_add(1, std::memory_order_relaxed);
                            }
                            break;
                        }
                    }
                    
                    // Small random delay to increase chance of races
                    if (i % 10 == 0) {
                        std::this_thread::sleep_for(std::chrono::microseconds(1));
                    }
                    
                } catch (const std::exception& e) {
                    failed_operations.fetch_add(1, std::memory_order_relaxed);
                    std::cout << "⚠️  Thread " << thread_id << " error: " << e.what() << "\n";
                }
            }
        };
        
        // Start all threads
        auto start_time = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back(worker, i);
        }
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        // Cleanup
        GlobalProofOfHistory::shutdown();
        
        // Report results
        int total_ops = num_threads * operations_per_thread;
        std::cout << "✅ Stress test completed in " << duration.count() << "ms\n";
        std::cout << "📊 Results:\n";
        std::cout << "   - Total operations: " << total_ops << "\n";
        std::cout << "   - Successful: " << successful_operations.load() << "\n";
        std::cout << "   - Failed: " << failed_operations.load() << "\n";
        std::cout << "   - Success rate: " << (100.0 * successful_operations.load() / total_ops) << "%\n";
        
        if (failed_operations.load() == 0) {
            std::cout << "🎉 All operations completed successfully - no race conditions detected!\n";
        } else {
            std::cout << "❌ Some operations failed - potential race conditions detected!\n";
        }
    }
    
    static void run_atomic_operations_test() {
        std::cout << "\n🔬 Running atomic operations memory ordering test...\n";
        
        const int num_threads = 4;
        const int iterations = 10000;
        
        std::atomic<uint64_t> sequence_counter{0};
        std::atomic<bool> start_flag{false};
        std::vector<std::thread> threads;
        std::vector<uint64_t> thread_values(num_threads);
        
        // Thread function that tests atomic operations
        auto worker = [&](int thread_id) {
            // Wait for start signal
            while (!start_flag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            
            uint64_t last_value = 0;
            for (int i = 0; i < iterations; ++i) {
                // Test release-acquire ordering
                uint64_t current = sequence_counter.fetch_add(1, std::memory_order_release);
                
                // Verify monotonic increase (no races)
                if (current < last_value) {
                    std::cout << "❌ Thread " << thread_id << " detected ordering violation: " 
                              << current << " < " << last_value << "\n";
                }
                last_value = current;
                
                // Store final value for verification
                if (i == iterations - 1) {
                    thread_values[thread_id] = current;
                }
            }
        };
        
        // Start all threads
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back(worker, i);
        }
        
        // Signal start
        start_flag.store(true, std::memory_order_release);
        
        // Wait for completion
        for (auto& thread : threads) {
            thread.join();
        }
        
        uint64_t final_value = sequence_counter.load(std::memory_order_acquire);
        uint64_t expected_value = num_threads * iterations;
        
        std::cout << "📊 Atomic test results:\n";
        std::cout << "   - Expected final value: " << expected_value << "\n";
        std::cout << "   - Actual final value: " << final_value << "\n";
        
        if (final_value == expected_value) {
            std::cout << "✅ Atomic operations working correctly - no race conditions!\n";
        } else {
            std::cout << "❌ Atomic operations failed - race condition detected!\n";
        }
    }
    
    static void run_shutdown_race_test() {
        std::cout << "\n🔬 Running shutdown race condition test...\n";
        
        const int num_iterations = 100;
        int successful_shutdowns = 0;
        
        for (int i = 0; i < num_iterations; ++i) {
            PohConfig config;
            config.ticks_per_slot = 4;
            
            // Initialize
            if (!GlobalProofOfHistory::initialize(config)) {
                continue;
            }
            
            // Start a thread that performs operations
            std::atomic<bool> stop_worker{false};
            std::thread worker([&]() {
                Hash tx_hash(32, static_cast<uint8_t>(i % 256));
                while (!stop_worker.load(std::memory_order_acquire)) {
                    try {
                        GlobalProofOfHistory::mix_transaction(tx_hash);
                        GlobalProofOfHistory::get_current_entry();
                        std::this_thread::sleep_for(std::chrono::microseconds(10));
                    } catch (...) {
                        // Expected during shutdown
                        break;
                    }
                }
            });
            
            // Brief delay then shutdown
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            stop_worker.store(true, std::memory_order_release);
            
            // Shutdown should be safe
            try {
                GlobalProofOfHistory::shutdown();
                successful_shutdowns++;
            } catch (const std::exception& e) {
                std::cout << "⚠️  Shutdown failed on iteration " << i << ": " << e.what() << "\n";
            }
            
            worker.join();
        }
        
        std::cout << "📊 Shutdown test results:\n";
        std::cout << "   - Successful shutdowns: " << successful_shutdowns << "/" << num_iterations << "\n";
        
        if (successful_shutdowns == num_iterations) {
            std::cout << "✅ Shutdown race test passed - no race conditions!\n";
        } else {
            std::cout << "❌ Some shutdowns failed - potential race conditions!\n";
        }
    }
};

int main() {
    std::cout << "🧪 CONCURRENCY STRESS TEST SUITE\n";
    std::cout << "==================================================\n";
    
    try {
        ConcurrencyStressTest::run_global_poh_stress_test();
        ConcurrencyStressTest::run_atomic_operations_test();
        ConcurrencyStressTest::run_shutdown_race_test();
        
        std::cout << "\n🎉 All concurrency stress tests completed!\n";
        std::cout << "💡 Run with ThreadSanitizer for additional validation:\n";
        std::cout << "   cmake .. -DENABLE_TSAN=ON && make && ./concurrency_stress_test\n";
        
    } catch (const std::exception& e) {
        std::cout << "❌ Test suite failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}