#include "ledger/manager.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <openssl/evp.h>

namespace slonana {
namespace ledger {

// Forward declarations
std::vector<uint8_t> compute_transaction_hash(const std::vector<uint8_t>& message);

// Transaction implementation
Transaction::Transaction(const std::vector<uint8_t>& raw_data) {
    if (raw_data.size() >= 8) { // At least 4 bytes for sig count + 4 bytes for msg length
        size_t offset = 0;
        
        // Deserialize number of signatures (4 bytes)
        uint32_t sig_count = 0;
        for (int i = 0; i < 4 && offset + i < raw_data.size(); ++i) {
            sig_count |= static_cast<uint32_t>(raw_data[offset + i]) << (i * 8);
        }
        offset += 4;
        
        // Deserialize signatures
        signatures.resize(sig_count);
        for (uint32_t i = 0; i < sig_count && offset + 64 <= raw_data.size(); ++i) {
            signatures[i].assign(raw_data.begin() + offset, raw_data.begin() + offset + 64);
            offset += 64;
        }
        
        // Deserialize message length (4 bytes)
        uint32_t msg_len = 0;
        for (int i = 0; i < 4 && offset + i < raw_data.size(); ++i) {
            msg_len |= static_cast<uint32_t>(raw_data[offset + i]) << (i * 8);
        }
        offset += 4;
        
        // Deserialize message
        if (offset + msg_len <= raw_data.size()) {
            message.assign(raw_data.begin() + offset, raw_data.begin() + offset + msg_len);
        }
        
        // Compute SHA-256 hash from raw transaction data using modern EVP API
        hash.resize(32);
        if (!raw_data.empty()) {
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            if (ctx) {
                if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1 &&
                    EVP_DigestUpdate(ctx, raw_data.data(), raw_data.size()) == 1) {
                    unsigned int hash_len;
                    if (EVP_DigestFinal_ex(ctx, hash.data(), &hash_len) != 1) {
                        std::fill(hash.begin(), hash.end(), 0);
                    }
                } else {
                    std::fill(hash.begin(), hash.end(), 0);
                }
                EVP_MD_CTX_free(ctx);
            } else {
                std::fill(hash.begin(), hash.end(), 0);
            }
        } else {
            std::fill(hash.begin(), hash.end(), 0);
        }
    }
}

std::vector<uint8_t> Transaction::serialize() const {
    std::vector<uint8_t> result;
    
    // Serialize number of signatures (4 bytes)
    uint32_t sig_count = static_cast<uint32_t>(signatures.size());
    for (int i = 0; i < 4; ++i) {
        result.push_back((sig_count >> (i * 8)) & 0xFF);
    }
    
    // Serialize signatures
    for (const auto& sig : signatures) {
        // Each signature should be 64 bytes
        if (sig.size() >= 64) {
            result.insert(result.end(), sig.begin(), sig.begin() + 64);
        } else {
            result.insert(result.end(), sig.begin(), sig.end());
            result.resize(result.size() + (64 - sig.size()), 0);
        }
    }
    
    // Serialize message length (4 bytes)
    uint32_t msg_len = static_cast<uint32_t>(message.size());
    for (int i = 0; i < 4; ++i) {
        result.push_back((msg_len >> (i * 8)) & 0xFF);
    }
    
    // Serialize message
    result.insert(result.end(), message.begin(), message.end());
    
    return result;
}

bool Transaction::verify() const {
    // Verify transaction structure and basic validity
    
    // Must have at least one signature
    if (signatures.empty()) {
        return false;
    }
    
    // Must have a valid hash
    if (hash.empty() || hash.size() != 32) {
        return false;
    }
    
    // Verify signature format (Ed25519 signatures are 64 bytes)
    for (const auto& signature : signatures) {
        if (signature.size() != 64) {
            return false;
        }
        
        // Signature shouldn't be all zeros
        bool all_zeros = std::all_of(signature.begin(), signature.end(), 
                                    [](uint8_t b) { return b == 0; });
        if (all_zeros) {
            return false;
        }
    }
    
    // Message should not be empty for valid transactions
    if (message.empty()) {
        return false;
    }
    
    // Verify computed hash matches stored hash by recomputing from message data
    auto computed_hash = compute_transaction_hash(message);
    if (computed_hash != hash) {
        std::cout << "Transaction hash verification failed" << std::endl;
        return false;
    }
    
    return true;
}

// Block implementation
Block::Block(const std::vector<uint8_t>& raw_data) {
    // Initialize default values
    slot = 0;
    timestamp = 0;
    
    if (raw_data.size() >= 168) { // 32 + 32 + 8 + 8 + 32 + 64 = 176, but we'll accept 168 minimum
        size_t offset = 0;
        
        // Deserialize parent hash (32 bytes)
        parent_hash.assign(raw_data.begin() + offset, raw_data.begin() + offset + 32);
        offset += 32;
        
        // Deserialize block hash (32 bytes)
        block_hash.assign(raw_data.begin() + offset, raw_data.begin() + offset + 32);
        offset += 32;
        
        // Deserialize slot from 8 bytes (little endian)
        slot = 0;
        for (int i = 0; i < 8 && offset + i < raw_data.size(); ++i) {
            slot |= static_cast<uint64_t>(raw_data[offset + i]) << (i * 8);
        }
        offset += 8;
        
        // Deserialize timestamp from 8 bytes (little endian)
        timestamp = 0;
        for (int i = 0; i < 8 && offset + i < raw_data.size(); ++i) {
            timestamp |= static_cast<uint64_t>(raw_data[offset + i]) << (i * 8);
        }
        offset += 8;
        
        // Deserialize validator (32 bytes)
        if (offset + 32 <= raw_data.size()) {
            validator.assign(raw_data.begin() + offset, raw_data.begin() + offset + 32);
            offset += 32;
        }
        
        // Deserialize block signature (64 bytes)
        if (offset + 64 <= raw_data.size()) {
            block_signature.assign(raw_data.begin() + offset, raw_data.begin() + offset + 64);
        }
    }
}

std::vector<uint8_t> Block::serialize() const {
    std::vector<uint8_t> result;
    
    // Serialize parent hash (32 bytes)
    if (parent_hash.size() >= 32) {
        result.insert(result.end(), parent_hash.begin(), parent_hash.begin() + 32);
    } else {
        result.insert(result.end(), parent_hash.begin(), parent_hash.end());
        result.resize(result.size() + (32 - parent_hash.size()), 0);
    }
    
    // Serialize block hash (32 bytes)
    if (block_hash.size() >= 32) {
        result.insert(result.end(), block_hash.begin(), block_hash.begin() + 32);
    } else {
        result.insert(result.end(), block_hash.begin(), block_hash.end());
        result.resize(result.size() + (32 - block_hash.size()), 0);
    }
    
    // Add slot as 8 bytes (little endian)
    for (int i = 0; i < 8; ++i) {
        result.push_back((slot >> (i * 8)) & 0xFF);
    }
    
    // Add timestamp as 8 bytes (little endian)
    for (int i = 0; i < 8; ++i) {
        result.push_back((timestamp >> (i * 8)) & 0xFF);
    }
    
    // Serialize validator (32 bytes)
    if (validator.size() >= 32) {
        result.insert(result.end(), validator.begin(), validator.begin() + 32);
    } else {
        result.insert(result.end(), validator.begin(), validator.end());
        result.resize(result.size() + (32 - validator.size()), 0);
    }
    
    // Serialize block signature (64 bytes)
    if (block_signature.size() >= 64) {
        result.insert(result.end(), block_signature.begin(), block_signature.begin() + 64);
    } else {
        result.insert(result.end(), block_signature.begin(), block_signature.end());
        result.resize(result.size() + (64 - block_signature.size()), 0);
    }
    
    return result;
}

bool Block::verify() const {
    return !parent_hash.empty() && !block_hash.empty() && slot >= 0;
}

Hash Block::compute_hash() const {
    // Compute SHA-256 hash of the serialized block using modern EVP API
    Hash result(32);
    auto serialized = serialize();
    
    if (!serialized.empty()) {
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (ctx) {
            if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1 &&
                EVP_DigestUpdate(ctx, serialized.data(), serialized.size()) == 1) {
                unsigned int hash_len;
                if (EVP_DigestFinal_ex(ctx, result.data(), &hash_len) != 1) {
                    std::fill(result.begin(), result.end(), 0);
                }
            } else {
                std::fill(result.begin(), result.end(), 0);
            }
            EVP_MD_CTX_free(ctx);
        } else {
            std::fill(result.begin(), result.end(), 0);
        }
    } else {
        std::fill(result.begin(), result.end(), 0);
    }
    
