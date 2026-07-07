#include "simrv/pipeline/Decoder.hpp"

namespace simrv::pipeline {

using simrv::isa::OperationId;
using enum simrv::isa::OperationId;

const std::array<std::string_view, static_cast<size_t>(isa::OperationIdCount)> OPERATION_NAME = {
    "LUI",        "AUIPC",    "JAL",       "JALR",      "BEQ",       "BNE",      "BLT",
    "BGE",        "BLTU",     "BGEU",      "LB",        "LH",        "LW",       "LD",
    "LBU",        "LHU",      "LWU",       "SB",        "SH",        "SW",       "SD",
    "ADDI",       "SLTI",     "SLTIU",     "XORI",      "ORI",       "ANDI",     "SLLI",
    "SRLI",       "SRAI",     "ADDIW",     "SLLIW",     "SRLIW",     "SRAIW",    "ADD",
    "SUB",        "SLL",      "SLT",       "SLTU",      "XOR",       "SRL",      "SRA",
    "OR",         "AND",      "ADDW",      "SUBW",      "SLLW",      "SRLW",     "SRAW",
    "FENCE",      "FENCE_I",  "ECALL",     "EBREAK",    "CSRRW",     "CSRRS",    "CSRRC",
    "CSRRWI",     "CSRRSI",   "CSRRCI",    "URET",      "SRET",      "MRET",     "WFI",
    "SFENCE_VMA", "MUL",      "MULH",      "MULHSU",    "MULHU",     "DIV",      "DIVU",
    "REM",        "REMU",     "MULW",      "DIVW",      "DIVUW",     "REMW",     "REMUW",
    "LR_W",       "SC_W",     "AMOSWAP_W", "AMOADD_W",  "AMOXOR_W",  "AMOAND_W", "AMOOR_W",
    "AMOMIN_W",   "AMOMAX_W", "AMOMINU_W", "AMOMAXU_W", "FLW",       "FSW",      "FMADD_S",
    "FMSUB_S",    "FNMADD_S", "FNMSUB_S",  "FADD_S",    "FSUB_S",    "FMUL_S",   "FDIV_S",
    "FSQRT_S",    "FSGNJ_S",  "FSGNJN_S",  "FSGNJX_S",  "FMIN_S",    "FMAX_S",   "FCVT_W_S",
    "FCVT_WU_S",  "FMV_X_W",  "FEQ_S",     "FLT_S",     "FLE_S",     "FCLASS_S", "FCVT_S_W",
    "FCVT_S_WU",  "FMV_W_X",  "FCVT_L_S",  "FCVT_LU_S", "FCVT_S_L",  "FCVT_S_LU", "FLD",
    "FSD",        "FMADD_D",  "FMSUB_D",   "FNMSUB_D",  "FNMADD_D",  "FADD_D",   "FSUB_D",
    "FMUL_D",     "FDIV_D",   "FSQRT_D",   "FSGNJ_D",   "FSGNJN_D",  "FSGNJX_D", "FMIN_D",
    "FMAX_D",     "FCVT_S_D", "FCVT_D_S",  "FEQ_D",     "FLT_D",     "FLE_D",    "FCLASS_D",
    "FCVT_W_D",   "FCVT_WU_D","FCVT_D_W",  "FCVT_D_WU", "FMV_X_D",   "FMV_D_X",  "FCVT_L_D",
    "FCVT_LU_D",  "FCVT_D_L", "FCVT_D_LU",
    "SH1ADD",     "SH2ADD",   "SH3ADD",    "SH1ADD.UW", "SH2ADD.UW", "SH3ADD.UW",
    "ADD.UW",     "SLLI.UW",  "ANDN",      "ORN",       "XNOR",      "CLZ",
    "CTZ",        "CPOP",     "MIN",       "MAX",       "MINU",      "MAXU",
    "SEXT.B",     "SEXT.H",   "ZEXT.H",    "ROL",       "ROR",       "RORI",
    "CLZW",       "CTZW",     "CPOPW",     "ROLW",      "RORW",      "RORIW",
    "CLMUL",      "CLMULH",   "CLMULR",    "BSET",      "BSETI",     "BCLR",
    "BCLRI",      "BINV",     "BINVI",     "BEXT",      "BEXTI",     "ORC.B",
    "REV8",       "PACK",     "PACKW",
    "VSETVLI",    "VSETIVLI", "VSETVL",
    "VLE8_V",     "VLE16_V",  "VLE32_V",
    "VSE8_V",     "VSE16_V",  "VSE32_V",
    "VADD_VV",    "VADD_VX",  "VADD_VI",
    "VSUB_VV",    "VSUB_VX",
    "VMUL_VV",    "VMUL_VX",
    "VDIV_VV",    "VDIV_VX",
    "VDIVU_VV",   "VDIVU_VX",
    "VAND_VV",    "VAND_VX",  "VAND_VI",
    "VOR_VV",     "VOR_VX",   "VOR_VI",
    "VXOR_VV",    "VXOR_VX",  "VXOR_VI",
    "VSLL_VV",    "VSLL_VX",  "VSLL_VI",
    "VSRL_VV",    "VSRL_VX",  "VSRL_VI",
    "VSRA_VV",    "VSRA_VX",  "VSRA_VI",
    "VMV_V_V",    "VMV_V_X",  "VMV_V_I",
    "VMV_X_S",    "VMV_S_X",
    "VMSEQ_VV",   "VMSEQ_VX", "VMSEQ_VI",
    "VMSNE_VV",   "VMSNE_VX", "VMSNE_VI",
    "VMSLT_VV",   "VMSLT_VX",
    "VMSLTU_VV",  "VMSLTU_VX",
    "VMSLE_VV",   "VMSLE_VX", "VMSLE_VI",
    "VMSLEU_VV",  "VMSLEU_VX", "VMSLEU_VI",
    "VMSGT_VX",   "VMSGT_VI",
    "VMSGTU_VX",  "VMSGTU_VI",
    "VMERGE_VVM", "VMERGE_VXM", "VMERGE_VIM",
    "VMIN_VV", "VMIN_VX", "VMINU_VV", "VMINU_VX",
    "VMAX_VV", "VMAX_VX", "VMAXU_VV", "VMAXU_VX",
    "VCHECK",
    "UNKNOWN"};

namespace {

auto decode_branch(uint32_t funct3) -> OperationId {
    switch (funct3) {
        case 0: return OperationId::BEQ;
        case 1: return OperationId::BNE;
        case 4: return OperationId::BLT;
        case 5: return OperationId::BGE;
        case 6: return OperationId::BLTU;
        case 7: return OperationId::BGEU;
        default: return OperationId::UNKNOWN;
    }
}

auto decode_load(uint32_t funct3) -> OperationId {
    switch (funct3) {
        case 0: return OperationId::LB;
        case 1: return OperationId::LH;
        case 2: return OperationId::LW;
        case 3: return OperationId::LD;
        case 4: return OperationId::LBU;
        case 5: return OperationId::LHU;
        case 6: return OperationId::LWU;
        default: return OperationId::UNKNOWN;
    }
}

auto decode_store(uint32_t funct3) -> OperationId {
    switch (funct3) {
        case 0: return OperationId::SB;
        case 1: return OperationId::SH;
        case 2: return OperationId::SW;
        case 3: return OperationId::SD;
        default: return OperationId::UNKNOWN;
    }
}

auto decode_op_imm(uint32_t funct3, uint32_t funct7, Instruction ir) -> OperationId {
    const uint32_t imm12 = ir >> 20;
    const uint32_t imm6 = imm12 >> 6;
    switch (funct3) {
        case 0: return OperationId::ADDI;
        case 1:
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
        case 2: return OperationId::SLTI;
        case 3: return OperationId::SLTIU;
        case 4: return OperationId::XORI;
        case 5:
            if (imm12 == 0x287) return OperationId::ORC_B;
            if (imm12 == 0x698 || imm12 == 0x6b8) return OperationId::REV8;
            if (imm6 == 0x18) return OperationId::RORI;
            if (imm6 == 0x12) return OperationId::BEXTI;
            if (funct7 == 0x00 || funct7 == 0x01) return OperationId::SRLI;
            if (funct7 == 0x20 || funct7 == 0x21) return OperationId::SRAI;
            return OperationId::UNKNOWN;
        case 6: return OperationId::ORI;
        case 7: return OperationId::ANDI;
        default: return OperationId::UNKNOWN;
    }
}

auto decode_op_standard(uint32_t funct3, uint32_t funct7) -> OperationId {
    switch (funct3) {
        case 0:
            if (funct7 == 0x00) return OperationId::ADD;
            if (funct7 == 0x20) return OperationId::SUB;
            return OperationId::UNKNOWN;
        case 1:
            if (funct7 == 0x30) return OperationId::ROL;
            if (funct7 == 0x05) return OperationId::CLMUL;
            if (funct7 == 0x14) return OperationId::BSET;
            if (funct7 == 0x24) return OperationId::BCLR;
            if (funct7 == 0x34) return OperationId::BINV;
            if (funct7 == 0x00) return OperationId::SLL;
            return OperationId::UNKNOWN;
        case 2:
            if (funct7 == 0x10) return OperationId::SH1ADD;
            if (funct7 == 0x05) return OperationId::CLMULR;
            if (funct7 == 0x00) return OperationId::SLT;
            return OperationId::UNKNOWN;
        case 3:
            if (funct7 == 0x05) return OperationId::CLMULH;
            if (funct7 == 0x00) return OperationId::SLTU;
            return OperationId::UNKNOWN;
        case 4:
            if (funct7 == 0x10) return OperationId::SH2ADD;
            if (funct7 == 0x20) return OperationId::XNOR;
            if (funct7 == 0x05) return OperationId::MIN;
            if (funct7 == 0x04) return OperationId::PACK;
            if (funct7 == 0x00) return OperationId::XOR;
            return OperationId::UNKNOWN;
        case 5:
            if (funct7 == 0x30) return OperationId::ROR;
            if (funct7 == 0x24) return OperationId::BEXT;
            if (funct7 == 0x05) return OperationId::MINU;
            if (funct7 == 0x00) return OperationId::SRL;
            if (funct7 == 0x20) return OperationId::SRA;
            return OperationId::UNKNOWN;
        case 6:
            if (funct7 == 0x10) return OperationId::SH3ADD;
            if (funct7 == 0x20) return OperationId::ORN;
            if (funct7 == 0x05) return OperationId::MAX;
            if (funct7 == 0x00) return OperationId::OR;
            return OperationId::UNKNOWN;
        case 7:
            if (funct7 == 0x20) return OperationId::ANDN;
            if (funct7 == 0x05) return OperationId::MAXU;
            if (funct7 == 0x00) return OperationId::AND;
            return OperationId::UNKNOWN;
        default: return OperationId::UNKNOWN;
    }
}

auto decode_op_imm32(uint32_t funct3, uint32_t funct7, Instruction ir) -> OperationId {
    const uint32_t imm12 = ir >> 20;
    const uint32_t imm6 = imm12 >> 6;
    switch (funct3) {
        case 0: return OperationId::ADDIW;
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
        default: return OperationId::UNKNOWN;
    }
}

auto decode_op32_standard(uint32_t funct3, uint32_t funct7) -> OperationId {
    switch (funct3) {
        case 0:
            if (funct7 == 0x04) return OperationId::ADD_UW;
            if (funct7 == 0x00) return OperationId::ADDW;
            if (funct7 == 0x20) return OperationId::SUBW;
            return OperationId::UNKNOWN;
        case 1:
            if (funct7 == 0x30) return OperationId::ROLW;
            if (funct7 == 0x00) return OperationId::SLLW;
            return OperationId::UNKNOWN;
        case 2:
            if (funct7 == 0x10) return OperationId::SH1ADD_UW;
            return OperationId::UNKNOWN;
        case 4:
            if (funct7 == 0x10) return OperationId::SH2ADD_UW;
            if (funct7 == 0x04) return OperationId::PACKW;
            return OperationId::UNKNOWN;
        case 5:
            if (funct7 == 0x30) return OperationId::RORW;
            if (funct7 == 0x00) return OperationId::SRLW;
            if (funct7 == 0x20) return OperationId::SRAW;
            return OperationId::UNKNOWN;
        case 6:
            if (funct7 == 0x10) return OperationId::SH3ADD_UW;
            return OperationId::UNKNOWN;
        default: return OperationId::UNKNOWN;
    }
}

auto decode_system_priv(uint32_t funct7, Instruction ir) -> OperationId {
    const uint32_t f12 = ir >> 20;
    switch (f12) {
        case 0x000: return OperationId::ECALL;
        case 0x001: return OperationId::EBREAK;
        case 0x002: return OperationId::URET;
        case 0x102: return OperationId::SRET;
        case 0x302: return OperationId::MRET;
        case 0x105: return OperationId::WFI;
        default:
            if (funct7 == 0x09) return OperationId::SFENCE_VMA;
            break;
    }
    return OperationId::UNKNOWN;
}

auto decode_system_csr(uint32_t funct3) -> OperationId {
    switch (funct3) {
        case 1: return OperationId::CSRRW;
        case 2: return OperationId::CSRRS;
        case 3: return OperationId::CSRRC;
        case 5: return OperationId::CSRRWI;
        case 6: return OperationId::CSRRSI;
        case 7: return OperationId::CSRRCI;
        default: return OperationId::UNKNOWN;
    }
}

auto decode_system(uint32_t funct3, uint32_t funct7, Instruction ir) -> OperationId {
    if (funct3 == 0) {
        return decode_system_priv(funct7, ir);
    }
    return decode_system_csr(funct3);
}

auto decode_ext_i(Opcode op, uint32_t funct3, uint32_t funct7, Instruction ir) -> OperationId {
    switch (op) {
        case Opcode::Lui: return OperationId::LUI;
        case Opcode::Auipc: return OperationId::AUIPC;
        case Opcode::Jal: return OperationId::JAL;
        case Opcode::Jalr: return OperationId::JALR;
        case Opcode::Branch: return decode_branch(funct3);
        case Opcode::Load: return decode_load(funct3);
        case Opcode::Store: return decode_store(funct3);
        case Opcode::OpImm: return decode_op_imm(funct3, funct7, ir);
        case Opcode::OpImm32: return decode_op_imm32(funct3, funct7, ir);
        case Opcode::Op: return decode_op_standard(funct3, funct7);
        case Opcode::Op32: return decode_op32_standard(funct3, funct7);
        case Opcode::MiscMem:
            if (funct3 == 0) return OperationId::FENCE;
            if (funct3 == 1) return OperationId::FENCE_I;
            return OperationId::UNKNOWN;
        case Opcode::System: return decode_system(funct3, funct7, ir);
        default: return OperationId::UNKNOWN;
    }
}

auto decode_ext_m_op(uint32_t funct3) -> OperationId {
    switch (funct3) {
        case 0: return OperationId::MUL;
        case 1: return OperationId::MULH;
        case 2: return OperationId::MULHSU;
        case 3: return OperationId::MULHU;
        case 4: return OperationId::DIV;
        case 5: return OperationId::DIVU;
        case 6: return OperationId::REM;
        case 7: return OperationId::REMU;
        default: return OperationId::UNKNOWN;
    }
}

auto decode_ext_m_op32(uint32_t funct3) -> OperationId {
    switch (funct3) {
        case 0: return OperationId::MULW;
        case 4: return OperationId::DIVW;
        case 5: return OperationId::DIVUW;
        case 6: return OperationId::REMW;
        case 7: return OperationId::REMUW;
        default: return OperationId::UNKNOWN;
    }
}

auto decode_ext_m(Opcode op, uint32_t funct3) -> OperationId {
    if (op == Opcode::Op32) {
        return decode_ext_m_op32(funct3);
    }
    return decode_ext_m_op(funct3);
}

auto decode_ext_a(uint32_t funct7, uint32_t funct3) -> OperationId {
    const uint32_t funct5 = funct7 >> 2;
    if (funct3 == 2) { // 32-bit AMO*W
        switch (funct5) {
            case 0x02: return OperationId::LR_W;
            case 0x03: return OperationId::SC_W;
            case 0x01: return OperationId::AMOSWAP_W;
            case 0x00: return OperationId::AMOADD_W;
            case 0x04: return OperationId::AMOXOR_W;
            case 0x0C: return OperationId::AMOAND_W;
            case 0x08: return OperationId::AMOOR_W;
            case 0x10: return OperationId::AMOMIN_W;
            case 0x14: return OperationId::AMOMAX_W;
            case 0x18: return OperationId::AMOMINU_W;
            case 0x1C: return OperationId::AMOMAXU_W;
            default: return OperationId::UNKNOWN;
        }
    } else if (funct3 == 3) { // 64-bit AMO*D
        switch (funct5) {
            case 0x02: return OperationId::LR_D;
            case 0x03: return OperationId::SC_D;
            case 0x01: return OperationId::AMOSWAP_D;
            case 0x00: return OperationId::AMOADD_D;
            case 0x04: return OperationId::AMOXOR_D;
            case 0x0C: return OperationId::AMOAND_D;
            case 0x08: return OperationId::AMOOR_D;
            case 0x10: return OperationId::AMOMIN_D;
            case 0x14: return OperationId::AMOMAX_D;
            case 0x18: return OperationId::AMOMINU_D;
            case 0x1C: return OperationId::AMOMAXU_D;
            default: return OperationId::UNKNOWN;
        }
    }
    return OperationId::UNKNOWN;
}

auto decode_fma(Opcode op, uint32_t funct7) -> OperationId {
    const bool is_double = ((funct7 & 0x03) == 1);
    switch (op) {
        case Opcode::MAdd: return is_double ? OperationId::FMADD_D : OperationId::FMADD_S;
        case Opcode::MSub: return is_double ? OperationId::FMSUB_D : OperationId::FMSUB_S;
        case Opcode::NMSub: return is_double ? OperationId::FNMSUB_D : OperationId::FNMSUB_S;
        case Opcode::NMAdd: return is_double ? OperationId::FNMADD_D : OperationId::FNMADD_S;
        default: return OperationId::UNKNOWN;
    }
}

auto decode_op_fp_single_arith(uint32_t funct3, uint32_t f5) -> OperationId {
    switch (f5) {
        case 0x00: return OperationId::FADD_S;
        case 0x01: return OperationId::FSUB_S;
        case 0x02: return OperationId::FMUL_S;
        case 0x03: return OperationId::FDIV_S;
        case 0x04:
            if (funct3 == 0) return OperationId::FSGNJ_S;
            if (funct3 == 1) return OperationId::FSGNJN_S;
            if (funct3 == 2) return OperationId::FSGNJX_S;
            break;
        case 0x05:
            if (funct3 == 0) return OperationId::FMIN_S;
            if (funct3 == 1) return OperationId::FMAX_S;
            break;
        case 0x0B: return OperationId::FSQRT_S;
        case 0x1E: return OperationId::FMV_W_X;
        default: break;
    }
    return OperationId::UNKNOWN;
}

auto decode_op_fp_single_fcvt(uint32_t f5, uint32_t rs2) -> OperationId {
    switch (f5) {
        case 0x08: return (rs2 == 1) ? OperationId::FCVT_S_D : OperationId::UNKNOWN;
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
        default: break;
    }
    return OperationId::UNKNOWN;
}

auto decode_op_fp_single_cmp(uint32_t funct3, uint32_t f5) -> OperationId {
    switch (f5) {
        case 0x14:
            if (funct3 == 0) return OperationId::FLE_S;
            if (funct3 == 1) return OperationId::FLT_S;
            if (funct3 == 2) return OperationId::FEQ_S;
            break;
        case 0x1C:
            return (funct3 == 0)   ? OperationId::FMV_X_W
                   : (funct3 == 1) ? OperationId::FCLASS_S
                                   : OperationId::UNKNOWN;
        default: break;
    }
    return OperationId::UNKNOWN;
}

auto decode_op_fp_single(uint32_t funct3, uint32_t f5, uint32_t rs2) -> OperationId {
    if (f5 == 0x08 || f5 == 0x18 || f5 == 0x1A) {
        return decode_op_fp_single_fcvt(f5, rs2);
    }
    if (f5 == 0x14 || f5 == 0x1C) {
        return decode_op_fp_single_cmp(funct3, f5);
    }
    return decode_op_fp_single_arith(funct3, f5);
}

auto decode_op_fp_double_arith(uint32_t funct3, uint32_t f5) -> OperationId {
    switch (f5) {
        case 0x00: return OperationId::FADD_D;
        case 0x01: return OperationId::FSUB_D;
        case 0x02: return OperationId::FMUL_D;
        case 0x03: return OperationId::FDIV_D;
        case 0x04:
            if (funct3 == 0) return OperationId::FSGNJ_D;
            if (funct3 == 1) return OperationId::FSGNJN_D;
            if (funct3 == 2) return OperationId::FSGNJX_D;
            break;
        case 0x05:
            if (funct3 == 0) return OperationId::FMIN_D;
            if (funct3 == 1) return OperationId::FMAX_D;
            break;
        case 0x0B: return OperationId::FSQRT_D;
        default: break;
    }
    return OperationId::UNKNOWN;
}

auto decode_op_fp_double_fcvt(uint32_t f5, uint32_t rs2) -> OperationId {
    switch (f5) {
        case 0x08: return (rs2 == 0) ? OperationId::FCVT_D_S : OperationId::UNKNOWN;
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
        default: break;
    }
    return OperationId::UNKNOWN;
}

auto decode_op_fp_double_cmp(uint32_t funct3, uint32_t f5) -> OperationId {
    switch (f5) {
        case 0x14:
            if (funct3 == 0) return OperationId::FLE_D;
            if (funct3 == 1) return OperationId::FLT_D;
            if (funct3 == 2) return OperationId::FEQ_D;
            break;
        case 0x1C:
            if (funct3 == 0) return OperationId::FMV_X_D;
            if (funct3 == 1) return OperationId::FCLASS_D;
            break;
        case 0x1E:
            return (funct3 == 0) ? OperationId::FMV_D_X : OperationId::UNKNOWN;
        default: break;
    }
    return OperationId::UNKNOWN;
}

auto decode_op_fp_double(uint32_t funct3, uint32_t f5, uint32_t rs2) -> OperationId {
    if (f5 == 0x08 || f5 == 0x18 || f5 == 0x1A) {
        return decode_op_fp_double_fcvt(f5, rs2);
    }
    if (f5 == 0x14 || f5 == 0x1C || f5 == 0x1E) {
        return decode_op_fp_double_cmp(funct3, f5);
    }
    return decode_op_fp_double_arith(funct3, f5);
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

    switch (funct7 >> 1) {
        case 0x00:
            if (funct3 == 0) return OperationId::VADD_VV;
            if (funct3 == 4) return OperationId::VADD_VX;
            if (funct3 == 3) return OperationId::VADD_VI;
            break;
        case 0x02:
            if (funct3 == 0) return OperationId::VSUB_VV;
            if (funct3 == 4) return OperationId::VSUB_VX;
            break;
        case 0x04:
            if (funct3 == 0) return OperationId::VMINU_VV;
            if (funct3 == 4) return OperationId::VMINU_VX;
            break;
        case 0x05:
            if (funct3 == 0) return OperationId::VMIN_VV;
            if (funct3 == 4) return OperationId::VMIN_VX;
            break;
        case 0x06:
            if (funct3 == 0) return OperationId::VMAXU_VV;
            if (funct3 == 4) return OperationId::VMAXU_VX;
            break;
        case 0x07:
            if (funct3 == 0) return OperationId::VMAX_VV;
            if (funct3 == 4) return OperationId::VMAX_VX;
            break;
        case 0x25:
            if (funct3 == 0) return OperationId::VSLL_VV;
            if (funct3 == 4) return OperationId::VSLL_VX;
            if (funct3 == 3) return OperationId::VSLL_VI;
            if (funct3 == 2) return OperationId::VMUL_VV;
            if (funct3 == 6) return OperationId::VMUL_VX;
            break;
        case 0x21:
            if (funct3 == 2) return OperationId::VDIV_VV;
            if (funct3 == 6) return OperationId::VDIV_VX;
            break;
        case 0x20:
            if (funct3 == 2) return OperationId::VDIVU_VV;
            if (funct3 == 6) return OperationId::VDIVU_VX;
            break;
        case 0x09:
            if (funct3 == 0) return OperationId::VAND_VV;
            if (funct3 == 4) return OperationId::VAND_VX;
            if (funct3 == 3) return OperationId::VAND_VI;
            break;
        case 0x0A:
            if (funct3 == 0) return OperationId::VOR_VV;
            if (funct3 == 4) return OperationId::VOR_VX;
            if (funct3 == 3) return OperationId::VOR_VI;
            break;
        case 0x0B:
            if (funct3 == 0) return OperationId::VXOR_VV;
            if (funct3 == 4) return OperationId::VXOR_VX;
            if (funct3 == 3) return OperationId::VXOR_VI;
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
            break;
        case 0x17:
            if (vm) {
                if (funct3 == 0) return OperationId::VMV_V_V;
                if (funct3 == 4) return OperationId::VMV_V_X;
                if (funct3 == 3) return OperationId::VMV_V_I;
            } else {
                if (funct3 == 0) return OperationId::VMERGE_VVM;
                if (funct3 == 4) return OperationId::VMERGE_VXM;
                if (funct3 == 3) return OperationId::VMERGE_VIM;
            }
            break;
        case 0x10:
            if (funct3 == 2) return OperationId::VMV_X_S;
            if (funct3 == 6) return OperationId::VMV_S_X;
            break;
        case 0x18:
            if (funct3 == 0) return OperationId::VMSEQ_VV;
            if (funct3 == 4) return OperationId::VMSEQ_VX;
            if (funct3 == 3) return OperationId::VMSEQ_VI;
            break;
        case 0x19:
            if (funct3 == 0) return OperationId::VMSNE_VV;
            if (funct3 == 4) return OperationId::VMSNE_VX;
            if (funct3 == 3) return OperationId::VMSNE_VI;
            break;
        case 0x1B:
            if (funct3 == 0) return OperationId::VMSLT_VV;
            if (funct3 == 4) return OperationId::VMSLT_VX;
            break;
        case 0x1A:
            if (funct3 == 0) return OperationId::VMSLTU_VV;
            if (funct3 == 4) return OperationId::VMSLTU_VX;
            break;
        case 0x1D:
            if (funct3 == 0) return OperationId::VMSLE_VV;
            if (funct3 == 4) return OperationId::VMSLE_VX;
            if (funct3 == 3) return OperationId::VMSLE_VI;
            break;
        case 0x1C:
            if (funct3 == 0) return OperationId::VMSLEU_VV;
            if (funct3 == 4) return OperationId::VMSLEU_VX;
            if (funct3 == 3) return OperationId::VMSLEU_VI;
            break;
        case 0x1E:
            if (funct3 == 4) return OperationId::VMSGT_VX;
            if (funct3 == 3) return OperationId::VMSGT_VI;
            break;
        case 0x1F:
            if (funct3 == 4) return OperationId::VMSGTU_VX;
            if (funct3 == 3) return OperationId::VMSGTU_VI;
            break;
        default:
            break;
    }
    return OperationId::UNKNOWN;
}

auto decode_ext_f_d(Opcode op, uint32_t funct3, uint32_t funct7, uint32_t rs2) -> OperationId {
    switch (op) {
        case Opcode::LoadFp:
            if (funct3 == 2) return OperationId::FLW;
            if (funct3 == 3) return OperationId::FLD;
            if (funct3 == 0) return OperationId::VLE8_V;
            if (funct3 == 5) return OperationId::VLE16_V;
            if (funct3 == 6) return OperationId::VLE32_V;
            return OperationId::UNKNOWN;
        case Opcode::StoreFp:
            if (funct3 == 2) return OperationId::FSW;
            if (funct3 == 3) return OperationId::FSD;
            if (funct3 == 0) return OperationId::VSE8_V;
            if (funct3 == 5) return OperationId::VSE16_V;
            if (funct3 == 6) return OperationId::VSE32_V;
            return OperationId::UNKNOWN;
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

auto make_i_type(uint32_t opcode, uint32_t funct3, uint32_t rd, uint32_t rs1,
                 uint32_t imm) -> Instruction {
    return opcode | (rd << 7) | (funct3 << 12) | (rs1 << 15) | ((imm & 0xFFF) << 20);
}

auto make_s_type(uint32_t opcode, uint32_t funct3, uint32_t rs1, uint32_t rs2,
                 uint32_t imm) -> Instruction {
    return opcode | ((imm & 0x1F) << 7) | (funct3 << 12) | (rs1 << 15) | (rs2 << 20) |
           (((imm >> 5) & 0x7F) << 25);
}

auto make_b_type(uint32_t opcode, uint32_t funct3, uint32_t rs1, uint32_t rs2,
                 uint32_t imm) -> Instruction {
    return opcode | (((imm >> 11) & 0x1) << 7) | (((imm >> 1) & 0xF) << 8) | (funct3 << 12) |
           (rs1 << 15) | (rs2 << 20) | (((imm >> 5) & 0x3F) << 25) |
           (((imm >> 12) & 0x1) << 31);
}

auto make_u_type(uint32_t opcode, uint32_t rd, uint32_t imm) -> Instruction {
    return opcode | (rd << 7) | (imm & 0xFFFFF000);
}

auto make_j_type(uint32_t opcode, uint32_t rd, uint32_t imm) -> Instruction {
    return opcode | (rd << 7) | (((imm >> 12) & 0xFF) << 12) | (((imm >> 11) & 0x1) << 20) |
           (((imm >> 1) & 0x3FF) << 21) | (((imm >> 20) & 0x1) << 31);
}

auto decompress_q0(Instruction ir, bool is_rv64, uint32_t op, uint32_t rs1_p, uint32_t rs2_p) -> Instruction {
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
            case 0: return make_r_type(0x33, 0x0, 0x20, rs1_p, rs1_p, rs2_p);
            case 1: return make_r_type(0x33, 0x4, 0x00, rs1_p, rs1_p, rs2_p);
            case 2: return make_r_type(0x33, 0x6, 0x00, rs1_p, rs1_p, rs2_p);
            case 3: return make_r_type(0x33, 0x7, 0x00, rs1_p, rs1_p, rs2_p);
            default: break;
        }
    } else {
        switch (funct_sub) {
            case 0: return make_r_type(0x3B, 0x0, 0x20, rs1_p, rs1_p, rs2_p);
            case 1: return make_r_type(0x3B, 0x0, 0x00, rs1_p, rs1_p, rs2_p);
            default: break;
        }
    }
    return 0;
}

auto decompress_q1_op1(Instruction ir, bool is_rv64, uint32_t rs1_rd) -> Instruction {
    if (is_rv64) {
        const uint32_t imm = ((ir >> 2) & 0x1F) | (((ir >> 12) & 0x1) ? 0xFFFFFFE0 : 0);
        return make_i_type(0x1B, 0x0, rs1_rd, rs1_rd, imm);
    } else {
        const uint32_t imm =
            ((ir >> 2) & 0x1) << 5 | ((ir >> 3) & 0x7) << 1 | ((ir >> 6) & 0x1) << 7 |
            ((ir >> 7) & 0x1) << 6 | ((ir >> 8) & 0x1) << 10 | ((ir >> 9) & 0x3) << 8 |
            ((ir >> 11) & 0x1) << 4 | (((ir >> 12) & 0x1) ? 0xFFFFF800 : 0);
        return make_j_type(0x6F, 1, imm);
    }
}

auto decompress_q1_op3(Instruction ir, uint32_t rs1_rd) -> Instruction {
    if (rs1_rd == 2) {
        const uint32_t imm = ((ir >> 2) & 0x1) << 5 |
                             ((ir >> 3) & 0x3) << 7 |
                             ((ir >> 5) & 0x1) << 6 |
                             ((ir >> 6) & 0x1) << 4 |
                             (((ir >> 12) & 0x1) ? 0xFFFFFE00 : 0);
        return make_i_type(0x13, 0x0, 2, 2, imm);
    } else {
        const uint32_t imm =
            (((ir >> 2) & 0x1F) << 12) | (((ir >> 12) & 0x1) ? 0xFFFE0000 : 0);
        return make_u_type(0x37, rs1_rd, imm);
    }
}

auto decompress_q1(Instruction ir, bool is_rv64, uint32_t op, uint32_t rs1_rd,
                   uint32_t rs1_p, uint32_t rs2_p) -> Instruction {
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
            const uint32_t imm =
                ((ir >> 2) & 0x1) << 5 | ((ir >> 3) & 0x7) << 1 | ((ir >> 6) & 0x1) << 7 |
                ((ir >> 7) & 0x1) << 6 | ((ir >> 8) & 0x1) << 10 | ((ir >> 9) & 0x3) << 8 |
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

auto decompress_q2(Instruction ir, bool is_rv64, uint32_t op, uint32_t rs1_rd, uint32_t rs2) -> Instruction {
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

} // namespace

auto decoder(Instruction ir) -> OperationId {
    simrv::pipeline::Decoder dec(ir);
    const auto op = dec.opcode();
    const auto funct3 = std::to_underlying(dec.funct3());
    const auto funct7 = dec.funct7();

    if (op == Opcode::Amo) {
        return decode_ext_a(funct7, funct3);
    }
    if (op == Opcode::LoadFp || op == Opcode::StoreFp || op == Opcode::OpFp ||
        op == Opcode::MAdd || op == Opcode::MSub || op == Opcode::NMSub || op == Opcode::NMAdd) {
        return decode_ext_f_d(op, funct3, funct7, std::to_underlying(dec.rs2()));
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
    return decode_ext_i(op, funct3, funct7, ir);
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

