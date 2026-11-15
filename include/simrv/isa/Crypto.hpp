/**
 * @file Crypto.hpp
 * @brief Unified standalone cryptographic mathematics substrate for RISC-V Scalar (Zk)
 *        and Vector (Zvk) cryptography extensions.
 */
#pragma once

#include <array>
#include <bit>
#include <cstdint>

#include "simrv/CompilerHints.hpp"

namespace simrv::crypto {

// =============================================================================
// AES (FIPS 197) Primitives & Tables
// =============================================================================

[[nodiscard]] auto aes_sbox_fwd(uint8_t in) noexcept -> uint8_t;
[[nodiscard]] auto aes_sbox_inv(uint8_t in) noexcept -> uint8_t;

[[nodiscard]] auto aes_sub_word(uint32_t w) noexcept -> uint32_t;
[[nodiscard]] auto aes_inv_sub_word(uint32_t w) noexcept -> uint32_t;

[[nodiscard]] auto gfmul(uint8_t a, uint8_t b) noexcept -> uint8_t;

[[nodiscard]] auto aes_mix_columns(uint32_t col) noexcept -> uint32_t;
[[nodiscard]] auto aes_inv_mix_columns(uint32_t col) noexcept -> uint32_t;

[[nodiscard]] auto aes_mixcolumn_byte_fwd(uint8_t so) noexcept -> uint32_t;
[[nodiscard]] auto aes_mixcolumn_byte_inv(uint8_t so) noexcept -> uint32_t;

[[nodiscard]] auto aes_rv64_shiftrows_fwd(uint64_t rs2, uint64_t rs1) noexcept -> uint64_t;
[[nodiscard]] auto aes_rv64_shiftrows_inv(uint64_t rs2, uint64_t rs1) noexcept -> uint64_t;

[[nodiscard]] auto aes64_ks1i(uint64_t rs1, unsigned rnum) noexcept -> uint64_t;
[[nodiscard]] auto aes64_ks2(uint64_t rs1, uint64_t rs2) noexcept -> uint64_t;
[[nodiscard]] auto aes64_im(uint64_t rs1) noexcept -> uint64_t;

[[nodiscard]] auto aes64_es(uint64_t rs1, uint64_t rs2) noexcept -> uint64_t;
[[nodiscard]] auto aes64_esm(uint64_t rs1, uint64_t rs2) noexcept -> uint64_t;
[[nodiscard]] auto aes64_ds(uint64_t rs1, uint64_t rs2) noexcept -> uint64_t;
[[nodiscard]] auto aes64_dsm(uint64_t rs1, uint64_t rs2) noexcept -> uint64_t;

[[nodiscard]] auto aes32_esi(uint32_t rs1, uint32_t rs2, unsigned bs) noexcept -> uint32_t;
[[nodiscard]] auto aes32_esmi(uint32_t rs1, uint32_t rs2, unsigned bs) noexcept -> uint32_t;
[[nodiscard]] auto aes32_dsi(uint32_t rs1, uint32_t rs2, unsigned bs) noexcept -> uint32_t;
[[nodiscard]] auto aes32_dsmi(uint32_t rs1, uint32_t rs2, unsigned bs) noexcept -> uint32_t;

// =============================================================================
// SHA-256 (FIPS 180-4)
// =============================================================================

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto sha256_sig0(uint32_t x) noexcept -> uint32_t {
    return std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3);
}

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto sha256_sig1(uint32_t x) noexcept -> uint32_t {
    return std::rotr(x, 17) ^ std::rotr(x, 19) ^ (x >> 10);
}

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto sha256_sum0(uint32_t x) noexcept -> uint32_t {
    return std::rotr(x, 2) ^ std::rotr(x, 13) ^ std::rotr(x, 22);
}

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto sha256_sum1(uint32_t x) noexcept -> uint32_t {
    return std::rotr(x, 6) ^ std::rotr(x, 11) ^ std::rotr(x, 25);
}

