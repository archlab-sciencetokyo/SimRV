#include <algorithm>

#include "VectorHelpers.hpp"
#include "simrv/execute/ExecuteUnit.hpp"

namespace simrv::execute {

namespace {

uint32_t vector_vlmax(const core::CPU& cpu, uint32_t sew) {
    const uint32_t encoded_lmul = static_cast<uint32_t>(cpu.state().vtype) & 0x7U;
    if (encoded_lmul <= 3U) {
        return (cpu.state().regs.vlen << encoded_lmul) / sew;
    }
    if (encoded_lmul >= 5U) {
        return cpu.state().regs.vlen / (sew << (8U - encoded_lmul));
    }
    return 0;
}

uint64_t scalar_fp_bits(const core::CPU& cpu, uint64_t bits, uint32_t sew) {
    if (sew == 32 && isa::misa_has_extension(cpu.state().misa, isa::IsaExtension::D) &&
        (bits & simrv::xlen::kF32BoxerBits) != simrv::xlen::kF32BoxerBits) {
        return simrv::xlen::kF32Qnan;
    }
    return bits;
}

template <typename T, typename Index>
void execute_vrgather_vector(core::CPU& cpu, RegId rd, RegId index_reg, RegId rs2, bool vm,
                             uint32_t vl, uint32_t vlmax) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    std::vector<T> source(vlmax);
    std::vector<Index> indices(vl);
    for (uint32_t i = 0; i < vlmax; ++i) {
        source[i] = vector::get_group_element<T>(cpu.state().regs, rs2, i);
    }
    for (uint32_t i = 0; i < vl; ++i) {
        indices[i] = vector::get_group_element<Index>(cpu.state().regs, index_reg, i);
    }
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        const uint64_t index = indices[i];
        vector::set_group_element<T>(cpu.state().regs, rd, i, index < vlmax ? source[index] : T{0});
    }
}

template <typename T>
void execute_vrgather_scalar(core::CPU& cpu, RegId rd, uint64_t index, RegId rs2, bool vm,
                             uint32_t vl, uint32_t vlmax) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    const T value = index < vlmax ? vector::get_group_element<T>(cpu.state().regs, rs2,
                                                                 static_cast<uint32_t>(index))
                                  : T{0};
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        vector::set_group_element<T>(cpu.state().regs, rd, i, value);
    }
}

// Whole register move helper
void execute_vmv_whole(core::CPU& cpu, RegId rd, RegId rs2, uint32_t nr, uint32_t sew) {
    uint32_t total_bytes = nr * cpu.state().regs.vlen_bytes();
    const uint32_t first_byte = static_cast<uint32_t>(cpu.state().vstart) * (sew / 8U);
    for (uint32_t i = first_byte; i < total_bytes; i++) {
        uint32_t src_reg = (static_cast<uint32_t>(rs2) + (i / cpu.state().regs.vlen_bytes())) % 32;
        uint32_t dst_reg = (static_cast<uint32_t>(rd) + (i / cpu.state().regs.vlen_bytes())) % 32;
        uint8_t val = cpu.state()
                          .regs.read_vector(static_cast<RegId>(src_reg))
                          .u8[i % cpu.state().regs.vlen_bytes()];
        cpu.state()
            .regs.read_vector(static_cast<RegId>(dst_reg))
            .u8[i % cpu.state().regs.vlen_bytes()] = val;
    }
}

// Vector compress helper
template <typename T>
void execute_vcompress(core::CPU& cpu, RegId rd, RegId rs2, RegId rs1, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(rs1);
    std::vector<T> src_vals;
    src_vals.reserve(vl);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        if (vector::is_element_active(mask_reg, i, false)) {
            src_vals.push_back(vector::get_group_element<T>(cpu.state().regs, rs2, i));
        }
    }
    for (uint32_t i = 0; i < src_vals.size(); i++) {
        vector::set_group_element<T>(cpu.state().regs, rd, i, src_vals[i]);
    }
}

// Vector Slide Up
template <typename T>
void execute_vslideup(core::CPU& cpu, RegId rd, uint64_t offset, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    std::vector<T> src_vals(vl);
    for (uint32_t i = 0; i < vl; i++) {
        src_vals[i] = vector::get_group_element<T>(cpu.state().regs, rs2, i);
    }

    for (uint64_t i = std::max(offset, static_cast<uint64_t>(cpu.state().vstart)); i < vl; i++) {
        if (!vector::is_element_active(mask_reg, static_cast<uint32_t>(i), vm)) continue;
        T val = src_vals[static_cast<size_t>(i - offset)];
        vector::set_group_element<T>(cpu.state().regs, rd, static_cast<uint32_t>(i), val);
    }
}

