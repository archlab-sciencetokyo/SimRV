/**
 * @file CsrTypes.hpp
 * @brief CSR register enums, bit masks, and helper functions.
 */
#pragma once

#include "simrv/xlen/Types.hpp"
#include "simrv/xlen/Constants.hpp"

namespace simrv::core {

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
    Vs = (3u << 9),
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

constexpr CSRValue kMstatusMask =
    (enum_mask(MstatusBit::Uie) | enum_mask(MstatusBit::Sie) | enum_mask(MstatusBit::Mie) |
     enum_mask(MstatusBit::Upie) | enum_mask(MstatusBit::Spie) | enum_mask(MstatusBit::Mpie) |
     enum_mask(MstatusBit::Spp) | enum_mask(MstatusBit::Mpp) | enum_mask(MstatusBit::Fs) |
     enum_mask(MstatusBit::Vs) |
     enum_mask(MstatusBit::Mprv) | enum_mask(MstatusBit::Sum) | enum_mask(MstatusBit::Mxr) |
     enum_mask(MstatusBit::Tvm) | enum_mask(MstatusBit::Tw) | enum_mask(MstatusBit::Tsr) |
     (simrv::xlen::kIsXLen64 ? (static_cast<CSRValue>(0xF) << 32) : 0));
constexpr CSRValue kSstatusMask =
    (enum_mask(MstatusBit::Uie) | enum_mask(MstatusBit::Sie) | enum_mask(MstatusBit::Upie) |
     enum_mask(MstatusBit::Spie) | enum_mask(MstatusBit::Spp) | enum_mask(MstatusBit::Fs) |
     enum_mask(MstatusBit::Vs) |
     enum_mask(MstatusBit::Xs) | enum_mask(MstatusBit::Sum) | enum_mask(MstatusBit::Mxr) |
     (simrv::xlen::kIsXLen64 ? (static_cast<CSRValue>(0x3) << 32) : 0));
constexpr CSRValue kMstatusFsDirty = enum_mask(MstatusBit::Fs);
constexpr CSRValue kMstatusSd = static_cast<CSRValue>(Word{1} << (simrv::xlen::kXLenBits - 1u));
constexpr CSRValue kMstatusSstatusReadMask =
    static_cast<CSRValue>(0x000de133u) | enum_mask(MstatusBit::Vs) | kMstatusSd |
    (simrv::xlen::kIsXLen64 ? (static_cast<CSRValue>(0x3) << 32) : 0);
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
    Vstart = 0x008,
    Vxsat = 0x009,
    Vxrm = 0x00A,
    Vcsr = 0x00F,
    Pmpcfg0 = 0x3A0,
    Pmpaddr0 = 0x3B0,
    Cycle = 0xC00,
    Time = 0xC01,
    Instret = 0xC02,
    Vl = 0xC20,
    Vtype = 0xC21,
    Vlenb = 0xC22,
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
    Mconfigptr = 0xF15,
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

} // namespace simrv::core
