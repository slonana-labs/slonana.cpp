#include "network/udp_batch_manager.h"
#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/socket.h>
#ifndef MSG_WAITFORONE
#define MSG_WAITFORONE 0x10000
#endif
#endif

namespace slonana {
namespace network {

UDPBatchManager::UDPBatchManager(const BatchConfig& config)
    : config_(config), socket_fd_(-1), running_(false), should_stop_(false) {}

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

  return true;
}

void UDPBatchManager::shutdown() {
  if (!running_.load()) {
    return;
  }

  should_stop_.store(true);
  running_.store(false);

  // Wake up threads
  send_queue_.cv.notify_all();

  // Join threads
  if (batch_sender_thread_.joinable()) {
    batch_sender_thread_.join();
  }
  if (batch_receiver_thread_.joinable()) {
    batch_receiver_thread_.join();
  }
}

bool UDPBatchManager::queue_packet(const Packet& packet) {
  if (!running_.load()) {
    return false;
  }

  std::unique_lock<std::mutex> lock(send_queue_.mutex);
  
  // Check queue size
  if (send_queue_.total_size() >= config_.buffer_pool_size) {
    stats_.queue_full_errors++;
    return false;
  }

  enqueue_by_priority(Packet(packet));
  send_queue_.cv.notify_one();
  return true;
}

bool UDPBatchManager::queue_packet(std::vector<uint8_t>&& data, 
                                   const std::string& addr, 
                                   uint16_t port, 
                                   uint8_t priority) {
  if (!running_.load()) {
    return false;
  }

  Packet packet;
  packet.data = std::move(data);
  packet.destination_addr = addr;
  packet.destination_port = port;
  packet.priority = priority;
  packet.timestamp = std::chrono::system_clock::now().time_since_epoch().count();

  return queue_packet(packet);
}

void UDPBatchManager::enqueue_by_priority(Packet&& packet) {
  if (config_.enable_priority_queue) {
    if (packet.priority >= 192) {
      send_queue_.high_priority.push(std::move(packet));
    } else if (packet.priority >= 64) {
      send_queue_.normal_priority.push(std::move(packet));
    } else {
      send_queue_.low_priority.push(std::move(packet));
    }
  } else {
    send_queue_.normal_priority.push(std::move(packet));
  }
}

UDPBatchManager::Packet UDPBatchManager::dequeue_by_priority() {
  // High priority first
  if (!send_queue_.high_priority.empty()) {
    auto packet = std::move(send_queue_.high_priority.front());
    send_queue_.high_priority.pop();
    return packet;
  }
  // Then normal priority
  if (!send_queue_.normal_priority.empty()) {
    auto packet = std::move(send_queue_.normal_priority.front());
    send_queue_.normal_priority.pop();
    return packet;
  }
  // Finally low priority
  if (!send_queue_.low_priority.empty()) {
    auto packet = std::move(send_queue_.low_priority.front());
    send_queue_.low_priority.pop();
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
  while (!should_stop_.load()) {
    std::vector<Packet> batch;
    batch.reserve(config_.max_batch_size);

    {
      std::unique_lock<std::mutex> lock(send_queue_.mutex);
      
      // Wait for packets or timeout
      send_queue_.cv.wait_for(lock, config_.batch_timeout, 
                              [this]() { return has_packets_to_send() || should_stop_.load(); });

      if (should_stop_.load()) {
        break;
      }

      // Collect batch
      while (batch.size() < config_.max_batch_size && has_packets_to_send()) {
        batch.push_back(dequeue_by_priority());
      }
    }

    // Send batch if we have packets
    if (!batch.empty()) {
#ifdef __linux__
      if (!send_batch_mmsg(batch)) {
        send_batch_fallback(batch);
      }
#else
      send_batch_fallback(batch);
#endif
      stats_.batches_sent++;
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
  if (packets.empty() || socket_fd_ < 0) {
    return false;
  }

  // Prepare mmsghdr structures
  std::vector<struct mmsghdr> msgs(packets.size());
  std::vector<struct iovec> iovecs(packets.size());
  std::vector<struct sockaddr_in> addrs(packets.size());

  for (size_t i = 0; i < packets.size(); ++i) {
    // Setup address
    addrs[i].sin_family = AF_INET;
    addrs[i].sin_port = htons(packets[i].destination_port);
    inet_pton(AF_INET, packets[i].destination_addr.c_str(), &addrs[i].sin_addr);

    // Setup iovec
    iovecs[i].iov_base = const_cast<uint8_t*>(packets[i].data.data());
    iovecs[i].iov_len = packets[i].data.size();

    // Setup mmsghdr
    std::memset(&msgs[i], 0, sizeof(struct mmsghdr));
    msgs[i].msg_hdr.msg_name = &addrs[i];
    msgs[i].msg_hdr.msg_namelen = sizeof(struct sockaddr_in);
    msgs[i].msg_hdr.msg_iov = &iovecs[i];
    msgs[i].msg_hdr.msg_iovlen = 1;
  }

  // Send batch using sendmmsg
  int sent = sendmmsg(socket_fd_, msgs.data(), packets.size(), 0);
  if (sent < 0) {
    std::cerr << "sendmmsg failed: " << strerror(errno) << std::endl;
    return false;
  }

  // Update statistics
  stats_.packets_sent += sent;
  for (int i = 0; i < sent; ++i) {
    stats_.total_bytes_sent += msgs[i].msg_len;
  }

  if (static_cast<size_t>(sent) < packets.size()) {
    stats_.dropped_packets += (packets.size() - sent);
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
  
  {
    std::unique_lock<std::mutex> lock(send_queue_.mutex);
    while (has_packets_to_send()) {
      batch.push_back(dequeue_by_priority());
    }
  }

  if (!batch.empty()) {
#ifdef __linux__
    if (!send_batch_mmsg(batch)) {
      send_batch_fallback(batch);
    }
#else
    send_batch_fallback(batch);
#endif
    stats_.batches_sent++;
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
