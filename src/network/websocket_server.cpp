#include "network/websocket_server.h"
#include <iostream>
#include <sstream>
#include <regex>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <poll.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

namespace slonana {
namespace network {

// WebSocket Connection Implementation
WebSocketConnection::WebSocketConnection(int fd, const std::string& addr) 
    : socket_fd(fd), client_address(addr), is_connected(true) {
}

WebSocketConnection::~WebSocketConnection() {
    close();
}

bool WebSocketConnection::send_message(const std::string& message) {
    if (!is_connected.load()) return false;
    
    auto frame = create_websocket_frame(message);
    ssize_t sent = send(socket_fd, frame.data(), frame.size(), MSG_NOSIGNAL);
    
    if (sent != static_cast<ssize_t>(frame.size())) {
        is_connected.store(false);
        return false;
    }
    
    return true;
}

bool WebSocketConnection::send_subscription_response(const SubscriptionMessage& msg) {
    std::ostringstream response;
    response << "{"
             << "\"jsonrpc\":\"2.0\","
             << "\"method\":\"" << msg.method << "\","
             << "\"params\":{"
             << "\"subscription\":" << msg.subscription_id << ","
             << "\"result\":" << msg.result
             << "}}";
    
    return send_message(response.str());
}

bool WebSocketConnection::add_subscription(const SubscriptionRequest& request) {
    std::lock_guard<std::mutex> lock(subscriptions_mutex);
    subscriptions[request.subscription_id] = request;
    return true;
}

bool WebSocketConnection::remove_subscription(uint64_t subscription_id) {
    std::lock_guard<std::mutex> lock(subscriptions_mutex);
    return subscriptions.erase(subscription_id) > 0;
}

std::vector<SubscriptionRequest> WebSocketConnection::get_subscriptions() const {
    std::lock_guard<std::mutex> lock(subscriptions_mutex);
    std::vector<SubscriptionRequest> result;
    for (const auto& pair : subscriptions) {
        result.push_back(pair.second);
    }
    return result;
}

void WebSocketConnection::close() {
    if (is_connected.exchange(false)) {
        ::close(socket_fd);
    }
}

std::vector<uint8_t> WebSocketConnection::create_websocket_frame(const std::string& payload) {
    std::vector<uint8_t> frame;
    
    // First byte: FIN=1, RSV=000, Opcode=0001 (text frame)
    frame.push_back(0x81);
    
    // Payload length
    if (payload.length() < 126) {
        frame.push_back(static_cast<uint8_t>(payload.length()));
    } else if (payload.length() <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>(payload.length() >> 8));
        frame.push_back(static_cast<uint8_t>(payload.length() & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((payload.length() >> (i * 8)) & 0xFF));
        }
    }
    
    // Payload data
    frame.insert(frame.end(), payload.begin(), payload.end());
    
    return frame;
}

// WebSocket Server Implementation
WebSocketServer::WebSocketServer(const std::string& address, int port)
    : bind_address(address), port(port), server_socket(-1), running(false),
      total_connections(0), active_subscriptions(0), messages_sent(0) {
}

WebSocketServer::~WebSocketServer() {
    stop();
}

bool WebSocketServer::start() {
    if (running.load()) return false;
    
    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        std::cerr << "Failed to create WebSocket server socket" << std::endl;
        return false;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set WebSocket socket options" << std::endl;
        ::close(server_socket);
        return false;
    }
    
    // Bind socket
    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, bind_address.c_str(), &server_addr.sin_addr);
    
    if (bind(server_socket, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        std::cerr << "Failed to bind WebSocket server to " << bind_address << ":" << port << std::endl;
        ::close(server_socket);
        return false;
    }
    
    // Listen
    if (listen(server_socket, 10) < 0) {
        std::cerr << "Failed to listen on WebSocket server" << std::endl;
        ::close(server_socket);
        return false;
    }
    
    running.store(true);
    
    // Start server threads
    server_thread = std::thread(&WebSocketServer::server_loop, this);
    notification_thread = std::thread(&WebSocketServer::notification_loop, this);
    
    std::cout << "WebSocket server started on " << bind_address << ":" << port << std::endl;
    return true;
}

