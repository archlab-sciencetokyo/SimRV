#include "VectorHelpers.hpp"
#include "simrv/execute/ExecuteUnit.hpp"

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
void execute_vsadd_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl,
                      Op op) {
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
void execute_vsadd_vi(core::CPU& cpu, RegId rd, int32_t imm, RegId rs2, bool vm, uint32_t vl,
                      Op op) {
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
void execute_vsmul_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl,
                      uint32_t vxrm) {
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
void execute_vsmul_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl,
                      uint32_t vxrm) {
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
void execute_vsshr_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl,
                      uint32_t vxrm) {
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
void execute_vsshr_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl,
                      uint32_t vxrm) {
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
void execute_vsshr_vi(core::CPU& cpu, RegId rd, int32_t imm, RegId rs2, bool vm, uint32_t vl,
                      uint32_t vxrm) {
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
void execute_vnclip(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl,
                    bool is_vx, bool is_vi, Register rs1_val, int32_t imm) {
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
            shift =
                static_cast<uint32_t>(vector::get_group_element<T_dest>(cpu.state().regs, rs1, i)) &
                shift_mask;
        }

        auto val2 = vector::get_group_element<T_src>(cpu.state().regs, rs2, i);
        auto res = vector::round_and_clip<T_dest, T_src>(val2, shift, vxrm, saturated);
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }

    if (saturated) {
        cpu.state().vxsat = 1;
    }
}

template <typename Func>
void dispatch_sew_type(uint32_t sew, Func&& func) {
    if (sew == 8)
        std::forward<Func>(func).template operator()<8>();
    else if (sew == 16)
        std::forward<Func>(func).template operator()<16>();
    else if (sew == 32)
        std::forward<Func>(func).template operator()<32>();
    else
        std::forward<Func>(func).template operator()<64>();
}

void execute_vsadd_family(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1, RegId rs2,
                          bool vm, uint32_t vl, uint32_t sew, Register rs1_val, int32_t simm5) {
    switch (op_id) {
        case isa::OperationId::VSADD_VV:
            dispatch_sew_type(sew, [&]<size_t Bits>() {
                using T = std::conditional_t<
                    Bits == 8, int8_t,
                    std::conditional_t<Bits == 16, int16_t,
                                       std::conditional_t<Bits == 32, int32_t, int64_t>>>;
                execute_vsadd_vv<T>(cpu, rd, rs1, rs2, vm, vl, vector::sat_add_signed<T>);
            });
            break;
        case isa::OperationId::VSADD_VX:
            dispatch_sew_type(sew, [&]<size_t Bits>() {
                using T = std::conditional_t<
                    Bits == 8, int8_t,
                    std::conditional_t<Bits == 16, int16_t,
                                       std::conditional_t<Bits == 32, int32_t, int64_t>>>;
                execute_vsadd_vx<T>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_add_signed<T>);
            });
            break;
        case isa::OperationId::VSADD_VI:
            dispatch_sew_type(sew, [&]<size_t Bits>() {
                using T = std::conditional_t<
                    Bits == 8, int8_t,
                    std::conditional_t<Bits == 16, int16_t,
                                       std::conditional_t<Bits == 32, int32_t, int64_t>>>;
                execute_vsadd_vi<T>(cpu, rd, simm5, rs2, vm, vl, vector::sat_add_signed<T>);
            });
            break;
        case isa::OperationId::VSADDU_VV:
            dispatch_sew_type(sew, [&]<size_t Bits>() {
                using T = std::conditional_t<
                    Bits == 8, uint8_t,
                    std::conditional_t<Bits == 16, uint16_t,
                                       std::conditional_t<Bits == 32, uint32_t, uint64_t>>>;
                execute_vsadd_vv<T>(cpu, rd, rs1, rs2, vm, vl, vector::sat_add_unsigned<T>);
            });
            break;
        case isa::OperationId::VSADDU_VX:
            dispatch_sew_type(sew, [&]<size_t Bits>() {
                using T = std::conditional_t<
                    Bits == 8, uint8_t,
                    std::conditional_t<Bits == 16, uint16_t,
                                       std::conditional_t<Bits == 32, uint32_t, uint64_t>>>;
                execute_vsadd_vx<T>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_add_unsigned<T>);
            });
            break;
        case isa::OperationId::VSADDU_VI:
            dispatch_sew_type(sew, [&]<size_t Bits>() {
                using T = std::conditional_t<
                    Bits == 8, uint8_t,
                    std::conditional_t<Bits == 16, uint16_t,
                                       std::conditional_t<Bits == 32, uint32_t, uint64_t>>>;
                execute_vsadd_vi<T>(cpu, rd, simm5, rs2, vm, vl, vector::sat_add_unsigned<T>);
            });
            break;
        default:
            break;
    }
}

