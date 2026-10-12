/**
 * @file InstructionFormat.hpp
 * @brief RISC-V instruction format identifiers.
 */
#pragma once

#include <cstdint>

namespace simrv::isa {

/**
 * @enum InstFormat
 * @brief Architectural RISC-V instruction format layout categories.
 */
enum class InstFormat : uint8_t {
    R,   ///< Register-Register Format (opcode, rd, funct3, rs1, rs2, funct7)
    I,   ///< Register-Immediate / Load / Jalr / CSR format (opcode, rd, funct3, rs1, imm12)
    S,   ///< Store Format (opcode, imm[11:5], funct3, rs1, rs2, imm[4:0])
    B,   ///< Conditional Branch Format (opcode, imm[12], imm[10:5], funct3, rs1, rs2, imm[4:1],
         ///< imm[11])
    U,   ///< Upper-Immediate format (opcode, rd, imm[31:12])
    J,   ///< Unconditional Jump format (opcode, rd, imm[20], imm[10:1], imm[11], imm[19:12])
    R4,  ///< 4-Operand Register Format (opcode, rd, funct3, rs1, rs2, funct2, rs3) - Used for FP
         ///< FMA
    Unknown  ///< Unidentified or invalid instruction format
};

}  // namespace simrv::isa
