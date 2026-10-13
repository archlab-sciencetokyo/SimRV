/**
 * @file Helpers.hpp
 * @brief XLEN masks and runtime architectural display helpers.
 */
#pragma once

#include <string>

#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::xlen {

inline constexpr unsigned kFetchSize = (kIsXLen64 ? 8u : 4u);
inline constexpr Word kAddrMask = (kIsXLen64 ? UINT64_MAX : UINT32_MAX);
inline constexpr Address maskAddress(Address address) noexcept { return address & kAddrMask; }

auto resolve_misa_string(CsrValue misa) -> std::string;
auto resolve_cause_string(uint64_t cause) -> std::string;
auto resolve_mstatus_short_string(CsrValue mstatus) -> std::string;
auto resolve_satp_string(CsrValue satp, unsigned xlen = kXLenBits) -> std::string;

}  // namespace simrv::xlen
