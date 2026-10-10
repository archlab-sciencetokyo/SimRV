/**
 * @file Pmp.hpp
 * @brief RISC-V Physical Memory Protection (PMP) address matching and validation.
 */
#pragma once

#include <bit>
#include <cstdint>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

enum class PmpAccessType : uint8_t {
    None = 0,
    Read = 1,
    Write = 2,
    Execute = 4,
    ReadWrite = Read | Write,
    ReadExecute = Read | Execute,
    All = Read | Write | Execute,
};

constexpr auto operator|(PmpAccessType lhs, PmpAccessType rhs) noexcept -> PmpAccessType {
    return static_cast<PmpAccessType>(std::to_underlying(lhs) | std::to_underlying(rhs));
}

constexpr auto operator&(PmpAccessType lhs, PmpAccessType rhs) noexcept -> PmpAccessType {
    return static_cast<PmpAccessType>(std::to_underlying(lhs) & std::to_underlying(rhs));
}

constexpr auto operator~(PmpAccessType access) noexcept -> PmpAccessType {
    return static_cast<PmpAccessType>(~std::to_underlying(access) & 0x7u);
}

constexpr auto operator|=(PmpAccessType& lhs, PmpAccessType rhs) noexcept -> PmpAccessType& {
    lhs = lhs | rhs;
    return lhs;
}

constexpr auto operator&=(PmpAccessType& lhs, PmpAccessType rhs) noexcept -> PmpAccessType& {
    lhs = lhs & rhs;
    return lhs;
}

[[nodiscard]] constexpr auto has_access(PmpAccessType available, PmpAccessType requested) noexcept
    -> bool {
    return (std::to_underlying(available) & std::to_underlying(requested)) ==
           std::to_underlying(requested);
}

namespace pmp {

using PmpAccessType = core::PmpAccessType;

constexpr uint8_t kPmpR = 0x01;
constexpr uint8_t kPmpW = 0x02;
constexpr uint8_t kPmpX = 0x04;
constexpr uint8_t kPmpA = 0x18;
constexpr uint8_t kPmpL = 0x80;

constexpr uint8_t kPmpModeOff = 0x00;
constexpr uint8_t kPmpModeTor = 0x08;
constexpr uint8_t kPmpModeNa4 = 0x10;
constexpr uint8_t kPmpModeNapot = 0x18;

/**
 * @struct PmpRange
 * @brief 64-bit address interval representing a matching PMP entry boundary.
 *
 * Prevents 32-bit wrap-around when RV32 platforms check entries spanning high memory.
 */
struct PmpRange {
    uint64_t base = 0;
    uint64_t limit = 0;
    bool full_range = false;

    [[nodiscard]] constexpr auto contains(uint64_t req_start, size_t size) const noexcept -> bool {
        if (full_range) return true;
        const uint64_t req_end = req_start + size;
        return req_start >= base && req_end <= limit && limit > base;
    }

    [[nodiscard]] constexpr auto overlaps(uint64_t req_start, size_t size) const noexcept -> bool {
        if (full_range) return true;
        const uint64_t req_end = req_start + size;
        return req_start < limit && req_end > base && limit > base;
    }

    [[nodiscard]] constexpr auto partial_overlap(uint64_t req_start, size_t size) const noexcept
        -> bool {
        return overlaps(req_start, size) && !contains(req_start, size);
    }
};

/**
 * @struct PmpConfigEntry
 * @brief Structured view over a single 8-bit PMP configuration register (pmpcfg).
 */
struct PmpConfigEntry {
    uint8_t raw = 0;

