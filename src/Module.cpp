/**
 * @file Module.cpp
 * @brief SimRV implementation unit.
 */
#include "Module.hpp"

/* immediate generation                                                                   */
uint32_t CB_imm_gen(uint32_t ir) {
    uint32_t ret = 0;

    uint32_t uimm_I = ir >> 20;
    int32_t imm_I = (int32_t)(uimm_I | (uimm_I & 0x800 ? 0xFFFFF000 : 0x0));

    uint32_t uimm_S = ((ir >> 25) << 5) | ((ir >> 7) & 0x1f);
    int32_t imm_S = (int32_t)(uimm_S | (uimm_S & 0x800 ? 0xFFFFF000 : 0x0));

    uint32_t uimm_B = ((ir >> 31) << 12) | (((ir >> 7) & 1) << 11) | (((ir >> 25) & 0x3f) << 5) |
                      (((ir >> 8) & 0xF) << 1);
    int32_t imm_B = (int32_t)(uimm_B | (uimm_B & 0x1000 ? 0xFFFFE000 : 0x0));

    uint32_t uimm_U = ir >> 12;
    int32_t imm_U = (int32_t)(uimm_U | (uimm_U & 0x80000 ? 0xFFF00000 : 0x0));

    uint32_t uimm_J = ((ir >> 31) << 20) | (((ir >> 12) & 0xFF) << 12) |
                      (((ir >> 20) & 0x1) << 11) | (((ir >> 21) & 0x3FF) << 1);
    int32_t imm_J = (int32_t)(uimm_J | (uimm_J & 0x100000 ? 0xFFE00000 : 0x0));

    uint32_t zimm = (uint32_t)((ir >> 15) & 0x1f);

    switch (ir & 0x7F) {
        case OPCODE_OP_IMM: {
            ret = imm_I;
            break;
        }
        case OPCODE_LOAD: {
            ret = imm_I;
            break;
        }
        case OPCODE_JALR: {
            ret = imm_I;
            break;
        }
        case OPCODE_STORE: {
            ret = imm_S;
            break;
        }
        case OPCODE_BRANCH: {
            ret = imm_B;
            break;
        }
        case OPCODE_LUI: {
            ret = imm_U;
            break;
        }
        case OPCODE_AUIPC: {
            ret = imm_U;
            break;
        }
        case OPCODE_JAL: {
            ret = imm_J;
            break;
        }
        case OPCODE_SYSTEM: {
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
uint32_t decomp_c0(uint32_t ir) {
    uint32_t funct3 = (ir >> 13) & 0x7;
    uint32_t rs1 = ((ir >> 7) & 0x7) + 8;
    uint32_t rs2 = ((ir >> 2) & 0x7) + 8;
    uint32_t rd = ((ir >> 2) & 0x7) + 8;
    uint32_t uimm1 =
        (((ir >> 5) & 0x1) << 6) | (((ir >> 10) & 0x7) << 3) | (((ir >> 6) & 0x1) << 2);
    uint32_t uimm2 = (((ir >> 5) & 0x3) << 6) | (((ir >> 10) & 0x7) << 3);
    uint32_t nzuimm = (((ir >> 7) & 0xF) << 6) | (((ir >> 11) & 0x3) << 4) |
                      (((ir >> 5) & 0x1) << 3) | (((ir >> 6) & 0x1) << 2);

    uint32_t ret = ir;
    switch (funct3) {
        case 0x0: {  // C.ADDI4SPN : addi rd', x2, nzuimm[9:2]
            ret = (nzuimm << 20) | (2 << 15) | (FUNCT3_ADD << 12) | (rd << 7) | OPCODE_OP_IMM;
            break;
        }
        case 0x1: {  // C.FLD : fld rd', offset[7:3](rs1')
            ret = (uimm2 << 20) | (rs1 << 15) | (FUNCT3_LW << 12) | (rd << 7) | OPCODE_LOAD_FP;
            break;
        }
        case 0x2: {  // C.LW : lw rd', offset[6:2](rs1')
            ret = (uimm1 << 20) | (rs1 << 15) | (FUNCT3_LW << 12) | (rd << 7) | OPCODE_LOAD;
            break;
        }
        case 0x3: {  // C.FLW : flw rd', offset[6:2](rs1')
            ret = (uimm1 << 20) | (rs1 << 15) | (FUNCT3_LD << 12) | (rd << 7) | OPCODE_LOAD_FP;
            break;
        }
        case 0x5: {  // C.FSD : fsd rs2', offset[7:3](rs1')
            ret = (((uimm1 >> 5) & 0x7F) << 25) | (rs2 << 20) | (rs1 << 15) | (FUNCT3_FSD << 12) |
                  ((uimm1 & 0x1F) << 7) | OPCODE_STORE_FP;
            break;
        }
        case 0x6: {  // C.SW : sw rs2', offset[6:2](rs1')
            ret = (((uimm1 >> 5) & 0x7F) << 25) | (rs2 << 20) | (rs1 << 15) | (FUNCT3_SW << 12) |
                  ((uimm1 & 0x1F) << 7) | OPCODE_STORE;
            break;
        }
        case 0x7: {  // C.FSW : fsw rs2', offset[6:2](rs1')
            ret = (((uimm1 >> 5) & 0x7F) << 25) | (rs2 << 20) | (rs1 << 15) | (FUNCT3_FSW << 12) |
                  ((uimm1 & 0x1F) << 7) | OPCODE_STORE_FP;
            break;
        }
    }
    return ret;
}

uint32_t decomp_c1(uint32_t ir) {
    uint32_t funct1 = (ir >> 10) & 0x3;
    uint32_t funct2 = (((ir >> 12) & 0x1) << 2) | ((ir >> 5) & 0x3);
    uint32_t funct3 = (ir >> 13) & 0x7;
    uint32_t rs1 = ((ir >> 7) & 0x7) + 8;
    uint32_t rs2 = ((ir >> 2) & 0x7) + 8;
    uint32_t rd = ((ir >> 7) & 0x7) + 8;
    uint32_t nzimm =
        (((ir >> 12) & 1) ? 0xFFFFFFE0 : 0x0) | (((ir >> 12) & 1) << 5) | ((ir >> 2) & 0x1F);
    uint32_t shamt = nzimm & 0x1F;
    uint32_t uimm1 = (((ir >> 12) & 0x1) << 11) | (((ir >> 8) & 0x1) << 10) |
                     (((ir >> 9) & 0x3) << 8) | (((ir >> 6) & 0x1) << 7) |
                     (((ir >> 7) & 0x1) << 6) | (((ir >> 2) & 0x1) << 5) |
                     (((ir >> 11) & 0x1) << 4) | (((ir >> 3) & 0x7) << 1);
    uint32_t uimm2 = (((ir >> 12) & 0x1) << 8) | (((ir >> 5) & 0x3) << 6) |
                     (((ir >> 2) & 0x1) << 5) | (((ir >> 10) & 0x3) << 3) |
                     (((ir >> 3) & 0x3) << 1);
    uint32_t uimm3 = (((ir >> 12) & 1) << 5) | ((ir >> 2) & 0x1F);
    uint32_t uimm4 = (((ir >> 12) & 0x1) << 9) | (((ir >> 3) & 0x3) << 7) |
                     (((ir >> 5) & 0x1) << 6) | (((ir >> 2) & 0x1) << 5) | (((ir >> 6) & 0x1) << 4);
    uint32_t imm1 = ((uimm1 & 0x800) ? 0xFFFFF000 : 0x0) | uimm1;
    uint32_t imm2 = ((uimm2 & 0x100) ? 0xFFFFFE00 : 0x0) | uimm2;
    uint32_t imm3 = ((uimm3 & 0x20) ? 0xFFFFFFC0 : 0x0) | uimm3;
    uint32_t imm4 = ((uimm4 & 0x200) ? 0xFFFFFE00 : 0x0) | uimm4;

    uint32_t ret = ir;
    switch (funct3) {
        case 0x0: {  // C.ADDI : addi rd, rd, nzimm[5:0]
            ret = (nzimm << 20) | (((ir >> 7) & 0x1F) << 15) | (FUNCT3_ADD << 12) |
                  (((ir >> 7) & 0x1F) << 7) | OPCODE_OP_IMM;
            break;
        }
        case 0x1: {  // C.JAL : jal x1, offset[11:1]
            ret = (((imm1 >> 20) & 0x1) << 31) | (((imm1 >> 1) & 0x3FF) << 21) |
                  (((imm1 >> 11) & 0x1) << 20) | (((imm1 >> 12) & 0xFF) << 12) | (1 << 7) |
                  OPCODE_JAL;
            break;
        }
        case 0x2: {  // C.LI : addi rd, x0, imm[5:0]
            ret = (imm3 << 20) | (FUNCT3_ADD << 12) | (ir & 0xF80) | OPCODE_OP_IMM;
            break;
        }
        case 0x3: {
            if (((ir >> 7) & 0x1F) == 2) {  // C.ADDI16SP : addi x2, x2, nzimm[9:4]
                ret = (imm4 << 20) | (2 << 15) | (FUNCT3_ADD << 12) | (2 << 7) | OPCODE_OP_IMM;
            } else {  // C.LUI : lui rd, nzimm[17:12]
                ret = (nzimm << 12) | (ir & 0xF80) | OPCODE_LUI;
            }
            break;
        }
        case 0x5: {  // C.J : jal x0, offset[11:1]
            ret = (((imm1 >> 20) & 0x1) << 31) | (((imm1 >> 1) & 0x3FF) << 21) |
                  (((imm1 >> 11) & 0x1) << 20) | (((imm1 >> 12) & 0xFF) << 12) | OPCODE_JAL;
            break;
        }
        case 0x6: {  // C.BEQZ : beq sr1', x0, offset[8:1]
            ret = (((imm2 >> 12) & 0x1) << 31) | (((imm2 >> 5) & 0x3F) << 25) | (rs1 << 15) |
                  (FUNCT3_BEQ << 12) | (((imm2 >> 1) & 0xF) << 8) | (((imm2 >> 11) & 0x1) << 7) |
                  OPCODE_BRANCH;
            break;
        }
        case 0x7: {  // C.BNEZ : bne rs1', x0, offset[8:1]
            ret = (((imm2 >> 12) & 0x1) << 31) | (((imm2 >> 5) & 0x3F) << 25) | (rs1 << 15) |
                  (FUNCT3_BNE << 12) | (((imm2 >> 1) & 0xF) << 8) | (((imm2 >> 11) & 0x1) << 7) |
                  OPCODE_BRANCH;
            break;
        }
        case 0x4: {
            switch (funct1) {
                case 0x0: {  // C.SRLI : srli rd', rd', shamt[5:0]
                    ret =
                        (shamt << 20) | (rd << 15) | (FUNCT3_SRL << 12) | (rd << 7) | OPCODE_OP_IMM;
                    break;
                }
                case 0x1: {  // C.SRAI : srai rd', rd', shamt[5:0]
                    ret = (1 << 30) | (shamt << 20) | (rd << 15) | (FUNCT3_SRL << 12) | (rd << 7) |
                          OPCODE_OP_IMM;
                    break;
                }
                case 0x2: {  // C.ANDI : andi rd', rd', imm[5:0]
                    ret =
                        (nzimm << 20) | (rd << 15) | (FUNCT3_AND << 12) | (rd << 7) | OPCODE_OP_IMM;
                    break;
                }
                case 0x3: {
                    switch (funct2) {
                        case 0x0: {  // C.SUB : sub rd', rd', rs2'
                            ret = (1 << 30) | (rs2 << 20) | (rd << 15) | (FUNCT3_ADD << 12) |
                                  (rd << 7) | OPCODE_OP;
                            break;
                        }
                        case 0x1: {  // C.XOR : xor rd', rd', rs2'
                            ret = (rs2 << 20) | (rd << 15) | (FUNCT3_XOR << 12) | (rd << 7) |
                                  OPCODE_OP;
                            break;
                        }
                        case 0x2: {  // C.OR : or rd', rd', rs2'
                            ret = (rs2 << 20) | (rd << 15) | (FUNCT3_OR << 12) | (rd << 7) |
                                  OPCODE_OP;
                            break;
                        }
                        case 0x3: {  // C.AND : and rd', rd', rs2'
                            ret = (rs2 << 20) | (rd << 15) | (FUNCT3_AND << 12) | (rd << 7) |
                                  OPCODE_OP;
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

uint32_t decomp_c2(uint32_t ir) {
    uint32_t funct3 = (ir >> 13) & 0x7;
    uint32_t rd = (ir >> 7) & 0x1F;
    uint32_t rs2 = (ir >> 2) & 0x1F;
    uint32_t uimm1 =
        (((ir >> 2) & 0x7) << 6) | (((ir >> 12) & 0x1) << 5) | (((ir >> 5) & 0x3) << 3);
    uint32_t uimm2 =
        (((ir >> 2) & 0x3) << 6) | (((ir >> 12) & 0x1) << 5) | (((ir >> 4) & 0x7) << 2);
    uint32_t uimm3 = (((ir >> 7) & 0x7) << 6) | (((ir >> 10) & 0x7) << 3);
    uint32_t uimm4 = (((ir >> 7) & 0x3) << 6) | (((ir >> 9) & 0xF) << 2);
    uint32_t nzuimm = (((ir >> 12) & 1) << 5) | ((ir >> 2) & 0x1F);
    uint32_t shamt = nzuimm & 0x1F;

    uint32_t ret = ir;
    switch (funct3) {
        case 0x0: {  // C.SLLI : slli rd, rd, shamt[5:0]
            ret = (shamt << 20) | (rd << 15) | (FUNCT3_SLL << 12) | (rd << 7) | OPCODE_OP_IMM;
            break;
        }
        case 0x1: {  // C.FLDSP : fld rd, offset[8:3](x2)
            ret = (uimm1 << 20) | (2 << 15) | (FUNCT3_FLD << 12) | (rd << 7) | OPCODE_LOAD_FP;
            break;
        }
        case 0x2: {  // C.LWSP : lw rd, offset[7:2](x2)
            ret = (uimm2 << 20) | (2 << 15) | (FUNCT3_LW << 12) | (rd << 7) | OPCODE_LOAD;
            break;
        }
        case 0x3: {  // C.FLWSP : flw rd, offset[7:2](x2)
            ret = (uimm2 << 20) | (2 << 15) | (FUNCT3_FLW << 12) | (rd << 7) | OPCODE_LOAD_FP;
            break;
        }
        case 0x5: {  // C.FSDSP : fsd rs2, offset[8:3](x2)
            ret = (((uimm3 >> 5) & 0x7F) << 25) | (rs2 << 20) | (2 << 15) | (FUNCT3_FSD << 12) |
                  ((uimm3 & 0x1F) << 7) | OPCODE_STORE_FP;
            break;
        }
        case 0x6: {  // C.SWSP : sw rs2, offset[7:2](x2)
            ret = (((uimm4 >> 5) & 0x7F) << 25) | (rs2 << 20) | (2 << 15) | (FUNCT3_SW << 12) |
                  ((uimm4 & 0x1F) << 7) | OPCODE_STORE;
            break;
        }
        case 0x7: {  // C.FSWSP : fsw rs2, offset[7:2](x2)
            ret = (((uimm4 >> 5) & 0x7F) << 25) | (rs2 << 20) | (2 << 15) | (FUNCT3_FSW << 12) |
                  ((uimm4 & 0x1F) << 7) | OPCODE_STORE_FP;
            break;
        }
        case 0x4: {
            switch ((ir >> 12) & 0x1) {
                case 0: {
                    // C.JR : jalr x0, rs1, 0
                    if (rd != 0 && rs2 == 0) {
                        ret = (rd << 15) | OPCODE_JALR;
                    }
                    // C.MV : add rd, x0, rs2
                    if (rd != 0 && rs2 != 0) {
                        ret = (rs2 << 20) | (FUNCT3_ADD << 12) | (rd << 7) | OPCODE_OP;
                    }
                    break;
                    case 1:
                        // C.EBREAK : ebreak
                        if (rd == 0 && rs2 == 0) {
                            ret = (FUNCT12_EBREAK << 20) | OPCODE_SYSTEM;
                        }
                        // C.JALR : jalr x1, rs, 0
                        if (rd != 0 && rs2 == 0) {
                            ret = (rd << 15) | (1 << 7) | OPCODE_JALR;
                        }
                        // C.ADD : add rd, rd, rs2
                        if (rd != 0 && rs2 != 0) {
                            ret = (rs2 << 20) | (rd << 15) | (FUNCT3_ADD << 12) | (rd << 7) |
                                  OPCODE_OP;
                        }
                        break;
                }
            }
            break;
        }
    }
    return ret;
}

uint32_t CB_inst_decomp(uint32_t ir) {
    switch (ir & 0x3) {
        case OPCODE_C0:
            return decomp_c0(ir & 0xFFFF);
        case OPCODE_C1:
            return decomp_c1(ir & 0xFFFF);
        case OPCODE_C2:
            return decomp_c2(ir & 0xFFFF);
    }
    return ir;
}

uint32_t ALU_IM(uint32_t in1, uint32_t in2, uint32_t funct3, uint32_t funct7) {
    uint32_t ret = 0;
    uint32_t shamt = in2 & 0x1f;

    if ((funct7 & 0x1) == 0) {
        switch (funct3) {
            case FUNCT3_ADD: {
                if (funct7)
                    ret = in1 - in2;
                else
                    ret = in1 + in2;
                break;
            }
            case FUNCT3_SLL: {
                ret = in1 << shamt;
                break;
            }
            case FUNCT3_SLT: {
                ret = (int32_t)in1 < (int32_t)in2;
                break;
            }
            case FUNCT3_SLTU: {
                ret = in1 < in2;
                break;
            }
            case FUNCT3_XOR: {
                ret = in1 ^ in2;
                break;
            }
            case FUNCT3_SRL: {
                if (funct7)
                    ret = (int32_t)in1 >> shamt;
                else
                    ret = in1 >> shamt;
                break;
            }
            case FUNCT3_OR: {
                ret = in1 | in2;
                break;
            }
            case FUNCT3_AND: {
                ret = in1 & in2;
                break;
            }
            default: {
                break;
            }
        }
    } else {
        uint64_t mul_SS = (int64_t)((int32_t)in1) * (int64_t)((int32_t)in2);
        uint64_t mul_SU = (int64_t)((int32_t)in1) * (uint64_t)in2;
        uint64_t mul_UU = (uint64_t)in1 * (uint64_t)in2;
        switch (funct3) {
            case FUNCT3_MUL: {
                ret = mul_SS & 0xFFFFFFFF;
                break;
            }
            case FUNCT3_MULH: {
                ret = (mul_SS >> 32) & 0xFFFFFFFF;
                break;
            }
            case FUNCT3_MULHSU: {
                ret = (mul_SU >> 32) & 0xFFFFFFFF;
                break;
            }
            case FUNCT3_MULHU: {
                ret = (mul_UU >> 32) & 0xFFFFFFFF;
                break;
            }
            case FUNCT3_DIV: {
                if (in2 == 0xFFFFFFFF)
                    ret = in1;
                else if (in2 == 0)
                    ret = 0xFFFFFFFF;
                else
                    ret = (int32_t)in1 / (int32_t)in2;
                break;
            }
            case FUNCT3_DIVU: {
                if (in2 == 0)
                    ret = 0xFFFFFFFF;
                else
                    ret = in1 / in2;
                break;
            }
            case FUNCT3_REM: {
                if (in2 == 0xFFFFFFFF)
                    ret = 0;
                else if (in2 == 0)
                    ret = in1;
                else
                    ret = (int32_t)in1 % (int32_t)in2;
                break;
            }
            case FUNCT3_REMU: {
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

uint32_t ALU_B(uint32_t in1, uint32_t in2, uint32_t funct3) {
    uint32_t ret = 0;
    switch (funct3) {
        case FUNCT3_BEQ: {
            ret = in1 == in2;
            break;
        }
        case FUNCT3_BNE: {
            ret = in1 != in2;
            break;
        }
        case FUNCT3_BLT: {
            ret = (int32_t)in1 < (int32_t)in2;
            break;
        }
        case FUNCT3_BGE: {
            ret = (int32_t)in1 >= (int32_t)in2;
            break;
        }
        case FUNCT3_BLTU: {
            ret = in1 < in2;
            break;
        }
        case FUNCT3_BGEU: {
            ret = in1 >= in2;
            break;
        }
    }
    return ret;
}

uint32_t ALU_A(uint32_t in1, uint32_t in2, uint32_t funct5) {
    uint32_t ret = 0;
    switch (funct5) {
        case FUNCT5_AMO_LR: {
            ret = 0;
            break;
        }
        case FUNCT5_AMO_SC: {
            ret = in1;
            break;
        }
        case FUNCT5_AMO_SWAP: {
            ret = in1;
            break;
        }
        case FUNCT5_AMO_ADD: {
            ret = in1 + in2;
            break;
        }
        case FUNCT5_AMO_AND: {
            ret = in1 & in2;
            break;
        }
        case FUNCT5_AMO_OR: {
            ret = in1 | in2;
            break;
        }
        case FUNCT5_AMO_XOR: {
            ret = in1 ^ in2;
            break;
        }
        case FUNCT5_AMO_MIN: {
            ret = (int32_t)in1 < (int32_t)in2 ? in1 : in2;
            break;
        }
        case FUNCT5_AMO_MINU: {
            ret = in1 < in2 ? in1 : in2;
            break;
        }
        case FUNCT5_AMO_MAX: {
            ret = (int32_t)in1 > (int32_t)in2 ? in1 : in2;
            break;
        }
        case FUNCT5_AMO_MAXU: {
            ret = in1 > in2 ? in1 : in2;
            break;
        }
    }
    return ret;
}

uint32_t ALU_C(uint32_t rcsr, uint32_t rrs1, uint32_t imm, uint32_t funct3) {
    uint32_t ret = 0;
    switch (funct3) {
        case FUNCT3_CSRRW: {
            ret = rrs1;
            break;
        }
        case FUNCT3_CSRRS: {
            ret = rcsr | rrs1;
            break;
        }
        case FUNCT3_CSRRC: {
            ret = rcsr & (~rrs1);
            break;
        }
        case FUNCT3_CSRRWI: {
            ret = imm;
            break;
        }
        case FUNCT3_CSRRSI: {
            ret = rcsr | imm;
            break;
        }
        case FUNCT3_CSRRCI: {
            ret = rcsr & (~imm);
            break;
        }
    }
    return ret;
}

char OPERATION_NAME[NUMOFID___][11] = {
    /* RV32I */
    "LUI_______", "AUIPC_____", "JAL_______", "JALR______", "BEQ_______", "BNE_______",
    "BLT_______", "BGE_______", "BLTU______", "BGEU______", "LB________", "LH________",
    "LW________", "LBU_______", "LHU_______", "SB________", "SH________", "SW________",
    "ADDI______", "SLTI______", "SLTIU_____", "XORI______", "ORI_______", "ANDI______",
    "SLLI______", "SRLI______", "SRAI______", "ADD_______", "SUB_______", "SLL_______",
    "SLT_______", "SLTU______", "XOR_______", "SRL_______", "SRA_______", "OR________",
    "AND_______", "FENCE_____", "FENCE_I___", "ECALL_____", "EBREAK____", "CSRRW_____",
    "CSRRS_____", "CSRRC_____", "CSRRWI____", "CSRRSI____", "CSRRCI____",
    /* Privileged */
    "URET______", "SRET______", "MRET______", "WFI_______", "SFENCE_VMA",
    /* RV32M */
    "MUL_______", "MULH______", "MULHSU____", "MULHU_____", "DIV_______", "DIVU______",
    "REM_______", "REMU______",
    /* RV32A */
    "LR_W______", "SC_W______", "AMOSWAP_W_", "AMOADD_W__", "AMOXOR_W__", "AMOAND_W__",
    "AMOOR_W___", "AMOMIN_W__", "AMOMAX_W__", "AMOMINU_W_", "AMOMAXU_W_",
    /* Others */
    "UNKNOWN___"};

/* decoder                                                                                */
OPERATION_ID decoder(uint32_t ir) {
    OPERATION_ID ret = UNKNOWN___;
    uint32_t opcode = ir & 0x7F;
    uint32_t funct3 = (ir >> 12) & 0x7;
    uint32_t funct5 = (ir >> 27) & 0x1F;
    uint32_t funct7 = (ir >> 25);
    uint32_t funct12 = (ir >> 20);
    switch (opcode) {
        case OPCODE_LUI:
            ret = LUI_______;
            break;
        case OPCODE_AUIPC:
            ret = AUIPC_____;
            break;
        case OPCODE_JAL:
            ret = JAL_______;
            break;
        case OPCODE_JALR:
            ret = JALR______;
            break;
        case OPCODE_BRANCH: {
            switch (funct3) {
                case FUNCT3_BEQ:
                    ret = BEQ_______;
                    break;
                case FUNCT3_BNE:
                    ret = BNE_______;
                    break;
                case FUNCT3_BLT:
                    ret = BLT_______;
                    break;
                case FUNCT3_BGE:
                    ret = BGE_______;
                    break;
                case FUNCT3_BLTU:
                    ret = BLTU______;
                    break;
                case FUNCT3_BGEU:
                    ret = BGEU______;
                    break;
            }
            break;
        }
        case OPCODE_LOAD: {
            switch (funct3) {
                case FUNCT3_LB:
                    ret = LB________;
                    break;
                case FUNCT3_LH:
                    ret = LH________;
                    break;
                case FUNCT3_LW:
                    ret = LW________;
                    break;
                case FUNCT3_LBU:
                    ret = LBU_______;
                    break;
                case FUNCT3_LHU:
                    ret = LHU_______;
                    break;
            }
            break;
        }
        case OPCODE_STORE: {
            switch (funct3) {
                case FUNCT3_SB:
                    ret = SB________;
                    break;
                case FUNCT3_SH:
                    ret = SH________;
                    break;
                case FUNCT3_SW:
                    ret = SW________;
                    break;
            }
            break;
        }
        case OPCODE_OP_IMM: {
            switch (funct3) {
                case FUNCT3_ADD:
                    ret = ADDI______;
                    break;
                case FUNCT3_SLT:
                    ret = SLTI______;
                    break;
                case FUNCT3_SLTU:
                    ret = SLTIU_____;
                    break;
                case FUNCT3_XOR:
                    ret = XORI______;
                    break;
                case FUNCT3_OR:
                    ret = ORI_______;
                    break;
                case FUNCT3_AND:
                    ret = ANDI______;
                    break;
                case FUNCT3_SLL:
                    ret = SLLI______;
                    break;
                case FUNCT3_SRL: {
                    if (funct7) {
                        ret = SRAI______;
                        break;
                    } else {
                        ret = SRLI______;
                        break;
                    }
                    break;
                }
            }
            break;
        }
        case OPCODE_OP: {
            if (funct7 & 0x1) {
                switch (funct3) {
                    case FUNCT3_MUL:
                        ret = MUL_______;
                        break;
                    case FUNCT3_MULH:
                        ret = MULH______;
                        break;
                    case FUNCT3_MULHSU:
                        ret = MULHSU____;
                        break;
                    case FUNCT3_MULHU:
                        ret = MULHU_____;
                        break;
                    case FUNCT3_DIV:
                        ret = DIV_______;
                        break;
                    case FUNCT3_DIVU:
                        ret = DIVU______;
                        break;
                    case FUNCT3_REM:
                        ret = REM_______;
                        break;
                    case FUNCT3_REMU:
                        ret = REMU______;
                        break;
                }
            } else {
                switch (funct3) {
                    case FUNCT3_ADD: {
                        if (funct7) {
                            ret = SUB_______;
                            break;
                        } else {
                            ret = ADD_______;
                            break;
                        }
                        break;
                    }
                    case FUNCT3_SLT:
                        ret = SLT_______;
                        break;
                    case FUNCT3_SLTU:
                        ret = SLTU______;
                        break;
                    case FUNCT3_XOR:
                        ret = XOR_______;
                        break;
                    case FUNCT3_OR:
                        ret = OR________;
                        break;
                    case FUNCT3_AND:
                        ret = AND_______;
                        break;
                    case FUNCT3_SLL:
                        ret = SLL_______;
                        break;
                    case FUNCT3_SRL: {
                        if (funct7) {
                            ret = SRA_______;
                            break;
                        } else {
                            ret = SRL_______;
                            break;
                        }
                        break;
                    }
                }
            }
            break;
        }
        case OPCODE_MISC_M: {
            switch (funct3) {
                case FUNCT3_FENCE:
                    ret = FENCE_____;
                    break;
                case FUNCT3_FENCE_I:
                    ret = FENCE_I___;
                    break;
            }
            break;
        }
        case OPCODE_AMO: {
            switch (funct5) {
                case FUNCT5_AMO_LR:
                    ret = LR_W______;
                    break;
                case FUNCT5_AMO_SC:
                    ret = SC_W______;
                    break;
                case FUNCT5_AMO_SWAP:
                    ret = AMOSWAP_W_;
                    break;
                case FUNCT5_AMO_ADD:
                    ret = AMOADD_W__;
                    break;
                case FUNCT5_AMO_XOR:
                    ret = AMOXOR_W__;
                    break;
                case FUNCT5_AMO_AND:
                    ret = AMOAND_W__;
                    break;
                case FUNCT5_AMO_OR:
                    ret = AMOOR_W___;
                    break;
                case FUNCT5_AMO_MIN:
                    ret = AMOMIN_W__;
                    break;
                case FUNCT5_AMO_MAX:
                    ret = AMOMAX_W__;
                    break;
                case FUNCT5_AMO_MINU:
                    ret = AMOMINU_W_;
                    break;
                case FUNCT5_AMO_MAXU:
                    ret = AMOMAXU_W_;
                    break;
            }
            break;
        }
        case OPCODE_SYSTEM: {
            switch (funct3) {
                case FUNCT3_CSRRW:
                    ret = CSRRW_____;
                    break;
                case FUNCT3_CSRRS:
                    ret = CSRRS_____;
                    break;
                case FUNCT3_CSRRC:
                    ret = CSRRC_____;
                    break;
                case FUNCT3_CSRRWI:
                    ret = CSRRWI____;
                    break;
                case FUNCT3_CSRRSI:
                    ret = CSRRSI____;
                    break;
                case FUNCT3_CSRRCI:
                    ret = CSRRCI____;
                    break;
                case FUNCT3_PRIV: {
                    switch (funct12) {
                        case FUNCT12_ECALL:
                            ret = ECALL_____;
                            break;
                        case FUNCT12_EBREAK:
                            ret = EBREAK____;
                            break;
                        case FUNCT12_URET:
                            ret = URET______;
                            break;
                        case FUNCT12_SRET:
                            ret = SRET______;
                            break;
                        case FUNCT12_MRET:
                            ret = MRET______;
                            break;
                        case FUNCT12_WFI:
                            ret = WFI_______;
                            break;
                        default: {
                            if (funct7 == FUNCT7_SFENCE_VMA) {
                                ret = SFENCE_VMA;
                            }
                            break;
                        }
                    }
                    break;
                }
            }
            break;
        }
        // case OPCODE_LOAD_FP : {
        //     switch (funct3) {
        //         case FUNCT3_FLW : ret = FLW_______; break;
        //         case FUNCT3_FLD : ret = FLD_______; break;
        //     }
        //     break;
        // }
        // case OPCODE_STORE_FP : {
        //     switch (funct3) {
        //         case FUNCT3_FSW : ret = FSW_______; break;
        //         case FUNCT3_FSD : ret = FSD_______; break;
        //     }
        //     break;
        // }
        // case OPCODE_MADD : {
        //     switch ((ir >> 25) & 0x3) {
        //         case 0x0 : ret = FMADD_S___; break;
        //         case 0x1 : ret = FMADD_D___; break;
        //     }
        //     break;
        // }
        // case OPCODE_MSUB : {
        //     switch ((ir >> 25) & 0x3) {
        //         case 0x0 : ret = FMSUB_S___; break;
        //         case 0x1 : ret = FMSUB_D___; break;
        //     }
        //     break;
        // }
        // case OPCODE_NMADD : {
        //     switch ((ir >> 25) & 0x3) {
        //         case 0x0 : ret = FNMADD_S__; break;
        //         case 0x1 : ret = FNMADD_D__; break;
        //     }
        //     break;
        // }
        // case OPCODE_NMSUB : {
        //     switch ((ir >> 25) & 0x3) {
        //         case 0x0 : ret = FNMSUB_S__; break;
        //         case 0x1 : ret = FNMSUB_D__; break;
        //     }
        //     break;
        // }
        // case OPCODE_OP_FP : {
        //     switch (ir >> 25) {
        //         case 0x00 : ret = FADD_S____; break;
        //         case 0x01 : ret = FADD_D____; break;
        //         case 0x04 : ret = FSUB_S____; break;
        //         case 0x05 : ret = FSUB_D____; break;
        //         case 0x08 : ret = FMUL_S____; break;
        //         case 0x09 : ret = FMUL_D____; break;
        //         case 0x0C : ret = FDIV_S____; break;
        //         case 0x0D : ret = FDIV_D____; break;
        //         case 0x10 : {
        //             switch ((ir >> 12) & 0x7) {
        //                 case 0x0 : ret = FSGNJ_S___; break;
        //                 case 0x1 : ret = FSGNJN_S__; break;
        //                 case 0x2 : ret = FSGNJX_S__; break;
        //             }
        //             break;
        //         }
        //         case 0x11 : {
        //             switch ((ir >> 12) & 0x7) {
        //                 case 0x0 : ret = FSGNJ_D___; break;
        //                 case 0x1 : ret = FSGNJN_D__; break;
        //                 case 0x2 : ret = FSGNJX_D__; break;
        //             }
        //             break;
        //         }
        //         case 0x14 : {
        //             switch ((ir >> 12) & 0x7) {
        //                 case 0x0 : ret = FMIN_S____; break;
        //                 case 0x1 : ret = FMAX_S____; break;
        //             }
        //             break;
        //         }
        //         case 0x15 : {
        //             switch ((ir >> 12) & 0x7) {
        //                 case 0x0 : ret = FMIN_D____; break;
        //                 case 0x1 : ret = FMAX_D____; break;
        //             }
        //             break;
        //         }
        //         case 0x20 : ret = FCVT_S_D__; break;
        //         case 0x21 : ret = FCVT_D_S__; break;
        //         case 0x2C : ret = FSQRT_S___; break;
        //         case 0x2D : ret = FSQRT_D___; break;
        //         case 0x50 : {
        //             switch ((ir >> 12) & 0x7) {
        //                 case 0x0 : ret = FLE_S_____; break;
        //                 case 0x1 : ret = FLT_S_____; break;
        //                 case 0x2 : ret = FEQ_S_____; break;
        //             }
        //             break;
        //         }
        //         case 0x51 : {
        //             switch ((ir >> 12) & 0x7) {
        //                 case 0x0 : ret = FLE_D_____; break;
        //                 case 0x1 : ret = FLT_D_____; break;
        //                 case 0x2 : ret = FEQ_D_____; break;
        //             }
        //             break;
        //         }
        //         case 0x60 : {
        //             switch ((ir >> 20) & 0x1F) {
        //                 case 0x0 : ret = FCVT_W_S__; break;
        //                 case 0x1 : ret = FCVT_WU_S_; break;
        //             }
        //             break;
        //         }
        //         case 0x61 : {
        //             switch ((ir >> 20) & 0x1F) {
        //                 case 0x0 : ret = FCVT_W_D__; break;
        //                 case 0x1 : ret = FCVT_WU_D_; break;
        //             }
        //             break;
        //         }
        //         case 0x68 : {
        //             switch ((ir >> 20) & 0x1F) {
        //                 case 0x0 : ret = FCVT_S_W__; break;
        //                 case 0x1 : ret = FCVT_S_WU_; break;
        //             }
        //             break;
        //         }
        //         case 0x69 : {
        //             switch ((ir >> 20) & 0x1F) {
        //                 case 0x0 : ret = FCVT_D_W__; break;
        //                 case 0x1 : ret = FCVT_D_WU_; break;
        //             }
        //             break;
        //         }
        //         case 0x70 : {
        //             switch ((ir >> 12) & 0x7) {
        //                 case 0x0 : ret = FMV_X_W___; break;
        //                 case 0x1 : ret = FCLASS_S__; break;
        //             }
        //             break;
        //         }
        //         case 0x71 : ret = FCLASS_D__; break;
        //         case 0x78 : ret = FMV_W_X___; break;
        //     }
        //     break;
        // }
        default: {
            fprintf(stdout, "__ Unknown Instruction %08x\n", ir);
            exit(0);
        }
    }
    return ret;
}

