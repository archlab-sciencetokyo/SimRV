/**
 * @file XLen.hpp
 * @brief XLEN-dependent scalar aliases and helpers.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

template <size_t Bits>
struct XLenTypes;

template <>
struct XLenTypes<32> {
    using Word = uint32_t;
    using SignedWord = int32_t;
    using Register = uint32_t;
    using Byte = std::byte;
    using CompressedInstruction = uint16_t;
    using FloatingRegister = uint64_t;
    using Counter = uint64_t;
};

template <>
struct XLenTypes<64> {
    using Word = uint64_t;
    using SignedWord = int64_t;
    using Register = uint64_t;
    using Byte = std::byte;
    using CompressedInstruction = uint16_t;
    using FloatingRegister = uint64_t;
    using Counter = uint64_t;
};

#ifndef SIMRV_XLEN
#define SIMRV_XLEN 32
#endif

static_assert(SIMRV_XLEN == 32 || SIMRV_XLEN == 64, "SIMRV_XLEN must be 32 or 64");

using ActiveXLenTypes = XLenTypes<SIMRV_XLEN>;
using Word = ActiveXLenTypes::Word;
using SignedWord = ActiveXLenTypes::SignedWord;
using Register = ActiveXLenTypes::Register;
using Byte = ActiveXLenTypes::Byte;
using CompressedInstruction = ActiveXLenTypes::CompressedInstruction;
using FloatingRegister = ActiveXLenTypes::FloatingRegister;
using Counter = ActiveXLenTypes::Counter;
using Address = Word;
using Instruction = Word;
using CSRValue = Word;
using CSRAddress = Address;
using ImmValue = SignedWord;
using TrapCause = Word;
using PrivilegeLevel = uint8_t;

inline constexpr unsigned kXLenBits = static_cast<unsigned>(sizeof(Word) * 8u);

inline constexpr auto xlen_shift_mask() -> Word { return static_cast<Word>(kXLenBits - 1u); }

template <typename T>
constexpr auto zero_extend(T value, unsigned bits) -> std::make_unsigned_t<T> {
    static_assert(std::is_integral_v<T>, "zero_extend requires an integral type");
    using UnsignedT = std::make_unsigned_t<T>;
    const UnsignedT mask = (static_cast<UnsignedT>(1u) << bits) - static_cast<UnsignedT>(1u);
    return static_cast<UnsignedT>(value) & mask;
}

template <typename T>
constexpr auto sign_extend(T value, unsigned bits) -> T {
    static_assert(std::is_integral_v<T>, "sign_extend requires an integral type");
    using UnsignedT = std::make_unsigned_t<T>;
    const UnsignedT sign_bit = static_cast<UnsignedT>(1u) << (bits - 1u);
    const UnsignedT value_mask = static_cast<UnsignedT>(1u) << bits;
    const UnsignedT masked =
        static_cast<UnsignedT>(value) & (value_mask - static_cast<UnsignedT>(1u));
    return static_cast<T>((masked ^ sign_bit) - sign_bit);
}
