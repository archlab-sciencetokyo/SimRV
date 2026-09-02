/**
 * @file Compressed.hpp
 * @brief RISC-V Compressed (C) extension definitions.
 */
#pragma once

#include <cstdint>

namespace simrv::isa {

/**
 * @enum CompressedOpcode
 * @brief Represents the 2-bit compressed opcode quadrant (bits [1:0] of 16-bit compressed
 * instructions).
 */
enum class CompressedOpcode : uint8_t {
    C0 = 0x0,  ///< Quadrant 00 (e.g. loads/stores with stack-pointer offsets, etc.)
    C1 = 0x1,  ///< Quadrant 01 (e.g. integer arithmetic/immediate instructions, jumps, etc.)
    C2 = 0x2,  ///< Quadrant 10 (e.g. stack-pointer arithmetic, register jumps, loads/stores)
    C3 = 0x3,  ///< Quadrant 11 (Reserved / Map to 32-bit instructions)
};

/**
 * @enum Q0Op
 * @brief Compressed quadrant-0 funct3 field (bits [15:13]) encoding.
 *
 * Encodes which instruction is present in the Q0 (00) quadrant.
 * See RISC-V Compressed ISA spec Table 16.5.
 */
enum class Q0Op : uint8_t {
    ADDI4SPN = 0,  ///< C.ADDI4SPN  — rd' = sp + nzuimm[9:2]
    FLD = 1,       ///< C.FLD       — RV32/64: frd' = mem[rs1'+uimm]
    LW = 2,        ///< C.LW        — rd' = mem[rs1'+uimm]
    LD_FLW = 3,    ///< C.LD (RV64) / C.FLW (RV32)
    Reserved = 4,  ///< Reserved (all-zero encoding is a hint NOP for C.ADDI4SPN w/ imm==0)
    FSD = 5,       ///< C.FSD       — RV32/64: mem[rs1'+uimm] = frs2'
    SW = 6,        ///< C.SW        — mem[rs1'+uimm] = rs2'
    SD_FSW = 7,    ///< C.SD (RV64) / C.FSW (RV32)
};

/**
 * @enum Q1Op
 * @brief Compressed quadrant-1 funct3 field (bits [15:13]) encoding.
 *
 * Encodes which instruction is present in the Q1 (01) quadrant.
 * See RISC-V Compressed ISA spec Table 16.6.
 */
enum class Q1Op : uint8_t {
    ADDI = 0,          ///< C.ADDI / C.NOP (rd=0)
    JAL_ADDIW = 1,     ///< C.JAL (RV32) / C.ADDIW (RV64)
    LI = 2,            ///< C.LI        — rd = imm
    LUI_ADDI16SP = 3,  ///< C.LUI (rd≠2) / C.ADDI16SP (rd=2)
    ARITH = 4,         ///< C.SRLI / C.SRAI / C.ANDI / C.SUB / C.XOR / C.OR / C.AND / C.SUBW /
                       ///<   C.ADDW (decoded via sub_op and bit12)
    J = 5,             ///< C.J         — pc += offset
    BEQZ = 6,          ///< C.BEQZ      — if rs1'==0: pc += offset
    BNEZ = 7,          ///< C.BNEZ      — if rs1'≠0: pc += offset
};

/**
 * @enum Q2Op
 * @brief Compressed quadrant-2 funct3 field (bits [15:13]) encoding.
 *
 * Encodes which instruction is present in the Q2 (10) quadrant.
 * See RISC-V Compressed ISA spec Table 16.7.
 */
enum class Q2Op : uint8_t {
    SLLI = 0,        ///< C.SLLI      — rd = rd << uimm[5:0]
    FLDSP = 1,       ///< C.FLDSP     — frd = mem[sp + uimm]
    LWSP = 2,        ///< C.LWSP      — rd = mem[sp + uimm]
    LDSP_FLWSP = 3,  ///< C.LDSP (RV64) / C.FLWSP (RV32)
    ALU_JR = 4,      ///< C.MV / C.ADD / C.JR / C.JALR / C.EBREAK (decoded via bit12 and rs2)
    FSDSP = 5,       ///< C.FSDSP     — mem[sp + uimm] = frs2
    SWSP = 6,        ///< C.SWSP      — mem[sp + uimm] = rs2
    SDSP_FSWSP = 7,  ///< C.SDSP (RV64) / C.FSWSP (RV32)
};

}  // namespace simrv::isa
