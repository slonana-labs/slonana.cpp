#include "consensus/proof_of_history.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

using std::atomic;
using std::mt19937;
using std::random_device;
using std::thread;
using std::uniform_int_distribution;
using std::vector;
using std::chrono::high_resolution_clock;
using std::chrono::microseconds;
using std::chrono::milliseconds;

class ConcurrencyStressTest {
public:
  static void run_global_poh_stress_test() {
    std::cout << "🔬 Running GlobalProofOfHistory concurrency stress test...\n";

    const int num_threads = 8;
    const int operations_per_thread = 1000;
    atomic<int> successful_operations{0};
    atomic<int> failed_operations{0};

    // Initialize GlobalProofOfHistory with default genesis hash
    slonana::consensus::PohConfig config;
    config.ticks_per_slot = 8;
    config.enable_lock_free_structures = true;
    slonana::consensus::Hash genesis_hash(32, 0x42); // Genesis hash for testing

    if (!slonana::consensus::GlobalProofOfHistory::initialize(config,
                                                              genesis_hash)) {
      std::cout << "❌ Failed to initialize GlobalProofOfHistory\n";
      return;
    }

    std::vector<std::thread> threads;

    // Thread function that performs concurrent operations
    auto worker = [&](int thread_id) {
      // Use thread_local RNG for better performance and cleaner per-thread
      // randomness
      thread_local random_device rd;
      thread_local mt19937 gen(rd());
      thread_local uniform_int_distribution<> dis(0, 255);

      for (int i = 0; i < operations_per_thread; ++i) {
        try {
          // Generate random hash
          slonana::consensus::Hash tx_hash(32);
          for (auto &byte : tx_hash) {
            byte = static_cast<uint8_t>(dis(gen));
          }

          // Test concurrent access patterns
          switch (i % 4) {
          case 0: {
            // Test mix_transaction
            uint64_t seq =
                slonana::consensus::GlobalProofOfHistory::mix_transaction(
                    tx_hash);
            if (seq > 0)
              successful_operations.fetch_add(1, std::memory_order_relaxed);
            break;
          }
          case 1: {
            // Test get_current_entry
            (void)slonana::consensus::GlobalProofOfHistory::get_current_entry();
            successful_operations.fetch_add(1, std::memory_order_relaxed);
            break;
          }
          case 2: {
            // Test get_current_slot
            (void)slonana::consensus::GlobalProofOfHistory::get_current_slot();
            successful_operations.fetch_add(1, std::memory_order_relaxed);
            break;
          }
          case 3: {
            // Test is_initialized (should always be true during test)
            if (slonana::consensus::GlobalProofOfHistory::is_initialized()) {
              successful_operations.fetch_add(1, std::memory_order_relaxed);
            }
            break;
          }
          }

          // Small random delay to increase chance of races
          if (i % 10 == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(1));
          }

        } catch (const std::exception &e) {
          failed_operations.fetch_add(1, std::memory_order_relaxed);
          std::cout << "❌ CRITICAL: Thread " << thread_id
                    << " error: " << e.what() << "\n";
          // Exit early on critical exceptions to prevent masking serious bugs
          break;
        }
      }
    };

    // Start all threads
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_threads; ++i) {
      threads.emplace_back(worker, i);
    }

    // Wait for all threads to complete
    for (auto &thread : threads) {
      thread.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    // Cleanup
    slonana::consensus::GlobalProofOfHistory::shutdown();

    // Report results
    int total_ops = num_threads * operations_per_thread;
    std::cout << "✅ Stress test completed in " << duration.count() << "ms\n";
    std::cout << "📊 Results:\n";
    std::cout << "   - Total operations: " << total_ops << "\n";
    std::cout << "   - Successful: " << successful_operations.load() << "\n";
    std::cout << "   - Failed: " << failed_operations.load() << "\n";
    std::cout << "   - Success rate: "
              << (100.0 * successful_operations.load() / total_ops) << "%\n";

    if (failed_operations.load() == 0) {
      std::cout << "🎉 All operations completed successfully - no race "
                   "conditions detected!\n";
    } else {
      std::cout << "❌ Some operations failed - potential race conditions "
                   "detected!\n";
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
        uint64_t current =
            sequence_counter.fetch_add(1, std::memory_order_release);

        // Verify monotonic increase (no races)
        if (current < last_value) {
          std::cout << "❌ Thread " << thread_id
                    << " detected ordering violation: " << current << " < "
                    << last_value << "\n";
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
    for (auto &thread : threads) {
      thread.join();
    }

    uint64_t final_value = sequence_counter.load(std::memory_order_acquire);
    uint64_t expected_value = num_threads * iterations;

    std::cout << "📊 Atomic test results:\n";
    std::cout << "   - Expected final value: " << expected_value << "\n";
    std::cout << "   - Actual final value: " << final_value << "\n";

    if (final_value == expected_value) {
      std::cout
          << "✅ Atomic operations working correctly - no race conditions!\n";
    } else {
      std::cout << "❌ Atomic operations failed - race condition detected!\n";
    }
  }

  static void run_shutdown_race_test() {
    std::cout << "\n🔬 Running shutdown race condition test...\n";

    const int num_iterations = 100;
    int successful_shutdowns = 0;

    for (int i = 0; i < num_iterations; ++i) {
      slonana::consensus::PohConfig config;
      config.ticks_per_slot = 4;
      slonana::consensus::Hash test_genesis_hash(32,
                                                 static_cast<uint8_t>(i % 256));

      // Initialize with test genesis hash
      if (!slonana::consensus::GlobalProofOfHistory::initialize(
              config, test_genesis_hash)) {
        continue;
      }

      // Start a thread that performs operations
      std::atomic<bool> stop_worker{false};
      std::thread worker([&]() {
        slonana::consensus::Hash tx_hash(32, static_cast<uint8_t>(i % 256));
        while (!stop_worker.load(std::memory_order_acquire)) {
          try {
            slonana::consensus::GlobalProofOfHistory::mix_transaction(tx_hash);
            slonana::consensus::GlobalProofOfHistory::get_current_entry();
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
        slonana::consensus::GlobalProofOfHistory::shutdown();
        successful_shutdowns++;
      } catch (const std::exception &e) {
        std::cout << "⚠️  Shutdown failed on iteration " << i << ": " << e.what()
                  << "\n";
      }

      worker.join();
    }

    std::cout << "📊 Shutdown test results:\n";
    std::cout << "   - Successful shutdowns: " << successful_shutdowns << "/"
              << num_iterations << "\n";

    if (successful_shutdowns == num_iterations) {
      std::cout << "✅ Shutdown race test passed - no race conditions!\n";
    } else {
      std::cout << "❌ Some shutdowns failed - potential race conditions!\n";
    }
  }
};

int main() {
  std::cout << "🧪 Concurrency Stress Test Suite\n";
  std::cout << "==================================================\n";

  try {
    ConcurrencyStressTest::run_global_poh_stress_test();
    ConcurrencyStressTest::run_atomic_operations_test();
    ConcurrencyStressTest::run_shutdown_race_test();

    std::cout << "\n🎉 All concurrency stress tests completed!\n";
    std::cout << "💡 Run with ThreadSanitizer for additional validation:\n";
    std::cout << "   cmake .. -DENABLE_TSAN=ON && make && "
                 "./concurrency_stress_test\n";

  } catch (const std::exception &e) {
    std::cout << "❌ Test suite failed: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}