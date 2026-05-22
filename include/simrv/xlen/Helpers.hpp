#pragma once

#include "simrv/xlen/Types.hpp"
#include "simrv/Define.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryAccess.hpp"

namespace simrv::xlen {

// Fetch size in bytes based on XLEN
inline constexpr unsigned kFetchSize = (kIsXLen64 ? 8u : 4u);

// Address mask respecting XLEN width
inline constexpr Word kAddrMask = (kIsXLen64 ? UINT64_MAX : UINT32_MAX);
inline constexpr Address maskAddress(Address a) noexcept { return a & kAddrMask; }

} // namespace simrv::xlen
