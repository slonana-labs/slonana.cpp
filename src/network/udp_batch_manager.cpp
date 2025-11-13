#include "network/udp_batch_manager.h"
#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// Advanced optimizations
#include <x86intrin.h> // For SIMD and prefetch

#ifdef __linux__
#include <pthread.h>
#ifndef MSG_WAITFORONE
#define MSG_WAITFORONE 0x10000
#endif
#endif

namespace slonana {
namespace network {

// Cache-line aligned atomics to prevent false sharing
struct alignas(64) AlignedAtomic {
  std::atomic<bool> value{false};
};

UDPBatchManager::UDPBatchManager(const BatchConfig& config)
    : config_(config), socket_fd_(-1), running_(false), should_stop_(false) {
  // Ensure proper alignment for performance
  static_assert(sizeof(std::atomic<bool>) <= 64, "Atomic too large for cache line");
}

UDPBatchManager::~UDPBatchManager() {
  shutdown();
}

bool UDPBatchManager::initialize(int socket_fd) {
  if (running_.load()) {
    return false;
  }

  socket_fd_ = socket_fd;
  running_.store(true);
  should_stop_.store(false);

  // Initialize buffer pool for zero-copy optimization
  if (config_.enable_zero_copy) {
    initialize_buffer_pool();
  }

  // Start batch processing threads
  batch_sender_thread_ = std::thread(&UDPBatchManager::batch_sender_loop, this);
  batch_receiver_thread_ = std::thread(&UDPBatchManager::batch_receiver_loop, this);

#ifdef __linux__
  // Pin threads to specific CPU cores for better cache locality
  cpu_set_t cpuset;
  
  // Pin sender thread to core 0
  CPU_ZERO(&cpuset);
  CPU_SET(0, &cpuset);
  pthread_setaffinity_np(batch_sender_thread_.native_handle(), sizeof(cpu_set_t), &cpuset);
  
  // Pin receiver thread to core 1
  CPU_ZERO(&cpuset);
  CPU_SET(1, &cpuset);
  pthread_setaffinity_np(batch_receiver_thread_.native_handle(), sizeof(cpu_set_t), &cpuset);
#endif

  return true;
}

void UDPBatchManager::shutdown() {
  if (!running_.load()) {
    return;
  }

  should_stop_.store(true);
  running_.store(false);

  // Signal threads with atomic flag (no mutex needed)
  send_queue_.has_data.store(true, std::memory_order_release);

  // Join threads
  if (batch_sender_thread_.joinable()) {
    batch_sender_thread_.join();
  }
  if (batch_receiver_thread_.joinable()) {
    batch_receiver_thread_.join();
  }
}

bool UDPBatchManager::queue_packet(const Packet& packet) {
  // Branch prediction: running check is likely true
  if (__builtin_expect(!running_.load(std::memory_order_relaxed), 0)) {
    return false;
  }
  
  // Lock-free check of queue size (approximate, but fast)
  if (__builtin_expect(send_queue_.total_size() >= config_.buffer_pool_size, 0)) {
    stats_.queue_full_errors.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  // Lock-free enqueue
  enqueue_by_priority(Packet(packet));
  send_queue_.has_data.store(true, std::memory_order_release);
  return true;
}

bool UDPBatchManager::queue_packet(std::vector<uint8_t>&& data, 
                                   const std::string& addr, 
                                   uint16_t port, 
                                   uint8_t priority) {
  // Branch prediction: running check is likely true
  if (__builtin_expect(!running_.load(std::memory_order_relaxed), 0)) {
    return false;
  }

  // Lock-free check of queue size (approximate, but fast)
  if (__builtin_expect(send_queue_.total_size() >= config_.buffer_pool_size, 0)) {
    stats_.queue_full_errors.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  Packet packet;
  packet.data = std::move(data);
  packet.destination_addr = addr;
  packet.destination_port = port;
  packet.priority = priority;
  packet.timestamp = std::chrono::system_clock::now().time_since_epoch().count();

  // Prefetch queue data structures
  __builtin_prefetch(&send_queue_.high_priority, 1, 3);
  __builtin_prefetch(&send_queue_.normal_priority, 1, 3);
  
  // Lock-free enqueue
  enqueue_by_priority(std::move(packet));
  send_queue_.has_data.store(true, std::memory_order_release);
  return true;
}

void UDPBatchManager::enqueue_by_priority(Packet&& packet) {
  if (__builtin_expect(config_.enable_priority_queue, 1)) {
    // Use computed goto-like optimization with branch hints
    // Most packets are normal priority (64-191)
    if (__builtin_expect(packet.priority >= 64, 1)) {
      if (__builtin_expect(packet.priority >= 192, 0)) {
        send_queue_.high_priority.push(std::move(packet));
      } else {
        send_queue_.normal_priority.push(std::move(packet));
      }
    } else {
      send_queue_.low_priority.push(std::move(packet));
    }
  } else {
    send_queue_.normal_priority.push(std::move(packet));
  }
}

UDPBatchManager::Packet UDPBatchManager::dequeue_by_priority() {
  Packet packet;
  
  // High priority first (lock-free)
  if (send_queue_.high_priority.try_pop(packet)) {
    return packet;
  }
  // Then normal priority (lock-free)
  if (send_queue_.normal_priority.try_pop(packet)) {
    return packet;
  }
  // Finally low priority (lock-free)
  if (send_queue_.low_priority.try_pop(packet)) {
    return packet;
  }
  
  return Packet();
}

bool UDPBatchManager::has_packets_to_send() const {
  return !send_queue_.high_priority.empty() || 
         !send_queue_.normal_priority.empty() || 
         !send_queue_.low_priority.empty();
}

void UDPBatchManager::batch_sender_loop() {
  // Pre-allocate batch vector once to avoid repeated allocations
  std::vector<Packet> batch;
  batch.reserve(config_.max_batch_size);
  
  while (__builtin_expect(!should_stop_.load(std::memory_order_relaxed), 1)) {
    batch.clear(); // Reuse the vector

    // Lock-free polling with adaptive sleep
    if (!has_packets_to_send()) {
      // Wait for signal or timeout (lock-free)
      auto start = std::chrono::steady_clock::now();
      while (!send_queue_.has_data.load(std::memory_order_acquire) && 
             !should_stop_.load(std::memory_order_relaxed)) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= config_.batch_timeout) break;
        // Adaptive spin-then-sleep
        if (elapsed < std::chrono::microseconds(10)) {
          _mm_pause(); // CPU hint for spin-wait
        } else {
          std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
      }
      send_queue_.has_data.store(false, std::memory_order_relaxed);
    }

    if (__builtin_expect(should_stop_.load(std::memory_order_relaxed), 0)) {
      break;
    }

    // Collect batch with lock-free dequeue
    // Unroll inner loop for better performance
    size_t batch_size = config_.max_batch_size;
    while (batch.size() < batch_size && __builtin_expect(has_packets_to_send(), 1)) {
      auto packet = dequeue_by_priority();
      if (packet.data.empty()) break; // No more packets
      batch.push_back(std::move(packet));
    }

    // Send batch if we have packets
    if (__builtin_expect(!batch.empty(), 1)) {
#ifdef __linux__
      if (!send_batch_mmsg(batch)) {
        send_batch_fallback(batch);
      }
#else
      send_batch_fallback(batch);
#endif
      stats_.batches_sent.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

void UDPBatchManager::batch_receiver_loop() {
  while (!should_stop_.load()) {
    std::vector<Packet> batch;
    
#ifdef __linux__
    batch = receive_batch_mmsg(config_.max_batch_size);
#else
    batch = receive_batch_fallback(config_.max_batch_size);
#endif

    if (!batch.empty()) {
      stats_.batches_received++;
      stats_.packets_received += batch.size();
      for (const auto& pkt : batch) {
        stats_.total_bytes_received += pkt.data.size();
      }
    }
    
    // Small sleep to avoid busy-waiting
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
}

#ifdef __linux__
bool UDPBatchManager::send_batch_mmsg(const std::vector<Packet>& packets) {
  if (__builtin_expect(packets.empty() || socket_fd_ < 0, 0)) {
    return false;
  }

  const size_t batch_size = packets.size();
  
  // Thread-local pre-allocated buffers (reused across calls, no allocation overhead)
  thread_local std::vector<struct mmsghdr> msgs;
  thread_local std::vector<struct iovec> iovecs;
  thread_local std::vector<struct sockaddr_in> addrs;
  
  // Resize once if needed (no reallocation on subsequent calls)
  if (msgs.capacity() < batch_size) {
    msgs.reserve(batch_size * 2); // Reserve extra to avoid frequent resizes
    iovecs.reserve(batch_size * 2);
    addrs.reserve(batch_size * 2);
  }
  msgs.resize(batch_size);
  iovecs.resize(batch_size);
  addrs.resize(batch_size);

  // Prefetch first packet data to reduce cache misses
  __builtin_prefetch(&packets[0], 0, 3);
  
  // Setup batch - optimized loop with prefetching
  for (size_t i = 0; i < batch_size; ++i) {
    // Prefetch next packet while processing current
    if (__builtin_expect(i + 1 < batch_size, 1)) {
      __builtin_prefetch(&packets[i + 1], 0, 3);
    }
    
    const auto& packet = packets[i];
    auto& addr = addrs[i];
    auto& iovec = iovecs[i];
    auto& msg = msgs[i];
    
    // Setup address - inline and optimized
    addr.sin_family = AF_INET;
    addr.sin_port = htons(packet.destination_port);
    inet_pton(AF_INET, packet.destination_addr.c_str(), &addr.sin_addr);

    // Setup iovec
    iovec.iov_base = const_cast<uint8_t*>(packet.data.data());
    iovec.iov_len = packet.data.size();

    // Setup mmsghdr - zero only necessary fields
    msg.msg_hdr.msg_name = &addr;
    msg.msg_hdr.msg_namelen = sizeof(struct sockaddr_in);
    msg.msg_hdr.msg_iov = &iovec;
    msg.msg_hdr.msg_iovlen = 1;
    msg.msg_hdr.msg_control = nullptr;
    msg.msg_hdr.msg_controllen = 0;
    msg.msg_hdr.msg_flags = 0;
    msg.msg_len = 0;
  }

  // Send batch using sendmmsg
  int sent = sendmmsg(socket_fd_, msgs.data(), batch_size, 0);
  if (__builtin_expect(sent < 0, 0)) {
    std::cerr << "sendmmsg failed: " << strerror(errno) << std::endl;
    return false;
  }

  // Update statistics with atomic operations
  stats_.packets_sent.fetch_add(sent, std::memory_order_relaxed);
  
  // Calculate total bytes sent
  size_t total_bytes = 0;
  for (int i = 0; i < sent; ++i) {
    total_bytes += msgs[i].msg_len;
  }
  stats_.total_bytes_sent.fetch_add(total_bytes, std::memory_order_relaxed);

  if (__builtin_expect(static_cast<size_t>(sent) < batch_size, 0)) {
    stats_.dropped_packets.fetch_add(batch_size - sent, std::memory_order_relaxed);
  }

  return true;
}

std::vector<UDPBatchManager::Packet> UDPBatchManager::receive_batch_mmsg(size_t max_packets) {
  std::vector<Packet> batch;
  if (socket_fd_ < 0) {
    return batch;
  }

  // Prepare mmsghdr structures
  std::vector<struct mmsghdr> msgs(max_packets);
  std::vector<struct iovec> iovecs(max_packets);
  std::vector<struct sockaddr_in> addrs(max_packets);
  std::vector<std::vector<uint8_t>> buffers(max_packets);

  for (size_t i = 0; i < max_packets; ++i) {
    buffers[i].resize(config_.max_packet_size);
    
    // Setup iovec
    iovecs[i].iov_base = buffers[i].data();
    iovecs[i].iov_len = config_.max_packet_size;

    // Setup mmsghdr
    std::memset(&msgs[i], 0, sizeof(struct mmsghdr));
    msgs[i].msg_hdr.msg_name = &addrs[i];
    msgs[i].msg_hdr.msg_namelen = sizeof(struct sockaddr_in);
    msgs[i].msg_hdr.msg_iov = &iovecs[i];
    msgs[i].msg_hdr.msg_iovlen = 1;
  }

  // Receive batch using recvmmsg with timeout
  struct timespec timeout;
  timeout.tv_sec = 0;
  timeout.tv_nsec = 1000000; // 1ms timeout

  int received = recvmmsg(socket_fd_, msgs.data(), max_packets, MSG_DONTWAIT | MSG_WAITFORONE, &timeout);
  if (received < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      std::cerr << "recvmmsg failed: " << strerror(errno) << std::endl;
    }
    return batch;
  }

  // Process received packets
  batch.reserve(received);
  for (int i = 0; i < received; ++i) {
    Packet pkt;
    pkt.data.assign(buffers[i].begin(), buffers[i].begin() + msgs[i].msg_len);
    
    // Extract source address
    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addrs[i].sin_addr, addr_str, INET_ADDRSTRLEN);
    pkt.destination_addr = addr_str;
    pkt.destination_port = ntohs(addrs[i].sin_port);
    pkt.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    
    batch.push_back(std::move(pkt));
  }

  return batch;
}
#endif

bool UDPBatchManager::send_batch_fallback(const std::vector<Packet>& packets) {
  if (socket_fd_ < 0) {
    return false;
  }

  size_t sent_count = 0;
  for (const auto& packet : packets) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(packet.destination_port);
    inet_pton(AF_INET, packet.destination_addr.c_str(), &addr.sin_addr);

    ssize_t sent = sendto(socket_fd_, packet.data.data(), packet.data.size(), 
                          MSG_DONTWAIT, (struct sockaddr*)&addr, sizeof(addr));
    
    if (sent > 0) {
      sent_count++;
      stats_.packets_sent++;
      stats_.total_bytes_sent += sent;
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
      stats_.dropped_packets++;
    }
  }

  return sent_count > 0;
}

std::vector<UDPBatchManager::Packet> UDPBatchManager::receive_batch_fallback(size_t max_packets) {
  std::vector<Packet> batch;
  if (socket_fd_ < 0) {
    return batch;
  }

  batch.reserve(max_packets);
  
  for (size_t i = 0; i < max_packets; ++i) {
    Packet pkt;
    pkt.data.resize(config_.max_packet_size);
    
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    
    ssize_t received = recvfrom(socket_fd_, pkt.data.data(), pkt.data.size(),
                                MSG_DONTWAIT, (struct sockaddr*)&addr, &addr_len);
    
    if (received < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break; // No more data available
      }
      continue; // Skip errors
    }

    pkt.data.resize(received);
    
    // Extract source address
    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, addr_str, INET_ADDRSTRLEN);
    pkt.destination_addr = addr_str;
    pkt.destination_port = ntohs(addr.sin_port);
    pkt.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    
    batch.push_back(std::move(pkt));
  }

  return batch;
}

std::vector<UDPBatchManager::Packet> UDPBatchManager::receive_batch(size_t max_packets) {
#ifdef __linux__
  return receive_batch_mmsg(max_packets);
#else
  return receive_batch_fallback(max_packets);
#endif
}

void UDPBatchManager::flush_batches() {
  std::vector<Packet> batch;
  
  // Lock-free flush - drain all queues
  while (has_packets_to_send()) {
    auto packet = dequeue_by_priority();
    if (packet.data.empty()) break; // No more packets
    batch.push_back(std::move(packet));
  }

  if (!batch.empty()) {
#ifdef __linux__
    if (!send_batch_mmsg(batch)) {
      send_batch_fallback(batch);
    }
#else
    send_batch_fallback(batch);
#endif
    stats_.batches_sent.fetch_add(1, std::memory_order_relaxed);
  }
}

void UDPBatchManager::reset_stats() {
  stats_.packets_sent.store(0);
  stats_.packets_received.store(0);
  stats_.batches_sent.store(0);
  stats_.batches_received.store(0);
  stats_.total_bytes_sent.store(0);
  stats_.total_bytes_received.store(0);
  stats_.dropped_packets.store(0);
  stats_.queue_full_errors.store(0);
}

void UDPBatchManager::set_batch_size(size_t size) {
  config_.max_batch_size = size;
}

void UDPBatchManager::set_batch_timeout(std::chrono::milliseconds timeout) {
  config_.batch_timeout = timeout;
}

void UDPBatchManager::initialize_buffer_pool() {
  buffer_pool_.resize(config_.buffer_pool_size);
  for (size_t i = 0; i < config_.buffer_pool_size; ++i) {
    buffer_pool_[i].reserve(config_.max_packet_size);
    available_buffers_.push(i);
  }
}

std::vector<uint8_t>* UDPBatchManager::acquire_buffer() {
  std::lock_guard<std::mutex> lock(buffer_pool_mutex_);
  if (available_buffers_.empty()) {
    return nullptr;
  }
  
  size_t idx = available_buffers_.front();
  available_buffers_.pop();
  return &buffer_pool_[idx];
}

void UDPBatchManager::release_buffer(std::vector<uint8_t>* buffer) {
  std::lock_guard<std::mutex> lock(buffer_pool_mutex_);
  
  // Find buffer index
  for (size_t i = 0; i < buffer_pool_.size(); ++i) {
    if (&buffer_pool_[i] == buffer) {
      buffer->clear();
      available_buffers_.push(i);
      break;
    }
  }
}

} // namespace network
} // namespace slonana
