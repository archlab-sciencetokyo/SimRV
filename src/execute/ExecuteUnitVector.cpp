#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/isa/Common.hpp"
#include <algorithm>
#include <type_traits>
#include <cstdio>
#include <limits>

namespace simrv::execute {

namespace {

// Helper to check/get active elements under mask
inline bool is_element_active(const core::VectorRegister& mask_reg, uint32_t i, bool vm) {
    if (vm) return true;
    return (mask_reg.u8[i / 8] & (1u << (i % 8))) != 0; // NOLINT(cppcoreguidelines-pro-type-union-access)
}

// Helper to write mask bits
inline void set_mask_bit(core::VectorRegister& dest, uint32_t i, bool val) {
    uint32_t byte_idx = i / 8;
    uint32_t bit_idx = i % 8;
    if (val) {
        dest.u8[byte_idx] |= (1u << bit_idx); // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else {
        dest.u8[byte_idx] &= ~(1u << bit_idx); // NOLINT(cppcoreguidelines-pro-type-union-access)
    }
}

// Helper to read group element
template <typename T>
inline T get_group_element(const core::RegisterFile& regs, RegId base_reg, uint32_t i) {
    constexpr uint32_t elems_per_reg = 32 / sizeof(T);
    uint32_t reg_offset = i / elems_per_reg;
    uint32_t elem_idx = i % elems_per_reg;
    auto actual_reg = static_cast<RegId>((std::to_underlying(base_reg) + reg_offset) & 0x1F);
    const auto& vreg = regs.read_vector(actual_reg);

    if constexpr (std::is_same_v<T, float>) {
        return vreg.f32[elem_idx]; // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else if constexpr (std::is_same_v<T, double>) {
        return vreg.f64[elem_idx]; // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else if constexpr (sizeof(T) == 1) {
        return static_cast<T>(vreg.u8[elem_idx]); // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else if constexpr (sizeof(T) == 2) {
        return static_cast<T>(vreg.u16[elem_idx]); // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else if constexpr (sizeof(T) == 4) {
        return static_cast<T>(vreg.u32[elem_idx]); // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else {
        return static_cast<T>(vreg.u64[elem_idx]); // NOLINT(cppcoreguidelines-pro-type-union-access)
    }
}

// Helper to write group element
template <typename T>
inline void set_group_element(core::RegisterFile& regs, RegId base_reg, uint32_t i, T val) {
    constexpr uint32_t elems_per_reg = 32 / sizeof(T);
    uint32_t reg_offset = i / elems_per_reg;
    uint32_t elem_idx = i % elems_per_reg;
    auto actual_reg = static_cast<RegId>((std::to_underlying(base_reg) + reg_offset) & 0x1F);
    auto& vreg = regs.read_vector(actual_reg);

    if constexpr (std::is_same_v<T, float>) {
        vreg.f32[elem_idx] = val; // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else if constexpr (std::is_same_v<T, double>) {
        vreg.f64[elem_idx] = val; // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else if constexpr (sizeof(T) == 1) {
        vreg.u8[elem_idx] = static_cast<uint8_t>(val); // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else if constexpr (sizeof(T) == 2) {
        vreg.u16[elem_idx] = static_cast<uint16_t>(val); // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else if constexpr (sizeof(T) == 4) {
        vreg.u32[elem_idx] = static_cast<uint32_t>(val); // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else {
        vreg.u64[elem_idx] = static_cast<uint64_t>(val); // NOLINT(cppcoreguidelines-pro-type-union-access)
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
        uint64_t val = 0;
        if constexpr (sizeof(T) == 8) {
            if constexpr (simrv::xlen::kIsXLen64) {
                val = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, mem_f3);
            } else {
                if (simrv::compiler::unlikely((addr & 7) != 0)) {
                    cpu.pipeline_context.pending_exception = ExceptionCode::MisalignedLoad;
                    cpu.pipeline_context.pending_tval = addr;
                    return;
                }
                Word lo = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, isa::Funct3::Lw);
                Word hi = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr + 4, isa::Funct3::Lw);
                val = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
            }
        } else {
            val = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, mem_f3);
        }
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
        if constexpr (sizeof(T) == 8) {
            if constexpr (simrv::xlen::kIsXLen64) {
                simrv::memory::MemoryAccess::storeInt(mem, cpu, addr, static_cast<Word>(val), mem_f3);
            } else {
                if (simrv::compiler::unlikely((addr & 7) != 0)) {
                    cpu.pipeline_context.pending_exception = ExceptionCode::MisalignedStore;
                    cpu.pipeline_context.pending_tval = addr;
                    return;
                }
                simrv::memory::MemoryAccess::storeInt(mem, cpu, addr, static_cast<Word>(val & 0xFFFFFFFFULL), isa::Funct3::Sw);
                simrv::memory::MemoryAccess::storeInt(mem, cpu, addr + 4, static_cast<Word>((val >> 32) & 0xFFFFFFFFULL), isa::Funct3::Sw);
            }
        } else {
            simrv::memory::MemoryAccess::storeInt(mem, cpu, addr, static_cast<Word>(val), mem_f3);
        }
        if (cpu.pipeline_context.pending_exception.has_value()) {
            return;
        }
    }
}

