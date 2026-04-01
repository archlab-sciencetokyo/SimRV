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
#include <type_traits>

template <size_t Bits>
struct XLenTypes;

template <>
struct XLenTypes<32> {
    using Word = uint32_t;
    using SignedWord = int32_t;
    using Register = uint32_t;
};

template <>
struct XLenTypes<64> {
    using Word = uint64_t;
    using SignedWord = int64_t;
    using Register = uint64_t;
};

// Active architectural scalar set. RV32 remains the default until RV64 bring-up.
using ActiveXLenTypes = XLenTypes<32>;
using Byte = std::byte;
using CompressedInstruction = uint16_t;
using Word = ActiveXLenTypes::Word;
using SignedWord = ActiveXLenTypes::SignedWord;
using Register = ActiveXLenTypes::Register;
using Counter = uint64_t;
using Address = Word;
using Instruction = Word;
using CSRValue = Word;
using CSRAddress = Address;
using ImmValue = SignedWord;
using TrapCause = Word;
using PrivilegeLevel = Word;

constexpr Address D_START_PC = static_cast<Address>(0x80000000u);
constexpr Address D_INITD_ADDR = static_cast<Address>(0x01000000u);

constexpr uint32_t FLAG_DUMP_EXEC = (1u << 0);
constexpr uint32_t FLAG_DUMP_REG = (1u << 1);
constexpr uint32_t FLAG_DUMP_CSR = (1u << 2);
constexpr uint32_t FLAG_DUMP_MIURA = (1u << 3);

constexpr Word PTE_V_MASK = (Word{1} << 0);
constexpr Word PTE_R_MASK = (Word{1} << 1);
constexpr Word PTE_W_MASK = (Word{1} << 2);
constexpr Word PTE_X_MASK = (Word{1} << 3);
constexpr Word PTE_U_MASK = (Word{1} << 4);
constexpr Word PTE_A_MASK = (Word{1} << 6);
constexpr Word PTE_D_MASK = (Word{1} << 7);

enum PTE_ACCESS { ACCESS_READ = 0, ACCESS_WRITE = 1, ACCESS_CODE = 2 };

constexpr uint32_t LEVELS = 2;
constexpr uint32_t PTE_SIZE = 4;
constexpr uint32_t PAGE_SIZE = (1u << 12);

constexpr Address VIRTIO_BASE_ADDR = static_cast<Address>(0x40000000u);
constexpr Address VIRTIO_SIZE = static_cast<Address>(0x08000000u);
constexpr uint16_t VRING_DESC_F_NEXT = 1;
constexpr uint16_t VRING_DESC_F_WRITE = 2;
constexpr uint16_t VRING_DESC_F_INDIRECT = 4;

/* console */
constexpr uint32_t CONSOLE_MAX_QUEUE_NUM = 2;
constexpr uint32_t VIRTIO_CONSOLE_IRQ = 1;

/* block device (disk) */
constexpr uint32_t SECTOR_SIZE = 512;
constexpr uint32_t DISK_BUF_SIZE = (512u * 512u);
constexpr uint32_t DISK_SIZE = (64u * 1024u * 1024u);
constexpr uint32_t DISK_MAX_QUEUE_NUM = 4;
constexpr uint32_t VIRTIO_DISK_IRQ = 2;
constexpr uint32_t VIRTIO_BLK_T_IN = 0;
constexpr uint32_t VIRTIO_BLK_T_OUT = 1;
constexpr uint32_t VIRTIO_BLK_S_OK = 0;
constexpr uint32_t VIRTIO_BLK_S_IOERR = 1;
constexpr uint32_t VIRTIO_BLK_S_UNSUPP = 2;

constexpr Address DISK_MASK = static_cast<Address>(0x03ffffffu);

constexpr Address PLIC_BASE_ADDR = static_cast<Address>(0x50000000u);
constexpr Address PLIC_SIZE = static_cast<Address>(0x00400000u);
constexpr Address PLIC_HART_BASE = static_cast<Address>(0x00200000u);
constexpr Address PLIC_HART_SIZE = static_cast<Address>(0x00001000u);

constexpr Address CLINT_BASE_ADDR = static_cast<Address>(0x60000000u);
constexpr Address CLINT_SIZE = static_cast<Address>(0x000c0000u);

