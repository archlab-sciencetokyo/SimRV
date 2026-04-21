/**
 * @file Define.hpp
 * @brief SimRV declarations.
 */
#pragma once

#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "XLen.hpp"

namespace simrv::boot {
inline constexpr Address kStartPc = static_cast<Address>(0x80000000u);
inline constexpr Address kInitDataAddress = static_cast<Address>(0x01000000u);
}  // namespace simrv::boot

using DumpFlags = uint32_t;

enum class DumpFlag : DumpFlags {
    Exec = (1u << 0),
    Reg = (1u << 1),
    Csr = (1u << 2),
};

using PteFlags = Word;

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

namespace simrv::virtio {
inline constexpr Address kBaseAddress = static_cast<Address>(0x40000000u);
inline constexpr Address kRegionSize = static_cast<Address>(0x08000000u);
inline constexpr uint32_t kConsoleMaxQueueNum = 2;
inline constexpr uint32_t kConsoleIrq = 1;
inline constexpr uint32_t kDiskSectorSize = 512;
inline constexpr uint32_t kDiskBufferSize = (512u * 512u);
inline constexpr uint32_t kDiskSize = (128u * 1024u * 1024u);
inline constexpr uint32_t kDiskMaxQueueNum = 4;
inline constexpr uint32_t kDiskIrq = 2;
}  // namespace simrv::virtio
enum class VringDescFlag : uint16_t {
    Next = 1,
    Write = 2,
    Indirect = 4,
};

using VringDescFlags = uint16_t;
enum class VirtioBlkType : uint32_t {
    In = 0,
    Out = 1,
};

enum class VirtioBlkStatus : uint32_t {
    Ok = 0,
    IoErr = 1,
    Unsupp = 2,
};

constexpr Address DISK_MASK = static_cast<Address>(0x03ffffffu);

namespace simrv::mmio {
inline constexpr Address kPlicBaseAddress = static_cast<Address>(0x50000000u);
inline constexpr Address kPlicSize = static_cast<Address>(0x00400000u);
inline constexpr Address kPlicHartBase = static_cast<Address>(0x00200000u);
inline constexpr Address kPlicHartSize = static_cast<Address>(0x00001000u);
inline constexpr Address kClintBaseAddress = static_cast<Address>(0x60000000u);
inline constexpr Address kClintSize = static_cast<Address>(0x000c0000u);
}  // namespace simrv::mmio

namespace simrv::memory {
inline constexpr Address kDramBaseAddress = static_cast<Address>(0x80000000u);
inline constexpr Address kDramSize = static_cast<Address>(64u * 1024u * 1024u);
inline constexpr Address kDramMask = static_cast<Address>(0x03ffffffu);
inline constexpr unsigned kPageShift = 12;
inline constexpr Address kPageMask = static_cast<Address>(0x00000fffu);
inline constexpr uint32_t kTlbSize = 4;
inline constexpr size_t kLocalCoreMemorySize = (32u * 1024u);
}  // namespace simrv::memory

namespace simrv::compiler {
template <typename T>
constexpr bool likely(T value) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_expect(static_cast<bool>(value), true);
#else
    return static_cast<bool>(value);
#endif
}

template <typename T>
constexpr bool unlikely(T value) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_expect(static_cast<bool>(value), false);
#else
    return static_cast<bool>(value);
#endif
}
}  // namespace simrv::compiler

enum class TrapFlag : TrapCause {
    Interrupt = static_cast<TrapCause>(1u << 31),
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

enum class PrivMode : PrivilegeLevel {
    U = 0,
    S = 1,
    H = 2,
    M = 3,
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

enum class PrivilegeMode : PrivilegeLevel {
    User = static_cast<PrivilegeLevel>(PrivMode::U),
    Supervisor = static_cast<PrivilegeLevel>(PrivMode::S),
    Hypervisor = static_cast<PrivilegeLevel>(PrivMode::H),
    Machine = static_cast<PrivilegeLevel>(PrivMode::M),
};

template <typename EnumType>
constexpr auto enum_mask(EnumType bit) -> std::underlying_type_t<EnumType> {
    return static_cast<std::underlying_type_t<EnumType>>(bit);
}

template <typename EnumType>
constexpr bool has_enum_mask(std::underlying_type_t<EnumType> value, EnumType bit) {
    return (value & enum_mask(bit)) != 0;
}

constexpr TrapCause kInterruptCauseBit = enum_mask(TrapFlag::Interrupt);
constexpr PrivilegeLevel kPrivUser = static_cast<PrivilegeLevel>(PrivilegeMode::User);
constexpr PrivilegeLevel kPrivSupervisor = static_cast<PrivilegeLevel>(PrivilegeMode::Supervisor);
constexpr PrivilegeLevel kPrivMachine = static_cast<PrivilegeLevel>(PrivilegeMode::Machine);

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
constexpr CSRValue kMstatusSd = static_cast<CSRValue>(1u << 31);
constexpr CSRValue kMstatusSstatusReadMask = static_cast<CSRValue>(0x000de133u);
constexpr CSRValue kMstatusReadMask = static_cast<CSRValue>(0xffffffffu);
constexpr CSRValue kFflagsMask = static_cast<CSRValue>(0x1fu);
constexpr CSRValue kFrmMask = static_cast<CSRValue>(0x7u);
constexpr CSRValue kFcsrMask = static_cast<CSRValue>(0xffu);
constexpr unsigned kFrmShift = 5;

struct MstatusFields {
    Word uie : 1;
    Word sie : 1;
    Word hie : 1;
    Word mie : 1;
    Word upie : 1;
    Word spie : 1;
    Word hpie : 1;
    Word mpie : 1;
    Word spp : 1;
    Word hpp : 2;
    Word mpp : 2;
    Word fs : 2;
    Word xs : 2;
    Word mprv : 1;
    Word sum : 1;
    Word mxr : 1;
    Word reserved : 12;
};

union MstatusView {
    Word rawValue;
    MstatusFields bits;

