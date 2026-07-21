/**
 * @file Base.hpp
 * @brief Base instruction set opcodes, profiles, and extensions.
 */
#pragma once

#include <cstdint>

namespace simrv::isa {

/**
 * @enum Opcode
 * @brief Represents standard RISC-V 7-bit base opcodes (bits [6:0] of the instruction).
 */
enum class Opcode : uint8_t {
    OpImm32 = 0x1B, ///< RV64I: Integer register-immediate instructions (32-bit word operations)
    Op = 0x33,      ///< RV32I/RV64I: Integer register-register instructions
    Op32 = 0x3B,    ///< RV64I: Integer register-register instructions (32-bit word operations)
    OpFp = 0x53,    ///< RV32F/RV32D: Floating-point register-register instructions
    Amo = 0x2F,     ///< RV32A/RV64A: Atomic memory operations
    OpImm = 0x13,   ///< RV32I/RV64I: Integer register-immediate instructions
    Load = 0x03,    ///< RV32I/RV64I: Memory load instructions
    LoadFp = 0x07,  ///< RV32F/RV32D: Floating-point memory load instructions
    Jalr = 0x67,    ///< RV32I/RV64I: Indirect jump (Jump and Link Register)
    Store = 0x23,   ///< RV32I/RV64I: Memory store instructions
    StoreFp = 0x27, ///< RV32F/RV32D: Floating-point memory store instructions
    Branch = 0x63,  ///< RV32I/RV64I: Conditional branch instructions
    MAdd = 0x43,    ///< RV32F/RV32D: Fused floating-point multiply-add
    MSub = 0x47,    ///< RV32F/RV32D: Fused floating-point multiply-subtract
    NMSub = 0x4B,   ///< RV32F/RV32D: Fused floating-point negated multiply-subtract
    NMAdd = 0x4F,   ///< RV32F/RV32D: Fused floating-point negated multiply-add
    Lui = 0x37,     ///< RV32I/RV64I: Load Upper Immediate
    Auipc = 0x17,   ///< RV32I/RV64I: Add Upper Immediate to PC
    Jal = 0x6F,     ///< RV32I/RV64I: Unconditional jump (Jump and Link)
    MiscMem = 0x0F, ///< RV32I/RV64I: Miscellaneous memory instructions (FENCE, FENCE.I)
    System = 0x73,  ///< RV32I/RV64I: System instructions (privilege calls, CSR access)
    OpV = 0x57,     ///< RISC-V Vector Extension: Vector instructions
    Custom0 = 0x0B, ///< RISC-V Custom/Non-standard Opcode Space 0
};

/**
 * @enum Funct3
 * @brief Represents the 3-bit sub-opcode field (bits [14:12] of the instruction).
 *
 * This field is used across various opcodes to specify the exact operation or variation
 * (e.g. arithmetic operations, branch comparison condition, load/store operand width).
 */
enum class Funct3 : uint8_t {
    /* Integer ALU (Op / OpImm / Op32 / OpImm32) */
    Add = 0x0,      ///< Add (or Sub when funct7 is set)
    Sll = 0x1,      ///< Shift Left Logical
    Slt = 0x2,      ///< Set Less Than
    Sltu = 0x3,     ///< Set Less Than Unsigned
    Xor = 0x4,      ///< Bitwise XOR
    Srl = 0x5,      ///< Shift Right Logical (or Sra when funct7 is set)
    Or = 0x6,       ///< Bitwise OR
    And = 0x7,      ///< Bitwise AND

    /* RV32M/RV64M Multiply-Divide extension */
    Mul = 0x0,      ///< Multiply Low
    Mulh = 0x1,     ///< Multiply High Signed-Signed
    Mulhsu = 0x2,   ///< Multiply High Signed-Unsigned
    Mulhu = 0x3,    ///< Multiply High Unsigned-Unsigned
    Div = 0x4,      ///< Division Signed
    Divu = 0x5,     ///< Division Unsigned
    Rem = 0x6,      ///< Remainder Signed
    Remu = 0x7,     ///< Remainder Unsigned

    /* Store Opcodes (Store / StoreFp) */
    Sb = 0x0,       ///< Store Byte
    Sh = 0x1,       ///< Store Halfword
    Sw = 0x2,       ///< Store Word
    Sd = 0x3,       ///< Store Doubleword

