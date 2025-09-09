#pragma once

#include "svm/engine.h"
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>

namespace slonana {
namespace svm {

/**
 * Enhanced execution engine with parallel processing capabilities
 *
 * This demonstrates the enhancement concepts outlined in the roadmap
 * while maintaining compatibility with the existing SVM engine.
 */
class EnhancedExecutionEngine : public ExecutionEngine {
public:
  EnhancedExecutionEngine();
  ~EnhancedExecutionEngine();

  // Enhanced parallel transaction execution
  ExecutionOutcome execute_parallel_transactions(
      const std::vector<std::vector<Instruction>> &transaction_batches,
      std::unordered_map<PublicKey, ProgramAccount> &accounts);

  // Program caching with JIT compilation simulation
  Result<bool> load_and_cache_program(const ProgramAccount &program);
  bool is_program_cached(const PublicKey &program_id) const;

  // Memory pool management
  void enable_memory_pooling(bool enabled = true);
  size_t get_pool_utilization() const;

  // Performance metrics
  struct PerformanceMetrics {
    uint64_t parallel_transactions_executed = 0;
    uint64_t cache_hits = 0;
    uint64_t cache_misses = 0;
    uint64_t pool_allocations = 0;
    uint64_t pool_reuses = 0;
    double average_execution_time_ms = 0.0;
  };

  PerformanceMetrics get_performance_metrics() const;

private:
  // Dependency analysis for parallel execution
  struct DependencyGraph {
    std::vector<std::vector<size_t>> independent_groups;
    std::unordered_set<PublicKey> read_accounts;
    std::unordered_set<PublicKey> write_accounts;
  };

  DependencyGraph
  analyze_dependencies(const std::vector<Instruction> &instructions) const;

  // Thread pool for parallel execution
  class ThreadPool {
  public:
    ThreadPool(size_t num_threads);
    ~ThreadPool();

    template <typename F>
    auto enqueue(F &&f) -> std::future<typename std::result_of<F()>::type>;

    void shutdown();

  private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_;
  };

  // Program cache with LRU eviction
  struct CachedProgram {
    ProgramAccount program;
    std::chrono::time_point<std::chrono::steady_clock> last_accessed;
    uint64_t execution_count;
    bool jit_compiled; // Simulation of JIT compilation
  };

  mutable std::unordered_map<PublicKey, CachedProgram> program_cache_;
  mutable std::mutex cache_mutex_;
  static constexpr size_t MAX_CACHE_SIZE = 1000;

  // Memory pool for account state
  struct PooledAccount {
    ProgramAccount account;
    bool in_use;
    std::chrono::time_point<std::chrono::steady_clock> allocated_time;
  };

  std::vector<std::unique_ptr<PooledAccount>> account_pool_;
  std::queue<PooledAccount *> available_accounts_;
  mutable std::mutex pool_mutex_;
  bool memory_pooling_enabled_;

  // Thread pool instance
  std::unique_ptr<ThreadPool> thread_pool_;

  // Performance tracking
  mutable PerformanceMetrics metrics_;
  mutable std::mutex metrics_mutex_;

  // Helper methods
  PooledAccount *acquire_pooled_account();
  void release_pooled_account(PooledAccount *account);
  void evict_cold_programs();

  ExecutionOutcome execute_instruction_group(
      const std::vector<Instruction> &instructions,
      std::unordered_map<PublicKey, ProgramAccount> &accounts);
};

/**
 * Enhanced SPL Token Program implementation
 *
 * Demonstrates how additional builtin programs can be added
 * to extend the SVM ecosystem.
 */
class SPLTokenProgram : public BuiltinProgram {
public:
  static const PublicKey TOKEN_PROGRAM_ID;
  static constexpr size_t MINT_ACCOUNT_SIZE = 82;
  static constexpr size_t TOKEN_ACCOUNT_SIZE = 165;

  SPLTokenProgram();
  ~SPLTokenProgram() override = default;

  PublicKey get_program_id() const override;

  ExecutionOutcome execute(const Instruction &instruction,
                           ExecutionContext &context) const override;

private:
  enum class TokenInstruction : uint8_t {
    InitializeMint = 0,
    InitializeAccount = 1,
    InitializeMultisig = 2,
    Transfer = 3,
    Approve = 4,
    Revoke = 5,
    SetAuthority = 6,
    MintTo = 7,
    Burn = 8,
    CloseAccount = 9,
    FreezeAccount = 10,
    ThawAccount = 11,
    TransferChecked = 12,
    ApproveChecked = 13,
    MintToChecked = 14,
    BurnChecked = 15
  };

  struct MintAccount {
    uint64_t supply;
    uint8_t decimals;
    bool is_initialized;
    PublicKey mint_authority;
    PublicKey freeze_authority;
  };

  struct TokenAccount {
    PublicKey mint;
    PublicKey owner;
    uint64_t amount;
    bool is_initialized;
    bool is_frozen;
    PublicKey delegate;
    uint64_t delegated_amount;
    uint8_t state;  // Added missing field
    bool is_native; // Added missing field
  };

  // Instruction handlers
  ExecutionOutcome handle_initialize_mint(const Instruction &instruction,
                                          ExecutionContext &context) const;

  ExecutionOutcome handle_initialize_account(const Instruction &instruction,
                                             ExecutionContext &context) const;

  ExecutionOutcome handle_transfer(const Instruction &instruction,
                                   ExecutionContext &context) const;

  ExecutionOutcome handle_mint_to(const Instruction &instruction,
                                  ExecutionContext &context) const;

  ExecutionOutcome handle_burn(const Instruction &instruction,
                               ExecutionContext &context) const;

  // Utility methods
  Result<MintAccount> deserialize_mint(const std::vector<uint8_t> &data) const;
  Result<TokenAccount>
  deserialize_token_account(const std::vector<uint8_t> &data) const;
  std::vector<uint8_t> serialize_mint(const MintAccount &mint) const;
  std::vector<uint8_t>
  serialize_token_account(const TokenAccount &account) const;
};

} // namespace svm
} // namespace slonana