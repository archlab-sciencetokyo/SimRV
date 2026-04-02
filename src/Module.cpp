/**
 * @file Module.cpp
 * @brief SimRV implementation unit.
 */
#include "Module.hpp"

namespace simrv::module {

/* immediate generation                                                                   */
Instruction CB_imm_gen(Instruction ir) {
    Word ret = 0;

    Word uimm_I = ir >> 20;
    SignedWord imm_I = (SignedWord)(uimm_I | (uimm_I & 0x800 ? 0xFFFFF000 : 0x0));

    Word uimm_S = ((ir >> 25) << 5) | ((ir >> 7) & 0x1f);
    SignedWord imm_S = (SignedWord)(uimm_S | (uimm_S & 0x800 ? 0xFFFFF000 : 0x0));

    Word uimm_B = ((ir >> 31) << 12) | (((ir >> 7) & 1) << 11) | (((ir >> 25) & 0x3f) << 5) |
                  (((ir >> 8) & 0xF) << 1);
    SignedWord imm_B = (SignedWord)(uimm_B | (uimm_B & 0x1000 ? 0xFFFFE000 : 0x0));

    Word uimm_U = ir >> 12;
    SignedWord imm_U = (SignedWord)(uimm_U | (uimm_U & 0x80000 ? 0xFFF00000 : 0x0));

    Word uimm_J = ((ir >> 31) << 20) | (((ir >> 12) & 0xFF) << 12) | (((ir >> 20) & 0x1) << 11) |
                  (((ir >> 21) & 0x3FF) << 1);
    SignedWord imm_J = (SignedWord)(uimm_J | (uimm_J & 0x100000 ? 0xFFE00000 : 0x0));

    Word zimm = (Word)((ir >> 15) & 0x1f);

    switch (opcode_of(ir)) {
        case Opcode::OpImm: {
            ret = imm_I;
            break;
        }
        case Opcode::Load: {
            ret = imm_I;
            break;
        }
        case Opcode::LoadFp: {
            ret = imm_I;
            break;
        }
        case Opcode::Jalr: {
            ret = imm_I;
            break;
        }
        case Opcode::Store: {
            ret = imm_S;
            break;
        }
        case Opcode::StoreFp: {
            ret = imm_S;
            break;
        }
        case Opcode::Branch: {
            ret = imm_B;
            break;
        }
        case Opcode::Lui: {
            ret = imm_U;
            break;
        }
        case Opcode::Auipc: {
            ret = imm_U;
            break;
        }
        case Opcode::Jal: {
            ret = imm_J;
            break;
        }
        case Opcode::System: {
            ret = zimm;
            break;
        }
        default: {
            ret = 0;
            break;
        }
    }

    return ret;
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
            ret = (nzuimm << 20) | (2 << 15) | (static_cast<Instruction>(Funct3::Add) << 12) |
                  (rd << 7) | static_cast<Instruction>(Opcode::OpImm);
            break;
        }
        case 0x1: {  // C.FLD : fld rd', offset[7:3](rs1')
            ret = (uimm2 << 20) | (rs1 << 15) | (static_cast<Instruction>(Funct3::Fld) << 12) |
                  (rd << 7) | static_cast<Instruction>(Opcode::LoadFp);
            break;
        }
        case 0x2: {  // C.LW : lw rd', offset[6:2](rs1')
            ret = (uimm1 << 20) | (rs1 << 15) | (static_cast<Instruction>(Funct3::Lw) << 12) |
                  (rd << 7) | static_cast<Instruction>(Opcode::Load);
            break;
        }
        case 0x3: {  // C.FLW : flw rd', offset[6:2](rs1')
            ret = (uimm1 << 20) | (rs1 << 15) | (static_cast<Instruction>(Funct3::Flw) << 12) |
                  (rd << 7) | static_cast<Instruction>(Opcode::LoadFp);
            break;
        }
        case 0x5: {  // C.FSD : fsd rs2', offset[7:3](rs1')
            ret = (((uimm2 >> 5) & 0x7F) << 25) | (rs2 << 20) | (rs1 << 15) |
                  (static_cast<Instruction>(Funct3::Fsd) << 12) | ((uimm2 & 0x1F) << 7) |
                  static_cast<Instruction>(Opcode::StoreFp);
            break;
        }
        case 0x6: {  // C.SW : sw rs2', offset[6:2](rs1')
            ret = (((uimm1 >> 5) & 0x7F) << 25) | (rs2 << 20) | (rs1 << 15) |
                  (static_cast<Instruction>(Funct3::Sw) << 12) | ((uimm1 & 0x1F) << 7) |
                  static_cast<Instruction>(Opcode::Store);
            break;
        }
        case 0x7: {  // C.FSW : fsw rs2', offset[6:2](rs1')
            ret = (((uimm1 >> 5) & 0x7F) << 25) | (rs2 << 20) | (rs1 << 15) |
                  (static_cast<Instruction>(Funct3::Fsw) << 12) | ((uimm1 & 0x1F) << 7) |
                  static_cast<Instruction>(Opcode::StoreFp);
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
            ret = (nzimm << 20) | (((ir >> 7) & 0x1F) << 15) |
                  (static_cast<Instruction>(Funct3::Add) << 12) | (((ir >> 7) & 0x1F) << 7) |
                  static_cast<Instruction>(Opcode::OpImm);
            break;
        }
        case 0x1: {  // C.JAL : jal x1, offset[11:1]
            ret = (((imm1 >> 20) & 0x1) << 31) | (((imm1 >> 1) & 0x3FF) << 21) |
                  (((imm1 >> 11) & 0x1) << 20) | (((imm1 >> 12) & 0xFF) << 12) | (1 << 7) |
                  static_cast<Instruction>(Opcode::Jal);
            break;
        }
        case 0x2: {  // C.LI : addi rd, x0, imm[5:0]
            ret = (imm3 << 20) | (static_cast<Instruction>(Funct3::Add) << 12) | (ir & 0xF80) |
                  static_cast<Instruction>(Opcode::OpImm);
            break;
        }
        case 0x3: {
            if (((ir >> 7) & 0x1F) == 2) {  // C.ADDI16SP : addi x2, x2, nzimm[9:4]
                ret = (imm4 << 20) | (2 << 15) | (static_cast<Instruction>(Funct3::Add) << 12) |
                      (2 << 7) | static_cast<Instruction>(Opcode::OpImm);
            } else {  // C.LUI : lui rd, nzimm[17:12]
                ret = (nzimm << 12) | (ir & 0xF80) | static_cast<Instruction>(Opcode::Lui);
            }
            break;
        }
        case 0x5: {  // C.J : jal x0, offset[11:1]
            ret = (((imm1 >> 20) & 0x1) << 31) | (((imm1 >> 1) & 0x3FF) << 21) |
                  (((imm1 >> 11) & 0x1) << 20) | (((imm1 >> 12) & 0xFF) << 12) |
                  static_cast<Instruction>(Opcode::Jal);
            break;
        }
        case 0x6: {  // C.BEQZ : beq sr1', x0, offset[8:1]
            ret = (((imm2 >> 12) & 0x1) << 31) | (((imm2 >> 5) & 0x3F) << 25) | (rs1 << 15) |
                  (static_cast<Instruction>(Funct3::Beq) << 12) | (((imm2 >> 1) & 0xF) << 8) |
                  (((imm2 >> 11) & 0x1) << 7) | static_cast<Instruction>(Opcode::Branch);
            break;
        }
        case 0x7: {  // C.BNEZ : bne rs1', x0, offset[8:1]
            ret = (((imm2 >> 12) & 0x1) << 31) | (((imm2 >> 5) & 0x3F) << 25) | (rs1 << 15) |
                  (static_cast<Instruction>(Funct3::Bne) << 12) | (((imm2 >> 1) & 0xF) << 8) |
                  (((imm2 >> 11) & 0x1) << 7) | static_cast<Instruction>(Opcode::Branch);
            break;
        }
        case 0x4: {
            switch (funct1) {
                case 0x0: {  // C.SRLI : srli rd', rd', shamt[5:0]
                    ret = (shamt << 20) | (rd << 15) |
                          (static_cast<Instruction>(Funct3::Srl) << 12) | (rd << 7) |
                          static_cast<Instruction>(Opcode::OpImm);
                    break;
                }
                case 0x1: {  // C.SRAI : srai rd', rd', shamt[5:0]
                    ret = (1 << 30) | (shamt << 20) | (rd << 15) |
                          (static_cast<Instruction>(Funct3::Srl) << 12) | (rd << 7) |
                          static_cast<Instruction>(Opcode::OpImm);
                    break;
                }
                case 0x2: {  // C.ANDI : andi rd', rd', imm[5:0]
                    ret = (nzimm << 20) | (rd << 15) |
                          (static_cast<Instruction>(Funct3::And) << 12) | (rd << 7) |
                          static_cast<Instruction>(Opcode::OpImm);
                    break;
                }
                case 0x3: {
                    switch (funct2) {
                        case 0x0: {  // C.SUB : sub rd', rd', rs2'
                            ret = (1 << 30) | (rs2 << 20) | (rd << 15) |
                                  (static_cast<Instruction>(Funct3::Add) << 12) | (rd << 7) |
                                  static_cast<Instruction>(Opcode::Op);
                            break;
                        }
                        case 0x1: {  // C.XOR : xor rd', rd', rs2'
                            ret = (rs2 << 20) | (rd << 15) |
                                  (static_cast<Instruction>(Funct3::Xor) << 12) | (rd << 7) |
                                  static_cast<Instruction>(Opcode::Op);
                            break;
                        }
                        case 0x2: {  // C.OR : or rd', rd', rs2'
                            ret = (rs2 << 20) | (rd << 15) |
                                  (static_cast<Instruction>(Funct3::Or) << 12) | (rd << 7) |
                                  static_cast<Instruction>(Opcode::Op);
                            break;
                        }
                        case 0x3: {  // C.AND : and rd', rd', rs2'
                            ret = (rs2 << 20) | (rd << 15) |
                                  (static_cast<Instruction>(Funct3::And) << 12) | (rd << 7) |
                                  static_cast<Instruction>(Opcode::Op);
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
            ret = (shamt << 20) | (rd << 15) | (static_cast<Instruction>(Funct3::Sll) << 12) |
                  (rd << 7) | static_cast<Instruction>(Opcode::OpImm);
            break;
        }
        case 0x1: {  // C.FLDSP : fld rd, offset[8:3](x2)
            ret = (uimm1 << 20) | (2 << 15) | (static_cast<Instruction>(Funct3::Fld) << 12) |
                  (rd << 7) | static_cast<Instruction>(Opcode::LoadFp);
            break;
        }
        case 0x2: {  // C.LWSP : lw rd, offset[7:2](x2)
            ret = (uimm2 << 20) | (2 << 15) | (static_cast<Instruction>(Funct3::Lw) << 12) |
                  (rd << 7) | static_cast<Instruction>(Opcode::Load);
            break;
        }
        case 0x3: {  // C.FLWSP : flw rd, offset[7:2](x2)
            ret = (uimm2 << 20) | (2 << 15) | (static_cast<Instruction>(Funct3::Flw) << 12) |
                  (rd << 7) | static_cast<Instruction>(Opcode::LoadFp);
            break;
        }
        case 0x5: {  // C.FSDSP : fsd rs2, offset[8:3](x2)
            ret = (((uimm3 >> 5) & 0x7F) << 25) | (rs2 << 20) | (2 << 15) |
                  (static_cast<Instruction>(Funct3::Fsd) << 12) | ((uimm3 & 0x1F) << 7) |
                  static_cast<Instruction>(Opcode::StoreFp);
            break;
        }
        case 0x6: {  // C.SWSP : sw rs2, offset[7:2](x2)
            ret = (((uimm4 >> 5) & 0x7F) << 25) | (rs2 << 20) | (2 << 15) |
                  (static_cast<Instruction>(Funct3::Sw) << 12) | ((uimm4 & 0x1F) << 7) |
                  static_cast<Instruction>(Opcode::Store);
            break;
        }
        case 0x7: {  // C.FSWSP : fsw rs2, offset[7:2](x2)
            ret = (((uimm4 >> 5) & 0x7F) << 25) | (rs2 << 20) | (2 << 15) |
                  (static_cast<Instruction>(Funct3::Fsw) << 12) | ((uimm4 & 0x1F) << 7) |
                  static_cast<Instruction>(Opcode::StoreFp);
            break;
        }
        case 0x4: {
            switch ((ir >> 12) & 0x1) {
                case 0: {
                    // C.JR : jalr x0, rs1, 0
                    if (rd != 0 && rs2 == 0) {
                        ret = (rd << 15) | static_cast<Instruction>(Opcode::Jalr);
                    }
                    // C.MV : add rd, x0, rs2
                    if (rd != 0 && rs2 != 0) {
                        ret = (rs2 << 20) | (static_cast<Instruction>(Funct3::Add) << 12) |
                              (rd << 7) | static_cast<Instruction>(Opcode::Op);
                    }
                    break;
                }
                case 1:
                    // C.EBREAK : ebreak
                    if (rd == 0 && rs2 == 0) {
                        ret = (static_cast<Instruction>(Funct12Priv::Ebreak) << 20) |
                              static_cast<Instruction>(Opcode::System);
                    }
                    // C.JALR : jalr x1, rs, 0
                    if (rd != 0 && rs2 == 0) {
                        ret = (rd << 15) | (1 << 7) | static_cast<Instruction>(Opcode::Jalr);
                    }
                    // C.ADD : add rd, rd, rs2
                    if (rd != 0 && rs2 != 0) {
                        ret = (rs2 << 20) | (rd << 15) |
                              (static_cast<Instruction>(Funct3::Add) << 12) | (rd << 7) |
                              static_cast<Instruction>(Opcode::Op);
                    }
                    break;
            }
            break;
        }
    }
    return ret;
}

Instruction CB_inst_decomp(Instruction ir) {
    switch (compressed_opcode_of(static_cast<CompressedInstruction>(ir & 0xFFFF))) {
        case Opcode::C0:
            return decomp_c0(ir & 0xFFFF);
        case Opcode::C1:
            return decomp_c1(ir & 0xFFFF);
        case Opcode::C2:
            return decomp_c2(ir & 0xFFFF);
        default:
            break;
    }
    return ir;
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
        case Funct3::Csrrw: {
            ret = rrs1;
            break;
        }
        case Funct3::Csrrs: {
            ret = rcsr | rrs1;
            break;
        }
        case Funct3::Csrrc: {
            ret = rcsr & (~rrs1);
            break;
        }
        case Funct3::Csrrwi: {
            ret = imm;
            break;
        }
        case Funct3::Csrrsi: {
            ret = rcsr | imm;
            break;
        }
        case Funct3::Csrrci: {
            ret = rcsr & (~imm);
            break;
        }
        default:
            break;
    }
    return ret;
}

char OPERATION_NAME[OperationIdCount][11] = {
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

/* decoder                                                                                */
OperationId decoder(Instruction ir) {
    OperationId ret = UNKNOWN;
    Opcode opcode = opcode_of(ir);
    Funct3 funct3 = funct3_of(ir);
    Funct5Amo funct5 = funct5_of(ir);
    Instruction funct7 = (ir >> 25);
    Funct12Priv funct12 = static_cast<Funct12Priv>(funct12_of(ir));
    switch (opcode) {
        case Opcode::Lui:
            ret = LUI;
            break;
        case Opcode::Auipc:
            ret = AUIPC;
            break;
        case Opcode::Jal:
            ret = JAL;
            break;
        case Opcode::Jalr:
            ret = JALR;
            break;
        case Opcode::Branch: {
            switch (funct3) {
                case Funct3::Beq:
                    ret = BEQ;
                    break;
                case Funct3::Bne:
                    ret = BNE;
                    break;
                case Funct3::Blt:
                    ret = BLT;
                    break;
                case Funct3::Bge:
                    ret = BGE;
                    break;
                case Funct3::Bltu:
                    ret = BLTU;
                    break;
                case Funct3::Bgeu:
                    ret = BGEU;
                    break;
                default:
                    break;
            }
            break;
        }
        case Opcode::Load: {
            switch (funct3) {
                case Funct3::Lb:
                    ret = LB;
                    break;
                case Funct3::Lh:
                    ret = LH;
                    break;
                case Funct3::Lw:
                    ret = LW;
                    break;
                case Funct3::Lbu:
                    ret = LBU;
                    break;
                case Funct3::Lhu:
                    ret = LHU;
                    break;
                default:
                    break;
            }
            break;
        }
        case Opcode::Store: {
            switch (funct3) {
                case Funct3::Sb:
                    ret = SB;
                    break;
                case Funct3::Sh:
                    ret = SH;
                    break;
                case Funct3::Sw:
                    ret = SW;
                    break;
                default:
                    break;
            }
            break;
        }
        case Opcode::OpImm: {
            switch (funct3) {
                case Funct3::Add:
                    ret = ADDI;
                    break;
                case Funct3::Slt:
                    ret = SLTI;
                    break;
                case Funct3::Sltu:
                    ret = SLTIU;
                    break;
                case Funct3::Xor:
                    ret = XORI;
                    break;
                case Funct3::Or:
                    ret = ORI;
                    break;
                case Funct3::And:
                    ret = ANDI;
                    break;
                case Funct3::Sll:
                    ret = SLLI;
                    break;
                case Funct3::Srl: {
                    if (funct7) {
                        ret = SRAI;
                        break;
                    } else {
                        ret = SRLI;
                        break;
                    }
                    break;
                }
                default:
                    break;
            }
            break;
        }
        case Opcode::Op: {
            if (funct7 & 0x1) {
                switch (funct3) {
                    case Funct3::Mul:
                        ret = MUL;
                        break;
                    case Funct3::Mulh:
                        ret = MULH;
                        break;
                    case Funct3::Mulhsu:
                        ret = MULHSU;
                        break;
                    case Funct3::Mulhu:
                        ret = MULHU;
                        break;
                    case Funct3::Div:
                        ret = DIV;
                        break;
                    case Funct3::Divu:
                        ret = DIVU;
                        break;
                    case Funct3::Rem:
                        ret = REM;
                        break;
                    case Funct3::Remu:
                        ret = REMU;
                        break;
                    default:
                        break;
                }
            } else {
                switch (funct3) {
                    case Funct3::Add: {
                        if (funct7) {
                            ret = SUB;
                            break;
                        } else {
                            ret = ADD;
                            break;
                        }
                        break;
                    }
                    case Funct3::Slt:
                        ret = SLT;
                        break;
                    case Funct3::Sltu:
                        ret = SLTU;
                        break;
                    case Funct3::Xor:
                        ret = XOR;
                        break;
                    case Funct3::Or:
                        ret = OR;
                        break;
                    case Funct3::And:
                        ret = AND;
                        break;
                    case Funct3::Sll:
                        ret = SLL;
                        break;
                    case Funct3::Srl: {
                        if (funct7) {
                            ret = SRA;
                            break;
                        } else {
                            ret = SRL;
                            break;
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
            break;
        }
        case Opcode::MiscMem: {
            switch (funct3) {
                case Funct3::Fence:
                    ret = FENCE;
                    break;
                case Funct3::FenceI:
                    ret = FENCE_I;
                    break;
                default:
                    break;
            }
            break;
        }
        case Opcode::Amo: {
            switch (funct5) {
                case Funct5Amo::Lr:
                    ret = LR_W;
                    break;
                case Funct5Amo::Sc:
                    ret = SC_W;
                    break;
                case Funct5Amo::Swap:
                    ret = AMOSWAP_W;
                    break;
                case Funct5Amo::Add:
                    ret = AMOADD_W;
                    break;
                case Funct5Amo::Xor:
                    ret = AMOXOR_W;
                    break;
                case Funct5Amo::And:
                    ret = AMOAND_W;
                    break;
                case Funct5Amo::Or:
                    ret = AMOOR_W;
                    break;
                case Funct5Amo::Min:
                    ret = AMOMIN_W;
                    break;
                case Funct5Amo::Max:
                    ret = AMOMAX_W;
                    break;
                case Funct5Amo::Minu:
                    ret = AMOMINU_W;
                    break;
                case Funct5Amo::Maxu:
                    ret = AMOMAXU_W;
                    break;
                default:
                    break;
            }
            break;
        }
        case Opcode::System: {
            switch (funct3) {
                case Funct3::Csrrw:
                    ret = CSRRW;
                    break;
                case Funct3::Csrrs:
                    ret = CSRRS;
                    break;
                case Funct3::Csrrc:
                    ret = CSRRC;
                    break;
                case Funct3::Csrrwi:
                    ret = CSRRWI;
                    break;
                case Funct3::Csrrsi:
                    ret = CSRRSI;
                    break;
                case Funct3::Csrrci:
                    ret = CSRRCI;
                    break;
                case Funct3::Priv: {
                    switch (funct12) {
                        case Funct12Priv::Ecall:
                            ret = ECALL;
                            break;
                        case Funct12Priv::Ebreak:
                            ret = EBREAK;
                            break;
                        case Funct12Priv::Uret:
                            ret = URET;
                            break;
                        case Funct12Priv::Sret:
                            ret = SRET;
                            break;
                        case Funct12Priv::Mret:
                            ret = MRET;
                            break;
                        case Funct12Priv::Wfi:
                            ret = WFI;
                            break;
                        default: {
                            if (funct7 == static_cast<Instruction>(Funct7Priv::SfenceVma)) {
                                ret = SFENCE_VMA;
                            }
                            break;
                        }
                    }
                    break;
                }
                default:
                    break;
            }
            break;
        }
        case Opcode::LoadFp: {
            switch (funct3) {
                case Funct3::Flw:
                    ret = FLW;
                    break;
                case Funct3::Fld:
                    ret = FLD;
                    break;
                default:
                    break;
            }
            break;
        }
        case Opcode::StoreFp: {
            switch (funct3) {
                case Funct3::Fsw:
                    ret = FSW;
                    break;
                case Funct3::Fsd:
                    ret = FSD;
                    break;
                default:
                    break;
            }
            break;
        }
        case Opcode::MAdd: {
            switch ((ir >> 25) & 0x3) {
                case 0x0:
                    ret = FMADD_S;
                    break;
                case 0x1:
                    ret = FMADD_D;
                    break;
                default:
                    break;
            }
            break;
        }
        case Opcode::MSub: {
            switch ((ir >> 25) & 0x3) {
                case 0x0:
                    ret = FMSUB_S;
                    break;
                case 0x1:
                    ret = FMSUB_D;
                    break;
                default:
                    break;
            }
            break;
        }
        case Opcode::NMAdd: {
            switch ((ir >> 25) & 0x3) {
                case 0x0:
                    ret = FNMADD_S;
                    break;
                case 0x1:
                    ret = FNMADD_D;
                    break;
                default:
                    break;
            }
            break;
        }
        case Opcode::NMSub: {
            switch ((ir >> 25) & 0x3) {
                case 0x0:
                    ret = FNMSUB_S;
                    break;
                case 0x1:
                    ret = FNMSUB_D;
                    break;
                default:
                    break;
            }
            break;
        }
        case Opcode::OpFp: {
            switch (ir >> 25) {
                case 0x00:
                    ret = FADD_S;
                    break;
                case 0x01:
                    ret = FADD_D;
                    break;
                case 0x04:
                    ret = FSUB_S;
                    break;
                case 0x05:
                    ret = FSUB_D;
                    break;
                case 0x08:
                    ret = FMUL_S;
                    break;
                case 0x09:
                    ret = FMUL_D;
                    break;
                case 0x0C:
                    ret = FDIV_S;
                    break;
                case 0x0D:
                    ret = FDIV_D;
                    break;
                case 0x10: {
                    switch ((ir >> 12) & 0x7) {
                        case 0x0:
                            ret = FSGNJ_S;
                            break;
                        case 0x1:
                            ret = FSGNJN_S;
                            break;
                        case 0x2:
                            ret = FSGNJX_S;
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case 0x11: {
                    switch ((ir >> 12) & 0x7) {
                        case 0x0:
                            ret = FSGNJ_D;
                            break;
                        case 0x1:
                            ret = FSGNJN_D;
                            break;
                        case 0x2:
                            ret = FSGNJX_D;
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case 0x14: {
                    switch ((ir >> 12) & 0x7) {
                        case 0x0:
                            ret = FMIN_S;
                            break;
                        case 0x1:
                            ret = FMAX_S;
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case 0x15: {
                    switch ((ir >> 12) & 0x7) {
                        case 0x0:
                            ret = FMIN_D;
                            break;
                        case 0x1:
                            ret = FMAX_D;
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case 0x20:
                    ret = FCVT_S_D;
                    break;
                case 0x21:
                    ret = FCVT_D_S;
                    break;
                case 0x2C:
                    ret = FSQRT_S;
                    break;
                case 0x2D:
                    ret = FSQRT_D;
                    break;
                case 0x50: {
                    switch ((ir >> 12) & 0x7) {
                        case 0x0:
                            ret = FLE_S;
                            break;
                        case 0x1:
                            ret = FLT_S;
                            break;
                        case 0x2:
                            ret = FEQ_S;
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case 0x51: {
                    switch ((ir >> 12) & 0x7) {
                        case 0x0:
                            ret = FLE_D;
                            break;
                        case 0x1:
                            ret = FLT_D;
                            break;
                        case 0x2:
                            ret = FEQ_D;
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case 0x60: {
                    switch ((ir >> 20) & 0x1F) {
                        case 0x0:
                            ret = FCVT_W_S;
                            break;
                        case 0x1:
                            ret = FCVT_WU_S;
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case 0x61: {
                    switch ((ir >> 20) & 0x1F) {
                        case 0x0:
                            ret = FCVT_W_D;
                            break;
                        case 0x1:
                            ret = FCVT_WU_D;
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case 0x68: {
                    switch ((ir >> 20) & 0x1F) {
                        case 0x0:
                            ret = FCVT_S_W;
                            break;
                        case 0x1:
                            ret = FCVT_S_WU;
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case 0x69: {
                    switch ((ir >> 20) & 0x1F) {
                        case 0x0:
                            ret = FCVT_D_W;
                            break;
                        case 0x1:
                            ret = FCVT_D_WU;
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case 0x70: {
                    switch ((ir >> 12) & 0x7) {
                        case 0x0:
                            ret = FMV_X_W;
                            break;
                        case 0x1:
                            ret = FCLASS_S;
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case 0x71:
                    ret = FCLASS_D;
                    break;
                case 0x78:
                    ret = FMV_W_X;
                    break;
                default:
                    break;
            }
            break;
        }
        default: {
            fprintf(stdout, "__ Unknown Instruction %08x\n", ir);
            exit(0);
        }
    }
    return ret;
}

}  // namespace simrv::module
