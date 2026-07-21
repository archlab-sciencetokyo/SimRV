#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/core/Cpu.hpp"

namespace simrv::execute {

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

    switch (op_id) {
        // Configuration
        case isa::OperationId::VSETVLI:
        case isa::OperationId::VSETIVLI:
        case isa::OperationId::VSETVL:
            execute_vector_config(cpu, op_id, ir, rd, rs1, rs2);
            break;

        // Memory
        case isa::OperationId::VLE8_V:
        case isa::OperationId::VLE16_V:
        case isa::OperationId::VLE32_V:
        case isa::OperationId::VLE64_V:
        case isa::OperationId::VSE8_V:
        case isa::OperationId::VSE16_V:
        case isa::OperationId::VSE32_V:
        case isa::OperationId::VSE64_V:
        case isa::OperationId::VLSE8_V:
        case isa::OperationId::VLSE16_V:
        case isa::OperationId::VLSE32_V:
        case isa::OperationId::VLSE64_V:
        case isa::OperationId::VSSE8_V:
        case isa::OperationId::VSSE16_V:
        case isa::OperationId::VSSE32_V:
        case isa::OperationId::VSSE64_V:
        case isa::OperationId::VLUXEI8_V:
        case isa::OperationId::VLOXEI8_V:
        case isa::OperationId::VLUXEI16_V:
        case isa::OperationId::VLOXEI16_V:
        case isa::OperationId::VLUXEI32_V:
        case isa::OperationId::VLOXEI32_V:
        case isa::OperationId::VLUXEI64_V:
        case isa::OperationId::VLOXEI64_V:
        case isa::OperationId::VSUXEI8_V:
        case isa::OperationId::VSOXEI8_V:
        case isa::OperationId::VSUXEI16_V:
        case isa::OperationId::VSOXEI16_V:
        case isa::OperationId::VSUXEI32_V:
        case isa::OperationId::VSOXEI32_V:
        case isa::OperationId::VL1RE8_V:
        case isa::OperationId::VL1RE16_V:
        case isa::OperationId::VL1RE32_V:
        case isa::OperationId::VL1RE64_V:
        case isa::OperationId::VL2RE8_V:
        case isa::OperationId::VL2RE16_V:
        case isa::OperationId::VL2RE32_V:
        case isa::OperationId::VL2RE64_V:
        case isa::OperationId::VL4RE8_V:
        case isa::OperationId::VL4RE16_V:
        case isa::OperationId::VL4RE32_V:
        case isa::OperationId::VL4RE64_V:
        case isa::OperationId::VL8RE8_V:
        case isa::OperationId::VL8RE16_V:
        case isa::OperationId::VL8RE32_V:
        case isa::OperationId::VL8RE64_V:
        case isa::OperationId::VS1R_V:
        case isa::OperationId::VS2R_V:
        case isa::OperationId::VS4R_V:
        case isa::OperationId::VS8R_V:
            execute_vector_memory(cpu, machine, op_id, rd, rs1, rs2, vm, vl, sew);
            break;

        // Floating-Point
        case isa::OperationId::VFADD_VV:
        case isa::OperationId::VFADD_VF:
        case isa::OperationId::VFMACC_VV:
        case isa::OperationId::VFMACC_VF:
            execute_vector_float(cpu, op_id, rd, rs1, rs2, vm, vl, sew);
            break;

        // Fixed-Point
        case isa::OperationId::VSADD_VV:
        case isa::OperationId::VSADD_VX:
        case isa::OperationId::VSADD_VI:
        case isa::OperationId::VSADDU_VV:
        case isa::OperationId::VSADDU_VX:
        case isa::OperationId::VSADDU_VI:
        case isa::OperationId::VSMUL_VV:
        case isa::OperationId::VSMUL_VX:
        case isa::OperationId::VSSRA_VV:
        case isa::OperationId::VSSRA_VX:
        case isa::OperationId::VSSRA_VI:
        case isa::OperationId::VSSRL_VV:
        case isa::OperationId::VSSRL_VX:
        case isa::OperationId::VSSRL_VI:
        case isa::OperationId::VSSUB_VV:
        case isa::OperationId::VSSUB_VX:
        case isa::OperationId::VSSUBU_VV:
        case isa::OperationId::VSSUBU_VX:
        case isa::OperationId::VNCLIP_WV:
        case isa::OperationId::VNCLIP_WX:
        case isa::OperationId::VNCLIP_WI:
        case isa::OperationId::VNCLIPU_WV:
        case isa::OperationId::VNCLIPU_WX:
        case isa::OperationId::VNCLIPU_WI:
            execute_vector_fixed_point(cpu, op_id, rd, rs1, rs2, vm, vl, sew, cpu.state().regs.read(rs1), simm5);
            break;

        // Permute and Move
        case isa::OperationId::VMV_X_S:
        case isa::OperationId::VMV_S_X:
        case isa::OperationId::VMERGE_VVM:
        case isa::OperationId::VMERGE_VXM:
        case isa::OperationId::VMERGE_VIM:
        case isa::OperationId::VID_V:
        case isa::OperationId::VMV_V_V:
        case isa::OperationId::VMV_V_X:
        case isa::OperationId::VMV_V_I:
        case isa::OperationId::VSLIDE1UP_VX:
        case isa::OperationId::VSLIDE1DOWN_VX:
        case isa::OperationId::VSLIDEDOWN_VX:
        case isa::OperationId::VSLIDEDOWN_VI:
        case isa::OperationId::VSLIDEUP_VX:
        case isa::OperationId::VSLIDEUP_VI:
        case isa::OperationId::VMV1R_V:
        case isa::OperationId::VMV2R_V:
        case isa::OperationId::VMV4R_V:
        case isa::OperationId::VMV8R_V:
        case isa::OperationId::VCOMPRESS_VM:
        case isa::OperationId::VFMV_F_S:
        case isa::OperationId::VFMV_S_F:
        case isa::OperationId::VFMERGE_VFM:
            execute_vector_permute(cpu, op_id, rd, rs1, rs2, vm, vl, sew, cpu.state().regs.read(rs1), simm5);
            break;

        // Integer Arithmetic (default/fallback for all remaining vector integer ops)
        default:
            execute_vector_integer(cpu, op_id, rd, rs1, rs2, vm, vl, sew, cpu.state().regs.read(rs1), simm5);
            break;
    }
}

} // namespace simrv::execute
