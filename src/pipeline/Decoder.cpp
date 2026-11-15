#include "simrv/pipeline/Decoder.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "simrv/execute/ExecuteUnitZk.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/pipeline/DecodeTable.hpp"

namespace simrv::pipeline {

using simrv::isa::OperationId;
using enum simrv::isa::OperationId;

const std::array<std::string_view, static_cast<size_t>(isa::OperationIdCount)> OPERATION_NAME = {
    "LUI", "AUIPC", "JAL", "JALR", "BEQ", "BNE", "BLT", "BGE", "BLTU", "BGEU", "LB", "LH", "LW",
    "LD", "LBU", "LHU", "LWU", "SB", "SH", "SW", "SD", "ADDI", "SLTI", "SLTIU", "XORI", "ORI",
    "ANDI", "SLLI", "SRLI", "SRAI", "ADDIW", "SLLIW", "SRLIW", "SRAIW", "ADD", "SUB", "SLL", "SLT",
    "SLTU", "XOR", "SRL", "SRA", "OR", "AND", "ADDW", "SUBW", "SLLW", "SRLW", "SRAW", "FENCE",
    "FENCE_I", "ECALL", "EBREAK", "CSRRW", "CSRRS", "CSRRC", "CSRRWI", "CSRRSI", "CSRRCI", "URET",
    "SRET", "MRET", "WFI", "SFENCE_VMA", "HFENCE_VVMA", "HFENCE_GVMA", "MUL", "MULH", "MULHSU",
    "MULHU", "DIV", "DIVU", "REM", "REMU", "MULW", "DIVW", "DIVUW", "REMW", "REMUW", "LR_W", "SC_W",
    "AMOSWAP_W", "AMOADD_W", "AMOXOR_W", "AMOAND_W", "AMOOR_W", "AMOMIN_W", "AMOMAX_W", "AMOMINU_W",
    "AMOMAXU_W", "LR_D", "SC_D", "AMOSWAP_D", "AMOADD_D", "AMOXOR_D", "AMOAND_D", "AMOOR_D",
    "AMOMIN_D", "AMOMAX_D", "AMOMINU_D", "AMOMAXU_D", "FLW", "FSW", "FMADD_S", "FMSUB_S",
    "FNMADD_S", "FNMSUB_S", "FADD_S", "FSUB_S", "FMUL_S", "FDIV_S", "FSQRT_S", "FSGNJ_S",
    "FSGNJN_S", "FSGNJX_S", "FMIN_S", "FMAX_S", "FCVT_W_S", "FCVT_WU_S", "FMV_X_W", "FEQ_S",
    "FLT_S", "FLE_S", "FCLASS_S", "FCVT_S_W", "FCVT_S_WU", "FMV_W_X", "FCVT_L_S", "FCVT_LU_S",
    "FCVT_S_L", "FCVT_S_LU", "FLD", "FSD", "FMADD_D", "FMSUB_D", "FNMSUB_D", "FNMADD_D", "FADD_D",
    "FSUB_D", "FMUL_D", "FDIV_D", "FSQRT_D", "FSGNJ_D", "FSGNJN_D", "FSGNJX_D", "FMIN_D", "FMAX_D",
    "FCVT_S_D", "FCVT_D_S", "FEQ_D", "FLT_D", "FLE_D", "FCLASS_D", "FCVT_W_D", "FCVT_WU_D",
    "FCVT_D_W", "FCVT_D_WU", "FMV_X_D", "FMV_D_X", "FCVT_L_D", "FCVT_LU_D", "FCVT_D_L", "FCVT_D_LU",
    "SH1ADD", "SH2ADD", "SH3ADD", "SH1ADD.UW", "SH2ADD.UW", "SH3ADD.UW", "ADD.UW", "SLLI.UW",
    "ANDN", "ORN", "XNOR", "CLZ", "CTZ", "CPOP", "MIN", "MAX", "MINU", "MAXU", "SEXT.B", "SEXT.H",
    "ZEXT.H", "ROL", "ROR", "RORI", "CLZW", "CTZW", "CPOPW", "ROLW", "RORW", "RORIW", "CLMUL",
    "CLMULH", "CLMULR", "BSET", "BSETI", "BCLR", "BCLRI", "BINV", "BINVI", "BEXT", "BEXTI", "ORC.B",
    "REV8", "PACK", "PACKW", "VSETVLI", "VSETIVLI", "VSETVL", "VLE8_V", "VLE16_V", "VLE32_V",
    "VLE64_V", "VLE8FF_V", "VLE16FF_V", "VLE32FF_V", "VLE64FF_V", "VLM_V", "VSE8_V", "VSE16_V",
    "VSE32_V", "VSE64_V", "VSM_V", "VADD_VV", "VADD_VX", "VADD_VI", "VSUB_VV", "VSUB_VX", "VMUL_VV",
    "VMUL_VX", "VMULH_VV", "VMULH_VX", "VMULHU_VV", "VMULHU_VX", "VMULHSU_VV", "VMULHSU_VX",
    "VDIV_VV", "VDIV_VX", "VDIVU_VV", "VDIVU_VX", "VREM_VV", "VREM_VX", "VREMU_VV", "VREMU_VX",
    "VAND_VV", "VAND_VX", "VAND_VI", "VOR_VV", "VOR_VX", "VOR_VI", "VXOR_VV", "VXOR_VX", "VXOR_VI",
    "VSLL_VV", "VSLL_VX", "VSLL_VI", "VSRL_VV", "VSRL_VX", "VSRL_VI", "VSRA_VV", "VSRA_VX",
    "VSRA_VI", "VMV_V_V", "VMV_V_X", "VMV_V_I", "VMV_X_S", "VMV_S_X", "VMSEQ_VV", "VMSEQ_VX",
    "VMSEQ_VI", "VMSNE_VV", "VMSNE_VX", "VMSNE_VI", "VMSLT_VV", "VMSLT_VX", "VMSLTU_VV",
    "VMSLTU_VX", "VMSLE_VV", "VMSLE_VX", "VMSLE_VI", "VMSLEU_VV", "VMSLEU_VX", "VMSLEU_VI",
    "VMSGT_VX", "VMSGT_VI", "VMSGTU_VX", "VMSGTU_VI", "VMERGE_VVM", "VMERGE_VXM", "VMERGE_VIM",
    "VMACC_VV", "VMACC_VX", "VMADD_VV", "VMADD_VX", "VNMSAC_VV", "VNMSAC_VX", "VNMSUB_VV",
    "VNMSUB_VX", "VWMACCU_VV", "VWMACCU_VX", "VWMACC_VV", "VWMACC_VX", "VWMACCUS_VX", "VWMACCSU_VV",
    "VWMACCSU_VX", "VMIN_VV", "VMIN_VX", "VMINU_VV", "VMINU_VX", "VMAX_VV", "VMAX_VX", "VMAXU_VV",
    "VMAXU_VX", "VLSE8_V", "VLSE16_V", "VLSE32_V", "VLSE64_V", "VSSE8_V", "VSSE16_V", "VSSE32_V",
    "VSSE64_V", "VLUXEI8_V", "VLUXEI16_V", "VLUXEI32_V", "VLUXEI64_V", "VLOXEI8_V", "VLOXEI16_V",
    "VLOXEI32_V", "VLOXEI64_V", "VSUXEI8_V", "VSUXEI16_V", "VSUXEI32_V", "VSUXEI64_V", "VSOXEI8_V",
    "VSOXEI16_V", "VSOXEI32_V", "VSOXEI64_V", "VID_V", "VFMADD_VV", "VFMADD_VF", "VFNMADD_VV",
    "VFNMADD_VF", "VFMSUB_VV", "VFMSUB_VF", "VFNMSUB_VV", "VFNMSUB_VF", "VFMACC_VV", "VFMACC_VF",
    "VFNMACC_VV", "VFNMACC_VF", "VFMSAC_VV", "VFMSAC_VF", "VFNMSAC_VV", "VFNMSAC_VF", "VREDSUM_VS",
    "VREDAND_VS", "VREDOR_VS", "VREDXOR_VS", "VREDMINU_VS", "VREDMIN_VS", "VREDMAXU_VS",
    "VREDMAX_VS", "VFREDUSUM_VS", "VFREDOSUM_VS", "VFREDMIN_VS", "VFREDMAX_VS", "VFWREDUSUM_VS",
    "VFWREDOSUM_VS", "VFWADD_VV", "VFWADD_VF", "VFWSUB_VV", "VFWSUB_VF", "VFWADD_WV", "VFWADD_WF",
    "VFWSUB_WV", "VFWSUB_WF", "VFWMUL_VV", "VFWMUL_VF", "VFWMACC_VV", "VFWMACC_VF", "VFWNMACC_VV",
    "VFWNMACC_VF", "VFWMSAC_VV", "VFWMSAC_VF", "VFWNMSAC_VV", "VFWNMSAC_VF", "VWMUL_VV", "VWMUL_VX",
    "VNCLIP_WV", "VNCLIP_WX", "VNCLIP_WI", "VNCLIPU_WV", "VNCLIPU_WX", "VNCLIPU_WI", "VNSRL_WV",
    "VNSRL_WX", "VNSRL_WI", "VNSRA_WV", "VNSRA_WX", "VNSRA_WI", "VSLIDE1UP_VX", "VSLIDE1DOWN_VX",
    "VFSLIDE1UP_VF", "VFSLIDE1DOWN_VF", "VFADD_VV", "VFADD_VF", "VFSUB_VV", "VFSUB_VF", "VFRSUB_VF",
    "VFMUL_VV", "VFMUL_VF", "VFDIV_VV", "VFDIV_VF", "VFRDIV_VF", "VFSQRT_V", "VFRSQRT7_V",
    "VFREC7_V", "VFCLASS_V", "VFCVT_XU_F_V", "VFCVT_X_F_V", "VFCVT_F_XU_V", "VFCVT_F_X_V",
    "VFCVT_RTZ_XU_F_V", "VFCVT_RTZ_X_F_V", "VFWCVT_XU_F_V", "VFWCVT_X_F_V", "VFWCVT_F_XU_V",
    "VFWCVT_F_X_V", "VFWCVT_F_F_V", "VFWCVT_RTZ_XU_F_V", "VFWCVT_RTZ_X_F_V", "VFNCVT_XU_F_W",
    "VFNCVT_X_F_W", "VFNCVT_F_XU_W", "VFNCVT_F_X_W", "VFNCVT_F_F_W", "VFNCVT_ROD_F_F_W",
    "VFNCVT_RTZ_XU_F_W", "VFNCVT_RTZ_X_F_W", "VFSGNJ_VV", "VFSGNJ_VF", "VFSGNJN_VV", "VFSGNJN_VF",
    "VFSGNJX_VV", "VFSGNJX_VF", "VFMIN_VV", "VFMIN_VF", "VFMAX_VV", "VFMAX_VF", "VMFEQ_VV",
    "VMFEQ_VF", "VMFNE_VV", "VMFNE_VF", "VMFLT_VV", "VMFLT_VF", "VMFLE_VV", "VMFLE_VF", "VMFGT_VF",
    "VMFGE_VF", "VRSUB_VX", "VRSUB_VI", "VSADD_VV", "VSADD_VX", "VSADD_VI", "VSADDU_VV",
    "VSADDU_VX", "VSADDU_VI", "VAADDU_VV", "VAADDU_VX", "VAADD_VV", "VAADD_VX", "VASUBU_VV",
    "VASUBU_VX", "VASUB_VV", "VASUB_VX", "VSBC_VVM", "VSBC_VXM", "VSEXT_VF2", "VSEXT_VF4",
    "VSEXT_VF8", "VZEXT_VF2", "VZEXT_VF4", "VZEXT_VF8", "VSLIDEDOWN_VX", "VSLIDEDOWN_VI",
    "VSLIDEUP_VX", "VSLIDEUP_VI", "VRGATHER_VV", "VRGATHER_VX", "VRGATHER_VI", "VRGATHEREI16_VV",
    "VSMUL_VV", "VSMUL_VX", "VSSRA_VV", "VSSRA_VX", "VSSRA_VI", "VSSRL_VV", "VSSRL_VX", "VSSRL_VI",
    "VSSUB_VV", "VSSUB_VX", "VSSUBU_VV", "VSSUBU_VX", "VWADD_VV", "VWADD_VX", "VWADD_WV",
    "VWADD_WX", "VWADDU_VV", "VWADDU_VX", "VWADDU_WV", "VWADDU_WX", "VWSUB_VV", "VWSUB_VX",
    "VWSUB_WV", "VWSUB_WX", "VWSUBU_VV", "VWSUBU_VX", "VWSUBU_WV", "VWSUBU_WX", "VWMULU_VV",
    "VWMULU_VX", "VWMULSU_VV", "VWMULSU_VX", "VWREDSUM_VS", "VWREDSUMU_VS", "VMV1R_V", "VMV2R_V",
    "VMV4R_V", "VMV8R_V", "VL1RE8_V", "VL1RE16_V", "VL1RE32_V", "VL1RE64_V", "VL2RE8_V",
    "VL2RE16_V", "VL2RE32_V", "VL2RE64_V", "VL4RE8_V", "VL4RE16_V", "VL4RE32_V", "VL4RE64_V",
    "VL8RE8_V", "VL8RE16_V", "VL8RE32_V", "VL8RE64_V", "VS1R_V", "VS2R_V", "VS4R_V", "VS8R_V",
    "VADC_VVM", "VADC_VXM", "VADC_VIM", "VMADC_VV", "VMADC_VX", "VMADC_VI", "VMADC_VVM",
    "VMADC_VXM", "VMADC_VIM", "VMSBC_VV", "VMSBC_VX", "VMSBC_VVM", "VMSBC_VXM", "VMSBF_M",
    "VMSIF_M", "VMSOF_M", "VFIRST_M", "VCPOP_M", "VIOTA_M", "VCOMPRESS_VM", "VANDN_VV", "VANDN_VX",
    "VROL_VV", "VROL_VX", "VROR_VV", "VROR_VX", "VROR_VI", "VCLZ_V", "VCTZ_V", "VCPOP_V", "VBREV_V",
    "VBREV8_V", "VREV8_V", "VCLMUL_VV", "VCLMUL_VX", "VCLMULH_VV", "VCLMULH_VX", "VMAND_MM",
    "VMNAND_MM", "VMANDN_MM", "VMOR_MM", "VMNOR_MM", "VMORN_MM", "VMXOR_MM", "VMXNOR_MM",
    "VFMV_F_S", "VFMV_S_F", "VFMERGE_VFM", "VFMV_V_F", "VWSLL_VV", "VWSLL_VX", "VWSLL_VI", "VCHECK",
    "CFU", "CZERO_EQZ", "CZERO_NEZ", "CBO_ZERO", "CBO_CLEAN", "CBO_FLUSH", "CBO_INVAL",
    "PREFETCH_I", "PREFETCH_R", "PREFETCH_W", "SINVAL_VMA", "SFENCE_W_INVAL", "SFENCE_INVAL_IR",
    "HINVAL_VVMA", "HINVAL_GVMA", "FLH", "FSH", "FMV_X_H", "FMV_H_X", "FCVT_S_H", "FCVT_H_S",
    "FCVT_D_H", "FCVT_H_D", "FLI_S", "FLI_D", "FMINM_S", "FMAXM_S", "FMINM_D", "FMAXM_D", "FLEQ_S",
    "FLTQ_S", "FLEQ_D", "FLTQ_D", "FROUND_S", "FROUNDNX_S", "FROUND_D", "FROUNDNX_D", "FCVTMOD_W_D",
    "MOP", "WRS_NTO", "WRS_STO", "HLV_B", "HLV_BU", "HLV_H", "HLV_HU", "HLV_W", "HLV_WU", "HLV_D",
    "HLVX_HU", "HLVX_WU", "HSV_B", "HSV_H", "HSV_W", "HSV_D", "AMOSWAP_B", "AMOADD_B", "AMOXOR_B",
    "AMOAND_B", "AMOOR_B", "AMOMIN_B", "AMOMAX_B", "AMOMINU_B", "AMOMAXU_B", "AMOSWAP_H",
    "AMOADD_H", "AMOXOR_H", "AMOAND_H", "AMOOR_H", "AMOMIN_H", "AMOMAX_H", "AMOMINU_H", "AMOMAXU_H",
    "AMOCAS_B", "AMOCAS_H", "AMOCAS_W", "AMOCAS_D", "AMOCAS_Q", "FMADD_H", "FMSUB_H", "FNMSUB_H",
    "FNMADD_H", "FADD_H", "FSUB_H", "FMUL_H", "FDIV_H", "FSQRT_H", "FSGNJ_H", "FSGNJN_H",
    "FSGNJX_H", "FMIN_H", "FMAX_H", "FCVT_W_H", "FCVT_WU_H", "FCVT_L_H", "FCVT_LU_H", "FCVT_H_W",
    "FCVT_H_WU", "FCVT_H_L", "FCVT_H_LU", "FEQ_H", "FLT_H", "FLE_H", "FCLASS_H", "FCVT_S_BF16",
    "FCVT_BF16_S", "VFWCVTBF16_F_F_V", "VFNCVTBF16_F_F_W", "VFWMACCBF16_VV", "VFWMACCBF16_VF",
    /* Track 2A: Vector Cryptography */
    "VAESEF_VV", "VAESEF_VS", "VAESDF_VV", "VAESDF_VS", "VAESEM_VV", "VAESEM_VS", "VAESDM_VV",
    "VAESDM_VS", "VAESZ_VS", "VAESKF1_VI", "VAESKF2_VI", "VGHSH_VV", "VGMUL_VV", "VSHA2MS_VV",
    "VSHA2CH_VV", "VSHA2CL_VV", "VSM3ME_VV", "VSM3C_VI", "VSM4K_VI", "VSM4R_VV", "VSM4R_VS",
    /* Track 2B: Scalar Cryptography */
    "AES32DSI", "AES32DSMI", "AES32ESI", "AES32ESMI", "AES64DS", "AES64DSM", "AES64ES", "AES64ESM",
    "AES64IM", "AES64KS1I", "AES64KS2", "SHA256SIG0", "SHA256SIG1", "SHA256SUM0", "SHA256SUM1",
    "SHA512SIG0", "SHA512SIG1", "SHA512SUM0", "SHA512SUM1", "SHA512SIG0L", "SHA512SIG0H",
    "SHA512SIG1L", "SHA512SIG1H", "SHA512SUM0R", "SHA512SUM1R", "SM4ED", "SM4KS", "SM3P0", "SM3P1",
    /* Track 2C: Code-Size */
    "CM_PUSH", "CM_POP", "CM_POPRET", "CM_POPRETZ", "CM_MVA01S", "CM_MVSA01", "CM_JT", "CM_JALT",
    "ILLEGAL"};

auto operation_name(isa::OperationId operation) noexcept -> std::string_view {
    const auto index = static_cast<std::size_t>(operation);
    return index < OPERATION_NAME.size() ? OPERATION_NAME[index] : "UNKNOWN";
}

namespace {

template <const auto& Table>
[[nodiscard]] constexpr auto decode_table(uint32_t index) noexcept -> OperationId {
    return index < Table.size() ? Table[index] : OperationId::ILLEGAL;
}

constexpr std::array<OperationId, 8> kBranchTable = {
    OperationId::BEQ, OperationId::BNE, OperationId::ILLEGAL, OperationId::ILLEGAL,
    OperationId::BLT, OperationId::BGE, OperationId::BLTU,    OperationId::BGEU};

constexpr std::array<OperationId, 8> kLoadTable = {
    OperationId::LB,  OperationId::LH,  OperationId::LW,  OperationId::LD,
    OperationId::LBU, OperationId::LHU, OperationId::LWU, OperationId::ILLEGAL};

constexpr std::array<OperationId, 8> kStoreTable = {
    OperationId::SB,      OperationId::SH,      OperationId::SW,      OperationId::SD,
    OperationId::ILLEGAL, OperationId::ILLEGAL, OperationId::ILLEGAL, OperationId::ILLEGAL};

constexpr auto decode_branch = decode_table<kBranchTable>;
constexpr auto decode_load = decode_table<kLoadTable>;
constexpr auto decode_store = decode_table<kStoreTable>;

constexpr std::array<OperationId, 8> kOpImmBaseTable = {
    OperationId::ADDI, OperationId::ILLEGAL, OperationId::SLTI, OperationId::SLTIU,
    OperationId::XORI, OperationId::ILLEGAL, OperationId::ORI,  OperationId::ANDI,
};

[[nodiscard]] constexpr auto decode_op_imm(uint32_t funct3, uint32_t funct7,
                                           simrv::xlen::Instruction ir) noexcept -> OperationId {
    const bool rd_is_zero = ((ir >> 7) & 0x1F) == 0;
    if (funct3 == 6 && rd_is_zero) {
        const uint32_t f5 = (ir >> 20U) & 0x1FU;
        if (f5 == 0x00) return OperationId::PREFETCH_I;
        if (f5 == 0x01) return OperationId::PREFETCH_R;
        if (f5 == 0x03) return OperationId::PREFETCH_W;
    }
    if (funct3 < kOpImmBaseTable.size() && kOpImmBaseTable[funct3] != OperationId::ILLEGAL) {
        return kOpImmBaseTable[funct3];
    }
    const uint32_t imm12 = ir >> 20;
    const uint32_t imm6 = imm12 >> 6;
    if (funct3 == 1) {
        if (imm12 == 0x100) return OperationId::SHA256SUM0;
        if (imm12 == 0x101) return OperationId::SHA256SUM1;
        if (imm12 == 0x102) return OperationId::SHA256SIG0;
        if (imm12 == 0x103) return OperationId::SHA256SIG1;
        if (imm12 == 0x104) return OperationId::SHA512SUM0;
        if (imm12 == 0x105) return OperationId::SHA512SUM1;
        if (imm12 == 0x106) return OperationId::SHA512SIG0;
        if (imm12 == 0x107) return OperationId::SHA512SIG1;
        if (imm12 == 0x108) return OperationId::SM3P0;
        if (imm12 == 0x109) return OperationId::SM3P1;
        if (imm12 == 0x300) return OperationId::AES64IM;
        if ((imm12 >> 4) == 0x31) {
            const uint32_t rnum = imm12 & 0xFU;
            if (rnum <= 10) return OperationId::AES64KS1I;
            return OperationId::ILLEGAL;
        }
        if (imm12 == 0x600) return OperationId::CLZ;
        if (imm12 == 0x601) return OperationId::CTZ;
        if (imm12 == 0x602) return OperationId::CPOP;
        if (imm12 == 0x604) return OperationId::SEXT_B;
        if (imm12 == 0x605) return OperationId::SEXT_H;
        if (imm6 == 0x0A) return OperationId::BSETI;
        if (imm6 == 0x12) return OperationId::BCLRI;
        if (imm6 == 0x1A) return OperationId::BINVI;
        if (funct7 == 0x00 || funct7 == 0x01) return OperationId::SLLI;
        return OperationId::ILLEGAL;
    }
    if (funct3 == 5) {
        if (imm12 == 0x287) return OperationId::ORC_B;
        if (imm12 == 0x698 || imm12 == 0x6b8) return OperationId::REV8;
        if (imm6 == 0x18) return OperationId::RORI;
        if (imm6 == 0x12) return OperationId::BEXTI;
        if (funct7 == 0x00 || funct7 == 0x01) return OperationId::SRLI;
        if (funct7 == 0x20 || funct7 == 0x21) return OperationId::SRAI;
        return OperationId::ILLEGAL;
    }
    return OperationId::ILLEGAL;
}

consteval auto generate_op_standard_table() {
    DecodeTable<OperationId, 8, 128, OperationId::ILLEGAL> table;
    // funct3 = 0
    table.assign(0, 0x00, OperationId::ADD);
    table.assign(0, 0x20, OperationId::SUB);

    // Track 2B: RV64 AES
    table.assign(0, 0x19, OperationId::AES64ES);
    table.assign(0, 0x1B, OperationId::AES64ESM);
    table.assign(0, 0x1D, OperationId::AES64DS);
    table.assign(0, 0x1F, OperationId::AES64DSM);
    table.assign(0, 0x3F, OperationId::AES64KS2);

    // Track 2B: RV32 SHA-512
    table.assign(0, 0x28, OperationId::SHA512SUM0R);
    table.assign(0, 0x29, OperationId::SHA512SUM1R);
    table.assign(0, 0x2A, OperationId::SHA512SIG0L);
    table.assign(0, 0x2B, OperationId::SHA512SIG1L);
    table.assign(0, 0x2E, OperationId::SHA512SIG0H);
    table.assign(0, 0x2F, OperationId::SHA512SIG1H);

    // Track 2B: RV32 AES Byte-Select (bs = 0..3)
    table.assign(0, 0x11, OperationId::AES32ESI);
    table.assign(0, 0x31, OperationId::AES32ESI);
    table.assign(0, 0x51, OperationId::AES32ESI);
    table.assign(0, 0x71, OperationId::AES32ESI);

    table.assign(0, 0x13, OperationId::AES32ESMI);
    table.assign(0, 0x33, OperationId::AES32ESMI);
    table.assign(0, 0x53, OperationId::AES32ESMI);
    table.assign(0, 0x73, OperationId::AES32ESMI);

    table.assign(0, 0x15, OperationId::AES32DSI);
    table.assign(0, 0x35, OperationId::AES32DSI);
    table.assign(0, 0x55, OperationId::AES32DSI);
    table.assign(0, 0x75, OperationId::AES32DSI);

    table.assign(0, 0x17, OperationId::AES32DSMI);
    table.assign(0, 0x37, OperationId::AES32DSMI);
    table.assign(0, 0x57, OperationId::AES32DSMI);
    table.assign(0, 0x77, OperationId::AES32DSMI);

    // Track 2B: ShangMi SM4 Byte-Select (bs = 0..3)
    table.assign(0, 0x18, OperationId::SM4ED);
    table.assign(0, 0x38, OperationId::SM4ED);
    table.assign(0, 0x58, OperationId::SM4ED);
    table.assign(0, 0x78, OperationId::SM4ED);

    table.assign(0, 0x1A, OperationId::SM4KS);
    table.assign(0, 0x3A, OperationId::SM4KS);
    table.assign(0, 0x5A, OperationId::SM4KS);
    table.assign(0, 0x7A, OperationId::SM4KS);

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
    table.assign(5, 0x07, OperationId::CZERO_EQZ);
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
    table.assign(7, 0x07, OperationId::CZERO_NEZ);
    table.assign(7, 0x20, OperationId::ANDN);

    return table;
}

constexpr auto kOpStandardTable = generate_op_standard_table();

[[nodiscard]] constexpr auto decode_op_standard(uint32_t funct3, uint32_t funct7) noexcept
    -> OperationId {
    if (funct3 < 8 && funct7 < 128) {
        return kOpStandardTable.lookup(funct3, funct7);
    }
    return OperationId::ILLEGAL;
}

auto decode_op_imm32(uint32_t funct3, uint32_t funct7, simrv::xlen::Instruction ir) -> OperationId {
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
            return OperationId::ILLEGAL;
        case 5:
            if (imm6 == 0x18) return OperationId::RORIW;
            if (funct7 == 0x00) return OperationId::SRLIW;
            if (funct7 == 0x20) return OperationId::SRAIW;
            return OperationId::ILLEGAL;
        default:
            return OperationId::ILLEGAL;
    }
}

consteval auto generate_op32_standard_table() {
    DecodeTable<OperationId, 8, 128, OperationId::ILLEGAL> table;
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
    return OperationId::ILLEGAL;
}

auto decode_system_priv(uint32_t funct7, simrv::xlen::Instruction ir) -> OperationId {
    const uint32_t f12 = ir >> 20;
    const bool rd_is_zero = ((ir >> 7U) & 0x1FU) == 0;
    const bool rs1_is_zero = ((ir >> 15U) & 0x1FU) == 0;
    switch (f12) {
        case 0x000:
            return (rd_is_zero && rs1_is_zero) ? OperationId::ECALL : OperationId::ILLEGAL;
        case 0x001:
            return (rd_is_zero && rs1_is_zero) ? OperationId::EBREAK : OperationId::ILLEGAL;
        case 0x002:
            // URET belonged to the abandoned draft N extension.
            return OperationId::ILLEGAL;
        case 0x102:
            return (rd_is_zero && rs1_is_zero) ? OperationId::SRET : OperationId::ILLEGAL;
        case 0x302:
            return (rd_is_zero && rs1_is_zero) ? OperationId::MRET : OperationId::ILLEGAL;
        case 0x105:
            return (rd_is_zero && rs1_is_zero) ? OperationId::WFI : OperationId::ILLEGAL;
        case 0x00D:
            return (rd_is_zero && rs1_is_zero && ((ir >> 12U) & 0x7U) == 0) ? OperationId::WRS_NTO
                                                                            : OperationId::ILLEGAL;
        case 0x01D:
            return (rd_is_zero && rs1_is_zero && ((ir >> 12U) & 0x7U) == 0) ? OperationId::WRS_STO
                                                                            : OperationId::ILLEGAL;
        case 0x180:
            return (rd_is_zero && rs1_is_zero) ? OperationId::SFENCE_W_INVAL : OperationId::ILLEGAL;
        case 0x181:
            return (rd_is_zero && rs1_is_zero) ? OperationId::SFENCE_INVAL_IR
                                               : OperationId::ILLEGAL;
        default:
            // SFENCE.VMA uses rs1/rs2 operands but reserves rd=x0.
            if (funct7 == 0x09 && rd_is_zero) return OperationId::SFENCE_VMA;
            if (funct7 == 0x0B && rd_is_zero) return OperationId::SINVAL_VMA;
            if (funct7 == 0x11 && rd_is_zero && ((ir >> 12U) & 0x7U) == 0)
                return OperationId::HFENCE_VVMA;
            if (funct7 == 0x13 && rd_is_zero && ((ir >> 12U) & 0x7U) == 0)
                return OperationId::HINVAL_VVMA;
            if (funct7 == 0x31 && rd_is_zero && ((ir >> 12U) & 0x7U) == 0)
                return OperationId::HFENCE_GVMA;
            if (funct7 == 0x33 && rd_is_zero && ((ir >> 12U) & 0x7U) == 0)
                return OperationId::HINVAL_GVMA;
            break;
    }
    return OperationId::ILLEGAL;
}

constexpr std::array<OperationId, 8> kSystemCsrTable = {
    OperationId::ILLEGAL, OperationId::CSRRW,  OperationId::CSRRS,  OperationId::CSRRC,
    OperationId::ILLEGAL, OperationId::CSRRWI, OperationId::CSRRSI, OperationId::CSRRCI,
};

constexpr auto decode_system_csr = decode_table<kSystemCsrTable>;

auto decode_system(uint32_t funct3, uint32_t funct7, simrv::xlen::Instruction ir) -> OperationId {
    if (funct3 == 4) {
        const uint32_t f12 = ir >> 20U;
        switch (f12) {
            case 0x600:
                return OperationId::HLV_B;
            case 0x601:
                return OperationId::HLV_BU;
            case 0x640:
                return OperationId::HLV_H;
            case 0x641:
                return OperationId::HLV_HU;
            case 0x643:
                return OperationId::HLVX_HU;
            case 0x680:
                return OperationId::HLV_W;
            case 0x681:
                return OperationId::HLV_WU;
            case 0x683:
                return OperationId::HLVX_WU;
            case 0x6C0:
                return OperationId::HLV_D;
            default:
                break;
        }
        if (funct7 == 0x31 && ((ir >> 7U) & 0x1FU) == 0) return OperationId::HSV_B;
        if (funct7 == 0x33 && ((ir >> 7U) & 0x1FU) == 0) return OperationId::HSV_H;
        if (funct7 == 0x35 && ((ir >> 7U) & 0x1FU) == 0) return OperationId::HSV_W;
        if (funct7 == 0x37 && ((ir >> 7U) & 0x1FU) == 0) return OperationId::HSV_D;
    }
    if (funct3 == 0) {
        return decode_system_priv(funct7, ir);
    }
    return decode_system_csr(funct3);
}

auto decode_misc_mem(uint32_t funct3, simrv::xlen::Instruction ir) -> OperationId {
    const bool rd_is_zero = ((ir >> 7U) & 0x1FU) == 0;
    const uint32_t f12 = ir >> 20U;
    if (funct3 == 0) {
        return OperationId::FENCE;
    }
    if (funct3 == 1) {
        return OperationId::FENCE_I;
    }
    if (funct3 == 2 && rd_is_zero) {
        switch (f12) {
            case 0x004:
                return OperationId::CBO_ZERO;
            case 0x001:
                return OperationId::CBO_CLEAN;
            case 0x002:
                return OperationId::CBO_FLUSH;
            case 0x000:
                return OperationId::CBO_INVAL;
            default:
                break;
        }
    }
    return OperationId::ILLEGAL;
}

auto decode_ext_i(Opcode op, uint32_t funct3, uint32_t funct7, simrv::xlen::Instruction ir)
    -> OperationId {
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
            return decode_misc_mem(funct3, ir);
        case Opcode::System:
            return decode_system(funct3, funct7, ir);
        default:
            return OperationId::ILLEGAL;
    }
}

