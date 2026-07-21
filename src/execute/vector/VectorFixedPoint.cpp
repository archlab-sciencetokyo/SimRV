#include "simrv/execute/ExecuteUnit.hpp"
#include "VectorHelpers.hpp"

namespace simrv::execute {

namespace {

// Saturating Add/Sub signed/unsigned
template <typename T, typename Op>
void execute_vsadd_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, Op op) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    bool saturated = false;
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        T val1 = vector::get_group_element<T>(cpu.state().regs, rs1, i);
        T res = op(val2, val1, saturated);
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
    }
    if (saturated) {
        cpu.state().vxsat = 1;
    }
}

template <typename T, typename Op>
void execute_vsadd_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl, Op op) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(rs1_val);
    bool saturated = false;
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        T res = op(val2, val1, saturated);
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
    }
    if (saturated) {
        cpu.state().vxsat = 1;
    }
}

template <typename T, typename Op>
void execute_vsadd_vi(core::CPU& cpu, RegId rd, int32_t imm, RegId rs2, bool vm, uint32_t vl, Op op) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(imm);
    bool saturated = false;
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        T res = op(val2, val1, saturated);
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
    }
    if (saturated) {
        cpu.state().vxsat = 1;
    }
}

// Saturating Multiply Fractional
template <typename T>
void execute_vsmul_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, uint32_t vxrm) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    bool saturated = false;
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        T val1 = vector::get_group_element<T>(cpu.state().regs, rs1, i);
        T res = vector::execute_vsmul_element<T>(val2, val1, vxrm, saturated);
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
    }
    if (saturated) {
        cpu.state().vxsat = 1;
    }
}

template <typename T>
void execute_vsmul_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl, uint32_t vxrm) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(rs1_val);
    bool saturated = false;
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        T res = vector::execute_vsmul_element<T>(val2, val1, vxrm, saturated);
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
    }
    if (saturated) {
        cpu.state().vxsat = 1;
    }
}

// Rounding Shift Right
template <typename T>
void execute_vsshr_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, uint32_t vxrm) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        T val1 = vector::get_group_element<T>(cpu.state().regs, rs1, i);
        uint32_t shift = static_cast<uint32_t>(val1) & (sizeof(T) * 8 - 1);
        T res = vector::round_shift<T, T>(val2, shift, vxrm);
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
    }
}

template <typename T>
void execute_vsshr_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl, uint32_t vxrm) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    uint32_t shift = static_cast<uint32_t>(rs1_val) & (sizeof(T) * 8 - 1);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        T res = vector::round_shift<T, T>(val2, shift, vxrm);
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
    }
}

template <typename T>
void execute_vsshr_vi(core::CPU& cpu, RegId rd, int32_t imm, RegId rs2, bool vm, uint32_t vl, uint32_t vxrm) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    uint32_t shift = static_cast<uint32_t>(imm) & (sizeof(T) * 8 - 1);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        T res = vector::round_shift<T, T>(val2, shift, vxrm);
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
    }
}

// Vector Narrowing Fixed-Point Clip (Generic)
template <typename T_dest, typename T_src>
void execute_vnclip(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, bool is_vx, bool is_vi, Register rs1_val, int32_t imm) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    uint32_t vxrm = cpu.state().vxrm;
    bool saturated = false;
    
    uint32_t sew_width = sizeof(T_dest) * 8;
    uint32_t shift_mask = 2 * sew_width - 1;
    
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        
        uint32_t shift = 0;
        if (is_vi) {
            shift = static_cast<uint32_t>(imm) & shift_mask;
        } else if (is_vx) {
            shift = static_cast<uint32_t>(rs1_val) & shift_mask;
        } else {
            shift = static_cast<uint32_t>(vector::get_group_element<T_dest>(cpu.state().regs, rs1, i)) & shift_mask;
        }
        
        T_src val2 = vector::get_group_element<T_src>(cpu.state().regs, rs2, i);
        T_dest res = vector::round_and_clip<T_dest, T_src>(val2, shift, vxrm, saturated);
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
    
    if (saturated) {
        cpu.state().vxsat = 1;
    }
}

} // namespace