// Vector strided load helper
template <typename T>
void execute_vlse(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId rd, Register base_addr, Register stride_reg_val, bool vm, uint32_t vl, isa::Funct3 mem_f3) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    auto stride = static_cast<int64_t>(static_cast<std::make_signed_t<Register>>(stride_reg_val));

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        Address addr = base_addr + i * stride;
        uint64_t val = 0;
        if constexpr (sizeof(T) == 8) {
            if constexpr (simrv::xlen::kIsXLen64) {
                val = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, mem_f3);
            } else {
                if (simrv::compiler::unlikely((addr & 7) != 0)) {
                    cpu.pipeline_context.pending_exception = ExceptionCode::MisalignedLoad;
                    cpu.pipeline_context.pending_tval = addr;
                    return;
                }
                Word lo = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, isa::Funct3::Lw);
                Word hi = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr + 4, isa::Funct3::Lw);
                val = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
            }
        } else {
            val = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, mem_f3);
        }
        if (cpu.pipeline_context.pending_exception.has_value()) {
            return;
        }
        set_group_element<T>(cpu.state().regs, rd, i, static_cast<T>(val));
    }
}

// Vector strided store helper
template <typename T>
void execute_vsse(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId vs3, Register base_addr, Register stride_reg_val, bool vm, uint32_t vl, isa::Funct3 mem_f3) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    auto stride = static_cast<int64_t>(static_cast<std::make_signed_t<Register>>(stride_reg_val));

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        Address addr = base_addr + i * stride;
        T val = get_group_element<T>(cpu.state().regs, vs3, i);
        if constexpr (sizeof(T) == 8) {
            if constexpr (simrv::xlen::kIsXLen64) {
                simrv::memory::MemoryAccess::storeInt(mem, cpu, addr, static_cast<Word>(val), mem_f3);
            } else {
                if (simrv::compiler::unlikely((addr & 7) != 0)) {
                    cpu.pipeline_context.pending_exception = ExceptionCode::MisalignedStore;
                    cpu.pipeline_context.pending_tval = addr;
                    return;
                }
                simrv::memory::MemoryAccess::storeInt(mem, cpu, addr, static_cast<Word>(val & 0xFFFFFFFFULL), isa::Funct3::Sw);
                simrv::memory::MemoryAccess::storeInt(mem, cpu, addr + 4, static_cast<Word>((val >> 32) & 0xFFFFFFFFULL), isa::Funct3::Sw);
            }
        } else {
            simrv::memory::MemoryAccess::storeInt(mem, cpu, addr, static_cast<Word>(val), mem_f3);
        }
        if (cpu.pipeline_context.pending_exception.has_value()) {
            return;
        }
    }
}

