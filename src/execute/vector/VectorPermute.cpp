#include "simrv/execute/ExecuteUnit.hpp"
#include "VectorHelpers.hpp"

namespace simrv::execute {

namespace {

// Whole register move helper
void execute_vmv_whole(core::CPU& cpu, RegId rd, RegId rs2, uint32_t nr) {
    uint32_t total_bytes = nr * cpu.state().regs.vlen_bytes();
    for (uint32_t i = 0; i < total_bytes; i++) {
        uint32_t src_reg = (static_cast<uint32_t>(rs2) + (i / cpu.state().regs.vlen_bytes())) % 32;
        uint32_t dst_reg = (static_cast<uint32_t>(rd) + (i / cpu.state().regs.vlen_bytes())) % 32;
        uint8_t val = cpu.state().regs.read_vector(static_cast<RegId>(src_reg)).u8[i % cpu.state().regs.vlen_bytes()];
        cpu.state().regs.read_vector(static_cast<RegId>(dst_reg)).u8[i % cpu.state().regs.vlen_bytes()] = val;
    }
}

// Vector compress helper
template <typename T>
void execute_vcompress(core::CPU& cpu, RegId rd, RegId rs2, RegId rs1, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(rs1);
    std::vector<T> src_vals;
    src_vals.reserve(vl);
    for (uint32_t i = 0; i < vl; i++) {
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
void execute_vslideup(core::CPU& cpu, RegId rd, uint32_t offset, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    
    std::vector<T> src_vals(vl);
    for (uint32_t i = 0; i < vl; i++) {
        src_vals[i] = vector::get_group_element<T>(cpu.state().regs, rs2, i);
    }
    
    for (uint32_t i = offset; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val = src_vals[i - offset];
        vector::set_group_element<T>(cpu.state().regs, rd, i, val);
    }
}

// Vector Slide Down
template <typename T>
void execute_vslidedown(core::CPU& cpu, RegId rd, uint32_t offset, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    
    std::vector<T> src_vals(vl);
    for (uint32_t i = 0; i < vl; i++) {
        src_vals[i] = vector::get_group_element<T>(cpu.state().regs, rs2, i);
    }
    
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val = (i + offset < vl) ? src_vals[i + offset] : 0;
        vector::set_group_element<T>(cpu.state().regs, rd, i, val);
    }
}

// Vector Slide 1 Down
template <typename T>
void execute_vslide1down(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl) {
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
    
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        vector::set_group_element<T>(cpu.state().regs, rd, i, temp[i]);
    }
}

// Vector Slide 1 Up
template <typename T>
void execute_vslide1up(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl) {
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
    
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        vector::set_group_element<T>(cpu.state().regs, rd, i, temp[i]);
    }
}

template <typename T>
void execute_vmerge_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = 0; i < vl; i++) {
        bool mask_bit = vector::is_element_active(mask_reg, i, false);
        T val = mask_bit ? vector::get_group_element<T>(cpu.state().regs, rs1, i) : vector::get_group_element<T>(cpu.state().regs, rs2, i);
        vector::set_group_element<T>(cpu.state().regs, rd, i, val);
    }
}

template <typename T>
void execute_vmerge_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(rs1_val);

    for (uint32_t i = 0; i < vl; i++) {
        bool mask_bit = vector::is_element_active(mask_reg, i, false);
        T val = mask_bit ? val1 : vector::get_group_element<T>(cpu.state().regs, rs2, i);
        vector::set_group_element<T>(cpu.state().regs, rd, i, val);
    }
}

template <typename T>
void execute_vmerge_vi(core::CPU& cpu, RegId rd, int32_t imm, RegId rs2, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(imm);

    for (uint32_t i = 0; i < vl; i++) {
        bool mask_bit = vector::is_element_active(mask_reg, i, false);
        T val = mask_bit ? val1 : vector::get_group_element<T>(cpu.state().regs, rs2, i);
        vector::set_group_element<T>(cpu.state().regs, rd, i, val);
    }
}

template <typename T>
void execute_vid(core::CPU& cpu, RegId rd, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        vector::set_group_element<T>(cpu.state().regs, rd, i, static_cast<T>(i));
    }
}

} // namespace

