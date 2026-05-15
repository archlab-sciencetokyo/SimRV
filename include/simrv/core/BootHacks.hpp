/**
 * @file BootHacks.hpp
 * @brief Encapsulation of legacy bootloader and environment compat hacks.
 */
#pragma once

#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class Machine;
struct ArchState;
}  // namespace simrv::core

namespace simrv::boot {

inline constexpr Address kStartPc = 0x80000000U;
inline constexpr Address kInitDataAddress = 0x82000000U;

/**
 * @brief Intercept and patch legacy RV32 BBL root page table setups.
 *
 * Older BBL payloads for RV32 incorrectly write virtual addresses into the
 * satp PPN field and rely on the hardware MMU forgiving invalid mappings.
 * This strictly isolates those hacks from the standard MMU logic.
 *
 * @param machine The active simulator machine
 * @param satp The newly written satp value (passed by reference to correct it)
 */
void handle_legacy_bbl_satp_hacks(simrv::core::Machine& machine, Word& satp);

/**
 * @brief Normalize trap PC for legacy BBL kernels.
 *
 * BBL sometimes traps from physical addresses but expects the sepc/mepc to
 * reflect the mapped high-kernel virtual address.
 *
 * @param state Architectural state context
 * @return Normalized PC address for trap CSRs
 */
[[nodiscard]] auto normalize_legacy_trap_pc(const core::ArchState& state) -> Address;

}  // namespace simrv::boot