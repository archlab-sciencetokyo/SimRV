/**
 * @file Memory.hpp
 * @brief Memory layout, DRAM, TLB, and page table constants.
 */
#pragma once

#include "simrv/XLen.hpp"
#include "simrv/memory/MemoryUtil.hpp"

namespace simrv::memory {
inline constexpr size_t kLocalCoreMemorySize = (static_cast<size_t>(32u * 1024u));
}  // namespace simrv::memory