// Vector indexed load helper
template <typename T_data, typename T_idx>
void execute_vluxei(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId rd, Register base_addr, RegId vs2, bool vm, uint32_t vl, isa::Funct3 mem_f3) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        auto offset = get_group_element<T_idx>(cpu.state().regs, vs2, i);
        Address addr = base_addr + static_cast<int64_t>(offset);

        uint64_t val = 0;
        if constexpr (sizeof(T_data) == 8) {
            if constexpr (simrv::xlen::kIsXLen64) {
                val = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, mem_f3);
            } else {
                if (simrv::compiler::unlikely((addr & 7) != 0)) {
                    cpu.pipeline_context.pending_exception = ExceptionCode::MisalignedLoad;
                    cpu.pipeline_context.pending_tval = addr;
                    return;
                }
                Word lo = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, isa::Funct3::Lw);
                Word hi = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr + 4, isa::Funct3::Lw);
                val = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
            }
        } else {
            val = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, mem_f3);
        }
        if (cpu.pipeline_context.pending_exception.has_value()) {
            return;
        }
        set_group_element<T_data>(cpu.state().regs, rd, i, static_cast<T_data>(val));
    }
}

template <typename T_idx>
void dispatch_vluxei(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId rd, Register base_addr, RegId vs2, bool vm, uint32_t vl, uint32_t sew) {
    if (sew == 8) {
        execute_vluxei<uint8_t, T_idx>(cpu, mem, rd, base_addr, vs2, vm, vl, isa::Funct3::Lbu);
    } else if (sew == 16) {
        execute_vluxei<uint16_t, T_idx>(cpu, mem, rd, base_addr, vs2, vm, vl, isa::Funct3::Lhu);
    } else if (sew == 32) {
        execute_vluxei<uint32_t, T_idx>(cpu, mem, rd, base_addr, vs2, vm, vl, isa::Funct3::Lw);
    } else {
        execute_vluxei<uint64_t, T_idx>(cpu, mem, rd, base_addr, vs2, vm, vl, isa::Funct3::Ld);
    }
}

// Vector indexed store helper
template <typename T_data, typename T_idx>
void execute_vsuxei(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId vs3, Register base_addr, RegId vs2, bool vm, uint32_t vl, isa::Funct3 mem_f3) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        auto offset = get_group_element<T_idx>(cpu.state().regs, vs2, i);
        Address addr = base_addr + static_cast<int64_t>(offset);
        auto val = get_group_element<T_data>(cpu.state().regs, vs3, i);
        if constexpr (sizeof(T_data) == 8) {
            if constexpr (simrv::xlen::kIsXLen64) {
                simrv::memory::MemoryAccess::storeInt(mem, cpu, addr, static_cast<Word>(val), mem_f3);
            } else {
                if (simrv::compiler::unlikely((addr & 7) != 0)) {
                    cpu.pipeline_context.pending_exception = ExceptionCode::MisalignedStore;
                    cpu.pipeline_context.pending_tval = addr;
                    return;
                }
                simrv::memory::MemoryAccess::storeInt(mem, cpu, addr, static_cast<Word>(val & 0xFFFFFFFFULL), isa::Funct3::Sw);
                simrv::memory::MemoryAccess::storeInt(mem, cpu, addr + 4, static_cast<Word>((val >> 32) & 0xFFFFFFFFULL), isa::Funct3::Sw);
            }
        } else {
            simrv::memory::MemoryAccess::storeInt(mem, cpu, addr, static_cast<Word>(val), mem_f3);
        }
        if (cpu.pipeline_context.pending_exception.has_value()) {
            return;
        }
    }
}

template <typename T_idx>
void dispatch_vsuxei(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId vs3, Register base_addr, RegId vs2, bool vm, uint32_t vl, uint32_t sew) {
    if (sew == 8) {
        execute_vsuxei<uint8_t, T_idx>(cpu, mem, vs3, base_addr, vs2, vm, vl, isa::Funct3::Sb);
    } else if (sew == 16) {
        execute_vsuxei<uint16_t, T_idx>(cpu, mem, vs3, base_addr, vs2, vm, vl, isa::Funct3::Sh);
    } else if (sew == 32) {
        execute_vsuxei<uint32_t, T_idx>(cpu, mem, vs3, base_addr, vs2, vm, vl, isa::Funct3::Sw);
    } else {
        execute_vsuxei<uint64_t, T_idx>(cpu, mem, vs3, base_addr, vs2, vm, vl, isa::Funct3::Sd);
    }
}