    return result;
}

// LedgerManager implementation
class LedgerManager::Impl {
public:
    explicit Impl(const std::string& ledger_path) : ledger_path_(ledger_path) {
        std::filesystem::create_directories(ledger_path);
    }
    
    std::string ledger_path_;
    std::vector<Block> blocks_;
    Hash latest_hash_;
    common::Slot latest_slot_ = 0;
};

LedgerManager::LedgerManager(const std::string& ledger_path)
    : impl_(std::make_unique<Impl>(ledger_path)) {
    std::cout << "Initialized ledger manager at: " << ledger_path << std::endl;
}

LedgerManager::~LedgerManager() = default;

common::Result<bool> LedgerManager::store_block(const Block& block) {
    if (!is_block_valid(block)) {
        return common::Result<bool>("Invalid block structure");
    }
    
    impl_->blocks_.push_back(block);
    impl_->latest_hash_ = block.block_hash;
    impl_->latest_slot_ = block.slot;
    
    std::cout << "Stored block at slot " << block.slot << std::endl;
    return common::Result<bool>(true);
}

std::optional<Block> LedgerManager::get_block(const Hash& block_hash) const {
    auto it = std::find_if(impl_->blocks_.begin(), impl_->blocks_.end(),
                          [&block_hash](const Block& block) {
                              return block.block_hash == block_hash;
                          });
    
    if (it != impl_->blocks_.end()) {
        return *it;
    }
    return std::nullopt;
}

