#include "simrv/decode/Decoder.hpp"

namespace simrv::decode {

const std::array<std::string_view, static_cast<size_t>(OperationIdCount)> OPERATION_NAME = {
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
    "FCVT_S_WU",  "FMV_W_X",  "FLD",       "FSD",       "FMADD_D",   "FMSUB_D",  "FNMSUB_D",
    "FNMADD_D",   "FADD_D",   "FSUB_D",    "FMUL_D",    "FDIV_D",    "FSQRT_D",  "FSGNJ_D",
    "FSGNJN_D",   "FSGNJX_D", "FMIN_D",    "FMAX_D",    "FCVT_S_D",  "FCVT_D_S", "FEQ_D",
    "FLT_D",      "FLE_D",    "FCLASS_D",  "FCVT_W_D",  "FCVT_WU_D", "FCVT_D_W", "FCVT_D_WU",
    "UNKNOWN"};

auto decoder(Instruction ir) -> OperationId {
    simrv::decode::Decoder dec(ir);
    const auto op = dec.opcode();
    const auto funct3 = std::to_underlying(dec.funct3());
    const auto funct7 = dec.funct7();

    switch (op) {
        case Opcode::kLui:
            return OperationId::LUI;
        case Opcode::kAuipc:
            return OperationId::AUIPC;
        case Opcode::kJal:
            return OperationId::JAL;
        case Opcode::kJalr:
            return OperationId::JALR;
        case Opcode::kBranch:
            switch (funct3) {
                case 0:
                    return OperationId::BEQ;
                case 1:
                    return OperationId::BNE;
                case 4:
                    return OperationId::BLT;
                case 5:
                    return OperationId::BGE;
                case 6:
                    return OperationId::BLTU;
                case 7:
                    return OperationId::BGEU;
                default:
                    break;
            }
            break;
        case Opcode::kLoad:
            switch (funct3) {
                case 0:
                    return OperationId::LB;
                case 1:
                    return OperationId::LH;
                case 2:
                    return OperationId::LW;
                case 3:
                    return OperationId::LD;
                case 4:
                    return OperationId::LBU;
                case 5:
                    return OperationId::LHU;
                case 6:
                    return OperationId::LWU;
                default:
                    break;
            }
            break;
        case Opcode::kStore:
            switch (funct3) {
                case 0:
                    return OperationId::SB;
                case 1:
                    return OperationId::SH;
                case 2:
                    return OperationId::SW;
                case 3:
                    return OperationId::SD;
                default:
                    break;
            }
            break;
        case Opcode::kOpImm:
            switch (funct3) {
                case 0:
                    return OperationId::ADDI;
                case 1:
                    return OperationId::SLLI;
                case 2:
                    return OperationId::SLTI;
                case 3:
                    return OperationId::SLTIU;
                case 4:
                    return OperationId::XORI;
                case 5:
                    return (funct7 & 0x20) ? OperationId::SRAI : OperationId::SRLI;
                case 6:
                    return OperationId::ORI;
                case 7:
                    return OperationId::ANDI;
                default:
                    break;
            }
            break;
        case Opcode::kOp:
            if (funct7 & 0x01) {
                switch (funct3) {
                    case 0:
                        return OperationId::MUL;
                    case 1:
                        return OperationId::MULH;
                    case 2:
                        return OperationId::MULHSU;
                    case 3:
                        return OperationId::MULHU;
                    case 4:
                        return OperationId::DIV;
                    case 5:
                        return OperationId::DIVU;
                    case 6:
                        return OperationId::REM;
                    case 7:
                        return OperationId::REMU;
                    default:
                        break;
                }
            } else {
                switch (funct3) {
                    case 0:
                        return (funct7 & 0x20) ? OperationId::SUB : OperationId::ADD;
                    case 1:
                        return OperationId::SLL;
                    case 2:
                        return OperationId::SLT;
                    case 3:
                        return OperationId::SLTU;
                    case 4:
                        return OperationId::XOR;
                    case 5:
                        return (funct7 & 0x20) ? OperationId::SRA : OperationId::SRL;
                    case 6:
                        return OperationId::OR;
                    case 7:
                        return OperationId::AND;
                    default:
                        break;
                }
            }
            break;
        case Opcode::kOpImm32:
            switch (funct3) {
                case 0:
                    return OperationId::ADDIW;
                case 1:
                    return OperationId::SLLIW;
                case 5:
                    return (funct7 & 0x20) ? OperationId::SRAIW : OperationId::SRLIW;
                default:
                    break;
            }
            break;
        case Opcode::kOp32:
            if (funct7 & 0x01) {
                switch (funct3) {
                    case 0:
                        return OperationId::MULW;
                    case 4:
                        return OperationId::DIVW;
                    case 5:
                        return OperationId::DIVUW;
                    case 6:
                        return OperationId::REMW;
                    case 7:
                        return OperationId::REMUW;
                    default:
                        break;
                }
            } else {
                switch (funct3) {
                    case 0:
                        return (funct7 & 0x20) ? OperationId::SUBW : OperationId::ADDW;
                    case 1:
                        return OperationId::SLLW;
                    case 5:
                        return (funct7 & 0x20) ? OperationId::SRAW : OperationId::SRLW;
                    default:
                        break;
                }
            }
            break;
        case Opcode::kMiscMem:
            return (funct3 == 1) ? OperationId::FENCE_I : OperationId::FENCE;
        case Opcode::kSystem:
            if (funct3 == 0) {
                const uint32_t f12 = ir >> 20;
                switch (f12) {
                    case 0x000:
                        return OperationId::ECALL;
                    case 0x001:
                        return OperationId::EBREAK;
                    case 0x002:
                        return OperationId::URET;
                    case 0x102:
                        return OperationId::SRET;
                    case 0x302:
                        return OperationId::MRET;
                    case 0x105:
                        return OperationId::WFI;
                    default:
                        if (funct7 == 0x09) return OperationId::SFENCE_VMA;
                        break;
                }
            } else {
                switch (funct3) {
                    case 1:
                        return OperationId::CSRRW;
                    case 2:
                        return OperationId::CSRRS;
                    case 3:
                        return OperationId::CSRRC;
                    case 5:
                        return OperationId::CSRRWI;
                    case 6:
                        return OperationId::CSRRSI;
                    case 7:
                        return OperationId::CSRRCI;
                    default:
                        break;
                }
            }
            break;
        case Opcode::kAmo: {
            const uint32_t funct5 = funct7 >> 2;
            switch (funct5) {
                case 0x02:
                    return OperationId::LR_W;
                case 0x03:
                    return OperationId::SC_W;
                case 0x01:
                    return OperationId::AMOSWAP_W;
                case 0x00:
                    return OperationId::AMOADD_W;
                case 0x04:
                    return OperationId::AMOXOR_W;
                case 0x0C:
                    return OperationId::AMOAND_W;
                case 0x08:
                    return OperationId::AMOOR_W;
                case 0x10:
                    return OperationId::AMOMIN_W;
                case 0x14:
                    return OperationId::AMOMAX_W;
                case 0x18:
                    return OperationId::AMOMINU_W;
                case 0x1C:
                    return OperationId::AMOMAXU_W;
                default:
                    break;
            }
            break;
        }
        case Opcode::kLoadFp:
            return (funct3 == 3) ? OperationId::FLD : OperationId::FLW;
        case Opcode::kStoreFp:
            return (funct3 == 3) ? OperationId::FSD : OperationId::FSW;
        case Opcode::kMadd:
            return ((funct7 & 0x03) == 1) ? OperationId::FMADD_D : OperationId::FMADD_S;
        case Opcode::kMsub:
            return ((funct7 & 0x03) == 1) ? OperationId::FMSUB_D : OperationId::FMSUB_S;
        case Opcode::kNmsub:
            return ((funct7 & 0x03) == 1) ? OperationId::FNMSUB_D : OperationId::FNMSUB_S;
        case Opcode::kNmadd:
            return ((funct7 & 0x03) == 1) ? OperationId::FNMADD_D : OperationId::FNMADD_S;
        case Opcode::kOpFp: {
            const uint32_t fmt = funct7 & 0x03;
            const uint32_t f5 = funct7 >> 2;
            const uint32_t rs2 = std::to_underlying(dec.rs2());
            if (fmt == 0) {  // Single precision (S)
                switch (f5) {
                    case 0x00:
                        return OperationId::FADD_S;
                    case 0x01:
                        return OperationId::FSUB_S;
                    case 0x02:
                        return OperationId::FMUL_S;
                    case 0x03:
                        return OperationId::FDIV_S;
                    case 0x04:
                        if (funct3 == 0) return OperationId::FSGNJ_S;
                        if (funct3 == 1) return OperationId::FSGNJN_S;
                        if (funct3 == 2) return OperationId::FSGNJX_S;
                        break;
                    case 0x05:
                        if (funct3 == 0) return OperationId::FMIN_S;
                        if (funct3 == 1) return OperationId::FMAX_S;
                        break;
                    case 0x08:
                        return (rs2 == 1) ? OperationId::FCVT_S_D : OperationId::UNKNOWN;
                    case 0x0B:
                        return OperationId::FSQRT_S;
                    case 0x14:
                        if (funct3 == 0) return OperationId::FLE_S;
                        if (funct3 == 1) return OperationId::FLT_S;
                        if (funct3 == 2) return OperationId::FEQ_S;
                        break;
                    case 0x18:
                        return (rs2 == 0)   ? OperationId::FCVT_W_S
                               : (rs2 == 1) ? OperationId::FCVT_WU_S
                                            : OperationId::UNKNOWN;
                    case 0x1A:
                        return (rs2 == 0)   ? OperationId::FCVT_S_W
                               : (rs2 == 1) ? OperationId::FCVT_S_WU
                                            : OperationId::UNKNOWN;
                    case 0x1C:
                        return (funct3 == 0)   ? OperationId::FMV_X_W
                               : (funct3 == 1) ? OperationId::FCLASS_S
                                               : OperationId::UNKNOWN;
                    case 0x1E:
                        return OperationId::FMV_W_X;
                    default:
                        break;
                }
            } else if (fmt == 1) {  // Double precision (D)
                switch (f5) {
                    case 0x00:
                        return OperationId::FADD_D;
                    case 0x01:
                        return OperationId::FSUB_D;
                    case 0x02:
                        return OperationId::FMUL_D;
                    case 0x03:
                        return OperationId::FDIV_D;
                    case 0x04:
                        if (funct3 == 0) return OperationId::FSGNJ_D;
                        if (funct3 == 1) return OperationId::FSGNJN_D;
                        if (funct3 == 2) return OperationId::FSGNJX_D;
                        break;
                    case 0x05:
                        if (funct3 == 0) return OperationId::FMIN_D;
                        if (funct3 == 1) return OperationId::FMAX_D;
                        break;
                    case 0x08:
                        return (rs2 == 0) ? OperationId::FCVT_D_S : OperationId::UNKNOWN;
                    case 0x0B:
                        return OperationId::FSQRT_D;
                    case 0x14:
                        if (funct3 == 0) return OperationId::FLE_D;
                        if (funct3 == 1) return OperationId::FLT_D;
                        if (funct3 == 2) return OperationId::FEQ_D;
                        break;
                    case 0x18:
                        return (rs2 == 0)   ? OperationId::FCVT_W_D
                               : (rs2 == 1) ? OperationId::FCVT_WU_D
                                            : OperationId::UNKNOWN;
                    case 0x1A:
                        return (rs2 == 0)   ? OperationId::FCVT_D_W
                               : (rs2 == 1) ? OperationId::FCVT_D_WU
                                            : OperationId::UNKNOWN;
                    case 0x1C:
                        return (funct3 == 1) ? OperationId::FCLASS_D : OperationId::UNKNOWN;
                    default:
                        break;
                }
            }
            break;
        }
        default:
            break;
    }

    return OperationId::UNKNOWN;
}

auto decompressInstruction(Instruction ir) -> Instruction {
    simrv::decode::Decoder dec(ir);
    if (!dec.is_compressed()) {
        return ir;
    }

    const uint32_t quadrant = ir & 0x3;
    const uint32_t op = dec.c_op();

    auto r_type = [](uint32_t opcode, uint32_t funct3, uint32_t funct7, uint32_t rd, uint32_t rs1,
                     uint32_t rs2) -> Instruction {
        return opcode | (rd << 7) | (funct3 << 12) | (rs1 << 15) | (rs2 << 20) | (funct7 << 25);
    };
    auto i_type = [](uint32_t opcode, uint32_t funct3, uint32_t rd, uint32_t rs1,
                     uint32_t imm) -> Instruction {
        return opcode | (rd << 7) | (funct3 << 12) | (rs1 << 15) | ((imm & 0xFFF) << 20);
    };
    auto s_type = [](uint32_t opcode, uint32_t funct3, uint32_t rs1, uint32_t rs2,
                     uint32_t imm) -> Instruction {
        return opcode | ((imm & 0x1F) << 7) | (funct3 << 12) | (rs1 << 15) | (rs2 << 20) |
               (((imm >> 5) & 0x7F) << 25);
    };
    auto b_type = [](uint32_t opcode, uint32_t funct3, uint32_t rs1, uint32_t rs2,
                     uint32_t imm) -> Instruction {
        return opcode | (((imm >> 11) & 0x1) << 7) | (((imm >> 1) & 0xF) << 8) | (funct3 << 12) |
               (rs1 << 15) | (rs2 << 20) | (((imm >> 5) & 0x3F) << 25) |
               (((imm >> 12) & 0x1) << 31);
    };
    auto u_type = [](uint32_t opcode, uint32_t rd, uint32_t imm) -> Instruction {
        return opcode | (rd << 7) | (imm & 0xFFFFF000);
    };
    auto j_type = [](uint32_t opcode, uint32_t rd, uint32_t imm) -> Instruction {
        return opcode | (rd << 7) | (((imm >> 12) & 0xFF) << 12) | (((imm >> 11) & 0x1) << 20) |
               (((imm >> 1) & 0x3FF) << 21) | (((imm >> 20) & 0x1) << 31);
    };

    const uint32_t rs1_rd = std::to_underlying(dec.c_rs1_rd());
    const uint32_t rs2 = std::to_underlying(dec.c_rs2());
    const uint32_t rs1_p = std::to_underlying(dec.c_rs1_rd_p());
    const uint32_t rs2_p = std::to_underlying(dec.c_rs2_p());

    if (quadrant == 0x0) {
        switch (op) {
            case 0: {
                const uint32_t imm = ((ir >> 7) & 0xF) << 6 | ((ir >> 11) & 0x3) << 4 |
                                     ((ir >> 5) & 0x1) << 3 | ((ir >> 6) & 0x1) << 2;
                if (imm == 0) return 0;
                return i_type(0x13, 0x0, rs2_p, 2, imm);
            }
            case 1: {
                const uint32_t imm = ((ir >> 5) & 0x3) << 6 | ((ir >> 10) & 0x7) << 3;
                return i_type(0x07, 0x3, rs2_p, rs1_p, imm);
            }
            case 2: {
                const uint32_t imm =
                    ((ir >> 5) & 0x1) << 6 | ((ir >> 10) & 0x7) << 3 | ((ir >> 6) & 0x1) << 2;
                return i_type(0x03, 0x2, rs2_p, rs1_p, imm);
            }
            case 3: {
                if constexpr (simrv::xlen::kIsXLen64) {
                    const uint32_t imm = ((ir >> 5) & 0x3) << 6 | ((ir >> 10) & 0x7) << 3;
                    return i_type(0x03, 0x3, rs2_p, rs1_p, imm);
                } else {
                    const uint32_t imm =
                        ((ir >> 5) & 0x1) << 6 | ((ir >> 10) & 0x7) << 3 | ((ir >> 6) & 0x1) << 2;
                    return i_type(0x07, 0x2, rs2_p, rs1_p, imm);
                }
            }
            case 5: {
                const uint32_t imm = ((ir >> 5) & 0x3) << 6 | ((ir >> 10) & 0x7) << 3;
                return s_type(0x27, 0x3, rs1_p, rs2_p, imm);
            }
            case 6: {
                const uint32_t imm =
                    ((ir >> 5) & 0x1) << 6 | ((ir >> 10) & 0x7) << 3 | ((ir >> 6) & 0x1) << 2;
                return s_type(0x23, 0x2, rs1_p, rs2_p, imm);
            }
            case 7: {
                if constexpr (simrv::xlen::kIsXLen64) {
                    const uint32_t imm = ((ir >> 5) & 0x3) << 6 | ((ir >> 10) & 0x7) << 3;
                    return s_type(0x23, 0x3, rs1_p, rs2_p, imm);
                } else {
                    const uint32_t imm =
                        ((ir >> 5) & 0x1) << 6 | ((ir >> 10) & 0x7) << 3 | ((ir >> 6) & 0x1) << 2;
                    return s_type(0x27, 0x2, rs1_p, rs2_p, imm);
                }
            }
            default:
                return 0;
        }
    } else if (quadrant == 0x1) {
        switch (op) {
            case 0: {
                const uint32_t imm = ((ir >> 2) & 0x1F) | (((ir >> 12) & 0x1) ? 0xFFFFFFE0 : 0);
                return i_type(0x13, 0x0, rs1_rd, rs1_rd, imm);
            }
            case 1: {
                if constexpr (simrv::xlen::kIsXLen64) {
                    const uint32_t imm = ((ir >> 2) & 0x1F) | (((ir >> 12) & 0x1) ? 0xFFFFFFE0 : 0);
                    return i_type(0x1B, 0x0, rs1_rd, rs1_rd, imm);
                } else {
                    const uint32_t imm =
                        ((ir >> 2) & 0x1) << 5 | ((ir >> 3) & 0x7) << 1 | ((ir >> 6) & 0x1) << 7 |
                        ((ir >> 7) & 0x1) << 6 | ((ir >> 8) & 0x1) << 10 | ((ir >> 9) & 0x3) << 8 |
                        ((ir >> 11) & 0x1) << 4 | (((ir >> 12) & 0x1) ? 0xFFFFF800 : 0);
                    return j_type(0x6F, 1, imm);
                }
            }
            case 2: {
                const uint32_t imm = ((ir >> 2) & 0x1F) | (((ir >> 12) & 0x1) ? 0xFFFFFFE0 : 0);
                return i_type(0x13, 0x0, rs1_rd, 0, imm);
            }
            case 3: {
                if (rs1_rd == 2) {
                    const uint32_t imm = ((ir >> 2) & 0x1) << 5 | ((ir >> 8) & 0x3) << 7 |
                                         ((ir >> 10) & 0x1) << 6 | ((ir >> 11) & 0x1) << 4 |
                                         (((ir >> 12) & 0x1) ? 0xFFFFFC00 : 0);
                    return i_type(0x13, 0x0, 2, 2, imm);
                } else {
                    const uint32_t imm =
                        (((ir >> 2) & 0x1F) << 12) | (((ir >> 12) & 0x1) ? 0xFFFE0000 : 0);
                    return u_type(0x37, rs1_rd, imm);
                }
            }
            case 4: {
                const uint32_t sub_op = (ir >> 10) & 0x3;
                if (sub_op == 0 || sub_op == 1) {
                    const uint32_t shamt = ((ir >> 2) & 0x1F) | (((ir >> 12) & 0x1) << 5);
                    const uint32_t funct7 = (sub_op == 1) ? 0x20 : 0x00;
                    return r_type(0x13, 0x5, funct7, rs1_p, rs1_p, shamt);
                } else if (sub_op == 2) {
                    const uint32_t imm = ((ir >> 2) & 0x1F) | (((ir >> 12) & 0x1) ? 0xFFFFFFE0 : 0);
                    return i_type(0x13, 0x7, rs1_p, rs1_p, imm);
                } else {
                    const uint32_t funct_sub = (ir >> 5) & 0x3;
                    const uint32_t bit12 = (ir >> 12) & 0x1;
                    if (bit12 == 0) {
                        if (funct_sub == 0) return r_type(0x33, 0x0, 0x20, rs1_p, rs1_p, rs2_p);
                        if (funct_sub == 1) return r_type(0x33, 0x4, 0x00, rs1_p, rs1_p, rs2_p);
                        if (funct_sub == 2) return r_type(0x33, 0x6, 0x00, rs1_p, rs1_p, rs2_p);
                        if (funct_sub == 3) return r_type(0x33, 0x7, 0x00, rs1_p, rs1_p, rs2_p);
                    } else {
                        if (funct_sub == 0) return r_type(0x3B, 0x0, 0x20, rs1_p, rs1_p, rs2_p);
                        if (funct_sub == 1) return r_type(0x3B, 0x0, 0x00, rs1_p, rs1_p, rs2_p);
                    }
                }
                break;
            }
            case 5: {
                const uint32_t imm =
                    ((ir >> 2) & 0x1) << 5 | ((ir >> 3) & 0x7) << 1 | ((ir >> 6) & 0x1) << 7 |
                    ((ir >> 7) & 0x1) << 6 | ((ir >> 8) & 0x1) << 10 | ((ir >> 9) & 0x3) << 8 |
                    ((ir >> 11) & 0x1) << 4 | (((ir >> 12) & 0x1) ? 0xFFFFF800 : 0);
                return j_type(0x6F, 0, imm);
            }
            case 6:
            case 7: {
                const uint32_t imm = ((ir >> 2) & 0x1) << 5 | ((ir >> 3) & 0x3) << 1 |
                                     ((ir >> 5) & 0x3) << 6 | ((ir >> 10) & 0x3) << 3 |
                                     (((ir >> 12) & 0x1) ? 0xFFFFFF00 : 0);
                return b_type(0x63, (op == 6) ? 0x0 : 0x1, rs1_p, 0, imm);
            }
        }
    } else if (quadrant == 0x2) {
        switch (op) {
            case 0: {
                const uint32_t shamt = ((ir >> 2) & 0x1F) | (((ir >> 12) & 0x1) << 5);
                return r_type(0x13, 0x1, 0x00, rs1_rd, rs1_rd, shamt);
            }
            case 1: {
                const uint32_t imm =
                    ((ir >> 2) & 0x7) << 6 | ((ir >> 5) & 0x3) << 3 | ((ir >> 12) & 0x1) << 5;
                return i_type(0x07, 0x3, rs1_rd, 2, imm);
            }
            case 2: {
                const uint32_t imm =
                    ((ir >> 2) & 0x3) << 6 | ((ir >> 4) & 0x7) << 2 | ((ir >> 12) & 0x1) << 5;
                return i_type(0x03, 0x2, rs1_rd, 2, imm);
            }
            case 3: {
                if constexpr (simrv::xlen::kIsXLen64) {
                    const uint32_t imm =
                        ((ir >> 2) & 0x7) << 6 | ((ir >> 5) & 0x3) << 3 | ((ir >> 12) & 0x1) << 5;
                    return i_type(0x03, 0x3, rs1_rd, 2, imm);
                } else {
                    const uint32_t imm =
                        ((ir >> 2) & 0x3) << 6 | ((ir >> 4) & 0x7) << 2 | ((ir >> 12) & 0x1) << 5;
                    return i_type(0x07, 0x2, rs1_rd, 2, imm);
                }
            }
            case 4: {
                const uint32_t bit12 = (ir >> 12) & 0x1;
                if (bit12 == 0) {
                    if (rs2 == 0)
                        return i_type(0x67, 0x0, 0, rs1_rd, 0);
                    else
                        return r_type(0x33, 0x0, 0x00, rs1_rd, 0, rs2);
                } else {
                    if (rs1_rd == 0 && rs2 == 0) return 0x00100073;
                    if (rs2 == 0)
                        return i_type(0x67, 0x0, 1, rs1_rd, 0);
                    else
                        return r_type(0x33, 0x0, 0x00, rs1_rd, rs1_rd, rs2);
                }
            }
            case 5: {
                const uint32_t imm = ((ir >> 7) & 0x7) << 3 | ((ir >> 10) & 0x7) << 6;
                return s_type(0x27, 0x3, 2, rs2, imm);
            }
            case 6: {
                const uint32_t imm = ((ir >> 7) & 0x3) << 6 | ((ir >> 9) & 0xF) << 2;
                return s_type(0x23, 0x2, 2, rs2, imm);
            }
            case 7: {
                if constexpr (simrv::xlen::kIsXLen64) {
                    const uint32_t imm = ((ir >> 7) & 0x7) << 3 | ((ir >> 10) & 0x7) << 6;
                    return s_type(0x23, 0x3, 2, rs2, imm);
                } else {
                    const uint32_t imm = ((ir >> 7) & 0x3) << 6 | ((ir >> 9) & 0xF) << 2;
                    return s_type(0x27, 0x2, 2, rs2, imm);
                }
            }
        }
    }
    return 0;
}

}  // namespace simrv::decode