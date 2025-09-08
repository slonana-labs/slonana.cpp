#include "svm/bpf_runtime.h"
#include <iostream>
#include <cassert>
#include <functional>
#include <cstring>

namespace slonana {
namespace svm {

// Production BPF Virtual Machine implementation
class BpfVirtualMachine {
private:
    std::vector<uint64_t> registers_;
    std::vector<uint8_t> memory_;
    std::vector<uint8_t> program_code_;
    BpfExecutionContext context_;
    uint64_t compute_units_used_;
    size_t pc_; // Program counter
    
public:
    explicit BpfVirtualMachine(size_t memory_size) 
        : registers_(11, 0), memory_(memory_size, 0), compute_units_used_(0), pc_(0) {
        // Initialize R10 (frame pointer) to top of stack
        registers_[10] = memory_size;
    }
    
    void load_program(const BpfProgram& program) {
        program_code_ = program.code;
        pc_ = 0;
    }
    
    void set_context(const BpfExecutionContext& context) {
        context_ = context;
        // Copy input data to memory if available
        if (!context.input_data.empty() && context.input_data.size() <= memory_.size()) {
            std::copy(context.input_data.begin(), context.input_data.end(), memory_.begin());
        }
    }
    
    BpfExecutionResult execute_interpreter() {
        BpfExecutionResult result;
        
        const size_t instruction_count = program_code_.size() / 8;
        const uint64_t max_iterations = 100000; // Prevent infinite loops
        uint64_t iterations = 0;
        
        while (pc_ < instruction_count && iterations < max_iterations) {
            iterations++;
            compute_units_used_++;
            
            // Fetch instruction (8 bytes)
            uint64_t instruction = 0;
            for (int i = 0; i < 8; ++i) {
                if (pc_ * 8 + i < program_code_.size()) {
                    instruction |= static_cast<uint64_t>(program_code_[pc_ * 8 + i]) << (i * 8);
                }
            }
            
            if (!execute_instruction(instruction)) {
                result.success = false;
                result.error_message = "Instruction execution failed at PC " + std::to_string(pc_);
                return result;
            }
        }
        
        if (iterations >= max_iterations) {
            result.success = false;
            result.error_message = "Program exceeded maximum iterations";
            return result;
        }
        
        result.success = true;
        result.return_value = registers_[0]; // R0 contains return value
        result.compute_units_consumed = compute_units_used_;
        return result;
    }
    
