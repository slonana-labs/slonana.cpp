#include "network/shred_distribution.h"
#include <algorithm>
#include <iostream>
#include <cstring>
#include <openssl/sha.h>
#include <openssl/evp.h>

namespace slonana {
namespace network {

// Shred implementation
Shred::Shred(uint64_t slot, uint32_t index, const std::vector<uint8_t>& data, ShredType type) {
    // Initialize header
    std::memset(&header_, 0, sizeof(header_));
    header_.slot = slot;
    header_.index = index;
    header_.version = 1; // Default version
    header_.variant = static_cast<uint8_t>(type);
    
    // Set payload
    payload_ = data;
    if (payload_.size() > MAX_PAYLOAD_SIZE) {
        payload_.resize(MAX_PAYLOAD_SIZE);
    }
}

bool Shred::verify_signature(const std::vector<uint8_t>& pubkey) const {
    if (pubkey.size() != 32) {
        return false;
    }
    
    // Create message to verify (header + payload without signature)
    std::vector<uint8_t> message;
    message.push_back(header_.variant);
    
    // Add slot (8 bytes)
    for (int i = 0; i < 8; ++i) {
        message.push_back(static_cast<uint8_t>((header_.slot >> (i * 8)) & 0xFF));
    }
    
    // Add index (4 bytes)
    for (int i = 0; i < 4; ++i) {
        message.push_back(static_cast<uint8_t>((header_.index >> (i * 8)) & 0xFF));
    }
    
    // Add version and fec_set_index
    for (int i = 0; i < 2; ++i) {
        message.push_back(static_cast<uint8_t>((header_.version >> (i * 8)) & 0xFF));
    }
    for (int i = 0; i < 2; ++i) {
        message.push_back(static_cast<uint8_t>((header_.fec_set_index >> (i * 8)) & 0xFF));
    }
    
    // Add payload
    message.insert(message.end(), payload_.begin(), payload_.end());
    
    // For now, return true (simplified signature verification)
    // In a real implementation, this would use Ed25519 verification
    return true;
}

bool Shred::sign(const std::vector<uint8_t>& private_key) {
    if (private_key.size() != 32) {
        return false;
    }
    
    // For now, create a dummy signature
    // In a real implementation, this would use Ed25519 signing
    std::memset(header_.signature, 0, 64);
    
    // Simple hash as signature placeholder (using modern OpenSSL API)
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        return false;
    }
    
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1) {
        EVP_MD_CTX_free(ctx);
        return false;
    }
    
    EVP_DigestUpdate(ctx, private_key.data(), private_key.size());
    EVP_DigestUpdate(ctx, &header_.variant, sizeof(header_) - 64); // Exclude signature field
    EVP_DigestUpdate(ctx, payload_.data(), payload_.size());
    
    uint8_t hash[32];
    unsigned int hash_len;
    if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return false;
    }
    
    EVP_MD_CTX_free(ctx);
    
    // Copy hash to signature (first 32 bytes)
    std::memcpy(header_.signature, hash, 32);
    
    return true;
}

ShredType Shred::get_type() const {
    return static_cast<ShredType>(header_.variant & 0x1);
}

bool Shred::is_valid() const {
    // Check size constraints
    if (payload_.size() > MAX_PAYLOAD_SIZE) {
        return false;
    }
    
    // Check that total size doesn't exceed maximum
    if (size() > MAX_SHRED_SIZE) {
        return false;
    }
    
    // Check version is reasonable
    if (header_.version == 0) {
        return false;
    }
    
    return true;
}

std::vector<uint8_t> Shred::serialize() const {
    std::vector<uint8_t> result;
    result.reserve(size());
    
    // Serialize header
    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&header_);
    result.insert(result.end(), header_bytes, header_bytes + SHRED_HEADER_SIZE);
    
    // Serialize payload
    result.insert(result.end(), payload_.begin(), payload_.end());
    
    return result;
}

std::optional<Shred> Shred::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < SHRED_HEADER_SIZE) {
        return std::nullopt;
    }
    
    Shred shred;
    
    // Deserialize header
    std::memcpy(&shred.header_, data.data(), SHRED_HEADER_SIZE);
    
    // Deserialize payload
    if (data.size() > SHRED_HEADER_SIZE) {
        size_t payload_size = data.size() - SHRED_HEADER_SIZE;
        shred.payload_.assign(data.begin() + SHRED_HEADER_SIZE, data.end());
    }
    
    // Validate
    if (!shred.is_valid()) {
        return std::nullopt;
    }
    
    return shred;
}

Shred Shred::create_data_shred(uint64_t slot, uint32_t index, const std::vector<uint8_t>& data) {
    return Shred(slot, index, data, ShredType::DATA);
}

Shred Shred::create_coding_shred(uint64_t slot, uint32_t index, uint16_t fec_set_index,
                                const std::vector<uint8_t>& coding_data) {
    Shred shred(slot, index, coding_data, ShredType::CODING);
    shred.header_.fec_set_index = fec_set_index;
    return shred;
}

