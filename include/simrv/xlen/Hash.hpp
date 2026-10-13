/**
 * @file Hash.hpp
 * @brief Hash support for XLEN semantic wrapper types.
 */
#pragma once

#include <functional>

#include "simrv/xlen/Types.hpp"

namespace std {

template <>
struct hash<simrv::xlen::PhysAddr> {
    constexpr auto operator()(simrv::xlen::PhysAddr addr) const noexcept -> size_t {
        return hash<simrv::xlen::Word>{}(addr.val);
    }
};

template <>
struct hash<simrv::xlen::VirtAddr> {
    constexpr auto operator()(simrv::xlen::VirtAddr addr) const noexcept -> size_t {
        return hash<simrv::xlen::Word>{}(addr.val);
    }
};

template <>
struct hash<simrv::xlen::HartId> {
    constexpr auto operator()(simrv::xlen::HartId id) const noexcept -> size_t {
        return hash<uint32_t>{}(id.val);
    }
};

template <>
struct hash<simrv::xlen::CsrNumber> {
    constexpr auto operator()(simrv::xlen::CsrNumber csr) const noexcept -> size_t {
        return hash<uint16_t>{}(csr.val);
    }
};

}  // namespace std