// Vector Slide Down
template <typename T>
void execute_vslidedown(core::CPU& cpu, RegId rd, uint64_t offset, RegId rs2, bool vm,
                        uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    const uint32_t vlmax = vector_vlmax(cpu, sizeof(T) * 8U);

    std::vector<T> src_vals(vlmax);
    for (uint32_t i = 0; i < vlmax; i++) {
        src_vals[i] = vector::get_group_element<T>(cpu.state().regs, rs2, i);
    }

    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val =
            offset < vlmax && i < vlmax - offset ? src_vals[static_cast<size_t>(i + offset)] : T{0};
        vector::set_group_element<T>(cpu.state().regs, rd, i, val);
    }
}

// Vector Slide 1 Down
template <typename T>
void execute_vslide1down(core::CPU& cpu, RegId rd, FloatingRegister rs1_val, RegId rs2, bool vm,
                         uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T scalar = static_cast<T>(rs1_val);

    std::vector<T> temp(vl);
    for (uint32_t i = 0; i < vl; i++) {
        if (i + 1 < vl) {
            temp[i] = vector::get_group_element<T>(cpu.state().regs, rs2, i + 1);
        } else {
            temp[i] = scalar;
        }
    }

    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        vector::set_group_element<T>(cpu.state().regs, rd, i, temp[i]);
    }
}

// Vector Slide 1 Up
template <typename T>
void execute_vslide1up(core::CPU& cpu, RegId rd, FloatingRegister rs1_val, RegId rs2, bool vm,
                       uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T scalar = static_cast<T>(rs1_val);

    std::vector<T> temp(vl);
    for (uint32_t i = 0; i < vl; i++) {
        if (i > 0) {
            temp[i] = vector::get_group_element<T>(cpu.state().regs, rs2, i - 1);
        } else {
            temp[i] = scalar;
        }
    }

    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        vector::set_group_element<T>(cpu.state().regs, rd, i, temp[i]);
    }
}

template <typename T>
void execute_vmerge_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        bool mask_bit = vector::is_element_active(mask_reg, i, false);
        T val = mask_bit ? vector::get_group_element<T>(cpu.state().regs, rs1, i)
                         : vector::get_group_element<T>(cpu.state().regs, rs2, i);
        vector::set_group_element<T>(cpu.state().regs, rd, i, val);
    }
}

template <typename T>
void execute_vmerge_scalar(core::CPU& cpu, RegId rd, T val1, RegId rs2, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        bool mask_bit = vector::is_element_active(mask_reg, i, false);
        T val = mask_bit ? val1 : vector::get_group_element<T>(cpu.state().regs, rs2, i);
        vector::set_group_element<T>(cpu.state().regs, rd, i, val);
    }
}

template <typename T>
void execute_vfmv_v_f(core::CPU& cpu, RegId rd, uint64_t bits, uint32_t vl) {
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        vector::set_group_element<T>(cpu.state().regs, rd, i, static_cast<T>(bits));
    }
}

template <typename T>
void execute_vmerge_vi(core::CPU& cpu, RegId rd, int32_t imm, RegId rs2, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(imm);

    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        bool mask_bit = vector::is_element_active(mask_reg, i, false);
        T val = mask_bit ? val1 : vector::get_group_element<T>(cpu.state().regs, rs2, i);
        vector::set_group_element<T>(cpu.state().regs, rd, i, val);
    }
}

template <typename T>
void execute_vid(core::CPU& cpu, RegId rd, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        vector::set_group_element<T>(cpu.state().regs, rd, i, static_cast<T>(i));
    }
}

