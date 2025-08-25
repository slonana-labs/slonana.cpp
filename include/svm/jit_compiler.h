#pragma once

#include "svm/engine.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <chrono>

namespace slonana {
namespace svm {

// Forward declarations
class JITCompiler;
class BytecodeOptimizer;
class NativeCodeCache;

enum class JITOptimizationLevel {
    NONE,        // No JIT compilation, interpret only
    BASIC,       // Basic optimizations
    AGGRESSIVE,  // Aggressive optimizations
    PROFILE_GUIDED // Profile-guided optimizations
};

enum class CompilationTrigger {
    IMMEDIATE,     // Compile immediately on first execution
    THRESHOLD,     // Compile after execution count threshold
    HOTSPOT,       // Compile when identified as hotspot
    MANUAL         // Manual compilation trigger
};

struct JITCompilationStats {
    uint64_t programs_compiled = 0;
    uint64_t compilation_time_ms = 0;
    uint64_t native_code_size_bytes = 0;
    uint64_t bytecode_size_bytes = 0;
    uint64_t cache_hits = 0;
    uint64_t cache_misses = 0;
    double compilation_speedup = 0.0;
    double execution_speedup = 0.0;
};

struct ProgramProfile {
    std::string program_id;
    uint64_t execution_count = 0;
    uint64_t total_execution_time_us = 0;
    uint64_t avg_execution_time_us = 0;
    uint64_t instruction_count = 0;
    uint64_t memory_access_count = 0;
    std::vector<uint64_t> hot_instructions;
    std::unordered_map<std::string, uint64_t> syscall_frequency;
    bool is_compiled = false;
    uint64_t compilation_timestamp = 0;
};

struct JITConfig {
    JITOptimizationLevel optimization_level = JITOptimizationLevel::BASIC;
    CompilationTrigger trigger = CompilationTrigger::THRESHOLD;
    uint64_t compilation_threshold = 100;
    bool enable_profiling = true;
    bool enable_caching = true;
    size_t max_cache_size_mb = 256;
    size_t max_compiled_programs = 1000;
    bool enable_aggressive_inlining = false;
    bool enable_loop_unrolling = false;
    bool enable_dead_code_elimination = true;
    bool enable_constant_folding = true;
    bool enable_bounds_check_elimination = false;
};

// Intermediate representation for JIT compilation
struct JITInstruction {
    uint8_t opcode;
    uint8_t dst_reg;
    uint8_t src_reg;
    int16_t offset;
    int32_t immediate;
    std::string comment;
    
    // Optimization hints
    bool is_hot = false;
    bool can_inline = false;
    bool bounds_check_needed = true;
    uint64_t execution_count = 0;
};

struct JITBasicBlock {
    std::vector<JITInstruction> instructions;
    std::vector<size_t> predecessors;
    std::vector<size_t> successors;
    bool is_hot_block = false;
    uint64_t execution_count = 0;
};

struct JITProgram {
    std::string program_id;
    std::vector<uint8_t> original_bytecode;
    std::vector<JITBasicBlock> basic_blocks;
    std::vector<uint8_t> native_code;
    void* native_function_ptr = nullptr;
    ProgramProfile profile;
    std::chrono::steady_clock::time_point compilation_time;
    bool is_optimized = false;
};

// Native code execution interface
class INativeExecutor {
public:
    virtual ~INativeExecutor() = default;
    virtual ExecutionResult execute_native(
        void* function_ptr,
        const std::vector<AccountInfo>& accounts,
        const std::vector<uint8_t>& instruction_data
    ) = 0;
    virtual bool supports_platform() const = 0;
    virtual std::string get_platform_name() const = 0;
};

// JIT compiler interface
class IJITBackend {
public:
    virtual ~IJITBackend() = default;
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual std::vector<uint8_t> compile_program(const JITProgram& program) = 0;
    virtual void* get_function_pointer(const std::vector<uint8_t>& native_code) = 0;
    virtual bool supports_optimization_level(JITOptimizationLevel level) const = 0;
    virtual std::string get_backend_name() const = 0;
};

// Bytecode optimizer for pre-compilation optimizations
class BytecodeOptimizer {
private:
    JITConfig config_;
    
public:
    BytecodeOptimizer(const JITConfig& config);
    