constexpr std::array<OperationId, 8> kMTable = {
    OperationId::MUL, OperationId::MULH, OperationId::MULHSU, OperationId::MULHU,
    OperationId::DIV, OperationId::DIVU, OperationId::REM,    OperationId::REMU};

constexpr std::array<OperationId, 8> kMTable32 = {
    OperationId::MULW, OperationId::ILLEGAL, OperationId::ILLEGAL, OperationId::ILLEGAL,
    OperationId::DIVW, OperationId::DIVUW,   OperationId::REMW,    OperationId::REMUW};

constexpr auto decode_ext_m_op = decode_table<kMTable>;
constexpr auto decode_ext_m_op32 = decode_table<kMTable32>;

[[nodiscard]] constexpr auto decode_ext_m(Opcode op, uint32_t funct3) noexcept -> OperationId {
    if (op == Opcode::Op32) {
        return decode_ext_m_op32(funct3);
    }
    return decode_ext_m_op(funct3);
}

consteval auto generate_amo_table() {
    DecodeTable<OperationId, 5, 32, OperationId::ILLEGAL> table;
    // funct3 == 0 (8-bit AMO*B - Zabha / Zacas)
    table.assign(0, 0x00, OperationId::AMOADD_B);
    table.assign(0, 0x01, OperationId::AMOSWAP_B);
    table.assign(0, 0x04, OperationId::AMOXOR_B);
    table.assign(0, 0x05, OperationId::AMOCAS_B);
    table.assign(0, 0x08, OperationId::AMOOR_B);
    table.assign(0, 0x0C, OperationId::AMOAND_B);
    table.assign(0, 0x10, OperationId::AMOMIN_B);
    table.assign(0, 0x14, OperationId::AMOMAX_B);
    table.assign(0, 0x18, OperationId::AMOMINU_B);
    table.assign(0, 0x1C, OperationId::AMOMAXU_B);

    // funct3 == 1 (16-bit AMO*H - Zabha / Zacas)
    table.assign(1, 0x00, OperationId::AMOADD_H);
    table.assign(1, 0x01, OperationId::AMOSWAP_H);
    table.assign(1, 0x04, OperationId::AMOXOR_H);
    table.assign(1, 0x05, OperationId::AMOCAS_H);
    table.assign(1, 0x08, OperationId::AMOOR_H);
    table.assign(1, 0x0C, OperationId::AMOAND_H);
    table.assign(1, 0x10, OperationId::AMOMIN_H);
    table.assign(1, 0x14, OperationId::AMOMAX_H);
    table.assign(1, 0x18, OperationId::AMOMINU_H);
    table.assign(1, 0x1C, OperationId::AMOMAXU_H);

    // funct3 == 2 (32-bit AMO*W)
    table.assign(2, 0x00, OperationId::AMOADD_W);
    table.assign(2, 0x01, OperationId::AMOSWAP_W);
    table.assign(2, 0x02, OperationId::LR_W);
    table.assign(2, 0x03, OperationId::SC_W);
    table.assign(2, 0x04, OperationId::AMOXOR_W);
    table.assign(2, 0x05, OperationId::AMOCAS_W);
    table.assign(2, 0x08, OperationId::AMOOR_W);
    table.assign(2, 0x0C, OperationId::AMOAND_W);
    table.assign(2, 0x10, OperationId::AMOMIN_W);
    table.assign(2, 0x14, OperationId::AMOMAX_W);
    table.assign(2, 0x18, OperationId::AMOMINU_W);
    table.assign(2, 0x1C, OperationId::AMOMAXU_W);

    // funct3 == 3 (64-bit AMO*D)
    table.assign(3, 0x00, OperationId::AMOADD_D);
    table.assign(3, 0x01, OperationId::AMOSWAP_D);
    table.assign(3, 0x02, OperationId::LR_D);
    table.assign(3, 0x03, OperationId::SC_D);
    table.assign(3, 0x04, OperationId::AMOXOR_D);
    table.assign(3, 0x05, OperationId::AMOCAS_D);
    table.assign(3, 0x08, OperationId::AMOOR_D);
    table.assign(3, 0x0C, OperationId::AMOAND_D);
    table.assign(3, 0x10, OperationId::AMOMIN_D);
    table.assign(3, 0x14, OperationId::AMOMAX_D);
    table.assign(3, 0x18, OperationId::AMOMINU_D);
    table.assign(3, 0x1C, OperationId::AMOMAXU_D);

    // funct3 == 4 (128-bit AMOCAS.Q - Zacas RV64)
    table.assign(4, 0x05, OperationId::AMOCAS_Q);

    return table;
}

