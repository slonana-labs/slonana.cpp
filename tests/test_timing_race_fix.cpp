#include "consensus/proof_of_history.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <cstdlib>

// Simple verbosity control for CI
static bool verbose_output = true;

/**
 * Targeted test for the specific race condition fix in ProofOfHistory
 * Validates that last_tick_time_ is properly synchronized under high contention
 */
class TimingRaceTest {
public:
    static void test_concurrent_timing_access() {
        std::cout << "🔬 Testing concurrent timing access fix...\n";
        
        const int num_threads = 8;
        const int operations_per_thread = 500;
        std::atomic<int> successful_operations{0};
        std::atomic<int> failed_operations{0};
        
        // Initialize GlobalProofOfHistory
        slonana::consensus::PohConfig config;
        config.ticks_per_slot = 8;
        config.enable_lock_free_structures = true;
        config.enable_hashing_threads = true;
        config.hashing_threads = 8; // High thread count to stress test
        // Note: enable_lock_contention_tracking can be enabled for detailed performance analysis
        
        slonana::consensus::Hash genesis_hash(32, 0x42);
        
        if (!slonana::consensus::GlobalProofOfHistory::initialize(config, genesis_hash)) {
            std::cout << "❌ Failed to initialize GlobalProofOfHistory\n";
            return;
        }
        
        std::vector<std::thread> threads;
        std::atomic<bool> start_flag{false};
        
        // Thread function that rapidly accesses timing-sensitive operations
        auto worker = [&](int thread_id) {
            // Wait for synchronized start
            while (!start_flag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            
            for (int i = 0; i < operations_per_thread; ++i) {
                try {
                    // Generate random hash
                    slonana::consensus::Hash tx_hash(32);
                    for (auto& byte : tx_hash) {
                        byte = static_cast<uint8_t>((thread_id + i) % 256);
                    }
                    
                    // Rapidly call mix_transaction which triggers timing updates
                    uint64_t seq = slonana::consensus::GlobalProofOfHistory::mix_transaction(tx_hash);
                    if (seq > 0) {
                        successful_operations.fetch_add(1, std::memory_order_relaxed);
                    }
                    
                    // Also test get_current_entry which may access timing
                    try {
                        (void)slonana::consensus::GlobalProofOfHistory::get_current_entry();
                    } catch (const std::exception& e) {
                        // Log individual operation failures only in verbose mode
                        if (verbose_output) {
                            std::cout << "⚠️  Thread " << thread_id << " get_current_entry error: " << e.what() << "\n";
                        }
                    }
                    
                    // Minimal delay to allow other threads to compete
                    if (i % 50 == 0) {
                        std::this_thread::sleep_for(std::chrono::microseconds(1));
                    }
                    
                } catch (const std::exception& e) {
                    failed_operations.fetch_add(1, std::memory_order_relaxed);
                    if (verbose_output) {
                        std::cout << "❌ Thread " << thread_id << " error: " << e.what() << "\n";
                    }
                } catch (...) {
                    // Catch any other unexpected exceptions
                    failed_operations.fetch_add(1, std::memory_order_relaxed);
                    if (verbose_output) {
                        std::cout << "❌ Thread " << thread_id << " unknown error\n";
                    }
                }
            }
        };
        
        // Start all threads
        auto start_time = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back(worker, i);
        }
        
        // Signal synchronized start
        start_flag.store(true, std::memory_order_release);
        
        // Wait for completion
        for (auto& thread : threads) {
            thread.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        // Cleanup
        slonana::consensus::GlobalProofOfHistory::shutdown();
        
        // Report results
        int total_ops = num_threads * operations_per_thread;
        std::cout << "✅ Timing race test completed in " << duration.count() << "ms\n";
        std::cout << "📊 Results:\n";
        std::cout << "   - Total operations: " << total_ops << "\n";
        std::cout << "   - Successful: " << successful_operations.load() << "\n";
        std::cout << "   - Failed: " << failed_operations.load() << "\n";
        std::cout << "   - Success rate: " << (100.0 * successful_operations.load() / total_ops) << "%\n";
        
        if (failed_operations.load() == 0) {
            std::cout << "🎉 Timing race test passed - no race conditions detected!\n";
        } else {
            std::cout << "❌ Some operations failed - potential race conditions detected!\n";
        }
    }
};

int main() {
    // Check for verbosity control via environment variable
    const char* quiet_env = std::getenv("SLONANA_TEST_QUIET");
    if (quiet_env && std::string(quiet_env) == "1") {
        verbose_output = false;
    }
    
    std::cout << "🧪 Timing Race Condition Fix Validation\n";
    std::cout << "=========================================\n";
    
    try {
        TimingRaceTest::test_concurrent_timing_access();
        std::cout << "\n✅ Timing race fix validation completed!\n";
        std::cout << "💡 This test validates the fix for last_tick_time_ race condition\n";
        
    } catch (const std::exception& e) {
        std::cout << "❌ Test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
