/**
 * @file Module.hpp
 * @brief ISA decode/decompress and ALU helper function declarations.
 */
#pragma once

#include <array>
#include <string_view>

#include "Define.hpp"

namespace simrv::module {

/**
 * @brief Decompress a 16-bit compressed instruction into canonical 32-bit form.
 * @param ir Raw fetched instruction word.
 * @return Canonical instruction.
 */
Instruction decompressInstruction(Instruction ir);
[[gnu::always_inline]] inline Instruction decodeImmediate(Instruction ir) {
    switch (opcode_of(ir)) {
        case Opcode::OpImm:
        case Opcode::Load:
        case Opcode::LoadFp:
        case Opcode::Jalr:
            return static_cast<Instruction>(static_cast<SignedWord>(ir) >> 20);
        case Opcode::Store: {
            const Instruction value = ((ir >> 7) & 0x1Fu) | ((ir >> 20) & 0xFE0u);
            return static_cast<Instruction>(static_cast<SignedWord>(value << 20) >> 20);
        }
        case Opcode::StoreFp: {
            const Instruction value = ((ir >> 7) & 0x1Fu) | ((ir >> 20) & 0xFE0u);
            return static_cast<Instruction>(static_cast<SignedWord>(value << 20) >> 20);
        }
        case Opcode::Branch: {
            const Instruction value = ((ir >> 31) << 12) | (((ir >> 7) & 1u) << 11) |
                                      (((ir >> 25) & 0x3Fu) << 5) | (((ir >> 8) & 0xFu) << 1);
            return static_cast<Instruction>(static_cast<SignedWord>(value << 19) >> 19);
        }
        case Opcode::Lui:
        case Opcode::Auipc:
            return static_cast<Instruction>(static_cast<SignedWord>(ir) >> 12);
        case Opcode::Jal: {
            const Instruction value = ((ir >> 31) << 20) | (((ir >> 12) & 0xFFu) << 12) |
                                      (((ir >> 20) & 0x1u) << 11) | (((ir >> 21) & 0x3FFu) << 1);
            return static_cast<Instruction>(static_cast<SignedWord>(value << 11) >> 11);
        }
        case Opcode::System:
            return static_cast<Instruction>((ir >> 15) & 0x1fu);
        default:
            return 0;
    }
}
OperationId decoder(Instruction ir);

/// Integer ALU operation helper (includes M-extension forms).
Register ALU_IM(Register in1, Register in2, Instruction funct3, Instruction funct7);
/// Branch condition evaluation helper.
Instruction ALU_B(Register in1, Register in2, Instruction funct3);
/// AMO operation helper.
Register ALU_A(Register in1, Register in2, Instruction funct5);
/// CSR value update helper.
CSRValue ALU_C(CSRValue rcsr, Register rrs1, Instruction imm, Instruction funct3);

inline constexpr std::array<std::string_view, kOperationIdCount> OPERATION_NAME = {
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

}  // namespace simrv::module