bool execute_vector_permute_scalar(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1,
                                   RegId rs2, uint32_t sew) {
    switch (op_id) {
        case isa::OperationId::VMV_X_S: {
            Register val = 0;
            if (sew == 8)
                val = static_cast<Register>(static_cast<int32_t>(static_cast<int8_t>(
                    vector::get_group_element<uint8_t>(cpu.state().regs, rs2, 0))));
            else if (sew == 16)
                val = static_cast<Register>(static_cast<int32_t>(static_cast<int16_t>(
                    vector::get_group_element<uint16_t>(cpu.state().regs, rs2, 0))));
            else if (sew == 32)
                val = static_cast<Register>(static_cast<int32_t>(
                    vector::get_group_element<uint32_t>(cpu.state().regs, rs2, 0)));
            else
                val = vector::get_group_element<uint64_t>(cpu.state().regs, rs2, 0);
            cpu.state().regs.write(rd, val);
            return true;
        }
        case isa::OperationId::VMV_S_X: {
            if (cpu.state().vl == 0 || cpu.state().vstart != 0) return true;
            Register val = cpu.state().regs.read(rs1);
            if (sew == 8)
                vector::set_group_element<uint8_t>(cpu.state().regs, rd, 0,
                                                   static_cast<uint8_t>(val));
            else if (sew == 16)
                vector::set_group_element<uint16_t>(cpu.state().regs, rd, 0,
                                                    static_cast<uint16_t>(val));
            else if (sew == 32)
                vector::set_group_element<uint32_t>(cpu.state().regs, rd, 0,
                                                    static_cast<uint32_t>(val));
            else
                vector::set_group_element<uint64_t>(
                    cpu.state().regs, rd, 0,
                    static_cast<uint64_t>(static_cast<int64_t>(static_cast<SignedWord>(val))));
            return true;
        }
        case isa::OperationId::VFMV_F_S: {
            uint64_t val = 0;
            if (sew == 16) {
                val = vector::get_group_element<uint16_t>(cpu.state().regs, rs2, 0);
                val |= 0xFFFFFFFFFFFF0000ULL;
            } else if (sew == 32) {
                val = vector::get_group_element<uint32_t>(cpu.state().regs, rs2, 0);
                val |= 0xFFFFFFFF00000000ULL;
            } else if (sew == 64) {
                val = vector::get_group_element<uint64_t>(cpu.state().regs, rs2, 0);
            }
            cpu.state().regs.write_fp(rd, val);
            return true;
        }
        case isa::OperationId::VFMV_S_F: {
            if (cpu.state().vl == 0 || cpu.state().vstart != 0) return true;
            uint64_t val = scalar_fp_bits(cpu, cpu.state().regs.read_fp(rs1), sew);
            if (sew == 16)
                vector::set_group_element<uint16_t>(cpu.state().regs, rd, 0,
                                                    static_cast<uint16_t>(val));
            else if (sew == 32)
                vector::set_group_element<uint32_t>(cpu.state().regs, rd, 0,
                                                    static_cast<uint32_t>(val));
            else if (sew == 64)
                vector::set_group_element<uint64_t>(cpu.state().regs, rd, 0, val);
            return true;
        }
        default:
            return false;
    }
}

