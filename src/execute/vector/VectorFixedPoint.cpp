#include "VectorHelpers.hpp"
#include "simrv/execute/ExecuteUnit.hpp"

namespace simrv::execute {

namespace {

__extension__ using SignedDoubleWord = __int128;

template <typename T>
using AverageWide = std::conditional_t<
    sizeof(T) == 1, int16_t,
    std::conditional_t<sizeof(T) == 2, int32_t,
                       std::conditional_t<sizeof(T) == 4, int64_t, SignedDoubleWord>>>;

template <typename Wide>
auto round_average(Wide value, uint32_t vxrm) -> Wide {
    const Wide shifted = value >> 1;
    const bool discarded = (value & Wide{1}) != 0;
    switch (vxrm & 0x3U) {
        case 0:  // rnu: round to nearest, ties up
            return shifted + static_cast<Wide>(discarded);
        case 1:  // rne: round to nearest, ties to even
            return shifted + static_cast<Wide>(discarded && ((shifted & Wide{1}) != 0));
        case 2:  // rdn: truncate (round down)
            return shifted;
        case 3:  // rod: round to odd
            return shifted | static_cast<Wide>(discarded);
        default:
            return shifted;
    }
}

template <typename T, bool Subtract>
auto average_element(T lhs, T rhs, uint32_t vxrm) -> T {
    using Wide = AverageWide<T>;
    const Wide wide_lhs = static_cast<Wide>(lhs);
    const Wide wide_rhs = static_cast<Wide>(rhs);
    const Wide exact = Subtract ? wide_lhs - wide_rhs : wide_lhs + wide_rhs;
    return static_cast<T>(round_average(exact, vxrm));
}

template <typename T, bool Subtract>
void execute_average_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        const T lhs = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        const T rhs = vector::get_group_element<T>(cpu.state().regs, rs1, i);
        vector::set_group_element<T>(cpu.state().regs, rd, i,
                                     average_element<T, Subtract>(lhs, rhs, cpu.state().vxrm));
    }
}

template <typename T, bool Subtract>
void execute_average_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm,
                        uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    const T rhs = vector::integer_scalar<T>(rs1_val);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        const T lhs = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        vector::set_group_element<T>(cpu.state().regs, rd, i,
                                     average_element<T, Subtract>(lhs, rhs, cpu.state().vxrm));
    }
}

// Saturating Add/Sub signed/unsigned
template <typename T, typename Op>
void execute_vsadd_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, Op op) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    bool saturated = false;
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
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
    T val1 = vector::integer_scalar<T>(rs1_val);
    bool saturated = false;
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
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
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
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
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
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
    T val1 = vector::integer_scalar<T>(rs1_val);
    bool saturated = false;
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
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
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
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
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
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
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
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

    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
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

void execute_average_family(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1, RegId rs2,
                            bool vm, uint32_t vl, uint32_t sew, Register rs1_val) {
    const bool is_unsigned =
        op_id == isa::OperationId::VAADDU_VV || op_id == isa::OperationId::VAADDU_VX ||
        op_id == isa::OperationId::VASUBU_VV || op_id == isa::OperationId::VASUBU_VX;
    const bool subtract =
        op_id == isa::OperationId::VASUBU_VV || op_id == isa::OperationId::VASUBU_VX ||
        op_id == isa::OperationId::VASUB_VV || op_id == isa::OperationId::VASUB_VX;
    const bool scalar = op_id == isa::OperationId::VAADDU_VX ||
                        op_id == isa::OperationId::VAADD_VX ||
                        op_id == isa::OperationId::VASUBU_VX || op_id == isa::OperationId::VASUB_VX;

    dispatch_sew_type(sew, [&]<size_t Bits>() {
        using Signed = std::conditional_t<
            Bits == 8, int8_t,
            std::conditional_t<Bits == 16, int16_t,
                               std::conditional_t<Bits == 32, int32_t, int64_t>>>;
        using Unsigned = std::make_unsigned_t<Signed>;
        if (is_unsigned) {
            if (scalar) {
                if (subtract)
                    execute_average_vx<Unsigned, true>(cpu, rd, rs1_val, rs2, vm, vl);
                else
                    execute_average_vx<Unsigned, false>(cpu, rd, rs1_val, rs2, vm, vl);
            } else if (subtract) {
                execute_average_vv<Unsigned, true>(cpu, rd, rs1, rs2, vm, vl);
            } else {
                execute_average_vv<Unsigned, false>(cpu, rd, rs1, rs2, vm, vl);
            }
        } else if (scalar) {
            if (subtract)
                execute_average_vx<Signed, true>(cpu, rd, rs1_val, rs2, vm, vl);
            else
                execute_average_vx<Signed, false>(cpu, rd, rs1_val, rs2, vm, vl);
        } else if (subtract) {
            execute_average_vv<Signed, true>(cpu, rd, rs1, rs2, vm, vl);
        } else {
            execute_average_vv<Signed, false>(cpu, rd, rs1, rs2, vm, vl);
        }
    });
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
            execute_vnclip<uint8_t, uint16_t>(cpu, rd, rs1, rs2, vm, vl, is_vx, is_vi, rs1_val,
                                              simm5);
        else
            execute_vnclip<int8_t, int16_t>(cpu, rd, rs1, rs2, vm, vl, is_vx, is_vi, rs1_val,
                                            simm5);
    } else if (sew == 16) {
        if (is_unsigned)
            execute_vnclip<uint16_t, uint32_t>(cpu, rd, rs1, rs2, vm, vl, is_vx, is_vi, rs1_val,
                                               simm5);
        else
            execute_vnclip<int16_t, int32_t>(cpu, rd, rs1, rs2, vm, vl, is_vx, is_vi, rs1_val,
                                             simm5);
    } else {
        if (is_unsigned)
            execute_vnclip<uint32_t, uint64_t>(cpu, rd, rs1, rs2, vm, vl, is_vx, is_vi, rs1_val,
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

    if (op_id >= isa::OperationId::VAADDU_VV && op_id <= isa::OperationId::VASUB_VX) {
        execute_average_family(cpu, op_id, rd, rs1, rs2, vm, vl, sew, rs1_val);
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
