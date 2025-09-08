#pragma once

#include "common/types.h"
#include "network/quic_client.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <unordered_set>

namespace slonana {
namespace network {

/**
 * QUIC Server Listener handles incoming QUIC connections
 */
class QuicListener {
public:
    QuicListener(uint16_t port);
    ~QuicListener();
    
    bool start();
    bool stop();
    bool is_listening() const { return listening_; }
    uint16_t get_port() const { return port_; }
    
private:
    uint16_t port_;
    bool listening_;
    std::thread listener_thread_;
    
    void listen_loop();
};

/**
 * QUIC Server Session represents a server-side connection with a client
 */
class QuicServerSession {
public:
    using SessionId = std::string;
    
    QuicServerSession(const std::string& client_address, uint16_t client_port);
    ~QuicServerSession();
    
    // Session lifecycle
    bool accept_connection();
    bool close_connection();
    bool is_active() const { return active_; }
    
    // Stream handling
    std::shared_ptr<QuicStream> accept_stream();
    void handle_stream_data(QuicStream::StreamId stream_id, const std::vector<uint8_t>& data);
    
    // Session properties
    SessionId get_session_id() const { return session_id_; }
    const std::string& get_client_address() const { return client_address_; }
    uint16_t get_client_port() const { return client_port_; }
    
    // Statistics
    size_t get_active_streams() const;
    std::chrono::steady_clock::time_point get_last_activity() const { return last_activity_; }
    
private:
    SessionId session_id_;
    std::string client_address_;
    uint16_t client_port_;
    bool active_;
    std::chrono::steady_clock::time_point last_activity_;
    
    std::unordered_map<QuicStream::StreamId, std::shared_ptr<QuicStream>> streams_;
    mutable std::mutex streams_mutex_;
    
    QuicStream::StreamId next_stream_id_;
};

/**
 * QUIC Server provides high-performance server-side QUIC implementation
 * Compatible with Agave's QUIC protocol for validator networking
 */
class QuicServer {
public:
    using ConnectionCallback = std::function<void(std::shared_ptr<QuicServerSession>)>;
    using DataCallback = std::function<void(const std::string&, QuicStream::StreamId, const std::vector<uint8_t>&)>;
    using DisconnectionCallback = std::function<void(const std::string&)>;
    
    QuicServer();
    ~QuicServer();
    
    // Server lifecycle
    bool initialize(uint16_t port);
    bool start();
    bool stop();
    bool shutdown();
    bool is_running() const { return running_; }
    
    // Session management
    std::shared_ptr<QuicServerSession> get_session(const std::string& session_id);
    void close_session(const std::string& session_id);
    size_t get_active_session_count() const;
    
    // Data transmission
    bool send_data(const std::string& session_id,
                   QuicStream::StreamId stream_id,
                   const std::vector<uint8_t>& data);
    
    // Callback registration
    void set_connection_callback(ConnectionCallback callback) { connection_callback_ = callback; }
    void set_data_callback(DataCallback callback) { data_callback_ = callback; }
    void set_disconnection_callback(DisconnectionCallback callback) { disconnection_callback_ = callback; }
    
    // Configuration
    void set_max_sessions(size_t max_sessions) { max_sessions_ = max_sessions; }
    void set_session_timeout(std::chrono::milliseconds timeout) { session_timeout_ = timeout; }
    void set_max_streams_per_session(size_t max_streams) { max_streams_per_session_ = max_streams; }
    
    // TLS Configuration
    bool configure_tls(const std::string& cert_file, const std::string& key_file);
    void enable_tls_verification(bool enabled) { tls_verification_enabled_ = enabled; }
    
    // Performance monitoring
    size_t get_total_bytes_sent() const { return total_bytes_sent_; }
    size_t get_total_bytes_received() const { return total_bytes_received_; }
    size_t get_total_connections() const { return total_connections_; }
    double get_average_session_duration() const;
    
    // Statistics
    struct Statistics {
        size_t active_sessions;
        size_t total_sessions;
        size_t active_streams;
        size_t total_streams;
        size_t bytes_sent;
        size_t bytes_received;
        double avg_rtt_ms;
        std::chrono::milliseconds uptime;
    };
    
    Statistics get_statistics() const;
    
private:
    bool initialized_;
    bool running_;
    uint16_t port_;
    std::unique_ptr<QuicListener> listener_;
    
    // Callbacks
    ConnectionCallback connection_callback_;
    DataCallback data_callback_;
    DisconnectionCallback disconnection_callback_;
    
    // Session management
    std::unordered_map<std::string, std::shared_ptr<QuicServerSession>> sessions_;
    mutable std::mutex sessions_mutex_;
    size_t max_sessions_;
    std::chrono::milliseconds session_timeout_;
    size_t max_streams_per_session_;
    
    // TLS configuration
    std::string cert_file_;
    std::string key_file_;
    bool tls_verification_enabled_;
    
    // Statistics
    std::atomic<size_t> total_bytes_sent_;
    std::atomic<size_t> total_bytes_received_;
    std::atomic<size_t> total_connections_;
    std::chrono::steady_clock::time_point start_time_;
    
    // Event processing
    void process_server_events();
    std::thread event_processor_;
    bool should_stop_;
    std::condition_variable stop_cv_;
    std::mutex stop_mutex_;
    
    // Session lifecycle management
    void handle_new_connection(const std::string& client_address, uint16_t client_port);
    void handle_session_data(const std::string& session_id, QuicStream::StreamId stream_id,
                           const std::vector<uint8_t>& data);
    void handle_session_disconnection(const std::string& session_id);
    
    // Cleanup and maintenance
    void cleanup_expired_sessions();
    std::string generate_session_id(const std::string& client_address, uint16_t client_port);
    
    // Security and validation
    bool validate_client_certificate(const std::string& client_address);
    bool rate_limit_check(const std::string& client_address);
    
    // Connection tracking
    std::unordered_set<std::string> active_client_addresses_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_connection_time_;
    mutable std::mutex client_tracking_mutex_;
};

} // namespace network
} // namespace slonana