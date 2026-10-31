#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>

#include "simrv/isa/InstructionFields.hpp"
#include "simrv/isa/IsaConfig.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::pipeline::operation {

enum class ExecutionClass : uint8_t {
    Default,
    Multiply,
    DivideOrRemainder,
    FpAlu,
    FpDivideOrSqrt,
    Cfu,
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
    simrv::xlen::CsrValue required_misa{0};
    uint8_t mem_access_bytes{0};
    simrv::isa::Funct3 mem_funct3{simrv::isa::Funct3::Lb};
    bool has_fixed_mem_funct3{false};
    bool rv64_only{false};
};

[[nodiscard]] consteval auto compute_operation_misa_mask(isa::OperationId op_id) noexcept
    -> simrv::xlen::CsrValue {
    using namespace simrv::isa;
    const auto op = std::to_underlying(op_id);
    const auto in_range = [op](OperationId first, OperationId last) {
        return op >= std::to_underlying(first) && op <= std::to_underlying(last);
    };

    if (in_range(OperationId::MUL, OperationId::REMUW)) {
        return misa_extension_bit(IsaExtension::M);
    }
    if (in_range(OperationId::LR_W, OperationId::AMOMAXU_D) || op_id == OperationId::WRS_NTO ||
        op_id == OperationId::WRS_STO) {
        return misa_extension_bit(IsaExtension::A);
    }
    if (in_range(OperationId::FLW, OperationId::FCVT_S_LU)) {
        return misa_extension_bit(IsaExtension::F);
    }
    if (in_range(OperationId::FLD, OperationId::FCVT_D_LU)) {
        return misa_extension_bit(IsaExtension::F) | misa_extension_bit(IsaExtension::D);
    }
    if (in_range(OperationId::FLH, OperationId::FCVT_H_D)) {
        const auto required = misa_extension_bit(IsaExtension::F);
        if (op_id == OperationId::FCVT_D_H || op_id == OperationId::FCVT_H_D) {
            return required | misa_extension_bit(IsaExtension::D);
        }
        return required;
    }
    switch (op_id) {
        case OperationId::FLI_S:
        case OperationId::FMINM_S:
        case OperationId::FMAXM_S:
        case OperationId::FLEQ_S:
        case OperationId::FLTQ_S:
        case OperationId::FROUND_S:
        case OperationId::FROUNDNX_S:
            return misa_extension_bit(IsaExtension::F);
        case OperationId::FLI_D:
        case OperationId::FMINM_D:
        case OperationId::FMAXM_D:
        case OperationId::FLEQ_D:
        case OperationId::FLTQ_D:
        case OperationId::FROUND_D:
        case OperationId::FROUNDNX_D:
        case OperationId::FCVTMOD_W_D:
            return misa_extension_bit(IsaExtension::F) | misa_extension_bit(IsaExtension::D);
        default:
            break;
    }
    if (in_range(OperationId::SH1ADD, OperationId::PACKW)) {
        return misa_extension_bit(IsaExtension::B);
    }
    if (in_range(OperationId::VSETVLI, OperationId::VWSLL_VI)) {
        simrv::xlen::CsrValue required = misa_extension_bit(IsaExtension::V);
        switch (op_id) {
            case OperationId::VFADD_VV:
            case OperationId::VFSLIDE1UP_VF:
            case OperationId::VFSLIDE1DOWN_VF:
            case OperationId::VFADD_VF:
            case OperationId::VFSUB_VV:
            case OperationId::VFSUB_VF:
            case OperationId::VFRSUB_VF:
            case OperationId::VFMUL_VV:
            case OperationId::VFMUL_VF:
            case OperationId::VFDIV_VV:
            case OperationId::VFDIV_VF:
            case OperationId::VFRDIV_VF:
            case OperationId::VFSQRT_V:
            case OperationId::VFRSQRT7_V:
            case OperationId::VFREC7_V:
            case OperationId::VFCLASS_V:
            case OperationId::VFCVT_XU_F_V:
            case OperationId::VFCVT_X_F_V:
            case OperationId::VFCVT_F_XU_V:
            case OperationId::VFCVT_F_X_V:
            case OperationId::VFCVT_RTZ_XU_F_V:
            case OperationId::VFCVT_RTZ_X_F_V:
            case OperationId::VFWCVT_XU_F_V:
            case OperationId::VFWCVT_X_F_V:
            case OperationId::VFWCVT_F_XU_V:
            case OperationId::VFWCVT_F_X_V:
            case OperationId::VFWCVT_F_F_V:
            case OperationId::VFWCVT_RTZ_XU_F_V:
            case OperationId::VFWCVT_RTZ_X_F_V:
            case OperationId::VFNCVT_XU_F_W:
            case OperationId::VFNCVT_X_F_W:
            case OperationId::VFNCVT_F_XU_W:
            case OperationId::VFNCVT_F_X_W:
            case OperationId::VFNCVT_F_F_W:
            case OperationId::VFNCVT_ROD_F_F_W:
            case OperationId::VFNCVT_RTZ_XU_F_W:
            case OperationId::VFNCVT_RTZ_X_F_W:
            case OperationId::VFSGNJ_VV:
            case OperationId::VFSGNJ_VF:
            case OperationId::VFSGNJN_VV:
            case OperationId::VFSGNJN_VF:
            case OperationId::VFSGNJX_VV:
            case OperationId::VFSGNJX_VF:
            case OperationId::VFMIN_VV:
            case OperationId::VFMIN_VF:
            case OperationId::VFMAX_VV:
            case OperationId::VFMAX_VF:
            case OperationId::VMFEQ_VV:
            case OperationId::VMFEQ_VF:
            case OperationId::VMFNE_VV:
            case OperationId::VMFNE_VF:
            case OperationId::VMFLT_VV:
            case OperationId::VMFLT_VF:
            case OperationId::VMFLE_VV:
            case OperationId::VMFLE_VF:
            case OperationId::VMFGT_VF:
            case OperationId::VMFGE_VF:
            case OperationId::VFMADD_VV:
            case OperationId::VFMADD_VF:
            case OperationId::VFNMADD_VV:
            case OperationId::VFNMADD_VF:
            case OperationId::VFMSUB_VV:
            case OperationId::VFMSUB_VF:
            case OperationId::VFNMSUB_VV:
            case OperationId::VFNMSUB_VF:
            case OperationId::VFMACC_VV:
            case OperationId::VFMACC_VF:
            case OperationId::VFNMACC_VV:
            case OperationId::VFNMACC_VF:
            case OperationId::VFMSAC_VV:
            case OperationId::VFMSAC_VF:
            case OperationId::VFNMSAC_VV:
            case OperationId::VFNMSAC_VF:
            case OperationId::VFMV_F_S:
            case OperationId::VFMV_S_F:
            case OperationId::VFMERGE_VFM:
            case OperationId::VFMV_V_F:
            case OperationId::VFREDUSUM_VS:
            case OperationId::VFREDOSUM_VS:
            case OperationId::VFREDMIN_VS:
            case OperationId::VFREDMAX_VS:
            case OperationId::VFWREDUSUM_VS:
            case OperationId::VFWREDOSUM_VS:
            case OperationId::VFWADD_VV:
            case OperationId::VFWADD_VF:
            case OperationId::VFWSUB_VV:
            case OperationId::VFWSUB_VF:
            case OperationId::VFWADD_WV:
            case OperationId::VFWADD_WF:
            case OperationId::VFWSUB_WV:
            case OperationId::VFWSUB_WF:
            case OperationId::VFWMUL_VV:
            case OperationId::VFWMUL_VF:
            case OperationId::VFWMACC_VV:
            case OperationId::VFWMACC_VF:
            case OperationId::VFWNMACC_VV:
            case OperationId::VFWNMACC_VF:
            case OperationId::VFWMSAC_VV:
            case OperationId::VFWMSAC_VF:
            case OperationId::VFWNMSAC_VV:
            case OperationId::VFWNMSAC_VF:
                required |= misa_extension_bit(IsaExtension::F);
                break;
            case OperationId::VANDN_VV:
            case OperationId::VANDN_VX:
            case OperationId::VROL_VV:
            case OperationId::VROL_VX:
            case OperationId::VROR_VV:
            case OperationId::VROR_VX:
            case OperationId::VROR_VI:
            case OperationId::VCLZ_V:
            case OperationId::VCTZ_V:
            case OperationId::VCPOP_V:
            case OperationId::VBREV_V:
            case OperationId::VBREV8_V:
            case OperationId::VREV8_V:
            case OperationId::VCLMUL_VV:
            case OperationId::VCLMUL_VX:
            case OperationId::VCLMULH_VV:
            case OperationId::VCLMULH_VX:
            case OperationId::VWSLL_VV:
            case OperationId::VWSLL_VX:
            case OperationId::VWSLL_VI:
                required |= misa_extension_bit(IsaExtension::B);
                break;
            default:
                break;
        }
        return required;
    }
    return misa_extension_bit(IsaExtension::I);
}

