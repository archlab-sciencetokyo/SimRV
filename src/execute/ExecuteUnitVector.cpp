#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/isa/Common.hpp"
#include <algorithm>
#include <type_traits>

namespace simrv::execute {

namespace {

// Helper to check/get active elements under mask
inline bool is_element_active(const core::VectorRegister& mask_reg, uint32_t i, bool vm) {
    if (vm) return true;
    return (mask_reg.u8[i / 8] & (1u << (i % 8))) != 0;
}

// Helper to write mask bits
inline void set_mask_bit(core::VectorRegister& dest, uint32_t i, bool val) {
    uint32_t byte_idx = i / 8;
    uint32_t bit_idx = i % 8;
    if (val) {
        dest.u8[byte_idx] |= (1u << bit_idx);
    } else {
        dest.u8[byte_idx] &= ~(1u << bit_idx);
    }
}

// Helper to read group element
template <typename T>
inline T get_group_element(const core::RegisterFile& regs, RegId base_reg, uint32_t i) {
    constexpr uint32_t elems_per_reg = 32 / sizeof(T);
    uint32_t reg_offset = i / elems_per_reg;
    uint32_t elem_idx = i % elems_per_reg;
    RegId actual_reg = static_cast<RegId>((std::to_underlying(base_reg) + reg_offset) & 0x1F);
    const auto& vreg = regs.read_vector(actual_reg);

    if constexpr (sizeof(T) == 1) {
        return static_cast<T>(vreg.u8[elem_idx]);
    } else if constexpr (sizeof(T) == 2) {
        return static_cast<T>(vreg.u16[elem_idx]);
    } else if constexpr (sizeof(T) == 4) {
        return static_cast<T>(vreg.u32[elem_idx]);
    } else {
        return static_cast<T>(vreg.u64[elem_idx]);
    }
}

// Helper to write group element
template <typename T>
inline void set_group_element(core::RegisterFile& regs, RegId base_reg, uint32_t i, T val) {
    constexpr uint32_t elems_per_reg = 32 / sizeof(T);
    uint32_t reg_offset = i / elems_per_reg;
    uint32_t elem_idx = i % elems_per_reg;
    RegId actual_reg = static_cast<RegId>((std::to_underlying(base_reg) + reg_offset) & 0x1F);
    auto& vreg = regs.read_vector(actual_reg);

    if constexpr (sizeof(T) == 1) {
        vreg.u8[elem_idx] = static_cast<uint8_t>(val);
    } else if constexpr (sizeof(T) == 2) {
        vreg.u16[elem_idx] = static_cast<uint16_t>(val);
    } else if constexpr (sizeof(T) == 4) {
        vreg.u32[elem_idx] = static_cast<uint32_t>(val);
    } else {
        vreg.u64[elem_idx] = static_cast<uint64_t>(val);
    }
}

// Standard templates for vector ALU operations
template <typename T, typename Op>
void perform_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, Op op) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        T val1 = get_group_element<T>(cpu.state().regs, rs1, i);
        T val2 = get_group_element<T>(cpu.state().regs, rs2, i);
        set_group_element<T>(cpu.state().regs, rd, i, op(val2, val1));
    }
}

template <typename T, typename Op>
void perform_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl, Op op) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(rs1_val);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        T val2 = get_group_element<T>(cpu.state().regs, rs2, i);
        set_group_element<T>(cpu.state().regs, rd, i, op(val2, val1));
    }
}

template <typename T, typename Op>
void perform_vi(core::CPU& cpu, RegId rd, int32_t imm, RegId rs2, bool vm, uint32_t vl, Op op) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(imm);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        T val2 = get_group_element<T>(cpu.state().regs, rs2, i);
        set_group_element<T>(cpu.state().regs, rd, i, op(val2, val1));
    }
}

// Vector comparisons
template <typename T, typename Op>
void perform_compare_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, Op op) {
    auto& dest = cpu.state().regs.read_vector(rd);
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        T val1 = get_group_element<T>(cpu.state().regs, rs1, i);
        T val2 = get_group_element<T>(cpu.state().regs, rs2, i);
        bool res = op(val2, val1);
        set_mask_bit(dest, i, res);
    }
}