constexpr auto kAmoTable = generate_amo_table();

[[nodiscard]] constexpr auto decode_ext_a(simrv::xlen::Funct7 funct7, isa::Funct3 funct3,
                                          simrv::xlen::RegId rd, simrv::xlen::RegId rs2) noexcept
    -> OperationId {
    const auto f3 = std::to_underlying(funct3);
    if (f3 > 4) {
        return OperationId::ILLEGAL;
    }
    const auto funct5 = static_cast<isa::Funct5Amo>(funct7 >> 2);
    // LR.W/LR.D reserve rs2=0; nonzero rs2 is a reserved instruction encoding.
    if (funct5 == isa::Funct5Amo::Lr && rs2 != simrv::xlen::RegId::Zero) {
        return OperationId::ILLEGAL;
    }
    const bool pair_unaligned =
        ((std::to_underlying(rd) & 1U) != 0U) || ((std::to_underlying(rs2) & 1U) != 0U);
    if (funct5 == isa::Funct5Amo::Cas) {
        if (f3 == 4) {
            if constexpr (!simrv::xlen::kIsXLen64) {
                return OperationId::ILLEGAL;
            }
            if (pair_unaligned) {
                return OperationId::ILLEGAL;
            }
        } else if (f3 == 3) {
            if constexpr (!simrv::xlen::kIsXLen64) {
                if (pair_unaligned) {
                    return OperationId::ILLEGAL;
                }
            }
        }
    } else {
        if (f3 == 4 || (!simrv::xlen::kIsXLen64 && f3 == 3)) {
            return OperationId::ILLEGAL;
        }
    }
    const auto funct5_idx = std::to_underlying(funct5);
    if (funct5_idx < 32) {
        return kAmoTable.lookup(f3, funct5_idx);
    }
    return OperationId::ILLEGAL;
}