constexpr Address DRAM_BASE_ADDR = static_cast<Address>(0x80000000u);
constexpr Address DRAM_SIZE = static_cast<Address>(64u * 1024u * 1024u);
constexpr Address DRAM_MASK = static_cast<Address>(0x03ffffffu);
constexpr unsigned D_PAGE_SHIFT = 12;
constexpr Address D_PAGE_MASK = static_cast<Address>(0x00000fffu);
constexpr uint32_t TLB_SIZE = 4;

constexpr size_t LCMEM_SIZE = (32u * 1024u);

constexpr TrapCause CAUSE_INTERRUPT = static_cast<TrapCause>(1u << 31);

enum EXCEPTION_CODE {
    CAUSE_MISALIGNED_FETCH = 0x0,
    CAUSE_FAULT_FETCH = 0x1,
    CAUSE_ILLEGAL_INSTRUCTION = 0x2,
    CAUSE_BREAKPOINT = 0x3,
    CAUSE_MISALIGNED_LOAD = 0x4,
    CAUSE_FAULT_LOAD = 0x5,
    CAUSE_MISALIGNED_STORE = 0x6,
    CAUSE_FAULT_STORE = 0x7,
    CAUSE_USER_ECALL = 0x8,
    CAUSE_SUPERVISOR_ECALL = 0x9,
    CAUSE_HYPERVISOR_ECALL = 0xa,
    CAUSE_MACHINE_ECALL = 0xb,
    CAUSE_FETCH_PAGE_FAULT = 0xc,
    CAUSE_LOAD_PAGE_FAULT = 0xd,
    CAUSE_STORE_PAGE_FAULT = 0xf
};

enum PRIV_MODE { PRIV_U = 0, PRIV_S = 1, PRIV_H = 2, PRIV_M = 3 };

constexpr CSRValue COUNTEREN_MASK = (1u << 0) | (1u << 2);

constexpr CSRValue MSTATUS_UIE = (1u << 0);
constexpr CSRValue MSTATUS_SIE = (1u << 1);
constexpr CSRValue MSTATUS_HIE = (1u << 2);
constexpr CSRValue MSTATUS_MIE = (1u << 3);
constexpr CSRValue MSTATUS_UPIE = (1u << 4);
constexpr CSRValue MSTATUS_SPIE = (1u << 5);
constexpr CSRValue MSTATUS_HPIE = (1u << 6);
constexpr CSRValue MSTATUS_MPIE = (1u << 7);
constexpr CSRValue MSTATUS_SPP = (1u << 8);
constexpr CSRValue MSTATUS_HPP = (3u << 9);
constexpr CSRValue MSTATUS_MPP = (3u << 11);
constexpr CSRValue MSTATUS_FS = (3u << 13);
constexpr CSRValue MSTATUS_XS = (3u << 15);
constexpr CSRValue MSTATUS_MPRV = (1u << 17);
constexpr CSRValue MSTATUS_SUM = (1u << 18);
constexpr CSRValue MSTATUS_MXR = (1u << 19);

constexpr unsigned MSTATUS_SPIE_SHIFT = 5;
constexpr unsigned MSTATUS_MPIE_SHIFT = 7;
constexpr unsigned MSTATUS_SPP_SHIFT = 8;
constexpr unsigned MSTATUS_MPP_SHIFT = 11;
constexpr unsigned MSTATUS_FS_SHIFT = 13;

constexpr CSRValue MSTATUS_MASK =
    (MSTATUS_UIE | MSTATUS_SIE | MSTATUS_MIE | MSTATUS_UPIE | MSTATUS_SPIE | MSTATUS_MPIE |
     MSTATUS_SPP | MSTATUS_MPP | MSTATUS_FS | MSTATUS_MPRV | MSTATUS_SUM | MSTATUS_MXR);

constexpr CSRValue SSTATUS_MASK0 =
    (MSTATUS_UIE | MSTATUS_SIE | MSTATUS_UPIE | MSTATUS_SPIE | MSTATUS_SPP | MSTATUS_FS |
     MSTATUS_XS | MSTATUS_SUM | MSTATUS_MXR);

constexpr CSRValue SSTATUS_MASK = SSTATUS_MASK0;