template <typename T, typename Op>
void perform_compare_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl, Op op) {
    auto& dest = cpu.state().regs.read_vector(rd);
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(rs1_val);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        T val2 = get_group_element<T>(cpu.state().regs, rs2, i);
        bool res = op(val2, val1);
        set_mask_bit(dest, i, res);
    }
}

template <typename T, typename Op>
void perform_compare_vi(core::CPU& cpu, RegId rd, int32_t imm, RegId rs2, bool vm, uint32_t vl, Op op) {
    auto& dest = cpu.state().regs.read_vector(rd);
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(imm);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        T val2 = get_group_element<T>(cpu.state().regs, rs2, i);
        bool res = op(val2, val1);
        set_mask_bit(dest, i, res);
    }
}

// Vector load helper
template <typename T>
void execute_vle(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId rd, Register base_addr, bool vm, uint32_t vl, isa::Funct3 mem_f3) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        Address addr = base_addr + i * sizeof(T);
        Word val = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, mem_f3);
        if (cpu.pipeline_context.pending_exception.has_value()) {
            return;
        }
        set_group_element<T>(cpu.state().regs, rd, i, static_cast<T>(val));
    }
}

// Vector store helper
template <typename T>
void execute_vse(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId vs3, Register base_addr, bool vm, uint32_t vl, isa::Funct3 mem_f3) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        Address addr = base_addr + i * sizeof(T);
        T val = get_group_element<T>(cpu.state().regs, vs3, i);
        simrv::memory::MemoryAccess::storeInt(mem, cpu, addr, static_cast<Word>(val), mem_f3);
        if (cpu.pipeline_context.pending_exception.has_value()) {
            return;
        }
    }
}

template <typename T>
void execute_vmerge_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = 0; i < vl; i++) {
        bool mask_bit = (mask_reg.u8[i / 8] & (1u << (i % 8))) != 0;
        T val = mask_bit ? get_group_element<T>(cpu.state().regs, rs1, i) : get_group_element<T>(cpu.state().regs, rs2, i);
        set_group_element<T>(cpu.state().regs, rd, i, val);
    }
}

template <typename T>
void execute_vmerge_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(rs1_val);

    for (uint32_t i = 0; i < vl; i++) {
        bool mask_bit = (mask_reg.u8[i / 8] & (1u << (i % 8))) != 0;
        T val = mask_bit ? val1 : get_group_element<T>(cpu.state().regs, rs2, i);
        set_group_element<T>(cpu.state().regs, rd, i, val);
    }
}

template <typename T>
void execute_vmerge_vi(core::CPU& cpu, RegId rd, int32_t imm, RegId rs2, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(imm);

    for (uint32_t i = 0; i < vl; i++) {
        bool mask_bit = (mask_reg.u8[i / 8] & (1u << (i % 8))) != 0;
        T val = mask_bit ? val1 : get_group_element<T>(cpu.state().regs, rs2, i);
        set_group_element<T>(cpu.state().regs, rd, i, val);
    }
}

} // namespace

