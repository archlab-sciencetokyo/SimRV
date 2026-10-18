/**
 * @file RegisterTypes.hpp
 * @brief Integer, floating-point, and vector architectural register identifiers.
 */
#pragma once

#include <concepts>
#include <cstdint>
#include <utility>

namespace simrv::xlen {

enum class RegId : uint8_t {
    Zero = 0,
    Ra = 1,
    Sp = 2,
    Gp = 3,
    Tp = 4,
    T0 = 5,
    T1 = 6,
    T2 = 7,
    S0 = 8,
    Fp = 8,
    S1 = 9,
    A0 = 10,
    A1 = 11,
    A2 = 12,
    A3 = 13,
    A4 = 14,
    A5 = 15,
    A6 = 16,
    A7 = 17,
    S2 = 18,
    S3 = 19,
    S4 = 20,
    S5 = 21,
    S6 = 22,
    S7 = 23,
    S8 = 24,
    S9 = 25,
    S10 = 26,
    S11 = 27,
    T3 = 28,
    T4 = 29,
    T5 = 30,
    T6 = 31,
    Discard = 32
};

enum class FpRegId : uint8_t {
    Ft0 = 0,
    Ft1 = 1,
    Ft2 = 2,
    Ft3 = 3,
    Ft4 = 4,
    Ft5 = 5,
    Ft6 = 6,
    Ft7 = 7,
    Fs0 = 8,
    Fs1 = 9,
    Fa0 = 10,
    Fa1 = 11,
    Fa2 = 12,
    Fa3 = 13,
    Fa4 = 14,
    Fa5 = 15,
    Fa6 = 16,
    Fa7 = 17,
    Fs2 = 18,
    Fs3 = 19,
    Fs4 = 20,
    Fs5 = 21,
    Fs6 = 22,
    Fs7 = 23,
    Fs8 = 24,
    Fs9 = 25,
    Fs10 = 26,
    Fs11 = 27,
    Ft8 = 28,
    Ft9 = 29,
    Ft10 = 30,
    Ft11 = 31
};

enum class VecRegId : uint8_t {
    V0 = 0,
    V1 = 1,
    V2 = 2,
    V3 = 3,
    V4 = 4,
    V5 = 5,
    V6 = 6,
    V7 = 7,
    V8 = 8,
    V9 = 9,
    V10 = 10,
    V11 = 11,
    V12 = 12,
    V13 = 13,
    V14 = 14,
    V15 = 15,
    V16 = 16,
    V17 = 17,
    V18 = 18,
    V19 = 19,
    V20 = 20,
    V21 = 21,
    V22 = 22,
    V23 = 23,
    V24 = 24,
    V25 = 25,
    V26 = 26,
    V27 = 27,
    V28 = 28,
    V29 = 29,
    V30 = 30,
    V31 = 31
};

/// Explicitly reinterpret an instruction's five-bit register index as an FPR index.
constexpr auto to_fp_reg_id(RegId id) noexcept -> FpRegId {
    return static_cast<FpRegId>(std::to_underlying(id) & 0x1Fu);
}

/// Explicitly reinterpret an instruction's five-bit register index as a vector register index.
constexpr auto to_vec_reg_id(RegId id) noexcept -> VecRegId {
    return static_cast<VecRegId>(std::to_underlying(id) & 0x1Fu);
}

constexpr auto to_fp_reg_id(FpRegId id) noexcept -> FpRegId { return id; }
constexpr auto to_vec_reg_id(VecRegId id) noexcept -> VecRegId { return id; }

template <std::integral Index>
constexpr auto to_fp_reg_id(Index index) noexcept -> FpRegId {
    return static_cast<FpRegId>(index);
}

template <std::integral Index>
constexpr auto to_vec_reg_id(Index index) noexcept -> VecRegId {
    return static_cast<VecRegId>(index);
}

template <typename T>
concept RegisterIdentifier =
    std::same_as<T, RegId> || std::same_as<T, FpRegId> || std::same_as<T, VecRegId>;

}  // namespace simrv::xlen