consteval auto generate_fma_table() {
    DecodeTable<OperationId, 128, 3, OperationId::ILLEGAL> table;
    table.assign(std::to_underlying(Opcode::MAdd), 0, OperationId::FMADD_S);
    table.assign(std::to_underlying(Opcode::MAdd), 1, OperationId::FMADD_D);
    table.assign(std::to_underlying(Opcode::MAdd), 2, OperationId::FMADD_H);
    table.assign(std::to_underlying(Opcode::MSub), 0, OperationId::FMSUB_S);
    table.assign(std::to_underlying(Opcode::MSub), 1, OperationId::FMSUB_D);
    table.assign(std::to_underlying(Opcode::MSub), 2, OperationId::FMSUB_H);
    table.assign(std::to_underlying(Opcode::NMSub), 0, OperationId::FNMSUB_S);
    table.assign(std::to_underlying(Opcode::NMSub), 1, OperationId::FNMSUB_D);
    table.assign(std::to_underlying(Opcode::NMSub), 2, OperationId::FNMSUB_H);
    table.assign(std::to_underlying(Opcode::NMAdd), 0, OperationId::FNMADD_S);
    table.assign(std::to_underlying(Opcode::NMAdd), 1, OperationId::FNMADD_D);
    table.assign(std::to_underlying(Opcode::NMAdd), 2, OperationId::FNMADD_H);
    return table;
}