void ExecuteUnit::execute_vector(core::CPU& cpu, core::Machine& machine, isa::OperationId op_id, Instruction ir) {
    const auto rd = static_cast<RegId>((ir >> 7) & 0x1F);
    const auto rs1 = static_cast<RegId>((ir >> 15) & 0x1F);
    const auto rs2 = static_cast<RegId>((ir >> 20) & 0x1F);
    const bool vm = ((ir >> 25) & 1) != 0;

    // Decode immediate fields
    auto simm5 = static_cast<int32_t>((ir >> 15) & 0x1F);
    if ((simm5 & 0x10) != 0) {
        simm5 |= ~0x1F;
    }

    uint32_t vl = cpu.state().vl;
    uint32_t sew = 8 << ((cpu.state().vtype >> 3) & 0x7);

    // Execute Vector Configuration instructions
    if (op_id == isa::OperationId::VSETVLI || op_id == isa::OperationId::VSETIVLI || op_id == isa::OperationId::VSETVL) {
        uint32_t vtype = 0;
        if (op_id == isa::OperationId::VSETVLI || op_id == isa::OperationId::VSETIVLI) {
            vtype = (ir >> 20) & 0x7FF;
        } else {
            vtype = static_cast<uint32_t>(cpu.state().regs.read(rs2));
        }

        uint32_t req_sew = 8 << ((vtype >> 3) & 0x7);
        uint32_t lmul_field = vtype & 0x7u;
        double lmul = 1.0;
        if (lmul_field == 0) lmul = 1.0;
        else if (lmul_field == 1) lmul = 2.0;
        else if (lmul_field == 2) lmul = 4.0;
        else if (lmul_field == 3) lmul = 8.0;
        else if (lmul_field == 5) lmul = 0.125;
        else if (lmul_field == 6) lmul = 0.25;
        else if (lmul_field == 7) lmul = 0.5;

        auto vlmax = static_cast<uint32_t>((256.0 / req_sew) * lmul);
        uint32_t new_vl = 0;

        if (op_id == isa::OperationId::VSETIVLI) {
            uint32_t uimm = (ir >> 15) & 0x1F;
            new_vl = std::min(uimm, vlmax);
        } else {
            if (rs1 == RegId::Zero) {
                new_vl = vlmax;
            } else {
                new_vl = std::min(static_cast<uint32_t>(cpu.state().regs.read(rs1)), vlmax);
            }
        }

        cpu.state().vl = new_vl;
        cpu.state().vtype = vtype;
        cpu.state().regs.write(rd, new_vl);

        cpu.state().mstatus |= enum_mask(core::MstatusBit::Vs);
        return;
    }

    // Execute Vector Load/Store instructions
    if (op_id == isa::OperationId::VLE8_V) {
        execute_vle<uint8_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), vm, vl, isa::Funct3::Lbu);
        return;
    }
    if (op_id == isa::OperationId::VLE16_V) {
        execute_vle<uint16_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), vm, vl, isa::Funct3::Lhu);
        return;
    }
    if (op_id == isa::OperationId::VLE32_V) {
        execute_vle<uint32_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), vm, vl, isa::Funct3::Lw);
        return;
    }
    if (op_id == isa::OperationId::VSE8_V) {
        execute_vse<uint8_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), vm, vl, isa::Funct3::Sb);
        return;
    }
    if (op_id == isa::OperationId::VSE16_V) {
        execute_vse<uint16_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), vm, vl, isa::Funct3::Sh);
        return;
    }
    if (op_id == isa::OperationId::VSE32_V) {
        execute_vse<uint32_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), vm, vl, isa::Funct3::Sw);
        return;
    }

    // Execute Vector move instructions (VMV_X_S, VMV_S_X)
    if (op_id == isa::OperationId::VMV_X_S) {
        Register val = 0;
        if (sew == 8) val = static_cast<Register>(static_cast<int32_t>(static_cast<int8_t>(get_group_element<uint8_t>(cpu.state().regs, rs2, 0))));
        else if (sew == 16) val = static_cast<Register>(static_cast<int32_t>(static_cast<int16_t>(get_group_element<uint16_t>(cpu.state().regs, rs2, 0))));
        else if (sew == 32) val = static_cast<Register>(static_cast<int32_t>(get_group_element<uint32_t>(cpu.state().regs, rs2, 0)));
        else val = get_group_element<uint64_t>(cpu.state().regs, rs2, 0);
        cpu.state().regs.write(rd, val);
        return;
    }
    if (op_id == isa::OperationId::VMV_S_X) {
        Register val = cpu.state().regs.read(rs1);
        if (sew == 8) set_group_element<uint8_t>(cpu.state().regs, rd, 0, static_cast<uint8_t>(val));
        else if (sew == 16) set_group_element<uint16_t>(cpu.state().regs, rd, 0, static_cast<uint16_t>(val));
        else if (sew == 32) set_group_element<uint32_t>(cpu.state().regs, rd, 0, static_cast<uint32_t>(val));
        else set_group_element<uint64_t>(cpu.state().regs, rd, 0, val);
        return;
    }

    // Dispatch remaining Vector ALU operations
    Register rs1_val = cpu.state().regs.read(rs1);

