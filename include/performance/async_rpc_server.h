#pragma once

#include <vector>
#include <thread>
#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <unordered_map>
#include <chrono>

namespace slonana {
namespace performance {

struct RPCRequest {
    std::string method;
    std::string params;
    std::string id;
    std::chrono::steady_clock::time_point timestamp;
};

struct RPCResponse {
    std::string result;
    std::string error;
    std::string id;
};

class ConnectionPool {
private:
    std::vector<int> available_connections_;
    std::mutex mutex_;
    std::condition_variable cv_;
    static constexpr size_t POOL_SIZE = 100;

public:
    ConnectionPool();
    ~ConnectionPool();
    
    int acquire_connection();
    void release_connection(int connection_id);
    size_t available_count() const;
};

class ResponseCache {
private:
    struct CacheEntry {
        RPCResponse response;
        std::chrono::steady_clock::time_point timestamp;
    };
    
    std::unordered_map<std::string, CacheEntry> cache_;
    mutable std::mutex mutex_;
    std::chrono::milliseconds ttl_{5000}; // 5 second TTL
    
public:
    bool get(const std::string& key, RPCResponse& response);
    void put(const std::string& key, const RPCResponse& response);
    void cleanup_expired();
    size_t size() const;
};

template<typename T>
class ThreadSafeQueue {
private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;

public:
    void push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(item));
        cv_.notify_one();
    }
    
    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }
    
    std::vector<T> pop_batch(size_t max_size) {
        std::vector<T> batch;
        std::lock_guard<std::mutex> lock(mutex_);
        
        size_t count = std::min(max_size, queue_.size());
        batch.reserve(count);
        
        for (size_t i = 0; i < count; ++i) {
            batch.emplace_back(std::move(queue_.front()));
            queue_.pop();
        }
        
        return batch;
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
};

class AsyncRPCServer {
private:
    std::vector<std::thread> worker_threads_;
    ThreadSafeQueue<RPCRequest> request_queue_;
    std::unique_ptr<ConnectionPool> connection_pool_;
    std::unique_ptr<ResponseCache> response_cache_;
    
    std::atomic<bool> running_{false};
    size_t num_workers_;
    size_t batch_size_;
    
    // Performance metrics
    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> cache_hits_{0};
    std::atomic<uint64_t> cache_misses_{0};
    std::atomic<double> avg_latency_ms_{0.0};
    
public:
    explicit AsyncRPCServer(size_t num_workers = 8, size_t batch_size = 50);
    ~AsyncRPCServer();
    
    bool start();
    void stop();
    
    void submit_request(RPCRequest request);
    RPCResponse handle_request(const RPCRequest& request);
    
    // Performance monitoring
    struct Statistics {
        uint64_t total_requests;
        uint64_t cache_hits;
        uint64_t cache_misses;
        double cache_hit_rate;
        double avg_latency_ms;
        size_t queue_size;
        size_t active_connections;
    };
    
    Statistics get_statistics() const;
    void reset_statistics();
    
private:
    void worker_thread_function();
    void process_requests_async();
    std::string generate_cache_key(const RPCRequest& request) const;
};

} // namespace performance
} // namespace slonana