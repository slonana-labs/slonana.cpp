#include "consensus/proof_of_history.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

/**
 * High-contention shutdown race condition test
 * Tests for race conditions during rapid init/shutdown cycles
 */
class ShutdownRaceTest {
public:
    static void test_rapid_init_shutdown() {
        std::cout << "🔬 Testing rapid init/shutdown race conditions...\n";
        
        const int num_cycles = 50;
        const int num_threads_per_cycle = 4;
        std::atomic<int> successful_cycles{0};
        std::atomic<int> failed_cycles{0};
        
        for (int cycle = 0; cycle < num_cycles; ++cycle) {
            try {
                // Initialize GlobalProofOfHistory
                slonana::consensus::PohConfig config;
                config.ticks_per_slot = 4;
                config.enable_lock_free_structures = true;
                config.enable_hashing_threads = true;
                config.hashing_threads = 4;
                
                slonana::consensus::Hash genesis_hash(32, static_cast<uint8_t>(cycle % 256));
                
                if (!slonana::consensus::GlobalProofOfHistory::initialize(config, genesis_hash)) {
                    failed_cycles.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                
                std::vector<std::thread> threads;
                std::atomic<bool> stop_workers{false};
                
                // Worker thread that performs operations
                auto worker = [&](int thread_id) {
                    int local_ops = 0;
                    while (!stop_workers.load(std::memory_order_acquire)) {
                        try {
                            // Generate hash
                            slonana::consensus::Hash tx_hash(32);
                            for (auto& byte : tx_hash) {
                                byte = static_cast<uint8_t>((cycle + thread_id + local_ops) % 256);
                            }
                            
                            // Try operations
                            (void)slonana::consensus::GlobalProofOfHistory::mix_transaction(tx_hash);
                            (void)slonana::consensus::GlobalProofOfHistory::get_current_entry();
                            (void)slonana::consensus::GlobalProofOfHistory::get_current_slot();
                            
                            local_ops++;
                            
                            // Short delay
                            if (local_ops % 10 == 0) {
                                std::this_thread::sleep_for(std::chrono::microseconds(1));
                            }
                            
                        } catch (const std::exception& e) {
                            // Expected during shutdown
                            break;
                        }
                    }
                };
                
                // Start worker threads
                for (int i = 0; i < num_threads_per_cycle; ++i) {
                    threads.emplace_back(worker, i);
                }
                
                // Let threads run briefly
                std::this_thread::sleep_for(std::chrono::milliseconds(1 + (cycle % 5)));
                
                // Signal stop and shutdown
                stop_workers.store(true, std::memory_order_release);
                
                // Shutdown should be safe even with active threads
                slonana::consensus::GlobalProofOfHistory::shutdown();
                
                // Wait for all threads to complete
                for (auto& thread : threads) {
                    thread.join();
                }
                
                successful_cycles.fetch_add(1, std::memory_order_relaxed);
                
            } catch (const std::exception& e) {
                failed_cycles.fetch_add(1, std::memory_order_relaxed);
                std::cout << "❌ Cycle " << cycle << " failed: " << e.what() << "\n";
                
                // Try to cleanup in case of partial initialization
                try {
                    slonana::consensus::GlobalProofOfHistory::shutdown();
                } catch (...) {
                    // Ignore cleanup errors
                }
            }
            
            // Small delay between cycles
            if (cycle % 10 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        
        std::cout << "📊 Shutdown race test results:\n";
        std::cout << "   - Successful cycles: " << successful_cycles.load() << "/" << num_cycles << "\n";
        std::cout << "   - Failed cycles: " << failed_cycles.load() << "\n";
        std::cout << "   - Success rate: " << (100.0 * successful_cycles.load() / num_cycles) << "%\n";
        
        if (failed_cycles.load() == 0) {
            std::cout << "🎉 Shutdown race test passed - no race conditions detected!\n";
        } else {
            std::cout << "❌ Some cycles failed - potential race conditions detected!\n";
        }
    }
};

int main() {
    std::cout << "🧪 Shutdown Race Condition Test\n";
    std::cout << "================================\n";
    
    try {
        ShutdownRaceTest::test_rapid_init_shutdown();
        std::cout << "\n✅ Shutdown race test completed!\n";
        std::cout << "💡 This test validates shutdown safety under high contention\n";
        
    } catch (const std::exception& e) {
        std::cout << "❌ Test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}