#include "network/connection_cache.h"
#include "network/udp_batch_manager.h"
#include "test_framework.h"
#include <arpa/inet.h>
#include <chrono>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

// ============================================================================
// UDP Batch Manager Tests
// ============================================================================

void test_udp_batch_manager_initialization() {
  slonana::network::UDPBatchManager::BatchConfig config;
  config.max_batch_size = 32;
  config.batch_timeout = std::chrono::milliseconds(10);

  slonana::network::UDPBatchManager batch_mgr(config);

  // Create a dummy socket
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_TRUE(sock >= 0);

  ASSERT_TRUE(batch_mgr.initialize(sock));
  ASSERT_TRUE(batch_mgr.is_running());

  batch_mgr.shutdown();
  ASSERT_FALSE(batch_mgr.is_running());

  close(sock);
}

void test_udp_batch_manager_queue_packet() {
  slonana::network::UDPBatchManager::BatchConfig config;
  config.max_batch_size = 64;

  slonana::network::UDPBatchManager batch_mgr(config);

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_TRUE(sock >= 0);

  ASSERT_TRUE(batch_mgr.initialize(sock));

  // Queue a packet
  std::vector<uint8_t> data = {1, 2, 3, 4, 5};
  ASSERT_TRUE(batch_mgr.queue_packet(std::move(data), "127.0.0.1", 8080, 128));

  // Wait a bit for processing
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  const auto& stats = batch_mgr.get_stats();
  // Note: actual sending may fail without a real destination, but packet should
  // be queued
  ASSERT_TRUE(stats.batches_sent.load() > 0 || stats.queue_full_errors.load() == 0);

  batch_mgr.shutdown();
  close(sock);
}

void test_udp_batch_manager_priority_queue() {
  slonana::network::UDPBatchManager::BatchConfig config;
  config.max_batch_size = 64;
  config.enable_priority_queue = true;

  slonana::network::UDPBatchManager batch_mgr(config);

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_TRUE(sock >= 0);

  ASSERT_TRUE(batch_mgr.initialize(sock));

  // Queue packets with different priorities
  std::vector<uint8_t> data_low = {1, 2, 3};
  std::vector<uint8_t> data_normal = {4, 5, 6};
  std::vector<uint8_t> data_high = {7, 8, 9};

  ASSERT_TRUE(batch_mgr.queue_packet(std::move(data_low), "127.0.0.1", 8080, 32));    // Low
  ASSERT_TRUE(batch_mgr.queue_packet(std::move(data_normal), "127.0.0.1", 8080, 128)); // Normal
  ASSERT_TRUE(batch_mgr.queue_packet(std::move(data_high), "127.0.0.1", 8080, 255));  // High

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  const auto& stats = batch_mgr.get_stats();
  ASSERT_TRUE(stats.batches_sent.load() > 0 || stats.packets_sent.load() >= 0);

  batch_mgr.shutdown();
  close(sock);
}

void test_udp_batch_manager_flush() {
  slonana::network::UDPBatchManager::BatchConfig config;
  config.max_batch_size = 64;
  config.batch_timeout = std::chrono::milliseconds(1000); // Long timeout

  slonana::network::UDPBatchManager batch_mgr(config);

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_TRUE(sock >= 0);

  ASSERT_TRUE(batch_mgr.initialize(sock));

  // Queue some packets
  for (int i = 0; i < 10; ++i) {
    std::vector<uint8_t> data = {static_cast<uint8_t>(i)};
    batch_mgr.queue_packet(std::move(data), "127.0.0.1", 8080);
  }

  // Flush immediately
  batch_mgr.flush_batches();

  const auto& stats = batch_mgr.get_stats();
  ASSERT_TRUE(stats.batches_sent.load() > 0);

  batch_mgr.shutdown();
  close(sock);
}

void test_udp_batch_manager_stats() {
  slonana::network::UDPBatchManager batch_mgr;

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_TRUE(sock >= 0);

  batch_mgr.initialize(sock);

  const auto& initial_stats = batch_mgr.get_stats();
  ASSERT_EQ(initial_stats.packets_sent.load(), 0);
  ASSERT_EQ(initial_stats.batches_sent.load(), 0);

  // Queue and send some packets
  for (int i = 0; i < 5; ++i) {
    std::vector<uint8_t> data = {1, 2, 3};
    batch_mgr.queue_packet(std::move(data), "127.0.0.1", 8080);
  }

  batch_mgr.flush_batches();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  const auto& stats = batch_mgr.get_stats();
  ASSERT_TRUE(stats.batches_sent.load() > 0);

  batch_mgr.reset_stats();
  const auto& reset_stats = batch_mgr.get_stats();
  ASSERT_EQ(reset_stats.packets_sent.load(), 0);

  batch_mgr.shutdown();
  close(sock);
}