// Vector floating-point MAC helpers
template <typename T>
void perform_vfmacc_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        T val1 = get_group_element<T>(cpu.state().regs, rs1, i);
        T val2 = get_group_element<T>(cpu.state().regs, rs2, i);
        T dest_val = get_group_element<T>(cpu.state().regs, rd, i);
        T res = dest_val + val2 * val1;
        set_group_element<T>(cpu.state().regs, rd, i, res);
    }
}

template <typename T>
void perform_vfmacc_vf(core::CPU& cpu, RegId rd, FloatingRegister rs1_val, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T scalar = 0;
    if constexpr (std::is_same_v<T, float>) {
        if ((rs1_val & simrv::xlen::kF32BoxerBits) != simrv::xlen::kF32BoxerBits) {
            scalar = std::bit_cast<float>(0x7fc00000U);
        } else {
            scalar = std::bit_cast<float>(static_cast<uint32_t>(rs1_val & 0xFFFFFFFFULL));
        }
    } else {
        scalar = std::bit_cast<double>(rs1_val);
    }

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        T val2 = get_group_element<T>(cpu.state().regs, rs2, i);
        T dest_val = get_group_element<T>(cpu.state().regs, rd, i);
        T res = dest_val + val2 * scalar;
        set_group_element<T>(cpu.state().regs, rd, i, res);
    }
}

template <typename T>
void perform_mac_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, bool overwrite_acc, bool subtract) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        T val1 = get_group_element<T>(cpu.state().regs, rs1, i);
        T val2 = get_group_element<T>(cpu.state().regs, rs2, i);
        T dest_val = get_group_element<T>(cpu.state().regs, rd, i);

        T prod = val1 * val2;
        T res = 0;
        if (overwrite_acc) {
            res = subtract ? (dest_val - prod) : (dest_val + prod);
        } else {
            T term1 = val1 * dest_val;
            res = subtract ? (val2 - term1) : (val2 + term1);
        }
        set_group_element<T>(cpu.state().regs, rd, i, res);
    }
}

template <typename T>
void perform_mac_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl, bool overwrite_acc, bool subtract) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(rs1_val);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        T val2 = get_group_element<T>(cpu.state().regs, rs2, i);
        T dest_val = get_group_element<T>(cpu.state().regs, rd, i);

        T prod = val1 * val2;
        T res = 0;
        if (overwrite_acc) {
            res = subtract ? (dest_val - prod) : (dest_val + prod);
        } else {
            T term1 = val1 * dest_val;
            res = subtract ? (val2 - term1) : (val2 + term1);
        }
        set_group_element<T>(cpu.state().regs, rd, i, res);
    }
}

template <typename T_dest, typename T_src1, typename T_src2>
void perform_widening_mac_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, bool subtract = false) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        auto val1 = get_group_element<T_src1>(cpu.state().regs, rs1, i);
        auto val2 = get_group_element<T_src2>(cpu.state().regs, rs2, i);
        auto dest_val = get_group_element<T_dest>(cpu.state().regs, rd, i);

        T_dest prod = static_cast<T_dest>(val1) * static_cast<T_dest>(val2);
        T_dest res = subtract ? (dest_val - prod) : (dest_val + prod);
        set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

template <typename T_dest, typename T_src1, typename T_src2>
void perform_widening_mac_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl, bool subtract = false) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    auto val1 = static_cast<T_src1>(rs1_val);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        auto val2 = get_group_element<T_src2>(cpu.state().regs, rs2, i);
        auto dest_val = get_group_element<T_dest>(cpu.state().regs, rd, i);

        T_dest prod = static_cast<T_dest>(val1) * static_cast<T_dest>(val2);
        T_dest res = subtract ? (dest_val - prod) : (dest_val + prod);
        set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

template <typename T>
void execute_vmerge_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = 0; i < vl; i++) {
        bool mask_bit = is_element_active(mask_reg, i, false);
        T val = mask_bit ? get_group_element<T>(cpu.state().regs, rs1, i) : get_group_element<T>(cpu.state().regs, rs2, i);
        set_group_element<T>(cpu.state().regs, rd, i, val);
    }
}

