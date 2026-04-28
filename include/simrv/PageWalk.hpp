/**
 * @file PageWalk.hpp
 * @brief Page walk and memory translation helpers for SV32 virtual memory.
 *
 * Provides centralized implementations of page table walking and related
 * address translation utilities used across the simulator.
 */
#pragma once

#include "Define.hpp"
#include "XLen.hpp"

// Forward declare CPU since this is used in signatures
class CPU;

namespace simrv::machine_detail {

/**
 * Perform SV32 page table walk to translate virtual to physical address.
 *
 * @param v_addr Virtual address to translate
 * @param p_addr Pointer to store translated physical address
 * @param access Type of access (Read, Write, Code)
 * @param cpu Pointer to CPU state for privilege/satp/mstatus checks
 * @param mmem Pointer to memory for page table access
 * @return true if page fault, false on success
 */
auto page_walk(Address v_addr, Address* p_addr, PteAccess access, CPU* cpu, Byte* mmem) -> bool;

}  // namespace simrv::machine_detail