void ExecuteUnit::execute_vector_permute(core::CPU& cpu, isa::OperationId op_id,
                                        RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, uint32_t sew,
                                        Register rs1_val, int32_t simm5) {
    switch (op_id) {
        case isa::OperationId::VMV_X_S: {
            Register val = 0;
            if (sew == 8) val = static_cast<Register>(static_cast<int32_t>(static_cast<int8_t>(vector::get_group_element<uint8_t>(cpu.state().regs, rs2, 0))));
            else if (sew == 16) val = static_cast<Register>(static_cast<int32_t>(static_cast<int16_t>(vector::get_group_element<uint16_t>(cpu.state().regs, rs2, 0))));
            else if (sew == 32) val = static_cast<Register>(static_cast<int32_t>(vector::get_group_element<uint32_t>(cpu.state().regs, rs2, 0)));
            else val = vector::get_group_element<uint64_t>(cpu.state().regs, rs2, 0);
            cpu.state().regs.write(rd, val);
            break;
        }
        case isa::OperationId::VMV_S_X: {
            Register val = cpu.state().regs.read(rs1);
            if (sew == 8) vector::set_group_element<uint8_t>(cpu.state().regs, rd, 0, static_cast<uint8_t>(val));
            else if (sew == 16) vector::set_group_element<uint16_t>(cpu.state().regs, rd, 0, static_cast<uint16_t>(val));
            else if (sew == 32) vector::set_group_element<uint32_t>(cpu.state().regs, rd, 0, static_cast<uint32_t>(val));
            else vector::set_group_element<uint64_t>(cpu.state().regs, rd, 0, val);
            break;
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
            break;
        }
        case isa::OperationId::VFMV_S_F: {
            uint64_t val = cpu.state().regs.read_fp(rs1);
            if (sew == 16) vector::set_group_element<uint16_t>(cpu.state().regs, rd, 0, static_cast<uint16_t>(val));
            else if (sew == 32) vector::set_group_element<uint32_t>(cpu.state().regs, rd, 0, static_cast<uint32_t>(val));
            else if (sew == 64) vector::set_group_element<uint64_t>(cpu.state().regs, rd, 0, val);
            break;
        }
        case isa::OperationId::VFMERGE_VFM: {
            uint64_t val = cpu.state().regs.read_fp(rs1);
            if (sew == 16) execute_vmerge_vx<uint16_t>(cpu, rd, val, rs2, vl);
            else if (sew == 32) execute_vmerge_vx<uint32_t>(cpu, rd, val, rs2, vl);
            else if (sew == 64) execute_vmerge_vx<uint64_t>(cpu, rd, val, rs2, vl);
            break;
        }

        case isa::OperationId::VMERGE_VVM:
            if (sew == 8) execute_vmerge_vv<uint8_t>(cpu, rd, rs1, rs2, vl);
            else if (sew == 16) execute_vmerge_vv<uint16_t>(cpu, rd, rs1, rs2, vl);
            else if (sew == 32) execute_vmerge_vv<uint32_t>(cpu, rd, rs1, rs2, vl);
            else execute_vmerge_vv<uint64_t>(cpu, rd, rs1, rs2, vl);
            break;
        case isa::OperationId::VMERGE_VXM:
            if (sew == 8) execute_vmerge_vx<uint8_t>(cpu, rd, rs1_val, rs2, vl);
            else if (sew == 16) execute_vmerge_vx<uint16_t>(cpu, rd, rs1_val, rs2, vl);
            else if (sew == 32) execute_vmerge_vx<uint32_t>(cpu, rd, rs1_val, rs2, vl);
            else execute_vmerge_vx<uint64_t>(cpu, rd, rs1_val, rs2, vl);
            break;
        case isa::OperationId::VMERGE_VIM:
            if (sew == 8) execute_vmerge_vi<uint8_t>(cpu, rd, simm5, rs2, vl);
            else if (sew == 16) execute_vmerge_vi<uint16_t>(cpu, rd, simm5, rs2, vl);
            else if (sew == 32) execute_vmerge_vi<uint32_t>(cpu, rd, simm5, rs2, vl);
            else execute_vmerge_vi<uint64_t>(cpu, rd, simm5, rs2, vl);
            break;

        case isa::OperationId::VID_V:
            if (sew == 8) execute_vid<uint8_t>(cpu, rd, vm, vl);
            else if (sew == 16) execute_vid<uint16_t>(cpu, rd, vm, vl);
            else if (sew == 32) execute_vid<uint32_t>(cpu, rd, vm, vl);
            else execute_vid<uint64_t>(cpu, rd, vm, vl);
            break;

        case isa::OperationId::VMV_V_V: {
            auto add_f = []<typename T>(T /*a*/, T b) -> T { return b; };
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, true, vl, add_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, true, vl, add_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, true, vl, add_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, true, vl, add_f);
            break;
        }
        case isa::OperationId::VMV_V_X: {
            auto add_f = []<typename T>(T /*a*/, T b) -> T { return b; };
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, true, vl, add_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, true, vl, add_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, true, vl, add_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, true, vl, add_f);
            break;
        }
        case isa::OperationId::VMV_V_I: {
            auto add_f = []<typename T>(T /*a*/, T b) -> T { return b; };
            if (sew == 8) vector::perform_vi<uint8_t>(cpu, rd, simm5, rs2, true, vl, add_f);
            else if (sew == 16) vector::perform_vi<uint16_t>(cpu, rd, simm5, rs2, true, vl, add_f);
            else if (sew == 32) vector::perform_vi<uint32_t>(cpu, rd, simm5, rs2, true, vl, add_f);
            else vector::perform_vi<uint64_t>(cpu, rd, simm5, rs2, true, vl, add_f);
            break;
        }

        case isa::OperationId::VSLIDE1UP_VX:
            if (sew == 8) execute_vslide1up<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) execute_vslide1up<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 32) execute_vslide1up<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else execute_vslide1up<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl);
            break;
        case isa::OperationId::VSLIDE1DOWN_VX:
            if (sew == 8) execute_vslide1down<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) execute_vslide1down<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 32) execute_vslide1down<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else execute_vslide1down<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl);
            break;

        case isa::OperationId::VSLIDEDOWN_VX: {
            auto offset = static_cast<uint32_t>(rs1_val);
            if (sew == 8) execute_vslidedown<uint8_t>(cpu, rd, offset, rs2, vm, vl);
            else if (sew == 16) execute_vslidedown<uint16_t>(cpu, rd, offset, rs2, vm, vl);
            else if (sew == 32) execute_vslidedown<uint32_t>(cpu, rd, offset, rs2, vm, vl);
            else execute_vslidedown<uint64_t>(cpu, rd, offset, rs2, vm, vl);
            break;
        }
        case isa::OperationId::VSLIDEDOWN_VI: {
            auto offset = static_cast<uint32_t>(simm5 & 0x1F);
            if (sew == 8) execute_vslidedown<uint8_t>(cpu, rd, offset, rs2, vm, vl);
            else if (sew == 16) execute_vslidedown<uint16_t>(cpu, rd, offset, rs2, vm, vl);
            else if (sew == 32) execute_vslidedown<uint32_t>(cpu, rd, offset, rs2, vm, vl);
            else execute_vslidedown<uint64_t>(cpu, rd, offset, rs2, vm, vl);
            break;
        }
        case isa::OperationId::VSLIDEUP_VX: {
            auto offset = static_cast<uint32_t>(rs1_val);
            if (sew == 8) execute_vslideup<uint8_t>(cpu, rd, offset, rs2, vm, vl);
            else if (sew == 16) execute_vslideup<uint16_t>(cpu, rd, offset, rs2, vm, vl);
            else if (sew == 32) execute_vslideup<uint32_t>(cpu, rd, offset, rs2, vm, vl);
            else execute_vslideup<uint64_t>(cpu, rd, offset, rs2, vm, vl);
            break;
        }
        case isa::OperationId::VSLIDEUP_VI: {
            auto offset = static_cast<uint32_t>(simm5 & 0x1F);
            if (sew == 8) execute_vslideup<uint8_t>(cpu, rd, offset, rs2, vm, vl);
            else if (sew == 16) execute_vslideup<uint16_t>(cpu, rd, offset, rs2, vm, vl);
            else if (sew == 32) execute_vslideup<uint32_t>(cpu, rd, offset, rs2, vm, vl);
            else execute_vslideup<uint64_t>(cpu, rd, offset, rs2, vm, vl);
            break;
        }
        case isa::OperationId::VMV1R_V:
            execute_vmv_whole(cpu, rd, rs2, 1);
            break;
        case isa::OperationId::VMV2R_V:
            execute_vmv_whole(cpu, rd, rs2, 2);
            break;
        case isa::OperationId::VMV4R_V:
            execute_vmv_whole(cpu, rd, rs2, 4);
            break;
        case isa::OperationId::VMV8R_V:
            execute_vmv_whole(cpu, rd, rs2, 8);
            break;
        case isa::OperationId::VCOMPRESS_VM:
            if (sew == 8) execute_vcompress<uint8_t>(cpu, rd, rs2, rs1, vl);
            else if (sew == 16) execute_vcompress<uint16_t>(cpu, rd, rs2, rs1, vl);
            else if (sew == 32) execute_vcompress<uint32_t>(cpu, rd, rs2, rs1, vl);
            else execute_vcompress<uint64_t>(cpu, rd, rs2, rs1, vl);
            break;
        default:
            break;
    }
}

} // namespace simrv::execute
