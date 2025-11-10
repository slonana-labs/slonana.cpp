#pragma once

#include "common/types.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

namespace slonana {
namespace network {

/**
 * High-performance UDP packet batching manager
 * Provides efficient packet batching with sendmmsg/recvmmsg support
 * Target: >50K packets/sec throughput
 */
class UDPBatchManager {
public:
  struct Packet {
    std::vector<uint8_t> data;
    std::string destination_addr;
    uint16_t destination_port;
    uint64_t timestamp;
    uint8_t priority; // 0 = lowest, 255 = highest

    Packet() : destination_port(0), timestamp(0), priority(128) {}
  };

  struct BatchConfig {
    size_t max_batch_size;
    std::chrono::milliseconds batch_timeout;
    size_t buffer_pool_size;
    size_t max_packet_size;
    bool enable_zero_copy;
    bool enable_priority_queue;
    
    BatchConfig()
        : max_batch_size(64),
          batch_timeout(5),
          buffer_pool_size(1024),
          max_packet_size(1500),
          enable_zero_copy(false),
          enable_priority_queue(true) {}
  };

  struct BatchStats {
    std::atomic<uint64_t> packets_sent{0};
    std::atomic<uint64_t> packets_received{0};
    std::atomic<uint64_t> batches_sent{0};
    std::atomic<uint64_t> batches_received{0};
    std::atomic<uint64_t> total_bytes_sent{0};
    std::atomic<uint64_t> total_bytes_received{0};
    std::atomic<uint64_t> dropped_packets{0};
    std::atomic<uint64_t> queue_full_errors{0};
    
    // Delete copy constructor and assignment to prevent atomic copying
    BatchStats() = default;
    BatchStats(const BatchStats&) = delete;
    BatchStats& operator=(const BatchStats&) = delete;
    
    double get_avg_packets_per_batch() const {
      auto batches = batches_sent.load();
      return batches > 0 ? static_cast<double>(packets_sent.load()) / batches : 0.0;
    }
    
    uint64_t get_throughput_pps() const {
      return packets_sent.load(); // Simplified - actual would track time
    }
  };

  explicit UDPBatchManager(const BatchConfig& config = BatchConfig());
  ~UDPBatchManager();

  // Lifecycle
  bool initialize(int socket_fd);
  void shutdown();
  bool is_running() const { return running_.load(); }

  // Packet operations
  bool queue_packet(const Packet& packet);
  bool queue_packet(std::vector<uint8_t>&& data, const std::string& addr, uint16_t port, uint8_t priority = 128);
  std::vector<Packet> receive_batch(size_t max_packets = 64);
  void flush_batches(); // Force send pending batches

  // Statistics
  const BatchStats& get_stats() const { return stats_; }
  void reset_stats();

  // Configuration
  void set_batch_size(size_t size);
  void set_batch_timeout(std::chrono::milliseconds timeout);

private:
  BatchConfig config_;
  int socket_fd_;
  std::atomic<bool> running_;
  BatchStats stats_;

  // Packet queues (priority-based)
  struct PacketQueue {
    std::queue<Packet> high_priority;
    std::queue<Packet> normal_priority;
    std::queue<Packet> low_priority;
    std::mutex mutex;
    std::condition_variable cv;
    size_t total_size() const {
      return high_priority.size() + normal_priority.size() + low_priority.size();
    }
  };
  PacketQueue send_queue_;

  // Memory pool for zero-copy
  std::vector<std::vector<uint8_t>> buffer_pool_;
  std::queue<size_t> available_buffers_;
  std::mutex buffer_pool_mutex_;

  // Batch processing
  std::thread batch_sender_thread_;
  std::thread batch_receiver_thread_;
  std::atomic<bool> should_stop_;

  void batch_sender_loop();
  void batch_receiver_loop();
  bool send_batch_mmsg(const std::vector<Packet>& packets);
  bool send_batch_fallback(const std::vector<Packet>& packets);
  std::vector<Packet> receive_batch_mmsg(size_t max_packets);
  std::vector<Packet> receive_batch_fallback(size_t max_packets);

  // Memory management
  std::vector<uint8_t>* acquire_buffer();
  void release_buffer(std::vector<uint8_t>* buffer);
  void initialize_buffer_pool();

  // Helper methods
  void enqueue_by_priority(Packet&& packet);
  Packet dequeue_by_priority();
  bool has_packets_to_send() const;
};

} // namespace network
} // namespace slonana
