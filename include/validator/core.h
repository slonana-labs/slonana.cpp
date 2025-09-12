#pragma once

#include "banking/banking_stage.h"
#include "common/types.h"
#include "ledger/manager.h"
#include "network/quic_client.h"
#include "network/quic_server.h"
#include <functional>
#include <memory>

namespace slonana {
namespace validator {

using namespace slonana::common;

/**
 * Vote structure for consensus participation
 */
struct Vote {
  Slot slot;
  Hash block_hash;
  PublicKey validator_identity;
  Signature vote_signature;
  uint64_t timestamp;

  std::vector<uint8_t> serialize() const;
  bool verify() const;
};

/**
 * Fork choice algorithm for determining the canonical chain
 */
class ForkChoice {
public:
  ForkChoice();
  ~ForkChoice();

  // Add blocks to the fork choice
  void add_block(const ledger::Block &block);
  void add_vote(const Vote &vote);

  // Get the canonical chain head
  Hash get_head() const;
  Slot get_head_slot() const;

  // Fork management
  std::vector<Hash> get_forks() const;
  uint64_t get_fork_weight(const Hash &fork_head) const;
  uint64_t get_validator_stake(const Hash &validator_pubkey) const;
  uint64_t get_confirmation_depth(const Hash &fork_head) const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;

  // Production-grade fork choice helper methods
  uint64_t calculate_consensus_weight(const ledger::Block &block) const;
  void update_consensus_weights() const;
  Hash select_best_fork_head() const;
  bool is_ancestor(const Hash &potential_ancestor,
                   const Hash &descendant) const;
};

/**
 * Block validation logic
 */
class BlockValidator {
public:
  explicit BlockValidator(std::shared_ptr<ledger::LedgerManager> ledger);
  ~BlockValidator();

  // Validate individual components
  bool validate_block_structure(const ledger::Block &block) const;
  bool validate_block_signature(const ledger::Block &block) const;
  bool validate_transactions(const ledger::Block &block) const;

  // Full block validation
  Result<bool> validate_block(const ledger::Block &block) const;

  // Chain validation
  bool validate_chain_continuity(const ledger::Block &block) const;

private:
  std::shared_ptr<ledger::LedgerManager> ledger_;
  class Impl;
  std::unique_ptr<Impl> impl_;
};

/**
 * Core validator logic orchestrating consensus participation
 */
class ValidatorCore {
public:
  using VoteCallback = std::function<void(const Vote &)>;
  using BlockCallback = std::function<void(const ledger::Block &)>;

  ValidatorCore(std::shared_ptr<ledger::LedgerManager> ledger,
                const PublicKey &validator_identity);
  ~ValidatorCore();

  // Start/stop validator operations
  Result<bool> start();
  void stop();

  // Process incoming blocks and votes
  void process_block(const ledger::Block &block);
  void process_vote(const Vote &vote);

  // Transaction processing
  void process_transaction(std::shared_ptr<ledger::Transaction> transaction);
  void process_transactions(
      std::vector<std::shared_ptr<ledger::Transaction>> transactions);

  // Register callbacks
  void set_vote_callback(VoteCallback callback);
  void set_block_callback(BlockCallback callback);

  // Validator status
  bool is_running() const;
  Slot get_current_slot() const;         // Returns PoH-driven current slot
  Slot get_blockchain_head_slot() const; // Returns highest processed block slot
  Hash get_current_head() const;

  // Getter methods for RPC server
  PublicKey get_validator_identity() const { return validator_identity_; }
  Result<std::vector<uint8_t>> get_genesis_block() const;
  std::string get_slot_leader(Slot slot) const;

  // Banking stage access
  banking::BankingStage *get_banking_stage() { return banking_stage_.get(); }
  const banking::BankingStage *get_banking_stage() const {
    return banking_stage_.get();
  }

  // QUIC networking
  bool enable_quic_networking(uint16_t port = 8000);
  bool disable_quic_networking();
  network::QuicServer *get_quic_server() { return quic_server_.get(); }
  network::QuicClient *get_quic_client() { return quic_client_.get(); }

  // Performance monitoring
  banking::BankingStage::Statistics get_transaction_statistics() const;
  network::QuicServer::Statistics get_quic_statistics() const;

private:
  std::shared_ptr<ledger::LedgerManager> ledger_;
  std::unique_ptr<ForkChoice> fork_choice_;
  std::unique_ptr<BlockValidator> block_validator_;
  PublicKey validator_identity_;

  // Enhanced banking stage for transaction processing
  std::unique_ptr<banking::BankingStage> banking_stage_;

  // QUIC networking components
  std::unique_ptr<network::QuicServer> quic_server_;
  std::unique_ptr<network::QuicClient> quic_client_;
  bool quic_enabled_;

  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace validator
} // namespace slonana