void WebSocketServer::stop() {
    if (!running.exchange(false)) return;
    
    // Close server socket
    if (server_socket >= 0) {
        ::close(server_socket);
        server_socket = -1;
    }
    
    // Wake up notification thread
    queue_cv.notify_all();
    
    // Wait for threads to finish
    if (server_thread.joinable()) {
        server_thread.join();
    }
    if (notification_thread.joinable()) {
        notification_thread.join();
    }
    
    // Close all connections
    std::lock_guard<std::mutex> lock(connections_mutex);
    for (auto& conn : connections) {
        conn->close();
    }
    connections.clear();
    
    std::cout << "WebSocket server stopped" << std::endl;
}

void WebSocketServer::server_loop() {
    while (running.load()) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
        if (client_socket < 0) {
            if (running.load()) {
                std::cerr << "WebSocket accept failed: " << strerror(errno) << std::endl;
            }
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::string client_address = std::string(client_ip) + ":" + std::to_string(ntohs(client_addr.sin_port));
        
        total_connections.fetch_add(1);
        
        // Handle client in separate thread
        std::thread client_thread(&WebSocketServer::handle_client_connection, this, client_socket, client_address);
        client_thread.detach();
        
        // Cleanup disconnected connections periodically
        cleanup_disconnected_connections();
    }
}

void WebSocketServer::handle_client_connection(int client_socket, const std::string& client_addr) {
    // Handle WebSocket handshake
    handle_websocket_handshake(client_socket);
    
    // Create connection object
    auto connection = std::make_shared<WebSocketConnection>(client_socket, client_addr);
    
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        connections.push_back(connection);
    }
    
    std::cout << "WebSocket client connected: " << client_addr << std::endl;
    
    // Handle messages from client
    char buffer[4096];
    while (connection->is_alive() && running.load()) {
        ssize_t received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            break;
        }
        
        // Parse WebSocket frame and extract message
        std::vector<uint8_t> frame(buffer, buffer + received);
        std::string message;
        if (parse_websocket_frame(frame, message)) {
            handle_websocket_message(connection, message);
        }
    }
    
    connection->close();
    std::cout << "WebSocket client disconnected: " << client_addr << std::endl;
}

void WebSocketServer::handle_websocket_handshake(int client_socket) {
    char buffer[4096];
    ssize_t received = recv(client_socket, buffer, sizeof(buffer), 0);
    if (received <= 0) return;
    
    std::string request(buffer, received);
    
    // Extract WebSocket key
    std::regex key_regex(R"(Sec-WebSocket-Key:\s*(.+))");
    std::smatch match;
    std::string websocket_key;
    if (std::regex_search(request, match, key_regex)) {
        websocket_key = match[1].str();
        // Remove carriage return if present
        if (!websocket_key.empty() && websocket_key.back() == '\r') {
            websocket_key.pop_back();
        }
    }
    
    if (websocket_key.empty()) {
        ::close(client_socket);
        return;
    }
    
    // Generate accept key
    std::string accept_key = compute_websocket_accept(websocket_key);
    
    // Send handshake response
    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << accept_key << "\r\n"
             << "\r\n";
    
    send(client_socket, response.str().c_str(), response.str().length(), 0);
}

std::string WebSocketServer::compute_websocket_accept(const std::string& key) {
    const std::string websocket_magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = key + websocket_magic;
    
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(combined.c_str()), combined.length(), hash);
    
    // Base64 encode
    BIO* bio = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);
    
    BIO_write(bio, hash, SHA_DIGEST_LENGTH);
    BIO_flush(bio);
    
    BUF_MEM* buffer_ptr;
    BIO_get_mem_ptr(bio, &buffer_ptr);
    
    std::string result(buffer_ptr->data, buffer_ptr->length);
    BIO_free_all(bio);
    
    return result;
}

