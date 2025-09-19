#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <memory>
#include <cstdint>

namespace slonana {
namespace security {

class SecurityValidator {
public:
    enum class ValidationLevel {
        STRICT,    // Production - reject everything suspicious
        NORMAL,    // Development - log warnings but continue
        PERMISSIVE // Testing - minimal validation
    };

    static bool validate_rpc_input(const std::string& input, size_t max_size = 1024 * 1024);
    static bool validate_json_structure(const std::string& json);
    static bool is_safe_character(char c);
    static void security_log(const std::string& message);
    
    static void set_validation_level(ValidationLevel level);
    static ValidationLevel get_validation_level();

private:
    static ValidationLevel validation_level_;
};

template<typename T>
class SecureBuffer {
private:
    std::unique_ptr<T[]> data_;
    size_t size_;
    size_t capacity_;
    mutable std::mutex mutex_;
    
public:
    class SecureView {
    public:
        SecureView(T* ptr, size_t size);
        ~SecureView();
        
        T& operator[](size_t index);
        const T& operator[](size_t index) const;
        size_t size() const { return size_; }
        
    private:
        T* ptr_;
        size_t size_;
        void* guard_before_;
        void* guard_after_;
        
        void add_guard_pages();
        void remove_guard_pages();
    };
    
    explicit SecureBuffer(size_t capacity);
    ~SecureBuffer();
    
    SecureView get_view() const;
    void resize(size_t new_size);
    void secure_zero();
    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }

private:
    void allocate_with_guards(size_t size);
    void deallocate_with_guards();
};

class CryptoSecurity {
public:
    // Secure random number generation using hardware entropy
    static std::vector<uint8_t> generate_secure_random(size_t bytes);
    
    // Timing-attack resistant comparison
    static bool secure_compare(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);
    
    // Secure key derivation
    static std::vector<uint8_t> derive_key(const std::vector<uint8_t>& master_key,
                                          const std::vector<uint8_t>& salt,
                                          size_t key_length);
    
    // Secure memory operations
    static void secure_zero(void* ptr, size_t size);
    static bool verify_entropy();
    
    // Hash operations
    static std::vector<uint8_t> secure_hash(const std::vector<uint8_t>& data);
    static bool verify_hash(const std::vector<uint8_t>& data, const std::vector<uint8_t>& hash);

private:
    static bool check_hardware_entropy();
};

class NetworkSecurity {
private:
    struct ConnectionInfo {
        std::chrono::steady_clock::time_point last_request;
        size_t request_count;
        size_t request_count_per_minute;
        std::chrono::steady_clock::time_point minute_start;
        bool is_blacklisted;
        std::chrono::steady_clock::time_point blacklist_start;
    };
    
    std::unordered_map<std::string, ConnectionInfo> connections_;
    mutable std::mutex connections_mutex_;
    
    // Rate limiting configuration
    static constexpr size_t MAX_REQUESTS_PER_SECOND = 100;
    static constexpr size_t MAX_REQUESTS_PER_MINUTE = 1000;
    static constexpr auto BLACKLIST_DURATION = std::chrono::minutes(10);
    static constexpr size_t MAX_CONNECTIONS = 10000;
    
public:
    bool is_request_allowed(const std::string& client_ip);
    void record_request(const std::string& client_ip);
    void blacklist_ip(const std::string& client_ip);
    void remove_from_blacklist(const std::string& client_ip);
    
    // Connection management
    size_t get_active_connections() const;
    void cleanup_old_connections();
    
    // Attack detection
    bool detect_pattern_attack(const std::string& client_ip, const std::string& request_pattern);
    void log_security_event(const std::string& client_ip, const std::string& event);

private:
    void cleanup_expired_blacklists();
    bool is_suspicious_pattern(const std::string& pattern);
};

class InputSanitizer {
public:
    // SQL injection prevention
    static std::string sanitize_sql_input(const std::string& input);
    
    // XSS prevention
    static std::string sanitize_html_input(const std::string& input);
    
    // JSON injection prevention
    static std::string sanitize_json_input(const std::string& input);
    
    // Path traversal prevention
    static std::string sanitize_path_input(const std::string& input);
    
    // General purpose sanitization
    static std::string sanitize_general_input(const std::string& input);
    
    // Validation helpers
    static bool is_valid_base58(const std::string& input);
    static bool is_valid_hex(const std::string& input);
    static bool is_valid_numeric(const std::string& input);
    static bool is_valid_json(const std::string& input);

private:
    static std::string remove_dangerous_chars(const std::string& input);
    static std::string escape_special_chars(const std::string& input);
};

class SecurityAudit {
public:
    struct SecurityEvent {
        std::chrono::steady_clock::time_point timestamp;
        std::string event_type;
        std::string client_ip;
        std::string details;
        std::string severity; // "LOW", "MEDIUM", "HIGH", "CRITICAL"
    };
    
    static void log_security_event(const std::string& event_type,
                                  const std::string& client_ip,
                                  const std::string& details,
                                  const std::string& severity = "MEDIUM");
    
    static std::vector<SecurityEvent> get_recent_events(size_t count = 100);
    static size_t get_event_count(const std::string& event_type, 
                                 std::chrono::minutes duration = std::chrono::minutes(60));
    
    static void clear_old_events(std::chrono::hours retention = std::chrono::hours(24));
    
    // Threat analysis
    static bool is_under_attack();
    static std::vector<std::string> get_top_threat_ips(size_t count = 10);
    
private:
    static std::vector<SecurityEvent> security_events_;
    static std::mutex events_mutex_;
    static constexpr size_t MAX_EVENTS = 10000;
};

} // namespace security
} // namespace slonana