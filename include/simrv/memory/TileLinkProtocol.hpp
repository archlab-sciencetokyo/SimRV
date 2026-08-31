/**
 * @file TileLinkProtocol.hpp
 * @brief Typed TileLink-C 1.8.1 message and permission definitions.
 */
#pragma once

#include <cstdint>

#include "simrv/xlen/Types.hpp"

namespace simrv::memory {

using TlData = uint64_t;
using TlMask = uint8_t;
using TlSourceId = uint16_t;
using TlSinkId = uint16_t;
using LineAddress = Address;
using CacheTag = Address;
using CacheSetIndex = uint32_t;
using CacheOffset = uint32_t;
using CoherenceSharersMask = uint64_t;

inline constexpr uint8_t kTlBeatBytes = 8;
inline constexpr uint8_t kTlBeatSize = 3;
inline constexpr uint8_t kTlBlockBytes = 32;
inline constexpr uint8_t kTlBlockSize = 5;

enum class TlPermission : uint8_t { None, Branch, Trunk };
enum class MesiState : uint8_t { Invalid, Shared, Exclusive, Modified };

enum class TlGrow : uint8_t { NtoB = 0, NtoT = 1, BtoT = 2 };
enum class TlCap : uint8_t { ToT = 0, ToB = 1, ToN = 2 };
enum class TlReport : uint8_t {
    TtoT = 0,
    TtoB = 1,
    TtoN = 2,
    BtoB = 3,
    BtoN = 4,
    NtoN = 5,
};
enum class TlArithmetic : uint8_t { Min = 0, Max = 1, MinU = 2, MaxU = 3, Add = 4 };
enum class TlLogical : uint8_t { Xor = 0, Or = 1, And = 2, Swap = 3 };
enum class TlIntent : uint8_t { PrefetchRead = 0, PrefetchWrite = 1 };

enum class TlOpcodeA : uint8_t {
    PutFullData = 0,
    PutPartialData = 1,
    ArithmeticData = 2,
    LogicalData = 3,
    Get = 4,
    Intent = 5,
    AcquireBlock = 6,
    AcquirePerm = 7,
};
enum class TlOpcodeB : uint8_t { ProbeBlock = 6, ProbePerm = 7 };
enum class TlOpcodeC : uint8_t {
    ProbeAck = 4,
    ProbeAckData = 5,
    Release = 6,
    ReleaseData = 7,
};
enum class TlOpcodeD : uint8_t {
    AccessAck = 0,
    AccessAckData = 1,
    HintAck = 2,
    Grant = 4,
    GrantData = 5,
    ReleaseAck = 6,
};
enum class TlOpcodeE : uint8_t { GrantAck = 0 };
enum class TlPort : uint8_t { Data = 0, Instruction = 1 };

struct TlManagerCapabilities {
    bool acquire_block = false;
    bool acquire_perm = false;
    bool get = false;
    bool put_full = false;
    bool put_partial = false;
    bool logical_or = false;
};

inline constexpr TlManagerCapabilities kCoherentRamCapabilities{
    .acquire_block = true,
    .acquire_perm = true,
    .get = true,
    .put_full = true,
    .put_partial = true,
    .logical_or = true,
};
inline constexpr TlManagerCapabilities kMmioCapabilities{
    .get = true,
    .put_full = true,
    .put_partial = true,
};

[[nodiscard]] constexpr auto permission_for(MesiState state) -> TlPermission {
    switch (state) {
        case MesiState::Invalid:
            return TlPermission::None;
        case MesiState::Shared:
            return TlPermission::Branch;
        case MesiState::Exclusive:
        case MesiState::Modified:
            return TlPermission::Trunk;
    }
    return TlPermission::None;
}

[[nodiscard]] constexpr auto mesi_for(TlCap cap, bool dirty = false) -> MesiState {
    switch (cap) {
        case TlCap::ToT:
            return dirty ? MesiState::Modified : MesiState::Exclusive;
        case TlCap::ToB:
            return MesiState::Shared;
        case TlCap::ToN:
            return MesiState::Invalid;
    }
    return MesiState::Invalid;
}

[[nodiscard]] constexpr auto report_for(MesiState from, TlCap cap) -> TlReport {
    const auto permission = permission_for(from);
    if (permission == TlPermission::Trunk) {
        if (cap == TlCap::ToT) return TlReport::TtoT;
        if (cap == TlCap::ToB) return TlReport::TtoB;
        return TlReport::TtoN;
    }
    if (permission == TlPermission::Branch) {
        return cap == TlCap::ToN ? TlReport::BtoN : TlReport::BtoB;
    }
    return TlReport::NtoN;
}

[[nodiscard]] constexpr auto make_tl_source(HartId hart, TlPort port) -> TlSourceId {
    return static_cast<TlSourceId>((static_cast<TlSourceId>(hart.val) << 1u) |
                                   static_cast<TlSourceId>(port));
}

struct TlChannelA {
    TlOpcodeA opcode{TlOpcodeA::Get};
    TlGrow grow{TlGrow::NtoB};
    TlArithmetic arithmetic{TlArithmetic::Add};
    TlLogical logical{TlLogical::Or};
    TlIntent intent{TlIntent::PrefetchRead};
    uint8_t size{0};
    TlSourceId source{0};
    HartId hart{0};
    PhysicalAddress address{0};
    TlMask mask{0};
    TlData data{0};
    bool corrupt{false};

    [[nodiscard]] static constexpr auto compute_mask(uint8_t transfer_size, PhysicalAddress address)
        -> TlMask {
        if (transfer_size > kTlBeatSize) return 0;
        const auto bytes = static_cast<unsigned>(1u << transfer_size);
        const auto lane = static_cast<unsigned>(address.raw() & (kTlBeatBytes - 1u));
        if (lane + bytes > kTlBeatBytes) return 0;
        const auto lanes = static_cast<unsigned>((1u << bytes) - 1u);
        return static_cast<TlMask>(lanes << lane);
    }

    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool {
        return size <= kTlBlockSize && (opcode != TlOpcodeA::PutFullData || mask != 0);
    }
};

struct TlChannelB {
    TlOpcodeB opcode{TlOpcodeB::ProbeBlock};
    TlCap cap{TlCap::ToN};
    uint8_t size{kTlBlockSize};
    TlSourceId source{0};
    PhysicalAddress address{0};
};

struct TlChannelC {
    TlOpcodeC opcode{TlOpcodeC::ProbeAck};
    TlReport report{TlReport::NtoN};
    uint8_t size{kTlBlockSize};
    TlSourceId source{0};
    HartId hart{0};
    PhysicalAddress address{0};
    TlData data{0};
    bool corrupt{false};

    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool { return size <= kTlBlockSize; }
};

struct TlChannelD {
    TlOpcodeD opcode{TlOpcodeD::AccessAckData};
    TlCap cap{TlCap::ToN};
    uint8_t size{0};
    TlSourceId source{0};
    TlSinkId sink{0};
    TlData data{0};
    bool denied{false};
    bool corrupt{false};

    [[nodiscard]] constexpr auto failed() const noexcept -> bool { return denied || corrupt; }
};

struct TlChannelE {
    TlOpcodeE opcode{TlOpcodeE::GrantAck};
    TlSinkId sink{0};
};

}  // namespace simrv::memory