bool WebSocketServer::parse_websocket_frame(const std::vector<uint8_t>& frame, std::string& payload) {
    if (frame.size() < 2) return false;
    
    uint8_t opcode = frame[0] & 0x0F;
    bool masked = (frame[1] & 0x80) != 0;
    uint64_t payload_len = frame[1] & 0x7F;
    
    size_t header_size = 2;
    
    // Handle extended payload length
    if (payload_len == 126) {
        if (frame.size() < 4) return false;
        payload_len = (frame[2] << 8) | frame[3];
        header_size = 4;
    } else if (payload_len == 127) {
        if (frame.size() < 10) return false;
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | frame[2 + i];
        }
        header_size = 10;
    }
    
    // Handle masking key
    uint8_t mask[4] = {0};
    if (masked) {
        if (frame.size() < header_size + 4) return false;
        for (int i = 0; i < 4; i++) {
            mask[i] = frame[header_size + i];
        }
        header_size += 4;
    }
    
    // Extract payload
    if (frame.size() < header_size + payload_len) return false;
    
    payload.clear();
    payload.reserve(payload_len);
    
    for (uint64_t i = 0; i < payload_len; i++) {
        uint8_t byte = frame[header_size + i];
        if (masked) {
            byte ^= mask[i % 4];
        }
        payload.push_back(static_cast<char>(byte));
    }
    
    return opcode == 1; // Text frame
}

