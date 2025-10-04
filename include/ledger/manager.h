/**
 * @file manager.h
 * @brief Defines the core data structures for the ledger, including transactions
 * and blocks, and the LedgerManager class for persistent storage.
 */
#pragma once

#include "common/types.h"
#include <memory>
#include <optional>
#include <vector>

namespace slonana {
namespace ledger {

using namespace slonana::common;

/**
 * @brief Represents a single transaction in the ledger.
 * @details Contains the signatures, the message payload (instructions), and the
 * transaction hash.
 */
struct Transaction {
  /// @brief A list of signatures, one for each required signer of the message.
  std::vector<Signature> signatures;
  /// @brief The serialized transaction message containing instructions and account keys.
  std::vector<uint8_t> message;
  /// @brief The unique hash identifying this transaction.
  Hash hash;

  Transaction() = default;
  explicit Transaction(const std::vector<uint8_t> &raw_data);

  std::vector<uint8_t> serialize() const;
  bool verify() const;
  
  /**
   * @brief Performs basic validation checks on the transaction structure.
   * @return True if the transaction has a valid structure, false otherwise.
   */
  bool is_valid() const;
  
  /**
   * @brief Gets the number of signatures in the transaction.
   * @return The count of signatures.
   */
  size_t signature_count() const;
  
  /**
   * @brief Gets the size of the transaction message.
   * @return The size of the message in bytes.
   */
  size_t message_size() const;
};

/**
 * @brief Represents a block in the blockchain.
 * @details A block contains a list of transactions, metadata such as the slot and
 * timestamp, and cryptographic hashes that link it to the previous block.
 */
struct Block {
  /// @brief The hash of the parent block, linking this block to the previous one.
  Hash parent_hash;
  /// @brief The hash of this block, calculated from its contents.
  Hash block_hash;
  /// @brief The slot number in which this block was produced.
  Slot slot;
  /// @brief The list of transactions included in this block.
  std::vector<Transaction> transactions;
  /// @brief The timestamp when the block was created.
  uint64_t timestamp;
  /// @brief The public key of the validator that produced this block.
  PublicKey validator;
  /// @brief The signature of the block hash, signed by the producing validator.
  Signature block_signature;

  Block() = default;
  explicit Block(const std::vector<uint8_t> &raw_data);

  std::vector<uint8_t> serialize() const;
  bool verify() const;
  Hash compute_hash() const;
};

/**
 * @brief Manages the persistent storage and retrieval of blocks and transactions.
 * @details Provides an API for interacting with the blockchain data stored on disk,
 * including storing new blocks, fetching existing blocks and transactions, and
 * performing maintenance operations.
 */
class LedgerManager {
public:
  explicit LedgerManager(const std::string &ledger_path);
  ~LedgerManager();

  common::Result<bool> store_block(const Block &block);
  std::optional<Block> get_block(const Hash &block_hash) const;
  std::optional<Block> get_block_by_slot(Slot slot) const;

  Hash get_latest_block_hash() const;
  Slot get_latest_slot() const;
  std::vector<Hash> get_block_chain(const Hash &from_hash, size_t count) const;

  std::optional<Transaction> get_transaction(const Hash &tx_hash) const;
  std::vector<Transaction> get_transactions_by_slot(Slot slot) const;

  bool is_block_valid(const Block &block) const;
  bool is_chain_consistent() const;

  common::Result<bool> compact_ledger();
  uint64_t get_ledger_size() const;

private:
  // PIMPL idiom to hide implementation details
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ledger
} // namespace slonana