bool execute_vector_permute_merge(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1,
                                  RegId rs2, uint32_t vl, uint32_t sew, Register rs1_val,
                                  int32_t simm5) {
    switch (op_id) {
        case isa::OperationId::VFMERGE_VFM:
        case isa::OperationId::VFMV_V_F: {
            uint64_t val = scalar_fp_bits(cpu, cpu.state().regs.read_fp(rs1), sew);
            if (op_id == isa::OperationId::VFMV_V_F) {
                if (sew == 16)
                    execute_vfmv_v_f<uint16_t>(cpu, rd, val, vl);
                else if (sew == 32)
                    execute_vfmv_v_f<uint32_t>(cpu, rd, val, vl);
                else if (sew == 64)
                    execute_vfmv_v_f<uint64_t>(cpu, rd, val, vl);
                return true;
            }
            if (sew == 16)
                execute_vmerge_scalar<uint16_t>(cpu, rd, static_cast<uint16_t>(val), rs2, vl);
            else if (sew == 32)
                execute_vmerge_scalar<uint32_t>(cpu, rd, static_cast<uint32_t>(val), rs2, vl);
            else if (sew == 64)
                execute_vmerge_scalar<uint64_t>(cpu, rd, val, rs2, vl);
            return true;
        }
        case isa::OperationId::VMERGE_VVM:
            if (sew == 8)
                execute_vmerge_vv<uint8_t>(cpu, rd, rs1, rs2, vl);
            else if (sew == 16)
                execute_vmerge_vv<uint16_t>(cpu, rd, rs1, rs2, vl);
            else if (sew == 32)
                execute_vmerge_vv<uint32_t>(cpu, rd, rs1, rs2, vl);
            else
                execute_vmerge_vv<uint64_t>(cpu, rd, rs1, rs2, vl);
            return true;
        case isa::OperationId::VMERGE_VXM:
            if (sew == 8)
                execute_vmerge_scalar<uint8_t>(cpu, rd, vector::integer_scalar<uint8_t>(rs1_val),
                                               rs2, vl);
            else if (sew == 16)
                execute_vmerge_scalar<uint16_t>(cpu, rd, vector::integer_scalar<uint16_t>(rs1_val),
                                                rs2, vl);
            else if (sew == 32)
                execute_vmerge_scalar<uint32_t>(cpu, rd, vector::integer_scalar<uint32_t>(rs1_val),
                                                rs2, vl);
            else
                execute_vmerge_scalar<uint64_t>(cpu, rd, vector::integer_scalar<uint64_t>(rs1_val),
                                                rs2, vl);
            return true;
        case isa::OperationId::VMERGE_VIM:
            if (sew == 8)
                execute_vmerge_vi<uint8_t>(cpu, rd, simm5, rs2, vl);
            else if (sew == 16)
                execute_vmerge_vi<uint16_t>(cpu, rd, simm5, rs2, vl);
            else if (sew == 32)
                execute_vmerge_vi<uint32_t>(cpu, rd, simm5, rs2, vl);
            else
                execute_vmerge_vi<uint64_t>(cpu, rd, simm5, rs2, vl);
            return true;
        default:
            return false;
    }
}

bool execute_vector_permute_vid(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1,
                                RegId rs2, bool vm, uint32_t vl, uint32_t sew) {
    if (op_id == isa::OperationId::VID_V) {
        if (sew == 8)
            execute_vid<uint8_t>(cpu, rd, vm, vl);
        else if (sew == 16)
            execute_vid<uint16_t>(cpu, rd, vm, vl);
        else if (sew == 32)
            execute_vid<uint32_t>(cpu, rd, vm, vl);
        else
            execute_vid<uint64_t>(cpu, rd, vm, vl);
        return true;
    }
    if (op_id == isa::OperationId::VCOMPRESS_VM) {
        if (sew == 8)
            execute_vcompress<uint8_t>(cpu, rd, rs2, rs1, vl);
        else if (sew == 16)
            execute_vcompress<uint16_t>(cpu, rd, rs2, rs1, vl);
        else if (sew == 32)
            execute_vcompress<uint32_t>(cpu, rd, rs2, rs1, vl);
        else
            execute_vcompress<uint64_t>(cpu, rd, rs2, rs1, vl);
        return true;
    }
    return false;
}

bool execute_vector_permute_mv(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1,
                               RegId rs2, uint32_t vl, uint32_t sew, Register rs1_val,
                               int32_t simm5) {
    switch (op_id) {
        case isa::OperationId::VMV_V_V: {
            auto add_f = []<typename T>(T, T b) -> T { return b; };
            if (sew == 8)
                vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, true, vl, add_f);
            else if (sew == 16)
                vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, true, vl, add_f);
            else if (sew == 32)
                vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, true, vl, add_f);
            else
                vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, true, vl, add_f);
            return true;
        }
        case isa::OperationId::VMV_V_X: {
            auto add_f = []<typename T>(T, T b) -> T { return b; };
            if (sew == 8)
                vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, true, vl, add_f);
            else if (sew == 16)
                vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, true, vl, add_f);
            else if (sew == 32)
                vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, true, vl, add_f);
            else
                vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, true, vl, add_f);
            return true;
        }
        case isa::OperationId::VMV_V_I: {
            auto add_f = []<typename T>(T, T b) -> T { return b; };
            if (sew == 8)
                vector::perform_vi<uint8_t>(cpu, rd, simm5, rs2, true, vl, add_f);
            else if (sew == 16)
                vector::perform_vi<uint16_t>(cpu, rd, simm5, rs2, true, vl, add_f);
            else if (sew == 32)
                vector::perform_vi<uint32_t>(cpu, rd, simm5, rs2, true, vl, add_f);
            else
                vector::perform_vi<uint64_t>(cpu, rd, simm5, rs2, true, vl, add_f);
            return true;
        }
        case isa::OperationId::VMV1R_V:
            execute_vmv_whole(cpu, rd, rs2, 1, sew);
            return true;
        case isa::OperationId::VMV2R_V:
            execute_vmv_whole(cpu, rd, rs2, 2, sew);
            return true;
        case isa::OperationId::VMV4R_V:
            execute_vmv_whole(cpu, rd, rs2, 4, sew);
            return true;
        case isa::OperationId::VMV8R_V:
            execute_vmv_whole(cpu, rd, rs2, 8, sew);
            return true;
        default:
            return false;
    }
}

