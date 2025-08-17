#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

namespace slonana {
namespace common {

/**
 * Common types and utilities used across the validator
 */

using Hash = std::vector<uint8_t>;
using PublicKey = std::vector<uint8_t>;
using Signature = std::vector<uint8_t>;
using Slot = uint64_t;
using Epoch = uint64_t;
using Lamports = uint64_t;

/**
 * Configuration structure for the validator
 */
struct ValidatorConfig {
    std::string identity_keypair_path;
    std::string ledger_path;
    std::string rpc_bind_address = "127.0.0.1:8899";
    std::string gossip_bind_address = "127.0.0.1:8001";
    bool enable_rpc = true;
    bool enable_gossip = true;
    uint32_t max_connections = 1000;
};

/**
 * Result type for operations that can fail
 */
template<typename T>
class Result {
private:
    bool success_;
    T value_;
    std::string error_;

public:
    explicit Result(T value) : success_(true), value_(std::move(value)) {}
    explicit Result(const char* error) : success_(false), error_(error) {}
    explicit Result(const std::string& error) : success_(false), error_(error) {}
    
    bool is_ok() const { return success_; }
    bool is_err() const { return !success_; }
    
    const T& value() const { return value_; }
    const std::string& error() const { return error_; }
};

} // namespace common
} // namespace slonana

// Hash function for std::vector<uint8_t> to enable use in unordered_map
namespace std {
template<>
struct hash<std::vector<uint8_t>> {
    size_t operator()(const std::vector<uint8_t>& v) const {
        size_t seed = v.size();
        for (auto& i : v) {
            seed ^= i + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};
}