template <typename T>
void execute_vmerge_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(rs1_val);

    for (uint32_t i = 0; i < vl; i++) {
        bool mask_bit = is_element_active(mask_reg, i, false);
        T val = mask_bit ? val1 : get_group_element<T>(cpu.state().regs, rs2, i);
        set_group_element<T>(cpu.state().regs, rd, i, val);
    }
}

template <typename T>
void execute_vmerge_vi(core::CPU& cpu, RegId rd, int32_t imm, RegId rs2, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(imm);

    for (uint32_t i = 0; i < vl; i++) {
        bool mask_bit = is_element_active(mask_reg, i, false);
        T val = mask_bit ? val1 : get_group_element<T>(cpu.state().regs, rs2, i);
        set_group_element<T>(cpu.state().regs, rd, i, val);
    }
}

template <typename T>
void execute_vid(core::CPU& cpu, RegId rd, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        set_group_element<T>(cpu.state().regs, rd, i, static_cast<T>(i));
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
    if (op_id == isa::OperationId::VLE64_V) {
        execute_vle<uint64_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), vm, vl, isa::Funct3::Ld);
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
    if (op_id == isa::OperationId::VSE64_V) {
        execute_vse<uint64_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), vm, vl, isa::Funct3::Sd);
        return;
    }

    // Execute Vector Strided Load instructions
    if (op_id == isa::OperationId::VLSE8_V) {
        execute_vlse<uint8_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), cpu.state().regs.read(rs2), vm, vl, isa::Funct3::Lbu);
        return;
    }
    if (op_id == isa::OperationId::VLSE16_V) {
        execute_vlse<uint16_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), cpu.state().regs.read(rs2), vm, vl, isa::Funct3::Lhu);
        return;
    }
    if (op_id == isa::OperationId::VLSE32_V) {
        execute_vlse<uint32_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), cpu.state().regs.read(rs2), vm, vl, isa::Funct3::Lw);
        return;
    }
    if (op_id == isa::OperationId::VLSE64_V) {
        execute_vlse<uint64_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), cpu.state().regs.read(rs2), vm, vl, isa::Funct3::Ld);
        return;
    }

    // Execute Vector Strided Store instructions
    if (op_id == isa::OperationId::VSSE8_V) {
        execute_vsse<uint8_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), cpu.state().regs.read(rs2), vm, vl, isa::Funct3::Sb);
        return;
    }
    if (op_id == isa::OperationId::VSSE16_V) {
        execute_vsse<uint16_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), cpu.state().regs.read(rs2), vm, vl, isa::Funct3::Sh);
        return;
    }
    if (op_id == isa::OperationId::VSSE32_V) {
        execute_vsse<uint32_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), cpu.state().regs.read(rs2), vm, vl, isa::Funct3::Sw);
        return;
    }
    if (op_id == isa::OperationId::VSSE64_V) {
        execute_vsse<uint64_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), cpu.state().regs.read(rs2), vm, vl, isa::Funct3::Sd);
        return;
    }

    // Execute Vector Indexed Load instructions
    if (op_id == isa::OperationId::VLUXEI8_V || op_id == isa::OperationId::VLOXEI8_V) {
        dispatch_vluxei<int8_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), rs2, vm, vl, sew);
        return;
    }
    if (op_id == isa::OperationId::VLUXEI16_V || op_id == isa::OperationId::VLOXEI16_V) {
        dispatch_vluxei<int16_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), rs2, vm, vl, sew);
        return;
    }
    if (op_id == isa::OperationId::VLUXEI32_V || op_id == isa::OperationId::VLOXEI32_V) {
        dispatch_vluxei<int32_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), rs2, vm, vl, sew);
        return;
    }
    if (op_id == isa::OperationId::VLUXEI64_V || op_id == isa::OperationId::VLOXEI64_V) {
        dispatch_vluxei<int64_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), rs2, vm, vl, sew);
        return;
    }

    // Execute Vector Indexed Store instructions
    if (op_id == isa::OperationId::VSUXEI8_V || op_id == isa::OperationId::VSOXEI8_V) {
        dispatch_vsuxei<int8_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), rs2, vm, vl, sew);
        return;
    }
    if (op_id == isa::OperationId::VSUXEI16_V || op_id == isa::OperationId::VSOXEI16_V) {
        dispatch_vsuxei<int16_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), rs2, vm, vl, sew);
        return;
    }
    if (op_id == isa::OperationId::VSUXEI32_V || op_id == isa::OperationId::VSOXEI32_V) {
        dispatch_vsuxei<int32_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), rs2, vm, vl, sew);
        return;
    }
    if (op_id == isa::OperationId::VSUXEI64_V || op_id == isa::OperationId::VSOXEI64_V) {
        dispatch_vsuxei<int64_t>(cpu, machine.memory_, rd, cpu.state().regs.read(rs1), rs2, vm, vl, sew);
        return;
    }

    // Vector floating-point MAC instructions
    if (op_id == isa::OperationId::VFMACC_VV) {
        if (sew == 32) perform_vfmacc_vv<float>(cpu, rd, rs1, rs2, vm, vl);
        else if (sew == 64) perform_vfmacc_vv<double>(cpu, rd, rs1, rs2, vm, vl);
        return;
    }
    if (op_id == isa::OperationId::VFMACC_VF) {
        FloatingRegister rs1_fp_val = cpu.state().regs.read_fp(rs1);
        if (sew == 32) perform_vfmacc_vf<float>(cpu, rd, rs1_fp_val, rs2, vm, vl);
        else if (sew == 64) perform_vfmacc_vf<double>(cpu, rd, rs1_fp_val, rs2, vm, vl);
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
    auto div_f = []<typename T>(T a, T b) -> T {
        using S = std::make_signed_t<T>;
        if (b == 0) return static_cast<T>(-1);
        if (static_cast<S>(a) == std::numeric_limits<S>::min() && static_cast<S>(b) == -1) return a;
        return static_cast<T>(static_cast<S>(a) / static_cast<S>(b));
    };
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
    
    // Vector single-width multiply-accumulate instructions
    if (op_id >= isa::OperationId::VMACC_VV && op_id <= isa::OperationId::VNSUB_VX) {
        bool is_vv = (op_id == isa::OperationId::VMACC_VV || op_id == isa::OperationId::VMADD_VV ||
                      op_id == isa::OperationId::VNMSAC_VV || op_id == isa::OperationId::VNSUB_VV);
        bool overwrite_acc = (op_id == isa::OperationId::VMACC_VV || op_id == isa::OperationId::VMACC_VX ||
                              op_id == isa::OperationId::VNMSAC_VV || op_id == isa::OperationId::VNMSAC_VX);
        bool subtract = (op_id == isa::OperationId::VNMSAC_VV || op_id == isa::OperationId::VNMSAC_VX ||
                         op_id == isa::OperationId::VNSUB_VV || op_id == isa::OperationId::VNSUB_VX);

        if (sew == 8) {
            if (is_vv) perform_mac_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, overwrite_acc, subtract);
            else perform_mac_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, overwrite_acc, subtract);
        } else if (sew == 16) {
            if (is_vv) perform_mac_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, overwrite_acc, subtract);
            else perform_mac_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, overwrite_acc, subtract);
        } else if (sew == 32) {
            if (is_vv) perform_mac_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, overwrite_acc, subtract);
            else perform_mac_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, overwrite_acc, subtract);
        } else {
            if (is_vv) perform_mac_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, overwrite_acc, subtract);
            else perform_mac_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, overwrite_acc, subtract);
        }
        return;
    }

    // Vector widening integer multiply-accumulate instructions
    if (op_id >= isa::OperationId::VWMACCU_VV && op_id <= isa::OperationId::VWMACCSU_VX) {
        if (sew == 8) {
            switch (op_id) {
                case isa::OperationId::VWMACCU_VV:
                    perform_widening_mac_vv<uint16_t, uint8_t, uint8_t>(cpu, rd, rs1, rs2, vm, vl); break;
                case isa::OperationId::VWMACCU_VX:
                    perform_widening_mac_vx<uint16_t, uint8_t, uint8_t>(cpu, rd, rs1_val, rs2, vm, vl); break;
                case isa::OperationId::VWMACC_VV:
                    perform_widening_mac_vv<int16_t, int8_t, int8_t>(cpu, rd, rs1, rs2, vm, vl); break;
                case isa::OperationId::VWMACC_VX:
                    perform_widening_mac_vx<int16_t, int8_t, int8_t>(cpu, rd, rs1_val, rs2, vm, vl); break;
                case isa::OperationId::VWMACCUS_VX:
                    perform_widening_mac_vx<int16_t, uint8_t, int8_t>(cpu, rd, rs1_val, rs2, vm, vl); break;
                case isa::OperationId::VWMACCSU_VV:
                    perform_widening_mac_vv<int16_t, int8_t, uint8_t>(cpu, rd, rs1, rs2, vm, vl); break;
                case isa::OperationId::VWMACCSU_VX:
                    perform_widening_mac_vx<int16_t, int8_t, uint8_t>(cpu, rd, rs1_val, rs2, vm, vl); break;
                default: break;
            }
        } else if (sew == 16) {
            switch (op_id) {
                case isa::OperationId::VWMACCU_VV:
                    perform_widening_mac_vv<uint32_t, uint16_t, uint16_t>(cpu, rd, rs1, rs2, vm, vl); break;
                case isa::OperationId::VWMACCU_VX:
                    perform_widening_mac_vx<uint32_t, uint16_t, uint16_t>(cpu, rd, rs1_val, rs2, vm, vl); break;
                case isa::OperationId::VWMACC_VV:
                    perform_widening_mac_vv<int32_t, int16_t, int16_t>(cpu, rd, rs1, rs2, vm, vl); break;
                case isa::OperationId::VWMACC_VX:
                    perform_widening_mac_vx<int32_t, int16_t, int16_t>(cpu, rd, rs1_val, rs2, vm, vl); break;
                case isa::OperationId::VWMACCUS_VX:
                    perform_widening_mac_vx<int32_t, uint16_t, int16_t>(cpu, rd, rs1_val, rs2, vm, vl); break;
                case isa::OperationId::VWMACCSU_VV:
                    perform_widening_mac_vv<int32_t, int16_t, uint16_t>(cpu, rd, rs1, rs2, vm, vl); break;
                case isa::OperationId::VWMACCSU_VX:
                    perform_widening_mac_vx<int32_t, int16_t, uint16_t>(cpu, rd, rs1_val, rs2, vm, vl); break;
                default: break;
            }
        } else if (sew == 32) {
            switch (op_id) {
                case isa::OperationId::VWMACCU_VV:
                    perform_widening_mac_vv<uint64_t, uint32_t, uint32_t>(cpu, rd, rs1, rs2, vm, vl); break;
                case isa::OperationId::VWMACCU_VX:
                    perform_widening_mac_vx<uint64_t, uint32_t, uint32_t>(cpu, rd, rs1_val, rs2, vm, vl); break;
                case isa::OperationId::VWMACC_VV:
                    perform_widening_mac_vv<int64_t, int32_t, int32_t>(cpu, rd, rs1, rs2, vm, vl); break;
                case isa::OperationId::VWMACC_VX:
                    perform_widening_mac_vx<int64_t, int32_t, int32_t>(cpu, rd, rs1_val, rs2, vm, vl); break;
                case isa::OperationId::VWMACCUS_VX:
                    perform_widening_mac_vx<int64_t, uint32_t, int32_t>(cpu, rd, rs1_val, rs2, vm, vl); break;
                case isa::OperationId::VWMACCSU_VV:
                    perform_widening_mac_vv<int64_t, int32_t, uint32_t>(cpu, rd, rs1, rs2, vm, vl); break;
                case isa::OperationId::VWMACCSU_VX:
                    perform_widening_mac_vx<int64_t, int32_t, uint32_t>(cpu, rd, rs1_val, rs2, vm, vl); break;
                default: break;
            }
        }
        return;
    }

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

    // VID (VID_V)
    if (op_id == isa::OperationId::VID_V) {
        if (sew == 8) {
            execute_vid<uint8_t>(cpu, rd, vm, vl);
        } else if (sew == 16) {
            execute_vid<uint16_t>(cpu, rd, vm, vl);
        } else if (sew == 32) {
            execute_vid<uint32_t>(cpu, rd, vm, vl);
        } else {
            execute_vid<uint64_t>(cpu, rd, vm, vl);
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