bool execute_vector_permute_slide1(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1,
                                   RegId rs2, bool vm, uint32_t vl, uint32_t sew,
                                   Register rs1_val) {
    const bool fp =
        op_id == isa::OperationId::VFSLIDE1UP_VF || op_id == isa::OperationId::VFSLIDE1DOWN_VF;
    FloatingRegister scalar_val = vector::integer_scalar<uint64_t>(rs1_val);
    if (fp) {
        scalar_val = scalar_fp_bits(cpu, cpu.state().regs.read_fp(rs1), sew);
    }
    switch (op_id) {
        case isa::OperationId::VSLIDE1UP_VX:
        case isa::OperationId::VFSLIDE1UP_VF: {
            if (sew == 8)
                execute_vslide1up<uint8_t>(cpu, rd, scalar_val, rs2, vm, vl);
            else if (sew == 16)
                execute_vslide1up<uint16_t>(cpu, rd, scalar_val, rs2, vm, vl);
            else if (sew == 32)
                execute_vslide1up<uint32_t>(cpu, rd, scalar_val, rs2, vm, vl);
            else
                execute_vslide1up<uint64_t>(cpu, rd, scalar_val, rs2, vm, vl);
            return true;
        }
        case isa::OperationId::VSLIDE1DOWN_VX:
        case isa::OperationId::VFSLIDE1DOWN_VF:
            if (sew == 8)
                execute_vslide1down<uint8_t>(cpu, rd, scalar_val, rs2, vm, vl);
            else if (sew == 16)
                execute_vslide1down<uint16_t>(cpu, rd, scalar_val, rs2, vm, vl);
            else if (sew == 32)
                execute_vslide1down<uint32_t>(cpu, rd, scalar_val, rs2, vm, vl);
            else
                execute_vslide1down<uint64_t>(cpu, rd, scalar_val, rs2, vm, vl);
            return true;
        default:
            return false;
    }
}

bool execute_vector_permute_slidedown_up(core::CPU& cpu, isa::OperationId op_id, RegId rd,
                                         RegId rs2, bool vm, uint32_t vl, uint32_t sew,
                                         Register rs1_val, int32_t simm5) {
    switch (op_id) {
        case isa::OperationId::VSLIDEDOWN_VX: {
            auto offset = static_cast<uint64_t>(rs1_val);
            if (sew == 8)
                execute_vslidedown<uint8_t>(cpu, rd, offset, rs2, vm, vl);
            else if (sew == 16)
                execute_vslidedown<uint16_t>(cpu, rd, offset, rs2, vm, vl);
            else if (sew == 32)
                execute_vslidedown<uint32_t>(cpu, rd, offset, rs2, vm, vl);
            else
                execute_vslidedown<uint64_t>(cpu, rd, offset, rs2, vm, vl);
            return true;
        }
        case isa::OperationId::VSLIDEDOWN_VI: {
            auto offset = static_cast<uint32_t>(simm5 & 0x1F);
            if (sew == 8)
                execute_vslidedown<uint8_t>(cpu, rd, offset, rs2, vm, vl);
            else if (sew == 16)
                execute_vslidedown<uint16_t>(cpu, rd, offset, rs2, vm, vl);
            else if (sew == 32)
                execute_vslidedown<uint32_t>(cpu, rd, offset, rs2, vm, vl);
            else
                execute_vslidedown<uint64_t>(cpu, rd, offset, rs2, vm, vl);
            return true;
        }
        case isa::OperationId::VSLIDEUP_VX: {
            auto offset = static_cast<uint64_t>(rs1_val);
            if (sew == 8)
                execute_vslideup<uint8_t>(cpu, rd, offset, rs2, vm, vl);
            else if (sew == 16)
                execute_vslideup<uint16_t>(cpu, rd, offset, rs2, vm, vl);
            else if (sew == 32)
                execute_vslideup<uint32_t>(cpu, rd, offset, rs2, vm, vl);
            else
                execute_vslideup<uint64_t>(cpu, rd, offset, rs2, vm, vl);
            return true;
        }
        case isa::OperationId::VSLIDEUP_VI: {
            auto offset = static_cast<uint32_t>(simm5 & 0x1F);
            if (sew == 8)
                execute_vslideup<uint8_t>(cpu, rd, offset, rs2, vm, vl);
            else if (sew == 16)
                execute_vslideup<uint16_t>(cpu, rd, offset, rs2, vm, vl);
            else if (sew == 32)
                execute_vslideup<uint32_t>(cpu, rd, offset, rs2, vm, vl);
            else
                execute_vslideup<uint64_t>(cpu, rd, offset, rs2, vm, vl);
            return true;
        }
        default:
            return false;
    }
}