void execute_vssub_family(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1, RegId rs2,
                          bool vm, uint32_t vl, uint32_t sew, Register rs1_val) {
    switch (op_id) {
        case isa::OperationId::VSSUB_VV:
            dispatch_sew_type(sew, [&]<size_t Bits>() {
                using T = std::conditional_t<
                    Bits == 8, int8_t,
                    std::conditional_t<Bits == 16, int16_t,
                                       std::conditional_t<Bits == 32, int32_t, int64_t>>>;
                execute_vsadd_vv<T>(cpu, rd, rs1, rs2, vm, vl, vector::sat_sub_signed<T>);
            });
            break;
        case isa::OperationId::VSSUB_VX:
            dispatch_sew_type(sew, [&]<size_t Bits>() {
                using T = std::conditional_t<
                    Bits == 8, int8_t,
                    std::conditional_t<Bits == 16, int16_t,
                                       std::conditional_t<Bits == 32, int32_t, int64_t>>>;
                execute_vsadd_vx<T>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_sub_signed<T>);
            });
            break;
        case isa::OperationId::VSSUBU_VV:
            dispatch_sew_type(sew, [&]<size_t Bits>() {
                using T = std::conditional_t<
                    Bits == 8, uint8_t,
                    std::conditional_t<Bits == 16, uint16_t,
                                       std::conditional_t<Bits == 32, uint32_t, uint64_t>>>;
                execute_vsadd_vv<T>(cpu, rd, rs1, rs2, vm, vl, vector::sat_sub_unsigned<T>);
            });
            break;
        case isa::OperationId::VSSUBU_VX:
            dispatch_sew_type(sew, [&]<size_t Bits>() {
                using T = std::conditional_t<
                    Bits == 8, uint8_t,
                    std::conditional_t<Bits == 16, uint16_t,
                                       std::conditional_t<Bits == 32, uint32_t, uint64_t>>>;
                execute_vsadd_vx<T>(cpu, rd, rs1_val, rs2, vm, vl, vector::sat_sub_unsigned<T>);
            });
            break;
        default:
            break;
    }
}

void execute_vsshr_family(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1, RegId rs2,
                          bool vm, uint32_t vl, uint32_t sew, Register rs1_val, int32_t simm5) {
    uint32_t vxrm = cpu.state().vxrm;
    switch (op_id) {
        case isa::OperationId::VSSRA_VV:
            dispatch_sew_type(sew, [&]<size_t Bits>() {
                using T = std::conditional_t<
                    Bits == 8, int8_t,
                    std::conditional_t<Bits == 16, int16_t,
                                       std::conditional_t<Bits == 32, int32_t, int64_t>>>;
                execute_vsshr_vv<T>(cpu, rd, rs1, rs2, vm, vl, vxrm);
            });
            break;
        case isa::OperationId::VSSRA_VX:
            dispatch_sew_type(sew, [&]<size_t Bits>() {
                using T = std::conditional_t<
                    Bits == 8, int8_t,
                    std::conditional_t<Bits == 16, int16_t,
                                       std::conditional_t<Bits == 32, int32_t, int64_t>>>;
                execute_vsshr_vx<T>(cpu, rd, rs1_val, rs2, vm, vl, vxrm);
            });
            break;
        case isa::OperationId::VSSRA_VI:
            dispatch_sew_type(sew, [&]<size_t Bits>() {
                using T = std::conditional_t<
                    Bits == 8, int8_t,
                    std::conditional_t<Bits == 16, int16_t,
                                       std::conditional_t<Bits == 32, int32_t, int64_t>>>;
                execute_vsshr_vi<T>(cpu, rd, simm5, rs2, vm, vl, vxrm);
            });
            break;
        case isa::OperationId::VSSRL_VV:
            dispatch_sew_type(sew, [&]<size_t Bits>() {
                using T = std::conditional_t<
                    Bits == 8, uint8_t,
                    std::conditional_t<Bits == 16, uint16_t,
                                       std::conditional_t<Bits == 32, uint32_t, uint64_t>>>;
                execute_vsshr_vv<T>(cpu, rd, rs1, rs2, vm, vl, vxrm);
            });
            break;
        case isa::OperationId::VSSRL_VX:
            dispatch_sew_type(sew, [&]<size_t Bits>() {
                using T = std::conditional_t<
                    Bits == 8, uint8_t,
                    std::conditional_t<Bits == 16, uint16_t,
                                       std::conditional_t<Bits == 32, uint32_t, uint64_t>>>;
                execute_vsshr_vx<T>(cpu, rd, rs1_val, rs2, vm, vl, vxrm);
            });
            break;
        case isa::OperationId::VSSRL_VI:
            dispatch_sew_type(sew, [&]<size_t Bits>() {
                using T = std::conditional_t<
                    Bits == 8, uint8_t,
                    std::conditional_t<Bits == 16, uint16_t,
                                       std::conditional_t<Bits == 32, uint32_t, uint64_t>>>;
                execute_vsshr_vi<T>(cpu, rd, simm5, rs2, vm, vl, vxrm);
            });
            break;
        default:
            break;
    }
}