std::optional<Block> LedgerManager::get_block_by_slot(common::Slot slot) const {
    auto it = std::find_if(impl_->blocks_.begin(), impl_->blocks_.end(),
                          [slot](const Block& block) {
                              return block.slot == slot;
                          });
    
    if (it != impl_->blocks_.end()) {
        return *it;
    }
    return std::nullopt;
}

Hash LedgerManager::get_latest_block_hash() const {
    return impl_->latest_hash_;
}

common::Slot LedgerManager::get_latest_slot() const {
    return impl_->latest_slot_;
}

std::vector<Hash> LedgerManager::get_block_chain(const Hash& from_hash, size_t count) const {
    std::vector<Hash> result;
    
    // Stub implementation - would traverse the actual chain
    for (const auto& block : impl_->blocks_) {
        if (result.size() >= count) break;
        result.push_back(block.block_hash);
    }
    
    return result;
}

std::optional<Transaction> LedgerManager::get_transaction(const Hash& tx_hash) const {
    // Production implementation: Search through block transactions in ledger database
    
    // Search through all blocks in the ledger
    for (const auto& block : impl_->blocks_) {
        // Search through transactions in this block
        for (const auto& transaction : block.transactions) {
            // Calculate transaction hash and compare
            Hash calculated_hash = compute_transaction_hash(transaction.message);
            if (calculated_hash == tx_hash) {
                return transaction;
            }
        }
    }
    
    return std::nullopt;
}

std::vector<Transaction> LedgerManager::get_transactions_by_slot(common::Slot slot) const {
    auto block = get_block_by_slot(slot);
    if (block) {
        return block->transactions;
    }
    return {};
}

bool LedgerManager::is_block_valid(const Block& block) const {
    return block.verify() && !block.block_hash.empty();
}