    std::vector<JITBasicBlock> optimize_bytecode(
        const std::vector<uint8_t>& bytecode,
        const ProgramProfile& profile
    );
    
private:
    std::vector<JITBasicBlock> build_control_flow_graph(const std::vector<uint8_t>& bytecode);
    void eliminate_dead_code(std::vector<JITBasicBlock>& blocks);
    void perform_constant_folding(std::vector<JITBasicBlock>& blocks);
    void inline_hot_functions(std::vector<JITBasicBlock>& blocks, const ProgramProfile& profile);
    void unroll_loops(std::vector<JITBasicBlock>& blocks);
    void eliminate_bounds_checks(std::vector<JITBasicBlock>& blocks);
    void optimize_register_allocation(std::vector<JITBasicBlock>& blocks);
    
    bool is_dead_instruction(const JITInstruction& instr, const std::vector<JITBasicBlock>& blocks);
    bool is_constant_expression(const JITInstruction& instr);
    bool can_eliminate_bounds_check(const JITInstruction& instr, const std::vector<JITBasicBlock>& blocks);
};

// Native code cache management
class NativeCodeCache {
private:
    std::unordered_map<std::string, std::unique_ptr<JITProgram>> cache_;
    mutable std::mutex cache_mutex_;
    size_t max_cache_size_mb_;
    size_t current_cache_size_bytes_ = 0;
    std::atomic<uint64_t> cache_hits_{0};
    std::atomic<uint64_t> cache_misses_{0};
    
public:
    NativeCodeCache(size_t max_size_mb);
    ~NativeCodeCache();
    
    void store_compiled_program(std::unique_ptr<JITProgram> program);
    JITProgram* get_compiled_program(const std::string& program_id);
    bool has_compiled_program(const std::string& program_id) const;
    void evict_program(const std::string& program_id);
    void clear_cache();
    
    size_t get_cache_size_bytes() const { return current_cache_size_bytes_; }
    uint64_t get_cache_hits() const { return cache_hits_.load(); }
    uint64_t get_cache_misses() const { return cache_misses_.load(); }
    double get_hit_ratio() const;
    
private:
    void evict_least_recently_used();
    size_t calculate_program_size(const JITProgram& program) const;
};

// Program profiler for collecting execution statistics
class ProgramProfiler {
private:
    std::unordered_map<std::string, ProgramProfile> profiles_;
    mutable std::mutex profiles_mutex_;
    bool profiling_enabled_ = true;
    
public:
    ProgramProfiler(bool enabled = true);
    
    void record_execution(
        const std::string& program_id,
        uint64_t execution_time_us,
        uint64_t instruction_count,
        const std::vector<std::string>& syscalls_used
    );
    
    void record_hot_instruction(const std::string& program_id, uint64_t instruction_offset);
    
    ProgramProfile get_profile(const std::string& program_id) const;
    std::vector<std::string> get_hot_programs(uint64_t min_execution_count = 50) const;
    
    void enable_profiling() { profiling_enabled_ = true; }
    void disable_profiling() { profiling_enabled_ = false; }
    bool is_profiling_enabled() const { return profiling_enabled_; }
    
    void clear_profiles();
    void export_profiles(const std::string& filename) const;
    void import_profiles(const std::string& filename);
};

// Bytecode registry for storing and retrieving program bytecode
class BytecodeRegistry {
private:
    std::unordered_map<std::string, std::vector<uint8_t>> bytecode_cache_;
    mutable std::mutex cache_mutex_;
    static std::unique_ptr<BytecodeRegistry> instance_;
    static std::mutex instance_mutex_;
    
    BytecodeRegistry() = default;
    
public:
    static BytecodeRegistry* get_instance();
    
    void register_program(const std::string& program_id, const std::vector<uint8_t>& bytecode);
    std::vector<uint8_t> get_program_bytecode(const std::string& program_id) const;
    bool has_program(const std::string& program_id) const;
    void remove_program(const std::string& program_id);
    void clear_all();
    