    explicit MstatusView(CSRValue raw = 0) : rawValue(static_cast<Word>(raw)) {}

    CSRValue raw() const { return static_cast<CSRValue>(rawValue); }
};

struct MipFields {
    Word usip : 1;
    Word ssip : 1;
    Word hsip : 1;
    Word msip : 1;
    Word utip : 1;
    Word stip : 1;
    Word htip : 1;
    Word mtip : 1;
    Word ueip : 1;
    Word seip : 1;
    Word heip : 1;
    Word meip : 1;
    Word reserved : 20;
};

union MipView {
    Word rawValue;
    MipFields bits;

    explicit MipView(CSRValue raw = 0) : rawValue(static_cast<Word>(raw)) {}

    CSRValue raw() const { return static_cast<CSRValue>(rawValue); }
};

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

enum class AmoStatus : Instruction {
    Success = 0,
    Failure = 1,
};

enum class Opcode : Instruction {
    C0 = 0x0,
    C1 = 0x1,
    C2 = 0x2,
    W = 0x3,
    Op = 0x33,
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

enum class Funct3 : Instruction {
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

enum class Funct12Priv : Instruction {
    Ecall = 0x000,
    Ebreak = 0x001,
    Uret = 0x002,
    Sret = 0x102,
    Mret = 0x302,
    Wfi = 0x105,
};

enum class Funct7Priv : Instruction {
    SfenceVma = 0x09,
};

enum class Funct5Amo : Instruction {
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

constexpr Opcode opcode_of(Instruction ir) { return static_cast<Opcode>(ir & 0x7F); }

constexpr Opcode compressed_opcode_of(CompressedInstruction ir) {
    return static_cast<Opcode>(ir & 0x3);
}

constexpr Funct3 funct3_of(Instruction ir) { return static_cast<Funct3>((ir >> 12) & 0x7); }

constexpr Instruction funct12_of(Instruction ir) { return (ir >> 20) & 0xFFF; }

constexpr Instruction funct7_of(Instruction ir) { return (ir >> 25) & 0x7F; }

constexpr Funct5Amo funct5_of(Instruction ir) { return static_cast<Funct5Amo>((ir >> 27) & 0x1F); }

enum class IsaExtension : unsigned {
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

constexpr CSRValue misa_extension_bit(IsaExtension ext) {
    return static_cast<CSRValue>(CSRValue{1} << static_cast<unsigned>(ext));
}

constexpr CSRValue misa_base_bits() {
    return misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M) |
           misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::F) |
           misa_extension_bit(IsaExtension::D) | misa_extension_bit(IsaExtension::C) |
           misa_extension_bit(IsaExtension::S) | misa_extension_bit(IsaExtension::U);
}

constexpr CSRValue misa_profile_bits(MisaProfile profile) {
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

constexpr CSRValue misa_mxl_field() {
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

constexpr CSRValue misa_with_mxl(CSRValue misa_extensions) {
    return misa_extensions | misa_mxl_field();
}

constexpr CSRValue kMisaDefault = misa_profile_bits(MisaProfile::GC);

constexpr bool misa_has_extension(CSRValue misa, IsaExtension ext) {
    return (misa & misa_extension_bit(ext)) != 0;
}

constexpr IsaExtension required_extension_for_instruction(Instruction ir, bool compressed) {
    if (compressed) {
        return IsaExtension::C;
    }

    switch (opcode_of(ir)) {
        case Opcode::Amo:
            return IsaExtension::A;
        case Opcode::Op:
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

constexpr bool instruction_enabled_by_misa(CSRValue misa, Instruction ir, bool compressed) {
    return misa_has_extension(misa, required_extension_for_instruction(ir, compressed));
}

enum OperationId : uint16_t {
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
    LBU,
    LHU,
    SB,
    SH,
    SW,
    ADDI,
    SLTI,
    SLTIU,
    XORI,
    ORI,
    ANDI,
    SLLI,
    SRLI,
    SRAI,
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

struct QueueState {
    Word Ready;
    Word Notify;
    Address DescLow;
    Address DescHigh;
    Address AvailLow;
    Address AvailHigh;
    Address UsedLow;
    Address UsedHigh;
    Word last_avail_idx;  //    uint16_t last_avail_idx;
};

struct BlockRequestHeader {
    Word type;
    Word ioprio;
    Counter sector_num;
};

struct Descriptor {
    Counter adr;
    Word len;
    uint16_t flags;
    uint16_t next;
};