void execute_vsmul_family(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1, RegId rs2,
                          bool vm, uint32_t vl, uint32_t sew, Register rs1_val) {
    uint32_t vxrm = cpu.state().vxrm;
    switch (op_id) {
        case isa::OperationId::VSMUL_VV:
            dispatch_sew_type(sew, [&]<size_t Bits>() {
                using T = std::conditional_t<
                    Bits == 8, int8_t,
                    std::conditional_t<Bits == 16, int16_t,
                                       std::conditional_t<Bits == 32, int32_t, int64_t>>>;
                execute_vsmul_vv<T>(cpu, rd, rs1, rs2, vm, vl, vxrm);
            });
            break;
        case isa::OperationId::VSMUL_VX:
            dispatch_sew_type(sew, [&]<size_t Bits>() {
                using T = std::conditional_t<
                    Bits == 8, int8_t,
                    std::conditional_t<Bits == 16, int16_t,
                                       std::conditional_t<Bits == 32, int32_t, int64_t>>>;
                execute_vsmul_vx<T>(cpu, rd, rs1_val, rs2, vm, vl, vxrm);
            });
            break;
        default:
            break;
    }
}

void execute_vnclip_family(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1, RegId rs2,
                           bool vm, uint32_t vl, uint32_t sew, Register rs1_val, int32_t simm5) {
    bool is_vx = (op_id == isa::OperationId::VNCLIP_WX || op_id == isa::OperationId::VNCLIPU_WX);
    bool is_vi = (op_id == isa::OperationId::VNCLIP_WI || op_id == isa::OperationId::VNCLIPU_WI);
    bool is_unsigned =
        (op_id >= isa::OperationId::VNCLIPU_WV && op_id <= isa::OperationId::VNCLIPU_WI);

    if (sew == 8) {
        if (is_unsigned)
            execute_vnclip<uint8_t, int16_t>(cpu, rd, rs1, rs2, vm, vl, is_vx, is_vi, rs1_val,
                                             simm5);
        else
            execute_vnclip<int8_t, int16_t>(cpu, rd, rs1, rs2, vm, vl, is_vx, is_vi, rs1_val,
                                            simm5);
    } else if (sew == 16) {
        if (is_unsigned)
            execute_vnclip<uint16_t, int32_t>(cpu, rd, rs1, rs2, vm, vl, is_vx, is_vi, rs1_val,
                                              simm5);
        else
            execute_vnclip<int16_t, int32_t>(cpu, rd, rs1, rs2, vm, vl, is_vx, is_vi, rs1_val,
                                             simm5);
    } else {
        if (is_unsigned)
            execute_vnclip<uint32_t, int64_t>(cpu, rd, rs1, rs2, vm, vl, is_vx, is_vi, rs1_val,
                                              simm5);
        else
            execute_vnclip<int32_t, int64_t>(cpu, rd, rs1, rs2, vm, vl, is_vx, is_vi, rs1_val,
                                             simm5);
    }
}

}  // namespace

void ExecuteUnit::execute_vector_fixed_point(core::CPU& cpu, isa::OperationId op_id, RegId rd,
                                             RegId rs1, RegId rs2, bool vm, uint32_t vl,
                                             uint32_t sew, Register rs1_val, int32_t simm5) {
    // Execute Vector Narrowing Fixed-Point Clip (Signed / Unsigned)
    if (op_id >= isa::OperationId::VNCLIP_WV && op_id <= isa::OperationId::VNCLIPU_WI) {
        execute_vnclip_family(cpu, op_id, rd, rs1, rs2, vm, vl, sew, rs1_val, simm5);
        return;
    }

    if (op_id >= isa::OperationId::VSADD_VV && op_id <= isa::OperationId::VSADDU_VI) {
        execute_vsadd_family(cpu, op_id, rd, rs1, rs2, vm, vl, sew, rs1_val, simm5);
        return;
    }

    if (op_id >= isa::OperationId::VSSUB_VV && op_id <= isa::OperationId::VSSUBU_VX) {
        execute_vssub_family(cpu, op_id, rd, rs1, rs2, vm, vl, sew, rs1_val);
        return;
    }

    if ((op_id >= isa::OperationId::VSSRA_VV && op_id <= isa::OperationId::VSSRA_VI) ||
        (op_id >= isa::OperationId::VSSRL_VV && op_id <= isa::OperationId::VSSRL_VI)) {
        execute_vsshr_family(cpu, op_id, rd, rs1, rs2, vm, vl, sew, rs1_val, simm5);
        return;
    }

    if (op_id == isa::OperationId::VSMUL_VV || op_id == isa::OperationId::VSMUL_VX) {
        execute_vsmul_family(cpu, op_id, rd, rs1, rs2, vm, vl, sew, rs1_val);
        return;
    }
}

}  // namespace simrv::execute