    std::vector<std::string> get_all_program_ids() const;
    size_t get_program_count() const;
};

// Main JIT compilation engine
class JITCompiler {
private:
    JITConfig config_;
    std::unique_ptr<IJITBackend> backend_;
    std::unique_ptr<INativeExecutor> executor_;
    std::unique_ptr<BytecodeOptimizer> optimizer_;
    std::unique_ptr<NativeCodeCache> cache_;
    std::unique_ptr<ProgramProfiler> profiler_;
    
    std::atomic<bool> compilation_enabled_{true};
    std::thread background_compiler_thread_;
    std::queue<std::string> compilation_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> shutdown_requested_{false};
    
    JITCompilationStats stats_;
    mutable std::mutex stats_mutex_;
    
public:
    JITCompiler(const JITConfig& config);
    ~JITCompiler();
    
    bool initialize();
    void shutdown();
    
    // Configuration
    void set_config(const JITConfig& config) { config_ = config; }
    JITConfig get_config() const { return config_; }
    void set_optimization_level(JITOptimizationLevel level);
    
    // Compilation control
    bool compile_program(const std::string& program_id, const std::vector<uint8_t>& bytecode);
    bool is_program_compiled(const std::string& program_id) const;
    void invalidate_program(const std::string& program_id);
    void trigger_compilation(const std::string& program_id);
    
    // Execution
    ExecutionResult execute_program(
        const std::string& program_id,
        const std::vector<uint8_t>& bytecode,
        const std::vector<AccountInfo>& accounts,
        const std::vector<uint8_t>& instruction_data
    );
    
    // Statistics and monitoring
    JITCompilationStats get_stats() const;
    std::vector<std::string> get_compiled_programs() const;
    ProgramProfile get_program_profile(const std::string& program_id) const;
    void reset_stats();
    
    // Cache management
    void clear_cache();
    size_t get_cache_size() const;
    double get_cache_hit_ratio() const;
    
    // Profiling
    void enable_profiling();
    void disable_profiling();
    std::vector<std::string> get_hot_programs() const;
    
private:
    void background_compiler_loop();
    bool should_compile_program(const std::string& program_id, const ProgramProfile& profile);
    void update_compilation_stats(uint64_t compilation_time_ms, size_t bytecode_size, size_t native_size);
    
    ExecutionResult execute_interpreted(
        const std::string& program_id,
        const std::vector<uint8_t>& bytecode,
        const std::vector<AccountInfo>& accounts,
        const std::vector<uint8_t>& instruction_data
    );
    
    ExecutionResult execute_compiled(
        JITProgram* program,
        const std::vector<AccountInfo>& accounts,
        const std::vector<uint8_t>& instruction_data
    );
};

// Factory for creating JIT backends
class JITBackendFactory {
public:
    static std::unique_ptr<IJITBackend> create_llvm_backend(const JITConfig& config);
    static std::unique_ptr<IJITBackend> create_cranelift_backend(const JITConfig& config);
    static std::unique_ptr<IJITBackend> create_native_backend(const JITConfig& config);
    static std::vector<std::string> get_available_backends();
    static bool is_backend_available(const std::string& backend_name);
};

// Utility functions for JIT operations
namespace jit_utils {
    bool is_jit_supported_platform();
    std::string get_target_triple();
    std::vector<std::string> get_cpu_features();
    size_t get_optimal_compilation_threshold();
    
    // Bytecode analysis
    bool is_loop_instruction(uint8_t opcode);
    bool is_branch_instruction(uint8_t opcode);
    bool is_syscall_instruction(uint8_t opcode);
    bool is_memory_instruction(uint8_t opcode);
    
    // Performance measurement
    uint64_t measure_execution_time(std::function<void()> func);
    double calculate_speedup(uint64_t interpreted_time, uint64_t compiled_time);
    
    // Platform-specific optimizations
    void configure_cpu_optimizations();
    void enable_branch_prediction_hints();
    void optimize_memory_layout();
}

}} // namespace slonana::svm