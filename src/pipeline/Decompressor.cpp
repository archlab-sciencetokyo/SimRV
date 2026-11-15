#include "simrv/pipeline/Decompressor.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

#include "simrv/isa/Compressed.hpp"
#include "simrv/pipeline/Decoder.hpp"

namespace simrv::pipeline {

namespace {

auto make_r_type(uint32_t opcode, uint32_t funct3, uint32_t funct7, uint32_t rd, uint32_t rs1,
                 uint32_t rs2) -> simrv::xlen::Instruction {
    return opcode | (rd << 7) | (funct3 << 12) | (rs1 << 15) | (rs2 << 20) | (funct7 << 25);
}

auto make_i_type(uint32_t opcode, uint32_t funct3, uint32_t rd, uint32_t rs1, uint32_t imm)
    -> simrv::xlen::Instruction {
    return opcode | (rd << 7) | (funct3 << 12) | (rs1 << 15) | ((imm & 0xFFF) << 20);
}

auto make_s_type(uint32_t opcode, uint32_t funct3, uint32_t rs1, uint32_t rs2, uint32_t imm)
    -> simrv::xlen::Instruction {
    return opcode | ((imm & 0x1F) << 7) | (funct3 << 12) | (rs1 << 15) | (rs2 << 20) |
           (((imm >> 5) & 0x7F) << 25);
}

auto make_b_type(uint32_t opcode, uint32_t funct3, uint32_t rs1, uint32_t rs2, uint32_t imm)
    -> simrv::xlen::Instruction {
    return opcode | (((imm >> 11) & 0x1) << 7) | (((imm >> 1) & 0xF) << 8) | (funct3 << 12) |
           (rs1 << 15) | (rs2 << 20) | (((imm >> 5) & 0x3F) << 25) | (((imm >> 12) & 0x1) << 31);
}

auto make_u_type(uint32_t opcode, uint32_t rd, uint32_t imm) -> simrv::xlen::Instruction {
    return opcode | (rd << 7) | (imm & 0xFFFFF000);
}

auto make_j_type(uint32_t opcode, uint32_t rd, uint32_t imm) -> simrv::xlen::Instruction {
    return opcode | (rd << 7) | (((imm >> 12) & 0xFF) << 12) | (((imm >> 11) & 0x1) << 20) |
           (((imm >> 1) & 0x3FF) << 21) | (((imm >> 20) & 0x1) << 31);
}

auto decompress_q0(simrv::xlen::Instruction ir, bool is_rv64, uint32_t op, uint32_t rs1_p,
                   uint32_t rs2_p) -> simrv::xlen::Instruction {
    switch (static_cast<isa::Q0Op>(op)) {
        case isa::Q0Op::ADDI4SPN: {
            const uint32_t imm = ((ir >> 7) & 0xF) << 6 | ((ir >> 11) & 0x3) << 4 |
                                 ((ir >> 5) & 0x1) << 3 | ((ir >> 6) & 0x1) << 2;
            if (imm == 0) return 0;
            return make_i_type(0x13, 0x0, rs2_p, 2, imm);
        }
        case isa::Q0Op::FLD: {
            const uint32_t imm = ((ir >> 5) & 0x3) << 6 | ((ir >> 10) & 0x7) << 3;
            return make_i_type(0x07, 0x3, rs2_p, rs1_p, imm);
        }
        case isa::Q0Op::LW: {
            const uint32_t imm =
                ((ir >> 5) & 0x1) << 6 | ((ir >> 10) & 0x7) << 3 | ((ir >> 6) & 0x1) << 2;
            return make_i_type(0x03, 0x2, rs2_p, rs1_p, imm);
        }
        case isa::Q0Op::LD_FLW: {
            if (is_rv64) {
                const uint32_t imm = ((ir >> 5) & 0x3) << 6 | ((ir >> 10) & 0x7) << 3;
                return make_i_type(0x03, 0x3, rs2_p, rs1_p, imm);
            } else {
                const uint32_t imm =
                    ((ir >> 5) & 0x1) << 6 | ((ir >> 10) & 0x7) << 3 | ((ir >> 6) & 0x1) << 2;
                return make_i_type(0x07, 0x2, rs2_p, rs1_p, imm);
            }
        }
        case isa::Q0Op::FSD: {
            const uint32_t imm = ((ir >> 5) & 0x3) << 6 | ((ir >> 10) & 0x7) << 3;
            return make_s_type(0x27, 0x3, rs1_p, rs2_p, imm);
        }
        case isa::Q0Op::SW: {
            const uint32_t imm =
                ((ir >> 5) & 0x1) << 6 | ((ir >> 10) & 0x7) << 3 | ((ir >> 6) & 0x1) << 2;
            return make_s_type(0x23, 0x2, rs1_p, rs2_p, imm);
        }
        case isa::Q0Op::SD_FSW: {
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

auto decompress_q1_op4(simrv::xlen::Instruction ir, uint32_t rs1_p, uint32_t rs2_p)
    -> simrv::xlen::Instruction {
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

auto decompress_q1_op1(simrv::xlen::Instruction ir, bool is_rv64, uint32_t rs1_rd)
    -> simrv::xlen::Instruction {
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

auto decompress_q1_op3(simrv::xlen::Instruction ir, uint32_t rs1_rd) -> simrv::xlen::Instruction {
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

auto decompress_q1(simrv::xlen::Instruction ir, bool is_rv64, uint32_t op, uint32_t rs1_rd,
                   uint32_t rs1_p, uint32_t rs2_p) -> simrv::xlen::Instruction {
    switch (static_cast<isa::Q1Op>(op)) {
        case isa::Q1Op::ADDI: {
            const uint32_t imm = ((ir >> 2) & 0x1F) | (((ir >> 12) & 0x1) ? 0xFFFFFFE0 : 0);
            return make_i_type(0x13, 0x0, rs1_rd, rs1_rd, imm);
        }
        case isa::Q1Op::JAL_ADDIW:
            return decompress_q1_op1(ir, is_rv64, rs1_rd);
        case isa::Q1Op::LI: {
            const uint32_t imm = ((ir >> 2) & 0x1F) | (((ir >> 12) & 0x1) ? 0xFFFFFFE0 : 0);
            return make_i_type(0x13, 0x0, rs1_rd, 0, imm);
        }
        case isa::Q1Op::LUI_ADDI16SP:
            return decompress_q1_op3(ir, rs1_rd);
        case isa::Q1Op::ARITH:
            return decompress_q1_op4(ir, rs1_p, rs2_p);
        case isa::Q1Op::J: {
            const uint32_t imm = ((ir >> 2) & 0x1) << 5 | ((ir >> 3) & 0x7) << 1 |
                                 ((ir >> 6) & 0x1) << 7 | ((ir >> 7) & 0x1) << 6 |
                                 ((ir >> 8) & 0x1) << 10 | ((ir >> 9) & 0x3) << 8 |
                                 ((ir >> 11) & 0x1) << 4 | (((ir >> 12) & 0x1) ? 0xFFFFF800 : 0);
            return make_j_type(0x6F, 0, imm);
        }
        case isa::Q1Op::BEQZ:
        case isa::Q1Op::BNEZ: {
            const uint32_t imm = ((ir >> 2) & 0x1) << 5 | ((ir >> 3) & 0x3) << 1 |
                                 ((ir >> 5) & 0x3) << 6 | ((ir >> 10) & 0x3) << 3 |
                                 (((ir >> 12) & 0x1) ? 0xFFFFFF00 : 0);
            return make_b_type(0x63, (static_cast<isa::Q1Op>(op) == isa::Q1Op::BEQZ) ? 0x0 : 0x1,
                               rs1_p, 0, imm);
        }
        default:
            break;
    }
    return 0;
}

auto decompress_q2_op4(simrv::xlen::Instruction ir, uint32_t rs1_rd, uint32_t rs2)
    -> simrv::xlen::Instruction {
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

auto decompress_q2(simrv::xlen::Instruction ir, bool is_rv64, uint32_t op, uint32_t rs1_rd,
                   uint32_t rs2) -> simrv::xlen::Instruction {
    switch (static_cast<isa::Q2Op>(op)) {
        case isa::Q2Op::SLLI: {
            const uint32_t shamt = ((ir >> 2) & 0x1F) | (((ir >> 12) & 0x1) << 5);
            return make_r_type(0x13, 0x1, 0x00, rs1_rd, rs1_rd, shamt);
        }
        case isa::Q2Op::FLDSP: {
            const uint32_t imm =
                ((ir >> 2) & 0x7) << 6 | ((ir >> 5) & 0x3) << 3 | ((ir >> 12) & 0x1) << 5;
            return make_i_type(0x07, 0x3, rs1_rd, 2, imm);
        }
        case isa::Q2Op::LWSP: {
            const uint32_t imm =
                ((ir >> 2) & 0x3) << 6 | ((ir >> 4) & 0x7) << 2 | ((ir >> 12) & 0x1) << 5;
            return make_i_type(0x03, 0x2, rs1_rd, 2, imm);
        }
        case isa::Q2Op::LDSP_FLWSP: {
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
        case isa::Q2Op::ALU_JR:
            return decompress_q2_op4(ir, rs1_rd, rs2);
        case isa::Q2Op::FSDSP: {
            const uint32_t imm = ((ir >> 7) & 0x7) << 6 | ((ir >> 10) & 0x7) << 3;
            return make_s_type(0x27, 0x3, 2, rs2, imm);
        }
        case isa::Q2Op::SWSP: {
            const uint32_t imm = ((ir >> 7) & 0x3) << 6 | ((ir >> 9) & 0xF) << 2;
            return make_s_type(0x23, 0x2, 2, rs2, imm);
        }
        case isa::Q2Op::SDSP_FSWSP: {
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

auto decompressInstruction(simrv::xlen::Instruction ir, bool is_rv64) -> simrv::xlen::Instruction {
    simrv::pipeline::Decoder dec(ir);
    if (!dec.is_compressed()) {
        return ir;
    }

    // Zcmop's C.MOP.n encodings occupy reserved C.LUI space. They are architectural
    // no-ops unless a later extension redefines them, so expand them to C.NOP rather
    // than treating them as the ordinary C.LUI rd, 0 encoding (which writes rd).
    static constexpr std::array<simrv::xlen::Instruction, 8> kCmopEncodings{
        0x6081U, 0x6181U, 0x6281U, 0x6381U, 0x6481U, 0x6581U, 0x6681U, 0x6781U};
    if (std::ranges::find(kCmopEncodings, ir & 0xFFFFU) != kCmopEncodings.end()) {
        return 0x00000013U;  // addi x0, x0, 0
    }

    // Zcb's extra compressed integer operations expand to ordinary integer loads,
    // stores, and bit-manipulation instructions. Decode these before the base C
    // quadrant handling, where the same encodings are otherwise reserved.
    const uint32_t rd_prime = 8U + ((ir >> 2U) & 0x7U);
    const uint32_t rs1_prime = 8U + ((ir >> 7U) & 0x7U);
    const uint32_t rs2_prime = rd_prime;
    if ((ir & 0xfc03U) == 0x8000U) {  // C.LBU
        const uint32_t imm = (((ir >> 5U) & 1U) << 1U) | ((ir >> 6U) & 1U);
        return make_i_type(0x03U, 0x4U, rd_prime, rs1_prime, imm);
    }
    if ((ir & 0xfc43U) == 0x8400U || (ir & 0xfc43U) == 0x8440U) {  // C.LHU / C.LH
        const bool signed_load = (ir & 0x40U) != 0;
        const uint32_t imm = ((ir >> 5U) & 1U) << 1U;
        return make_i_type(0x03U, signed_load ? 0x1U : 0x5U, rd_prime, rs1_prime, imm);
    }
    if ((ir & 0xfc03U) == 0x8800U) {  // C.SB
        const uint32_t imm = (((ir >> 5U) & 1U) << 1U) | ((ir >> 6U) & 1U);
        return make_s_type(0x23U, 0x0U, rs1_prime, rs2_prime, imm);
    }
    if ((ir & 0xfc43U) == 0x8c00U) {  // C.SH
        const uint32_t imm = ((ir >> 5U) & 1U) << 1U;
        return make_s_type(0x23U, 0x1U, rs1_prime, rs2_prime, imm);
    }
    if ((ir & 0xfc63U) == 0x9c41U) {  // C.MUL
        return make_r_type(0x33U, 0x0U, 0x01U, rs1_prime, rs1_prime, rd_prime);
    }
    if ((ir & 0xfc7fU) == 0x9c75U) {  // C.NOT
        return make_i_type(0x13U, 0x4U, rs1_prime, rs1_prime, 0xfffU);
    }
    if ((ir & 0xfc7fU) == 0x9c65U || (ir & 0xfc7fU) == 0x9c6dU) {  // C.SEXT.B / C.SEXT.H
        const uint32_t imm = ((ir & 0x7fU) == 0x65U) ? 0x604U : 0x605U;
        return make_i_type(0x13U, 0x1U, rs1_prime, rs1_prime, imm);
    }
    if ((ir & 0xfc7fU) == 0x9c61U) {  // C.ZEXT.B
        return make_i_type(0x13U, 0x7U, rs1_prime, rs1_prime, 0xffU);
    }
    if ((ir & 0xfc7fU) == 0x9c69U) {  // C.ZEXT.H (PACK rd, rs1, x0)
        return make_r_type(is_rv64 ? 0x3bU : 0x33U, 0x4U, 0x04U, rs1_prime, rs1_prime, 0U);
    }
    if ((ir & 0xfc7fU) == 0x9c71U && is_rv64) {  // C.ZEXT.W (ADD.UW rd, rs1, x0)
        return make_r_type(0x3bU, 0x0U, 0x04U, rs1_prime, rs1_prime, 0U);
    }

    const uint32_t op = dec.c_op();
    switch (ir & 0x3) {
        case 0x0:
            return decompress_q0(ir, is_rv64, op, std::to_underlying(dec.c_rs1_rd_p()),
                                 std::to_underlying(dec.c_rs2_p()));
        case 0x1:
            return decompress_q1(ir, is_rv64, op, std::to_underlying(dec.c_rs1_rd()),
                                 std::to_underlying(dec.c_rs1_rd_p()),
                                 std::to_underlying(dec.c_rs2_p()));
        case 0x2:
            return decompress_q2(ir, is_rv64, op, std::to_underlying(dec.c_rs1_rd()),
                                 std::to_underlying(dec.c_rs2()));
        default:
            return 0;
    }
}

}  // namespace simrv::pipeline
