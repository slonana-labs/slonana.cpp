#pragma once

#include "common/types.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace slonana {
namespace network {

enum class SubscriptionType {
  ACCOUNT_CHANGE,
  SIGNATURE_STATUS,
  SLOT_CHANGE,
  BLOCK_CHANGE,
  PROGRAM_ACCOUNT,
  LOG_SUBSCRIPTION
};

struct SubscriptionRequest {
  SubscriptionType type;
  std::string pubkey;     // For account/signature subscriptions
  std::string program_id; // For program account subscriptions
  uint64_t subscription_id;
  std::string filters; // JSON filters for advanced subscriptions
};

struct SubscriptionMessage {
  uint64_t subscription_id;
  std::string method;
  std::string params;
  std::string result;
  int64_t timestamp;
};

class WebSocketConnection {
private:
  int socket_fd;
  std::string client_address;
  std::unordered_map<uint64_t, SubscriptionRequest> subscriptions;
  mutable std::mutex subscriptions_mutex;
  std::atomic<bool> is_connected;

public:
  WebSocketConnection(int fd, const std::string &addr);
  ~WebSocketConnection();

  bool send_message(const std::string &message);
  bool send_subscription_response(const SubscriptionMessage &msg);
  bool add_subscription(const SubscriptionRequest &request);
  bool remove_subscription(uint64_t subscription_id);
  std::vector<SubscriptionRequest> get_subscriptions() const;
  bool is_alive() const { return is_connected.load(); }
  void close();

  const std::string &get_address() const { return client_address; }
  int get_socket() const { return socket_fd; }

private:
  std::vector<uint8_t> create_websocket_frame(const std::string &payload);
};

class WebSocketServer {
private:
  std::string bind_address;
  int port;
  int server_socket;
  std::atomic<bool> running;
  std::thread server_thread;
  std::thread notification_thread;

  // Connection management
  std::vector<std::shared_ptr<WebSocketConnection>> connections;
  mutable std::mutex connections_mutex;

  // Subscription management
  std::unordered_map<std::string, std::vector<uint64_t>> account_subscriptions;
  std::unordered_map<std::string, std::vector<uint64_t>>
      signature_subscriptions;
  std::vector<uint64_t> slot_subscriptions;
  std::vector<uint64_t> block_subscriptions;
  std::mutex subscription_mutex;

  // Notification queue
  std::queue<SubscriptionMessage> notification_queue;
  mutable std::mutex queue_mutex;
  std::condition_variable queue_cv;

  // Statistics
  std::atomic<uint64_t> total_connections;
  std::atomic<uint64_t> active_subscriptions;
  std::atomic<uint64_t> messages_sent;

  void server_loop();
  void notification_loop();
  void handle_client_connection(int client_socket,
                                const std::string &client_addr);
  void handle_websocket_handshake(int client_socket);
  void handle_websocket_message(std::shared_ptr<WebSocketConnection> conn,
                                const std::string &message);
  void cleanup_disconnected_connections();

  // WebSocket protocol handlers
  std::string generate_websocket_key();
  std::string compute_websocket_accept(const std::string &key);
  bool parse_websocket_frame(const std::vector<uint8_t> &frame,
                             std::string &payload);
  std::vector<uint8_t> create_websocket_frame(const std::string &payload);

  // Subscription handlers
  void handle_account_subscribe(std::shared_ptr<WebSocketConnection> conn,
                                const std::string &request);
  void handle_signature_subscribe(std::shared_ptr<WebSocketConnection> conn,
                                  const std::string &request);
  void handle_slot_subscribe(std::shared_ptr<WebSocketConnection> conn,
                             const std::string &request);
  void handle_block_subscribe(std::shared_ptr<WebSocketConnection> conn,
                              const std::string &request);
  void handle_unsubscribe(std::shared_ptr<WebSocketConnection> conn,
                          const std::string &request);

public:
  WebSocketServer(const std::string &address = "127.0.0.1", int port = 8900);
  ~WebSocketServer();

  bool start();
  void stop();
  bool is_running() const { return running.load(); }

  // Public notification methods for validator components
  void notify_account_change(const std::string &pubkey,
                             const std::string &account_data);
  void notify_signature_status(const std::string &signature,
                               const std::string &status);
  void notify_slot_change(uint64_t slot, const std::string &parent_slot,
                          uint64_t root_slot);
  void notify_block_change(uint64_t slot, const std::string &block_hash,
                           const std::string &block_data);
  void notify_program_account_change(const std::string &program_id,
                                     const std::string &account_pubkey,
                                     const std::string &account_data);

  // Statistics and monitoring
  struct WebSocketStats {
    uint64_t total_connections;
    uint64_t active_connections;
    uint64_t active_subscriptions;
    uint64_t messages_sent;
    uint64_t uptime_seconds;
  };

  WebSocketStats get_stats() const;
  std::vector<std::string> get_connected_clients() const;
};

} // namespace network
} // namespace slonana