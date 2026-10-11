/**
 * @file Format.hpp
 * @brief std::format support for XLEN semantic types.
 */
#pragma once

#include <format>

#include "simrv/xlen/Types.hpp"

template <>
struct std::formatter<simrv::xlen::PhysAddr> : std::formatter<simrv::xlen::Word> {
    auto format(simrv::xlen::PhysAddr addr, std::format_context& ctx) const {
        return std::formatter<simrv::xlen::Word>::format(addr.val, ctx);
    }
};

template <>
struct std::formatter<simrv::xlen::VirtAddr> : std::formatter<simrv::xlen::Word> {
    auto format(simrv::xlen::VirtAddr addr, std::format_context& ctx) const {
        return std::formatter<simrv::xlen::Word>::format(addr.val, ctx);
    }
};

template <>
struct std::formatter<simrv::xlen::HartId> : std::formatter<uint32_t> {
    auto format(simrv::xlen::HartId id, std::format_context& ctx) const {
        return std::formatter<uint32_t>::format(id.val, ctx);
    }
};

template <>
struct std::formatter<simrv::xlen::CsrNumber> : std::formatter<uint16_t> {
    auto format(simrv::xlen::CsrNumber csr, std::format_context& ctx) const {
        return std::formatter<uint16_t>::format(csr.val, ctx);
    }
};
