/**
 * @file Format.hpp
 * @brief std::format support for XLEN semantic types.
 */
#pragma once

#include <format>

#include "simrv/xlen/Types.hpp"

template <>
struct std::formatter<PhysAddr> : std::formatter<Word> {
    auto format(PhysAddr addr, std::format_context& ctx) const {
        return std::formatter<Word>::format(addr.val, ctx);
    }
};

template <>
struct std::formatter<VirtAddr> : std::formatter<Word> {
    auto format(VirtAddr addr, std::format_context& ctx) const {
        return std::formatter<Word>::format(addr.val, ctx);
    }
};

template <>
struct std::formatter<HartId> : std::formatter<uint32_t> {
    auto format(HartId id, std::format_context& ctx) const {
        return std::formatter<uint32_t>::format(id.val, ctx);
    }
};

template <>
struct std::formatter<CsrNumber> : std::formatter<uint16_t> {
    auto format(CsrNumber csr, std::format_context& ctx) const {
        return std::formatter<uint16_t>::format(csr.val, ctx);
    }
};