void WebSocketServer::handle_websocket_message(std::shared_ptr<WebSocketConnection> conn, const std::string& message) {
    // Parse JSON-RPC request
    std::regex method_regex("\"method\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch match;
    
    if (!std::regex_search(message, match, method_regex)) {
        return;
    }
    
    std::string method = match[1].str();
    
    // Handle subscription methods
    if (method == "accountSubscribe") {
        handle_account_subscribe(conn, message);
    } else if (method == "signatureSubscribe") {
        handle_signature_subscribe(conn, message);
    } else if (method == "slotSubscribe") {
        handle_slot_subscribe(conn, message);
    } else if (method == "blockSubscribe") {
        handle_block_subscribe(conn, message);
    } else if (method.find("Unsubscribe") != std::string::npos) {
        handle_unsubscribe(conn, message);
    }
}

void WebSocketServer::handle_account_subscribe(std::shared_ptr<WebSocketConnection> conn, const std::string& request) {
    // Extract pubkey from params
    std::regex pubkey_regex("\"params\"\\s*:\\s*\\[\\s*\"([^\"]+)\"");
    std::smatch match;
    
    if (!std::regex_search(request, match, pubkey_regex)) {
        return;
    }
    
    std::string pubkey = match[1].str();
    uint64_t subscription_id = active_subscriptions.fetch_add(1) + 1;
    
    SubscriptionRequest sub_request{
        SubscriptionType::ACCOUNT_CHANGE,
        pubkey,
        "",
        subscription_id,
        ""
    };
    
    conn->add_subscription(sub_request);
    
    {
        std::lock_guard<std::mutex> lock(subscription_mutex);
        account_subscriptions[pubkey].push_back(subscription_id);
    }
    
    // Send subscription confirmation
    std::ostringstream response;
    response << "{\"jsonrpc\":\"2.0\",\"result\":" << subscription_id << ",\"id\":1}";
    conn->send_message(response.str());
    
    std::cout << "WebSocket: Account subscription created for " << pubkey << " (ID: " << subscription_id << ")" << std::endl;
}

void WebSocketServer::handle_signature_subscribe(std::shared_ptr<WebSocketConnection> conn, const std::string& request) {
    std::regex sig_regex("\"params\"\\s*:\\s*\\[\\s*\"([^\"]+)\"");
    std::smatch match;
    
    if (!std::regex_search(request, match, sig_regex)) {
        return;
    }
    
    std::string signature = match[1].str();
    uint64_t subscription_id = active_subscriptions.fetch_add(1) + 1;
    
    SubscriptionRequest sub_request{
        SubscriptionType::SIGNATURE_STATUS,
        signature,
        "",
        subscription_id,
        ""
    };
    
    conn->add_subscription(sub_request);
    
    {
        std::lock_guard<std::mutex> lock(subscription_mutex);
        signature_subscriptions[signature].push_back(subscription_id);
    }
    
    std::ostringstream response;
    response << "{\"jsonrpc\":\"2.0\",\"result\":" << subscription_id << ",\"id\":1}";
    conn->send_message(response.str());
    
    std::cout << "WebSocket: Signature subscription created for " << signature << " (ID: " << subscription_id << ")" << std::endl;
}

void WebSocketServer::handle_slot_subscribe(std::shared_ptr<WebSocketConnection> conn, const std::string& request) {
    uint64_t subscription_id = active_subscriptions.fetch_add(1) + 1;
    
    SubscriptionRequest sub_request{
        SubscriptionType::SLOT_CHANGE,
        "",
        "",
        subscription_id,
        ""
    };
    
    conn->add_subscription(sub_request);
    
    {
        std::lock_guard<std::mutex> lock(subscription_mutex);
        slot_subscriptions.push_back(subscription_id);
    }
    
    std::ostringstream response;
    response << "{\"jsonrpc\":\"2.0\",\"result\":" << subscription_id << ",\"id\":1}";
    conn->send_message(response.str());
    
    std::cout << "WebSocket: Slot subscription created (ID: " << subscription_id << ")" << std::endl;
}

void WebSocketServer::handle_block_subscribe(std::shared_ptr<WebSocketConnection> conn, const std::string& request) {
    uint64_t subscription_id = active_subscriptions.fetch_add(1) + 1;
    
    SubscriptionRequest sub_request{
        SubscriptionType::BLOCK_CHANGE,
        "",
        "",
        subscription_id,
        ""
    };
    
    conn->add_subscription(sub_request);
    
    {
        std::lock_guard<std::mutex> lock(subscription_mutex);
        block_subscriptions.push_back(subscription_id);
    }
    
    std::ostringstream response;
    response << "{\"jsonrpc\":\"2.0\",\"result\":" << subscription_id << ",\"id\":1}";
    conn->send_message(response.str());
    
    std::cout << "WebSocket: Block subscription created (ID: " << subscription_id << ")" << std::endl;
}

void WebSocketServer::handle_unsubscribe(std::shared_ptr<WebSocketConnection> conn, const std::string& request) {
    std::regex id_regex("\"params\"\\s*:\\s*\\[\\s*(\\d+)");
    std::smatch match;
    
    if (!std::regex_search(request, match, id_regex)) {
        return;
    }
    
    uint64_t subscription_id = std::stoull(match[1].str());
    
    if (conn->remove_subscription(subscription_id)) {
        std::ostringstream response;
        response << "{\"jsonrpc\":\"2.0\",\"result\":true,\"id\":1}";
        conn->send_message(response.str());
        
        std::cout << "WebSocket: Subscription removed (ID: " << subscription_id << ")" << std::endl;
    }
}

void WebSocketServer::notification_loop() {
    while (running.load()) {
        std::unique_lock<std::mutex> lock(queue_mutex);
        queue_cv.wait(lock, [this] { return !notification_queue.empty() || !running.load(); });
        
        while (!notification_queue.empty() && running.load()) {
            SubscriptionMessage msg = notification_queue.front();
            notification_queue.pop();
            lock.unlock();
            
            // Send notification to relevant subscribers
            std::lock_guard<std::mutex> conn_lock(connections_mutex);
            for (auto& conn : connections) {
                if (!conn->is_alive()) continue;
                
                auto subscriptions = conn->get_subscriptions();
                for (const auto& sub : subscriptions) {
                    if (sub.subscription_id == msg.subscription_id) {
                        conn->send_subscription_response(msg);
                        messages_sent.fetch_add(1);
                        break;
                    }
                }
            }
            
            lock.lock();
        }
    }
}

void WebSocketServer::cleanup_disconnected_connections() {
    std::lock_guard<std::mutex> lock(connections_mutex);
    connections.erase(
        std::remove_if(connections.begin(), connections.end(),
                      [](const std::shared_ptr<WebSocketConnection>& conn) {
                          return !conn->is_alive();
                      }),
        connections.end()
    );
}

// Public notification methods
void WebSocketServer::notify_account_change(const std::string& pubkey, const std::string& account_data) {
    std::lock_guard<std::mutex> lock(subscription_mutex);
    auto it = account_subscriptions.find(pubkey);
    if (it == account_subscriptions.end()) return;
    
    for (uint64_t sub_id : it->second) {
        SubscriptionMessage msg{
            sub_id,
            "accountNotification",
            "",
            account_data,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()
        };
        
        std::lock_guard<std::mutex> queue_lock(queue_mutex);
        notification_queue.push(msg);
    }
    queue_cv.notify_one();
}

void WebSocketServer::notify_signature_status(const std::string& signature, const std::string& status) {
    std::lock_guard<std::mutex> lock(subscription_mutex);
    auto it = signature_subscriptions.find(signature);
    if (it == signature_subscriptions.end()) return;
    
    for (uint64_t sub_id : it->second) {
        SubscriptionMessage msg{
            sub_id,
            "signatureNotification",
            "",
            status,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()
        };
        
        std::lock_guard<std::mutex> queue_lock(queue_mutex);
        notification_queue.push(msg);
    }
    queue_cv.notify_one();
}

void WebSocketServer::notify_slot_change(uint64_t slot, const std::string& parent_slot, uint64_t root_slot) {
    std::lock_guard<std::mutex> lock(subscription_mutex);
    
    for (uint64_t sub_id : slot_subscriptions) {
        std::ostringstream slot_data;
        slot_data << "{\"slot\":" << slot << ",\"parent\":" << parent_slot << ",\"root\":" << root_slot << "}";
        
        SubscriptionMessage msg{
            sub_id,
            "slotNotification",
            "",
            slot_data.str(),
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()
        };
        
        std::lock_guard<std::mutex> queue_lock(queue_mutex);
        notification_queue.push(msg);
    }
    queue_cv.notify_one();
}

void WebSocketServer::notify_block_change(uint64_t slot, const std::string& block_hash, const std::string& block_data) {
    std::lock_guard<std::mutex> lock(subscription_mutex);
    
    for (uint64_t sub_id : block_subscriptions) {
        SubscriptionMessage msg{
            sub_id,
            "blockNotification",
            "",
            block_data,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()
        };
        
        std::lock_guard<std::mutex> queue_lock(queue_mutex);
        notification_queue.push(msg);
    }
    queue_cv.notify_one();
}

void WebSocketServer::notify_program_account_change(const std::string& program_id, const std::string& account_pubkey, const std::string& account_data) {
    std::lock_guard<std::mutex> lock(subscription_mutex);
    
    // For now, treat program account changes as regular account changes
    // In a full implementation, we'd have separate program account subscriptions
    auto it = account_subscriptions.find(account_pubkey);
    if (it == account_subscriptions.end()) return;
    
    for (uint64_t sub_id : it->second) {
        SubscriptionMessage msg{
            sub_id,
            "programAccountNotification",
            "",
            account_data,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()
        };
        
        std::lock_guard<std::mutex> queue_lock(queue_mutex);
        notification_queue.push(msg);
    }
    queue_cv.notify_one();
}

WebSocketServer::WebSocketStats WebSocketServer::get_stats() const {
    std::lock_guard<std::mutex> lock(connections_mutex);
    return {
        total_connections.load(),
        static_cast<uint64_t>(connections.size()),
        active_subscriptions.load(),
        messages_sent.load(),
        0 // uptime_seconds - would need start time tracking
    };
}

std::vector<std::string> WebSocketServer::get_connected_clients() const {
    std::lock_guard<std::mutex> lock(connections_mutex);
    std::vector<std::string> clients;
    for (const auto& conn : connections) {
        if (conn->is_alive()) {
            clients.push_back(conn->get_address());
        }
    }
    return clients;
}

}} // namespace slonana::network