    /* Load Opcodes (Load / LoadFp) */
    Lb = 0x0,       ///< Load Byte
    Lh = 0x1,       ///< Load Halfword
    Lw = 0x2,       ///< Load Word
    Ld = 0x3,       ///< Load Doubleword
    Lbu = 0x4,      ///< Load Byte Unsigned
    Lhu = 0x5,      ///< Load Halfword Unsigned
    Lwu = 0x6,      ///< Load Word Unsigned

    /* Floating-Point Load/Store sizes (aliases of Lw/Ld, Sw/Sd) */
    Flw = 0x2,      ///< Floating-Point Load Word
    Fld = 0x3,      ///< Floating-Point Load Doubleword
    Fsw = 0x2,      ///< Floating-Point Store Word
    Fsd = 0x3,      ///< Floating-Point Store Doubleword

    /* Branch Opcodes (Branch) */
    Beq = 0x0,      ///< Branch if Equal
    Bne = 0x1,      ///< Branch if Not Equal
    Blt = 0x4,      ///< Branch if Less Than
    Bge = 0x5,      ///< Branch if Greater Than or Equal
    Bltu = 0x6,     ///< Branch if Less Than Unsigned
    Bgeu = 0x7,     ///< Branch if Greater or Equal Unsigned

    /* MiscMem Opcodes (MiscMem) */
    Fence = 0x0,    ///< Memory/Device Ordering Fence
    FenceI = 0x1,   ///< Instruction Cache Ordering Fence

    /* System Opcodes (System) */
    Priv = 0x0,     ///< Privileged System call (ECALL, EBREAK, SRET, MRET, WFI)
    Csrrw = 0x1,    ///< CSR Read/Write
    Csrrs = 0x2,    ///< CSR Read and Set Bits
    Csrrc = 0x3,    ///< CSR Read and Clear Bits
    Csrrwi = 0x5,   ///< CSR Read/Write Immediate
    Csrrsi = 0x6,   ///< CSR Read and Set Bits Immediate
    Csrrci = 0x7,   ///< CSR Read and Clear Bits Immediate
};

/**
 * @enum IsaExtension
 * @brief Identifies standard RISC-V extensions mapped to their bit positions in the MISA CSR.
 */
enum class IsaExtension : uint8_t {
    A = 0,  ///< Atomic Instructions extension
    B = 1,  ///< Bitmanip Instructions extension
    C = 2,  ///< Compressed Instructions extension
    D = 3,  ///< Double-Precision Floating-Point extension
    F = 5,  ///< Single-Precision Floating-Point extension
    I = 8,  ///< Base Integer Instruction Set extension
    M = 12, ///< Integer Multiply/Divide extension
    S = 18, ///< Supervisor Privilege Level support
    U = 20, ///< User Privilege Level support
    V = 21, ///< Vector Extension
};

/**
 * @enum MisaProfile
 * @brief Pre-configured machine profiles matching common RISC-V extension sets.
 */
enum class MisaProfile : uint8_t {
    I,    ///< RV32I / RV64I base integer only
    IMAC, ///< Integer, Multiply/Divide, Atomic, and Compressed
    GC,   ///< General Purpose (IMAFD) + Compressed (equivalent to RV32GC or RV64GC)
};

/**
 * @enum InstFormat
 * @brief Architectural RISC-V instruction format layout categories.
 */
enum class InstFormat : uint8_t {
    R,       ///< Register-Register Format (opcode, rd, funct3, rs1, rs2, funct7)
    I,       ///< Register-Immediate / Load / Jalr / CSR format (opcode, rd, funct3, rs1, imm12)
    S,       ///< Store Format (opcode, imm[11:5], funct3, rs1, rs2, imm[4:0])
    B,       ///< Conditional Branch Format (opcode, imm[12], imm[10:5], funct3, rs1, rs2, imm[4:1], imm[11])
    U,       ///< Upper-Immediate format (opcode, rd, imm[31:12])
    J,       ///< Unconditional Jump format (opcode, rd, imm[20], imm[10:1], imm[11], imm[19:12])
    R4,      ///< 4-Operand Register Format (opcode, rd, funct3, rs1, rs2, funct2, rs3) - Used for FP FMA
    Unknown  ///< Unidentified or invalid instruction format
};

} // namespace simrv::isa
