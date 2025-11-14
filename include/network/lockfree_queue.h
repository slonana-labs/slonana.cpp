#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <new>

namespace slonana {
namespace network {

/**
 * Lock-free single-producer multi-consumer (SPMC) queue
 * Optimized for high-throughput packet processing with multiple consumer threads
 * Producer side is completely lock-free, consumer side uses lightweight mutex
 */
template<typename T>
class LockFreeQueue {
private:
  struct alignas(64) Node { // Cache-line aligned to prevent false sharing
    T data;
    std::atomic<Node*> next{nullptr};
    
    Node() = default;
    explicit Node(T&& value) : data(std::move(value)) {}
  };

  alignas(64) std::atomic<Node*> head_; // Producer writes here (lock-free)
  alignas(64) std::atomic<Node*> tail_; // Consumer reads here
  alignas(64) std::atomic<size_t> size_{0};
  alignas(64) mutable std::mutex consumer_mutex_; // For multiple consumers

public:
  std::atomic<bool> has_data{false}; // Signal flag for threads

  LockFreeQueue() {
    Node* dummy = new Node();
    head_.store(dummy, std::memory_order_relaxed);
    tail_.store(dummy, std::memory_order_relaxed);
  }

  ~LockFreeQueue() {
    while (auto node = tail_.load(std::memory_order_relaxed)) {
      auto next = node->next.load(std::memory_order_relaxed);
      delete node;
      if (!next) break;
      tail_.store(next, std::memory_order_relaxed);
    }
  }

  // Producer-side: push new element (completely lock-free)
  void push(T&& value) {
    Node* node = new Node(std::move(value));
    Node* prev_head = head_.exchange(node, std::memory_order_acq_rel);
    prev_head->next.store(node, std::memory_order_release);
    size_.fetch_add(1, std::memory_order_relaxed);
    has_data.store(true, std::memory_order_release);
  }

  // Consumer-side: try to pop element (thread-safe for multiple consumers)
  bool try_pop(T& result) {
    std::lock_guard<std::mutex> lock(consumer_mutex_);
    
    Node* tail = tail_.load(std::memory_order_relaxed);
    Node* next = tail->next.load(std::memory_order_acquire);
    
    if (!next) {
      return false; // Queue is empty
    }

    result = std::move(next->data);
    tail_.store(next, std::memory_order_relaxed);
    delete tail;
    size_.fetch_sub(1, std::memory_order_relaxed);
    return true;
  }

  bool empty() const {
    Node* tail = tail_.load(std::memory_order_relaxed);
    Node* next = tail->next.load(std::memory_order_acquire);
    return next == nullptr;
  }

  size_t size() const {
    return size_.load(std::memory_order_relaxed);
  }
};

} // namespace network
} // namespace slonana
