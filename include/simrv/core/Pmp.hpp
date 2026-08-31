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
    Read = 1,
    Write = 2,
    Execute = 4,
};

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
 * @struct PmpConfigEntry
 * @brief Structured view over a single 8-bit PMP configuration register (pmpcfg).
 */
struct PmpConfigEntry {
    uint8_t raw = 0;

    [[nodiscard]] constexpr auto mode() const noexcept -> PmpMode {
        return static_cast<PmpMode>(raw & kPmpA);
    }
    [[nodiscard]] constexpr auto is_locked() const noexcept -> bool { return (raw & kPmpL) != 0; }
    [[nodiscard]] constexpr auto readable() const noexcept -> bool { return (raw & kPmpR) != 0; }
    [[nodiscard]] constexpr auto writable() const noexcept -> bool { return (raw & kPmpW) != 0; }
    [[nodiscard]] constexpr auto executable() const noexcept -> bool { return (raw & kPmpX) != 0; }
    [[nodiscard]] constexpr auto allows(PmpAccessType access) const noexcept -> bool {
        return (raw & std::to_underlying(access)) != 0;
    }
};

inline auto check_access(const ArchState& state, Address paddr, size_t size, PmpAccessType access,
                         std::optional<PrivilegeLevel> priv_override = std::nullopt) -> bool {
    PrivilegeLevel effective_priv = priv_override.value_or(state.priv);
    if (!priv_override.has_value() && access != PmpAccessType::Execute &&
        (state.mstatus & enum_mask(MstatusBit::Mprv)) != 0) {
        effective_priv =
            static_cast<PrivilegeLevel>((state.mstatus & enum_mask(MstatusBit::Mpp)) >> 11);
    }

    if (simrv::compiler::likely(state.num_active_pmp == 0)) {
        return true;
    }
    if (effective_priv == PrivilegeLevel::Machine && !state.has_locked_pmp) {
        return true;
    }

    const Address req_start = paddr;
    const Address req_end = paddr + size;

    for (size_t i = 0; i < state.num_active_pmp; ++i) {
        const PmpConfigEntry cfg{state.pmpcfg[i]};
        const PmpMode mode = cfg.mode();
        if (mode == PmpMode::Off) {
            continue;
        }

        Address base = 0;
        Address limit = 0;
        bool full_range = false;

        if (mode == PmpMode::Tor) {
            base = (i == 0) ? 0 : (state.pmpaddr[i - 1] << 2);
            limit = state.pmpaddr[i] << 2;
            if (limit <= base) {
                continue;
            }
        } else if (mode == PmpMode::Na4) {
            base = state.pmpaddr[i] << 2;
            limit = base + 4;
        } else if (mode == PmpMode::Napot) {
            const auto raw = static_cast<uint64_t>(state.pmpaddr[i]);
            const int t = std::countr_one(raw);
            constexpr unsigned kAddrBits = sizeof(Address) * 8;
            if (t + 3 >= static_cast<int>(kAddrBits)) {
                base = 0;
                full_range = true;
            } else {
                const uint64_t mask = (uint64_t{1} << t) - 1;
                base = static_cast<Address>((raw & ~mask) << 2);
                limit = base + (static_cast<Address>(1) << (t + 3));
            }
        }

        bool matches = false;
        if (full_range) {
            matches = true;
        } else if (req_start < limit && req_end > base) {
            // Partial overlap that is not fully contained within [base, limit] fails per RISC-V
            // spec
            if (req_start < base || req_end > limit) {
                return false;
            }
            matches = true;
        }

        if (matches) {
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
