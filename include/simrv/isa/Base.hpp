/**
 * @file Base.hpp
 * @brief Base instruction set opcodes, profiles, and extensions.
 */
#pragma once

#include <cstdint>

namespace simrv::isa {

enum class Opcode : uint8_t {
    OpImm32 = 0x1B,
    Op = 0x33,
    Op32 = 0x3B,
    OpFp = 0x53,
    Amo = 0x2F,
    OpImm = 0x13,
    Load = 0x03,
    LoadFp = 0x07,
    Jalr = 0x67,
    Store = 0x23,
    StoreFp = 0x27,
    Branch = 0x63,
    MAdd = 0x43,
    MSub = 0x47,
    NMSub = 0x4B,
    NMAdd = 0x4F,
    Lui = 0x37,
    Auipc = 0x17,
    Jal = 0x6F,
    MiscMem = 0x0F,
    System = 0x73,
};

enum class Funct3 : uint8_t {
    Add = 0x0,
    Sll = 0x1,
    Slt = 0x2,
    Sltu = 0x3,
    Xor = 0x4,
    Srl = 0x5,
    Or = 0x6,
    And = 0x7,
    Mul = 0x0,
    Mulh = 0x1,
    Mulhsu = 0x2,
    Mulhu = 0x3,
    Div = 0x4,
    Divu = 0x5,
    Rem = 0x6,
    Remu = 0x7,
    Sb = 0x0,
    Sh = 0x1,
    Sw = 0x2,
    Sd = 0x3,
    Lb = 0x0,
    Lh = 0x1,
    Lw = 0x2,
    Ld = 0x3,
    Lbu = 0x4,
    Lhu = 0x5,
    Flw = 0x2,
    Fld = 0x3,
    Fsw = 0x2,
    Fsd = 0x3,
    Beq = 0x0,
    Bne = 0x1,
    Blt = 0x4,
    Bge = 0x5,
    Bltu = 0x6,
    Bgeu = 0x7,
    Fence = 0x0,
    FenceI = 0x1,
    Priv = 0x0,
    Csrrw = 0x1,
    Csrrs = 0x2,
    Csrrc = 0x3,
    Csrrwi = 0x5,
    Csrrsi = 0x6,
    Csrrci = 0x7,
};

enum class IsaExtension : uint8_t {
    A = 0,
    C = 2,
    D = 3,
    F = 5,
    I = 8,
    M = 12,
    S = 18,
    U = 20,
};

enum class MisaProfile : uint8_t {
    I,
    IMAC,
    GC,
};

} // namespace simrv::isa