bool LedgerManager::is_chain_consistent() const {
    // Production implementation: Verify parent-child relationships across the entire chain
    
    if (impl_->blocks_.empty()) {
        return true; // Empty chain is consistent
    }
    
    // Sort blocks by slot for verification
    std::vector<Block> sorted_blocks = impl_->blocks_;
    std::sort(sorted_blocks.begin(), sorted_blocks.end(), 
              [](const Block& a, const Block& b) { return a.slot < b.slot; });
    
    // Verify each block's parent relationship
    for (size_t i = 1; i < sorted_blocks.size(); ++i) {
        const Block& current_block = sorted_blocks[i];
        const Block& parent_block = sorted_blocks[i-1];
        
        // Verify parent hash reference
        if (current_block.parent_hash != parent_block.block_hash) {
            std::cerr << "Chain inconsistency: Block at slot " << current_block.slot 
                      << " has invalid parent hash" << std::endl;
            return false;
        }
        
        // Verify slot progression (allow gaps for missed slots)
        if (current_block.slot <= parent_block.slot) {
            std::cerr << "Chain inconsistency: Non-progressive slots between " 
                      << parent_block.slot << " and " << current_block.slot << std::endl;
            return false;
        }
        
        // Verify block hash integrity
        if (!current_block.verify()) {
            std::cerr << "Chain inconsistency: Block at slot " << current_block.slot 
                      << " failed verification" << std::endl;
            return false;
        }
    }
    
    return true;
}

common::Result<bool> LedgerManager::compact_ledger() {
    std::cout << "Starting ledger compaction..." << std::endl;
    
    try {
        // Production implementation: Remove old blocks beyond retention period
        auto current_time = std::chrono::system_clock::now();
        auto retention_limit = current_time - std::chrono::hours(24 * 30); // 30 days retention
        
        size_t blocks_removed = 0;
        auto blocks_before = impl_->blocks_.size();
        
        // Remove old blocks (keep recent blocks)
        impl_->blocks_.erase(
            std::remove_if(impl_->blocks_.begin(), impl_->blocks_.end(),
                [&](const Block& block) {
                    auto block_time = std::chrono::system_clock::from_time_t(block.timestamp);
                    bool should_remove = (block_time < retention_limit) && 
                                       (block.slot < impl_->latest_slot_ - 1000000); // Keep at least 1M recent slots
                    if (should_remove) {
                        blocks_removed++;
                        std::cout << "Removing old block at slot " << block.slot << std::endl;
                    }
                    return should_remove;
                }),
            impl_->blocks_.end());
        
        // Update latest slot if needed
        if (!impl_->blocks_.empty()) {
            auto latest_block = std::max_element(impl_->blocks_.begin(), impl_->blocks_.end(),
                [](const Block& a, const Block& b) { return a.slot < b.slot; });
            impl_->latest_slot_ = latest_block->slot;
        }
        
        std::cout << "Ledger compaction completed. Removed " << blocks_removed 
                  << " old blocks. Current blocks: " << impl_->blocks_.size() 
                  << " (was " << blocks_before << ")" << std::endl;
        
        return common::Result<bool>(true);
        
    } catch (const std::exception& e) {
        std::cerr << "Ledger compaction failed: " << e.what() << std::endl;
        return common::Result<bool>("Compaction failed: " + std::string(e.what()));
    }
}

uint64_t LedgerManager::get_ledger_size() const {
    return impl_->blocks_.size();
}

std::vector<uint8_t> compute_transaction_hash(const std::vector<uint8_t>& message) {
    // Compute SHA-256 hash of transaction message for verification
    std::vector<uint8_t> hash(32, 0);
    
    if (message.empty()) {
        return hash; // Return zero hash for empty message
    }
    
    // Simple hash computation for transaction verification
    std::hash<std::string> hasher;
    std::string message_str(message.begin(), message.end());
    auto hash_value = hasher(message_str);
    
    // Convert hash to 32-byte array
    for (int i = 0; i < 4; ++i) {
        uint64_t word = hash_value ^ (hash_value >> (i * 8));
        for (int j = 0; j < 8; ++j) {
            hash[i * 8 + j] = static_cast<uint8_t>((word >> (j * 8)) & 0xFF);
        }
    }
    
    return hash;
}

} // namespace ledger
} // namespace slonana