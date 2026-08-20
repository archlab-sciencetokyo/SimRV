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

constexpr uint8_t kPmpR = 0x01;
constexpr uint8_t kPmpW = 0x02;
constexpr uint8_t kPmpX = 0x04;
constexpr uint8_t kPmpA = 0x18;
constexpr uint8_t kPmpL = 0x80;

constexpr uint8_t kPmpModeOff = 0x00;
constexpr uint8_t kPmpModeTor = 0x08;
constexpr uint8_t kPmpModeNa4 = 0x10;
constexpr uint8_t kPmpModeNapot = 0x18;

inline auto check_access(const ArchState& state, Address paddr, size_t size, PmpAccessType access)
    -> bool {
    PrivilegeLevel effective_priv = state.priv;
    if (access != PmpAccessType::Execute && (state.mstatus & enum_mask(MstatusBit::Mprv)) != 0) {
        effective_priv =
            static_cast<PrivilegeLevel>((state.mstatus & enum_mask(MstatusBit::Mpp)) >> 11);
    }

    bool has_any_active_pmp = false;

    for (size_t i = 0; i < ArchState::kNumPmpEntries; ++i) {
        const uint8_t cfg = state.pmpcfg[i];
        const uint8_t mode = cfg & kPmpA;
        if (mode == kPmpModeOff) {
            continue;
        }
        has_any_active_pmp = true;

        Address base = 0;
        Address limit = 0;

        if (mode == kPmpModeTor) {
            base = (i == 0) ? 0 : (state.pmpaddr[i - 1] << 2);
            limit = state.pmpaddr[i] << 2;
        } else if (mode == kPmpModeNa4) {
            base = state.pmpaddr[i] << 2;
            limit = base + 4;
        } else if (mode == kPmpModeNapot) {
            const auto raw = static_cast<uint64_t>(state.pmpaddr[i]);
            const int t = std::countr_one(raw);
            const uint64_t mask = (1ULL << t) - 1;
            base = static_cast<Address>((raw & ~mask) << 2);
            limit = base + (1ULL << (t + 3));
        }

        if (paddr >= base && (paddr + size) <= limit) {
            const bool is_locked = (cfg & kPmpL) != 0;
            if (effective_priv == PrivilegeLevel::Machine && !is_locked) {
                return true;
            }
            if (access == PmpAccessType::Read && (cfg & kPmpR) == 0) return false;
            if (access == PmpAccessType::Write && (cfg & kPmpW) == 0) return false;
            if (access == PmpAccessType::Execute && (cfg & kPmpX) == 0) return false;
            return true;
        }
    }

    if (effective_priv == PrivilegeLevel::Machine) {
        return true;
    }
    return !has_any_active_pmp;
}

}  // namespace pmp

}  // namespace simrv::core
