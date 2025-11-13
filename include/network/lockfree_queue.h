#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>

namespace slonana {
namespace network {

/**
 * Lock-free single-producer single-consumer queue
 * Optimized for high-throughput packet processing
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

  alignas(64) std::atomic<Node*> head_; // Producer writes here
  alignas(64) std::atomic<Node*> tail_; // Consumer reads here
  alignas(64) std::atomic<size_t> size_{0};

public:
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

  // Producer-side: push new element
  void push(T&& value) {
    Node* node = new Node(std::move(value));
    Node* prev_head = head_.exchange(node, std::memory_order_acq_rel);
    prev_head->next.store(node, std::memory_order_release);
    size_.fetch_add(1, std::memory_order_relaxed);
  }

  // Consumer-side: try to pop element
  bool try_pop(T& result) {
    Node* tail = tail_.load(std::memory_order_relaxed);
    Node* next = tail->next.load(std::memory_order_acquire);
    
    if (!next) {
      return false;
    }

    result = std::move(next->data);
    tail_.store(next, std::memory_order_release);
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