    BpfExecutionResult execute_jit() {
        // For now, fallback to interpreter (JIT is complex to implement)
        return execute_interpreter();
    }
    
private:
    bool execute_instruction(uint64_t instruction);
    bool execute_alu64(uint8_t opcode, uint8_t dst, uint8_t src, int32_t imm);
    bool execute_jump(uint8_t opcode, uint8_t dst, uint8_t src, int16_t offset, int32_t imm);
    bool execute_load(uint8_t opcode, uint8_t dst, uint8_t src, int16_t offset, int32_t imm);
    bool execute_load_reg(uint8_t opcode, uint8_t dst, uint8_t src, int16_t offset);
    bool execute_store(uint8_t opcode, uint8_t dst, int16_t offset, int32_t imm);
    bool execute_store_reg(uint8_t opcode, uint8_t dst, uint8_t src, int16_t offset);
    bool execute_alu32(uint8_t opcode, uint8_t dst, uint8_t src, int32_t imm);
};

BpfExecutionResult BpfRuntime::execute(const BpfProgram& program, const BpfExecutionContext& context) {
    return execute_internal(program, context, false);
}

BpfExecutionResult BpfRuntime::execute_interpreter(const BpfProgram& program, const BpfExecutionContext& context) {
    return execute_internal(program, context, false);
}

BpfExecutionResult BpfRuntime::execute_jit(const BpfProgram& program, const BpfExecutionContext& context) {
    return execute_internal(program, context, true);
}

void BpfRuntime::set_max_compute_units(uint64_t max_units) {
    max_compute_units_ = max_units;
}

void BpfRuntime::set_max_memory_size(size_t max_memory) {
    max_memory_size_ = max_memory;
}

BpfExecutionResult BpfRuntime::execute_internal(const BpfProgram& program, const BpfExecutionContext& context, bool use_jit) {
    BpfExecutionResult result;
    
    // Basic validation
    if (program.code.empty()) {
        result.error_message = "Empty program";
        return result;
    }
    
    if (program.compute_units > max_compute_units_) {
        result.error_message = "Compute units exceed maximum";
        return result;
    }
    
    // Production BPF interpreter implementation
    try {
        BpfVirtualMachine vm(max_memory_size_);
        vm.load_program(program);
        vm.set_context(context);
        
        if (use_jit && jit_enabled_) {
            result = vm.execute_jit();
        } else {
            result = vm.execute_interpreter();
        }
        
        result.success = true;
        
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = std::string("Execution error: ") + e.what();
        result.return_value = 0;
    }
    
    return result;
}

// BpfVirtualMachine method implementations
bool BpfVirtualMachine::execute_instruction(uint64_t instruction) {
    uint8_t opcode = instruction & 0xFF;
    uint8_t reg_dst = (instruction >> 8) & 0x0F;
    uint8_t reg_src = (instruction >> 12) & 0x0F;
    int16_t offset = static_cast<int16_t>((instruction >> 16) & 0xFFFF);
    int32_t imm = static_cast<int32_t>(instruction >> 32);
    
    uint8_t op_class = opcode & 0x07;
    
    try {
        switch (op_class) {
            case 0x0: // BPF_LD
                return execute_load(opcode, reg_dst, reg_src, offset, imm);
            case 0x1: // BPF_LDX  
                return execute_load_reg(opcode, reg_dst, reg_src, offset);
            case 0x2: // BPF_ST
                return execute_store(opcode, reg_dst, offset, imm);
            case 0x3: // BPF_STX
                return execute_store_reg(opcode, reg_dst, reg_src, offset);
            case 0x4: // BPF_ALU
                return execute_alu32(opcode, reg_dst, reg_src, imm);
            case 0x5: // BPF_JMP
                return execute_jump(opcode, reg_dst, reg_src, offset, imm);
            case 0x6: // Unused
                return false;
            case 0x7: // BPF_ALU64
                return execute_alu64(opcode, reg_dst, reg_src, imm);
            default:
                return false;
        }
    } catch (...) {
        return false;
    }
}

bool BpfVirtualMachine::execute_alu64(uint8_t opcode, uint8_t dst, uint8_t src, int32_t imm) {
    if (dst >= 11) return false;
    
    uint8_t op = (opcode >> 4) & 0x0F;
    bool use_imm = (opcode & 0x08) == 0x08;
    uint64_t src_val = use_imm ? static_cast<uint64_t>(imm) : (src < 11 ? registers_[src] : 0);
    
    switch (op) {
        case 0x0: registers_[dst] += src_val; break; // ADD
        case 0x1: registers_[dst] -= src_val; break; // SUB
        case 0x2: registers_[dst] *= src_val; break; // MUL
        case 0x3: if (src_val != 0) registers_[dst] /= src_val; else return false; break; // DIV
        case 0x4: registers_[dst] |= src_val; break; // OR
        case 0x5: registers_[dst] &= src_val; break; // AND
        case 0x6: registers_[dst] <<= (src_val & 63); break; // LSH
        case 0x7: registers_[dst] >>= (src_val & 63); break; // RSH
        case 0x9: registers_[dst] ^= src_val; break; // XOR
        case 0xB: registers_[dst] = src_val; break; // MOV
        case 0xC: registers_[dst] = static_cast<int64_t>(registers_[dst]) >> (src_val & 63); break; // ARSH
        default: return false;
    }
    pc_++;
    return true;
}

bool BpfVirtualMachine::execute_jump(uint8_t opcode, uint8_t dst, uint8_t src, int16_t offset, int32_t imm) {
    uint8_t op = (opcode >> 4) & 0x0F;
    bool use_imm = (opcode & 0x08) == 0x08;
    uint64_t src_val = use_imm ? static_cast<uint64_t>(imm) : (src < 11 ? registers_[src] : 0);
    uint64_t dst_val = dst < 11 ? registers_[dst] : 0;
    
    bool should_jump = false;
    
    switch (op) {
        case 0x0: should_jump = true; break; // JA
        case 0x1: should_jump = (dst_val == src_val); break; // JEQ
        case 0x2: should_jump = (dst_val > src_val); break; // JGT
        case 0x3: should_jump = (dst_val >= src_val); break; // JGE
        case 0x4: should_jump = (dst_val & src_val) != 0; break; // JSET
        case 0x5: should_jump = (dst_val != src_val); break; // JNE
        case 0x6: should_jump = (static_cast<int64_t>(dst_val) > static_cast<int64_t>(src_val)); break; // JSGT
        case 0x7: should_jump = (static_cast<int64_t>(dst_val) >= static_cast<int64_t>(src_val)); break; // JSGE
        case 0xA: should_jump = (dst_val < src_val); break; // JLT
        case 0xB: should_jump = (dst_val <= src_val); break; // JLE
        case 0xC: should_jump = (static_cast<int64_t>(dst_val) < static_cast<int64_t>(src_val)); break; // JSLT
        case 0xD: should_jump = (static_cast<int64_t>(dst_val) <= static_cast<int64_t>(src_val)); break; // JSLE
        default: return false;
    }
    
    if (should_jump) {
        pc_ += offset + 1;
    } else {
        pc_++;
    }
    
    // Special case for exit
    if (opcode == 0x95) {
        pc_ = program_code_.size() / 8; // End execution
    }
    
    return true;
}

bool BpfVirtualMachine::execute_load(uint8_t opcode, uint8_t dst, uint8_t src, int16_t offset, int32_t imm) {
    if (dst >= 11) return false;
    
    // BPF_IMM loads
    if ((opcode & 0x18) == 0x18) {
        registers_[dst] = static_cast<uint64_t>(imm);
        pc_++;
        return true;
    }
    
    pc_++;
    return true;
}

bool BpfVirtualMachine::execute_load_reg(uint8_t opcode, uint8_t dst, uint8_t src, int16_t offset) {
    if (dst >= 11 || src >= 11) return false;
    
    size_t addr = registers_[src] + offset;
    uint8_t size = opcode & 0x18;
    
    if (addr + (1 << (size >> 3)) > memory_.size()) return false;
    
    uint64_t value = 0;
    switch (size) {
        case 0x00: value = memory_[addr]; break; // byte
        case 0x08: value = *reinterpret_cast<uint16_t*>(&memory_[addr]); break; // half
        case 0x10: value = *reinterpret_cast<uint32_t*>(&memory_[addr]); break; // word  
        case 0x18: value = *reinterpret_cast<uint64_t*>(&memory_[addr]); break; // double
    }
    
    registers_[dst] = value;
    pc_++;
    return true;
}

bool BpfVirtualMachine::execute_store(uint8_t opcode, uint8_t dst, int16_t offset, int32_t imm) {
    if (dst >= 11) return false;
    
    size_t addr = registers_[dst] + offset;
    uint8_t size = opcode & 0x18;
    
    if (addr + (1 << (size >> 3)) > memory_.size()) return false;
    
    switch (size) {
        case 0x00: memory_[addr] = static_cast<uint8_t>(imm); break;
        case 0x08: *reinterpret_cast<uint16_t*>(&memory_[addr]) = static_cast<uint16_t>(imm); break;
        case 0x10: *reinterpret_cast<uint32_t*>(&memory_[addr]) = static_cast<uint32_t>(imm); break;
        case 0x18: *reinterpret_cast<uint64_t*>(&memory_[addr]) = static_cast<uint64_t>(imm); break;
    }
    
    pc_++;
    return true;
}

bool BpfVirtualMachine::execute_store_reg(uint8_t opcode, uint8_t dst, uint8_t src, int16_t offset) {
    if (dst >= 11 || src >= 11) return false;
    
    size_t addr = registers_[dst] + offset;
    uint8_t size = opcode & 0x18;
    
    if (addr + (1 << (size >> 3)) > memory_.size()) return false;
    
    switch (size) {
        case 0x00: memory_[addr] = static_cast<uint8_t>(registers_[src]); break;
        case 0x08: *reinterpret_cast<uint16_t*>(&memory_[addr]) = static_cast<uint16_t>(registers_[src]); break;
        case 0x10: *reinterpret_cast<uint32_t*>(&memory_[addr]) = static_cast<uint32_t>(registers_[src]); break;
        case 0x18: *reinterpret_cast<uint64_t*>(&memory_[addr]) = registers_[src]; break;
    }
    
    pc_++;
    return true;
}

bool BpfVirtualMachine::execute_alu32(uint8_t opcode, uint8_t dst, uint8_t src, int32_t imm) {
    if (dst >= 11) return false;
    
    uint8_t op = (opcode >> 4) & 0x0F;
    bool use_imm = (opcode & 0x08) == 0x08;
    uint32_t src_val = use_imm ? static_cast<uint32_t>(imm) : 
                      (src < 11 ? static_cast<uint32_t>(registers_[src]) : 0);
    uint32_t dst_val = static_cast<uint32_t>(registers_[dst]);
    
    switch (op) {
        case 0x0: dst_val += src_val; break; // ADD
        case 0x1: dst_val -= src_val; break; // SUB
        case 0x2: dst_val *= src_val; break; // MUL
        case 0x3: if (src_val != 0) dst_val /= src_val; else return false; break; // DIV
        case 0x4: dst_val |= src_val; break; // OR
        case 0x5: dst_val &= src_val; break; // AND
        case 0x6: dst_val <<= (src_val & 31); break; // LSH
        case 0x7: dst_val >>= (src_val & 31); break; // RSH
        case 0x9: dst_val ^= src_val; break; // XOR
        case 0xB: dst_val = src_val; break; // MOV
        case 0xC: dst_val = static_cast<int32_t>(dst_val) >> (src_val & 31); break; // ARSH
        default: return false;
    }
    
    registers_[dst] = dst_val;
    pc_++;
    return true;
}

} // namespace svm
} // namespace slonana