constexpr CSRValue MIP_USIP = (1u << 0);
constexpr CSRValue MIP_SSIP = (1u << 1);
constexpr CSRValue MIP_HSIP = (1u << 2);
constexpr CSRValue MIP_MSIP = (1u << 3);
constexpr CSRValue MIP_UTIP = (1u << 4);
constexpr CSRValue MIP_STIP = (1u << 5);
constexpr CSRValue MIP_HTIP = (1u << 6);
constexpr CSRValue MIP_MTIP = (1u << 7);
constexpr CSRValue MIP_UEIP = (1u << 8);
constexpr CSRValue MIP_SEIP = (1u << 9);
constexpr CSRValue MIP_HEIP = (1u << 10);
constexpr CSRValue MIP_MEIP = (1u << 11);

enum class MstatusBit : CSRValue {
    Uie = MSTATUS_UIE,
    Sie = MSTATUS_SIE,
    Mie = MSTATUS_MIE,
    Upie = MSTATUS_UPIE,
    Spie = MSTATUS_SPIE,
    Mpie = MSTATUS_MPIE,
    Spp = MSTATUS_SPP,
    Mpp = MSTATUS_MPP,
    Fs = MSTATUS_FS,
    Mprv = MSTATUS_MPRV,
    Sum = MSTATUS_SUM,
    Mxr = MSTATUS_MXR,
};

enum class MipBit : CSRValue {
    Msip = MIP_MSIP,
    Ssip = MIP_SSIP,
    Stip = MIP_STIP,
    Mtip = MIP_MTIP,
    Seip = MIP_SEIP,
    Meip = MIP_MEIP,
};

enum class PrivilegeMode : PrivilegeLevel {
    User = PRIV_U,
    Supervisor = PRIV_S,
    Hypervisor = PRIV_H,
    Machine = PRIV_M,
};

template <typename EnumType>
constexpr auto enum_mask(EnumType bit) -> std::underlying_type_t<EnumType> {
    return static_cast<std::underlying_type_t<EnumType>>(bit);
}

template <typename EnumType>
constexpr bool has_enum_mask(std::underlying_type_t<EnumType> value, EnumType bit) {
    return (value & enum_mask(bit)) != 0;
}

constexpr TrapCause kInterruptCauseBit = static_cast<TrapCause>(CAUSE_INTERRUPT);
constexpr PrivilegeLevel kPrivUser = static_cast<PrivilegeLevel>(PrivilegeMode::User);
constexpr PrivilegeLevel kPrivSupervisor = static_cast<PrivilegeLevel>(PrivilegeMode::Supervisor);
constexpr PrivilegeLevel kPrivMachine = static_cast<PrivilegeLevel>(PrivilegeMode::Machine);

constexpr CSRValue kMstatusMask = static_cast<CSRValue>(MSTATUS_MASK);
constexpr CSRValue kSstatusMask = static_cast<CSRValue>(SSTATUS_MASK);
constexpr CSRValue kMstatusMie = enum_mask(MstatusBit::Mie);
constexpr CSRValue kMstatusSie = enum_mask(MstatusBit::Sie);
constexpr CSRValue kMstatusSpie = enum_mask(MstatusBit::Spie);
constexpr CSRValue kMstatusMpie = enum_mask(MstatusBit::Mpie);
constexpr CSRValue kMstatusSpp = enum_mask(MstatusBit::Spp);
constexpr CSRValue kMstatusMpp = enum_mask(MstatusBit::Mpp);
constexpr CSRValue kMstatusFs = enum_mask(MstatusBit::Fs);
constexpr unsigned kMstatusSpieShift = MSTATUS_SPIE_SHIFT;
constexpr unsigned kMstatusMpieShift = MSTATUS_MPIE_SHIFT;
constexpr unsigned kMstatusSppShift = MSTATUS_SPP_SHIFT;
constexpr unsigned kMstatusMppShift = MSTATUS_MPP_SHIFT;

constexpr CSRValue kMipMtip = enum_mask(MipBit::Mtip);

struct MstatusFields {
    uint32_t uie : 1;
    uint32_t sie : 1;
    uint32_t hie : 1;
    uint32_t mie : 1;
    uint32_t upie : 1;
    uint32_t spie : 1;
    uint32_t hpie : 1;
    uint32_t mpie : 1;
    uint32_t spp : 1;
    uint32_t hpp : 2;
    uint32_t mpp : 2;
    uint32_t fs : 2;
    uint32_t xs : 2;
    uint32_t mprv : 1;
    uint32_t sum : 1;
    uint32_t mxr : 1;
    uint32_t reserved : 12;
};