[[nodiscard]] consteval auto compute_operation_rv64_only(isa::OperationId op_id) noexcept -> bool {
    using enum isa::OperationId;
    switch (op_id) {
        case LD:
        case LWU:
        case SD:
        case HLV_WU:
        case HLV_D:
        case HLVX_WU:
        case HSV_D:
        case ADDIW:
        case SLLIW:
        case SRLIW:
        case SRAIW:
        case ADDW:
        case SUBW:
        case SLLW:
        case SRLW:
        case SRAW:
        case MULW:
        case DIVW:
        case DIVUW:
        case REMW:
        case REMUW:
        case FCVT_L_S:
        case FCVT_LU_S:
        case FCVT_S_L:
        case FCVT_S_LU:
        case FCVT_L_D:
        case FCVT_LU_D:
        case FCVT_D_L:
        case FCVT_D_LU:
        case FMV_X_D:
        case FMV_D_X:
        case VLUXEI64_V:
        case VLOXEI64_V:
        case VSUXEI64_V:
        case VSOXEI64_V:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] consteval auto compute_operation_mem_bytes(isa::OperationId op_id) noexcept
    -> uint8_t {
    using enum isa::OperationId;
    switch (op_id) {
        case LB:
        case LBU:
        case SB:
        case HLV_B:
        case HLV_BU:
        case HSV_B:
            return 1;
        case LH:
        case LHU:
        case SH:
        case FLH:
        case FSH:
        case HLV_H:
        case HLV_HU:
        case HLVX_HU:
        case HSV_H:
            return 2;
        case LW:
        case LWU:
        case SW:
        case FLW:
        case FSW:
        case HLV_W:
        case HLV_WU:
        case HLVX_WU:
        case HSV_W:
        case LR_W:
        case SC_W:
        case AMOSWAP_W:
        case AMOADD_W:
        case AMOXOR_W:
        case AMOAND_W:
        case AMOOR_W:
        case AMOMIN_W:
        case AMOMAX_W:
        case AMOMINU_W:
        case AMOMAXU_W:
            return 4;
        case LD:
        case SD:
        case FLD:
        case FSD:
        case HLV_D:
        case HSV_D:
        case LR_D:
        case SC_D:
        case AMOSWAP_D:
        case AMOADD_D:
        case AMOXOR_D:
        case AMOAND_D:
        case AMOOR_D:
        case AMOMIN_D:
        case AMOMAX_D:
        case AMOMINU_D:
        case AMOMAXU_D:
            return 8;
        case CBO_ZERO:
            return 64;
        default:
            if (op_id >= VLE8_V && op_id <= VLE64_V) {
                return static_cast<uint8_t>(
                    1U << (std::to_underlying(op_id) - std::to_underlying(VLE8_V)));
            }
            if (op_id >= VSE8_V && op_id <= VSE64_V) {
                return static_cast<uint8_t>(
                    1U << (std::to_underlying(op_id) - std::to_underlying(VSE8_V)));
            }
            return 0;
    }
}

[[nodiscard]] consteval auto compute_operation_fixed_funct3(isa::OperationId op_id,
                                                            simrv::isa::Funct3& out_f3) noexcept
    -> bool {
    using enum isa::OperationId;
    using simrv::isa::Funct3;
    switch (op_id) {
        case HLV_B:
            out_f3 = Funct3::Lb;
            return true;
        case HLV_BU:
            out_f3 = Funct3::Lbu;
            return true;
        case HLV_H:
            out_f3 = Funct3::Lh;
            return true;
        case HLV_HU:
        case HLVX_HU:
            out_f3 = Funct3::Lhu;
            return true;
        case HLV_W:
            out_f3 = Funct3::Lw;
            return true;
        case HLV_WU:
        case HLVX_WU:
            out_f3 = Funct3::Lwu;
            return true;
        case HLV_D:
        case HSV_D:
            out_f3 = Funct3::Ld;
            return true;
        case HSV_B:
            out_f3 = Funct3::Sb;
            return true;
        case HSV_H:
            out_f3 = Funct3::Sh;
            return true;
        case HSV_W:
            out_f3 = Funct3::Sw;
            return true;
        default:
            return false;
    }
}

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
    set_props({HLV_B, HLV_BU, HLV_H, HLV_HU, HLV_W, HLV_WU, HLV_D, HLVX_HU, HLVX_WU}, exec_def,
              {.rd = b_int, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none}, mem_load);

    // Stores
    set_props({SB, SH, SW, SD}, exec_def, {.rd = b_none, .rs1 = b_int, .rs2 = b_int, .rs3 = b_none},
              mem_store);
    set_props({HSV_B, HSV_H, HSV_W, HSV_D}, exec_def,
              {.rd = b_none, .rs1 = b_int, .rs2 = b_int, .rs3 = b_none}, mem_store);

    // Integer ALU Immediate
    set_props({ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI, ADDIW, SLLIW, SRLIW, SRAIW},
              exec_def, {.rd = b_int, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none});

    // Integer ALU Register-Register
    set_props({ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND, ADDW, SUBW, SLLW, SRLW, SRAW},
              exec_def, {.rd = b_int, .rs1 = b_int, .rs2 = b_int, .rs3 = b_none});

    // Zicond (czero.eqz, czero.nez)
    set_props({CZERO_EQZ, CZERO_NEZ}, exec_def,
              {.rd = b_int, .rs1 = b_int, .rs2 = b_int, .rs3 = b_none});

    // Zicbo (Cache Block Operations)
    set_props({CBO_ZERO}, exec_def, {.rd = b_none, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none},
              mem_store);
    set_props({CBO_CLEAN, CBO_FLUSH, CBO_INVAL}, exec_def,
              {.rd = b_none, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none}, mem_none, ctrl_none,
              side_fence);
    set_props({PREFETCH_I, PREFETCH_R, PREFETCH_W}, exec_def,
              {.rd = b_none, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none}, mem_none, ctrl_none);

    // Svinval (Fine-Grained Page Table Invalidation)
    set_props({SINVAL_VMA}, exec_def, {.rd = b_none, .rs1 = b_int, .rs2 = b_int, .rs3 = b_none},
              mem_none, ctrl_none, side_sys);
    set_props({SFENCE_W_INVAL, SFENCE_INVAL_IR}, exec_def,
              {.rd = b_none, .rs1 = b_none, .rs2 = b_none, .rs3 = b_none}, mem_none, ctrl_none,
              side_fence);

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

    // Privileged & System Extensions (Zimop, Zawrs, Svinval, Hypervisor)
    set_props({MOP}, exec_def, {.rd = b_int, .rs1 = b_none, .rs2 = b_none, .rs3 = b_none});
    set_props({URET, SRET, MRET, WFI}, exec_def,
              {.rd = b_none, .rs1 = b_none, .rs2 = b_none, .rs3 = b_none}, mem_none, ctrl_none,
              side_sys);
    set_props({WRS_NTO, WRS_STO}, exec_def,
              {.rd = b_none, .rs1 = b_none, .rs2 = b_none, .rs3 = b_none});
    set_props({SFENCE_VMA}, exec_def, {.rd = b_none, .rs1 = b_int, .rs2 = b_int, .rs3 = b_none},
              mem_none, ctrl_none, side_sys);
    set_props({HFENCE_VVMA, HFENCE_GVMA}, exec_def,
              {.rd = b_none, .rs1 = b_int, .rs2 = b_int, .rs3 = b_none}, mem_none, ctrl_none,
              side_sys);
    set_props({HINVAL_VVMA, HINVAL_GVMA}, exec_def,
              {.rd = b_none, .rs1 = b_int, .rs2 = b_int, .rs3 = b_none}, mem_none, ctrl_none,
              side_sys);

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
    set_props({FLH}, exec_def, {.rd = b_fp, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none}, mem_load);
    set_props({FSH}, exec_def, {.rd = b_none, .rs1 = b_int, .rs2 = b_fp, .rs3 = b_none}, mem_store);
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
    set_props({FMV_X_H}, exec_fpalu, {.rd = b_int, .rs1 = b_fp, .rs2 = b_none, .rs3 = b_none});
    set_props({FMV_H_X}, exec_fpalu, {.rd = b_fp, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none});
    set_props({FCVT_S_H, FCVT_H_S, FCVT_D_H, FCVT_H_D}, exec_fpalu,
              {.rd = b_fp, .rs1 = b_fp, .rs2 = b_none, .rs3 = b_none});
    set_props({FLI_S}, exec_fpalu, {.rd = b_fp, .rs1 = b_none, .rs2 = b_none, .rs3 = b_none});
    set_props({FMINM_S, FMAXM_S}, exec_fpalu,
              {.rd = b_fp, .rs1 = b_fp, .rs2 = b_fp, .rs3 = b_none});
    set_props({FLEQ_S, FLTQ_S}, exec_fpalu, {.rd = b_int, .rs1 = b_fp, .rs2 = b_fp, .rs3 = b_none});
    set_props({FROUND_S, FROUNDNX_S}, exec_fpalu,
              {.rd = b_fp, .rs1 = b_fp, .rs2 = b_none, .rs3 = b_none});
    set_props({FLI_D}, exec_fpalu, {.rd = b_fp, .rs1 = b_none, .rs2 = b_none, .rs3 = b_none});
    set_props({FMINM_D, FMAXM_D}, exec_fpalu,
              {.rd = b_fp, .rs1 = b_fp, .rs2 = b_fp, .rs3 = b_none});
    set_props({FLEQ_D, FLTQ_D}, exec_fpalu, {.rd = b_int, .rs1 = b_fp, .rs2 = b_fp, .rs3 = b_none});
    set_props({FROUND_D, FROUNDNX_D}, exec_fpalu,
              {.rd = b_fp, .rs1 = b_fp, .rs2 = b_none, .rs3 = b_none});
    set_props({FCVTMOD_W_D}, exec_fpalu, {.rd = b_int, .rs1 = b_fp, .rs2 = b_none, .rs3 = b_none});

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
    set_props({VLE8_V,     VLE16_V,    VLE32_V,    VLE64_V,   VLE8FF_V,   VLE16FF_V,  VLE32FF_V,
               VLE64FF_V,  VLM_V,      VLSE8_V,    VLSE16_V,  VLSE32_V,   VLSE64_V,   VLUXEI8_V,
               VLUXEI16_V, VLUXEI32_V, VLUXEI64_V, VLOXEI8_V, VLOXEI16_V, VLOXEI32_V, VLOXEI64_V,
               VL1RE8_V,   VL1RE16_V,  VL1RE32_V,  VL1RE64_V, VL2RE8_V,   VL2RE16_V,  VL2RE32_V,
               VL2RE64_V,  VL4RE8_V,   VL4RE16_V,  VL4RE32_V, VL4RE64_V,  VL8RE8_V,   VL8RE16_V,
               VL8RE32_V,  VL8RE64_V},
              exec_def, {.rd = b_vec, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none}, mem_load,
              ctrl_none, side_serial);
    set_props({VSE8_V,     VSE16_V,    VSE32_V,    VSE64_V,    VSM_V,      VSSE8_V,    VSSE16_V,
               VSSE32_V,   VSSE64_V,   VSUXEI8_V,  VSUXEI16_V, VSUXEI32_V, VSUXEI64_V, VSOXEI8_V,
               VSOXEI16_V, VSOXEI32_V, VSOXEI64_V, VS1R_V,     VS2R_V,     VS4R_V,     VS8R_V},
              exec_def, {.rd = b_none, .rs1 = b_int, .rs2 = b_vec, .rs3 = b_none}, mem_store,
              ctrl_none, side_serial);

    // Vector Compute Operations (marked Serializing)
    set_props(
        {VADD_VV,     VSUB_VV,    VMUL_VV,     VMULH_VV,        VMULHU_VV,  VMULHSU_VV, VDIV_VV,
         VDIVU_VV,    VREM_VV,    VREMU_VV,    VAND_VV,         VOR_VV,     VXOR_VV,    VSLL_VV,
         VSRL_VV,     VSRA_VV,    VMSEQ_VV,    VMSNE_VV,        VMSLT_VV,   VMSLTU_VV,  VMSLE_VV,
         VMSLEU_VV,   VMACC_VV,   VMADD_VV,    VNMSAC_VV,       VNMSUB_VV,  VWMACCU_VV, VWMACC_VV,
         VWMACCSU_VV, VMIN_VV,    VMINU_VV,    VMAX_VV,         VMAXU_VV,   VFMADD_VV,  VFNMADD_VV,
         VFMSUB_VV,   VFNMSUB_VV, VFMACC_VV,   VFNMACC_VV,      VFMSAC_VV,  VFNMSAC_VV, VWMUL_VV,
         VFADD_VV,    VFSUB_VV,   VFMUL_VV,    VFDIV_VV,        VSADD_VV,   VSADDU_VV,  VSMUL_VV,
         VSSRA_VV,    VSSRL_VV,   VSSUB_VV,    VSSUBU_VV,       VWADD_VV,   VWADDU_VV,  VWSUB_VV,
         VWSUBU_VV,   VWMULU_VV,  VWMULSU_VV,  VANDN_VV,        VROL_VV,    VROR_VV,    VCLMUL_VV,
         VCLMULH_VV,  VWSLL_VV,   VRGATHER_VV, VRGATHEREI16_VV, VAADDU_VV,  VAADD_VV,   VASUBU_VV,
         VASUB_VV,    VFSGNJ_VV,  VFSGNJN_VV,  VFSGNJX_VV,      VMFEQ_VV,   VMFNE_VV,   VMFLT_VV,
         VMFLE_VV,    VFMIN_VV,   VFMAX_VV,    VFWADD_VV,       VFWSUB_VV,  VFWADD_WV,  VFWSUB_WV,
         VFWMUL_VV,   VFWMACC_VV, VFWNMACC_VV, VFWMSAC_VV,      VFWNMSAC_VV},
        exec_def, {.rd = b_vec, .rs1 = b_vec, .rs2 = b_vec, .rs3 = b_none}, mem_none, ctrl_none,
        side_serial);

    set_props({VADD_VX,        VSUB_VX,     VMUL_VX,    VMULH_VX,    VMULHU_VX,     VMULHSU_VX,
               VDIV_VX,        VDIVU_VX,    VREM_VX,    VREMU_VX,    VAND_VX,       VOR_VX,
               VXOR_VX,        VSLL_VX,     VSRL_VX,    VSRA_VX,     VMSEQ_VX,      VMSNE_VX,
               VMSLT_VX,       VMSLTU_VX,   VMSLE_VX,   VMSLEU_VX,   VMSGT_VX,      VMSGTU_VX,
               VMACC_VX,       VMADD_VX,    VNMSAC_VX,  VNMSUB_VX,   VWMACCU_VX,    VWMACC_VX,
               VWMACCUS_VX,    VWMACCSU_VX, VMIN_VX,    VMINU_VX,    VMAX_VX,       VMAXU_VX,
               VWMUL_VX,       VNCLIP_WX,   VNCLIPU_WX, VNSRL_WX,    VNSRA_WX,      VSLIDE1UP_VX,
               VSLIDE1DOWN_VX, VRSUB_VX,    VSADD_VX,   VSADDU_VX,   VSLIDEDOWN_VX, VSLIDEUP_VX,
               VSMUL_VX,       VSSRA_VX,    VSSRL_VX,   VSSUB_VX,    VSSUBU_VX,     VWADD_VX,
               VWADD_WX,       VWADDU_VX,   VWADDU_WX,  VWSUB_VX,    VWSUB_WX,      VWSUBU_VX,
               VWSUBU_WX,      VWMULU_VX,   VWMULSU_VX, VANDN_VX,    VROL_VX,       VROR_VX,
               VCLMUL_VX,      VCLMULH_VX,  VWSLL_VX,   VRGATHER_VX, VAADDU_VX,     VAADD_VX,
               VASUBU_VX,      VASUB_VX},
              exec_def, {.rd = b_vec, .rs1 = b_int, .rs2 = b_vec, .rs3 = b_none}, mem_none,
              ctrl_none, side_serial);

    set_props({VADD_VI,     VAND_VI,  VOR_VI,   VXOR_VI,   VSLL_VI,  VSRL_VI,    VSRA_VI,
               VMSEQ_VI,    VMSNE_VI, VMSLE_VI, VMSLEU_VI, VMSGT_VI, VMSGTU_VI,  VNCLIP_WI,
               VNCLIPU_WI,  VNSRL_WI, VNSRA_WI, VRSUB_VI,  VSADD_VI, VSADDU_VI,  VSLIDEDOWN_VI,
               VSLIDEUP_VI, VSSRA_VI, VSSRL_VI, VROR_VI,   VWSLL_VI, VRGATHER_VI},
              exec_def, {.rd = b_vec, .rs1 = b_none, .rs2 = b_vec, .rs3 = b_none}, mem_none,
              ctrl_none, side_serial);

    set_props(
        {VFMADD_VF,     VFNMADD_VF,      VFMSUB_VF, VFNMSUB_VF, VFMACC_VF,  VFNMACC_VF, VFMSAC_VF,
         VFNMSAC_VF,    VFADD_VF,        VFSUB_VF,  VFRSUB_VF,  VFMUL_VF,   VFDIV_VF,   VFRDIV_VF,
         VFSLIDE1UP_VF, VFSLIDE1DOWN_VF, VFSGNJ_VF, VFSGNJN_VF, VFSGNJX_VF, VFMIN_VF,   VFMAX_VF,
         VMFEQ_VF,      VMFNE_VF,        VMFLT_VF,  VMFLE_VF,   VMFGT_VF,   VMFGE_VF,   VFMERGE_VFM,
         VFWADD_VF,     VFWSUB_VF,       VFWADD_WF, VFWSUB_WF,  VFWMUL_VF,  VFWMACC_VF, VFWNMACC_VF,
         VFWMSAC_VF,    VFWNMSAC_VF},
        exec_def, {.rd = b_vec, .rs1 = b_fp, .rs2 = b_vec, .rs3 = b_none}, mem_none, ctrl_none,
        side_serial);
    set_props({VFMV_S_F, VFMV_V_F}, exec_def,
              {.rd = b_vec, .rs1 = b_fp, .rs2 = b_none, .rs3 = b_none}, mem_none, ctrl_none,
              side_serial);
    set_props({VMV_V_V}, exec_def, {.rd = b_vec, .rs1 = b_vec, .rs2 = b_none, .rs3 = b_none},
              mem_none, ctrl_none, side_serial);
    set_props({VMV_V_X, VMV_S_X}, exec_def,
              {.rd = b_vec, .rs1 = b_int, .rs2 = b_none, .rs3 = b_none}, mem_none, ctrl_none,
              side_serial);
    set_props({VMV_V_I, VID_V}, exec_def,
              {.rd = b_vec, .rs1 = b_none, .rs2 = b_none, .rs3 = b_none}, mem_none, ctrl_none,
              side_serial);

    set_props({VMERGE_VVM,  VSBC_VVM,    VADC_VVM,      VMADC_VV,     VMADC_VVM,    VMSBC_VV,
               VMSBC_VVM,   VMAND_MM,    VMNAND_MM,     VMANDN_MM,    VMOR_MM,      VMNOR_MM,
               VMORN_MM,    VMXOR_MM,    VMXNOR_MM,     VCOMPRESS_VM, VWADD_WV,     VWADDU_WV,
               VWSUB_WV,    VWSUBU_WV,   VNCLIP_WV,     VNCLIPU_WV,   VNSRL_WV,     VNSRA_WV,
               VREDSUM_VS,  VREDAND_VS,  VREDOR_VS,     VREDXOR_VS,   VREDMINU_VS,  VREDMIN_VS,
               VREDMAXU_VS, VREDMAX_VS,  VWREDSUM_VS,   VWREDSUMU_VS, VFREDUSUM_VS, VFREDOSUM_VS,
               VFREDMIN_VS, VFREDMAX_VS, VFWREDUSUM_VS, VFWREDOSUM_VS},
              exec_def, {.rd = b_vec, .rs1 = b_vec, .rs2 = b_vec, .rs3 = b_none}, mem_none,
              ctrl_none, side_serial);

    set_props({VMERGE_VXM, VSBC_VXM, VADC_VXM, VMADC_VX, VMADC_VXM, VMSBC_VX, VMSBC_VXM}, exec_def,
              {.rd = b_vec, .rs1 = b_int, .rs2 = b_vec, .rs3 = b_none}, mem_none, ctrl_none,
              side_serial);

    set_props({VMERGE_VIM, VADC_VIM, VMADC_VI, VMADC_VIM}, exec_def,
              {.rd = b_vec, .rs1 = b_none, .rs2 = b_vec, .rs3 = b_none}, mem_none, ctrl_none,
              side_serial);

    // RVV unary encodings select the operation with rs1; their vector input is vs2.
    // Keep the physical operand banks accurate for scoreboard dependencies.
    set_props({VSEXT_VF2,        VSEXT_VF4,       VSEXT_VF8,        VZEXT_VF2,
               VZEXT_VF4,        VZEXT_VF8,       VCLZ_V,           VCTZ_V,
               VCPOP_V,          VBREV_V,         VBREV8_V,         VREV8_V,
               VFSQRT_V,         VFRSQRT7_V,      VFREC7_V,         VFCLASS_V,
               VFCVT_XU_F_V,     VFCVT_X_F_V,     VFCVT_F_XU_V,     VFCVT_F_X_V,
               VFCVT_RTZ_XU_F_V, VFCVT_RTZ_X_F_V, VFWCVT_XU_F_V,    VFWCVT_X_F_V,
               VFWCVT_F_XU_V,    VFWCVT_F_X_V,    VFWCVT_F_F_V,     VFWCVT_RTZ_XU_F_V,
               VFWCVT_RTZ_X_F_V, VFNCVT_XU_F_W,   VFNCVT_X_F_W,     VFNCVT_F_XU_W,
               VFNCVT_F_X_W,     VFNCVT_F_F_W,    VFNCVT_ROD_F_F_W, VFNCVT_RTZ_XU_F_W,
               VFNCVT_RTZ_X_F_W, VIOTA_M,         VMSBF_M,          VMSIF_M,
               VMSOF_M,          VMV1R_V,         VMV2R_V,          VMV4R_V,
               VMV8R_V},
              exec_def, {.rd = b_vec, .rs1 = b_none, .rs2 = b_vec, .rs3 = b_none}, mem_none,
              ctrl_none, side_serial);
    set_props({VMV_X_S, VFIRST_M, VCPOP_M}, exec_def,
              {.rd = b_int, .rs1 = b_none, .rs2 = b_vec, .rs3 = b_none}, mem_none, ctrl_none,
              side_serial);
    set_props({VFMV_F_S}, exec_def, {.rd = b_fp, .rs1 = b_none, .rs2 = b_vec, .rs3 = b_none},
              mem_none, ctrl_none, side_serial);

    set_props({CFU}, ExecutionClass::Cfu, {.rd = b_int, .rs1 = b_int, .rs2 = b_int, .rs3 = b_none});

    for (std::size_t i = 0; i < isa::kOperationIdCount; ++i) {
        const auto op = static_cast<isa::OperationId>(i);
        auto& entry = info[i];
        entry.required_misa = compute_operation_misa_mask(op);
        entry.rv64_only = compute_operation_rv64_only(op);
        entry.mem_access_bytes = compute_operation_mem_bytes(op);
        simrv::isa::Funct3 fixed_f3 = simrv::isa::Funct3::Lb;
        if (compute_operation_fixed_funct3(op, fixed_f3)) {
            entry.mem_funct3 = fixed_f3;
            entry.has_fixed_mem_funct3 = true;
        }
    }

    return info;
}

inline constexpr auto kOperationInfo = generate_operation_info();

[[nodiscard]] constexpr auto info(isa::OperationId operation) noexcept -> OperationInfo {
    const auto index = static_cast<std::size_t>(operation);
    return index < kOperationInfo.size() ? kOperationInfo[index] : OperationInfo{};
}

}  // namespace simrv::pipeline::operation
