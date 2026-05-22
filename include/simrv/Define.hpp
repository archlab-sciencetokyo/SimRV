/**
 * @file Define.hpp
 * @brief Core ISA constants, enums, and shared simulator type definitions.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

#ifndef SIMRV_CORE_COUNT
#define SIMRV_CORE_COUNT 1
#endif
inline constexpr unsigned kCoreCount = SIMRV_CORE_COUNT;

using DumpFlags = uint8_t;

enum class DumpFlag : DumpFlags {
    Exec = (1u << 0),
    Reg = (1u << 1),
    Csr = (1u << 2),
};

using PteFlags = uint8_t;

enum class PteFlag : PteFlags {
    V = (1 << 0),
    R = (1 << 1),
    W = (1 << 2),
    X = (1 << 3),
    U = (1 << 4),
    A = (1 << 6),
    D = (1 << 7),
};

enum class PteAccess : uint8_t { Read = 0, Write = 1, Code = 2 };

constexpr uint32_t LEVELS = 2;
constexpr uint32_t PTE_SIZE = 4;
constexpr uint32_t PAGE_SIZE = (1u << 12);

// Virtio vring descriptor and block device status/type enums

using VringDescFlags = uint8_t;
enum class VringDescFlag : VringDescFlags {
    Next = 1,
    Write = 2,
    Indirect = 4,
};

enum class VirtioBlkType : uint8_t {
    In = 0,
    Out = 1,
};

enum class VirtioBlkStatus : uint8_t {
    Ok = 0,
    IoErr = 1,
    Unsupp = 2,
};

constexpr Address DISK_MASK = static_cast<Address>(0x03ffffffu);

namespace simrv::compiler {
template <typename T>
constexpr auto likely(T value) -> bool {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_expect(static_cast<bool>(value), true);
#else
    return static_cast<bool>(value);
#endif
}

template <typename T>
constexpr auto unlikely(T value) -> bool {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_expect(static_cast<bool>(value), false);
#else
    return static_cast<bool>(value);
#endif
}
}  // namespace simrv::compiler

enum class TrapFlag : TrapCause {
    Interrupt = static_cast<TrapCause>(Word{1} << (simrv::xlen::kXLenBits - 1u)),
};

enum class ExceptionCode : TrapCause {
    MisalignedFetch = 0x0,
    FaultFetch = 0x1,
    IllegalInstruction = 0x2,
    Breakpoint = 0x3,
    MisalignedLoad = 0x4,
    FaultLoad = 0x5,
    MisalignedStore = 0x6,
    FaultStore = 0x7,
    UserEcall = 0x8,
    SupervisorEcall = 0x9,
    HypervisorEcall = 0xa,
    MachineEcall = 0xb,
    FetchPageFault = 0xc,
    LoadPageFault = 0xd,
    StorePageFault = 0xf,
};



enum class MstatusBit : CSRValue {
    Uie = (1u << 0),
    Sie = (1u << 1),
    Hie = (1u << 2),
    Mie = (1u << 3),
    Upie = (1u << 4),
    Spie = (1u << 5),
    Hpie = (1u << 6),
    Mpie = (1u << 7),
    Spp = (1u << 8),
    Hpp = (3u << 9),
    Mpp = (3u << 11),
    Fs = (3u << 13),
    Xs = (3u << 15),
    Mprv = (1u << 17),
    Sum = (1u << 18),
    Mxr = (1u << 19),
    Tvm = (1u << 20),
    Tw = (1u << 21),
    Tsr = (1u << 22),
};

enum class MipBit : CSRValue {
    Usip = (1u << 0),
    Ssip = (1u << 1),
    Hsip = (1u << 2),
    Msip = (1u << 3),
    Utip = (1u << 4),
    Stip = (1u << 5),
    Htip = (1u << 6),
    Mtip = (1u << 7),
    Ueip = (1u << 8),
    Seip = (1u << 9),
    Heip = (1u << 10),
    Meip = (1u << 11),
};



template <typename EnumType>
constexpr auto enum_mask(EnumType bit) {
    return std::to_underlying(bit);
}

template <typename EnumType>
constexpr bool has_enum_mask(std::underlying_type_t<EnumType> value, EnumType bit) {
    return (value & enum_mask(bit)) != 0;
}

constexpr TrapCause kInterruptCauseBit = enum_mask(TrapFlag::Interrupt);
constexpr TrapCause kExceptionCodeMask = static_cast<TrapCause>(kInterruptCauseBit - 1u);

constexpr auto trap_exception_code(TrapCause cause) -> TrapCause {
    return cause & kExceptionCodeMask;
}

constexpr auto trap_is_interrupt(TrapCause cause) -> bool {
    return (cause & kInterruptCauseBit) != 0u;
}

constexpr PrivilegeLevel kPrivUser = PrivilegeLevel::User;
constexpr PrivilegeLevel kPrivSupervisor = PrivilegeLevel::Supervisor;
constexpr PrivilegeLevel kPrivMachine = PrivilegeLevel::Machine;

constexpr CSRValue kMstatusMask =
    (enum_mask(MstatusBit::Uie) | enum_mask(MstatusBit::Sie) | enum_mask(MstatusBit::Mie) |
     enum_mask(MstatusBit::Upie) | enum_mask(MstatusBit::Spie) | enum_mask(MstatusBit::Mpie) |
     enum_mask(MstatusBit::Spp) | enum_mask(MstatusBit::Mpp) | enum_mask(MstatusBit::Fs) |
     enum_mask(MstatusBit::Mprv) | enum_mask(MstatusBit::Sum) | enum_mask(MstatusBit::Mxr));
constexpr CSRValue kSstatusMask =
    (enum_mask(MstatusBit::Uie) | enum_mask(MstatusBit::Sie) | enum_mask(MstatusBit::Upie) |
     enum_mask(MstatusBit::Spie) | enum_mask(MstatusBit::Spp) | enum_mask(MstatusBit::Fs) |
     enum_mask(MstatusBit::Xs) | enum_mask(MstatusBit::Sum) | enum_mask(MstatusBit::Mxr));
constexpr CSRValue kMstatusFsDirty = enum_mask(MstatusBit::Fs);
constexpr CSRValue kMstatusSd = static_cast<CSRValue>(Word{1} << (simrv::xlen::kXLenBits - 1u));
constexpr CSRValue kMstatusSstatusReadMask = static_cast<CSRValue>(0x000de133u) | kMstatusSd;
constexpr CSRValue kMstatusReadMask = static_cast<CSRValue>(kXLenMask);
constexpr CSRValue kFflagsMask = static_cast<CSRValue>(0x1fu);
constexpr CSRValue kFrmMask = static_cast<CSRValue>(0x7u);
constexpr CSRValue kFcsrMask = static_cast<CSRValue>(0xffu);
constexpr unsigned kFrmShift = 5;

enum class Csr : CSRAddress {
    Ustatus = 0x000,
    Uie = 0x004,
    Utvec = 0x005,
    Uscratch = 0x040,
    Uepc = 0x041,
    Ucause = 0x042,
    Utval = 0x043,
    Uip = 0x044,
    Fflags = 0x001,
    Frm = 0x002,
    Fcsr = 0x003,
    Pmpcfg0 = 0x3A0,
    Pmpaddr0 = 0x3B0,
    Cycle = 0xC00,
    Time = 0xC01,
    Instret = 0xC02,
    Sstatus = 0x100,
    Sedeleg = 0x102,
    Sideleg = 0x103,
    Sie = 0x104,
    Stvec = 0x105,
    Scounteren = 0x106,
    Sscratch = 0x140,
    Sepc = 0x141,
    Scause = 0x142,
    Stval = 0x143,
    Sip = 0x144,
    Satp = 0x180,
    Mvendorid = 0xF11,
    Marchid = 0xF12,
    Mimpid = 0xF13,
    Mhartid = 0xF14,
    Mstatus = 0x300,
    Misa = 0x301,
    Medeleg = 0x302,
    Mideleg = 0x303,
    Mie = 0x304,
    Mtvec = 0x305,
    Mcounteren = 0x306,
    Mscratch = 0x340,
    Mepc = 0x341,
    Mcause = 0x342,
    Mtval = 0x343,
    Mip = 0x344,
    Mcycle = 0xB00,
    Minstret = 0xB02,
    Mcycleh = 0xB80,
    Minstreth = 0xB82,
    Cycleh = 0xC80,
    Timeh = 0xC81,
    Instreth = 0xC82,
};

constexpr CSRAddress csr_addr(Csr csr) { return static_cast<CSRAddress>(csr); }

//
//
//
// RISC-V Instruction Set Architecture
//
//
// The RISC-V Instruction Set Manual
// Volume I: User-Level ISA
// Document Version 2.2
//
//
//

constexpr size_t XLEN = sizeof(Word) * 8;
constexpr size_t MLEN = sizeof(Address) * 8;
constexpr size_t FLEN = 64;
constexpr Instruction RV32_NOP = 0x00000013;

enum class AmoStatus : uint8_t {
    Success = 0,
    Failure = 1,
};

enum class CompressedOpcode : uint8_t {
    C0 = 0x0,
    C1 = 0x1,
    C2 = 0x2,
    C3 = 0x3,
};

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

enum class Funct7Fp : Word {
    FaddS = 0x00,
    FaddD = 0x01,
    FsubS = 0x04,
    FsubD = 0x05,
    FmulS = 0x08,
    FmulD = 0x09,
    FdivS = 0x0c,
    FdivD = 0x0d,
    FsqrtS = 0x2c,
    FsqrtD = 0x2d,
    FsgnjS = 0x10,
    FsgnjD = 0x11,
    FminmaxS = 0x14,
    FminmaxD = 0x15,
    FcvtSD = 0x20,
    FcvtDS = 0x21,
    FcmpS = 0x50,
    FcmpD = 0x51,
    FcvtWS = 0x60,
    FcvtWD = 0x61,
    FcvtSW = 0x68,
    FcvtDW = 0x69,
    FmvXW = 0x70,
    FmvXD = 0x71,
    FmvWX = 0x78,
    FmvDX = 0x79,
};

enum class Funct3Fp : Word {
    Min = 0x0,
    Max = 0x1,
    Leq = 0x0,
    Lt = 0x1,
    Eq = 0x2,
    FmvXW = 0x0,
    Fclass = 0x1,
};

enum class FflagsBit : uint32_t {
    Nx = 0x01,
    Uf = 0x02,
    Of = 0x04,
    Dz = 0x08,
    Nv = 0x10,
};

enum class RoundingMode : Word {
    Rne = 0x0,
    Rtz = 0x1,
    Rdn = 0x2,
    Rup = 0x3,
    Rmm = 0x4,
    Dyn = 0x7,
};

enum class Funct12Priv : uint16_t {
    Ecall = 0x000,
    Ebreak = 0x001,
    Uret = 0x002,
    Sret = 0x102,
    Mret = 0x302,
    Wfi = 0x105,
};

enum class Funct7Priv : uint8_t {
    SfenceVma = 0x09,
};

enum class Funct5Amo : uint8_t {
    Lr = 0x02,
    Sc = 0x03,
    Swap = 0x01,
    Add = 0x00,
    And = 0x0c,
    Or = 0x08,
    Xor = 0x04,
    Min = 0x10,
    Minu = 0x18,
    Max = 0x14,
    Maxu = 0x1c,
};

constexpr auto opcode_of(Instruction ir) -> Opcode { return static_cast<Opcode>(ir & 0x7F); }

constexpr auto compressed_opcode_of(CompressedInstruction ir) -> CompressedOpcode {
    return static_cast<CompressedOpcode>(ir & 0x3);
}

constexpr auto funct3_of(Instruction ir) -> Funct3 { return static_cast<Funct3>((ir >> 12) & 0x7); }

constexpr auto funct12_of(Instruction ir) -> Instruction { return (ir >> 20) & 0xFFF; }

constexpr auto funct7_of(Instruction ir) -> Instruction { return (ir >> 25) & 0x7F; }

constexpr auto funct5_of(Instruction ir) -> Funct5Amo {
    return static_cast<Funct5Amo>((ir >> 27) & 0x1F);
}

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
    // Keep the API profile-oriented so RV64 presets can be added without touching call sites.
};

constexpr auto misa_extension_bit(IsaExtension ext) -> CSRValue {
    return static_cast<CSRValue>(CSRValue{1} << static_cast<unsigned>(ext));
}

constexpr auto misa_base_bits() -> CSRValue {
    return misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M) |
           misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::F) |
           misa_extension_bit(IsaExtension::D) | misa_extension_bit(IsaExtension::C) |
           misa_extension_bit(IsaExtension::S) | misa_extension_bit(IsaExtension::U);
}

constexpr auto misa_profile_bits(MisaProfile profile) -> CSRValue {
    switch (profile) {
        case MisaProfile::I:
            return misa_extension_bit(IsaExtension::I);
        case MisaProfile::IMAC:
            return misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M) |
                   misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::C);
        case MisaProfile::GC:
            return misa_base_bits();
        default:
            return misa_extension_bit(IsaExtension::I);
    }
}

constexpr auto misa_mxl_field() -> CSRValue {
    if constexpr (sizeof(CSRValue) == 4) {
        // MXL=01 for RV32 in bits [31:30]
        return static_cast<CSRValue>(1u << 30);
    } else if constexpr (sizeof(CSRValue) == 8) {
        // MXL=10 for RV64 in bits [63:62]
        return static_cast<CSRValue>(2ull << 62);
    } else {
        return 0;
    }
}

constexpr auto misa_with_mxl(CSRValue misa_extensions) -> CSRValue {
    return misa_extensions | misa_mxl_field();
}

constexpr CSRValue kMisaDefault = misa_profile_bits(MisaProfile::GC);

constexpr auto misa_has_extension(CSRValue misa, IsaExtension ext) -> bool {
    return (misa & misa_extension_bit(ext)) != 0;
}

constexpr auto required_extension_for_instruction(Instruction ir, bool compressed) -> IsaExtension {
    if (compressed) {
        return IsaExtension::C;
    }

    switch (opcode_of(ir)) {
        case Opcode::Amo:
            return IsaExtension::A;
        case Opcode::Op:
        case Opcode::Op32:
            return (funct7_of(ir) & 0x1u) ? IsaExtension::M : IsaExtension::I;
        case Opcode::LoadFp:
        case Opcode::StoreFp:
            return (funct3_of(ir) == Funct3::Fld || funct3_of(ir) == Funct3::Fsd) ? IsaExtension::D
                                                                                  : IsaExtension::F;
        case Opcode::OpFp:
        case Opcode::MAdd:
        case Opcode::MSub:
        case Opcode::NMAdd:
        case Opcode::NMSub:
            return (((ir >> 25) & 0x3u) == 0x1u) ? IsaExtension::D : IsaExtension::F;
        default:
            return IsaExtension::I;
    }
}

constexpr auto instruction_enabled_by_misa(CSRValue misa, Instruction ir, bool compressed) -> bool {
    return misa_has_extension(misa, required_extension_for_instruction(ir, compressed));
}

enum OperationId : uint8_t {
    /* RV32I */
    LUI,
    AUIPC,
    JAL,
    JALR,
    BEQ,
    BNE,
    BLT,
    BGE,
    BLTU,
    BGEU,
    LB,
    LH,
    LW,
    LD,
    LBU,
    LHU,
    LWU,
    SB,
    SH,
    SW,
    SD,
    ADDI,
    SLTI,
    SLTIU,
    XORI,
    ORI,
    ANDI,
    SLLI,
    SRLI,
    SRAI,
    ADDIW,
    SLLIW,
    SRLIW,
    SRAIW,
    ADD,
    SUB,
    SLL,
    SLT,
    SLTU,
    XOR,
    SRL,
    SRA,
    OR,
    AND,
    ADDW,
    SUBW,
    SLLW,
    SRLW,
    SRAW,
    FENCE,
    FENCE_I,
    ECALL,
    EBREAK,
    CSRRW,
    CSRRS,
    CSRRC,
    CSRRWI,
    CSRRSI,
    CSRRCI,
    /* Privileged */
    URET,
    SRET,
    MRET,
    WFI,
    SFENCE_VMA,
    /* RV32M */
    MUL,
    MULH,
    MULHSU,
    MULHU,
    DIV,
    DIVU,
    REM,
    REMU,
    MULW,
    DIVW,
    DIVUW,
    REMW,
    REMUW,
    /* RV32A */
    LR_W,
    SC_W,
    AMOSWAP_W,
    AMOADD_W,
    AMOXOR_W,
    AMOAND_W,
    AMOOR_W,
    AMOMIN_W,
    AMOMAX_W,
    AMOMINU_W,
    AMOMAXU_W,
    /* RV32F */
    FLW,
    FSW,
    FMADD_S,
    FMSUB_S,
    FNMADD_S,
    FNMSUB_S,
    FADD_S,
    FSUB_S,
    FMUL_S,
    FDIV_S,
    FSQRT_S,
    FSGNJ_S,
    FSGNJN_S,
    FSGNJX_S,
    FMIN_S,
    FMAX_S,
    FCVT_W_S,
    FCVT_WU_S,
    FMV_X_W,
    FEQ_S,
    FLT_S,
    FLE_S,
    FCLASS_S,
    FCVT_S_W,
    FCVT_S_WU,
    FMV_W_X,
    /* RV32D */
    FLD,
    FSD,
    FMADD_D,
    FMSUB_D,
    FNMSUB_D,
    FNMADD_D,
    FADD_D,
    FSUB_D,
    FMUL_D,
    FDIV_D,
    FSQRT_D,
    FSGNJ_D,
    FSGNJN_D,
    FSGNJX_D,
    FMIN_D,
    FMAX_D,
    FCVT_S_D,
    FCVT_D_S,
    FEQ_D,
    FLT_D,
    FLE_D,
    FCLASS_D,
    FCVT_W_D,
    FCVT_WU_D,
    FCVT_D_W,
    FCVT_D_WU,
    /* Others */
    UNKNOWN,
    OperationIdCount
};

/* Operation-id ranges by ISA extension/profile. */
constexpr OperationId kOpRangeRv32iBegin = LUI;
constexpr OperationId kOpRangeRv32iEnd = CSRRCI;

constexpr OperationId kOpRangePrivBegin = URET;
constexpr OperationId kOpRangePrivEnd = SFENCE_VMA;

constexpr OperationId kOpRangeRv32mBegin = MUL;
constexpr OperationId kOpRangeRv32mEnd = REMU;

constexpr OperationId kOpRangeRv32aBegin = LR_W;
constexpr OperationId kOpRangeRv32aEnd = AMOMAXU_W;

constexpr OperationId kOpRangeRv32fBegin = FLW;
constexpr OperationId kOpRangeRv32fEnd = FMV_W_X;

constexpr OperationId kOpRangeRv32dBegin = FLD;
constexpr OperationId kOpRangeRv32dEnd = FCVT_D_WU;

constexpr size_t kOperationIdCount = static_cast<size_t>(OperationIdCount);