template <typename T>
bool execute_vector_permute_gather_typed(core::CPU& cpu, isa::OperationId op_id, RegId rd,
                                         RegId rs1, RegId rs2, bool vm, uint32_t vl, uint32_t vlmax,
                                         Register rs1_val, int32_t simm5) {
    switch (op_id) {
        case isa::OperationId::VRGATHER_VV:
            execute_vrgather_vector<T, T>(cpu, rd, rs1, rs2, vm, vl, vlmax);
            return true;
        case isa::OperationId::VRGATHEREI16_VV:
            execute_vrgather_vector<T, uint16_t>(cpu, rd, rs1, rs2, vm, vl, vlmax);
            return true;
        case isa::OperationId::VRGATHER_VX:
            execute_vrgather_scalar<T>(cpu, rd, static_cast<uint64_t>(rs1_val), rs2, vm, vl, vlmax);
            return true;
        case isa::OperationId::VRGATHER_VI:
            execute_vrgather_scalar<T>(cpu, rd, static_cast<uint32_t>(simm5) & 0x1FU, rs2, vm, vl,
                                       vlmax);
            return true;
        default:
            return false;
    }
}

bool execute_vector_permute_gather(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1,
                                   RegId rs2, bool vm, uint32_t vl, uint32_t sew, Register rs1_val,
                                   int32_t simm5) {
    const uint32_t vlmax = vector_vlmax(cpu, sew);
    if (sew == 8)
        return execute_vector_permute_gather_typed<uint8_t>(cpu, op_id, rd, rs1, rs2, vm, vl, vlmax,
                                                            rs1_val, simm5);
    if (sew == 16)
        return execute_vector_permute_gather_typed<uint16_t>(cpu, op_id, rd, rs1, rs2, vm, vl,
                                                             vlmax, rs1_val, simm5);
    if (sew == 32)
        return execute_vector_permute_gather_typed<uint32_t>(cpu, op_id, rd, rs1, rs2, vm, vl,
                                                             vlmax, rs1_val, simm5);
    return execute_vector_permute_gather_typed<uint64_t>(cpu, op_id, rd, rs1, rs2, vm, vl, vlmax,
                                                         rs1_val, simm5);
}

}  // namespace

void ExecuteUnit::execute_vector_permute(core::CPU& cpu, isa::OperationId op_id, RegId rd,
                                         RegId rs1, RegId rs2, bool vm, uint32_t vl, uint32_t sew,
                                         Register rs1_val, int32_t simm5) {
    if (execute_vector_permute_scalar(cpu, op_id, rd, rs1, rs2, sew)) return;
    if (execute_vector_permute_merge(cpu, op_id, rd, rs1, rs2, vl, sew, rs1_val, simm5)) return;
    if (execute_vector_permute_vid(cpu, op_id, rd, rs1, rs2, vm, vl, sew)) return;
    if (execute_vector_permute_mv(cpu, op_id, rd, rs1, rs2, vl, sew, rs1_val, simm5)) return;
    if (execute_vector_permute_slide1(cpu, op_id, rd, rs1, rs2, vm, vl, sew, rs1_val)) return;
    if (execute_vector_permute_slidedown_up(cpu, op_id, rd, rs2, vm, vl, sew, rs1_val, simm5))
        return;
    if (execute_vector_permute_gather(cpu, op_id, rd, rs1, rs2, vm, vl, sew, rs1_val, simm5))
        return;
}

}  // namespace simrv::execute