union MstatusView {
    uint32_t raw32;
    MstatusFields bits;

    explicit MstatusView(CSRValue raw = 0) : raw32(static_cast<uint32_t>(raw)) {}

    CSRValue raw() const { return static_cast<CSRValue>(raw32); }
};

struct MipFields {
    uint32_t usip : 1;
    uint32_t ssip : 1;
    uint32_t hsip : 1;
    uint32_t msip : 1;
    uint32_t utip : 1;
    uint32_t stip : 1;
    uint32_t htip : 1;
    uint32_t mtip : 1;
    uint32_t ueip : 1;
    uint32_t seip : 1;
    uint32_t heip : 1;
    uint32_t meip : 1;
    uint32_t reserved : 20;
};

union MipView {
    uint32_t raw32;
    MipFields bits;

    explicit MipView(CSRValue raw = 0) : raw32(static_cast<uint32_t>(raw)) {}

    CSRValue raw() const { return static_cast<CSRValue>(raw32); }
};

/* User-Mode */
constexpr CSRAddress CSR_USTATUS = 0x000;
constexpr CSRAddress CSR_UIE = 0x004;
constexpr CSRAddress CSR_UTVEC = 0x005;
constexpr CSRAddress CSR_USCRATCH = 0x040;
constexpr CSRAddress CSR_UEPC = 0x041;
constexpr CSRAddress CSR_UCAUSE = 0x042;
constexpr CSRAddress CSR_UTVAL = 0x043;
constexpr CSRAddress CSR_UIP = 0x044;
constexpr CSRAddress CSR_FFLAGS = 0x001;
constexpr CSRAddress CSR_FRM = 0x002;
constexpr CSRAddress CSR_FCSR = 0x003;
constexpr CSRAddress CSR_CYCLE = 0xC00;
constexpr CSRAddress CSR_TIME = 0xC01;
constexpr CSRAddress CSR_INSTRET = 0xC02;

/* Supervisor-Mode */
constexpr CSRAddress CSR_SSTATUS = 0x100;
constexpr CSRAddress CSR_SEDELEG = 0x102;
constexpr CSRAddress CSR_SIDELEG = 0x103;
constexpr CSRAddress CSR_SIE = 0x104;
constexpr CSRAddress CSR_STVEC = 0x105;
constexpr CSRAddress CSR_SCOUNTEREN = 0x106;
constexpr CSRAddress CSR_SSCRATCH = 0x140;
constexpr CSRAddress CSR_SEPC = 0x141;
constexpr CSRAddress CSR_SCAUSE = 0x142;
constexpr CSRAddress CSR_STVAL = 0x143;
constexpr CSRAddress CSR_SIP = 0x144;
constexpr CSRAddress CSR_SATP = 0x180;

/* Machine-Mode */
constexpr CSRAddress CSR_MVENDORID = 0xF11;
constexpr CSRAddress CSR_MARCHID = 0xF12;
constexpr CSRAddress CSR_MIMPID = 0xF13;
constexpr CSRAddress CSR_MHARTID = 0xF14;
constexpr CSRAddress CSR_MSTATUS = 0x300;
constexpr CSRAddress CSR_MISA = 0x301;
constexpr CSRAddress CSR_MEDELEG = 0x302;
constexpr CSRAddress CSR_MIDELEG = 0x303;
constexpr CSRAddress CSR_MIE = 0x304;
constexpr CSRAddress CSR_MTVEC = 0x305;
constexpr CSRAddress CSR_MCOUNTEREN = 0x306;
constexpr CSRAddress CSR_MSCRATCH = 0x340;
constexpr CSRAddress CSR_MEPC = 0x341;
constexpr CSRAddress CSR_MCAUSE = 0x342;
constexpr CSRAddress CSR_MTVAL = 0x343;
constexpr CSRAddress CSR_MIP = 0x344;
constexpr CSRAddress CSR_MCYCLE = 0xB00;
constexpr CSRAddress CSR_MINSTRET = 0xB02;
constexpr CSRAddress CSR_MCYCLEH = 0xB80;
constexpr CSRAddress CSR_MINSTRETH = 0xB82;
constexpr CSRAddress CSR_CYCLEH = 0xC80;
constexpr CSRAddress CSR_TIMEH = 0xC81;
constexpr CSRAddress CSR_INSTRETH = 0xC82;