constexpr auto kFmaTable = generate_fma_table();

[[nodiscard]] constexpr auto decode_fma(Opcode op, uint32_t funct7) noexcept -> OperationId {
    const uint32_t fmt = funct7 & 0x03;
    if (fmt > 2) {
        return OperationId::ILLEGAL;
    }
    return kFmaTable.lookup(std::to_underlying(op), fmt);
}

consteval auto generate_fp_single_table() {
    DecodeTable<OperationId, 32, 8, OperationId::ILLEGAL> table;
    table.assign_row(0x00, OperationId::FADD_S);
    table.assign_row(0x01, OperationId::FSUB_S);
    table.assign_row(0x02, OperationId::FMUL_S);
    table.assign_row(0x03, OperationId::FDIV_S);
    table.assign(0x04, 0, OperationId::FSGNJ_S);
    table.assign(0x04, 1, OperationId::FSGNJN_S);
    table.assign(0x04, 2, OperationId::FSGNJX_S);
    table.assign(0x05, 0, OperationId::FMIN_S);
    table.assign(0x05, 1, OperationId::FMAX_S);
    table.assign(0x05, 2, OperationId::FMINM_S);
    table.assign(0x05, 3, OperationId::FMAXM_S);
    table.assign_row(0x0B, OperationId::FSQRT_S);
    table.assign(0x14, 0, OperationId::FLE_S);
    table.assign(0x14, 1, OperationId::FLT_S);
    table.assign(0x14, 2, OperationId::FEQ_S);
    table.assign(0x14, 4, OperationId::FLEQ_S);
    table.assign(0x14, 5, OperationId::FLTQ_S);
    table.assign(0x1C, 0, OperationId::FMV_X_W);
    table.assign(0x1C, 1, OperationId::FCLASS_S);
    table.assign(0x1E, 0, OperationId::FMV_W_X);
    return table;
}

constexpr auto kFpSingleTable = generate_fp_single_table();

