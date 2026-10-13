/**
 * @file Memory.hpp
 * @brief Memory layout, DRAM, TLB, and page table constants.
 */
#pragma once

#include <cstddef>

#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::memory {
inline constexpr size_t kLocalCoreMemorySize = (static_cast<size_t>(32u * 1024u));
}  // namespace simrv::memory
