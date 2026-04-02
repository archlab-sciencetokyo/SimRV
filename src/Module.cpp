/**
 * @file Module.cpp
 * @brief SimRV implementation unit.
 */
#include "Module.hpp"

#include <array>
#include <string_view>

namespace simrv::module {

namespace {
constexpr SignedWord sign_extend(Word value, unsigned bits) {
    const Word sign_bit = Word{1} << (bits - 1);
    const Word extend_mask = ~Word{0} << bits;
    return static_cast<SignedWord>((value & sign_bit) ? (value | extend_mask) : value);
}

constexpr Instruction make_i_type(Word imm, Word rs1, Funct3 funct3, Word rd, Opcode opcode) {
    return (imm << 20) | (rs1 << 15) | (static_cast<Instruction>(funct3) << 12) | (rd << 7) |
           static_cast<Instruction>(opcode);
}

constexpr Instruction make_r_type(Word funct7, Word rs2, Word rs1, Funct3 funct3, Word rd,
                                  Opcode opcode) {
    return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (static_cast<Instruction>(funct3) << 12) |
           (rd << 7) | static_cast<Instruction>(opcode);
}

constexpr Instruction make_s_type(Word imm, Word rs2, Word rs1, Funct3 funct3, Opcode opcode) {
    return (((imm >> 5) & 0x7F) << 25) | (rs2 << 20) | (rs1 << 15) |
           (static_cast<Instruction>(funct3) << 12) | ((imm & 0x1F) << 7) |
           static_cast<Instruction>(opcode);
}

constexpr Instruction make_b_type(Word imm, Word rs2, Word rs1, Funct3 funct3, Opcode opcode) {
    return (((imm >> 12) & 0x1) << 31) | (((imm >> 5) & 0x3F) << 25) | (rs2 << 20) | (rs1 << 15) |
           (static_cast<Instruction>(funct3) << 12) | (((imm >> 1) & 0xF) << 8) |
           (((imm >> 11) & 0x1) << 7) | static_cast<Instruction>(opcode);
}

constexpr Instruction make_u_type(Word imm, Word rd, Opcode opcode) {
    return (imm << 12) | (rd << 7) | static_cast<Instruction>(opcode);
}

constexpr Instruction make_j_type(Word imm, Word rd, Opcode opcode) {
    return (((imm >> 20) & 0x1) << 31) | (((imm >> 1) & 0x3FF) << 21) |
           (((imm >> 11) & 0x1) << 20) | (((imm >> 12) & 0xFF) << 12) | (rd << 7) |
           static_cast<Instruction>(opcode);
}

constexpr Instruction imm_i(Instruction ir) {
    return static_cast<Instruction>(sign_extend(ir >> 20, 12));
}

constexpr Instruction imm_s(Instruction ir) {
    const Word value = ((ir >> 25) << 5) | ((ir >> 7) & 0x1f);
    return static_cast<Instruction>(sign_extend(value, 12));
}

constexpr Instruction imm_b(Instruction ir) {
    const Word value = ((ir >> 31) << 12) | (((ir >> 7) & 1) << 11) | (((ir >> 25) & 0x3f) << 5) |
                       (((ir >> 8) & 0xF) << 1);
    return static_cast<Instruction>(sign_extend(value, 13));
}

constexpr Instruction imm_u(Instruction ir) {
    return static_cast<Instruction>(sign_extend(ir >> 12, 20));
}

constexpr Instruction imm_j(Instruction ir) {
    const Word value = ((ir >> 31) << 20) | (((ir >> 12) & 0xFF) << 12) |
                       (((ir >> 20) & 0x1) << 11) | (((ir >> 21) & 0x3FF) << 1);
    return static_cast<Instruction>(sign_extend(value, 21));
}
}  // namespace

/* immediate generation                                                                   */
Instruction CB_imm_gen(Instruction ir) {
    switch (opcode_of(ir)) {
        case Opcode::OpImm:
        case Opcode::Load:
        case Opcode::LoadFp:
        case Opcode::Jalr:
            return imm_i(ir);
        case Opcode::Store:
        case Opcode::StoreFp:
            return imm_s(ir);
        case Opcode::Branch:
            return imm_b(ir);
        case Opcode::Lui:
        case Opcode::Auipc:
            return imm_u(ir);
        case Opcode::Jal:
            return imm_j(ir);
        case Opcode::System:
            return static_cast<Instruction>((ir >> 15) & 0x1f);
        default:
            return 0;
    }
}

/* inst_decomp                                                                            */
Instruction decomp_c0(Instruction ir) {
    Word funct3 = (ir >> 13) & 0x7;
    Word rs1 = ((ir >> 7) & 0x7) + 8;
    Word rs2 = ((ir >> 2) & 0x7) + 8;
    Word rd = ((ir >> 2) & 0x7) + 8;
    Word uimm1 = (((ir >> 5) & 0x1) << 6) | (((ir >> 10) & 0x7) << 3) | (((ir >> 6) & 0x1) << 2);
    Word uimm2 = (((ir >> 5) & 0x3) << 6) | (((ir >> 10) & 0x7) << 3);
    Word nzuimm = (((ir >> 7) & 0xF) << 6) | (((ir >> 11) & 0x3) << 4) | (((ir >> 5) & 0x1) << 3) |
                  (((ir >> 6) & 0x1) << 2);

    Word ret = ir;
    switch (funct3) {
        case 0x0: {  // C.ADDI4SPN : addi rd', x2, nzuimm[9:2]
            ret = make_i_type(nzuimm, 2, Funct3::Add, rd, Opcode::OpImm);
            break;
        }
        case 0x1: {  // C.FLD : fld rd', offset[7:3](rs1')
            ret = make_i_type(uimm2, rs1, Funct3::Fld, rd, Opcode::LoadFp);
            break;
        }
        case 0x2: {  // C.LW : lw rd', offset[6:2](rs1')
            ret = make_i_type(uimm1, rs1, Funct3::Lw, rd, Opcode::Load);
            break;
        }
        case 0x3: {  // C.FLW : flw rd', offset[6:2](rs1')
            ret = make_i_type(uimm1, rs1, Funct3::Flw, rd, Opcode::LoadFp);
            break;
        }
        case 0x5: {  // C.FSD : fsd rs2', offset[7:3](rs1')
            ret = make_s_type(uimm2, rs2, rs1, Funct3::Fsd, Opcode::StoreFp);
            break;
        }
        case 0x6: {  // C.SW : sw rs2', offset[6:2](rs1')
            ret = make_s_type(uimm1, rs2, rs1, Funct3::Sw, Opcode::Store);
            break;
        }
        case 0x7: {  // C.FSW : fsw rs2', offset[6:2](rs1')
            ret = make_s_type(uimm1, rs2, rs1, Funct3::Fsw, Opcode::StoreFp);
            break;
        }
    }
    return ret;
}

Instruction decomp_c1(Instruction ir) {
    Word funct1 = (ir >> 10) & 0x3;
    Word funct2 = (((ir >> 12) & 0x1) << 2) | ((ir >> 5) & 0x3);
    Word funct3 = (ir >> 13) & 0x7;
    Word rs1 = ((ir >> 7) & 0x7) + 8;
    Word rs2 = ((ir >> 2) & 0x7) + 8;
    Word rd = ((ir >> 7) & 0x7) + 8;
    Word nzimm =
        (((ir >> 12) & 1) ? 0xFFFFFFE0 : 0x0) | (((ir >> 12) & 1) << 5) | ((ir >> 2) & 0x1F);
    Word shamt = nzimm & 0x1F;
    Word uimm1 = (((ir >> 12) & 0x1) << 11) | (((ir >> 8) & 0x1) << 10) | (((ir >> 9) & 0x3) << 8) |
                 (((ir >> 6) & 0x1) << 7) | (((ir >> 7) & 0x1) << 6) | (((ir >> 2) & 0x1) << 5) |
                 (((ir >> 11) & 0x1) << 4) | (((ir >> 3) & 0x7) << 1);
    Word uimm2 = (((ir >> 12) & 0x1) << 8) | (((ir >> 5) & 0x3) << 6) | (((ir >> 2) & 0x1) << 5) |
                 (((ir >> 10) & 0x3) << 3) | (((ir >> 3) & 0x3) << 1);
    Word uimm3 = (((ir >> 12) & 1) << 5) | ((ir >> 2) & 0x1F);
    Word uimm4 = (((ir >> 12) & 0x1) << 9) | (((ir >> 3) & 0x3) << 7) | (((ir >> 5) & 0x1) << 6) |
                 (((ir >> 2) & 0x1) << 5) | (((ir >> 6) & 0x1) << 4);
    Word imm1 = ((uimm1 & 0x800) ? 0xFFFFF000 : 0x0) | uimm1;
    Word imm2 = ((uimm2 & 0x100) ? 0xFFFFFE00 : 0x0) | uimm2;
    Word imm3 = ((uimm3 & 0x20) ? 0xFFFFFFC0 : 0x0) | uimm3;
    Word imm4 = ((uimm4 & 0x200) ? 0xFFFFFE00 : 0x0) | uimm4;

    Word ret = ir;
    switch (funct3) {
        case 0x0: {  // C.ADDI : addi rd, rd, nzimm[5:0]
            ret =
                make_i_type(nzimm, (ir >> 7) & 0x1F, Funct3::Add, (ir >> 7) & 0x1F, Opcode::OpImm);
            break;
        }
        case 0x1: {  // C.JAL : jal x1, offset[11:1]
            ret = make_j_type(imm1, 1, Opcode::Jal);
            break;
        }
        case 0x2: {  // C.LI : addi rd, x0, imm[5:0]
            ret = make_i_type(imm3, 0, Funct3::Add, (ir >> 7) & 0x1F, Opcode::OpImm);
            break;
        }
        case 0x3: {
            if (((ir >> 7) & 0x1F) == 2) {  // C.ADDI16SP : addi x2, x2, nzimm[9:4]
                ret = make_i_type(imm4, 2, Funct3::Add, 2, Opcode::OpImm);
            } else {  // C.LUI : lui rd, nzimm[17:12]
                ret = make_u_type(nzimm, (ir >> 7) & 0x1F, Opcode::Lui);
            }
            break;
        }
        case 0x5: {  // C.J : jal x0, offset[11:1]
            ret = make_j_type(imm1, 0, Opcode::Jal);
            break;
        }
        case 0x6: {  // C.BEQZ : beq sr1', x0, offset[8:1]
            ret = make_b_type(imm2, 0, rs1, Funct3::Beq, Opcode::Branch);
            break;
        }
        case 0x7: {  // C.BNEZ : bne rs1', x0, offset[8:1]
            ret = make_b_type(imm2, 0, rs1, Funct3::Bne, Opcode::Branch);
            break;
        }
        case 0x4: {
            switch (funct1) {
                case 0x0: {  // C.SRLI : srli rd', rd', shamt[5:0]
                    ret = make_i_type(shamt, rd, Funct3::Srl, rd, Opcode::OpImm);
                    break;
                }
                case 0x1: {  // C.SRAI : srai rd', rd', shamt[5:0]
                    ret = make_i_type(shamt, rd, Funct3::Srl, rd, Opcode::OpImm) |
                          (Instruction{1} << 30);
                    break;
                }
                case 0x2: {  // C.ANDI : andi rd', rd', imm[5:0]
                    ret = make_i_type(nzimm, rd, Funct3::And, rd, Opcode::OpImm);
                    break;
                }
                case 0x3: {
                    switch (funct2) {
                        case 0x0: {  // C.SUB : sub rd', rd', rs2'
                            ret = make_r_type(1, rs2, rd, Funct3::Add, rd, Opcode::Op);
                            break;
                        }
                        case 0x1: {  // C.XOR : xor rd', rd', rs2'
                            ret = make_r_type(0, rs2, rd, Funct3::Xor, rd, Opcode::Op);
                            break;
                        }
                        case 0x2: {  // C.OR : or rd', rd', rs2'
                            ret = make_r_type(0, rs2, rd, Funct3::Or, rd, Opcode::Op);
                            break;
                        }
                        case 0x3: {  // C.AND : and rd', rd', rs2'
                            ret = make_r_type(0, rs2, rd, Funct3::And, rd, Opcode::Op);
                            break;
                        }
                    }
                    break;
                }
            }
            break;
        }
    }
    return ret;
}

Instruction decomp_c2(Instruction ir) {
    Word funct3 = (ir >> 13) & 0x7;
    Word rd = (ir >> 7) & 0x1F;
    Word rs2 = (ir >> 2) & 0x1F;
    Word uimm1 = (((ir >> 2) & 0x7) << 6) | (((ir >> 12) & 0x1) << 5) | (((ir >> 5) & 0x3) << 3);
    Word uimm2 = (((ir >> 2) & 0x3) << 6) | (((ir >> 12) & 0x1) << 5) | (((ir >> 4) & 0x7) << 2);
    Word uimm3 = (((ir >> 7) & 0x7) << 6) | (((ir >> 10) & 0x7) << 3);
    Word uimm4 = (((ir >> 7) & 0x3) << 6) | (((ir >> 9) & 0xF) << 2);
    Word nzuimm = (((ir >> 12) & 1) << 5) | ((ir >> 2) & 0x1F);
    Word shamt = nzuimm & 0x1F;

    Word ret = ir;
    switch (funct3) {
        case 0x0: {  // C.SLLI : slli rd, rd, shamt[5:0]
            ret = make_i_type(shamt, rd, Funct3::Sll, rd, Opcode::OpImm);
            break;
        }
        case 0x1: {  // C.FLDSP : fld rd, offset[8:3](x2)
            ret = make_i_type(uimm1, 2, Funct3::Fld, rd, Opcode::LoadFp);
            break;
        }
        case 0x2: {  // C.LWSP : lw rd, offset[7:2](x2)
            ret = make_i_type(uimm2, 2, Funct3::Lw, rd, Opcode::Load);
            break;
        }
        case 0x3: {  // C.FLWSP : flw rd, offset[7:2](x2)
            ret = make_i_type(uimm2, 2, Funct3::Flw, rd, Opcode::LoadFp);
            break;
        }
        case 0x5: {  // C.FSDSP : fsd rs2, offset[8:3](x2)
            ret = make_s_type(uimm3, rs2, 2, Funct3::Fsd, Opcode::StoreFp);
            break;
        }
        case 0x6: {  // C.SWSP : sw rs2, offset[7:2](x2)
            ret = make_s_type(uimm4, rs2, 2, Funct3::Sw, Opcode::Store);
            break;
        }
        case 0x7: {  // C.FSWSP : fsw rs2, offset[7:2](x2)
            ret = make_s_type(uimm4, rs2, 2, Funct3::Fsw, Opcode::StoreFp);
            break;
        }
        case 0x4: {
            switch ((ir >> 12) & 0x1) {
                case 0: {
                    // C.JR : jalr x0, rs1, 0
                    if (rd != 0 && rs2 == 0) {
                        ret = make_i_type(0, rd, Funct3::Add, 0, Opcode::Jalr);
                    }
                    // C.MV : add rd, x0, rs2
                    if (rd != 0 && rs2 != 0) {
                        ret = make_r_type(0, rs2, 0, Funct3::Add, rd, Opcode::Op);
                    }
                    break;
                }
                case 1:
                    // C.EBREAK : ebreak
                    if (rd == 0 && rs2 == 0) {
                        ret = make_i_type(static_cast<Instruction>(Funct12Priv::Ebreak), 0,
                                          Funct3::Priv, 0, Opcode::System);
                    }
                    // C.JALR : jalr x1, rs, 0
                    if (rd != 0 && rs2 == 0) {
                        ret = make_i_type(0, rd, Funct3::Add, 1, Opcode::Jalr);
                    }
                    // C.ADD : add rd, rd, rs2
                    if (rd != 0 && rs2 != 0) {
                        ret = make_r_type(0, rs2, rd, Funct3::Add, rd, Opcode::Op);
                    }
                    break;
            }
            break;
        }
    }
    return ret;
}

constexpr Instruction decode_compressed_dispatch(CompressedInstruction ir) {
    switch (compressed_opcode_of(ir)) {
        case Opcode::C0:
            return decomp_c0(ir);
        case Opcode::C1:
            return decomp_c1(ir);
        case Opcode::C2:
            return decomp_c2(ir);
        default:
            return ir;
    }
}

Instruction CB_inst_decomp(Instruction ir) {
    if ((ir & 0x3u) == 0x3u) {
        return ir;
    }
    return decode_compressed_dispatch(static_cast<CompressedInstruction>(ir & 0xFFFF));
}

Register ALU_IM(Register in1, Register in2, Instruction funct3, Instruction funct7) {
    Word ret = 0;
    Word shamt = in2 & 0x1f;
    const Funct3 f3 = static_cast<Funct3>(funct3);

    if ((funct7 & 0x1) == 0) {
        switch (f3) {
            case Funct3::Add: {
                if (funct7)
                    ret = in1 - in2;
                else
                    ret = in1 + in2;
                break;
            }
            case Funct3::Sll: {
                ret = in1 << shamt;
                break;
            }
            case Funct3::Slt: {
                ret = (SignedWord)in1 < (SignedWord)in2;
                break;
            }
            case Funct3::Sltu: {
                ret = in1 < in2;
                break;
            }
            case Funct3::Xor: {
                ret = in1 ^ in2;
                break;
            }
            case Funct3::Srl: {
                if (funct7)
                    ret = (SignedWord)in1 >> shamt;
                else
                    ret = in1 >> shamt;
                break;
            }
            case Funct3::Or: {
                ret = in1 | in2;
                break;
            }
            case Funct3::And: {
                ret = in1 & in2;
                break;
            }
            default: {
                break;
            }
        }
    } else {
        Counter mul_SS = (int64_t)((SignedWord)in1) * (int64_t)((SignedWord)in2);
        Counter mul_SU = (int64_t)((SignedWord)in1) * (Counter)in2;
        Counter mul_UU = (Counter)in1 * (Counter)in2;
        switch (f3) {
            case Funct3::Mul: {
                ret = mul_SS & 0xFFFFFFFF;
                break;
            }
            case Funct3::Mulh: {
                ret = (mul_SS >> 32) & 0xFFFFFFFF;
                break;
            }
            case Funct3::Mulhsu: {
                ret = (mul_SU >> 32) & 0xFFFFFFFF;
                break;
            }
            case Funct3::Mulhu: {
                ret = (mul_UU >> 32) & 0xFFFFFFFF;
                break;
            }
            case Funct3::Div: {
                if (in2 == 0xFFFFFFFF)
                    ret = in1;
                else if (in2 == 0)
                    ret = 0xFFFFFFFF;
                else
                    ret = (SignedWord)in1 / (SignedWord)in2;
                break;
            }
            case Funct3::Divu: {
                if (in2 == 0)
                    ret = 0xFFFFFFFF;
                else
                    ret = in1 / in2;
                break;
            }
            case Funct3::Rem: {
                if (in2 == 0xFFFFFFFF)
                    ret = 0;
                else if (in2 == 0)
                    ret = in1;
                else
                    ret = (SignedWord)in1 % (SignedWord)in2;
                break;
            }
            case Funct3::Remu: {
                if (in2 == 0)
                    ret = in1;
                else
                    ret = in1 % in2;
                break;
            }
            default: {
                break;
            }
        }
    }
    return ret;
}

Instruction ALU_B(Register in1, Register in2, Instruction funct3) {
    Word ret = 0;
    switch (static_cast<Funct3>(funct3)) {
        case Funct3::Beq: {
            ret = in1 == in2;
            break;
        }
        case Funct3::Bne: {
            ret = in1 != in2;
            break;
        }
        case Funct3::Blt: {
            ret = (SignedWord)in1 < (SignedWord)in2;
            break;
        }
        case Funct3::Bge: {
            ret = (SignedWord)in1 >= (SignedWord)in2;
            break;
        }
        case Funct3::Bltu: {
            ret = in1 < in2;
            break;
        }
        case Funct3::Bgeu: {
            ret = in1 >= in2;
            break;
        }
        default:
            break;
    }
    return ret;
}

Register ALU_A(Register in1, Register in2, Instruction funct5) {
    Word ret = 0;
    switch (static_cast<Funct5Amo>(funct5)) {
        case Funct5Amo::Lr: {
            ret = 0;
            break;
        }
        case Funct5Amo::Sc: {
            ret = in1;
            break;
        }
        case Funct5Amo::Swap: {
            ret = in1;
            break;
        }
        case Funct5Amo::Add: {
            ret = in1 + in2;
            break;
        }
        case Funct5Amo::And: {
            ret = in1 & in2;
            break;
        }
        case Funct5Amo::Or: {
            ret = in1 | in2;
            break;
        }
        case Funct5Amo::Xor: {
            ret = in1 ^ in2;
            break;
        }
        case Funct5Amo::Min: {
            ret = (SignedWord)in1 < (SignedWord)in2 ? in1 : in2;
            break;
        }
        case Funct5Amo::Minu: {
            ret = in1 < in2 ? in1 : in2;
            break;
        }
        case Funct5Amo::Max: {
            ret = (SignedWord)in1 > (SignedWord)in2 ? in1 : in2;
            break;
        }
        case Funct5Amo::Maxu: {
            ret = in1 > in2 ? in1 : in2;
            break;
        }
        default:
            break;
    }
    return ret;
}

CSRValue ALU_C(CSRValue rcsr, Register rrs1, Instruction imm, Instruction funct3) {
    Word ret = 0;
    switch (static_cast<Funct3>(funct3)) {
        case Funct3::Csrrw:
            ret = rrs1;
            break;
        case Funct3::Csrrs:
            ret = rcsr | rrs1;
            break;
        case Funct3::Csrrc:
            ret = rcsr & (~rrs1);
            break;
        case Funct3::Csrrwi:
            ret = imm;
            break;
        case Funct3::Csrrsi:
            ret = rcsr | imm;
            break;
        case Funct3::Csrrci:
            ret = rcsr & (~imm);
            break;
        default:
            break;
    }
    return ret;
}

const std::array<std::string_view, kOperationIdCount> OPERATION_NAME = {
    /* RV32I */
    "LUI", "AUIPC", "JAL", "JALR", "BEQ", "BNE", "BLT", "BGE", "BLTU", "BGEU", "LB", "LH", "LW",
    "LBU", "LHU", "SB", "SH", "SW", "ADDI", "SLTI", "SLTIU", "XORI", "ORI", "ANDI", "SLLI", "SRLI",
    "SRAI", "ADD", "SUB", "SLL", "SLT", "SLTU", "XOR", "SRL", "SRA", "OR", "AND", "FENCE",
    "FENCE_I", "ECALL", "EBREAK", "CSRRW", "CSRRS", "CSRRC", "CSRRWI", "CSRRSI", "CSRRCI",
    /* Privileged */
    "URET", "SRET", "MRET", "WFI", "SFENCE_VMA",
    /* RV32M */
    "MUL", "MULH", "MULHSU", "MULHU", "DIV", "DIVU", "REM", "REMU",
    /* RV32A */
    "LR_W", "SC_W", "AMOSWAP_W", "AMOADD_W", "AMOXOR_W", "AMOAND_W", "AMOOR_W", "AMOMIN_W",
    "AMOMAX_W", "AMOMINU_W", "AMOMAXU_W",
    /* RV32F */
    "FLW", "FSW", "FMADD_S", "FMSUB_S", "FNMADD_S", "FNMSUB_S", "FADD_S", "FSUB_S", "FMUL_S",
    "FDIV_S", "FSQRT_S", "FSGNJ_S", "FSGNJN_S", "FSGNJX_S", "FMIN_S", "FMAX_S", "FCVT_W_S",
    "FCVT_WU_S", "FMV_X_W", "FEQ_S", "FLT_S", "FLE_S", "FCLASS_S", "FCVT_S_W", "FCVT_S_WU",
    "FMV_W_X",
    /* RV32D */
    "FLD", "FSD", "FMADD_D", "FMSUB_D", "FNMSUB_D", "FNMADD_D", "FADD_D", "FSUB_D", "FMUL_D",
    "FDIV_D", "FSQRT_D", "FSGNJ_D", "FSGNJN_D", "FSGNJX_D", "FMIN_D", "FMAX_D", "FCVT_S_D",
    "FCVT_D_S", "FEQ_D", "FLT_D", "FLE_D", "FCLASS_D", "FCVT_W_D", "FCVT_WU_D", "FCVT_D_W",
    "FCVT_D_WU",
    /* Others */
    "UNKNOWN"};

namespace {
template <typename Table>
constexpr OperationId lookup_operation(const Table& table, std::size_t index) {
    return index < table.size() ? table[index] : UNKNOWN;
}

constexpr std::array<OperationId, 8> kBranchOps = {BEQ, BNE, UNKNOWN, UNKNOWN,
                                                   BLT, BGE, BLTU,    BGEU};
constexpr std::array<OperationId, 8> kLoadOps = {LB, LH, LW, UNKNOWN, LBU, LHU, UNKNOWN, UNKNOWN};
constexpr std::array<OperationId, 8> kStoreOps = {SB,      SH,      SW,      UNKNOWN,
                                                  UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN};
constexpr std::array<OperationId, 8> kOpImmOps = {ADDI,    SLTI, SLTIU, XORI,
                                                  UNKNOWN, ORI,  ANDI,  SLLI};
constexpr std::array<OperationId, 8> kOpBaseOps = {ADD, UNKNOWN, SLT, SLTU, XOR, UNKNOWN, OR, AND};
constexpr std::array<OperationId, 8> kOpMulDivOps = {MUL, MULH, MULHSU, MULHU,
                                                     DIV, DIVU, REM,    REMU};
constexpr std::array<OperationId, 8> kMiscMemOps = {FENCE,   FENCE_I, UNKNOWN, UNKNOWN,
                                                    UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN};

constexpr std::array<OperationId, 32> kAmoOps = [] {
    std::array<OperationId, 32> table{};
    table.fill(UNKNOWN);
    table[static_cast<std::size_t>(Funct5Amo::Add)] = AMOADD_W;
    table[static_cast<std::size_t>(Funct5Amo::Swap)] = AMOSWAP_W;
    table[static_cast<std::size_t>(Funct5Amo::Lr)] = LR_W;
    table[static_cast<std::size_t>(Funct5Amo::Sc)] = SC_W;
    table[static_cast<std::size_t>(Funct5Amo::Xor)] = AMOXOR_W;
    table[static_cast<std::size_t>(Funct5Amo::And)] = AMOAND_W;
    table[static_cast<std::size_t>(Funct5Amo::Or)] = AMOOR_W;
    table[static_cast<std::size_t>(Funct5Amo::Min)] = AMOMIN_W;
    table[static_cast<std::size_t>(Funct5Amo::Max)] = AMOMAX_W;
    table[static_cast<std::size_t>(Funct5Amo::Minu)] = AMOMINU_W;
    table[static_cast<std::size_t>(Funct5Amo::Maxu)] = AMOMAXU_W;
    return table;
}();

constexpr std::array<OperationId, 8> kCsrOps = {UNKNOWN, CSRRW,  CSRRS,  CSRRC,
                                                UNKNOWN, CSRRWI, CSRRSI, CSRRCI};
constexpr std::array<OperationId, 8> kLoadFpOps = {UNKNOWN, UNKNOWN, FLW,     FLD,
                                                   UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN};
constexpr std::array<OperationId, 8> kStoreFpOps = {UNKNOWN, UNKNOWN, FSW,     FSD,
                                                    UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN};

constexpr OperationId decode_funct12_priv(Funct12Priv funct12, Instruction funct7) {
    switch (funct12) {
        case Funct12Priv::Ecall:
            return ECALL;
        case Funct12Priv::Ebreak:
            return EBREAK;
        case Funct12Priv::Uret:
            return URET;
        case Funct12Priv::Sret:
            return SRET;
        case Funct12Priv::Mret:
            return MRET;
        case Funct12Priv::Wfi:
            return WFI;
        default:
            return funct7 == static_cast<Instruction>(Funct7Priv::SfenceVma) ? SFENCE_VMA : UNKNOWN;
    }
}
}  // namespace

OperationId decoder(Instruction ir) {
    const Opcode opcode = opcode_of(ir);
    const Funct3 funct3 = funct3_of(ir);
    const Funct5Amo funct5 = funct5_of(ir);
    const Instruction funct7 = ir >> 25;
    const Funct12Priv funct12 = static_cast<Funct12Priv>(funct12_of(ir));

    switch (opcode) {
        case Opcode::Lui:
            return LUI;
        case Opcode::Auipc:
            return AUIPC;
        case Opcode::Jal:
            return JAL;
        case Opcode::Jalr:
            return JALR;
        case Opcode::Branch:
            return lookup_operation(kBranchOps, static_cast<std::size_t>(funct3));
        case Opcode::Load:
            return lookup_operation(kLoadOps, static_cast<std::size_t>(funct3));
        case Opcode::Store:
            return lookup_operation(kStoreOps, static_cast<std::size_t>(funct3));
        case Opcode::OpImm:
            return (funct3 == Funct3::Srl)
                       ? (funct7 ? SRAI : SRLI)
                       : lookup_operation(kOpImmOps, static_cast<std::size_t>(funct3));
        case Opcode::Op:
            if (funct7 & 0x1) {
                return lookup_operation(kOpMulDivOps, static_cast<std::size_t>(funct3));
            }
            if (funct3 == Funct3::Add) {
                return funct7 ? SUB : ADD;
            }
            if (funct3 == Funct3::Srl) {
                return funct7 ? SRA : SRL;
            }
            return lookup_operation(kOpBaseOps, static_cast<std::size_t>(funct3));
        case Opcode::MiscMem:
            return lookup_operation(kMiscMemOps, static_cast<std::size_t>(funct3));
        case Opcode::Amo:
            return lookup_operation(kAmoOps, static_cast<std::size_t>(funct5));
        case Opcode::System:
            return (funct3 == Funct3::Priv)
                       ? decode_funct12_priv(funct12, funct7)
                       : lookup_operation(kCsrOps, static_cast<std::size_t>(funct3));
        case Opcode::LoadFp:
            return lookup_operation(kLoadFpOps, static_cast<std::size_t>(funct3));
        case Opcode::StoreFp:
            return lookup_operation(kStoreFpOps, static_cast<std::size_t>(funct3));
        case Opcode::MAdd:
            switch ((ir >> 25) & 0x3) {
                case 0x0:
                    return FMADD_S;
                case 0x1:
                    return FMADD_D;
                default:
                    return UNKNOWN;
            }
        case Opcode::MSub:
            switch ((ir >> 25) & 0x3) {
                case 0x0:
                    return FMSUB_S;
                case 0x1:
                    return FMSUB_D;
                default:
                    return UNKNOWN;
            }
        case Opcode::NMAdd:
            switch ((ir >> 25) & 0x3) {
                case 0x0:
                    return FNMADD_S;
                case 0x1:
                    return FNMADD_D;
                default:
                    return UNKNOWN;
            }
        case Opcode::NMSub:
            switch ((ir >> 25) & 0x3) {
                case 0x0:
                    return FNMSUB_S;
                case 0x1:
                    return FNMSUB_D;
                default:
                    return UNKNOWN;
            }
        case Opcode::OpFp:
            switch (ir >> 25) {
                case 0x00:
                    return FADD_S;
                case 0x01:
                    return FADD_D;
                case 0x04:
                    return FSUB_S;
                case 0x05:
                    return FSUB_D;
                case 0x08:
                    return FMUL_S;
                case 0x09:
                    return FMUL_D;
                case 0x0C:
                    return FDIV_S;
                case 0x0D:
                    return FDIV_D;
                case 0x10:
                    switch (static_cast<std::size_t>(funct3)) {
                        case 0x0:
                            return FSGNJ_S;
                        case 0x1:
                            return FSGNJN_S;
                        case 0x2:
                            return FSGNJX_S;
                        default:
                            return UNKNOWN;
                    }
                case 0x11:
                    switch (static_cast<std::size_t>(funct3)) {
                        case 0x0:
                            return FSGNJ_D;
                        case 0x1:
                            return FSGNJN_D;
                        case 0x2:
                            return FSGNJX_D;
                        default:
                            return UNKNOWN;
                    }
                case 0x14:
                    switch (static_cast<std::size_t>(funct3)) {
                        case 0x0:
                            return FMIN_S;
                        case 0x1:
                            return FMAX_S;
                        default:
                            return UNKNOWN;
                    }
                case 0x15:
                    switch (static_cast<std::size_t>(funct3)) {
                        case 0x0:
                            return FMIN_D;
                        case 0x1:
                            return FMAX_D;
                        default:
                            return UNKNOWN;
                    }
                case 0x20:
                    return FCVT_S_D;
                case 0x21:
                    return FCVT_D_S;
                case 0x2C:
                    return FSQRT_S;
                case 0x2D:
                    return FSQRT_D;
                case 0x50:
                    switch (static_cast<std::size_t>(funct3)) {
                        case 0x0:
                            return FLE_S;
                        case 0x1:
                            return FLT_S;
                        case 0x2:
                            return FEQ_S;
                        default:
                            return UNKNOWN;
                    }
                case 0x51:
                    switch (static_cast<std::size_t>(funct3)) {
                        case 0x0:
                            return FLE_D;
                        case 0x1:
                            return FLT_D;
                        case 0x2:
                            return FEQ_D;
                        default:
                            return UNKNOWN;
                    }
                case 0x60:
                    switch ((ir >> 20) & 0x1F) {
                        case 0x0:
                            return FCVT_W_S;
                        case 0x1:
                            return FCVT_WU_S;
                        default:
                            return UNKNOWN;
                    }
                case 0x61:
                    switch ((ir >> 20) & 0x1F) {
                        case 0x0:
                            return FCVT_W_D;
                        case 0x1:
                            return FCVT_WU_D;
                        default:
                            return UNKNOWN;
                    }
                case 0x68:
                    switch ((ir >> 20) & 0x1F) {
                        case 0x0:
                            return FCVT_S_W;
                        case 0x1:
                            return FCVT_S_WU;
                        default:
                            return UNKNOWN;
                    }
                case 0x69:
                    switch ((ir >> 20) & 0x1F) {
                        case 0x0:
                            return FCVT_D_W;
                        case 0x1:
                            return FCVT_D_WU;
                        default:
                            return UNKNOWN;
                    }
                case 0x70:
                    switch (static_cast<std::size_t>(funct3)) {
                        case 0x0:
                            return FMV_X_W;
                        case 0x1:
                            return FCLASS_S;
                        default:
                            return UNKNOWN;
                    }
                case 0x71:
                    return FCLASS_D;
                case 0x78:
                    return FMV_W_X;
                default:
                    return UNKNOWN;
            }
        default:
            fprintf(stdout, "__ Unknown Instruction %08x\n", ir);
            exit(0);
    }
}
}  // namespace simrv::module