    [[nodiscard]] constexpr auto mode() const noexcept -> PmpMode {
        return static_cast<PmpMode>(raw & kPmpA);
    }
    [[nodiscard]] constexpr auto is_locked() const noexcept -> bool {
        return (raw & PmpPermission::Lock) != 0;
    }
    [[nodiscard]] constexpr auto readable() const noexcept -> bool {
        return (raw & PmpPermission::Read) != 0;
    }
    [[nodiscard]] constexpr auto writable() const noexcept -> bool {
        return (raw & PmpPermission::Write) != 0;
    }
    [[nodiscard]] constexpr auto executable() const noexcept -> bool {
        return (raw & PmpPermission::Execute) != 0;
    }
    [[nodiscard]] constexpr auto permissions() const noexcept -> PmpAccessType {
        return static_cast<PmpAccessType>(raw & 0x7u);
    }
    [[nodiscard]] constexpr auto allows(PmpAccessType access) const noexcept -> bool {
        return has_access(permissions(), access);
    }
};

using PmpConfigView = PmpConfigEntry;

[[nodiscard]] inline auto calculate_pmp_range(PmpConfigEntry cfg, Address pmpaddr_curr,
                                              Address pmpaddr_prev) noexcept -> PmpRange {
    const PmpMode mode = cfg.mode();
    PmpRange range{};
    if (mode == PmpMode::Tor) {
        range.base = static_cast<uint64_t>(pmpaddr_prev) << 2;
        range.limit = static_cast<uint64_t>(pmpaddr_curr) << 2;
    } else if (mode == PmpMode::Na4) {
        range.base = static_cast<uint64_t>(pmpaddr_curr) << 2;
        range.limit = range.base + 4;
    } else if (mode == PmpMode::Napot) {
        const auto raw = static_cast<uint64_t>(pmpaddr_curr);
        const int t = std::countr_one(raw);
        constexpr unsigned kAddrBits = sizeof(Address) * 8;
        if (t + 3 >= static_cast<int>(kAddrBits)) {
            range.base = 0;
            range.full_range = true;
        } else {
            const uint64_t mask = (uint64_t{1} << t) - 1;
            range.base = (raw & ~mask) << 2;
            range.limit = range.base + (uint64_t{1} << (t + 3));
        }
    }
    return range;
}

inline auto check_access(const ArchState& state, Address paddr, size_t size, PmpAccessType access,
                         std::optional<PrivilegeLevel> priv_override = std::nullopt) -> bool {
    PrivilegeLevel effective_priv = priv_override.value_or(state.priv);
    // MPRV is effective only when executing in Machine mode per RISC-V Privileged Architecture
    if (!priv_override.has_value() && access != PmpAccessType::Execute &&
        state.priv == PrivilegeLevel::Machine && (state.mstatus & MstatusBit::Mprv) != 0) {
        effective_priv = mstatus_mpp(state.mstatus);
    }

    if (simrv::compiler::likely(state.num_active_pmp == 0)) {
        return true;
    }
    if (effective_priv == PrivilegeLevel::Machine && !state.has_locked_pmp) {
        return true;
    }

    const uint64_t req_start = static_cast<uint64_t>(paddr);

    for (size_t i = 0; i < state.num_active_pmp; ++i) {
        const PmpConfigEntry cfg{state.pmpcfg[i]};
        const PmpMode mode = cfg.mode();
        if (mode == PmpMode::Off) {
            continue;
        }

        const Address prev_addr = (i == 0) ? 0 : state.pmpaddr[i - 1];
        const PmpRange range = calculate_pmp_range(cfg, state.pmpaddr[i], prev_addr);

        if (mode == PmpMode::Tor && range.limit <= range.base) {
            continue;
        }

        // Partial overlap that is not fully contained within [base, limit] fails per RISC-V spec
        if (range.partial_overlap(req_start, size)) {
            return false;
        }

        if (range.contains(req_start, size)) {
            if (effective_priv == PrivilegeLevel::Machine && !cfg.is_locked()) {
                return true;
            }
            return cfg.allows(access);
        }
    }

    return effective_priv == PrivilegeLevel::Machine;
}

inline auto check_access(const ArchState& state, PhysAddr paddr, size_t size, PmpAccessType access,
                         std::optional<PrivilegeLevel> priv_override = std::nullopt) -> bool {
    return check_access(state, paddr.raw(), size, access, priv_override);
}

}  // namespace pmp

}  // namespace simrv::core