#define DISPATCH_V_OP(op_class, op_fun) \
    if (sew == 8) { \
        if (op_id == isa::OperationId::op_class##_VV) { perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VX) { perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VI) { perform_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl, op_fun); return; } \
    } else if (sew == 16) { \
        if (op_id == isa::OperationId::op_class##_VV) { perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VX) { perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VI) { perform_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl, op_fun); return; } \
    } else if (sew == 32) { \
        if (op_id == isa::OperationId::op_class##_VV) { perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VX) { perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VI) { perform_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl, op_fun); return; } \
    } else { \
        if (op_id == isa::OperationId::op_class##_VV) { perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VX) { perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VI) { perform_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl, op_fun); return; } \
    }

#define DISPATCH_V_OP_NO_IMM(op_class, op_fun) \
    if (sew == 8) { \
        if (op_id == isa::OperationId::op_class##_VV) { perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VX) { perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, op_fun); return; } \
    } else if (sew == 16) { \
        if (op_id == isa::OperationId::op_class##_VV) { perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VX) { perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, op_fun); return; } \
    } else if (sew == 32) { \
        if (op_id == isa::OperationId::op_class##_VV) { perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VX) { perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, op_fun); return; } \
    } else { \
        if (op_id == isa::OperationId::op_class##_VV) { perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VX) { perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, op_fun); return; } \
    }

#define DISPATCH_V_CMP(op_class, op_fun) \
    if (sew == 8) { \
        if (op_id == isa::OperationId::op_class##_VV) { perform_compare_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VX) { perform_compare_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VI) { perform_compare_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl, op_fun); return; } \
    } else if (sew == 16) { \
        if (op_id == isa::OperationId::op_class##_VV) { perform_compare_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VX) { perform_compare_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VI) { perform_compare_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl, op_fun); return; } \
    } else if (sew == 32) { \
        if (op_id == isa::OperationId::op_class##_VV) { perform_compare_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VX) { perform_compare_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VI) { perform_compare_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl, op_fun); return; } \
    } else { \
        if (op_id == isa::OperationId::op_class##_VV) { perform_compare_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VX) { perform_compare_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VI) { perform_compare_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl, op_fun); return; } \
    }

#define DISPATCH_V_CMP_NO_IMM(op_class, op_fun) \
    if (sew == 8) { \
        if (op_id == isa::OperationId::op_class##_VV) { perform_compare_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VX) { perform_compare_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, op_fun); return; } \
    } else if (sew == 16) { \
        if (op_id == isa::OperationId::op_class##_VV) { perform_compare_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VX) { perform_compare_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, op_fun); return; } \
    } else if (sew == 32) { \
        if (op_id == isa::OperationId::op_class##_VV) { perform_compare_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VX) { perform_compare_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, op_fun); return; } \
    } else { \
        if (op_id == isa::OperationId::op_class##_VV) { perform_compare_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, op_fun); return; } \
        if (op_id == isa::OperationId::op_class##_VX) { perform_compare_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, op_fun); return; } \
    }

    // Arithmetic functions
    auto add_f = []<typename T>(T a, T b) -> T { return a + b; };
    auto sub_f = []<typename T>(T a, T b) -> T { return a - b; };
    auto mul_f = []<typename T>(T a, T b) -> T { return a * b; };
    auto div_f = []<typename T>(T a, T b) -> T { return b == 0 ? static_cast<T>(-1) : static_cast<T>(static_cast<std::make_signed_t<T>>(a) / static_cast<std::make_signed_t<T>>(b)); };
    auto divu_f = []<typename T>(T a, T b) -> T { return b == 0 ? static_cast<T>(-1) : a / b; };
    auto and_f = []<typename T>(T a, T b) -> T { return a & b; };
    auto or_f = []<typename T>(T a, T b) -> T { return a | b; };
    auto xor_f = []<typename T>(T a, T b) -> T { return a ^ b; };
    auto sll_f = []<typename T>(T a, T b) -> T { return static_cast<T>(a << (b & (sizeof(T) * 8 - 1))); };
    auto srl_f = []<typename T>(T a, T b) -> T { return static_cast<T>(a >> (b & (sizeof(T) * 8 - 1))); };
    auto sra_f = []<typename T>(T a, T b) -> T { return static_cast<T>(static_cast<std::make_signed_t<T>>(a) >> (b & (sizeof(T) * 8 - 1))); };
    auto minu_f = []<typename T>(T a, T b) -> T { return std::min(a, b); };
    auto min_f = []<typename T>(T a, T b) -> T {
        using S = std::make_signed_t<T>;
        return static_cast<T>(std::min(static_cast<S>(a), static_cast<S>(b)));
    };
    auto maxu_f = []<typename T>(T a, T b) -> T { return std::max(a, b); };
    auto max_f = []<typename T>(T a, T b) -> T {
        using S = std::make_signed_t<T>;
        return static_cast<T>(std::max(static_cast<S>(a), static_cast<S>(b)));
    };

    DISPATCH_V_OP(VADD, add_f)
    DISPATCH_V_OP_NO_IMM(VSUB, sub_f)
    DISPATCH_V_OP_NO_IMM(VMUL, mul_f)
    DISPATCH_V_OP_NO_IMM(VDIV, div_f)
    DISPATCH_V_OP_NO_IMM(VDIVU, divu_f)
    DISPATCH_V_OP(VAND, and_f)
    DISPATCH_V_OP(VOR, or_f)
    DISPATCH_V_OP(VXOR, xor_f)
    DISPATCH_V_OP(VSLL, sll_f)
    DISPATCH_V_OP(VSRL, srl_f)
    DISPATCH_V_OP(VSRA, sra_f)
    DISPATCH_V_OP_NO_IMM(VMIN, min_f)
    DISPATCH_V_OP_NO_IMM(VMINU, minu_f)
    DISPATCH_V_OP_NO_IMM(VMAX, max_f)
    DISPATCH_V_OP_NO_IMM(VMAXU, maxu_f)

    // Comparisons
    auto eq_f = []<typename T>(T a, T b) -> bool { return a == b; };
    auto ne_f = []<typename T>(T a, T b) -> bool { return a != b; };
    auto lt_f = []<typename T>(T a, T b) -> bool { return static_cast<std::make_signed_t<T>>(a) < static_cast<std::make_signed_t<T>>(b); };
    auto ltu_f = []<typename T>(T a, T b) -> bool { return a < b; };
    auto le_f = []<typename T>(T a, T b) -> bool { return static_cast<std::make_signed_t<T>>(a) <= static_cast<std::make_signed_t<T>>(b); };
    auto leu_f = []<typename T>(T a, T b) -> bool { return a <= b; };
    auto gt_f = []<typename T>(T a, T b) -> bool { return static_cast<std::make_signed_t<T>>(a) > static_cast<std::make_signed_t<T>>(b); };
    auto gtu_f = []<typename T>(T a, T b) -> bool { return a > b; };

    DISPATCH_V_CMP(VMSEQ, eq_f)
    DISPATCH_V_CMP(VMSNE, ne_f)
    DISPATCH_V_CMP_NO_IMM(VMSLT, lt_f)
    DISPATCH_V_CMP_NO_IMM(VMSLTU, ltu_f)
    DISPATCH_V_CMP(VMSLE, le_f)
    DISPATCH_V_CMP(VMSLEU, leu_f)
    
    // Greater-Than (VMSGT)
    if (sew == 8) {
        if (op_id == isa::OperationId::VMSGT_VX) { perform_compare_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, gt_f); return; }
        if (op_id == isa::OperationId::VMSGT_VI) { perform_compare_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl, gt_f); return; }
        if (op_id == isa::OperationId::VMSGTU_VX) { perform_compare_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, gtu_f); return; }
        if (op_id == isa::OperationId::VMSGTU_VI) { perform_compare_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl, gtu_f); return; }
    } else if (sew == 16) {
        if (op_id == isa::OperationId::VMSGT_VX) { perform_compare_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, gt_f); return; }
        if (op_id == isa::OperationId::VMSGT_VI) { perform_compare_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl, gt_f); return; }
        if (op_id == isa::OperationId::VMSGTU_VX) { perform_compare_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, gtu_f); return; }
        if (op_id == isa::OperationId::VMSGTU_VI) { perform_compare_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl, gtu_f); return; }
    } else if (sew == 32) {
        if (op_id == isa::OperationId::VMSGT_VX) { perform_compare_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, gt_f); return; }
        if (op_id == isa::OperationId::VMSGT_VI) { perform_compare_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl, gt_f); return; }
        if (op_id == isa::OperationId::VMSGTU_VX) { perform_compare_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, gtu_f); return; }
        if (op_id == isa::OperationId::VMSGTU_VI) { perform_compare_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl, gtu_f); return; }
    } else {
        if (op_id == isa::OperationId::VMSGT_VX) { perform_compare_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, gt_f); return; }
        if (op_id == isa::OperationId::VMSGT_VI) { perform_compare_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl, gt_f); return; }
        if (op_id == isa::OperationId::VMSGTU_VX) { perform_compare_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, gtu_f); return; }
        if (op_id == isa::OperationId::VMSGTU_VI) { perform_compare_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl, gtu_f); return; }
    }

    // VMERGE (merge vector registers using mask)
    if (op_id == isa::OperationId::VMERGE_VVM || op_id == isa::OperationId::VMERGE_VXM || op_id == isa::OperationId::VMERGE_VIM) {
        if (sew == 8) {
            if (op_id == isa::OperationId::VMERGE_VVM) execute_vmerge_vv<uint8_t>(cpu, rd, rs1, rs2, vl);
            else if (op_id == isa::OperationId::VMERGE_VXM) execute_vmerge_vx<uint8_t>(cpu, rd, rs1_val, rs2, vl);
            else execute_vmerge_vi<uint8_t>(cpu, rd, simm5, rs2, vl);
        } else if (sew == 16) {
            if (op_id == isa::OperationId::VMERGE_VVM) execute_vmerge_vv<uint16_t>(cpu, rd, rs1, rs2, vl);
            else if (op_id == isa::OperationId::VMERGE_VXM) execute_vmerge_vx<uint16_t>(cpu, rd, rs1_val, rs2, vl);
            else execute_vmerge_vi<uint16_t>(cpu, rd, simm5, rs2, vl);
        } else if (sew == 32) {
            if (op_id == isa::OperationId::VMERGE_VVM) execute_vmerge_vv<uint32_t>(cpu, rd, rs1, rs2, vl);
            else if (op_id == isa::OperationId::VMERGE_VXM) execute_vmerge_vx<uint32_t>(cpu, rd, rs1_val, rs2, vl);
            else execute_vmerge_vi<uint32_t>(cpu, rd, simm5, rs2, vl);
        } else {
            if (op_id == isa::OperationId::VMERGE_VVM) execute_vmerge_vv<uint64_t>(cpu, rd, rs1, rs2, vl);
            else if (op_id == isa::OperationId::VMERGE_VXM) execute_vmerge_vx<uint64_t>(cpu, rd, rs1_val, rs2, vl);
            else execute_vmerge_vi<uint64_t>(cpu, rd, simm5, rs2, vl);
        }
        return;
    }

    // VMV splats (VMV_V_V, VMV_V_X, VMV_V_I)
    if (op_id == isa::OperationId::VMV_V_V || op_id == isa::OperationId::VMV_V_X || op_id == isa::OperationId::VMV_V_I) {
        auto add_f = []<typename T>(T /*a*/, T b) -> T { return b; };
        if (sew == 8) {
            if (op_id == isa::OperationId::VMV_V_V) perform_vv<uint8_t>(cpu, rd, rs1, rs2, true, vl, add_f);
            else if (op_id == isa::OperationId::VMV_V_X) perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, true, vl, add_f);
            else perform_vi<uint8_t>(cpu, rd, simm5, rs2, true, vl, add_f);
        } else if (sew == 16) {
            if (op_id == isa::OperationId::VMV_V_V) perform_vv<uint16_t>(cpu, rd, rs1, rs2, true, vl, add_f);
            else if (op_id == isa::OperationId::VMV_V_X) perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, true, vl, add_f);
            else perform_vi<uint16_t>(cpu, rd, simm5, rs2, true, vl, add_f);
        } else if (sew == 32) {
            if (op_id == isa::OperationId::VMV_V_V) perform_vv<uint32_t>(cpu, rd, rs1, rs2, true, vl, add_f);
            else if (op_id == isa::OperationId::VMV_V_X) perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, true, vl, add_f);
            else perform_vi<uint32_t>(cpu, rd, simm5, rs2, true, vl, add_f);
        } else {
            if (op_id == isa::OperationId::VMV_V_V) perform_vv<uint64_t>(cpu, rd, rs1, rs2, true, vl, add_f);
            else if (op_id == isa::OperationId::VMV_V_X) perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, true, vl, add_f);
            else perform_vi<uint64_t>(cpu, rd, simm5, rs2, true, vl, add_f);
        }
        return;
    }

#undef DISPATCH_V_OP
#undef DISPATCH_V_OP_NO_IMM
#undef DISPATCH_V_CMP
#undef DISPATCH_V_CMP_NO_IMM
}

} // namespace simrv::execute
