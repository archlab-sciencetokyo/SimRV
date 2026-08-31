#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>

#include "simrv/isa/OperationId.hpp"

namespace simrv::pipeline::operation {

enum class ExecutionClass : uint8_t {
    Default,
    Multiply,
    DivideOrRemainder,
    FpAlu,
    FpDivideOrSqrt,
};

enum class RegBank : uint8_t {
    None,
    Integer,
    Float,
    Vector,
};

enum class MemoryAccessKind : uint8_t {
    None,
    Load,
    Store,
    Atomic,
};

enum class ControlFlowKind : uint8_t {
    None,
    Branch,
    Jump,
    Jalr,
};

enum class SideEffectFlags : uint8_t {
    None = 0,
    Csr = 1U << 0U,
    Fence = 1U << 1U,
    System = 1U << 2U,
    Serializing = 1U << 3U,
    Trap = 1U << 4U,
};

[[nodiscard]] constexpr auto operator|(SideEffectFlags lhs, SideEffectFlags rhs) noexcept
    -> SideEffectFlags {
    return static_cast<SideEffectFlags>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

[[nodiscard]] constexpr auto operator&(SideEffectFlags lhs, SideEffectFlags rhs) noexcept -> bool {
    return (static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs)) != 0;
}

struct OperandDescriptor {
    RegBank rd{RegBank::None};
    RegBank rs1{RegBank::None};
    RegBank rs2{RegBank::None};
    RegBank rs3{RegBank::None};
};

struct OperationInfo {
    ExecutionClass execution_class{ExecutionClass::Default};
    OperandDescriptor operands{};
    MemoryAccessKind memory{MemoryAccessKind::None};
    ControlFlowKind control{ControlFlowKind::None};
    SideEffectFlags side_effects{SideEffectFlags::None};
};

consteval auto generate_operation_info() {
    std::array<OperationInfo, isa::kOperationIdCount> info{};

    const auto set_props = [&info](std::initializer_list<isa::OperationId> operations,
                                   ExecutionClass exec_class, OperandDescriptor ops,
                                   MemoryAccessKind mem = MemoryAccessKind::None,
                                   ControlFlowKind ctrl = ControlFlowKind::None,
                                   SideEffectFlags side = SideEffectFlags::None) {
        for (const auto op : operations) {
            auto& entry = info.at(static_cast<std::size_t>(op));
            entry.execution_class = exec_class;
            entry.operands = ops;
            entry.memory = mem;
            entry.control = ctrl;
            entry.side_effects = side;
        }
    };

    using enum isa::OperationId;

    constexpr auto b_none = RegBank::None;
    constexpr auto b_int = RegBank::Integer;
    constexpr auto b_fp = RegBank::Float;
    constexpr auto b_vec = RegBank::Vector;

    constexpr auto exec_def = ExecutionClass::Default;
    constexpr auto exec_mul = ExecutionClass::Multiply;
    constexpr auto exec_div = ExecutionClass::DivideOrRemainder;
    constexpr auto exec_fpalu = ExecutionClass::FpAlu;
    constexpr auto exec_fpdiv = ExecutionClass::FpDivideOrSqrt;

    constexpr auto mem_none = MemoryAccessKind::None;
    constexpr auto mem_load = MemoryAccessKind::Load;
    constexpr auto mem_store = MemoryAccessKind::Store;
    constexpr auto mem_atomic = MemoryAccessKind::Atomic;

    constexpr auto ctrl_none = ControlFlowKind::None;
    constexpr auto ctrl_branch = ControlFlowKind::Branch;
    constexpr auto ctrl_jump = ControlFlowKind::Jump;
    constexpr auto ctrl_jalr = ControlFlowKind::Jalr;

    constexpr auto side_fence = SideEffectFlags::Fence | SideEffectFlags::Serializing;
    constexpr auto side_csr = SideEffectFlags::Csr | SideEffectFlags::Serializing;
    constexpr auto side_sys = SideEffectFlags::System | SideEffectFlags::Serializing;
    constexpr auto side_trap =
        SideEffectFlags::Trap | SideEffectFlags::System | SideEffectFlags::Serializing;
    constexpr auto side_serial = SideEffectFlags::Serializing;

    // RV32I / RV64I Upper Immediate
    set_props({LUI, AUIPC}, exec_def, {.rd = b_int, .rs1 = b_none, .rs2 = b_none, .rs3 = b_none});

    // Control Flow
    set_props({JAL}, exec_def, {.rd = b_int, .rs1 = b_none, .rs2 = b_none, .rs3 = b_none}, mem_none,
              ctrl_jump);
    set_props({JALR}, exec_def, {.rd = b_int, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none}, mem_none,
              ctrl_jalr);
    set_props({BEQ, BNE, BLT, BGE, BLTU, BGEU}, exec_def,
              {.rd = b_none, .rs1 = b_int, .rs2 = b_int, .rs3 = b_none}, mem_none, ctrl_branch);

    // Loads
    set_props({LB, LH, LW, LD, LBU, LHU, LWU}, exec_def,
              {.rd = b_int, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none}, mem_load);

    // Stores
    set_props({SB, SH, SW, SD}, exec_def, {.rd = b_none, .rs1 = b_int, .rs2 = b_int, .rs3 = b_none},
              mem_store);

    // Integer ALU Immediate
    set_props({ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI, ADDIW, SLLIW, SRLIW, SRAIW},
              exec_def, {.rd = b_int, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none});

    // Integer ALU Register-Register
    set_props({ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND, ADDW, SUBW, SLLW, SRLW, SRAW},
              exec_def, {.rd = b_int, .rs1 = b_int, .rs2 = b_int, .rs3 = b_none});

    // System / Fences / CSRs
    set_props({FENCE, FENCE_I}, exec_def,
              {.rd = b_none, .rs1 = b_none, .rs2 = b_none, .rs3 = b_none}, mem_none, ctrl_none,
              side_fence);
    set_props({ECALL, EBREAK}, exec_def,
              {.rd = b_none, .rs1 = b_none, .rs2 = b_none, .rs3 = b_none}, mem_none, ctrl_none,
              side_trap);
    set_props({CSRRW, CSRRS, CSRRC}, exec_def,
              {.rd = b_int, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none}, mem_none, ctrl_none,
              side_csr);
    set_props({CSRRWI, CSRRSI, CSRRCI}, exec_def,
              {.rd = b_int, .rs1 = b_none, .rs2 = b_none, .rs3 = b_none}, mem_none, ctrl_none,
              side_csr);

    // Privileged
    set_props({URET, SRET, MRET, WFI}, exec_def,
              {.rd = b_none, .rs1 = b_none, .rs2 = b_none, .rs3 = b_none}, mem_none, ctrl_none,
              side_sys);
    set_props({SFENCE_VMA}, exec_def, {.rd = b_none, .rs1 = b_int, .rs2 = b_int, .rs3 = b_none},
              mem_none, ctrl_none, side_sys);

    // M Extension (Multiply / Divide)
    set_props({MUL, MULH, MULHSU, MULHU, MULW}, exec_mul,
              {.rd = b_int, .rs1 = b_int, .rs2 = b_int, .rs3 = b_none});
    set_props({DIV, DIVU, REM, REMU, DIVW, DIVUW, REMW, REMUW}, exec_div,
              {.rd = b_int, .rs1 = b_int, .rs2 = b_int, .rs3 = b_none});

    // A Extension (Atomics)
    set_props({LR_W, LR_D}, exec_def, {.rd = b_int, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none},
              mem_atomic);
    set_props({SC_W,     SC_D,     AMOSWAP_W, AMOADD_W,  AMOXOR_W,  AMOAND_W, AMOOR_W,
               AMOMIN_W, AMOMAX_W, AMOMINU_W, AMOMAXU_W, AMOSWAP_D, AMOADD_D, AMOXOR_D,
               AMOAND_D, AMOOR_D,  AMOMIN_D,  AMOMAX_D,  AMOMINU_D, AMOMAXU_D},
              exec_def, {.rd = b_int, .rs1 = b_int, .rs2 = b_int, .rs3 = b_none}, mem_atomic);

    // F Extension (Single-Precision Floating Point)
    set_props({FLW}, exec_def, {.rd = b_fp, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none}, mem_load);
    set_props({FSW}, exec_def, {.rd = b_none, .rs1 = b_int, .rs2 = b_fp, .rs3 = b_none}, mem_store);
    set_props({FADD_S, FSUB_S, FMUL_S, FSGNJ_S, FSGNJN_S, FSGNJX_S, FMIN_S, FMAX_S}, exec_fpalu,
              {.rd = b_fp, .rs1 = b_fp, .rs2 = b_fp, .rs3 = b_none});
    set_props({FMADD_S, FMSUB_S, FNMADD_S, FNMSUB_S}, exec_fpalu,
              {.rd = b_fp, .rs1 = b_fp, .rs2 = b_fp, .rs3 = b_fp});
    set_props({FDIV_S}, exec_fpdiv, {.rd = b_fp, .rs1 = b_fp, .rs2 = b_fp, .rs3 = b_none});
    set_props({FSQRT_S}, exec_fpdiv, {.rd = b_fp, .rs1 = b_fp, .rs2 = b_none, .rs3 = b_none});
    set_props({FEQ_S, FLT_S, FLE_S}, exec_fpalu,
              {.rd = b_int, .rs1 = b_fp, .rs2 = b_fp, .rs3 = b_none});
    set_props({FCLASS_S, FMV_X_W, FCVT_W_S, FCVT_WU_S, FCVT_L_S, FCVT_LU_S}, exec_fpalu,
              {.rd = b_int, .rs1 = b_fp, .rs2 = b_none, .rs3 = b_none});
    set_props({FMV_W_X, FCVT_S_W, FCVT_S_WU, FCVT_S_L, FCVT_S_LU}, exec_fpalu,
              {.rd = b_fp, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none});

    // D Extension (Double-Precision Floating Point)
    set_props({FLD}, exec_def, {.rd = b_fp, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none}, mem_load);
    set_props({FSD}, exec_def, {.rd = b_none, .rs1 = b_int, .rs2 = b_fp, .rs3 = b_none}, mem_store);
    set_props({FADD_D, FSUB_D, FMUL_D, FSGNJ_D, FSGNJN_D, FSGNJX_D, FMIN_D, FMAX_D}, exec_fpalu,
              {.rd = b_fp, .rs1 = b_fp, .rs2 = b_fp, .rs3 = b_none});
    set_props({FMADD_D, FMSUB_D, FNMADD_D, FNMSUB_D}, exec_fpalu,
              {.rd = b_fp, .rs1 = b_fp, .rs2 = b_fp, .rs3 = b_fp});
    set_props({FDIV_D}, exec_fpdiv, {.rd = b_fp, .rs1 = b_fp, .rs2 = b_fp, .rs3 = b_none});
    set_props({FSQRT_D}, exec_fpdiv, {.rd = b_fp, .rs1 = b_fp, .rs2 = b_none, .rs3 = b_none});
    set_props({FEQ_D, FLT_D, FLE_D}, exec_fpalu,
              {.rd = b_int, .rs1 = b_fp, .rs2 = b_fp, .rs3 = b_none});
    set_props({FCLASS_D, FMV_X_D, FCVT_W_D, FCVT_WU_D, FCVT_L_D, FCVT_LU_D}, exec_fpalu,
              {.rd = b_int, .rs1 = b_fp, .rs2 = b_none, .rs3 = b_none});
    set_props({FMV_D_X, FCVT_D_W, FCVT_D_WU, FCVT_D_L, FCVT_D_LU}, exec_fpalu,
              {.rd = b_fp, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none});
    set_props({FCVT_S_D, FCVT_D_S}, exec_fpalu,
              {.rd = b_fp, .rs1 = b_fp, .rs2 = b_none, .rs3 = b_none});

    // B Extension (Bit Manipulation)
    set_props({SH1ADD, SH2ADD, SH3ADD, SH1ADD_UW, SH2ADD_UW, SH3ADD_UW, ADD_UW, ANDN, ORN,
               XNOR,   MIN,    MAX,    MINU,      MAXU,      ROL,       ROR,    ROLW, RORW,
               CLMUL,  CLMULH, CLMULR, BSET,      BCLR,      BINV,      BEXT,   PACK, PACKW},
              exec_def, {.rd = b_int, .rs1 = b_int, .rs2 = b_int, .rs3 = b_none});
    set_props({SLLI_UW, CLZ, CTZ, CPOP, SEXT_B, SEXT_H, ZEXT_H, RORI, CLZW, CTZW, CPOPW, RORIW,
               BSETI, BCLRI, BINVI, BEXTI, ORC_B, REV8},
              exec_def, {.rd = b_int, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none});

    // Vector Extension (VSET / Loads / Stores / Compute)
    set_props({VSETVLI, VSETIVLI, VSETVL}, exec_def,
              {.rd = b_int, .rs1 = b_int, .rs2 = b_int, .rs3 = b_none}, mem_none, ctrl_none,
              side_serial);
    set_props({VLE8_V,     VLE16_V,    VLE32_V,    VLE64_V,    VLSE8_V,    VLSE16_V,  VLSE32_V,
               VLSE64_V,   VLUXEI8_V,  VLUXEI16_V, VLUXEI32_V, VLUXEI64_V, VLOXEI8_V, VLOXEI16_V,
               VLOXEI32_V, VLOXEI64_V, VL1RE8_V,   VL1RE16_V,  VL1RE32_V,  VL1RE64_V, VL2RE8_V,
               VL2RE16_V,  VL2RE32_V,  VL2RE64_V,  VL4RE8_V,   VL4RE16_V,  VL4RE32_V, VL4RE64_V,
               VL8RE8_V,   VL8RE16_V,  VL8RE32_V,  VL8RE64_V},
              exec_def, {.rd = b_vec, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none}, mem_load,
              ctrl_none, side_serial);
    set_props({VSE8_V,     VSE16_V,    VSE32_V,    VSE64_V,    VSSE8_V,    VSSE16_V,  VSSE32_V,
               VSSE64_V,   VSUXEI8_V,  VSUXEI16_V, VSUXEI32_V, VSUXEI64_V, VSOXEI8_V, VSOXEI16_V,
               VSOXEI32_V, VSOXEI64_V, VS1R_V,     VS2R_V,     VS4R_V,     VS8R_V},
              exec_def, {.rd = b_none, .rs1 = b_int, .rs2 = b_vec, .rs3 = b_none}, mem_store,
              ctrl_none, side_serial);

    // Vector Compute Operations (marked Serializing)
    set_props({VADD_VV,    VSUB_VV,    VMUL_VV,    VDIV_VV,     VDIVU_VV, VAND_VV,   VOR_VV,
               VXOR_VV,    VSLL_VV,    VSRL_VV,    VSRA_VV,     VMV_V_V,  VMSEQ_VV,  VMSNE_VV,
               VMSLT_VV,   VMSLTU_VV,  VMSLE_VV,   VMSLEU_VV,   VMACC_VV, VMADD_VV,  VNMSAC_VV,
               VNSUB_VV,   VWMACCU_VV, VWMACC_VV,  VWMACCSU_VV, VMIN_VV,  VMINU_VV,  VMAX_VV,
               VMAXU_VV,   VFMACC_VV,  VWMUL_VV,   VFADD_VV,    VSADD_VV, VSADDU_VV, VSMUL_VV,
               VSSRA_VV,   VSSRL_VV,   VSSUB_VV,   VSSUBU_VV,   VWADD_VV, VWADDU_VV, VWSUB_VV,
               VWSUBU_VV,  VWMULU_VV,  VWMULSU_VV, VANDN_VV,    VROL_VV,  VROR_VV,   VCLMUL_VV,
               VCLMULH_VV, VWSLL_VV},
              exec_def, {.rd = b_vec, .rs1 = b_vec, .rs2 = b_vec, .rs3 = b_none}, mem_none,
              ctrl_none, side_serial);

    set_props({VADD_VX,   VSUB_VX,       VMUL_VX,      VDIV_VX,        VDIVU_VX,    VAND_VX,
               VOR_VX,    VXOR_VX,       VSLL_VX,      VSRL_VX,        VSRA_VX,     VMV_V_X,
               VMV_S_X,   VMSEQ_VX,      VMSNE_VX,     VMSLT_VX,       VMSLTU_VX,   VMSLE_VX,
               VMSLEU_VX, VMSGT_VX,      VMSGTU_VX,    VMACC_VX,       VMADD_VX,    VNMSAC_VX,
               VNSUB_VX,  VWMACCU_VX,    VWMACC_VX,    VWMACCUS_VX,    VWMACCSU_VX, VMIN_VX,
               VMINU_VX,  VMAX_VX,       VMAXU_VX,     VWMUL_VX,       VNCLIP_WX,   VNCLIPU_WX,
               VNSRL_WX,  VNSRA_WX,      VSLIDE1UP_VX, VSLIDE1DOWN_VX, VRSUB_VX,    VSADD_VX,
               VSADDU_VX, VSLIDEDOWN_VX, VSLIDEUP_VX,  VSMUL_VX,       VSSRA_VX,    VSSRL_VX,
               VSSUB_VX,  VSSUBU_VX,     VWADD_VX,     VWADD_WX,       VWADDU_VX,   VWADDU_WX,
               VWSUB_VX,  VWSUB_WX,      VWSUBU_VX,    VWSUBU_WX,      VWMULU_VX,   VWMULSU_VX,
               VANDN_VX,  VROL_VX,       VROR_VX,      VCLMUL_VX,      VCLMULH_VX,  VWSLL_VX},
              exec_def, {.rd = b_vec, .rs1 = b_int, .rs2 = b_vec, .rs3 = b_none}, mem_none,
              ctrl_none, side_serial);

    set_props({VADD_VI,       VAND_VI,     VOR_VI,   VXOR_VI,  VSLL_VI,   VSRL_VI,  VSRA_VI,
               VMV_V_I,       VMSEQ_VI,    VMSNE_VI, VMSLE_VI, VMSLEU_VI, VMSGT_VI, VMSGTU_VI,
               VNCLIP_WI,     VNCLIPU_WI,  VNSRL_WI, VNSRA_WI, VRSUB_VI,  VSADD_VI, VSADDU_VI,
               VSLIDEDOWN_VI, VSLIDEUP_VI, VSSRA_VI, VSSRL_VI, VROR_VI,   VWSLL_VI},
              exec_def, {.rd = b_vec, .rs1 = b_none, .rs2 = b_vec, .rs3 = b_none}, mem_none,
              ctrl_none, side_serial);

    set_props({VMV_X_S, VFIRST_M, VCPOP_M}, exec_def,
              {.rd = b_int, .rs1 = b_vec, .rs2 = b_none, .rs3 = b_none}, mem_none, ctrl_none,
              side_serial);
    set_props({VFMACC_VF, VFADD_VF, VFMERGE_VFM}, exec_def,
              {.rd = b_vec, .rs1 = b_fp, .rs2 = b_vec, .rs3 = b_none}, mem_none, ctrl_none,
              side_serial);
    set_props({VFMV_F_S}, exec_def, {.rd = b_fp, .rs1 = b_vec, .rs2 = b_none, .rs3 = b_none},
              mem_none, ctrl_none, side_serial);
    set_props({VFMV_S_F}, exec_def, {.rd = b_vec, .rs1 = b_fp, .rs2 = b_none, .rs3 = b_none},
              mem_none, ctrl_none, side_serial);

    set_props({VID_V,   VSEXT_VF2, VSEXT_VF4, VSEXT_VF8, VZEXT_VF2, VZEXT_VF4, VZEXT_VF8,
               VCLZ_V,  VCTZ_V,    VCPOP_V,   VBREV_V,   VBREV8_V,  VREV8_V,   VIOTA_M,
               VMSBF_M, VMSIF_M,   VMSOF_M,   VMV1R_V,   VMV2R_V,   VMV4R_V,   VMV8R_V},
              exec_def, {.rd = b_vec, .rs1 = b_vec, .rs2 = b_none, .rs3 = b_none}, mem_none,
              ctrl_none, side_serial);

    set_props({VMERGE_VVM, VSBC_VVM,     VADC_VVM,  VMADC_VV,   VMADC_VVM,   VMSBC_VV,    VMSBC_VVM,
               VMAND_MM,   VMNAND_MM,    VMANDN_MM, VMOR_MM,    VMNOR_MM,    VMORN_MM,    VMXOR_MM,
               VMXNOR_MM,  VCOMPRESS_VM, VWADD_WV,  VWADDU_WV,  VWSUB_WV,    VWSUBU_WV,   VNCLIP_WV,
               VNCLIPU_WV, VNSRL_WV,     VNSRA_WV,  VREDSUM_VS, VWREDSUM_VS, VWREDSUMU_VS},
              exec_def, {.rd = b_vec, .rs1 = b_vec, .rs2 = b_vec, .rs3 = b_none}, mem_none,
              ctrl_none, side_serial);

    set_props({VMERGE_VXM, VSBC_VXM, VADC_VXM, VMADC_VX, VMADC_VXM, VMSBC_VX, VMSBC_VXM}, exec_def,
              {.rd = b_vec, .rs1 = b_int, .rs2 = b_vec, .rs3 = b_none}, mem_none, ctrl_none,
              side_serial);

    set_props({VMERGE_VIM, VADC_VIM, VMADC_VI, VMADC_VIM}, exec_def,
              {.rd = b_vec, .rs1 = b_none, .rs2 = b_vec, .rs3 = b_none}, mem_none, ctrl_none,
              side_serial);

    return info;
}

inline constexpr auto kOperationInfo = generate_operation_info();

[[nodiscard]] constexpr auto info(isa::OperationId operation) noexcept -> OperationInfo {
    const auto index = static_cast<std::size_t>(operation);
    return index < kOperationInfo.size() ? kOperationInfo[index] : OperationInfo{};
}

}  // namespace simrv::pipeline::operation