// TurbineBroadcast implementation
TurbineBroadcast::TurbineBroadcast(const TurbineNode& self_node)
    : self_node_(self_node), max_retransmit_attempts_(3),
      retransmit_timeout_(std::chrono::milliseconds(100)) {
}

void TurbineBroadcast::initialize(std::unique_ptr<TurbineTree> tree) {
    std::lock_guard<std::mutex> lock(broadcast_mutex_);
    tree_ = std::move(tree);
    std::cout << "📡 TurbineBroadcast: Initialized with tree" << std::endl;
}

std::string TurbineBroadcast::shred_key(const Shred& shred) const {
    return std::to_string(shred.slot()) + "_" + std::to_string(shred.index());
}

bool TurbineBroadcast::should_retransmit(const Shred& shred) const {
    std::lock_guard<std::mutex> lock(tracking_mutex_);
    
    auto key = shred_key(shred);
    auto it = retransmit_counts_.find(key);
    
    if (it == retransmit_counts_.end()) {
        return true; // First transmission
    }
    
    return it->second < max_retransmit_attempts_;
}

std::vector<TurbineNode> TurbineBroadcast::select_broadcast_targets(const Shred& shred) const {
    std::lock_guard<std::mutex> lock(broadcast_mutex_);
    
    if (!tree_) {
        return {};
    }
    
    // Get children nodes for this node
    auto children = tree_->get_children(self_node_);
    
    // For retransmissions, also include retransmit peers
    if (!should_retransmit(shred)) {
        auto retransmit_peers = tree_->get_retransmit_peers(self_node_);
        children.insert(children.end(), retransmit_peers.begin(), retransmit_peers.end());
    }
    
    return children;
}

void TurbineBroadcast::update_stats(const Shred& shred, bool sent) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    if (sent) {
        stats_.shreds_sent++;
    } else {
        stats_.shreds_received++;
    }
    
    stats_.last_activity = std::chrono::steady_clock::now();
}

void TurbineBroadcast::broadcast_shreds(const std::vector<Shred>& shreds) {
    if (shreds.empty()) {
        return;
    }
    
    for (const auto& shred : shreds) {
        auto targets = select_broadcast_targets(shred);
        
        if (!targets.empty() && send_callback_) {
            send_callback_(shred, targets);
            update_stats(shred, true);
            
            // Update tracking
            {
                std::lock_guard<std::mutex> lock(tracking_mutex_);
                auto key = shred_key(shred);
                shred_timestamps_[key] = std::chrono::steady_clock::now();
                retransmit_counts_[key]++;
            }
        }
    }
    
    std::cout << "📡 TurbineBroadcast: Broadcast " << shreds.size() << " shreds" << std::endl;
}

void TurbineBroadcast::handle_received_shred(const Shred& shred, const std::string& from) {
    // Check for duplicates
    if (is_duplicate_shred(shred)) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.duplicate_shreds++;
        return;
    }
    
    // Validate shred
    if (!ShredValidator::validate_shred(shred)) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.invalid_shreds++;
        return;
    }
    
    update_stats(shred, false);
    
    // Call receive callback if set
    if (receive_callback_) {
        receive_callback_(shred, from);
    }
    
    // Forward to children if we're not a leaf node
    auto children = select_broadcast_targets(shred);
    if (!children.empty()) {
        retransmit_shred(shred, children);
    }
}

void TurbineBroadcast::retransmit_shred(const Shred& shred, const std::vector<TurbineNode>& peers) {
    if (!should_retransmit(shred)) {
        return;
    }
    
    if (send_callback_) {
        send_callback_(shred, peers);
        
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.shreds_retransmitted++;
        }
        
        {
            std::lock_guard<std::mutex> lock(tracking_mutex_);
            auto key = shred_key(shred);
            retransmit_counts_[key]++;
        }
    }
}

void TurbineBroadcast::set_send_callback(
    std::function<void(const Shred&, const std::vector<TurbineNode>&)> callback) {
    send_callback_ = std::move(callback);
}

void TurbineBroadcast::set_receive_callback(
    std::function<void(const Shred&, const std::string&)> callback) {
    receive_callback_ = std::move(callback);
}

ShredDistributionStats TurbineBroadcast::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void TurbineBroadcast::reset_stats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = ShredDistributionStats{};
}

void TurbineBroadcast::update_tree(std::unique_ptr<TurbineTree> new_tree) {
    std::lock_guard<std::mutex> lock(broadcast_mutex_);
    tree_ = std::move(new_tree);
    std::cout << "📡 TurbineBroadcast: Updated tree" << std::endl;
}

const TurbineTree& TurbineBroadcast::get_tree() const {
    std::lock_guard<std::mutex> lock(broadcast_mutex_);
    if (!tree_) {
        throw std::runtime_error("No tree initialized");
    }
    return *tree_;
}

bool TurbineBroadcast::is_duplicate_shred(const Shred& shred) const {
    std::lock_guard<std::mutex> lock(tracking_mutex_);
    
    auto key = shred_key(shred);
    return shred_timestamps_.find(key) != shred_timestamps_.end();
}