auto decode_op_fp_single_fcvt(uint32_t f5, uint32_t rs2) -> OperationId {
    switch (f5) {
        case 0x08:
            return (rs2 == 1) ? OperationId::FCVT_S_D : OperationId::ILLEGAL;
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
    return OperationId::ILLEGAL;
}

auto decode_op_fp_single(uint32_t funct3, uint32_t f5, uint32_t rs2) -> OperationId {
    if (f5 == 0x1E && rs2 == 1 && funct3 == 0) return OperationId::FLI_S;
    if (f5 == 0x08 && (rs2 == 4 || rs2 == 5))
        return rs2 == 4 ? OperationId::FROUND_S : OperationId::FROUNDNX_S;
    if (f5 == 0x08 || f5 == 0x18 || f5 == 0x1A) {
        return decode_op_fp_single_fcvt(f5, rs2);
    }
    if ((f5 == 0x0B || f5 == 0x1C || f5 == 0x1E) && rs2 != 0) {
        return OperationId::ILLEGAL;
    }
    return kFpSingleTable.lookup(f5, funct3);
}

consteval auto generate_fp_double_table() {
    DecodeTable<OperationId, 32, 8, OperationId::ILLEGAL> table;
    table.assign_row(0x00, OperationId::FADD_D);
    table.assign_row(0x01, OperationId::FSUB_D);
    table.assign_row(0x02, OperationId::FMUL_D);
    table.assign_row(0x03, OperationId::FDIV_D);
    table.assign(0x04, 0, OperationId::FSGNJ_D);
    table.assign(0x04, 1, OperationId::FSGNJN_D);
    table.assign(0x04, 2, OperationId::FSGNJX_D);
    table.assign(0x05, 0, OperationId::FMIN_D);
    table.assign(0x05, 1, OperationId::FMAX_D);
    table.assign(0x05, 2, OperationId::FMINM_D);
    table.assign(0x05, 3, OperationId::FMAXM_D);
    table.assign_row(0x0B, OperationId::FSQRT_D);
    table.assign(0x14, 0, OperationId::FLE_D);
    table.assign(0x14, 1, OperationId::FLT_D);
    table.assign(0x14, 2, OperationId::FEQ_D);
    table.assign(0x14, 4, OperationId::FLEQ_D);
    table.assign(0x14, 5, OperationId::FLTQ_D);
    table.assign(0x1C, 0, OperationId::FMV_X_D);
    table.assign(0x1C, 1, OperationId::FCLASS_D);
    table.assign(0x1E, 0, OperationId::FMV_D_X);
    return table;
}

constexpr auto kFpDoubleTable = generate_fp_double_table();

auto decode_op_fp_double_fcvt(uint32_t funct3, uint32_t f5, uint32_t rs2) -> OperationId {
    if (f5 == 0x18 && rs2 == 8 && funct3 == 1) return OperationId::FCVTMOD_W_D;
    switch (f5) {
        case 0x08:
            // Widening FCVT.D.S is exact and reserves the rm field as zero.
            return (rs2 == 0 && funct3 == 0) ? OperationId::FCVT_D_S : OperationId::ILLEGAL;
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
    return OperationId::ILLEGAL;
}

auto decode_op_fp_double(uint32_t funct3, uint32_t f5, uint32_t rs2) -> OperationId {
    if (f5 == 0x1E && rs2 == 1 && funct3 == 0) return OperationId::FLI_D;
    if (f5 == 0x08 && (rs2 == 4 || rs2 == 5))
        return rs2 == 4 ? OperationId::FROUND_D : OperationId::FROUNDNX_D;
    if (f5 == 0x08 || f5 == 0x18 || f5 == 0x1A) {
        return decode_op_fp_double_fcvt(funct3, f5, rs2);
    }
    if ((f5 == 0x0B || f5 == 0x1C || f5 == 0x1E) && rs2 != 0) {
        return OperationId::ILLEGAL;
    }
    return kFpDoubleTable.lookup(f5, funct3);
}

auto decode_op_fp(uint32_t funct3, uint32_t funct7, uint32_t rs2) -> OperationId {
    const uint32_t fmt = funct7 & 0x03;
    const uint32_t f5 = funct7 >> 2;
    if (fmt == 0) {
        if (f5 == 0x08 && rs2 == 2 && funct3 == 0) return OperationId::FCVT_S_H;
        if (f5 == 0x08 && rs2 == 6) return OperationId::FCVT_S_BF16;
        return decode_op_fp_single(funct3, f5, rs2);
    }
    if (fmt == 1) {
        if (f5 == 0x08 && rs2 == 2 && funct3 == 0) return OperationId::FCVT_D_H;
        return decode_op_fp_double(funct3, f5, rs2);
    }
    if (fmt == 2) {
        switch (f5) {
            case 0x00:
                return OperationId::FADD_H;
            case 0x01:
                return OperationId::FSUB_H;
            case 0x02:
                return OperationId::FMUL_H;
            case 0x03:
                return OperationId::FDIV_H;
            case 0x04:
                if (funct3 == 0) return OperationId::FSGNJ_H;
                if (funct3 == 1) return OperationId::FSGNJN_H;
                if (funct3 == 2) return OperationId::FSGNJX_H;
                return OperationId::ILLEGAL;
            case 0x05:
                if (funct3 == 0) return OperationId::FMIN_H;
                if (funct3 == 1) return OperationId::FMAX_H;
                return OperationId::ILLEGAL;
            case 0x08:
                if (rs2 == 0) return OperationId::FCVT_H_S;
                if (rs2 == 1) return OperationId::FCVT_H_D;
                if (rs2 == 8) return OperationId::FCVT_BF16_S;
                return OperationId::ILLEGAL;
            case 0x0B:
                return rs2 == 0 ? OperationId::FSQRT_H : OperationId::ILLEGAL;
            case 0x14:
                if (funct3 == 2) return OperationId::FEQ_H;
                if (funct3 == 1) return OperationId::FLT_H;
                if (funct3 == 0) return OperationId::FLE_H;
                return OperationId::ILLEGAL;
            case 0x18:
                if (rs2 == 0) return OperationId::FCVT_W_H;
                if (rs2 == 1) return OperationId::FCVT_WU_H;
                if (rs2 == 2) return OperationId::FCVT_L_H;
                if (rs2 == 3) return OperationId::FCVT_LU_H;
                return OperationId::ILLEGAL;
            case 0x1A:
                if (rs2 == 0) return OperationId::FCVT_H_W;
                if (rs2 == 1) return OperationId::FCVT_H_WU;
                if (rs2 == 2) return OperationId::FCVT_H_L;
                if (rs2 == 3) return OperationId::FCVT_H_LU;
                return OperationId::ILLEGAL;
            case 0x1C:
                if (rs2 == 0 && funct3 == 0) return OperationId::FMV_X_H;
                if (rs2 == 0 && funct3 == 1) return OperationId::FCLASS_H;
                return OperationId::ILLEGAL;
            case 0x1E:
                return (rs2 == 0 && funct3 == 0) ? OperationId::FMV_H_X : OperationId::ILLEGAL;
            default:
                return OperationId::ILLEGAL;
        }
    }
    return OperationId::ILLEGAL;
}

namespace {

consteval auto generate_vector_common_table() {
    DecodeTable<OperationId, 64, 8, OperationId::ILLEGAL> table;
    table.assign_row(0x00, {{0, OperationId::VADD_VV},
                            {4, OperationId::VADD_VX},
                            {3, OperationId::VADD_VI},
                            {1, OperationId::VFADD_VV},
                            {5, OperationId::VFADD_VF},
                            {2, OperationId::VREDSUM_VS}});
    table.assign_row(0x01, {{0, OperationId::VANDN_VV},
                            {4, OperationId::VANDN_VX},
                            {2, OperationId::VREDAND_VS},
                            {1, OperationId::VFREDUSUM_VS}});
    table.assign_row(0x02, {{0, OperationId::VSUB_VV},
                            {4, OperationId::VSUB_VX},
                            {2, OperationId::VREDOR_VS},
                            {1, OperationId::VFSUB_VV},
                            {5, OperationId::VFSUB_VF}});
    table.assign_row(0x03, {{4, OperationId::VRSUB_VX},
                            {3, OperationId::VRSUB_VI},
                            {2, OperationId::VREDXOR_VS},
                            {1, OperationId::VFREDOSUM_VS}});
    table.assign_row(0x04, {{0, OperationId::VMINU_VV},
                            {4, OperationId::VMINU_VX},
                            {2, OperationId::VREDMINU_VS},
                            {1, OperationId::VFMIN_VV},
                            {5, OperationId::VFMIN_VF}});
    table.assign_row(0x05, {{0, OperationId::VMIN_VV},
                            {4, OperationId::VMIN_VX},
                            {2, OperationId::VREDMIN_VS},
                            {1, OperationId::VFREDMIN_VS}});
    table.assign_row(0x06, {{0, OperationId::VMAXU_VV},
                            {4, OperationId::VMAXU_VX},
                            {2, OperationId::VREDMAXU_VS},
                            {1, OperationId::VFMAX_VV},
                            {5, OperationId::VFMAX_VF}});
    table.assign_row(0x07, {{0, OperationId::VMAX_VV},
                            {4, OperationId::VMAX_VX},
                            {2, OperationId::VREDMAX_VS},
                            {1, OperationId::VFREDMAX_VS}});
    table.assign_row(0x08, {{2, OperationId::VAADDU_VV},
                            {6, OperationId::VAADDU_VX},
                            {1, OperationId::VFSGNJ_VV},
                            {5, OperationId::VFSGNJ_VF}});
    table.assign_row(0x09, {{0, OperationId::VAND_VV},
                            {4, OperationId::VAND_VX},
                            {3, OperationId::VAND_VI},
                            {2, OperationId::VAADD_VV},
                            {6, OperationId::VAADD_VX},
                            {1, OperationId::VFSGNJN_VV},
                            {5, OperationId::VFSGNJN_VF}});
    table.assign_row(0x0A, {{0, OperationId::VOR_VV},
                            {4, OperationId::VOR_VX},
                            {3, OperationId::VOR_VI},
                            {2, OperationId::VASUBU_VV},
                            {6, OperationId::VASUBU_VX},
                            {1, OperationId::VFSGNJX_VV},
                            {5, OperationId::VFSGNJX_VF}});
    table.assign_row(0x0B, {{0, OperationId::VXOR_VV},
                            {4, OperationId::VXOR_VX},
                            {3, OperationId::VXOR_VI},
                            {2, OperationId::VASUB_VV},
                            {6, OperationId::VASUB_VX}});
    table.assign_row(0x0C, {{0, OperationId::VRGATHER_VV},
                            {4, OperationId::VRGATHER_VX},
                            {3, OperationId::VRGATHER_VI},
                            {2, OperationId::VCLMUL_VV},
                            {6, OperationId::VCLMUL_VX}});
    table.assign_row(0x0D, {{2, OperationId::VCLMULH_VV}, {6, OperationId::VCLMULH_VX}});
    table.assign_row(0x0E, {{0, OperationId::VRGATHEREI16_VV},
                            {6, OperationId::VSLIDE1UP_VX},
                            {5, OperationId::VFSLIDE1UP_VF},
                            {4, OperationId::VSLIDEUP_VX},
                            {3, OperationId::VSLIDEUP_VI}});
    table.assign_row(0x0F, {{6, OperationId::VSLIDE1DOWN_VX},
                            {5, OperationId::VFSLIDE1DOWN_VF},
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
                            {2, OperationId::VMANDN_MM},
                            {1, OperationId::VMFEQ_VV},
                            {5, OperationId::VMFEQ_VF}});
    table.assign_row(0x19, {{0, OperationId::VMSNE_VV},
                            {4, OperationId::VMSNE_VX},
                            {3, OperationId::VMSNE_VI},
                            {2, OperationId::VMAND_MM},
                            {1, OperationId::VMFLE_VV},
                            {5, OperationId::VMFLE_VF}});
    table.assign_row(
        0x1A,
        {{0, OperationId::VMSLTU_VV}, {4, OperationId::VMSLTU_VX}, {2, OperationId::VMOR_MM}});
    table.assign_row(0x1B, {{0, OperationId::VMSLT_VV},
                            {4, OperationId::VMSLT_VX},
                            {2, OperationId::VMXOR_MM},
                            {1, OperationId::VMFLT_VV},
                            {5, OperationId::VMFLT_VF}});
    table.assign_row(0x1C, {{0, OperationId::VMSLEU_VV},
                            {4, OperationId::VMSLEU_VX},
                            {3, OperationId::VMSLEU_VI},
                            {2, OperationId::VMORN_MM},
                            {1, OperationId::VMFNE_VV},
                            {5, OperationId::VMFNE_VF}});
    table.assign_row(0x1D, {{0, OperationId::VMSLE_VV},
                            {4, OperationId::VMSLE_VX},
                            {3, OperationId::VMSLE_VI},
                            {2, OperationId::VMNAND_MM},
                            {5, OperationId::VMFGT_VF}});
    table.assign_row(
        0x1E,
        {{4, OperationId::VMSGTU_VX}, {3, OperationId::VMSGTU_VI}, {2, OperationId::VMNOR_MM}});
    table.assign_row(0x1F, {{4, OperationId::VMSGT_VX},
                            {3, OperationId::VMSGT_VI},
                            {2, OperationId::VMXNOR_MM},
                            {5, OperationId::VMFGE_VF}});
    table.assign_row(0x20, {{2, OperationId::VDIVU_VV},
                            {6, OperationId::VDIVU_VX},
                            {0, OperationId::VSADDU_VV},
                            {4, OperationId::VSADDU_VX},
                            {3, OperationId::VSADDU_VI},
                            {1, OperationId::VFDIV_VV},
                            {5, OperationId::VFDIV_VF}});
    table.assign_row(0x21, {{2, OperationId::VDIV_VV},
                            {6, OperationId::VDIV_VX},
                            {0, OperationId::VSADD_VV},
                            {4, OperationId::VSADD_VX},
                            {3, OperationId::VSADD_VI},
                            {5, OperationId::VFRDIV_VF}});
    table.assign_row(0x22, {{2, OperationId::VREMU_VV},
                            {6, OperationId::VREMU_VX},
                            {0, OperationId::VSSUBU_VV},
                            {4, OperationId::VSSUBU_VX}});
    table.assign_row(0x23, {{2, OperationId::VREM_VV},
                            {6, OperationId::VREM_VX},
                            {0, OperationId::VSSUB_VV},
                            {4, OperationId::VSSUB_VX}});
    table.assign_row(0x24, {{2, OperationId::VMULHU_VV},
                            {6, OperationId::VMULHU_VX},
                            {1, OperationId::VFMUL_VV},
                            {5, OperationId::VFMUL_VF}});
    table.assign_row(0x25, {{0, OperationId::VSLL_VV},
                            {4, OperationId::VSLL_VX},
                            {3, OperationId::VSLL_VI},
                            {2, OperationId::VMUL_VV},
                            {6, OperationId::VMUL_VX}});
    table.assign_row(0x26, {{2, OperationId::VMULHSU_VV}, {6, OperationId::VMULHSU_VX}});
    table.assign_row(0x27, {{2, OperationId::VMULH_VV},
                            {6, OperationId::VMULH_VX},
                            {0, OperationId::VSMUL_VV},
                            {4, OperationId::VSMUL_VX},
                            {5, OperationId::VFRSUB_VF}});
    table.assign_row(0x28, {{0, OperationId::VSRL_VV},
                            {4, OperationId::VSRL_VX},
                            {3, OperationId::VSRL_VI},
                            {1, OperationId::VFMADD_VV},
                            {5, OperationId::VFMADD_VF}});
    table.assign_row(0x29, {{0, OperationId::VSRA_VV},
                            {4, OperationId::VSRA_VX},
                            {3, OperationId::VSRA_VI},
                            {2, OperationId::VMADD_VV},
                            {6, OperationId::VMADD_VX},
                            {1, OperationId::VFNMADD_VV},
                            {5, OperationId::VFNMADD_VF}});
    table.assign_row(0x2A, {{0, OperationId::VSSRL_VV},
                            {4, OperationId::VSSRL_VX},
                            {3, OperationId::VSSRL_VI},
                            {1, OperationId::VFMSUB_VV},
                            {5, OperationId::VFMSUB_VF}});
    table.assign_row(0x2B, {{2, OperationId::VNMSUB_VV},
                            {6, OperationId::VNMSUB_VX},
                            {0, OperationId::VSSRA_VV},
                            {4, OperationId::VSSRA_VX},
                            {3, OperationId::VSSRA_VI},
                            {1, OperationId::VFNMSUB_VV},
                            {5, OperationId::VFNMSUB_VF}});
    table.assign_row(0x2C, {{0, OperationId::VNSRL_WV},
                            {4, OperationId::VNSRL_WX},
                            {3, OperationId::VNSRL_WI},
                            {1, OperationId::VFMACC_VV},
                            {5, OperationId::VFMACC_VF}});
    table.assign_row(0x2D, {{0, OperationId::VNSRA_WV},
                            {4, OperationId::VNSRA_WX},
                            {3, OperationId::VNSRA_WI},
                            {2, OperationId::VMACC_VV},
                            {6, OperationId::VMACC_VX},
                            {1, OperationId::VFNMACC_VV},
                            {5, OperationId::VFNMACC_VF}});
    table.assign_row(0x2E, {{0, OperationId::VNCLIPU_WV},
                            {4, OperationId::VNCLIPU_WX},
                            {3, OperationId::VNCLIPU_WI},
                            {1, OperationId::VFMSAC_VV},
                            {5, OperationId::VFMSAC_VF}});
    table.assign_row(0x2F, {{2, OperationId::VNMSAC_VV},
                            {6, OperationId::VNMSAC_VX},
                            {0, OperationId::VNCLIP_WV},
                            {4, OperationId::VNCLIP_WX},
                            {3, OperationId::VNCLIP_WI},
                            {1, OperationId::VFNMSAC_VV},
                            {5, OperationId::VFNMSAC_VF}});
    table.assign_row(0x30, {{2, OperationId::VWADDU_VV},
                            {6, OperationId::VWADDU_VX},
                            {0, OperationId::VWREDSUMU_VS},
                            {1, OperationId::VFWADD_VV},
                            {5, OperationId::VFWADD_VF}});
    table.assign_row(0x31, {{2, OperationId::VWADD_VV},
                            {6, OperationId::VWADD_VX},
                            {0, OperationId::VWREDSUM_VS},
                            {1, OperationId::VFWREDUSUM_VS}});
    table.assign_row(0x32, {{2, OperationId::VWSUBU_VV},
                            {6, OperationId::VWSUBU_VX},
                            {1, OperationId::VFWSUB_VV},
                            {5, OperationId::VFWSUB_VF}});
    table.assign_row(
        0x33,
        {{2, OperationId::VWSUB_VV}, {6, OperationId::VWSUB_VX}, {1, OperationId::VFWREDOSUM_VS}});
    table.assign_row(0x34, {{2, OperationId::VWADDU_WV},
                            {6, OperationId::VWADDU_WX},
                            {1, OperationId::VFWADD_WV},
                            {5, OperationId::VFWADD_WF}});
    table.assign_row(0x35, {{2, OperationId::VWADD_WV},
                            {6, OperationId::VWADD_WX},
                            {0, OperationId::VWSLL_VV},
                            {4, OperationId::VWSLL_VX},
                            {3, OperationId::VWSLL_VI}});
    table.assign_row(0x36, {{2, OperationId::VWSUBU_WV},
                            {6, OperationId::VWSUBU_WX},
                            {1, OperationId::VFWSUB_WV},
                            {5, OperationId::VFWSUB_WF}});
    table.assign_row(0x37, {{2, OperationId::VWSUB_WV}, {6, OperationId::VWSUB_WX}});
    table.assign_row(0x38, {{2, OperationId::VWMULU_VV},
                            {6, OperationId::VWMULU_VX},
                            {1, OperationId::VFWMUL_VV},
                            {5, OperationId::VFWMUL_VF}});
    table.assign_row(0x3A, {{2, OperationId::VWMULSU_VV}, {6, OperationId::VWMULSU_VX}});
    table.assign_row(0x3B, {{1, OperationId::VFWMACCBF16_VV},
                            {5, OperationId::VFWMACCBF16_VF},
                            {2, OperationId::VWMUL_VV},
                            {6, OperationId::VWMUL_VX}});
    table.assign_row(0x3C, {{2, OperationId::VWMACCU_VV},
                            {6, OperationId::VWMACCU_VX},
                            {1, OperationId::VFWMACC_VV},
                            {5, OperationId::VFWMACC_VF}});
    table.assign_row(0x3D, {{2, OperationId::VWMACC_VV},
                            {6, OperationId::VWMACC_VX},
                            {1, OperationId::VFWNMACC_VV},
                            {5, OperationId::VFWNMACC_VF}});
    table.assign_row(0x3E, {{6, OperationId::VWMACCUS_VX},
                            {1, OperationId::VFWMSAC_VV},
                            {5, OperationId::VFWMSAC_VF}});
    table.assign_row(0x3F, {{2, OperationId::VWMACCSU_VV},
                            {6, OperationId::VWMACCSU_VX},
                            {1, OperationId::VFWNMSAC_VV},
                            {5, OperationId::VFWNMSAC_VF}});
    return table;
}

consteval auto generate_vector_mask_table(bool vm) {
    DecodeTable<OperationId, 64, 8, OperationId::ILLEGAL> table;
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

auto decode_ext_v_special(uint32_t f6, uint32_t funct3, simrv::xlen::Instruction ir)
    -> OperationId {
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
            if (funct3 == 1) {
                const uint32_t vs1 = (ir >> 15U) & 0x1FU;
                if (vs1 == 0) return OperationId::VFCVT_XU_F_V;
                if (vs1 == 1) return OperationId::VFCVT_X_F_V;
                if (vs1 == 2) return OperationId::VFCVT_F_XU_V;
                if (vs1 == 3) return OperationId::VFCVT_F_X_V;
                if (vs1 == 6) return OperationId::VFCVT_RTZ_XU_F_V;
                if (vs1 == 7) return OperationId::VFCVT_RTZ_X_F_V;
                if (vs1 == 8) return OperationId::VFWCVT_XU_F_V;
                if (vs1 == 9) return OperationId::VFWCVT_X_F_V;
                if (vs1 == 10) return OperationId::VFWCVT_F_XU_V;
                if (vs1 == 11) return OperationId::VFWCVT_F_X_V;
                if (vs1 == 12) return OperationId::VFWCVT_F_F_V;
                if (vs1 == 13) return OperationId::VFWCVTBF16_F_F_V;
                if (vs1 == 14) return OperationId::VFWCVT_RTZ_XU_F_V;
                if (vs1 == 15) return OperationId::VFWCVT_RTZ_X_F_V;
                if (vs1 == 16) return OperationId::VFNCVT_XU_F_W;
                if (vs1 == 17) return OperationId::VFNCVT_X_F_W;
                if (vs1 == 18) return OperationId::VFNCVT_F_XU_W;
                if (vs1 == 19) return OperationId::VFNCVT_F_X_W;
                if (vs1 == 20) return OperationId::VFNCVT_F_F_W;
                if (vs1 == 21) return OperationId::VFNCVT_ROD_F_F_W;
                if (vs1 == 22) return OperationId::VFNCVT_RTZ_XU_F_W;
                if (vs1 == 23) return OperationId::VFNCVT_RTZ_X_F_W;
                if (vs1 == 29) return OperationId::VFNCVTBF16_F_F_W;
            } else if (funct3 == 2) {
                uint32_t rs1_val = (ir >> 15) & 0x1F;
                if (rs1_val == 7) return OperationId::VSEXT_VF2;
                if (rs1_val == 5) return OperationId::VSEXT_VF4;
                if (rs1_val == 3) return OperationId::VSEXT_VF8;
                if (rs1_val == 6) return OperationId::VZEXT_VF2;
                if (rs1_val == 4) return OperationId::VZEXT_VF4;
                if (rs1_val == 2) return OperationId::VZEXT_VF8;
                if (rs1_val == 8) return OperationId::VBREV8_V;
                if (rs1_val == 9) return OperationId::VREV8_V;
                if (rs1_val == 10) return OperationId::VBREV_V;
                if (rs1_val == 12) return OperationId::VCLZ_V;
                if (rs1_val == 13) return OperationId::VCTZ_V;
                if (rs1_val == 14) return OperationId::VCPOP_V;
            }
            break;
        case 0x13:
            if (funct3 == 1) {
                const uint32_t vs1 = (ir >> 15U) & 0x1FU;
                if (vs1 == 0) return OperationId::VFSQRT_V;
                if (vs1 == 4) return OperationId::VFRSQRT7_V;
                if (vs1 == 5) return OperationId::VFREC7_V;
                if (vs1 == 16) return OperationId::VFCLASS_V;
            }
            break;
        case 0x14:
            if (funct3 == 2) {
                uint32_t rs1_val = (ir >> 15) & 0x1F;
                if (rs1_val == 1) return OperationId::VMSBF_M;
                if (rs1_val == 2) return OperationId::VMSOF_M;
                if (rs1_val == 3) return OperationId::VMSIF_M;
                if (rs1_val == 16) return OperationId::VIOTA_M;
                if (rs1_val == 17 && ((ir >> 20) & 0x1F) == 0) return OperationId::VID_V;
            }
            break;
        case 0x17:
            if (funct3 == 5 && ((ir >> 20U) & 0x1FU) == 0) return OperationId::VFMV_V_F;
            break;
        default:
            break;
    }
    return OperationId::ILLEGAL;
}

auto decode_vector_whole_register_move(simrv::xlen::Instruction ir) -> OperationId {
    switch ((ir >> 15) & 0x1F) {
        case 0:
            return OperationId::VMV1R_V;
        case 1:
            return OperationId::VMV2R_V;
        case 3:
            return OperationId::VMV4R_V;
        case 7:
            return OperationId::VMV8R_V;
        default:
            return OperationId::ILLEGAL;
    }
}

}  // namespace

auto decode_ext_v(uint32_t funct3, uint32_t funct7, simrv::xlen::Instruction ir) -> OperationId {
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
        return OperationId::ILLEGAL;
    }

    const bool vm = (ir & (1u << 25)) != 0;
    const uint32_t f6 = funct7 >> 1;

    // VROR.VI uses bit 26 as the sixth immediate bit, unlike the other vector
    // encodings that treat it as part of funct6.
    if (funct3 == 3 && (f6 == 0x14 || f6 == 0x15)) return OperationId::VROR_VI;

    // A small number of vector encodings use rs1/vs2 as a secondary opcode.
    if ((f6 == 0x10 && (funct3 == 1 || funct3 == 2 || funct3 == 5)) ||
        (f6 == 0x12 && (funct3 == 1 || funct3 == 2)) || (f6 == 0x14 && funct3 == 2) ||
        (f6 == 0x13 && funct3 == 1) || (vm && f6 == 0x17 && funct3 == 5)) {
        return decode_ext_v_special(f6, funct3, ir);
    }
    if (f6 == 0x27 && funct3 == 3) {
        return vm ? decode_vector_whole_register_move(ir) : OperationId::ILLEGAL;
    }

    const auto mask_dependent = (vm ? kVectorUnmaskedTable : kVectorMaskedTable).lookup(f6, funct3);
    if ((mask_dependent == OperationId::VMV_V_V || mask_dependent == OperationId::VMV_V_X ||
         mask_dependent == OperationId::VMV_V_I) &&
        ((ir >> 20U) & 0x1FU) != 0) {
        return OperationId::ILLEGAL;
    }
    return mask_dependent != OperationId::ILLEGAL ? mask_dependent
                                                  : kVectorCommonTable.lookup(f6, funct3);
}

auto decode_ext_f_d(Opcode op, uint32_t funct3, uint32_t funct7, uint32_t rs2,
                    simrv::xlen::Instruction ir) -> OperationId {
    switch (op) {
        case Opcode::LoadFp: {
            if (funct3 == 1) return OperationId::FLH;
            if (funct3 == 2) return OperationId::FLW;
            if (funct3 == 3) return OperationId::FLD;
            if ((ir & (1U << 28U)) != 0) return OperationId::ILLEGAL;
            uint32_t mop = (ir >> 26) & 3;
            if (mop == 0) {
                uint32_t lumop = (ir >> 20) & 0x1F;
                if (lumop == 8 && ((ir >> 25) & 1) != 0) {
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
                } else if (lumop == 11 && ((ir >> 25) & 1) != 0 && ((ir >> 29) & 7) == 0 &&
                           funct3 == 0) {
                    return OperationId::VLM_V;
                } else if (lumop == 16) {
                    if (funct3 == 0) return OperationId::VLE8FF_V;
                    if (funct3 == 5) return OperationId::VLE16FF_V;
                    if (funct3 == 6) return OperationId::VLE32FF_V;
                    if (funct3 == 7) return OperationId::VLE64FF_V;
                } else if (lumop == 0) {
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
            return OperationId::ILLEGAL;
        }
        case Opcode::StoreFp: {
            if (funct3 == 1) return OperationId::FSH;
            if (funct3 == 2) return OperationId::FSW;
            if (funct3 == 3) return OperationId::FSD;
            if ((ir & (1U << 28U)) != 0) return OperationId::ILLEGAL;
            uint32_t mop = (ir >> 26) & 3;
            if (mop == 0) {
                uint32_t sumop = (ir >> 20) & 0x1F;
                if (sumop == 8 && ((ir >> 25) & 1) != 0) {
                    uint32_t nf = (ir >> 29) & 7;
                    if (funct3 == 0) {
                        if (nf == 0) return OperationId::VS1R_V;
                        if (nf == 1) return OperationId::VS2R_V;
                        if (nf == 3) return OperationId::VS4R_V;
                        if (nf == 7) return OperationId::VS8R_V;
                    }
                } else if (sumop == 11 && ((ir >> 25) & 1) != 0 && ((ir >> 29) & 7) == 0 &&
                           funct3 == 0) {
                    return OperationId::VSM_V;
                } else if (sumop == 0) {
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
            return OperationId::ILLEGAL;
        }
        case Opcode::MAdd:
        case Opcode::MSub:
        case Opcode::NMSub:
        case Opcode::NMAdd:
            return decode_fma(op, funct7);
        case Opcode::OpFp:
            return decode_op_fp(funct3, funct7, rs2);
        default:
            return OperationId::ILLEGAL;
    }
}

}  // namespace

auto decoder(simrv::xlen::Instruction ir) -> OperationId {
    simrv::pipeline::Decoder dec(ir);
    const auto op = dec.opcode();
    const auto funct3 = std::to_underlying(dec.funct3());
    const auto funct7 = dec.funct7();

    // Zimop defines 32 MOP.R and 8 MOP.RR encodings. Until redefined by another
    // extension, each writes zero to rd. Match only the specified funct/opcode fields;
    // rd, rs1, and rs2 remain variable as required by the encoding.
    if (op == Opcode::System && funct3 == 0x4U) {
        static constexpr std::array<uint32_t, 40> kMopMatches{
            0x81c04073U, 0x81d04073U, 0x81e04073U, 0x81f04073U, 0x85c04073U, 0x85d04073U,
            0x85e04073U, 0x85f04073U, 0x89c04073U, 0x89d04073U, 0x89e04073U, 0x89f04073U,
            0x8dc04073U, 0x8dd04073U, 0x8de04073U, 0x8df04073U, 0xc1c04073U, 0xc1d04073U,
            0xc1e04073U, 0xc1f04073U, 0xc5c04073U, 0xc5d04073U, 0xc5e04073U, 0xc5f04073U,
            0xc9c04073U, 0xc9d04073U, 0xc9e04073U, 0xc9f04073U, 0xcdc04073U, 0xcdd04073U,
            0xcde04073U, 0xcdf04073U, 0x82004073U, 0x86004073U, 0x8a004073U, 0x8e004073U,
            0xc2004073U, 0xc6004073U, 0xca004073U, 0xce004073U};
        constexpr uint32_t kMopRMask = 0xfff0707fU;
        constexpr uint32_t kMopRrMask = 0xfe00707fU;
        for (size_t index = 0; index < kMopMatches.size(); ++index) {
            const uint32_t mask = index < 32 ? kMopRMask : kMopRrMask;
            if ((ir & mask) == kMopMatches[index]) return OperationId::MOP;
        }
    }

    if (op == Opcode::Amo) {
        return decode_ext_a(funct7, dec.funct3(), dec.rd(), dec.rs2());
    }
    if (op == Opcode::LoadFp || op == Opcode::StoreFp || op == Opcode::OpFp || op == Opcode::MAdd ||
        op == Opcode::MSub || op == Opcode::NMSub || op == Opcode::NMAdd) {
        const OperationId decoded =
            decode_ext_f_d(op, funct3, funct7, std::to_underlying(dec.rs2()), ir);
        if constexpr (!simrv::xlen::kIsXLen64) {
            if (isa::requires_rv64(decoded)) {
                return OperationId::ILLEGAL;
            }
        }
        return decoded;
    }
    if (op == Opcode::OpV) {
        return decode_ext_v(funct3, funct7, ir);
    }
    if (op == Opcode::Custom0) {
        return OperationId::CFU;
    }
    if ((op == Opcode::Op || op == Opcode::Op32) && (funct7 == 0x01)) {
        return decode_ext_m(op, funct3);
    }
    auto res = decode_ext_i(op, funct3, funct7, ir);
    if (isa::is_zk_op(res)) {
        execute::ExecuteUnitZk::set_current_funct7(funct7);
    }
    return res;
}

}  // namespace simrv::pipeline
