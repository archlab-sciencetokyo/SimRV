/**
 * @file RegisterNames.hpp
 * @brief Canonical RISC-V architectural and ABI register name definitions.
 */
#pragma once

#include <array>
#include <string_view>
#include <utility>

#include "simrv/xlen/RegisterTypes.hpp"

namespace simrv::isa {

/// Standard RISC-V GPR ABI register names (using canonical "s0").
inline constexpr std::array<const char*, 32> kGprAbiNames = {
    "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
    "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
    "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

/// Pedagogical GPR ABI register names displaying "s0/fp" for educational and inspection clarity.
inline constexpr std::array<const char*, 32> kGprPedagogicalNames = {
    "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0/fp", "s1", "a0",
    "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3",    "s4", "s5",
    "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5",    "t6"};

/// Standard RISC-V FPR ABI register names.
inline constexpr std::array<const char*, 32> kFprAbiNames = {
    "ft0", "ft1", "ft2", "ft3", "ft4",  "ft5",  "ft6", "ft7", "fs0",  "fs1", "fa0",
    "fa1", "fa2", "fa3", "fa4", "fa5",  "fa6",  "fa7", "fs2", "fs3",  "fs4", "fs5",
    "fs6", "fs7", "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"};

[[nodiscard]] constexpr auto gpr_abi_name(xlen::RegId reg) noexcept -> std::string_view {
    const auto idx = std::to_underlying(reg);
    return (idx < kGprAbiNames.size()) ? kGprAbiNames[idx] : "unknown";
}

[[nodiscard]] constexpr auto fpr_abi_name(xlen::FpRegId reg) noexcept -> std::string_view {
    const auto idx = std::to_underlying(reg);
    return (idx < kFprAbiNames.size()) ? kFprAbiNames[idx] : "unknown";
}

}  // namespace simrv::isa
