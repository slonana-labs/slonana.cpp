#pragma once

#include "common/types.h"
#include "security/secure_messaging.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <openssl/ssl.h>
#include <queue>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unordered_map>
#include <vector>

namespace slonana {
namespace network {

/**
 * QUIC Stream represents a multiplexed stream within a QUIC connection
 */
class QuicStream {
public:
  using StreamId = uint64_t;

  QuicStream(StreamId id);
  ~QuicStream();

  // Stream operations
  bool send_data(const std::vector<uint8_t> &data);
  std::vector<uint8_t> receive_data();
  bool close();
  bool is_closed() const;

  // Stream properties
  StreamId get_id() const { return stream_id_; }
  size_t bytes_sent() const { return bytes_sent_; }
  size_t bytes_received() const { return bytes_received_; }

  // Static helper for QUIC protocol
  static void encode_varint_static(std::vector<uint8_t> &buffer,
                                   uint64_t value);

private:
  StreamId stream_id_;
  bool closed_;
  size_t bytes_sent_;
  size_t bytes_received_;
  std::queue<std::vector<uint8_t>> pending_data_;
  std::vector<uint8_t> outbound_buffer_;
  mutable std::mutex mutex_;

  // Helper methods
  void encode_varint(std::vector<uint8_t> &buffer, uint64_t value);
};

/**
 * QUIC Connection manages a single QUIC connection with stream multiplexing
 */
class QuicConnection {
public:
  using ConnectionId = std::string;

  QuicConnection(const std::string &remote_address, uint16_t remote_port);
  ~QuicConnection();

  // Connection lifecycle
  bool connect();
  bool disconnect();
  bool is_connected() const;

  // Stream management
  std::shared_ptr<QuicStream> create_stream();
  std::shared_ptr<QuicStream> get_stream(QuicStream::StreamId stream_id);
  void close_stream(QuicStream::StreamId stream_id);

  // Connection properties
  const std::string &get_remote_address() const { return remote_address_; }
  uint16_t get_remote_port() const { return remote_port_; }
  ConnectionId get_connection_id() const { return connection_id_; }

  // Statistics
  size_t get_stream_count() const;
  std::chrono::milliseconds get_rtt() const { return rtt_; }

private:
  std::string remote_address_;
  uint16_t remote_port_;
  ConnectionId connection_id_;
  bool connected_;
  QuicStream::StreamId next_stream_id_;
  std::chrono::milliseconds rtt_;

  std::unordered_map<QuicStream::StreamId, std::shared_ptr<QuicStream>>
      streams_;
  mutable std::mutex streams_mutex_;

  // Socket and TLS
  int socket_fd_;
  struct sockaddr_in server_addr_;
  SSL_CTX *tls_ctx_;
  SSL *tls_ssl_;

  // Internal connection management
  void handle_connection_events();
  bool perform_quic_handshake();
  void process_quic_packet(const uint8_t *data, size_t length);
  void send_pending_packets();
  void handle_connection_maintenance();
  void cleanup_tls();
  
  // Refactored handshake helper methods - eliminates duplicate code
  std::vector<uint8_t> create_initial_packet();
  bool send_initial_packet(const std::vector<uint8_t>& initial_packet);
  bool wait_for_handshake_response();
  std::thread event_thread_;
  bool should_stop_;
};

/**
 * QUIC Client provides high-performance UDP-based communications with TLS
 * encryption Compatible with Agave's QUIC implementation for validator
 * communications
 */
class QuicClient {
public:
  using DataCallback =
      std::function<void(const std::vector<uint8_t> &, const std::string &)>;
  using ErrorCallback = std::function<void(const std::string &)>;

  explicit QuicClient(const slonana::common::ValidatorConfig& config = {});
  ~QuicClient();

  // Client lifecycle
  bool initialize();
  bool shutdown();

  // Connection management
  std::shared_ptr<QuicConnection> connect(const std::string &address,
                                          uint16_t port);
  bool disconnect(const std::string &connection_id);
  std::shared_ptr<QuicConnection>
  get_connection(const std::string &connection_id);

  // Secure data transmission
  bool send_secure_data(const std::string &connection_id,
                       QuicStream::StreamId stream_id,
                       const std::vector<uint8_t> &data,
                       const std::string& message_type = "quic_data");
  
  // Traditional data transmission (for backward compatibility)
  bool send_data(const std::string &connection_id,
                 QuicStream::StreamId stream_id,
                 const std::vector<uint8_t> &data);

  // Callback registration
  void set_data_callback(DataCallback callback) { data_callback_ = callback; }
  void set_error_callback(ErrorCallback callback) {
    error_callback_ = callback;
  }

  // Connection pooling
  void enable_connection_pooling(bool enabled) {
    connection_pooling_enabled_ = enabled;
  }
  void set_max_connections(size_t max_connections) {
    max_connections_ = max_connections;
  }

  // Statistics
  size_t get_connection_count() const;
  size_t get_total_bytes_sent() const { return total_bytes_sent_; }
  size_t get_total_bytes_received() const { return total_bytes_received_; }
  
  // Security statistics
  slonana::security::SecureMessaging::SecurityStats get_security_stats() const;

  // Performance tuning
  void set_send_buffer_size(size_t size) { send_buffer_size_ = size; }
  void set_receive_buffer_size(size_t size) { receive_buffer_size_ = size; }

private:
  bool initialized_;
  slonana::common::ValidatorConfig config_;
  std::unique_ptr<slonana::security::SecureMessaging> secure_messaging_;
  DataCallback data_callback_;
  ErrorCallback error_callback_;

  // Connection pool
  std::unordered_map<std::string, std::shared_ptr<QuicConnection>> connections_;
  mutable std::mutex connections_mutex_;
  bool connection_pooling_enabled_;
  size_t max_connections_;

  // Statistics
  std::atomic<size_t> total_bytes_sent_;
  std::atomic<size_t> total_bytes_received_;

  // Buffer configuration
  size_t send_buffer_size_;
  size_t receive_buffer_size_;

  // Event processing
  void process_events();
  std::thread event_processor_;
  bool should_stop_;
  std::condition_variable stop_cv_;
  std::mutex stop_mutex_;

  // Connection pool management
  void cleanup_expired_connections();
  std::string generate_connection_id(const std::string &address, uint16_t port);

  // Production QUIC methods
  void update_congestion_control();
  void handle_retransmissions();
  void update_connection_stats();
  
  // Secure messaging helpers
  bool setup_secure_messaging();
};

} // namespace network
} // namespace slonana