enum class Csr : CSRAddress {
    Ustatus = CSR_USTATUS,
    Uie = CSR_UIE,
    Utvec = CSR_UTVEC,
    Uscratch = CSR_USCRATCH,
    Uepc = CSR_UEPC,
    Ucause = CSR_UCAUSE,
    Utval = CSR_UTVAL,
    Uip = CSR_UIP,
    Fflags = CSR_FFLAGS,
    Frm = CSR_FRM,
    Fcsr = CSR_FCSR,
    Cycle = CSR_CYCLE,
    Time = CSR_TIME,
    Instret = CSR_INSTRET,
    Sstatus = CSR_SSTATUS,
    Sedeleg = CSR_SEDELEG,
    Sideleg = CSR_SIDELEG,
    Sie = CSR_SIE,
    Stvec = CSR_STVEC,
    Scounteren = CSR_SCOUNTEREN,
    Sscratch = CSR_SSCRATCH,
    Sepc = CSR_SEPC,
    Scause = CSR_SCAUSE,
    Stval = CSR_STVAL,
    Sip = CSR_SIP,
    Satp = CSR_SATP,
    Mvendorid = CSR_MVENDORID,
    Marchid = CSR_MARCHID,
    Mimpid = CSR_MIMPID,
    Mhartid = CSR_MHARTID,
    Mstatus = CSR_MSTATUS,
    Misa = CSR_MISA,
    Medeleg = CSR_MEDELEG,
    Mideleg = CSR_MIDELEG,
    Mie = CSR_MIE,
    Mtvec = CSR_MTVEC,
    Mcounteren = CSR_MCOUNTEREN,
    Mscratch = CSR_MSCRATCH,
    Mepc = CSR_MEPC,
    Mcause = CSR_MCAUSE,
    Mtval = CSR_MTVAL,
    Mip = CSR_MIP,
    Mcycle = CSR_MCYCLE,
    Minstret = CSR_MINSTRET,
    Mcycleh = CSR_MCYCLEH,
    Minstreth = CSR_MINSTRETH,
    Cycleh = CSR_CYCLEH,
    Timeh = CSR_TIMEH,
    Instreth = CSR_INSTRETH,
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

constexpr size_t XLEN = 32;
constexpr size_t MLEN = 32;
constexpr size_t FLEN = 64;
constexpr Instruction RV32_NOP = 0x00000013;

constexpr Instruction OPCODE_C0 = 0x0;
constexpr Instruction OPCODE_C1 = 0x1;
constexpr Instruction OPCODE_C2 = 0x2;
constexpr Instruction OPCODE_W = 0x3;

constexpr Instruction OPCODE_OP = 0x33;
constexpr Instruction OPCODE_OP_FP = 0x53;
constexpr Instruction OPCODE_AMO = 0x2F;
constexpr Instruction OPCODE_OP_IMM = 0x13;
constexpr Instruction OPCODE_LOAD = 0x03;
constexpr Instruction OPCODE_LOAD_FP = 0x07;
constexpr Instruction OPCODE_JALR = 0x67;
constexpr Instruction OPCODE_STORE = 0x23;
constexpr Instruction OPCODE_STORE_FP = 0x27;
constexpr Instruction OPCODE_BRANCH = 0x63;
constexpr Instruction OPCODE_LUI = 0x37;
constexpr Instruction OPCODE_AUIPC = 0x17;
constexpr Instruction OPCODE_JAL = 0x6F;
constexpr Instruction OPCODE_MADD = 0x43;
constexpr Instruction OPCODE_MSUB = 0x47;
constexpr Instruction OPCODE_NMADD = 0x4F;
constexpr Instruction OPCODE_NMSUB = 0x4B;
constexpr Instruction OPCODE_MISC_M = 0x0F;
constexpr Instruction OPCODE_SYSTEM = 0x73;

constexpr Instruction FUNCT3_ADD = 0x0;
constexpr Instruction FUNCT3_SLL = 0x1;
constexpr Instruction FUNCT3_SLT = 0x2;
constexpr Instruction FUNCT3_SLTU = 0x3;
constexpr Instruction FUNCT3_XOR = 0x4;
constexpr Instruction FUNCT3_SRL = 0x5;
constexpr Instruction FUNCT3_OR = 0x6;
constexpr Instruction FUNCT3_AND = 0x7;

constexpr Instruction FUNCT3_MUL = 0x0;
constexpr Instruction FUNCT3_MULH = 0x1;
constexpr Instruction FUNCT3_MULHSU = 0x2;
constexpr Instruction FUNCT3_MULHU = 0x3;
constexpr Instruction FUNCT3_DIV = 0x4;
constexpr Instruction FUNCT3_DIVU = 0x5;
constexpr Instruction FUNCT3_REM = 0x6;
constexpr Instruction FUNCT3_REMU = 0x7;

constexpr Instruction FUNCT3_SB = 0x0;
constexpr Instruction FUNCT3_SH = 0x1;
constexpr Instruction FUNCT3_SW = 0x2;
constexpr Instruction FUNCT3_SD = 0x3;
constexpr Instruction FUNCT3_FSW = 0x2;
constexpr Instruction FUNCT3_FSD = 0x3;

constexpr Instruction FUNCT3_LB = 0x0;
constexpr Instruction FUNCT3_LH = 0x1;
constexpr Instruction FUNCT3_LW = 0x2;
constexpr Instruction FUNCT3_LD = 0x3;
constexpr Instruction FUNCT3_LBU = 0x4;
constexpr Instruction FUNCT3_LHU = 0x5;
constexpr Instruction FUNCT3_LWU = 0x6;
constexpr Instruction FUNCT3_FLW = 0x2;
constexpr Instruction FUNCT3_FLD = 0x3;

constexpr Instruction FUNCT3_BEQ = 0x0;
constexpr Instruction FUNCT3_BNE = 0x1;
constexpr Instruction FUNCT3_BLT = 0x4;
constexpr Instruction FUNCT3_BGE = 0x5;
constexpr Instruction FUNCT3_BLTU = 0x6;
constexpr Instruction FUNCT3_BGEU = 0x7;

constexpr Instruction FUNCT3_FENCE = 0x0;
constexpr Instruction FUNCT3_FENCE_I = 0x1;

constexpr Instruction FUNCT3_PRIV = 0x0;
constexpr Instruction FUNCT3_CSRRW = 0x1;
constexpr Instruction FUNCT3_CSRRS = 0x2;
constexpr Instruction FUNCT3_CSRRC = 0x3;
constexpr Instruction FUNCT3_CSRRWI = 0x5;
constexpr Instruction FUNCT3_CSRRSI = 0x6;
constexpr Instruction FUNCT3_CSRRCI = 0x7;

constexpr Instruction FUNCT12_ECALL = 0x000;
constexpr Instruction FUNCT12_EBREAK = 0x001;
constexpr Instruction FUNCT12_ERET = 0x100;
constexpr Instruction FUNCT12_MRET = 0x302;
constexpr Instruction FUNCT12_SRET = 0x102;
constexpr Instruction FUNCT12_URET = 0x002;
constexpr Instruction FUNCT12_WFI = 0x105;
constexpr Instruction FUNCT7_SFENCE_VMA = 0x09;

constexpr Instruction FUNCT5_AMO_LR = 0x02;
constexpr Instruction FUNCT5_AMO_SC = 0x03;
constexpr Instruction FUNCT5_AMO_SWAP = 0x01;
constexpr Instruction FUNCT5_AMO_ADD = 0x00;
constexpr Instruction FUNCT5_AMO_AND = 0x0c;
constexpr Instruction FUNCT5_AMO_OR = 0x08;
constexpr Instruction FUNCT5_AMO_XOR = 0x04;
constexpr Instruction FUNCT5_AMO_MIN = 0x10;
constexpr Instruction FUNCT5_AMO_MINU = 0x18;
constexpr Instruction FUNCT5_AMO_MAX = 0x14;
constexpr Instruction FUNCT5_AMO_MAXU = 0x1c;

constexpr Instruction AMO_FAILURE_CODE = 1;
constexpr Instruction AMO_SUCCESS_CODE = 0;

enum class Opcode : Instruction {
    C0 = OPCODE_C0,
    C1 = OPCODE_C1,
    C2 = OPCODE_C2,
    W = OPCODE_W,
    Op = OPCODE_OP,
    OpFp = OPCODE_OP_FP,
    Amo = OPCODE_AMO,
    OpImm = OPCODE_OP_IMM,
    Load = OPCODE_LOAD,
    LoadFp = OPCODE_LOAD_FP,
    Jalr = OPCODE_JALR,
    Store = OPCODE_STORE,
    StoreFp = OPCODE_STORE_FP,
    Branch = OPCODE_BRANCH,
    MAdd = OPCODE_MADD,
    MSub = OPCODE_MSUB,
    NMSub = OPCODE_NMSUB,
    NMAdd = OPCODE_NMADD,
    Lui = OPCODE_LUI,
    Auipc = OPCODE_AUIPC,
    Jal = OPCODE_JAL,
    MiscMem = OPCODE_MISC_M,
    System = OPCODE_SYSTEM,
};

enum class Funct3 : Instruction {
    Add = FUNCT3_ADD,
    Sll = FUNCT3_SLL,
    Slt = FUNCT3_SLT,
    Sltu = FUNCT3_SLTU,
    Xor = FUNCT3_XOR,
    Srl = FUNCT3_SRL,
    Or = FUNCT3_OR,
    And = FUNCT3_AND,
    Mul = FUNCT3_MUL,
    Mulh = FUNCT3_MULH,
    Mulhsu = FUNCT3_MULHSU,
    Mulhu = FUNCT3_MULHU,
    Div = FUNCT3_DIV,
    Divu = FUNCT3_DIVU,
    Rem = FUNCT3_REM,
    Remu = FUNCT3_REMU,
    Sb = FUNCT3_SB,
    Sh = FUNCT3_SH,
    Sw = FUNCT3_SW,
    Sd = FUNCT3_SD,
    Lb = FUNCT3_LB,
    Lh = FUNCT3_LH,
    Lw = FUNCT3_LW,
    Ld = FUNCT3_LD,
    Lbu = FUNCT3_LBU,
    Lhu = FUNCT3_LHU,
    Flw = FUNCT3_FLW,
    Fld = FUNCT3_FLD,
    Fsw = FUNCT3_FSW,
    Fsd = FUNCT3_FSD,
    Beq = FUNCT3_BEQ,
    Bne = FUNCT3_BNE,
    Blt = FUNCT3_BLT,
    Bge = FUNCT3_BGE,
    Bltu = FUNCT3_BLTU,
    Bgeu = FUNCT3_BGEU,
    Fence = FUNCT3_FENCE,
    FenceI = FUNCT3_FENCE_I,
    Priv = FUNCT3_PRIV,
    Csrrw = FUNCT3_CSRRW,
    Csrrs = FUNCT3_CSRRS,
    Csrrc = FUNCT3_CSRRC,
    Csrrwi = FUNCT3_CSRRWI,
    Csrrsi = FUNCT3_CSRRSI,
    Csrrci = FUNCT3_CSRRCI,
};

enum class Funct12Priv : Instruction {
    Ecall = FUNCT12_ECALL,
    Ebreak = FUNCT12_EBREAK,
    Uret = FUNCT12_URET,
    Sret = FUNCT12_SRET,
    Mret = FUNCT12_MRET,
    Wfi = FUNCT12_WFI,
};

enum class Funct7Priv : Instruction {
    SfenceVma = FUNCT7_SFENCE_VMA,
};

enum class Funct5Amo : Instruction {
    Lr = FUNCT5_AMO_LR,
    Sc = FUNCT5_AMO_SC,
    Swap = FUNCT5_AMO_SWAP,
    Add = FUNCT5_AMO_ADD,
    And = FUNCT5_AMO_AND,
    Or = FUNCT5_AMO_OR,
    Xor = FUNCT5_AMO_XOR,
    Min = FUNCT5_AMO_MIN,
    Minu = FUNCT5_AMO_MINU,
    Max = FUNCT5_AMO_MAX,
    Maxu = FUNCT5_AMO_MAXU,
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

constexpr CSRValue misa_extension_bit(IsaExtension ext) {
    return static_cast<CSRValue>(CSRValue{1} << static_cast<unsigned>(ext));
}

constexpr CSRValue kMisaRv32i = misa_extension_bit(IsaExtension::I);
constexpr CSRValue kMisaRv32imac =
    misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M) |
    misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::C);
constexpr CSRValue kMisaRv32gc =
    misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M) |
    misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::F) |
    misa_extension_bit(IsaExtension::D) | misa_extension_bit(IsaExtension::C) |
    misa_extension_bit(IsaExtension::S) | misa_extension_bit(IsaExtension::U);

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
