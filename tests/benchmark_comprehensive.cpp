#include <iostream>
#include <chrono>
#include <thread>
#include "../include/network/udp_batch_manager.h"

int main() {
    std::cout << "=== Comprehensive UDP Batch Manager Benchmark ===\n\n";
    
    // Test configurations
    struct TestConfig {
        int threads;
        size_t buffer_pool;
        size_t batch_size;
        std::string name;
    };
    
    std::vector<TestConfig> configs = {
        {8, 200000, 64, "8 threads, 200K pool, 64 batch"},
        {8, 200000, 128, "8 threads, 200K pool, 128 batch"},
        {8, 100000, 64, "8 threads, 100K pool, 64 batch"},
        {16, 200000, 64, "16 threads, 200K pool, 64 batch"},
    };
    
    for (const auto& config : configs) {
        std::cout << "Testing: " << config.name << "\n";
        std::cout << "----------------------------------------\n";
        
        UDPBatchManager::BatchConfig batch_config;
        batch_config.max_batch_size = config.batch_size;
        batch_config.num_sender_threads = config.threads;
        batch_config.buffer_pool_size = config.buffer_pool;
        
        UDPBatchManager mgr(batch_config);
        
        // Simulate socket (use invalid fd for benchmark)
        int socket_fd = -1;
        mgr.initialize(socket_fd);
        
        // Queue packets
        const size_t total_packets = 1000000;
        std::vector<uint8_t> packet_data(1024, 0xAB);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (size_t i = 0; i < total_packets; ++i) {
            mgr.queue_packet(packet_data.data(), packet_data.size(), 
                           "127.0.0.1", 8080, 128);
        }
        
        // Wait a bit for processing
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        double seconds = duration / 1000000.0;
        
        double pps = total_packets / seconds;
        double gbps = (pps * 1024 * 8) / 1e9;
        double per_thread = pps / config.threads;
        
        std::cout << "  Duration: " << seconds << " seconds\n";
        std::cout << "  Throughput: " << (pps / 1000) << "K packets/sec\n";
        std::cout << "  Bandwidth: " << gbps << " Gbps\n";
        std::cout << "  Per-thread: " << (per_thread / 1000) << "K pps/thread\n";
        std::cout << "  vs 50K target: " << (pps / 50000) << "x\n";
        std::cout << "\n";
        
        mgr.shutdown();
    }
    
    return 0;
}
