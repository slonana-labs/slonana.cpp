/**
 * @file manager.cpp
 * @brief Implements the LedgerManager and related data structures.
 *
 * This file provides the concrete implementations for the `Transaction` and
 * `Block` structs, as well as the `LedgerManager` class, which handles the
 * persistent storage of the blockchain.
 */
#include "ledger/manager.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <openssl/evp.h>

namespace slonana {
namespace ledger {

// Forward declarations
std::vector<uint8_t>
compute_transaction_hash(const std::vector<uint8_t> &message);

/**
 * @brief Constructs a Transaction object by deserializing raw byte data.
 * @param raw_data A byte vector containing the serialized transaction.
 */
Transaction::Transaction(const std::vector<uint8_t> &raw_data) {
  if (raw_data.size() < 8) return;

  size_t offset = 0;
  uint32_t sig_count = 0;
  // ... (deserialization logic for signatures and message) ...

  // Compute transaction hash
  hash.resize(32);
  if (!raw_data.empty()) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx) {
      if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1 &&
          EVP_DigestUpdate(ctx, raw_data.data(), raw_data.size()) == 1) {
        unsigned int hash_len;
        EVP_DigestFinal_ex(ctx, hash.data(), &hash_len);
      }
      EVP_MD_CTX_free(ctx);
    }
  }
}

/**
 * @brief Serializes the transaction into a byte vector.
 * @return A byte vector representing the serialized transaction.
 */
std::vector<uint8_t> Transaction::serialize() const {
  std::vector<uint8_t> result;
  uint32_t sig_count = static_cast<uint32_t>(signatures.size());
  for (int i = 0; i < 4; ++i) result.push_back((sig_count >> (i * 8)) & 0xFF);

  for (const auto &sig : signatures) {
    result.insert(result.end(), sig.begin(), sig.end());
    if (sig.size() < 64) result.resize(result.size() + (64 - sig.size()), 0);
  }

  uint32_t msg_len = static_cast<uint32_t>(message.size());
  for (int i = 0; i < 4; ++i) result.push_back((msg_len >> (i * 8)) & 0xFF);
  result.insert(result.end(), message.begin(), message.end());

  return result;
}

/**
 * @brief Verifies the integrity and validity of the transaction.
 * @details This includes checking for the presence of signatures and a valid
 * hash, verifying the signature format, and comparing the stored hash with a
 * freshly computed one.
 * @return True if the transaction is valid, false otherwise.
 */
bool Transaction::verify() const {
  try {
    if (signatures.empty() || hash.empty() || hash.size() != 32) return false;

    for (const auto &signature : signatures) {
      if (signature.size() != 64) return false;
      // Additional signature verification logic would go here.
    }

    if (message.empty()) return false;

    auto computed_hash = compute_transaction_hash(message);
    if (computed_hash != hash) {
      std::cout << "Transaction hash verification failed" << std::endl;
      return false;
    }
    return true;
  } catch (const std::exception &e) {
    std::cerr << "ERROR: Transaction verification exception: " << e.what() << std::endl;
    return false;
  }
}

/**
 * @brief Constructs a Block object by deserializing raw byte data.
 * @param raw_data A byte vector containing the serialized block.
 */
Block::Block(const std::vector<uint8_t> &raw_data) {
  slot = 0;
  timestamp = 0;
  if (raw_data.size() < 168) return; // Minimum size check

  size_t offset = 0;
  parent_hash.assign(raw_data.begin() + offset, raw_data.begin() + offset + 32);
  offset += 32;
  block_hash.assign(raw_data.begin() + offset, raw_data.begin() + offset + 32);
  offset += 32;
  // ... (deserialization for slot, timestamp, validator, signature) ...
}

/**
 * @brief Serializes the block's metadata into a byte vector.
 * @note This serialization does not include the transactions.
 * @return A byte vector representing the serialized block header.
 */
std::vector<uint8_t> Block::serialize() const {
  std::vector<uint8_t> result;
  // ... (serialization logic) ...
  return result;
}

/**
 * @brief Verifies the basic integrity of the block.
 * @details Checks that essential fields like hashes are not empty. It does not
 * verify the block signature or the validity of its transactions.
 * @return True if the block structure is valid, false otherwise.
 */
bool Block::verify() const {
  return !parent_hash.empty() && !block_hash.empty() && slot >= 0;
}

/**
 * @brief Computes the SHA-256 hash of the block's serialized data.
 * @return A 32-byte hash of the block.
 */
Hash Block::compute_hash() const {
  Hash result(32);
  auto serialized = serialize();
  if (!serialized.empty()) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx) {
      if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1 &&
          EVP_DigestUpdate(ctx, serialized.data(), serialized.size()) == 1) {
        unsigned int hash_len;
        EVP_DigestFinal_ex(ctx, result.data(), &hash_len);
      }
      EVP_MD_CTX_free(ctx);
    }
  }
  return result;
}

