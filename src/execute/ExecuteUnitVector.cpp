#include "simrv/core/Cpu.hpp"
#include "simrv/execute/ExecuteUnit.hpp"

namespace simrv::execute {

namespace {
/** RVV 1.0 operations whose traps are always reported with vstart=0. */
constexpr auto requires_zero_vstart(isa::OperationId op_id) -> bool {
    switch (op_id) {
        case isa::OperationId::VREDSUM_VS:
        case isa::OperationId::VWREDSUM_VS:
        case isa::OperationId::VWREDSUMU_VS:
        case isa::OperationId::VCPOP_M:
        case isa::OperationId::VFIRST_M:
        case isa::OperationId::VMSBF_M:
        case isa::OperationId::VMSIF_M:
        case isa::OperationId::VMSOF_M:
        case isa::OperationId::VIOTA_M:
        case isa::OperationId::VCOMPRESS_VM:
            return true;
        default:
            return false;
    }
}

/** Vector operations that consume or produce scalar/vector floating-point state. */
constexpr auto is_vector_fp(isa::OperationId op_id) -> bool {
    switch (op_id) {
        case isa::OperationId::VFADD_VV:
        case isa::OperationId::VFADD_VF:
        case isa::OperationId::VFMACC_VV:
        case isa::OperationId::VFMACC_VF:
        case isa::OperationId::VFMV_F_S:
        case isa::OperationId::VFMV_S_F:
        case isa::OperationId::VFMERGE_VFM:
            return true;
        default:
            return false;
    }
}

/** Implemented vector FP arithmetic operations that consult frm and accrue fflags. */
constexpr auto is_vector_fp_arithmetic(isa::OperationId op_id) -> bool {
    return op_id == isa::OperationId::VFADD_VV || op_id == isa::OperationId::VFADD_VF ||
           op_id == isa::OperationId::VFMACC_VV || op_id == isa::OperationId::VFMACC_VF;
}
}  // namespace

void ExecuteUnit::execute_vector(core::CPU& cpu, core::Machine& machine, isa::OperationId op_id,
                                 Instruction ir) {
    if (cpu.state().vstart != 0 && requires_zero_vstart(op_id)) {
        cpu.pipeline_context.pending_exception = ExceptionCode::IllegalInstruction;
        cpu.pipeline_context.pending_tval = ir;
        return;
    }

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

    if (is_vector_fp(op_id)) {
        const bool scalar_fp_enabled =
            (sew == 32 && isa::misa_has_extension(cpu.state().misa, isa::IsaExtension::F)) ||
            (sew == 64 && isa::misa_has_extension(cpu.state().misa, isa::IsaExtension::D));
        const bool fs_enabled =
            (cpu.state().mstatus & enum_mask(core::MstatusBit::Fs)) != static_cast<CSRValue>(0);
        const Word frm = (cpu.state().fcsr >> 5U) & 0x7U;
        if (!scalar_fp_enabled || !fs_enabled || (is_vector_fp_arithmetic(op_id) && frm >= 5U)) {
            // SEW=16 vector FP belongs to Zvfh, which SimRV does not advertise.
            cpu.pipeline_context.pending_exception = ExceptionCode::IllegalInstruction;
            cpu.pipeline_context.pending_tval = ir;
            return;
        }
    }

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
            execute_vector_fixed_point(cpu, op_id, rd, rs1, rs2, vm, vl, sew,
                                       cpu.state().regs.read(rs1), simm5);
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
            execute_vector_permute(cpu, op_id, rd, rs1, rs2, vm, vl, sew,
                                   cpu.state().regs.read(rs1), simm5);
            break;

        // Integer Arithmetic (default/fallback for all remaining vector integer ops)
        default:
            execute_vector_integer(cpu, op_id, rd, rs1, rs2, vm, vl, sew,
                                   cpu.state().regs.read(rs1), simm5);
            break;
    }

    // Vector 1.0 requires every successfully completed vector instruction, including vset*vl*, to
    // reset vstart. Faulting vector memory operations leave the faulting element index for resume.
    if (!cpu.pipeline_context.pending_exception.has_value()) {
        if (is_vector_fp(op_id)) {
            // Vector FP may update scalar FP registers or fflags. Setting FS Dirty
            // conservatively is permitted even when a particular result is exact.
            cpu.state().mstatus |= enum_mask(core::MstatusBit::Fs);
        }
        cpu.state().vstart = 0;
    }
}

}  // namespace simrv::execute
