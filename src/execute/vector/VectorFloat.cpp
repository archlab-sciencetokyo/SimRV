#include "simrv/execute/ExecuteUnit.hpp"
#include "VectorHelpers.hpp"

namespace simrv::execute {

namespace {

// Vector-Vector Floating-Point Addition
template <typename T>
void execute_vfadd_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        
        if constexpr (std::is_same_v<T, uint16_t>) {
            uint16_t val1_raw = vector::get_group_element<uint16_t>(cpu.state().regs, rs1, i);
            uint16_t val2_raw = vector::get_group_element<uint16_t>(cpu.state().regs, rs2, i);
            float val1 = vector::fp16_to_fp32(val1_raw);
            float val2 = vector::fp16_to_fp32(val2_raw);
            float res = val2 + val1;
            vector::set_group_element<uint16_t>(cpu.state().regs, rd, i, vector::fp32_to_fp16(res));
        } else {
            T val1 = vector::get_group_element<T>(cpu.state().regs, rs1, i);
            T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
            T res = val2 + val1;
            vector::set_group_element<T>(cpu.state().regs, rd, i, res);
        }
    }
}

// Vector-Scalar Floating-Point Addition
template <typename T>
void execute_vfadd_vf(core::CPU& cpu, RegId rd, FloatingRegister rs1_val, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        
        if constexpr (std::is_same_v<T, uint16_t>) {
            uint16_t val1_raw = static_cast<uint16_t>(rs1_val & 0xFFFFULL);
            uint16_t val2_raw = vector::get_group_element<uint16_t>(cpu.state().regs, rs2, i);
            float val1 = vector::fp16_to_fp32(val1_raw);
            float val2 = vector::fp16_to_fp32(val2_raw);
            float res = val2 + val1;
            vector::set_group_element<uint16_t>(cpu.state().regs, rd, i, vector::fp32_to_fp16(res));
        } else if constexpr (std::is_same_v<T, float>) {
            float val1 = 0;
            if ((rs1_val & simrv::xlen::kF32BoxerBits) != simrv::xlen::kF32BoxerBits) {
                val1 = std::bit_cast<float>(0x7fc00000U);
            } else {
                val1 = std::bit_cast<float>(static_cast<uint32_t>(rs1_val & 0xFFFFFFFFULL));
            }
            float val2 = vector::get_group_element<float>(cpu.state().regs, rs2, i);
            float res = val2 + val1;
            vector::set_group_element<float>(cpu.state().regs, rd, i, res);
        } else {
            double val1 = std::bit_cast<double>(rs1_val);
            double val2 = vector::get_group_element<double>(cpu.state().regs, rs2, i);
            double res = val2 + val1;
            vector::set_group_element<double>(cpu.state().regs, rd, i, res);
        }
    }
}

// Vector floating-point MAC helpers
template <typename T>
void perform_vfmacc_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val1 = vector::get_group_element<T>(cpu.state().regs, rs1, i);
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        T dest_val = vector::get_group_element<T>(cpu.state().regs, rd, i);
        T res = dest_val + val2 * val1;
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
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
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        T dest_val = vector::get_group_element<T>(cpu.state().regs, rd, i);
        T res = dest_val + val2 * scalar;
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
    }
}

} // namespace

void ExecuteUnit::execute_vector_float(core::CPU& cpu, isa::OperationId op_id,
                                      RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, uint32_t sew) {
    switch (op_id) {
        case isa::OperationId::VFMACC_VV:
            if (sew == 32) perform_vfmacc_vv<float>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 64) perform_vfmacc_vv<double>(cpu, rd, rs1, rs2, vm, vl);
            break;
        case isa::OperationId::VFMACC_VF: {
            FloatingRegister rs1_fp_val = cpu.state().regs.read_fp(rs1);
            if (sew == 32) perform_vfmacc_vf<float>(cpu, rd, rs1_fp_val, rs2, vm, vl);
            else if (sew == 64) perform_vfmacc_vf<double>(cpu, rd, rs1_fp_val, rs2, vm, vl);
            break;
        }
        case isa::OperationId::VFADD_VV:
            if (sew == 16) execute_vfadd_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 32) execute_vfadd_vv<float>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 64) execute_vfadd_vv<double>(cpu, rd, rs1, rs2, vm, vl);
            break;
        case isa::OperationId::VFADD_VF: {
            FloatingRegister rs1_fp_val = cpu.state().regs.read_fp(rs1);
            if (sew == 16) execute_vfadd_vf<uint16_t>(cpu, rd, rs1_fp_val, rs2, vm, vl);
            else if (sew == 32) execute_vfadd_vf<float>(cpu, rd, rs1_fp_val, rs2, vm, vl);
            else if (sew == 64) execute_vfadd_vf<double>(cpu, rd, rs1_fp_val, rs2, vm, vl);
            break;
        }
        default:
            break;
    }
}

} // namespace simrv::execute