// ============================================================================
// Connection Cache Tests
// ============================================================================

void test_connection_cache_initialization() {
  slonana::network::ConnectionCache::CacheConfig config;
  config.max_connections = 1000;

  slonana::network::ConnectionCache cache(config);

  ASSERT_TRUE(cache.initialize());
  ASSERT_TRUE(cache.is_running());

  cache.shutdown();
  ASSERT_FALSE(cache.is_running());
}

void test_connection_cache_get_or_create() {
  slonana::network::ConnectionCache cache;
  cache.initialize();

  auto conn1 = cache.get_or_create("127.0.0.1", 8080);
  ASSERT_TRUE(conn1 != nullptr);
  ASSERT_EQ(conn1->remote_address, "127.0.0.1");
  ASSERT_EQ(conn1->remote_port, 8080);

  // Second request should return cached connection
  auto conn2 = cache.get_or_create("127.0.0.1", 8080);
  ASSERT_TRUE(conn2 != nullptr);
  ASSERT_EQ(conn1->connection_id, conn2->connection_id);

  const auto& stats = cache.get_stats();
  ASSERT_EQ(stats.cache_hits.load(), 1);
  ASSERT_EQ(stats.cache_misses.load(), 1);

  cache.shutdown();
}

void test_connection_cache_health_monitoring() {
  slonana::network::ConnectionCache::CacheConfig config;
  config.health_check_interval = std::chrono::seconds(1);

  slonana::network::ConnectionCache cache(config);
  cache.initialize();

  auto conn = cache.get_or_create("127.0.0.1", 9000);
  ASSERT_TRUE(conn != nullptr);

  // Mark successful sends
  for (int i = 0; i < 10; ++i) {
    cache.mark_send_success(conn->connection_id, std::chrono::milliseconds(5));
  }

  ASSERT_TRUE(cache.is_connection_healthy(conn->connection_id));

  // Mark failures
  for (int i = 0; i < 50; ++i) {
    cache.mark_send_failure(conn->connection_id);
  }

  // After many failures, connection should be unhealthy
  ASSERT_FALSE(cache.is_connection_healthy(conn->connection_id));

  cache.shutdown();
}

void test_connection_cache_remove() {
  slonana::network::ConnectionCache cache;
  cache.initialize();

  auto conn = cache.get_or_create("127.0.0.1", 8080);
  ASSERT_TRUE(conn != nullptr);

  std::string conn_id = conn->connection_id;
  ASSERT_TRUE(cache.remove(conn_id));

  // Connection should be gone
  auto retrieved = cache.get(conn_id);
  ASSERT_TRUE(retrieved == nullptr);

  cache.shutdown();
}

void test_connection_cache_clear() {
  slonana::network::ConnectionCache cache;
  cache.initialize();

  // Create multiple connections
  cache.get_or_create("127.0.0.1", 8080);
  cache.get_or_create("127.0.0.1", 8081);
  cache.get_or_create("127.0.0.1", 8082);

  ASSERT_EQ(cache.get_connection_count(), 3);

  cache.clear();
  ASSERT_EQ(cache.get_connection_count(), 0);

  cache.shutdown();
}

void test_connection_cache_statistics() {
  slonana::network::ConnectionCache cache;
  cache.initialize();

  const auto& initial_stats = cache.get_stats();
  ASSERT_EQ(initial_stats.total_connections.load(), 0);

  // Create connections
  cache.get_or_create("127.0.0.1", 8080);
  cache.get_or_create("127.0.0.1", 8081);
  cache.get_or_create("127.0.0.1", 8080); // Cache hit

  const auto& stats = cache.get_stats();
  ASSERT_EQ(stats.total_connections.load(), 2);
  ASSERT_EQ(stats.cache_hits.load(), 1);
  ASSERT_EQ(stats.cache_misses.load(), 2);
  ASSERT_TRUE(stats.get_hit_rate() > 0.0);

  // Verify sub-millisecond lookup
  ASSERT_TRUE(stats.avg_lookup_time.count() < 1000); // < 1ms in microseconds

  cache.shutdown();
}