// =============================================================================
// SHA-512 (FIPS 180-4) — 64-bit and RV32 Split-Word Formulations
// =============================================================================

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto sha512_sig0(uint64_t x) noexcept -> uint64_t {
    return std::rotr(x, 1) ^ std::rotr(x, 8) ^ (x >> 7);
}

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto sha512_sig1(uint64_t x) noexcept -> uint64_t {
    return std::rotr(x, 19) ^ std::rotr(x, 61) ^ (x >> 6);
}

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto sha512_sum0(uint64_t x) noexcept -> uint64_t {
    return std::rotr(x, 28) ^ std::rotr(x, 34) ^ std::rotr(x, 39);
}

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto sha512_sum1(uint64_t x) noexcept -> uint64_t {
    return std::rotr(x, 14) ^ std::rotr(x, 18) ^ std::rotr(x, 41);
}

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto sha512_sig0l(uint32_t rs1, uint32_t rs2) noexcept
    -> uint32_t {
    return (rs1 >> 1) ^ (rs1 >> 7) ^ (rs1 >> 8) ^ (rs2 << 31) ^ (rs2 << 25) ^ (rs2 << 24);
}

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto sha512_sig0h(uint32_t rs1, uint32_t rs2) noexcept
    -> uint32_t {
    return (rs1 >> 1) ^ (rs1 >> 7) ^ (rs1 >> 8) ^ (rs2 << 31) ^ (rs2 << 24);
}

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto sha512_sig1l(uint32_t rs1, uint32_t rs2) noexcept
    -> uint32_t {
    return (rs1 << 3) ^ (rs1 >> 6) ^ (rs1 >> 19) ^ (rs2 >> 29) ^ (rs2 << 26) ^ (rs2 << 13);
}

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto sha512_sig1h(uint32_t rs1, uint32_t rs2) noexcept
    -> uint32_t {
    return (rs1 << 3) ^ (rs1 >> 6) ^ (rs1 >> 19) ^ (rs2 >> 29) ^ (rs2 << 13);
}

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto sha512_sum0r(uint32_t rs1, uint32_t rs2) noexcept
    -> uint32_t {
    return (rs1 << 25) ^ (rs1 << 30) ^ (rs1 >> 28) ^ (rs2 >> 7) ^ (rs2 >> 2) ^ (rs2 << 4);
}

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto sha512_sum1r(uint32_t rs1, uint32_t rs2) noexcept
    -> uint32_t {
    return (rs1 << 23) ^ (rs1 >> 14) ^ (rs1 >> 18) ^ (rs2 >> 9) ^ (rs2 << 18) ^ (rs2 << 14);
}

// =============================================================================
// SM3 Hash Permutations (GB/T 32918)
// =============================================================================

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto sm3_p0(uint32_t x) noexcept -> uint32_t {
    return x ^ std::rotl(x, 9) ^ std::rotl(x, 17);
}

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto sm3_p1(uint32_t x) noexcept -> uint32_t {
    return x ^ std::rotl(x, 15) ^ std::rotl(x, 23);
}

// =============================================================================
// SM4 Block Cipher (GB/T 32907-2016)
// =============================================================================

[[nodiscard]] auto sm4_sbox(uint8_t in) noexcept -> uint8_t;
[[nodiscard]] auto sm4_round_byte(uint32_t rs1, uint32_t rs2, unsigned bs) noexcept -> uint32_t;
[[nodiscard]] auto sm4_key_schedule_byte(uint32_t rs1, uint32_t rs2, unsigned bs) noexcept
    -> uint32_t;

// =============================================================================
// GHASH / GMUL Multiplication over GF(2^128)
// =============================================================================

void ghash_multiply(const uint64_t h[2], const uint64_t x[2], uint64_t out[2]) noexcept;

}  // namespace simrv::crypto

namespace simrv::isa::crypto {
using namespace simrv::crypto;
}