/**
 * @brief Private implementation (PIMPL) for the LedgerManager class.
 * @details This class holds the internal state and logic for the ledger,
 * such as the storage path and in-memory block cache. This pattern helps to
 * hide implementation details from the public header file.
 */
class LedgerManager::Impl {
public:
  explicit Impl(const std::string &ledger_path) : ledger_path_(ledger_path) {
    std::filesystem::create_directories(ledger_path);
    load_blocks_from_disk();
  }

  void save_block_to_disk(const Block &block);
  void load_blocks_from_disk();

  std::string ledger_path_;
  std::vector<Block> blocks_;
  Hash latest_hash_;
  common::Slot latest_slot_ = 0;
};

/**
 * @brief Saves a single block to a binary file on disk.
 * @param block The block to be saved.
 */
void LedgerManager::Impl::save_block_to_disk(const Block &block) {
    std::string block_file = ledger_path_ + "/block_" + std::to_string(block.slot) + ".dat";
    std::ofstream file(block_file, std::ios::binary);
    if (file.is_open()) {
        auto serialized = block.serialize();
        file.write(reinterpret_cast<const char *>(serialized.data()), serialized.size());
    }
}

/**
 * @brief Loads all available blocks from the ledger directory into memory on startup.
 */
void LedgerManager::Impl::load_blocks_from_disk() {
    try {
        if (!std::filesystem::exists(ledger_path_)) return;
        for (const auto &entry : std::filesystem::directory_iterator(ledger_path_)) {
            if (entry.is_regular_file() && entry.path().filename().string().find("block_") == 0) {
                std::ifstream file(entry.path(), std::ios::binary);
                if (file.is_open()) {
                    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    if (!data.empty()) {
                        Block block(data);
                        blocks_.push_back(block);
                        if (block.slot > latest_slot_) {
                            latest_slot_ = block.slot;
                            latest_hash_ = block.block_hash;
                        }
                    }
                }
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "Warning: Could not load blocks from disk: " << e.what() << std::endl;
    }
}

/**
 * @brief Constructs a LedgerManager and initializes it with the given path.
 * @param ledger_path The file system path to the directory where ledger data is stored.
 */
LedgerManager::LedgerManager(const std::string &ledger_path)
    : impl_(std::make_unique<Impl>(ledger_path)) {
  std::cout << "Initialized ledger manager at: " << ledger_path << std::endl;
}

/**
 * @brief Destructor for the LedgerManager.
 */
LedgerManager::~LedgerManager() = default;

/**
 * @brief Stores a block in the ledger, both in-memory and on disk.
 * @param block The block to be stored.
 * @return A Result indicating success or failure.
 */
common::Result<bool> LedgerManager::store_block(const Block &block) {
  if (!is_block_valid(block)) {
    return common::Result<bool>("Invalid block structure");
  }
  impl_->blocks_.push_back(block);
  impl_->latest_hash_ = block.block_hash;
  impl_->latest_slot_ = block.slot;
  impl_->save_block_to_disk(block);
  std::cout << "Stored block at slot " << block.slot << std::endl;
  return common::Result<bool>(true);
}

/**
 * @brief Retrieves a block from the ledger by its hash.
 * @param block_hash The hash of the block to retrieve.
 * @return An `std::optional<Block>` containing the block if found, otherwise `std::nullopt`.
 */
std::optional<Block> LedgerManager::get_block(const Hash &block_hash) const {
  auto it = std::find_if(impl_->blocks_.begin(), impl_->blocks_.end(),
                         [&](const Block &b) { return b.block_hash == block_hash; });
  if (it != impl_->blocks_.end()) return *it;
  return std::nullopt;
}

/**
 * @brief Retrieves a block from the ledger by its slot number.
 * @param slot The slot number of the block to retrieve.
 * @return An `std::optional<Block>` containing the block if found, otherwise `std::nullopt`.
 */
std::optional<Block> LedgerManager::get_block_by_slot(common::Slot slot) const {
  auto it = std::find_if(impl_->blocks_.begin(), impl_->blocks_.end(),
                         [slot](const Block &b) { return b.slot == slot; });
  if (it != impl_->blocks_.end()) return *it;
  return std::nullopt;
}

/**
 * @brief Gets the hash of the most recently added block.
 * @return The hash of the latest block.
 */
Hash LedgerManager::get_latest_block_hash() const {
  return impl_->latest_hash_;
}

/**
 * @brief Gets the slot number of the most recently added block.
 * @return The latest slot number.
 */
common::Slot LedgerManager::get_latest_slot() const {
  return impl_->latest_slot_;
}

/**
 * @brief Retrieves a sequence of block hashes starting from a given hash.
 * @note This is a stub implementation. A full implementation would traverse the chain backwards.
 * @param from_hash The hash of the block to start from.
 * @param count The maximum number of block hashes to retrieve.
 * @return A vector of block hashes.
 */
std::vector<Hash> LedgerManager::get_block_chain(const Hash &from_hash, size_t count) const {
  std::vector<Hash> result;
  for (const auto &block : impl_->blocks_) {
    if (result.size() >= count) break;
    result.push_back(block.block_hash);
  }
  return result;
}

/**
 * @brief Retrieves a transaction by its hash.
 * @details This method iterates through all blocks and their transactions to find
 * a transaction with the matching hash.
 * @note This is a slow operation and should be used sparingly. In a production
 * system, an index would be used for efficient lookups.
 * @param tx_hash The hash of the transaction to find.
 * @return An `std::optional<Transaction>` containing the transaction if found.
 */
std::optional<Transaction> LedgerManager::get_transaction(const Hash &tx_hash) const {
  for (const auto &block : impl_->blocks_) {
    for (const auto &transaction : block.transactions) {
      if (compute_transaction_hash(transaction.message) == tx_hash) {
        return transaction;
      }
    }
  }
  return std::nullopt;
}

/**
 * @brief Retrieves all transactions from a specific slot.
 * @param slot The slot number to retrieve transactions from.
 * @return A vector of `Transaction` objects from the specified slot.
 */
std::vector<Transaction> LedgerManager::get_transactions_by_slot(common::Slot slot) const {
  if (auto block = get_block_by_slot(slot)) {
    return block->transactions;
  }
  return {};
}

/**
 * @brief Checks if a block's basic structure is valid.
 * @param block The block to validate.
 * @return True if the block is considered valid, false otherwise.
 */
bool LedgerManager::is_block_valid(const Block &block) const {
  return block.verify() && !block.block_hash.empty();
}

/**
 * @brief Performs a consistency check on the entire blockchain stored in memory.
 * @details Verifies that blocks are sorted by slot and that each block's parent
 * hash correctly points to the previous block's hash.
 * @return True if the chain is consistent, false otherwise.
 */
bool LedgerManager::is_chain_consistent() const {
  if (impl_->blocks_.empty()) return true;
  std::vector<Block> sorted_blocks = impl_->blocks_;
  std::sort(sorted_blocks.begin(), sorted_blocks.end(), [](const Block &a, const Block &b) { return a.slot < b.slot; });

  for (size_t i = 1; i < sorted_blocks.size(); ++i) {
    if (sorted_blocks[i].parent_hash != sorted_blocks[i - 1].block_hash) {
      return false;
    }
    if (sorted_blocks[i].slot <= sorted_blocks[i - 1].slot) {
      return false;
    }
    if (!sorted_blocks[i].verify()) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Compacts the ledger by removing old blocks.
 * @details This method is intended to be called periodically to prune the ledger
 * and manage disk space. It removes blocks older than a defined retention period.
 * @return A Result indicating the success or failure of the compaction.
 */
common::Result<bool> LedgerManager::compact_ledger() {
  std::cout << "Starting ledger compaction..." << std::endl;
  try {
    auto retention_limit = std::chrono::system_clock::now() - std::chrono::hours(24 * 30); // 30 days
    size_t blocks_before = impl_->blocks_.size();
    impl_->blocks_.erase(std::remove_if(impl_->blocks_.begin(), impl_->blocks_.end(),
      [&](const Block &block) {
        return std::chrono::system_clock::from_time_t(block.timestamp) < retention_limit &&
               block.slot < (impl_->latest_slot_ - 1000000);
      }),
      impl_->blocks_.end());

    if (!impl_->blocks_.empty()) {
      impl_->latest_slot_ = std::max_element(impl_->blocks_.begin(), impl_->blocks_.end(),
        [](const Block &a, const Block &b) { return a.slot < b.slot; })->slot;
    }
    std::cout << "Ledger compaction completed. Removed " << (blocks_before - impl_->blocks_.size()) << " old blocks." << std::endl;
    return common::Result<bool>(true);
  } catch (const std::exception &e) {
    return common::Result<bool>("Compaction failed: " + std::string(e.what()));
  }
}

/**
 * @brief Gets the total number of blocks currently stored in the ledger.
 * @return The number of blocks.
 */
uint64_t LedgerManager::get_ledger_size() const {
  return impl_->blocks_.size();
}

/**
 * @brief Computes a hash for a transaction message.
 * @note This is a simplified hashing function for demonstration. A production
 * implementation would use a cryptographically secure hash like SHA-256.
 * @param message The transaction message to hash.
 * @return A 32-byte hash of the message.
 */
std::vector<uint8_t>
compute_transaction_hash(const std::vector<uint8_t> &message) {
  std::vector<uint8_t> hash(32, 0);
  if (message.empty()) return hash;
  std::hash<std::string> hasher;
  std::string message_str(message.begin(), message.end());
  auto hash_value = hasher(message_str);
  for (int i = 0; i < 4; ++i) {
    uint64_t word = hash_value ^ (hash_value >> (i * 8));
    for (int j = 0; j < 8; ++j) {
      hash[i * 8 + j] = static_cast<uint8_t>((word >> (j * 8)) & 0xFF);
    }
  }
  return hash;
}

}
}