void TurbineBroadcast::cleanup_tracking_data(std::chrono::milliseconds max_age) {
    std::lock_guard<std::mutex> lock(tracking_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    
    // Remove old entries
    auto it = shred_timestamps_.begin();
    while (it != shred_timestamps_.end()) {
        if (now - it->second > max_age) {
            retransmit_counts_.erase(it->first);
            it = shred_timestamps_.erase(it);
        } else {
            ++it;
        }
    }
}

void TurbineBroadcast::set_retransmit_params(uint32_t max_attempts, std::chrono::milliseconds timeout) {
    max_retransmit_attempts_ = max_attempts;
    retransmit_timeout_ = timeout;
}

bool TurbineBroadcast::force_retransmit(const Shred& shred) {
    auto targets = select_broadcast_targets(shred);
    
    if (!targets.empty() && send_callback_) {
        send_callback_(shred, targets);
        update_stats(shred, true);
        return true;
    }
    
    return false;
}

// ShredValidator implementation
bool ShredValidator::validate_shred(const Shred& shred) {
    return shred.is_valid();
}

bool ShredValidator::validate_signature(const Shred& shred, const std::vector<uint8_t>& expected_pubkey) {
    return shred.verify_signature(expected_pubkey);
}

bool ShredValidator::validate_slot_progression(const Shred& shred, uint64_t last_slot) {
    // Allow current slot or future slots
    return shred.slot() >= last_slot;
}

bool ShredValidator::validate_index(const Shred& shred, uint32_t max_index) {
    return shred.index() <= max_index;
}

// Utility functions implementation
namespace shred_utils {

std::vector<Shred> split_data_into_shreds(const std::vector<uint8_t>& data, 
                                         uint64_t slot, uint32_t start_index) {
    std::vector<Shred> shreds;
    
    if (data.empty()) {
        return shreds;
    }
    
    size_t max_payload = Shred::max_payload_size();
    size_t num_shreds = (data.size() + max_payload - 1) / max_payload;
    
    for (size_t i = 0; i < num_shreds; ++i) {
        size_t offset = i * max_payload;
        size_t chunk_size = std::min(max_payload, data.size() - offset);
        
        std::vector<uint8_t> chunk(data.begin() + offset, data.begin() + offset + chunk_size);
        
        shreds.push_back(Shred::create_data_shred(slot, start_index + i, chunk));
    }
    
    return shreds;
}

std::vector<uint8_t> reconstruct_data_from_shreds(const std::vector<Shred>& shreds) {
    if (shreds.empty()) {
        return {};
    }
    
    // Sort shreds by index
    auto sorted_shreds = shreds;
    std::sort(sorted_shreds.begin(), sorted_shreds.end(),
              [](const Shred& a, const Shred& b) {
                  return a.index() < b.index();
              });
    
    std::vector<uint8_t> result;
    
    for (const auto& shred : sorted_shreds) {
        const auto& payload = shred.payload();
        result.insert(result.end(), payload.begin(), payload.end());
    }
    
    return result;
}

std::vector<Shred> generate_coding_shreds(const std::vector<Shred>& data_shreds,
                                         size_t num_coding_shreds) {
    std::vector<Shred> coding_shreds;
    
    if (data_shreds.empty() || num_coding_shreds == 0) {
        return coding_shreds;
    }
    
    // Simple coding scheme (in real implementation, use Reed-Solomon)
    uint64_t slot = data_shreds[0].slot();
    uint32_t base_index = data_shreds.back().index() + 1;
    
    for (size_t i = 0; i < num_coding_shreds; ++i) {
        // Create dummy coding data
        std::vector<uint8_t> coding_data(100, static_cast<uint8_t>(i));
        
        coding_shreds.push_back(Shred::create_coding_shred(
            slot, base_index + i, static_cast<uint16_t>(i), coding_data));
    }
    
    return coding_shreds;
}

std::vector<Shred> recover_missing_shreds(const std::vector<Shred>& available_shreds,
                                         const std::vector<uint32_t>& missing_indices) {
    // Simplified recovery (in real implementation, use Reed-Solomon)
    std::vector<Shred> recovered;
    
    if (available_shreds.empty() || missing_indices.empty()) {
        return recovered;
    }
    
    uint64_t slot = available_shreds[0].slot();
    
    for (uint32_t index : missing_indices) {
        // Create dummy recovered shred
        std::vector<uint8_t> dummy_data(50, static_cast<uint8_t>(index % 256));
        recovered.push_back(Shred::create_data_shred(slot, index, dummy_data));
    }
    
    return recovered;
}

std::string calculate_shred_hash(const Shred& shred) {
    auto serialized = shred.serialize();
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(serialized.data(), serialized.size(), hash);
    
    std::string result;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", hash[i]);
        result += buf;
    }
    
    return result;
}

Shred compress_shred(const Shred& shred) {
    // Simplified compression (in real implementation, use proper compression)
    return shred;
}

Shred decompress_shred(const Shred& compressed_shred) {
    // Simplified decompression
    return compressed_shred;
}

} // namespace shred_utils

} // namespace network
} // namespace slonana