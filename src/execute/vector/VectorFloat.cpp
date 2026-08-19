#include <cfenv>
#include <cmath>

#include "VectorHelpers.hpp"
#include "simrv/execute/ExecuteUnit.hpp"

namespace simrv::execute {

namespace {

template <typename T>
auto canonicalize_nan(T value) -> T {
    if (!std::isnan(value)) return value;
    if constexpr (std::is_same_v<T, float>) {
        return std::bit_cast<float>(simrv::xlen::kF32Qnan);
    } else {
        return std::bit_cast<double>(simrv::xlen::kF64Qnan);
    }
}

auto host_exceptions_to_fflags(int exceptions) -> CSRValue {
    CSRValue flags = 0;
    if ((exceptions & FE_INEXACT) != 0) flags |= enum_mask(isa::FflagsBit::Nx);
    if ((exceptions & FE_UNDERFLOW) != 0) flags |= enum_mask(isa::FflagsBit::Uf);
    if ((exceptions & FE_OVERFLOW) != 0) flags |= enum_mask(isa::FflagsBit::Of);
    if ((exceptions & FE_DIVBYZERO) != 0) flags |= enum_mask(isa::FflagsBit::Dz);
    if ((exceptions & FE_INVALID) != 0) flags |= enum_mask(isa::FflagsBit::Nv);
    return flags;
}

auto frm_to_host_round(CSRValue fcsr) -> int {
    switch ((fcsr >> 5U) & 0x7U) {
        case enum_mask(isa::RoundingMode::Rtz):
            return FE_TOWARDZERO;
        case enum_mask(isa::RoundingMode::Rdn):
            return FE_DOWNWARD;
        case enum_mask(isa::RoundingMode::Rup):
            return FE_UPWARD;
        default:
            // FE_TONEAREST implements RNE. RMM still requires a software
            // tie-breaking path and is recorded in the compliance document.
            return FE_TONEAREST;
    }
}

// Vector-Vector Floating-Point Addition
template <typename T>
void execute_vfadd_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;

        if constexpr (std::is_same_v<T, uint16_t>) {
            auto val1_raw = vector::get_group_element<uint16_t>(cpu.state().regs, rs1, i);
            auto val2_raw = vector::get_group_element<uint16_t>(cpu.state().regs, rs2, i);
            float val1 = vector::fp16_to_fp32(val1_raw);
            float val2 = vector::fp16_to_fp32(val2_raw);
            float res = canonicalize_nan(val2 + val1);
            vector::set_group_element<uint16_t>(cpu.state().regs, rd, i, vector::fp32_to_fp16(res));
        } else {
            T val1 = vector::get_group_element<T>(cpu.state().regs, rs1, i);
            T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
            T res = canonicalize_nan(val2 + val1);
            vector::set_group_element<T>(cpu.state().regs, rd, i, res);
        }
    }
}

// Vector-Scalar Floating-Point Addition
template <typename T>
void execute_vfadd_vf(core::CPU& cpu, RegId rd, FloatingRegister rs1_val, RegId rs2, bool vm,
                      uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;

        if constexpr (std::is_same_v<T, uint16_t>) {
            auto val1_raw = static_cast<uint16_t>(rs1_val & 0xFFFFULL);
            auto val2_raw = vector::get_group_element<uint16_t>(cpu.state().regs, rs2, i);
            float val1 = vector::fp16_to_fp32(val1_raw);
            float val2 = vector::fp16_to_fp32(val2_raw);
            float res = canonicalize_nan(val2 + val1);
            vector::set_group_element<uint16_t>(cpu.state().regs, rd, i, vector::fp32_to_fp16(res));
        } else if constexpr (std::is_same_v<T, float>) {
            float val1 = 0;
            if ((rs1_val & simrv::xlen::kF32BoxerBits) != simrv::xlen::kF32BoxerBits) {
                val1 = std::bit_cast<float>(0x7fc00000U);
            } else {
                val1 = std::bit_cast<float>(static_cast<uint32_t>(rs1_val & 0xFFFFFFFFULL));
            }
            auto val2 = vector::get_group_element<float>(cpu.state().regs, rs2, i);
            float res = val2 + val1;
            vector::set_group_element<float>(cpu.state().regs, rd, i, res);
        } else {
            double val1 = std::bit_cast<double>(rs1_val);
            auto val2 = vector::get_group_element<double>(cpu.state().regs, rs2, i);
            double res = canonicalize_nan(val2 + val1);
            vector::set_group_element<double>(cpu.state().regs, rd, i, res);
        }
    }
}

// Vector floating-point MAC helpers
template <typename T>
void perform_vfmacc_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val1 = vector::get_group_element<T>(cpu.state().regs, rs1, i);
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        T dest_val = vector::get_group_element<T>(cpu.state().regs, rd, i);
        // VFMACC is architecturally fused and performs only one rounding.
        T res = canonicalize_nan(std::fma(val2, val1, dest_val));
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
    }
}

template <typename T>
void perform_vfmacc_vf(core::CPU& cpu, RegId rd, FloatingRegister rs1_val, RegId rs2, bool vm,
                       uint32_t vl) {
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

    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        T dest_val = vector::get_group_element<T>(cpu.state().regs, rd, i);
        // VFMACC is architecturally fused and performs only one rounding.
        T res = canonicalize_nan(std::fma(val2, scalar, dest_val));
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
    }
}

}  // namespace

void ExecuteUnit::execute_vector_float(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1,
                                       RegId rs2, bool vm, uint32_t vl, uint32_t sew) {
    const int old_round = std::fegetround();
    std::fesetround(frm_to_host_round(cpu.state().fcsr));
    std::feclearexcept(FE_ALL_EXCEPT);

    switch (op_id) {
        case isa::OperationId::VFMACC_VV:
            if (sew == 32)
                perform_vfmacc_vv<float>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 64)
                perform_vfmacc_vv<double>(cpu, rd, rs1, rs2, vm, vl);
            break;
        case isa::OperationId::VFMACC_VF: {
            FloatingRegister rs1_fp_val = cpu.state().regs.read_fp(rs1);
            if (sew == 32)
                perform_vfmacc_vf<float>(cpu, rd, rs1_fp_val, rs2, vm, vl);
            else if (sew == 64)
                perform_vfmacc_vf<double>(cpu, rd, rs1_fp_val, rs2, vm, vl);
            break;
        }
        case isa::OperationId::VFADD_VV:
            if (sew == 16)
                execute_vfadd_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 32)
                execute_vfadd_vv<float>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 64)
                execute_vfadd_vv<double>(cpu, rd, rs1, rs2, vm, vl);
            break;
        case isa::OperationId::VFADD_VF: {
            FloatingRegister rs1_fp_val = cpu.state().regs.read_fp(rs1);
            if (sew == 16)
                execute_vfadd_vf<uint16_t>(cpu, rd, rs1_fp_val, rs2, vm, vl);
            else if (sew == 32)
                execute_vfadd_vf<float>(cpu, rd, rs1_fp_val, rs2, vm, vl);
            else if (sew == 64)
                execute_vfadd_vf<double>(cpu, rd, rs1_fp_val, rs2, vm, vl);
            break;
        }
        default:
            break;
    }

    cpu.state().fcsr |= host_exceptions_to_fflags(std::fetestexcept(FE_ALL_EXCEPT));
    std::fesetround(old_round);
}

}  // namespace simrv::execute