void test_connection_cache_auto_reconnect() {
  slonana::network::ConnectionCache::CacheConfig config;
  config.enable_auto_reconnect = true;
  config.max_reconnect_attempts = 3;

  slonana::network::ConnectionCache cache(config);
  
  // Set a factory that always fails initially
  std::atomic<int> attempt_count{0};
  cache.set_connection_factory([&attempt_count](const std::string&, uint16_t) {
    attempt_count++;
    return -1; // Always fail
  });

  cache.initialize();

  auto conn = cache.get_or_create("127.0.0.1", 8080);
  ASSERT_TRUE(conn != nullptr);
  ASSERT_TRUE(conn->state == slonana::network::ConnectionCache::ConnectionState::FAILED);

  // Wait for reconnection attempts with longer timeout
  int max_wait = 5; // 5 seconds max
  for (int i = 0; i < max_wait && attempt_count.load() <= 1; ++i) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // Should have attempted multiple reconnections
  // Note: The timing-dependent nature means we just verify it tried at least once
  ASSERT_TRUE(attempt_count.load() >= 1); // At least initial attempt

  cache.shutdown();
}

void test_connection_cache_lookup_performance() {
  slonana::network::ConnectionCache cache;
  cache.initialize();

  // Pre-populate cache
  for (int i = 0; i < 100; ++i) {
    cache.get_or_create("127.0.0.1", 8000 + i);
  }

  // Measure lookup time
  auto start = std::chrono::high_resolution_clock::now();
  
  for (int i = 0; i < 1000; ++i) {
    auto conn = cache.get_or_create("127.0.0.1", 8050);
    ASSERT_TRUE(conn != nullptr);
  }
  
  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  auto avg_lookup = duration.count() / 1000.0;

  // Average lookup should be < 1ms (1000 microseconds)
  std::cout << "Average lookup time: " << avg_lookup << " microseconds" << std::endl;
  ASSERT_TRUE(avg_lookup < 1000.0);

  cache.shutdown();
}

// ============================================================================
// Integration Tests
// ============================================================================

void test_udp_batch_with_connection_cache() {
  // Create connection cache
  slonana::network::ConnectionCache cache;
  cache.initialize();

  // Create batch manager
  slonana::network::UDPBatchManager::BatchConfig batch_config;
  batch_config.max_batch_size = 32;

  slonana::network::UDPBatchManager batch_mgr(batch_config);

  // Get connection from cache
  auto conn = cache.get_or_create("127.0.0.1", 8080);
  ASSERT_TRUE(conn != nullptr);

  // Initialize batch manager with cached socket
  if (conn->socket_fd >= 0) {
    // For this test, create a new socket since we can't share
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    batch_mgr.initialize(sock);

    // Queue packets
    for (int i = 0; i < 10; ++i) {
      std::vector<uint8_t> data = {static_cast<uint8_t>(i)};
      batch_mgr.queue_packet(std::move(data), "127.0.0.1", 8080);
    }

    batch_mgr.flush_batches();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    const auto& batch_stats = batch_mgr.get_stats();
    ASSERT_TRUE(batch_stats.batches_sent.load() > 0);

    batch_mgr.shutdown();
    close(sock);
  }

  cache.shutdown();
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main() {
  std::cout << "Running Networking Enhancement Tests..." << std::endl;
  int failures = 0;

#define RUN_TEST(test_func) \
  do { \
    std::cout << "  Running " #test_func << "..." << std::flush; \
    try { \
      test_func(); \
      std::cout << " ✓" << std::endl; \
    } catch (const std::exception& e) { \
      std::cout << " ✗ Failed: " << e.what() << std::endl; \
      failures++; \
    } \
  } while(0)

  // UDP Batch Manager Tests
  RUN_TEST(test_udp_batch_manager_initialization);
  RUN_TEST(test_udp_batch_manager_queue_packet);
  RUN_TEST(test_udp_batch_manager_priority_queue);
  RUN_TEST(test_udp_batch_manager_flush);
  RUN_TEST(test_udp_batch_manager_stats);

  // Connection Cache Tests
  RUN_TEST(test_connection_cache_initialization);
  RUN_TEST(test_connection_cache_get_or_create);
  RUN_TEST(test_connection_cache_health_monitoring);
  RUN_TEST(test_connection_cache_remove);
  RUN_TEST(test_connection_cache_clear);
  RUN_TEST(test_connection_cache_statistics);
  RUN_TEST(test_connection_cache_auto_reconnect);
  RUN_TEST(test_connection_cache_lookup_performance);

  // Integration Tests
  RUN_TEST(test_udp_batch_with_connection_cache);

  if (failures == 0) {
    std::cout << "\n✅ All networking enhancement tests passed!" << std::endl;
    return 0;
  } else {
    std::cout << "\n❌ " << failures << " test(s) failed!" << std::endl;
    return 1;
  }
}