void ExecuteUnit::execute_vector_fixed_point(core::CPU& cpu, isa::OperationId op_id,
                                             RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, uint32_t sew,
                                             Register rs1_val, int32_t simm5) {
    // Execute Vector Narrowing Fixed-Point Clip (Signed / Unsigned)
    if (op_id >= isa::OperationId::VNCLIP_WV && op_id <= isa::OperationId::VNCLIPU_WI) {
        bool is_vx = (op_id == isa::OperationId::VNCLIP_WX || op_id == isa::OperationId::VNCLIPU_WX);
        bool is_vi = (op_id == isa::OperationId::VNCLIP_WI || op_id == isa::OperationId::VNCLIPU_WI);
        bool is_unsigned = (op_id >= isa::OperationId::VNCLIPU_WV && op_id <= isa::OperationId::VNCLIPU_WI);
        
        if (sew == 8) {
            if (is_unsigned) execute_vnclip<uint8_t, int16_t>(cpu, rd, rs1, rs2, vm, vl, is_vx, is_vi, rs1_val, simm5);
            else execute_vnclip<int8_t, int16_t>(cpu, rd, rs1, rs2, vm, vl, is_vx, is_vi, rs1_val, simm5);
        } else if (sew == 16) {
            if (is_unsigned) execute_vnclip<uint16_t, int32_t>(cpu, rd, rs1, rs2, vm, vl, is_vx, is_vi, rs1_val, simm5);
            else execute_vnclip<int16_t, int32_t>(cpu, rd, rs1, rs2, vm, vl, is_vx, is_vi, rs1_val, simm5);
        } else {
            if (is_unsigned) execute_vnclip<uint32_t, int64_t>(cpu, rd, rs1, rs2, vm, vl, is_vx, is_vi, rs1_val, simm5);
            else execute_vnclip<int32_t, int64_t>(cpu, rd, rs1, rs2, vm, vl, is_vx, is_vi, rs1_val, simm5);
        }
        return;
    }

    switch (op_id) {
        case isa::OperationId::VSADD_VV:
            if (sew == 8) execute_vsadd_vv<int8_t>(cpu, rd, rs1, rs2, vm, vl, vector::sat_add_signed<int8_t>);
            else if (sew == 16) execute_vsadd_vv<int16_t>(cpu, rd, rs1, rs2, vm, vl, vector::sat_add_signed<int16_t>);
            else if (sew == 32) execute_vsadd_vv<int32_t>(cpu, rd, rs1, rs2, vm, vl, vector::sat_add_signed<int32_t>);
            else execute_vsadd_vv<int64_t>(cpu, rd, rs1, rs2, vm, vl, vector::sat_add_signed<int64_t>);
            return;
        case isa::OperationId::VSADD_VX:
            if (sew == 8) execute_vsadd_vx<int8_t>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_add_signed<int8_t>);
            else if (sew == 16) execute_vsadd_vx<int16_t>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_add_signed<int16_t>);
            else if (sew == 32) execute_vsadd_vx<int32_t>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_add_signed<int32_t>);
            else execute_vsadd_vx<int64_t>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_add_signed<int64_t>);
            return;
        case isa::OperationId::VSADD_VI:
            if (sew == 8) execute_vsadd_vi<int8_t>(cpu, rd, simm5, rs2, vm, vl, vector::sat_add_signed<int8_t>);
            else if (sew == 16) execute_vsadd_vi<int16_t>(cpu, rd, simm5, rs2, vm, vl, vector::sat_add_signed<int16_t>);
            else if (sew == 32) execute_vsadd_vi<int32_t>(cpu, rd, simm5, rs2, vm, vl, vector::sat_add_signed<int32_t>);
            else execute_vsadd_vi<int64_t>(cpu, rd, simm5, rs2, vm, vl, vector::sat_add_signed<int64_t>);
            return;
        case isa::OperationId::VSADDU_VV:
            if (sew == 8) execute_vsadd_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, vector::sat_add_unsigned<uint8_t>);
            else if (sew == 16) execute_vsadd_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, vector::sat_add_unsigned<uint16_t>);
            else if (sew == 32) execute_vsadd_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, vector::sat_add_unsigned<uint32_t>);
            else execute_vsadd_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, vector::sat_add_unsigned<uint64_t>);
            return;
        case isa::OperationId::VSADDU_VX:
            if (sew == 8) execute_vsadd_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_add_unsigned<uint8_t>);
            else if (sew == 16) execute_vsadd_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_add_unsigned<uint16_t>);
            else if (sew == 32) execute_vsadd_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_add_unsigned<uint32_t>);
            else execute_vsadd_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_add_unsigned<uint64_t>);
            return;
        case isa::OperationId::VSADDU_VI:
            if (sew == 8) execute_vsadd_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl, vector::sat_add_unsigned<uint8_t>);
            else if (sew == 16) execute_vsadd_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl, vector::sat_add_unsigned<uint16_t>);
            else if (sew == 32) execute_vsadd_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl, vector::sat_add_unsigned<uint32_t>);
            else execute_vsadd_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl, vector::sat_add_unsigned<uint64_t>);
            return;
        case isa::OperationId::VSMUL_VV: {
            uint32_t vxrm = cpu.state().vxrm;
            if (sew == 8) execute_vsmul_vv<int8_t>(cpu, rd, rs1, rs2, vm, vl, vxrm);
            else if (sew == 16) execute_vsmul_vv<int16_t>(cpu, rd, rs1, rs2, vm, vl, vxrm);
            else if (sew == 32) execute_vsmul_vv<int32_t>(cpu, rd, rs1, rs2, vm, vl, vxrm);
            else execute_vsmul_vv<int64_t>(cpu, rd, rs1, rs2, vm, vl, vxrm);
            return;
        }
        case isa::OperationId::VSMUL_VX: {
            uint32_t vxrm = cpu.state().vxrm;
            if (sew == 8) execute_vsmul_vx<int8_t>(cpu, rd, rs1_val, rs2, vm, vl, vxrm);
            else if (sew == 16) execute_vsmul_vx<int16_t>(cpu, rd, rs1_val, rs2, vm, vl, vxrm);
            else if (sew == 32) execute_vsmul_vx<int32_t>(cpu, rd, rs1_val, rs2, vm, vl, vxrm);
            else execute_vsmul_vx<int64_t>(cpu, rd, rs1_val, rs2, vm, vl, vxrm);
            return;
        }
        case isa::OperationId::VSSRA_VV: {
            uint32_t vxrm = cpu.state().vxrm;
            if (sew == 8) execute_vsshr_vv<int8_t>(cpu, rd, rs1, rs2, vm, vl, vxrm);
            else if (sew == 16) execute_vsshr_vv<int16_t>(cpu, rd, rs1, rs2, vm, vl, vxrm);
            else if (sew == 32) execute_vsshr_vv<int32_t>(cpu, rd, rs1, rs2, vm, vl, vxrm);
            else execute_vsshr_vv<int64_t>(cpu, rd, rs1, rs2, vm, vl, vxrm);
            return;
        }
        case isa::OperationId::VSSRA_VX: {
            uint32_t vxrm = cpu.state().vxrm;
            if (sew == 8) execute_vsshr_vx<int8_t>(cpu, rd, rs1_val, rs2, vm, vl, vxrm);
            else if (sew == 16) execute_vsshr_vx<int16_t>(cpu, rd, rs1_val, rs2, vm, vl, vxrm);
            else if (sew == 32) execute_vsshr_vx<int32_t>(cpu, rd, rs1_val, rs2, vm, vl, vxrm);
            else execute_vsshr_vx<int64_t>(cpu, rd, rs1_val, rs2, vm, vl, vxrm);
            return;
        }
        case isa::OperationId::VSSRA_VI: {
            uint32_t vxrm = cpu.state().vxrm;
            if (sew == 8) execute_vsshr_vi<int8_t>(cpu, rd, simm5, rs2, vm, vl, vxrm);
            else if (sew == 16) execute_vsshr_vi<int16_t>(cpu, rd, simm5, rs2, vm, vl, vxrm);
            else if (sew == 32) execute_vsshr_vi<int32_t>(cpu, rd, simm5, rs2, vm, vl, vxrm);
            else execute_vsshr_vi<int64_t>(cpu, rd, simm5, rs2, vm, vl, vxrm);
            return;
        }
        case isa::OperationId::VSSRL_VV: {
            uint32_t vxrm = cpu.state().vxrm;
            if (sew == 8) execute_vsshr_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, vxrm);
            else if (sew == 16) execute_vsshr_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, vxrm);
            else if (sew == 32) execute_vsshr_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, vxrm);
            else execute_vsshr_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, vxrm);
            return;
        }
        case isa::OperationId::VSSRL_VX: {
            uint32_t vxrm = cpu.state().vxrm;
            if (sew == 8) execute_vsshr_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, vxrm);
            else if (sew == 16) execute_vsshr_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, vxrm);
            else if (sew == 32) execute_vsshr_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, vxrm);
            else execute_vsshr_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, vxrm);
            return;
        }
        case isa::OperationId::VSSRL_VI: {
            uint32_t vxrm = cpu.state().vxrm;
            if (sew == 8) execute_vsshr_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl, vxrm);
            else if (sew == 16) execute_vsshr_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl, vxrm);
            else if (sew == 32) execute_vsshr_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl, vxrm);
            else execute_vsshr_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl, vxrm);
            return;
        }
        case isa::OperationId::VSSUB_VV:
            if (sew == 8) execute_vsadd_vv<int8_t>(cpu, rd, rs1, rs2, vm, vl, vector::sat_sub_signed<int8_t>);
            else if (sew == 16) execute_vsadd_vv<int16_t>(cpu, rd, rs1, rs2, vm, vl, vector::sat_sub_signed<int16_t>);
            else if (sew == 32) execute_vsadd_vv<int32_t>(cpu, rd, rs1, rs2, vm, vl, vector::sat_sub_signed<int32_t>);
            else execute_vsadd_vv<int64_t>(cpu, rd, rs1, rs2, vm, vl, vector::sat_sub_signed<int64_t>);
            return;
        case isa::OperationId::VSSUB_VX:
            if (sew == 8) execute_vsadd_vx<int8_t>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_sub_signed<int8_t>);
            else if (sew == 16) execute_vsadd_vx<int16_t>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_sub_signed<int16_t>);
            else if (sew == 32) execute_vsadd_vx<int32_t>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_sub_signed<int32_t>);
            else execute_vsadd_vx<int64_t>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_sub_signed<int64_t>);
            return;
        case isa::OperationId::VSSUBU_VV:
            if (sew == 8) execute_vsadd_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, vector::sat_sub_unsigned<uint8_t>);
            else if (sew == 16) execute_vsadd_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, vector::sat_sub_unsigned<uint16_t>);
            else if (sew == 32) execute_vsadd_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, vector::sat_sub_unsigned<uint32_t>);
            else execute_vsadd_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, vector::sat_sub_unsigned<uint64_t>);
            return;
        case isa::OperationId::VSSUBU_VX:
            if (sew == 8) execute_vsadd_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_sub_unsigned<uint8_t>);
            else if (sew == 16) execute_vsadd_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_sub_unsigned<uint16_t>);
            else if (sew == 32) execute_vsadd_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_sub_unsigned<uint32_t>);
            else execute_vsadd_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_sub_unsigned<uint64_t>);
            return;
        default:
            break;
    }
}

} // namespace simrv::execute
