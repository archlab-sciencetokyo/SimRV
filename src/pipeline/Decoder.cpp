#include "simrv/pipeline/Decoder.hpp"

#include <cstdio>
#include <utility>

#include "simrv/pipeline/DecodeTable.hpp"

namespace simrv::pipeline {

using simrv::isa::OperationId;
using enum simrv::isa::OperationId;

const std::array<std::string_view, static_cast<size_t>(isa::OperationIdCount)> OPERATION_NAME = {
    "LUI",
    "AUIPC",
    "JAL",
    "JALR",
    "BEQ",
    "BNE",
    "BLT",
    "BGE",
    "BLTU",
    "BGEU",
    "LB",
    "LH",
    "LW",
    "LD",
    "LBU",
    "LHU",
    "LWU",
    "SB",
    "SH",
    "SW",
    "SD",
    "ADDI",
    "SLTI",
    "SLTIU",
    "XORI",
    "ORI",
    "ANDI",
    "SLLI",
    "SRLI",
    "SRAI",
    "ADDIW",
    "SLLIW",
    "SRLIW",
    "SRAIW",
    "ADD",
    "SUB",
    "SLL",
    "SLT",
    "SLTU",
    "XOR",
    "SRL",
    "SRA",
    "OR",
    "AND",
    "ADDW",
    "SUBW",
    "SLLW",
    "SRLW",
    "SRAW",
    "FENCE",
    "FENCE_I",
    "ECALL",
    "EBREAK",
    "CSRRW",
    "CSRRS",
    "CSRRC",
    "CSRRWI",
    "CSRRSI",
    "CSRRCI",
    "URET",
    "SRET",
    "MRET",
    "WFI",
    "SFENCE_VMA",
    "MUL",
    "MULH",
    "MULHSU",
    "MULHU",
    "DIV",
    "DIVU",
    "REM",
    "REMU",
    "MULW",
    "DIVW",
    "DIVUW",
    "REMW",
    "REMUW",
    "LR_W",
    "SC_W",
    "AMOSWAP_W",
    "AMOADD_W",
    "AMOXOR_W",
    "AMOAND_W",
    "AMOOR_W",
    "AMOMIN_W",
    "AMOMAX_W",
    "AMOMINU_W",
    "AMOMAXU_W",
    "LR_D",
    "SC_D",
    "AMOSWAP_D",
    "AMOADD_D",
    "AMOXOR_D",
    "AMOAND_D",
    "AMOOR_D",
    "AMOMIN_D",
    "AMOMAX_D",
    "AMOMINU_D",
    "AMOMAXU_D",
    "FLW",
    "FSW",
    "FMADD_S",
    "FMSUB_S",
    "FNMADD_S",
    "FNMSUB_S",
    "FADD_S",
    "FSUB_S",
    "FMUL_S",
    "FDIV_S",
    "FSQRT_S",
    "FSGNJ_S",
    "FSGNJN_S",
    "FSGNJX_S",
    "FMIN_S",
    "FMAX_S",
    "FCVT_W_S",
    "FCVT_WU_S",
    "FMV_X_W",
    "FEQ_S",
    "FLT_S",
    "FLE_S",
    "FCLASS_S",
    "FCVT_S_W",
    "FCVT_S_WU",
    "FMV_W_X",
    "FCVT_L_S",
    "FCVT_LU_S",
    "FCVT_S_L",
    "FCVT_S_LU",
    "FLD",
    "FSD",
    "FMADD_D",
    "FMSUB_D",
    "FNMSUB_D",
    "FNMADD_D",
    "FADD_D",
    "FSUB_D",
    "FMUL_D",
    "FDIV_D",
    "FSQRT_D",
    "FSGNJ_D",
    "FSGNJN_D",
    "FSGNJX_D",
    "FMIN_D",
    "FMAX_D",
    "FCVT_S_D",
    "FCVT_D_S",
    "FEQ_D",
    "FLT_D",
    "FLE_D",
    "FCLASS_D",
    "FCVT_W_D",
    "FCVT_WU_D",
    "FCVT_D_W",
    "FCVT_D_WU",
    "FMV_X_D",
    "FMV_D_X",
    "FCVT_L_D",
    "FCVT_LU_D",
    "FCVT_D_L",
    "FCVT_D_LU",
    "SH1ADD",
    "SH2ADD",
    "SH3ADD",
    "SH1ADD.UW",
    "SH2ADD.UW",
    "SH3ADD.UW",
    "ADD.UW",
    "SLLI.UW",
    "ANDN",
    "ORN",
    "XNOR",
    "CLZ",
    "CTZ",
    "CPOP",
    "MIN",
    "MAX",
    "MINU",
    "MAXU",
    "SEXT.B",
    "SEXT.H",
    "ZEXT.H",
    "ROL",
    "ROR",
    "RORI",
    "CLZW",
    "CTZW",
    "CPOPW",
    "ROLW",
    "RORW",
    "RORIW",
    "CLMUL",
    "CLMULH",
    "CLMULR",
    "BSET",
    "BSETI",
    "BCLR",
    "BCLRI",
    "BINV",
    "BINVI",
    "BEXT",
    "BEXTI",
    "ORC.B",
    "REV8",
    "PACK",
    "PACKW",
    "VSETVLI",
    "VSETIVLI",
    "VSETVL",
    "VLE8_V",
    "VLE16_V",
    "VLE32_V",
    "VLE64_V",
    "VSE8_V",
    "VSE16_V",
    "VSE32_V",
    "VSE64_V",
    "VADD_VV",
    "VADD_VX",
    "VADD_VI",
    "VSUB_VV",
    "VSUB_VX",
    "VMUL_VV",
    "VMUL_VX",
    "VDIV_VV",
    "VDIV_VX",
    "VDIVU_VV",
    "VDIVU_VX",
    "VAND_VV",
    "VAND_VX",
    "VAND_VI",
    "VOR_VV",
    "VOR_VX",
    "VOR_VI",
    "VXOR_VV",
    "VXOR_VX",
    "VXOR_VI",
    "VSLL_VV",
    "VSLL_VX",
    "VSLL_VI",
    "VSRL_VV",
    "VSRL_VX",
    "VSRL_VI",
    "VSRA_VV",
    "VSRA_VX",
    "VSRA_VI",
    "VMV_V_V",
    "VMV_V_X",
    "VMV_V_I",
    "VMV_X_S",
    "VMV_S_X",
    "VMSEQ_VV",
    "VMSEQ_VX",
    "VMSEQ_VI",
    "VMSNE_VV",
    "VMSNE_VX",
    "VMSNE_VI",
    "VMSLT_VV",
    "VMSLT_VX",
    "VMSLTU_VV",
    "VMSLTU_VX",
    "VMSLE_VV",
    "VMSLE_VX",
    "VMSLE_VI",
    "VMSLEU_VV",
    "VMSLEU_VX",
    "VMSLEU_VI",
    "VMSGT_VX",
    "VMSGT_VI",
    "VMSGTU_VX",
    "VMSGTU_VI",
    "VMERGE_VVM",
    "VMERGE_VXM",
    "VMERGE_VIM",
    "VMACC_VV",
    "VMACC_VX",
    "VMADD_VV",
    "VMADD_VX",
    "VNMSAC_VV",
    "VNMSAC_VX",
    "VNSUB_VV",
    "VNSUB_VX",
    "VWMACCU_VV",
    "VWMACCU_VX",
    "VWMACC_VV",
    "VWMACC_VX",
    "VWMACCUS_VX",
    "VWMACCSU_VV",
    "VWMACCSU_VX",
    "VMIN_VV",
    "VMIN_VX",
    "VMINU_VV",
    "VMINU_VX",
    "VMAX_VV",
    "VMAX_VX",
    "VMAXU_VV",
    "VMAXU_VX",
    "VLSE8_V",
    "VLSE16_V",
    "VLSE32_V",
    "VLSE64_V",
    "VSSE8_V",
    "VSSE16_V",
    "VSSE32_V",
    "VSSE64_V",
    "VLUXEI8_V",
    "VLUXEI16_V",
    "VLUXEI32_V",
    "VLUXEI64_V",
    "VLOXEI8_V",
    "VLOXEI16_V",
    "VLOXEI32_V",
    "VLOXEI64_V",
    "VSUXEI8_V",
    "VSUXEI16_V",
    "VSUXEI32_V",
    "VSUXEI64_V",
    "VSOXEI8_V",
    "VSOXEI16_V",
    "VSOXEI32_V",
    "VSOXEI64_V",
    "VID_V",
    "VFMACC_VV",
    "VFMACC_VF",
    "VREDSUM_VS",
    "VWMUL_VV",
    "VWMUL_VX",
    "VNCLIP_WV",
    "VNCLIP_WX",
    "VNCLIP_WI",
    "VNCLIPU_WV",
    "VNCLIPU_WX",
    "VNCLIPU_WI",
    "VNSRL_WV",
    "VNSRL_WX",
    "VNSRL_WI",
    "VNSRA_WV",
    "VNSRA_WX",
    "VNSRA_WI",
    "VSLIDE1UP_VX",
    "VSLIDE1DOWN_VX",
    "VFADD_VV",
    "VFADD_VF",
    "VRSUB_VX",
    "VRSUB_VI",
    "VSADD_VV",
    "VSADD_VX",
    "VSADD_VI",
    "VSADDU_VV",
    "VSADDU_VX",
    "VSADDU_VI",
    "VSBC_VVM",
    "VSBC_VXM",
    "VSEXT_VF2",
    "VSEXT_VF4",
    "VSEXT_VF8",
    "VZEXT_VF2",
    "VZEXT_VF4",
    "VZEXT_VF8",
    "VSLIDEDOWN_VX",
    "VSLIDEDOWN_VI",
    "VSLIDEUP_VX",
    "VSLIDEUP_VI",
    "VSMUL_VV",
    "VSMUL_VX",
    "VSSRA_VV",
    "VSSRA_VX",
    "VSSRA_VI",
    "VSSRL_VV",
    "VSSRL_VX",
    "VSSRL_VI",
    "VSSUB_VV",
    "VSSUB_VX",
    "VSSUBU_VV",
    "VSSUBU_VX",
    "VWADD_VV",
    "VWADD_VX",
    "VWADD_WV",
    "VWADD_WX",
    "VWADDU_VV",
    "VWADDU_VX",
    "VWADDU_WV",
    "VWADDU_WX",
    "VWSUB_VV",
    "VWSUB_VX",
    "VWSUB_WV",
    "VWSUB_WX",
    "VWSUBU_VV",
    "VWSUBU_VX",
    "VWSUBU_WV",
    "VWSUBU_WX",
    "VWMULU_VV",
    "VWMULU_VX",
    "VWMULSU_VV",
    "VWMULSU_VX",
    "VWREDSUM_VS",
    "VWREDSUMU_VS",
    "VMV1R_V",
    "VMV2R_V",
    "VMV4R_V",
    "VMV8R_V",
    "VL1RE8_V",
    "VL1RE16_V",
    "VL1RE32_V",
    "VL1RE64_V",
    "VL2RE8_V",
    "VL2RE16_V",
    "VL2RE32_V",
    "VL2RE64_V",
    "VL4RE8_V",
    "VL4RE16_V",
    "VL4RE32_V",
    "VL4RE64_V",
    "VL8RE8_V",
    "VL8RE16_V",
    "VL8RE32_V",
    "VL8RE64_V",
    "VS1R_V",
    "VS2R_V",
    "VS4R_V",
    "VS8R_V",
    "VADC_VVM",
    "VADC_VXM",
    "VADC_VIM",
    "VMADC_VV",
    "VMADC_VX",
    "VMADC_VI",
    "VMADC_VVM",
    "VMADC_VXM",
    "VMADC_VIM",
    "VMSBC_VV",
    "VMSBC_VX",
    "VMSBC_VVM",
    "VMSBC_VXM",
    "VMSBF_M",
    "VMSIF_M",
    "VMSOF_M",
    "VFIRST_M",
    "VCPOP_M",
    "VIOTA_M",
    "VCOMPRESS_VM",
    "VANDN_VV",
    "VANDN_VX",
    "VROL_VV",
    "VROL_VX",
    "VROR_VV",
    "VROR_VX",
    "VROR_VI",
    "VCLZ_V",
    "VCTZ_V",
    "VCPOP_V",
    "VBREV_V",
    "VBREV8_V",
    "VREV8_V",
    "VCLMUL_VV",
    "VCLMUL_VX",
    "VCLMULH_VV",
    "VCLMULH_VX",
    "VMAND_MM",
    "VMNAND_MM",
    "VMANDN_MM",
    "VMOR_MM",
    "VMNOR_MM",
    "VMORN_MM",
    "VMXOR_MM",
    "VMXNOR_MM",
    "VFMV_F_S",
    "VFMV_S_F",
    "VFMERGE_VFM",
    "VWSLL_VV",
    "VWSLL_VX",
    "VWSLL_VI",
    "VCHECK",
    "UNKNOWN"};

auto operation_name(isa::OperationId operation) noexcept -> std::string_view {
    const auto index = static_cast<std::size_t>(operation);
    return index < OPERATION_NAME.size() ? OPERATION_NAME[index] : "UNKNOWN";
}

namespace {

constexpr std::array<OperationId, 8> kBranchTable = {
    OperationId::BEQ, OperationId::BNE, OperationId::UNKNOWN, OperationId::UNKNOWN,
    OperationId::BLT, OperationId::BGE, OperationId::BLTU,    OperationId::BGEU};

constexpr std::array<OperationId, 8> kLoadTable = {
    OperationId::LB,  OperationId::LH,  OperationId::LW,  OperationId::LD,
    OperationId::LBU, OperationId::LHU, OperationId::LWU, OperationId::UNKNOWN};

constexpr std::array<OperationId, 8> kStoreTable = {
    OperationId::SB,      OperationId::SH,      OperationId::SW,      OperationId::SD,
    OperationId::UNKNOWN, OperationId::UNKNOWN, OperationId::UNKNOWN, OperationId::UNKNOWN};

[[nodiscard]] constexpr auto decode_branch(uint32_t funct3) noexcept -> OperationId {
    return funct3 < kBranchTable.size() ? kBranchTable[funct3] : OperationId::UNKNOWN;
}

[[nodiscard]] constexpr auto decode_load(uint32_t funct3) noexcept -> OperationId {
    return funct3 < kLoadTable.size() ? kLoadTable[funct3] : OperationId::UNKNOWN;
}

[[nodiscard]] constexpr auto decode_store(uint32_t funct3) noexcept -> OperationId {
    return funct3 < kStoreTable.size() ? kStoreTable[funct3] : OperationId::UNKNOWN;
}

constexpr std::array<OperationId, 8> kOpImmBaseTable = {
    OperationId::ADDI, OperationId::UNKNOWN, OperationId::SLTI, OperationId::SLTIU,
    OperationId::XORI, OperationId::UNKNOWN, OperationId::ORI,  OperationId::ANDI,
};

[[nodiscard]] constexpr auto decode_op_imm(uint32_t funct3, uint32_t funct7,
                                           Instruction ir) noexcept -> OperationId {
    if (funct3 < kOpImmBaseTable.size() && kOpImmBaseTable[funct3] != OperationId::UNKNOWN) {
        return kOpImmBaseTable[funct3];
    }
    const uint32_t imm12 = ir >> 20;
    const uint32_t imm6 = imm12 >> 6;
    if (funct3 == 1) {
        if (imm12 == 0x600) return OperationId::CLZ;
        if (imm12 == 0x601) return OperationId::CTZ;
        if (imm12 == 0x602) return OperationId::CPOP;
        if (imm12 == 0x604) return OperationId::SEXT_B;
        if (imm12 == 0x605) return OperationId::SEXT_H;
        if (imm6 == 0x0A) return OperationId::BSETI;
        if (imm6 == 0x12) return OperationId::BCLRI;
        if (imm6 == 0x1A) return OperationId::BINVI;
        if (funct7 == 0x00 || funct7 == 0x01) return OperationId::SLLI;
        return OperationId::UNKNOWN;
    }
    if (funct3 == 5) {
        if (imm12 == 0x287) return OperationId::ORC_B;
        if (imm12 == 0x698 || imm12 == 0x6b8) return OperationId::REV8;
        if (imm6 == 0x18) return OperationId::RORI;
        if (imm6 == 0x12) return OperationId::BEXTI;
        if (funct7 == 0x00 || funct7 == 0x01) return OperationId::SRLI;
        if (funct7 == 0x20 || funct7 == 0x21) return OperationId::SRAI;
        return OperationId::UNKNOWN;
    }
    return OperationId::UNKNOWN;
}

consteval auto generate_op_standard_table() {
    DecodeTable<OperationId, 8, 128, OperationId::UNKNOWN> table;
    // funct3 = 0
    table.assign(0, 0x00, OperationId::ADD);
    table.assign(0, 0x20, OperationId::SUB);

    // funct3 = 1
    table.assign(1, 0x00, OperationId::SLL);
    table.assign(1, 0x05, OperationId::CLMUL);
    table.assign(1, 0x14, OperationId::BSET);
    table.assign(1, 0x24, OperationId::BCLR);
    table.assign(1, 0x30, OperationId::ROL);
    table.assign(1, 0x34, OperationId::BINV);

    // funct3 = 2
    table.assign(2, 0x00, OperationId::SLT);
    table.assign(2, 0x05, OperationId::CLMULR);
    table.assign(2, 0x10, OperationId::SH1ADD);

    // funct3 = 3
    table.assign(3, 0x00, OperationId::SLTU);
    table.assign(3, 0x05, OperationId::CLMULH);

    // funct3 = 4
    table.assign(4, 0x00, OperationId::XOR);
    table.assign(4, 0x04, OperationId::PACK);
    table.assign(4, 0x05, OperationId::MIN);
    table.assign(4, 0x10, OperationId::SH2ADD);
    table.assign(4, 0x20, OperationId::XNOR);

    // funct3 = 5
    table.assign(5, 0x00, OperationId::SRL);
    table.assign(5, 0x05, OperationId::MINU);
    table.assign(5, 0x20, OperationId::SRA);
    table.assign(5, 0x24, OperationId::BEXT);
    table.assign(5, 0x30, OperationId::ROR);

    // funct3 = 6
    table.assign(6, 0x00, OperationId::OR);
    table.assign(6, 0x05, OperationId::MAX);
    table.assign(6, 0x10, OperationId::SH3ADD);
    table.assign(6, 0x20, OperationId::ORN);

    // funct3 = 7
    table.assign(7, 0x00, OperationId::AND);
    table.assign(7, 0x05, OperationId::MAXU);
    table.assign(7, 0x20, OperationId::ANDN);

    return table;
}

constexpr auto kOpStandardTable = generate_op_standard_table();

[[nodiscard]] constexpr auto decode_op_standard(uint32_t funct3, uint32_t funct7) noexcept
    -> OperationId {
    if (funct3 < 8 && funct7 < 128) {
        return kOpStandardTable.lookup(funct3, funct7);
    }
    return OperationId::UNKNOWN;
}

auto decode_op_imm32(uint32_t funct3, uint32_t funct7, Instruction ir) -> OperationId {
    const uint32_t imm12 = ir >> 20;
    const uint32_t imm6 = imm12 >> 6;
    switch (funct3) {
        case 0:
            return OperationId::ADDIW;
        case 1:
            if (imm12 == 0x600) return OperationId::CLZW;
            if (imm12 == 0x601) return OperationId::CTZW;
            if (imm12 == 0x602) return OperationId::CPOPW;
            if (imm6 == 0x02) return OperationId::SLLI_UW;
            if (funct7 == 0x00) return OperationId::SLLIW;
            return OperationId::UNKNOWN;
        case 5:
            if (imm6 == 0x18) return OperationId::RORIW;
            if (funct7 == 0x00) return OperationId::SRLIW;
            if (funct7 == 0x20) return OperationId::SRAIW;
            return OperationId::UNKNOWN;
        default:
            return OperationId::UNKNOWN;
    }
}

consteval auto generate_op32_standard_table() {
    DecodeTable<OperationId, 8, 128, OperationId::UNKNOWN> table;
    // funct3 = 0
    table.assign(0, 0x00, OperationId::ADDW);
    table.assign(0, 0x04, OperationId::ADD_UW);
    table.assign(0, 0x20, OperationId::SUBW);

    // funct3 = 1
    table.assign(1, 0x00, OperationId::SLLW);
    table.assign(1, 0x30, OperationId::ROLW);

    // funct3 = 2
    table.assign(2, 0x10, OperationId::SH1ADD_UW);

    // funct3 = 4
    table.assign(4, 0x04, OperationId::PACKW);
    table.assign(4, 0x10, OperationId::SH2ADD_UW);

    // funct3 = 5
    table.assign(5, 0x00, OperationId::SRLW);
    table.assign(5, 0x20, OperationId::SRAW);
    table.assign(5, 0x30, OperationId::RORW);

    // funct3 = 6
    table.assign(6, 0x10, OperationId::SH3ADD_UW);

    return table;
}

constexpr auto kOp32StandardTable = generate_op32_standard_table();

[[nodiscard]] constexpr auto decode_op32_standard(uint32_t funct3, uint32_t funct7) noexcept
    -> OperationId {
    if (funct3 < 8 && funct7 < 128) {
        return kOp32StandardTable.lookup(funct3, funct7);
    }
    return OperationId::UNKNOWN;
}

auto decode_system_priv(uint32_t funct7, Instruction ir) -> OperationId {
    const uint32_t f12 = ir >> 20;
    const bool rd_is_zero = ((ir >> 7U) & 0x1FU) == 0;
    const bool rs1_is_zero = ((ir >> 15U) & 0x1FU) == 0;
    switch (f12) {
        case 0x000:
            return (rd_is_zero && rs1_is_zero) ? OperationId::ECALL : OperationId::UNKNOWN;
        case 0x001:
            return (rd_is_zero && rs1_is_zero) ? OperationId::EBREAK : OperationId::UNKNOWN;
        case 0x002:
            // URET belonged to the abandoned draft N extension.
            return OperationId::UNKNOWN;
        case 0x102:
            return (rd_is_zero && rs1_is_zero) ? OperationId::SRET : OperationId::UNKNOWN;
        case 0x302:
            return (rd_is_zero && rs1_is_zero) ? OperationId::MRET : OperationId::UNKNOWN;
        case 0x105:
            return (rd_is_zero && rs1_is_zero) ? OperationId::WFI : OperationId::UNKNOWN;
        default:
            // SFENCE.VMA uses rs1/rs2 operands but reserves rd=x0.
            if (funct7 == 0x09 && rd_is_zero) return OperationId::SFENCE_VMA;
            break;
    }
    return OperationId::UNKNOWN;
}

constexpr std::array<OperationId, 8> kSystemCsrTable = {
    OperationId::UNKNOWN, OperationId::CSRRW,  OperationId::CSRRS,  OperationId::CSRRC,
    OperationId::UNKNOWN, OperationId::CSRRWI, OperationId::CSRRSI, OperationId::CSRRCI,
};

[[nodiscard]] constexpr auto decode_system_csr(uint32_t funct3) noexcept -> OperationId {
    return funct3 < kSystemCsrTable.size() ? kSystemCsrTable[funct3] : OperationId::UNKNOWN;
}

auto decode_system(uint32_t funct3, uint32_t funct7, Instruction ir) -> OperationId {
    if (funct3 == 0) {
        return decode_system_priv(funct7, ir);
    }
    return decode_system_csr(funct3);
}

constexpr std::array<OperationId, 8> kMiscMemTable = {
    OperationId::FENCE,   OperationId::FENCE_I, OperationId::UNKNOWN, OperationId::UNKNOWN,
    OperationId::UNKNOWN, OperationId::UNKNOWN, OperationId::UNKNOWN, OperationId::UNKNOWN,
};

[[nodiscard]] constexpr auto decode_misc_mem(uint32_t funct3) noexcept -> OperationId {
    return funct3 < kMiscMemTable.size() ? kMiscMemTable[funct3] : OperationId::UNKNOWN;
}

auto decode_ext_i(Opcode op, uint32_t funct3, uint32_t funct7, Instruction ir) -> OperationId {
    switch (op) {
        case Opcode::Lui:
            return OperationId::LUI;
        case Opcode::Auipc:
            return OperationId::AUIPC;
        case Opcode::Jal:
            return OperationId::JAL;
        case Opcode::Jalr:
            return OperationId::JALR;
        case Opcode::Branch:
            return decode_branch(funct3);
        case Opcode::Load:
            return decode_load(funct3);
        case Opcode::Store:
            return decode_store(funct3);
        case Opcode::OpImm:
            return decode_op_imm(funct3, funct7, ir);
        case Opcode::OpImm32:
            return decode_op_imm32(funct3, funct7, ir);
        case Opcode::Op:
            return decode_op_standard(funct3, funct7);
        case Opcode::Op32:
            return decode_op32_standard(funct3, funct7);
        case Opcode::MiscMem:
            return decode_misc_mem(funct3);
        case Opcode::System:
            return decode_system(funct3, funct7, ir);
        default:
            return OperationId::UNKNOWN;
    }
}

constexpr std::array<OperationId, 8> kMTable = {
    OperationId::MUL, OperationId::MULH, OperationId::MULHSU, OperationId::MULHU,
    OperationId::DIV, OperationId::DIVU, OperationId::REM,    OperationId::REMU};

constexpr std::array<OperationId, 8> kMTable32 = {
    OperationId::MULW, OperationId::UNKNOWN, OperationId::UNKNOWN, OperationId::UNKNOWN,
    OperationId::DIVW, OperationId::DIVUW,   OperationId::REMW,    OperationId::REMUW};

[[nodiscard]] constexpr auto decode_ext_m_op(uint32_t funct3) noexcept -> OperationId {
    return funct3 < kMTable.size() ? kMTable[funct3] : OperationId::UNKNOWN;
}

[[nodiscard]] constexpr auto decode_ext_m_op32(uint32_t funct3) noexcept -> OperationId {
    return funct3 < kMTable32.size() ? kMTable32[funct3] : OperationId::UNKNOWN;
}

[[nodiscard]] constexpr auto decode_ext_m(Opcode op, uint32_t funct3) noexcept -> OperationId {
    if (op == Opcode::Op32) {
        return decode_ext_m_op32(funct3);
    }
    return decode_ext_m_op(funct3);
}

consteval auto generate_amo_table() {
    DecodeTable<OperationId, 2, 32, OperationId::UNKNOWN> table;
    // funct3 == 2 (32-bit AMO*W)
    table.assign(0, 0x00, OperationId::AMOADD_W);
    table.assign(0, 0x01, OperationId::AMOSWAP_W);
    table.assign(0, 0x02, OperationId::LR_W);
    table.assign(0, 0x03, OperationId::SC_W);
    table.assign(0, 0x04, OperationId::AMOXOR_W);
    table.assign(0, 0x08, OperationId::AMOOR_W);
    table.assign(0, 0x0C, OperationId::AMOAND_W);
    table.assign(0, 0x10, OperationId::AMOMIN_W);
    table.assign(0, 0x14, OperationId::AMOMAX_W);
    table.assign(0, 0x18, OperationId::AMOMINU_W);
    table.assign(0, 0x1C, OperationId::AMOMAXU_W);

    // funct3 == 3 (64-bit AMO*D)
    table.assign(1, 0x00, OperationId::AMOADD_D);
    table.assign(1, 0x01, OperationId::AMOSWAP_D);
    table.assign(1, 0x02, OperationId::LR_D);
    table.assign(1, 0x03, OperationId::SC_D);
    table.assign(1, 0x04, OperationId::AMOXOR_D);
    table.assign(1, 0x08, OperationId::AMOOR_D);
    table.assign(1, 0x0C, OperationId::AMOAND_D);
    table.assign(1, 0x10, OperationId::AMOMIN_D);
    table.assign(1, 0x14, OperationId::AMOMAX_D);
    table.assign(1, 0x18, OperationId::AMOMINU_D);
    table.assign(1, 0x1C, OperationId::AMOMAXU_D);

    return table;
}

constexpr auto kAmoTable = generate_amo_table();

[[nodiscard]] constexpr auto decode_ext_a(uint32_t funct7, uint32_t funct3, uint32_t rs2) noexcept
    -> OperationId {
    if (!isa::amo_width_supported(static_cast<isa::Funct3>(funct3))) {
        return OperationId::UNKNOWN;
    }
    const uint32_t funct5 = funct7 >> 2;
    // LR.W/LR.D reserve rs2=0; nonzero rs2 is a reserved instruction encoding.
    if (funct5 == enum_mask(isa::Funct5Amo::Lr) && rs2 != 0) {
        return OperationId::UNKNOWN;
    }
    if (funct5 < 32) {
        return kAmoTable.lookup(funct3 - 2, funct5);
    }
    return OperationId::UNKNOWN;
}

constexpr std::array<std::array<OperationId, 2>, 4> kFmaTable = {{
    {OperationId::FMADD_S, OperationId::FMADD_D},
    {OperationId::FMSUB_S, OperationId::FMSUB_D},
    {OperationId::FNMSUB_S, OperationId::FNMSUB_D},
    {OperationId::FNMADD_S, OperationId::FNMADD_D},
}};

[[nodiscard]] constexpr auto decode_fma(Opcode op, uint32_t funct7) noexcept -> OperationId {
    const uint32_t fmt = funct7 & 0x03;
    if (fmt > 1) {
        return OperationId::UNKNOWN;
    }
    size_t op_idx = 0;
    switch (op) {
        case Opcode::MAdd:
            op_idx = 0;
            break;
        case Opcode::MSub:
            op_idx = 1;
            break;
        case Opcode::NMSub:
            op_idx = 2;
            break;
        case Opcode::NMAdd:
            op_idx = 3;
            break;
        default:
            return OperationId::UNKNOWN;
    }
    return kFmaTable[op_idx][fmt];
}

consteval auto generate_fp_single_table() {
    DecodeTable<OperationId, 32, 8, OperationId::UNKNOWN> table;
    table.assign_row(0x00, OperationId::FADD_S);
    table.assign_row(0x01, OperationId::FSUB_S);
    table.assign_row(0x02, OperationId::FMUL_S);
    table.assign_row(0x03, OperationId::FDIV_S);
    table.assign(0x04, 0, OperationId::FSGNJ_S);
    table.assign(0x04, 1, OperationId::FSGNJN_S);
    table.assign(0x04, 2, OperationId::FSGNJX_S);
    table.assign(0x05, 0, OperationId::FMIN_S);
    table.assign(0x05, 1, OperationId::FMAX_S);
    table.assign_row(0x0B, OperationId::FSQRT_S);
    table.assign(0x14, 0, OperationId::FLE_S);
    table.assign(0x14, 1, OperationId::FLT_S);
    table.assign(0x14, 2, OperationId::FEQ_S);
    table.assign(0x1C, 0, OperationId::FMV_X_W);
    table.assign(0x1C, 1, OperationId::FCLASS_S);
    table.assign(0x1E, 0, OperationId::FMV_W_X);
    return table;
}

constexpr auto kFpSingleTable = generate_fp_single_table();

auto decode_op_fp_single_fcvt(uint32_t f5, uint32_t rs2) -> OperationId {
    switch (f5) {
        case 0x08:
            return (rs2 == 1) ? OperationId::FCVT_S_D : OperationId::UNKNOWN;
        case 0x18:
            if (rs2 == 0) return OperationId::FCVT_W_S;
            if (rs2 == 1) return OperationId::FCVT_WU_S;
            if (rs2 == 2) return OperationId::FCVT_L_S;
            if (rs2 == 3) return OperationId::FCVT_LU_S;
            break;
        case 0x1A:
            if (rs2 == 0) return OperationId::FCVT_S_W;
            if (rs2 == 1) return OperationId::FCVT_S_WU;
            if (rs2 == 2) return OperationId::FCVT_S_L;
            if (rs2 == 3) return OperationId::FCVT_S_LU;
            break;
        default:
            break;
    }
    return OperationId::UNKNOWN;
}

auto decode_op_fp_single(uint32_t funct3, uint32_t f5, uint32_t rs2) -> OperationId {
    if (f5 == 0x08 || f5 == 0x18 || f5 == 0x1A) {
        return decode_op_fp_single_fcvt(f5, rs2);
    }
    if ((f5 == 0x0B || f5 == 0x1C || f5 == 0x1E) && rs2 != 0) {
        return OperationId::UNKNOWN;
    }
    return kFpSingleTable.lookup(f5, funct3);
}

consteval auto generate_fp_double_table() {
    DecodeTable<OperationId, 32, 8, OperationId::UNKNOWN> table;
    table.assign_row(0x00, OperationId::FADD_D);
    table.assign_row(0x01, OperationId::FSUB_D);
    table.assign_row(0x02, OperationId::FMUL_D);
    table.assign_row(0x03, OperationId::FDIV_D);
    table.assign(0x04, 0, OperationId::FSGNJ_D);
    table.assign(0x04, 1, OperationId::FSGNJN_D);
    table.assign(0x04, 2, OperationId::FSGNJX_D);
    table.assign(0x05, 0, OperationId::FMIN_D);
    table.assign(0x05, 1, OperationId::FMAX_D);
    table.assign_row(0x0B, OperationId::FSQRT_D);
    table.assign(0x14, 0, OperationId::FLE_D);
    table.assign(0x14, 1, OperationId::FLT_D);
    table.assign(0x14, 2, OperationId::FEQ_D);
    table.assign(0x1C, 0, OperationId::FMV_X_D);
    table.assign(0x1C, 1, OperationId::FCLASS_D);
    table.assign(0x1E, 0, OperationId::FMV_D_X);
    return table;
}

constexpr auto kFpDoubleTable = generate_fp_double_table();

auto decode_op_fp_double_fcvt(uint32_t funct3, uint32_t f5, uint32_t rs2) -> OperationId {
    switch (f5) {
        case 0x08:
            // Widening FCVT.D.S is exact and reserves the rm field as zero.
            return (rs2 == 0 && funct3 == 0) ? OperationId::FCVT_D_S : OperationId::UNKNOWN;
        case 0x18:
            if (rs2 == 0) return OperationId::FCVT_W_D;
            if (rs2 == 1) return OperationId::FCVT_WU_D;
            if (rs2 == 2) return OperationId::FCVT_L_D;
            if (rs2 == 3) return OperationId::FCVT_LU_D;
            break;
        case 0x1A:
            if (rs2 == 0) return OperationId::FCVT_D_W;
            if (rs2 == 1) return OperationId::FCVT_D_WU;
            if (rs2 == 2) return OperationId::FCVT_D_L;
            if (rs2 == 3) return OperationId::FCVT_D_LU;
            break;
        default:
            break;
    }
    return OperationId::UNKNOWN;
}

auto decode_op_fp_double(uint32_t funct3, uint32_t f5, uint32_t rs2) -> OperationId {
    if (f5 == 0x08 || f5 == 0x18 || f5 == 0x1A) {
        return decode_op_fp_double_fcvt(funct3, f5, rs2);
    }
    if ((f5 == 0x0B || f5 == 0x1C || f5 == 0x1E) && rs2 != 0) {
        return OperationId::UNKNOWN;
    }
    return kFpDoubleTable.lookup(f5, funct3);
}

auto decode_op_fp(uint32_t funct3, uint32_t funct7, uint32_t rs2) -> OperationId {
    const uint32_t fmt = funct7 & 0x03;
    const uint32_t f5 = funct7 >> 2;
    if (fmt == 0) {
        return decode_op_fp_single(funct3, f5, rs2);
    }
    if (fmt == 1) {
        return decode_op_fp_double(funct3, f5, rs2);
    }
    return OperationId::UNKNOWN;
}

namespace {

consteval auto generate_vector_common_table() {
    DecodeTable<OperationId, 64, 8, OperationId::UNKNOWN> table;
    table.assign_row(0x00, {{0, OperationId::VADD_VV},
                            {4, OperationId::VADD_VX},
                            {3, OperationId::VADD_VI},
                            {1, OperationId::VFADD_VV},
                            {5, OperationId::VFADD_VF},
                            {2, OperationId::VREDSUM_VS}});
    table.assign_row(0x01, {{0, OperationId::VANDN_VV}, {4, OperationId::VANDN_VX}});
    table.assign_row(0x02, {{0, OperationId::VSUB_VV}, {4, OperationId::VSUB_VX}});
    table.assign_row(0x03, {{4, OperationId::VRSUB_VX}, {3, OperationId::VRSUB_VI}});
    table.assign_row(0x04, {{0, OperationId::VMINU_VV}, {4, OperationId::VMINU_VX}});
    table.assign_row(0x05, {{0, OperationId::VMIN_VV}, {4, OperationId::VMIN_VX}});
    table.assign_row(0x06, {{0, OperationId::VMAXU_VV}, {4, OperationId::VMAXU_VX}});
    table.assign_row(0x07, {{0, OperationId::VMAX_VV}, {4, OperationId::VMAX_VX}});
    table.assign_row(
        0x09, {{0, OperationId::VAND_VV}, {4, OperationId::VAND_VX}, {3, OperationId::VAND_VI}});
    table.assign_row(
        0x0A, {{0, OperationId::VOR_VV}, {4, OperationId::VOR_VX}, {3, OperationId::VOR_VI}});
    table.assign_row(
        0x0B, {{0, OperationId::VXOR_VV}, {4, OperationId::VXOR_VX}, {3, OperationId::VXOR_VI}});
    table.assign_row(0x0C, {{2, OperationId::VCLMUL_VV}, {6, OperationId::VCLMUL_VX}});
    table.assign_row(0x0D, {{2, OperationId::VCLMULH_VV}, {6, OperationId::VCLMULH_VX}});
    table.assign_row(0x0E, {{6, OperationId::VSLIDE1UP_VX},
                            {4, OperationId::VSLIDEUP_VX},
                            {3, OperationId::VSLIDEUP_VI}});
    table.assign_row(0x0F, {{6, OperationId::VSLIDE1DOWN_VX},
                            {4, OperationId::VSLIDEDOWN_VX},
                            {3, OperationId::VSLIDEDOWN_VI}});
    table.assign(0x10, 6, OperationId::VMV_S_X);
    table.assign_row(0x12, {{0, OperationId::VSBC_VVM}, {4, OperationId::VSBC_VXM}});
    table.assign_row(
        0x14, {{0, OperationId::VROR_VV}, {4, OperationId::VROR_VX}, {3, OperationId::VROR_VI}});
    table.assign_row(0x15, {{0, OperationId::VROL_VV}, {4, OperationId::VROL_VX}});
    table.assign_row(0x18, {{0, OperationId::VMSEQ_VV},
                            {4, OperationId::VMSEQ_VX},
                            {3, OperationId::VMSEQ_VI},
                            {2, OperationId::VMNAND_MM}});
    table.assign_row(0x19, {{0, OperationId::VMSNE_VV},
                            {4, OperationId::VMSNE_VX},
                            {3, OperationId::VMSNE_VI},
                            {2, OperationId::VMAND_MM}});
    table.assign_row(
        0x1A,
        {{0, OperationId::VMSLTU_VV}, {4, OperationId::VMSLTU_VX}, {2, OperationId::VMANDN_MM}});
    table.assign_row(
        0x1B, {{0, OperationId::VMSLT_VV}, {4, OperationId::VMSLT_VX}, {2, OperationId::VMXOR_MM}});
    table.assign_row(0x1C, {{0, OperationId::VMSLEU_VV},
                            {4, OperationId::VMSLEU_VX},
                            {3, OperationId::VMSLEU_VI},
                            {2, OperationId::VMOR_MM}});
    table.assign_row(0x1D, {{0, OperationId::VMSLE_VV},
                            {4, OperationId::VMSLE_VX},
                            {3, OperationId::VMSLE_VI},
                            {2, OperationId::VMNOR_MM}});
    table.assign_row(
        0x1E,
        {{4, OperationId::VMSGTU_VX}, {3, OperationId::VMSGTU_VI}, {2, OperationId::VMORN_MM}});
    table.assign_row(
        0x1F,
        {{4, OperationId::VMSGT_VX}, {3, OperationId::VMSGT_VI}, {2, OperationId::VMXNOR_MM}});
    table.assign_row(0x20, {{2, OperationId::VDIVU_VV},
                            {6, OperationId::VDIVU_VX},
                            {0, OperationId::VSADDU_VV},
                            {4, OperationId::VSADDU_VX},
                            {3, OperationId::VSADDU_VI}});
    table.assign_row(0x21, {{2, OperationId::VDIV_VV},
                            {6, OperationId::VDIV_VX},
                            {0, OperationId::VSADD_VV},
                            {4, OperationId::VSADD_VX},
                            {3, OperationId::VSADD_VI}});
    table.assign_row(0x22, {{0, OperationId::VSSUBU_VV}, {4, OperationId::VSSUBU_VX}});
    table.assign_row(0x23, {{0, OperationId::VSSUB_VV}, {4, OperationId::VSSUB_VX}});
    table.assign_row(0x25, {{0, OperationId::VSLL_VV},
                            {4, OperationId::VSLL_VX},
                            {3, OperationId::VSLL_VI},
                            {2, OperationId::VMUL_VV},
                            {6, OperationId::VMUL_VX}});
    table.assign_row(0x27, {{0, OperationId::VSMUL_VV}, {4, OperationId::VSMUL_VX}});
    table.assign_row(
        0x28, {{0, OperationId::VSRL_VV}, {4, OperationId::VSRL_VX}, {3, OperationId::VSRL_VI}});
    table.assign_row(0x29, {{0, OperationId::VSRA_VV},
                            {4, OperationId::VSRA_VX},
                            {3, OperationId::VSRA_VI},
                            {2, OperationId::VMADD_VV},
                            {6, OperationId::VMADD_VX}});
    table.assign_row(
        0x2A, {{0, OperationId::VSSRL_VV}, {4, OperationId::VSSRL_VX}, {3, OperationId::VSSRL_VI}});
    table.assign_row(0x2B, {{2, OperationId::VNSUB_VV},
                            {6, OperationId::VNSUB_VX},
                            {0, OperationId::VSSRA_VV},
                            {4, OperationId::VSSRA_VX},
                            {3, OperationId::VSSRA_VI}});
    table.assign_row(0x2C, {{0, OperationId::VNSRL_WV},
                            {4, OperationId::VNSRL_WX},
                            {3, OperationId::VNSRL_WI},
                            {1, OperationId::VFMACC_VV},
                            {5, OperationId::VFMACC_VF}});
    table.assign_row(0x2D, {{0, OperationId::VNSRA_WV},
                            {4, OperationId::VNSRA_WX},
                            {3, OperationId::VNSRA_WI},
                            {2, OperationId::VMACC_VV},
                            {6, OperationId::VMACC_VX}});
    table.assign_row(
        0x2E,
        {{0, OperationId::VNCLIPU_WV}, {4, OperationId::VNCLIPU_WX}, {3, OperationId::VNCLIPU_WI}});
    table.assign_row(0x2F, {{2, OperationId::VNMSAC_VV},
                            {6, OperationId::VNMSAC_VX},
                            {0, OperationId::VNCLIP_WV},
                            {4, OperationId::VNCLIP_WX},
                            {3, OperationId::VNCLIP_WI}});
    table.assign_row(
        0x30,
        {{2, OperationId::VWADDU_VV}, {6, OperationId::VWADDU_VX}, {0, OperationId::VWREDSUMU_VS}});
    table.assign_row(
        0x31,
        {{2, OperationId::VWADD_VV}, {6, OperationId::VWADD_VX}, {0, OperationId::VWREDSUM_VS}});
    table.assign_row(0x32, {{2, OperationId::VWSUBU_VV}, {6, OperationId::VWSUBU_VX}});
    table.assign_row(0x33, {{2, OperationId::VWSUB_VV}, {6, OperationId::VWSUB_VX}});
    table.assign_row(0x34, {{2, OperationId::VWADDU_WV}, {6, OperationId::VWADDU_WX}});
    table.assign_row(0x35, {{2, OperationId::VWADD_WV},
                            {6, OperationId::VWADD_WX},
                            {0, OperationId::VWSLL_VV},
                            {4, OperationId::VWSLL_VX},
                            {3, OperationId::VWSLL_VI}});
    table.assign_row(0x36, {{2, OperationId::VWSUBU_WV}, {6, OperationId::VWSUBU_WX}});
    table.assign_row(0x37, {{2, OperationId::VWSUB_WV}, {6, OperationId::VWSUB_WX}});
    table.assign_row(0x38, {{2, OperationId::VWMULU_VV}, {6, OperationId::VWMULU_VX}});
    table.assign_row(0x3A, {{2, OperationId::VWMULSU_VV}, {6, OperationId::VWMULSU_VX}});
    table.assign_row(0x3B, {{2, OperationId::VWMUL_VV}, {6, OperationId::VWMUL_VX}});
    table.assign_row(0x3C, {{2, OperationId::VWMACCU_VV}, {6, OperationId::VWMACCU_VX}});
    table.assign_row(0x3D, {{2, OperationId::VWMACC_VV}, {6, OperationId::VWMACC_VX}});
    table.assign(0x3E, 6, OperationId::VWMACCUS_VX);
    table.assign_row(0x3F, {{2, OperationId::VWMACCSU_VV}, {6, OperationId::VWMACCSU_VX}});
    return table;
}

consteval auto generate_vector_mask_table(bool vm) {
    DecodeTable<OperationId, 64, 8, OperationId::UNKNOWN> table;
    if (vm) {
        table.assign_row(
            0x11,
            {{0, OperationId::VMADC_VV}, {4, OperationId::VMADC_VX}, {3, OperationId::VMADC_VI}});
        table.assign_row(0x13, {{0, OperationId::VMSBC_VV}, {4, OperationId::VMSBC_VX}});
        table.assign_row(0x17, {{0, OperationId::VMV_V_V},
                                {4, OperationId::VMV_V_X},
                                {3, OperationId::VMV_V_I},
                                {2, OperationId::VCOMPRESS_VM}});
    } else {
        table.assign_row(
            0x10,
            {{0, OperationId::VADC_VVM}, {4, OperationId::VADC_VXM}, {3, OperationId::VADC_VIM}});
        table.assign_row(0x11, {{0, OperationId::VMADC_VVM},
                                {4, OperationId::VMADC_VXM},
                                {3, OperationId::VMADC_VIM}});
        table.assign_row(0x13, {{0, OperationId::VMSBC_VVM}, {4, OperationId::VMSBC_VXM}});
        table.assign_row(0x17, {{0, OperationId::VMERGE_VVM},
                                {4, OperationId::VMERGE_VXM},
                                {3, OperationId::VMERGE_VIM},
                                {5, OperationId::VFMERGE_VFM}});
    }
    return table;
}

constexpr auto kVectorCommonTable = generate_vector_common_table();
constexpr auto kVectorMaskedTable = generate_vector_mask_table(false);
constexpr auto kVectorUnmaskedTable = generate_vector_mask_table(true);

auto decode_ext_v_special(uint32_t f6, uint32_t funct3, Instruction ir) -> OperationId {
    switch (f6) {
        case 0x10:
            if (funct3 == 2) {
                uint32_t rs1_val = (ir >> 15) & 0x1F;
                if (rs1_val == 0) return OperationId::VMV_X_S;
                if (rs1_val == 16) return OperationId::VCPOP_M;
                if (rs1_val == 17) return OperationId::VFIRST_M;
            } else if (funct3 == 1) {
                uint32_t rs1_val = (ir >> 15) & 0x1F;
                if (rs1_val == 0) return OperationId::VFMV_F_S;
            } else if (funct3 == 5) {
                uint32_t vs2 = (ir >> 20) & 0x1F;
                if (vs2 == 0) return OperationId::VFMV_S_F;
            }
            break;
        case 0x12:
            if (funct3 == 0) return OperationId::VSBC_VVM;
            if (funct3 == 4) return OperationId::VSBC_VXM;
            if (funct3 == 2) {
                uint32_t rs1_val = (ir >> 15) & 0x1F;
                if (rs1_val == 7) return OperationId::VSEXT_VF2;
                if (rs1_val == 5) return OperationId::VSEXT_VF4;
                if (rs1_val == 3) return OperationId::VSEXT_VF8;
                if (rs1_val == 6) return OperationId::VZEXT_VF2;
                if (rs1_val == 4) return OperationId::VZEXT_VF4;
                if (rs1_val == 2) return OperationId::VZEXT_VF8;
                if (rs1_val == 8) return OperationId::VCLZ_V;
                if (rs1_val == 9) return OperationId::VBREV8_V;
                if (rs1_val == 10) return OperationId::VREV8_V;
                if (rs1_val == 12) return OperationId::VBREV_V;
                if (rs1_val == 13) return OperationId::VCTZ_V;
                if (rs1_val == 14) return OperationId::VCPOP_V;
            }
            break;
        case 0x14:
            if (funct3 == 0) return OperationId::VROR_VV;
            if (funct3 == 4) return OperationId::VROR_VX;
            if (funct3 == 3) return OperationId::VROR_VI;
            if (funct3 == 2) {
                uint32_t rs1_val = (ir >> 15) & 0x1F;
                if (rs1_val == 1) return OperationId::VMSBF_M;
                if (rs1_val == 2) return OperationId::VMSOF_M;
                if (rs1_val == 3) return OperationId::VMSIF_M;
                if (rs1_val == 16) return OperationId::VIOTA_M;
                if (rs1_val == 17 && ((ir >> 20) & 0x1F) == 0) return OperationId::VID_V;
            }
            break;
        default:
            break;
    }
    return OperationId::UNKNOWN;
}

auto decode_ext_v_range2(uint32_t f6, uint32_t funct3, Instruction ir) -> OperationId {
    switch (f6) {
        case 0x20:
            if (funct3 == 2) return OperationId::VDIVU_VV;
            if (funct3 == 6) return OperationId::VDIVU_VX;
            if (funct3 == 0) return OperationId::VSADDU_VV;
            if (funct3 == 4) return OperationId::VSADDU_VX;
            if (funct3 == 3) return OperationId::VSADDU_VI;
            break;
        case 0x21:
            if (funct3 == 2) return OperationId::VDIV_VV;
            if (funct3 == 6) return OperationId::VDIV_VX;
            if (funct3 == 0) return OperationId::VSADD_VV;
            if (funct3 == 4) return OperationId::VSADD_VX;
            if (funct3 == 3) return OperationId::VSADD_VI;
            break;
        case 0x22:
            if (funct3 == 0) return OperationId::VSSUBU_VV;
            if (funct3 == 4) return OperationId::VSSUBU_VX;
            break;
        case 0x23:
            if (funct3 == 0) return OperationId::VSSUB_VV;
            if (funct3 == 4) return OperationId::VSSUB_VX;
            break;
        case 0x25:
            if (funct3 == 0) return OperationId::VSLL_VV;
            if (funct3 == 4) return OperationId::VSLL_VX;
            if (funct3 == 3) return OperationId::VSLL_VI;
            if (funct3 == 2) return OperationId::VMUL_VV;
            if (funct3 == 6) return OperationId::VMUL_VX;
            break;
        case 0x27:
            if (funct3 == 0) return OperationId::VSMUL_VV;
            if (funct3 == 4) return OperationId::VSMUL_VX;
            if (funct3 == 3) {
                uint32_t simm5 = (ir >> 15) & 0x1F;
                if (simm5 == 0) return OperationId::VMV1R_V;
                if (simm5 == 1) return OperationId::VMV2R_V;
                if (simm5 == 3) return OperationId::VMV4R_V;
                if (simm5 == 7) return OperationId::VMV8R_V;
            }
            break;
        case 0x28:
            if (funct3 == 0) return OperationId::VSRL_VV;
            if (funct3 == 4) return OperationId::VSRL_VX;
            if (funct3 == 3) return OperationId::VSRL_VI;
            break;
        case 0x29:
            if (funct3 == 0) return OperationId::VSRA_VV;
            if (funct3 == 4) return OperationId::VSRA_VX;
            if (funct3 == 3) return OperationId::VSRA_VI;
            if (funct3 == 2) return OperationId::VMADD_VV;
            if (funct3 == 6) return OperationId::VMADD_VX;
            break;
        case 0x2A:
            if (funct3 == 0) return OperationId::VSSRL_VV;
            if (funct3 == 4) return OperationId::VSSRL_VX;
            if (funct3 == 3) return OperationId::VSSRL_VI;
            break;
        case 0x2B:
            if (funct3 == 2) return OperationId::VNSUB_VV;
            if (funct3 == 6) return OperationId::VNSUB_VX;
            if (funct3 == 0) return OperationId::VSSRA_VV;
            if (funct3 == 4) return OperationId::VSSRA_VX;
            if (funct3 == 3) return OperationId::VSSRA_VI;
            break;
        case 0x2C:
            if (funct3 == 0) return OperationId::VNSRL_WV;
            if (funct3 == 4) return OperationId::VNSRL_WX;
            if (funct3 == 3) return OperationId::VNSRL_WI;
            if (funct3 == 1) return OperationId::VFMACC_VV;
            if (funct3 == 5) return OperationId::VFMACC_VF;
            break;
        case 0x2D:
            if (funct3 == 0) return OperationId::VNSRA_WV;
            if (funct3 == 4) return OperationId::VNSRA_WX;
            if (funct3 == 3) return OperationId::VNSRA_WI;
            if (funct3 == 2) return OperationId::VMACC_VV;
            if (funct3 == 6) return OperationId::VMACC_VX;
            break;
        case 0x2E:
            if (funct3 == 0) return OperationId::VNCLIPU_WV;
            if (funct3 == 4) return OperationId::VNCLIPU_WX;
            if (funct3 == 3) return OperationId::VNCLIPU_WI;
            break;
        case 0x2F:
            if (funct3 == 2) return OperationId::VNMSAC_VV;
            if (funct3 == 6) return OperationId::VNMSAC_VX;
            if (funct3 == 0) return OperationId::VNCLIP_WV;
            if (funct3 == 4) return OperationId::VNCLIP_WX;
            if (funct3 == 3) return OperationId::VNCLIP_WI;
            break;
        case 0x30:
            if (funct3 == 2) return OperationId::VWADDU_VV;
            if (funct3 == 6) return OperationId::VWADDU_VX;
            if (funct3 == 0) return OperationId::VWREDSUMU_VS;
            break;
        case 0x31:
            if (funct3 == 2) return OperationId::VWADD_VV;
            if (funct3 == 6) return OperationId::VWADD_VX;
            if (funct3 == 0) return OperationId::VWREDSUM_VS;
            break;
        case 0x32:
            if (funct3 == 2) return OperationId::VWSUBU_VV;
            if (funct3 == 6) return OperationId::VWSUBU_VX;
            break;
        case 0x33:
            if (funct3 == 2) return OperationId::VWSUB_VV;
            if (funct3 == 6) return OperationId::VWSUB_VX;
            break;
        case 0x34:
            if (funct3 == 2) return OperationId::VWADDU_WV;
            if (funct3 == 6) return OperationId::VWADDU_WX;
            break;
        case 0x35:
            if (funct3 == 2) return OperationId::VWADD_WV;
            if (funct3 == 6) return OperationId::VWADD_WX;
            if (funct3 == 0) return OperationId::VWSLL_VV;
            if (funct3 == 4) return OperationId::VWSLL_VX;
            if (funct3 == 3) return OperationId::VWSLL_VI;
            break;
        case 0x36:
            if (funct3 == 2) return OperationId::VWSUBU_WV;
            if (funct3 == 6) return OperationId::VWSUBU_WX;
            break;
        case 0x37:
            if (funct3 == 2) return OperationId::VWSUB_WV;
            if (funct3 == 6) return OperationId::VWSUB_WX;
            break;
        case 0x38:
            if (funct3 == 2) return OperationId::VWMULU_VV;
            if (funct3 == 6) return OperationId::VWMULU_VX;
            break;
        case 0x3A:
            if (funct3 == 2) return OperationId::VWMULSU_VV;
            if (funct3 == 6) return OperationId::VWMULSU_VX;
            break;
        case 0x3B:
            if (funct3 == 2) return OperationId::VWMUL_VV;
            if (funct3 == 6) return OperationId::VWMUL_VX;
            break;
        case 0x3C:
            if (funct3 == 2) return OperationId::VWMACCU_VV;
            if (funct3 == 6) return OperationId::VWMACCU_VX;
            break;
        case 0x3D:
            if (funct3 == 2) return OperationId::VWMACC_VV;
            if (funct3 == 6) return OperationId::VWMACC_VX;
            break;
        case 0x3E:
            if (funct3 == 6) return OperationId::VWMACCUS_VX;
            break;
        case 0x3F:
            if (funct3 == 2) return OperationId::VWMACCSU_VV;
            if (funct3 == 6) return OperationId::VWMACCSU_VX;
            break;
        default:
            break;
    }
    return OperationId::UNKNOWN;
}

}  // namespace

auto decode_ext_v(uint32_t funct3, uint32_t funct7, Instruction ir) -> OperationId {
    if (funct3 == 7) {
        if ((ir & (1u << 31)) == 0) {
            return OperationId::VSETVLI;
        }
        if (((ir >> 30) & 0x3) == 0x3) {
            return OperationId::VSETIVLI;
        }
        if (funct7 == 0x40) {
            return OperationId::VSETVL;
        }
        return OperationId::UNKNOWN;
    }

    const bool vm = (ir & (1u << 25)) != 0;
    const uint32_t f6 = funct7 >> 1;

    // A small number of vector encodings use rs1/vs2 as a secondary opcode.
    if ((f6 == 0x10 && (funct3 == 1 || funct3 == 2 || funct3 == 5)) ||
        ((f6 == 0x12 || f6 == 0x14) && funct3 == 2)) {
        return decode_ext_v_special(f6, funct3, ir);
    }
    if (f6 == 0x27 && funct3 == 3) {
        return decode_ext_v_range2(f6, funct3, ir);
    }

    const auto mask_dependent = (vm ? kVectorUnmaskedTable : kVectorMaskedTable).lookup(f6, funct3);
    return mask_dependent != OperationId::UNKNOWN ? mask_dependent
                                                  : kVectorCommonTable.lookup(f6, funct3);
}

auto decode_ext_f_d(Opcode op, uint32_t funct3, uint32_t funct7, uint32_t rs2, Instruction ir)
    -> OperationId {
    switch (op) {
        case Opcode::LoadFp: {
            if (funct3 == 2) return OperationId::FLW;
            if (funct3 == 3) return OperationId::FLD;
            uint32_t mop = (ir >> 26) & 3;
            if (mop == 0) {
                uint32_t lumop = (ir >> 20) & 0x1F;
                if (lumop == 8) {
                    uint32_t nf = (ir >> 29) & 7;
                    if (nf == 0) {
                        if (funct3 == 0) return OperationId::VL1RE8_V;
                        if (funct3 == 5) return OperationId::VL1RE16_V;
                        if (funct3 == 6) return OperationId::VL1RE32_V;
                        if (funct3 == 7) return OperationId::VL1RE64_V;
                    }
                    if (nf == 1) {
                        if (funct3 == 0) return OperationId::VL2RE8_V;
                        if (funct3 == 5) return OperationId::VL2RE16_V;
                        if (funct3 == 6) return OperationId::VL2RE32_V;
                        if (funct3 == 7) return OperationId::VL2RE64_V;
                    }
                    if (nf == 3) {
                        if (funct3 == 0) return OperationId::VL4RE8_V;
                        if (funct3 == 5) return OperationId::VL4RE16_V;
                        if (funct3 == 6) return OperationId::VL4RE32_V;
                        if (funct3 == 7) return OperationId::VL4RE64_V;
                    }
                    if (nf == 7) {
                        if (funct3 == 0) return OperationId::VL8RE8_V;
                        if (funct3 == 5) return OperationId::VL8RE16_V;
                        if (funct3 == 6) return OperationId::VL8RE32_V;
                        if (funct3 == 7) return OperationId::VL8RE64_V;
                    }
                } else {
                    if (funct3 == 0) return OperationId::VLE8_V;
                    if (funct3 == 5) return OperationId::VLE16_V;
                    if (funct3 == 6) return OperationId::VLE32_V;
                    if (funct3 == 7) return OperationId::VLE64_V;
                }
            } else if (mop == 2) {
                if (funct3 == 0) return OperationId::VLSE8_V;
                if (funct3 == 5) return OperationId::VLSE16_V;
                if (funct3 == 6) return OperationId::VLSE32_V;
                if (funct3 == 7) return OperationId::VLSE64_V;
            } else if (mop == 1) {
                if (funct3 == 0) return OperationId::VLUXEI8_V;
                if (funct3 == 5) return OperationId::VLUXEI16_V;
                if (funct3 == 6) return OperationId::VLUXEI32_V;
                if (funct3 == 7) return OperationId::VLUXEI64_V;
            } else if (mop == 3) {
                if (funct3 == 0) return OperationId::VLOXEI8_V;
                if (funct3 == 5) return OperationId::VLOXEI16_V;
                if (funct3 == 6) return OperationId::VLOXEI32_V;
                if (funct3 == 7) return OperationId::VLOXEI64_V;
            }
            return OperationId::UNKNOWN;
        }
        case Opcode::StoreFp: {
            if (funct3 == 2) return OperationId::FSW;
            if (funct3 == 3) return OperationId::FSD;
            uint32_t mop = (ir >> 26) & 3;
            if (mop == 0) {
                uint32_t sumop = (ir >> 20) & 0x1F;
                if (sumop == 8) {
                    uint32_t nf = (ir >> 29) & 7;
                    if (funct3 == 0) {
                        if (nf == 0) return OperationId::VS1R_V;
                        if (nf == 1) return OperationId::VS2R_V;
                        if (nf == 3) return OperationId::VS4R_V;
                        if (nf == 7) return OperationId::VS8R_V;
                    }
                } else {
                    if (funct3 == 0) return OperationId::VSE8_V;
                    if (funct3 == 5) return OperationId::VSE16_V;
                    if (funct3 == 6) return OperationId::VSE32_V;
                    if (funct3 == 7) return OperationId::VSE64_V;
                }
            } else if (mop == 2) {
                if (funct3 == 0) return OperationId::VSSE8_V;
                if (funct3 == 5) return OperationId::VSSE16_V;
                if (funct3 == 6) return OperationId::VSSE32_V;
                if (funct3 == 7) return OperationId::VSSE64_V;
            } else if (mop == 1) {
                if (funct3 == 0) return OperationId::VSUXEI8_V;
                if (funct3 == 5) return OperationId::VSUXEI16_V;
                if (funct3 == 6) return OperationId::VSUXEI32_V;
                if (funct3 == 7) return OperationId::VSUXEI64_V;
            } else if (mop == 3) {
                if (funct3 == 0) return OperationId::VSOXEI8_V;
                if (funct3 == 5) return OperationId::VSOXEI16_V;
                if (funct3 == 6) return OperationId::VSOXEI32_V;
                if (funct3 == 7) return OperationId::VSOXEI64_V;
            }
            return OperationId::UNKNOWN;
        }
        case Opcode::MAdd:
        case Opcode::MSub:
        case Opcode::NMSub:
        case Opcode::NMAdd:
            return decode_fma(op, funct7);
        case Opcode::OpFp:
            return decode_op_fp(funct3, funct7, rs2);
        default:
            return OperationId::UNKNOWN;
    }
}

auto make_r_type(uint32_t opcode, uint32_t funct3, uint32_t funct7, uint32_t rd, uint32_t rs1,
                 uint32_t rs2) -> Instruction {
    return opcode | (rd << 7) | (funct3 << 12) | (rs1 << 15) | (rs2 << 20) | (funct7 << 25);
}

auto make_i_type(uint32_t opcode, uint32_t funct3, uint32_t rd, uint32_t rs1, uint32_t imm)
    -> Instruction {
    return opcode | (rd << 7) | (funct3 << 12) | (rs1 << 15) | ((imm & 0xFFF) << 20);
}

auto make_s_type(uint32_t opcode, uint32_t funct3, uint32_t rs1, uint32_t rs2, uint32_t imm)
    -> Instruction {
    return opcode | ((imm & 0x1F) << 7) | (funct3 << 12) | (rs1 << 15) | (rs2 << 20) |
           (((imm >> 5) & 0x7F) << 25);
}

auto make_b_type(uint32_t opcode, uint32_t funct3, uint32_t rs1, uint32_t rs2, uint32_t imm)
    -> Instruction {
    return opcode | (((imm >> 11) & 0x1) << 7) | (((imm >> 1) & 0xF) << 8) | (funct3 << 12) |
           (rs1 << 15) | (rs2 << 20) | (((imm >> 5) & 0x3F) << 25) | (((imm >> 12) & 0x1) << 31);
}

auto make_u_type(uint32_t opcode, uint32_t rd, uint32_t imm) -> Instruction {
    return opcode | (rd << 7) | (imm & 0xFFFFF000);
}

auto make_j_type(uint32_t opcode, uint32_t rd, uint32_t imm) -> Instruction {
    return opcode | (rd << 7) | (((imm >> 12) & 0xFF) << 12) | (((imm >> 11) & 0x1) << 20) |
           (((imm >> 1) & 0x3FF) << 21) | (((imm >> 20) & 0x1) << 31);
}

auto decompress_q0(Instruction ir, bool is_rv64, uint32_t op, uint32_t rs1_p, uint32_t rs2_p)
    -> Instruction {
    switch (op) {
        case 0: {
            const uint32_t imm = ((ir >> 7) & 0xF) << 6 | ((ir >> 11) & 0x3) << 4 |
                                 ((ir >> 5) & 0x1) << 3 | ((ir >> 6) & 0x1) << 2;
            if (imm == 0) return 0;
            return make_i_type(0x13, 0x0, rs2_p, 2, imm);
        }
        case 1: {
            const uint32_t imm = ((ir >> 5) & 0x3) << 6 | ((ir >> 10) & 0x7) << 3;
            return make_i_type(0x07, 0x3, rs2_p, rs1_p, imm);
        }
        case 2: {
            const uint32_t imm =
                ((ir >> 5) & 0x1) << 6 | ((ir >> 10) & 0x7) << 3 | ((ir >> 6) & 0x1) << 2;
            return make_i_type(0x03, 0x2, rs2_p, rs1_p, imm);
        }
        case 3: {
            if (is_rv64) {
                const uint32_t imm = ((ir >> 5) & 0x3) << 6 | ((ir >> 10) & 0x7) << 3;
                return make_i_type(0x03, 0x3, rs2_p, rs1_p, imm);
            } else {
                const uint32_t imm =
                    ((ir >> 5) & 0x1) << 6 | ((ir >> 10) & 0x7) << 3 | ((ir >> 6) & 0x1) << 2;
                return make_i_type(0x07, 0x2, rs2_p, rs1_p, imm);
            }
        }
        case 5: {
            const uint32_t imm = ((ir >> 5) & 0x3) << 6 | ((ir >> 10) & 0x7) << 3;
            return make_s_type(0x27, 0x3, rs1_p, rs2_p, imm);
        }
        case 6: {
            const uint32_t imm =
                ((ir >> 5) & 0x1) << 6 | ((ir >> 10) & 0x7) << 3 | ((ir >> 6) & 0x1) << 2;
            return make_s_type(0x23, 0x2, rs1_p, rs2_p, imm);
        }
        case 7: {
            if (is_rv64) {
                const uint32_t imm = ((ir >> 5) & 0x3) << 6 | ((ir >> 10) & 0x7) << 3;
                return make_s_type(0x23, 0x3, rs1_p, rs2_p, imm);
            } else {
                const uint32_t imm =
                    ((ir >> 5) & 0x1) << 6 | ((ir >> 10) & 0x7) << 3 | ((ir >> 6) & 0x1) << 2;
                return make_s_type(0x27, 0x2, rs1_p, rs2_p, imm);
            }
        }
        default:
            return 0;
    }
}

auto decompress_q1_op4(Instruction ir, uint32_t rs1_p, uint32_t rs2_p) -> Instruction {
    const uint32_t sub_op = (ir >> 10) & 0x3;
    if (sub_op == 0 || sub_op == 1) {
        const uint32_t shamt = ((ir >> 2) & 0x1F) | (((ir >> 12) & 0x1) << 5);
        const uint32_t funct7 = (sub_op == 1) ? 0x20 : 0x00;
        return make_r_type(0x13, 0x5, funct7, rs1_p, rs1_p, shamt);
    }
    if (sub_op == 2) {
        const uint32_t imm = ((ir >> 2) & 0x1F) | (((ir >> 12) & 0x1) ? 0xFFFFFFE0 : 0);
        return make_i_type(0x13, 0x7, rs1_p, rs1_p, imm);
    }
    const uint32_t funct_sub = (ir >> 5) & 0x3;
    const uint32_t bit12 = (ir >> 12) & 0x1;
    if (bit12 == 0) {
        switch (funct_sub) {
            case 0:
                return make_r_type(0x33, 0x0, 0x20, rs1_p, rs1_p, rs2_p);
            case 1:
                return make_r_type(0x33, 0x4, 0x00, rs1_p, rs1_p, rs2_p);
            case 2:
                return make_r_type(0x33, 0x6, 0x00, rs1_p, rs1_p, rs2_p);
            case 3:
                return make_r_type(0x33, 0x7, 0x00, rs1_p, rs1_p, rs2_p);
            default:
                break;
        }
    } else {
        switch (funct_sub) {
            case 0:
                return make_r_type(0x3B, 0x0, 0x20, rs1_p, rs1_p, rs2_p);
            case 1:
                return make_r_type(0x3B, 0x0, 0x00, rs1_p, rs1_p, rs2_p);
            default:
                break;
        }
    }
    return 0;
}

auto decompress_q1_op1(Instruction ir, bool is_rv64, uint32_t rs1_rd) -> Instruction {
    if (is_rv64) {
        const uint32_t imm = ((ir >> 2) & 0x1F) | (((ir >> 12) & 0x1) ? 0xFFFFFFE0 : 0);
        return make_i_type(0x1B, 0x0, rs1_rd, rs1_rd, imm);
    } else {
        const uint32_t imm = ((ir >> 2) & 0x1) << 5 | ((ir >> 3) & 0x7) << 1 |
                             ((ir >> 6) & 0x1) << 7 | ((ir >> 7) & 0x1) << 6 |
                             ((ir >> 8) & 0x1) << 10 | ((ir >> 9) & 0x3) << 8 |
                             ((ir >> 11) & 0x1) << 4 | (((ir >> 12) & 0x1) ? 0xFFFFF800 : 0);
        return make_j_type(0x6F, 1, imm);
    }
}

auto decompress_q1_op3(Instruction ir, uint32_t rs1_rd) -> Instruction {
    if (rs1_rd == 2) {
        const uint32_t imm = ((ir >> 2) & 0x1) << 5 | ((ir >> 3) & 0x3) << 7 |
                             ((ir >> 5) & 0x1) << 6 | ((ir >> 6) & 0x1) << 4 |
                             (((ir >> 12) & 0x1) ? 0xFFFFFE00 : 0);
        return make_i_type(0x13, 0x0, 2, 2, imm);
    } else {
        const uint32_t imm = (((ir >> 2) & 0x1F) << 12) | (((ir >> 12) & 0x1) ? 0xFFFE0000 : 0);
        return make_u_type(0x37, rs1_rd, imm);
    }
}

auto decompress_q1(Instruction ir, bool is_rv64, uint32_t op, uint32_t rs1_rd, uint32_t rs1_p,
                   uint32_t rs2_p) -> Instruction {
    switch (op) {
        case 0: {
            const uint32_t imm = ((ir >> 2) & 0x1F) | (((ir >> 12) & 0x1) ? 0xFFFFFFE0 : 0);
            return make_i_type(0x13, 0x0, rs1_rd, rs1_rd, imm);
        }
        case 1:
            return decompress_q1_op1(ir, is_rv64, rs1_rd);
        case 2: {
            const uint32_t imm = ((ir >> 2) & 0x1F) | (((ir >> 12) & 0x1) ? 0xFFFFFFE0 : 0);
            return make_i_type(0x13, 0x0, rs1_rd, 0, imm);
        }
        case 3:
            return decompress_q1_op3(ir, rs1_rd);
        case 4:
            return decompress_q1_op4(ir, rs1_p, rs2_p);
        case 5: {
            const uint32_t imm = ((ir >> 2) & 0x1) << 5 | ((ir >> 3) & 0x7) << 1 |
                                 ((ir >> 6) & 0x1) << 7 | ((ir >> 7) & 0x1) << 6 |
                                 ((ir >> 8) & 0x1) << 10 | ((ir >> 9) & 0x3) << 8 |
                                 ((ir >> 11) & 0x1) << 4 | (((ir >> 12) & 0x1) ? 0xFFFFF800 : 0);
            return make_j_type(0x6F, 0, imm);
        }
        case 6:
        case 7: {
            const uint32_t imm = ((ir >> 2) & 0x1) << 5 | ((ir >> 3) & 0x3) << 1 |
                                 ((ir >> 5) & 0x3) << 6 | ((ir >> 10) & 0x3) << 3 |
                                 (((ir >> 12) & 0x1) ? 0xFFFFFF00 : 0);
            return make_b_type(0x63, (op == 6) ? 0x0 : 0x1, rs1_p, 0, imm);
        }
        default:
            break;
    }
    return 0;
}

auto decompress_q2_op4(Instruction ir, uint32_t rs1_rd, uint32_t rs2) -> Instruction {
    const uint32_t bit12 = (ir >> 12) & 0x1;
    if (bit12 == 0) {
        if (rs2 == 0) {
            return make_i_type(0x67, 0x0, 0, rs1_rd, 0);
        } else {
            return make_r_type(0x33, 0x0, 0x00, rs1_rd, 0, rs2);
        }
    } else {
        if (rs1_rd == 0 && rs2 == 0) return 0x00100073;
        if (rs2 == 0) {
            return make_i_type(0x67, 0x0, 1, rs1_rd, 0);
        } else {
            return make_r_type(0x33, 0x0, 0x00, rs1_rd, rs1_rd, rs2);
        }
    }
}

auto decompress_q2(Instruction ir, bool is_rv64, uint32_t op, uint32_t rs1_rd, uint32_t rs2)
    -> Instruction {
    switch (op) {
        case 0: {
            const uint32_t shamt = ((ir >> 2) & 0x1F) | (((ir >> 12) & 0x1) << 5);
            return make_r_type(0x13, 0x1, 0x00, rs1_rd, rs1_rd, shamt);
        }
        case 1: {
            const uint32_t imm =
                ((ir >> 2) & 0x7) << 6 | ((ir >> 5) & 0x3) << 3 | ((ir >> 12) & 0x1) << 5;
            return make_i_type(0x07, 0x3, rs1_rd, 2, imm);
        }
        case 2: {
            const uint32_t imm =
                ((ir >> 2) & 0x3) << 6 | ((ir >> 4) & 0x7) << 2 | ((ir >> 12) & 0x1) << 5;
            return make_i_type(0x03, 0x2, rs1_rd, 2, imm);
        }
        case 3: {
            if (is_rv64) {
                const uint32_t imm =
                    ((ir >> 2) & 0x7) << 6 | ((ir >> 5) & 0x3) << 3 | ((ir >> 12) & 0x1) << 5;
                return make_i_type(0x03, 0x3, rs1_rd, 2, imm);
            } else {
                const uint32_t imm =
                    ((ir >> 2) & 0x3) << 6 | ((ir >> 4) & 0x7) << 2 | ((ir >> 12) & 0x1) << 5;
                return make_i_type(0x07, 0x2, rs1_rd, 2, imm);
            }
        }
        case 4:
            return decompress_q2_op4(ir, rs1_rd, rs2);
        case 5: {
            const uint32_t imm = ((ir >> 7) & 0x7) << 6 | ((ir >> 10) & 0x7) << 3;
            return make_s_type(0x27, 0x3, 2, rs2, imm);
        }
        case 6: {
            const uint32_t imm = ((ir >> 7) & 0x3) << 6 | ((ir >> 9) & 0xF) << 2;
            return make_s_type(0x23, 0x2, 2, rs2, imm);
        }
        case 7: {
            if (is_rv64) {
                const uint32_t imm = ((ir >> 7) & 0x7) << 6 | ((ir >> 10) & 0x7) << 3;
                return make_s_type(0x23, 0x3, 2, rs2, imm);
            } else {
                const uint32_t imm = ((ir >> 7) & 0x3) << 6 | ((ir >> 9) & 0xF) << 2;
                return make_s_type(0x27, 0x2, 2, rs2, imm);
            }
        }
        default:
            break;
    }
    return 0;
}

}  // namespace

auto decoder(Instruction ir) -> OperationId {
    simrv::pipeline::Decoder dec(ir);
    const auto op = dec.opcode();
    const auto funct3 = std::to_underlying(dec.funct3());
    const auto funct7 = dec.funct7();

    if (op == Opcode::Amo) {
        return decode_ext_a(funct7, funct3, std::to_underlying(dec.rs2()));
    }
    if (op == Opcode::LoadFp || op == Opcode::StoreFp || op == Opcode::OpFp || op == Opcode::MAdd ||
        op == Opcode::MSub || op == Opcode::NMSub || op == Opcode::NMAdd) {
        const OperationId decoded =
            decode_ext_f_d(op, funct3, funct7, std::to_underlying(dec.rs2()), ir);
        if constexpr (!simrv::xlen::kIsXLen64) {
            if (isa::requires_rv64(decoded)) {
                return OperationId::UNKNOWN;
            }
        }
        return decoded;
    }
    if (op == Opcode::OpV) {
        return decode_ext_v(funct3, funct7, ir);
    }
    if (op == Opcode::Custom0) {
        return OperationId::VCHECK;
    }
    if ((op == Opcode::Op || op == Opcode::Op32) && (funct7 == 0x01)) {
        return decode_ext_m(op, funct3);
    }
    auto res = decode_ext_i(op, funct3, funct7, ir);
    return res;
}

auto decompressInstruction(Instruction ir, bool is_rv64) -> Instruction {
    simrv::pipeline::Decoder dec(ir);
    if (!dec.is_compressed()) {
        return ir;
    }

    const uint32_t quadrant = ir & 0x3;
    const uint32_t op = dec.c_op();

    const uint32_t rs1_rd = std::to_underlying(dec.c_rs1_rd());
    const uint32_t rs2 = std::to_underlying(dec.c_rs2());
    const uint32_t rs1_p = std::to_underlying(dec.c_rs1_rd_p());
    const uint32_t rs2_p = std::to_underlying(dec.c_rs2_p());

    if (quadrant == 0x0) {
        return decompress_q0(ir, is_rv64, op, rs1_p, rs2_p);
    }
    if (quadrant == 0x1) {
        return decompress_q1(ir, is_rv64, op, rs1_rd, rs1_p, rs2_p);
    }
    if (quadrant == 0x2) {
        return decompress_q2(ir, is_rv64, op, rs1_rd, rs2);
    }
    return 0;
}

}  // namespace simrv::pipeline
