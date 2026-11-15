/**
 * @file Crypto.cpp
 * @brief Standalone cryptographic algorithm implementations for RISC-V Scalar and Vector Crypto.
 */
#include "simrv/isa/Crypto.hpp"

namespace simrv::crypto {

namespace {

constexpr std::array<uint8_t, 256> kAesSBoxFwd = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

constexpr std::array<uint8_t, 256> kAesSBoxInv = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d};

constexpr std::array<uint32_t, 11> kAesRcon = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20,
                                               0x40, 0x80, 0x1b, 0x36, 0x00};

constexpr std::array<uint8_t, 256> kSm4SBox = {
    0xd6, 0x90, 0xe9, 0xfe, 0xcc, 0xe1, 0x3d, 0xb7, 0x16, 0xb6, 0x14, 0xc2, 0x28, 0xfb, 0x2c, 0x05,
    0x2b, 0x67, 0x9a, 0x76, 0x2a, 0xbe, 0x04, 0xc3, 0xaa, 0x44, 0x13, 0x26, 0x49, 0x86, 0x06, 0x99,
    0x9c, 0x42, 0x50, 0xf4, 0x91, 0xef, 0x98, 0x7a, 0x33, 0x54, 0x0b, 0x43, 0xed, 0xcf, 0xac, 0x62,
    0xe4, 0xb3, 0x1c, 0xa9, 0xc9, 0x08, 0xe8, 0x95, 0x80, 0xdf, 0x94, 0xfa, 0x75, 0x8f, 0x3f, 0xa6,
    0x47, 0x07, 0xa7, 0xfc, 0xf3, 0x73, 0x17, 0xba, 0x83, 0x59, 0x3c, 0x19, 0xe6, 0x85, 0x4f, 0xa8,
    0x68, 0x6b, 0x81, 0xb2, 0x71, 0x64, 0xda, 0x8b, 0xf8, 0xeb, 0x0f, 0x4b, 0x70, 0x56, 0x9d, 0x35,
    0x1e, 0x24, 0x0e, 0x5e, 0x63, 0x58, 0xd1, 0xa2, 0x25, 0x22, 0x7c, 0x3b, 0x01, 0x21, 0x78, 0x87,
    0xd4, 0x00, 0x46, 0x57, 0x9f, 0xd3, 0x27, 0x52, 0x4c, 0x36, 0x02, 0xe7, 0xa0, 0xc4, 0xc8, 0x9e,
    0xea, 0xbf, 0x8a, 0xd2, 0x40, 0xc7, 0x38, 0xb5, 0xa3, 0xf7, 0xf2, 0xce, 0xf9, 0x61, 0x15, 0xa1,
    0xe0, 0xae, 0x5d, 0xa4, 0x9b, 0x34, 0x1a, 0x55, 0xad, 0x93, 0x32, 0x30, 0xf5, 0x8c, 0xb1, 0xe3,
    0x1d, 0xf6, 0xe2, 0x2e, 0x82, 0x66, 0xca, 0x60, 0xc0, 0x29, 0x23, 0xab, 0x0d, 0x53, 0x4e, 0x6f,
    0xd5, 0xdb, 0x37, 0x45, 0xde, 0xfd, 0x8e, 0x2f, 0x03, 0xff, 0x6a, 0x72, 0x6d, 0x6c, 0x5b, 0x51,
    0x8d, 0x1b, 0xaf, 0x92, 0xbb, 0xdd, 0xbc, 0x7f, 0x11, 0xd9, 0x5c, 0x41, 0x1f, 0x10, 0x5a, 0xd8,
    0x0a, 0xc1, 0x31, 0x88, 0xa5, 0xcd, 0x7b, 0xbd, 0x2d, 0x74, 0xd0, 0x12, 0xb8, 0xe5, 0xb4, 0xb0,
    0x89, 0x69, 0x97, 0x4a, 0x0c, 0x96, 0x77, 0x7e, 0x65, 0xb9, 0xf1, 0x09, 0xc5, 0x6e, 0xc6, 0x84,
    0x18, 0xf0, 0x7d, 0xec, 0x3a, 0xdc, 0x4d, 0x20, 0x79, 0xee, 0x5f, 0x3e, 0xd7, 0xcb, 0x39, 0x48};

}  // namespace

auto aes_sbox_fwd(uint8_t in) noexcept -> uint8_t { return kAesSBoxFwd[in]; }

auto aes_sbox_inv(uint8_t in) noexcept -> uint8_t { return kAesSBoxInv[in]; }

auto aes_sub_word(uint32_t w) noexcept -> uint32_t {
    return (static_cast<uint32_t>(kAesSBoxFwd[(w >> 24) & 0xff]) << 24) |
           (static_cast<uint32_t>(kAesSBoxFwd[(w >> 16) & 0xff]) << 16) |
           (static_cast<uint32_t>(kAesSBoxFwd[(w >> 8) & 0xff]) << 8) |
           static_cast<uint32_t>(kAesSBoxFwd[w & 0xff]);
}

auto aes_inv_sub_word(uint32_t w) noexcept -> uint32_t {
    return (static_cast<uint32_t>(kAesSBoxInv[(w >> 24) & 0xff]) << 24) |
           (static_cast<uint32_t>(kAesSBoxInv[(w >> 16) & 0xff]) << 16) |
           (static_cast<uint32_t>(kAesSBoxInv[(w >> 8) & 0xff]) << 8) |
           static_cast<uint32_t>(kAesSBoxInv[w & 0xff]);
}

auto gfmul(uint8_t a, uint8_t b) noexcept -> uint8_t {
    uint8_t res = 0;
    uint8_t temp = a;
    for (int i = 0; i < 8; ++i) {
        if ((b >> i) & 1) {
            res ^= temp;
        }
        const bool carry = (temp & 0x80) != 0;
        temp <<= 1;
        if (carry) {
            temp ^= 0x1b;
        }
    }
    return res;
}

auto aes_mix_columns(uint32_t col) noexcept -> uint32_t {
    const uint8_t s0 = col & 0xff;
    const uint8_t s1 = (col >> 8) & 0xff;
    const uint8_t s2 = (col >> 16) & 0xff;
    const uint8_t s3 = (col >> 24) & 0xff;

    const uint8_t b0 = gfmul(s0, 2) ^ gfmul(s1, 3) ^ s2 ^ s3;
    const uint8_t b1 = s0 ^ gfmul(s1, 2) ^ gfmul(s2, 3) ^ s3;
    const uint8_t b2 = s0 ^ s1 ^ gfmul(s2, 2) ^ gfmul(s3, 3);
    const uint8_t b3 = gfmul(s0, 3) ^ s1 ^ s2 ^ gfmul(s3, 2);

    return (static_cast<uint32_t>(b3) << 24) | (static_cast<uint32_t>(b2) << 16) |
           (static_cast<uint32_t>(b1) << 8) | static_cast<uint32_t>(b0);
}

auto aes_inv_mix_columns(uint32_t col) noexcept -> uint32_t {
    const uint8_t s0 = col & 0xff;
    const uint8_t s1 = (col >> 8) & 0xff;
    const uint8_t s2 = (col >> 16) & 0xff;
    const uint8_t s3 = (col >> 24) & 0xff;

    const uint8_t b0 = gfmul(s0, 0x0e) ^ gfmul(s1, 0x0b) ^ gfmul(s2, 0x0d) ^ gfmul(s3, 0x09);
    const uint8_t b1 = gfmul(s0, 0x09) ^ gfmul(s1, 0x0e) ^ gfmul(s2, 0x0b) ^ gfmul(s3, 0x0d);
    const uint8_t b2 = gfmul(s0, 0x0d) ^ gfmul(s1, 0x09) ^ gfmul(s2, 0x0e) ^ gfmul(s3, 0x0b);
    const uint8_t b3 = gfmul(s0, 0x0b) ^ gfmul(s1, 0x0d) ^ gfmul(s2, 0x09) ^ gfmul(s3, 0x0e);

    return (static_cast<uint32_t>(b3) << 24) | (static_cast<uint32_t>(b2) << 16) |
           (static_cast<uint32_t>(b1) << 8) | static_cast<uint32_t>(b0);
}

auto aes_mixcolumn_byte_fwd(uint8_t so) noexcept -> uint32_t {
    const uint8_t b3 = gfmul(so, 3);
    const uint8_t b2 = so;
    const uint8_t b1 = so;
    const uint8_t b0 = gfmul(so, 2);
    return (static_cast<uint32_t>(b3) << 24) | (static_cast<uint32_t>(b2) << 16) |
           (static_cast<uint32_t>(b1) << 8) | static_cast<uint32_t>(b0);
}

auto aes_mixcolumn_byte_inv(uint8_t so) noexcept -> uint32_t {
    const uint8_t b3 = gfmul(so, 0x0b);
    const uint8_t b2 = gfmul(so, 0x0d);
    const uint8_t b1 = gfmul(so, 0x09);
    const uint8_t b0 = gfmul(so, 0x0e);
    return (static_cast<uint32_t>(b3) << 24) | (static_cast<uint32_t>(b2) << 16) |
           (static_cast<uint32_t>(b1) << 8) | static_cast<uint32_t>(b0);
}

auto aes_rv64_shiftrows_fwd(uint64_t rs2, uint64_t rs1) noexcept -> uint64_t {
    auto getb = [](uint64_t v, int i) -> uint64_t { return (v >> (i * 8)) & 0xffULL; };
    return (getb(rs1, 3) << 56) | (getb(rs2, 6) << 48) | (getb(rs2, 1) << 40) |
           (getb(rs1, 4) << 32) | (getb(rs2, 7) << 24) | (getb(rs2, 2) << 16) |
           (getb(rs1, 5) << 8) | getb(rs1, 0);
}

auto aes_rv64_shiftrows_inv(uint64_t rs2, uint64_t rs1) noexcept -> uint64_t {
    auto getb = [](uint64_t v, int i) -> uint64_t { return (v >> (i * 8)) & 0xffULL; };
    return (getb(rs2, 3) << 56) | (getb(rs2, 6) << 48) | (getb(rs1, 1) << 40) |
           (getb(rs1, 4) << 32) | (getb(rs1, 7) << 24) | (getb(rs2, 2) << 16) |
           (getb(rs2, 5) << 8) | getb(rs1, 0);
}

auto aes64_ks1i(uint64_t rs1, unsigned rnum) noexcept -> uint64_t {
    if (rnum > 10) return 0;
    auto alpha = static_cast<uint32_t>(rs1 >> 32);
    if (rnum != 10) {
        alpha = std::rotr(alpha, 8);
    }
    alpha = aes_sub_word(alpha);
    alpha ^= kAesRcon[rnum];
    return (static_cast<uint64_t>(alpha) << 32) | alpha;
}

auto aes64_ks2(uint64_t rs1, uint64_t rs2) noexcept -> uint64_t {
    const auto w0 = static_cast<uint32_t>((rs1 >> 32) ^ rs2);
    const auto w1 = static_cast<uint32_t>(w0 ^ (rs2 >> 32));
    return (static_cast<uint64_t>(w1) << 32) | w0;
}

auto aes64_im(uint64_t rs1) noexcept -> uint64_t {
    const auto lo = aes_inv_mix_columns(static_cast<uint32_t>(rs1));
    const auto hi = aes_inv_mix_columns(static_cast<uint32_t>(rs1 >> 32));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

auto aes64_es(uint64_t rs1, uint64_t rs2) noexcept -> uint64_t {
    const uint64_t sr = aes_rv64_shiftrows_fwd(rs2, rs1);
    uint64_t res = 0;
    for (int i = 0; i < 8; ++i) {
        const auto b = static_cast<uint8_t>((sr >> (i * 8)) & 0xffULL);
        res |= static_cast<uint64_t>(aes_sbox_fwd(b)) << (i * 8);
    }
    return res;
}

auto aes64_esm(uint64_t rs1, uint64_t rs2) noexcept -> uint64_t {
    const uint64_t sub = aes64_es(rs1, rs2);
    const auto lo = aes_mix_columns(static_cast<uint32_t>(sub));
    const auto hi = aes_mix_columns(static_cast<uint32_t>(sub >> 32));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

auto aes64_ds(uint64_t rs1, uint64_t rs2) noexcept -> uint64_t {
    const uint64_t sr = aes_rv64_shiftrows_inv(rs2, rs1);
    uint64_t res = 0;
    for (int i = 0; i < 8; ++i) {
        const auto b = static_cast<uint8_t>((sr >> (i * 8)) & 0xffULL);
        res |= static_cast<uint64_t>(aes_sbox_inv(b)) << (i * 8);
    }
    return res;
}

auto aes64_dsm(uint64_t rs1, uint64_t rs2) noexcept -> uint64_t {
    const uint64_t sub = aes64_ds(rs1, rs2);
    const auto lo = aes_inv_mix_columns(static_cast<uint32_t>(sub));
    const auto hi = aes_inv_mix_columns(static_cast<uint32_t>(sub >> 32));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

auto aes32_esi(uint32_t rs1, uint32_t rs2, unsigned bs) noexcept -> uint32_t {
    const unsigned shamt = (bs & 3U) * 8U;
    const auto so = aes_sbox_fwd(static_cast<uint8_t>((rs2 >> shamt) & 0xffU));
    return rs1 ^ (static_cast<uint32_t>(so) << shamt);
}

auto aes32_esmi(uint32_t rs1, uint32_t rs2, unsigned bs) noexcept -> uint32_t {
    const unsigned shamt = (bs & 3U) * 8U;
    const auto so = aes_sbox_fwd(static_cast<uint8_t>((rs2 >> shamt) & 0xffU));
    const uint32_t col = aes_mixcolumn_byte_fwd(so);
    return rs1 ^ std::rotl(col, shamt);
}

auto aes32_dsi(uint32_t rs1, uint32_t rs2, unsigned bs) noexcept -> uint32_t {
    const unsigned shamt = (bs & 3U) * 8U;
    const auto so = aes_sbox_inv(static_cast<uint8_t>((rs2 >> shamt) & 0xffU));
    return rs1 ^ (static_cast<uint32_t>(so) << shamt);
}

auto aes32_dsmi(uint32_t rs1, uint32_t rs2, unsigned bs) noexcept -> uint32_t {
    const unsigned shamt = (bs & 3U) * 8U;
    const auto so = aes_sbox_inv(static_cast<uint8_t>((rs2 >> shamt) & 0xffU));
    const uint32_t col = aes_mixcolumn_byte_inv(so);
    return rs1 ^ std::rotl(col, shamt);
}

auto sm4_sbox(uint8_t in) noexcept -> uint8_t { return kSm4SBox[in]; }

auto sm4_round_byte(uint32_t rs1, uint32_t rs2, unsigned bs) noexcept -> uint32_t {
    const unsigned shamt = (bs & 3U) * 8U;
    const uint32_t x = kSm4SBox[static_cast<uint8_t>((rs2 >> shamt) & 0xffU)];
    const uint32_t y =
        x ^ (x << 8U) ^ (x << 2U) ^ (x << 18U) ^ ((x & 0x3fU) << 26U) ^ ((x & 0xc0U) << 10U);
    const uint32_t z = std::rotl(y, shamt);
    return z ^ rs1;
}

auto sm4_key_schedule_byte(uint32_t rs1, uint32_t rs2, unsigned bs) noexcept -> uint32_t {
    const unsigned shamt = (bs & 3U) * 8U;
    const uint32_t x = kSm4SBox[static_cast<uint8_t>((rs2 >> shamt) & 0xffU)];
    const uint32_t y = x ^ ((x & 0x07U) << 29U) ^ ((x & 0xfeU) << 7U) ^ ((x & 0x01U) << 23U) ^
                       ((x & 0xf8U) << 13U);
    const uint32_t z = std::rotl(y, shamt);
    return z ^ rs1;
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

void ghash_multiply(const uint64_t h[2], const uint64_t x[2], uint64_t out[2]) noexcept {
    unsigned __int128 H = (static_cast<unsigned __int128>(h[1]) << 64) | h[0];
    const unsigned __int128 X = (static_cast<unsigned __int128>(x[1]) << 64) | x[0];
    const unsigned __int128 R = static_cast<unsigned __int128>(0xe1) << 120;
    unsigned __int128 Z = 0;
    unsigned __int128 V = H;
    for (int i = 0; i < 128; ++i) {
        if ((X >> (127 - i)) & 1) {
            Z ^= V;
        }
        if (V & 1) {
            V = (V >> 1) ^ R;
        } else {
            V = (V >> 1);
        }
    }
    out[0] = static_cast<uint64_t>(Z);
    out[1] = static_cast<uint64_t>(Z >> 64);
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace {

constexpr std::array<uint32_t, 32> kSm4Ck = {
    0x00070e15U, 0x1c232a31U, 0x383f464dU, 0x545b6269U, 0x70777e85U, 0x8c939aa1U, 0xa8afb6bdU,
    0xc4cbd2d9U, 0xe0e7eef5U, 0xfc030a11U, 0x181f262dU, 0x343b4249U, 0x50575e65U, 0x6c737a81U,
    0x888f969dU, 0xa4abb2b9U, 0xc0c7ced5U, 0xdce3eaf1U, 0xf8ff060dU, 0x141b2229U, 0x30373e45U,
    0x4c535a61U, 0x686f767dU, 0x848b9299U, 0xa0a7aeb5U, 0xbcc3cad1U, 0xd8dfe6edU, 0xf4fb0209U,
    0x10171e25U, 0x2c333a41U, 0x484f565dU, 0x646b7279U};

constexpr auto byte_at(uint32_t w, unsigned idx) noexcept -> uint8_t {
    return static_cast<uint8_t>((w >> (idx * 8U)) & 0xffU);
}

constexpr auto pack_bytes(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) noexcept -> uint32_t {
    return static_cast<uint32_t>(b0) | (static_cast<uint32_t>(b1) << 8U) |
           (static_cast<uint32_t>(b2) << 16U) | (static_cast<uint32_t>(b3) << 24U);
}

void aes_sub_shift_fwd(const uint32_t in[4], uint32_t out[4]) noexcept {
    uint32_t sb[4];
    for (unsigned c = 0; c < 4U; ++c) sb[c] = aes_sub_word(in[c]);
    for (unsigned c = 0; c < 4U; ++c) {
        out[c] = pack_bytes(byte_at(sb[c], 0U), byte_at(sb[(c + 1U) & 3U], 1U),
                            byte_at(sb[(c + 2U) & 3U], 2U), byte_at(sb[(c + 3U) & 3U], 3U));
    }
}

void aes_inv_shift_sub(const uint32_t in[4], uint32_t out[4]) noexcept {
    uint32_t sb[4];
    for (unsigned c = 0; c < 4U; ++c) sb[c] = aes_inv_sub_word(in[c]);
    for (unsigned c = 0; c < 4U; ++c) {
        out[c] = pack_bytes(byte_at(sb[c], 0U), byte_at(sb[(c + 3U) & 3U], 1U),
                            byte_at(sb[(c + 2U) & 3U], 2U), byte_at(sb[(c + 1U) & 3U], 3U));
    }
}

constexpr auto brev8_u32(uint32_t x) noexcept -> uint32_t {
    x = ((x & 0x55555555U) << 1U) | ((x >> 1U) & 0x55555555U);
    x = ((x & 0x33333333U) << 2U) | ((x >> 2U) & 0x33333333U);
    x = ((x & 0x0F0F0F0FU) << 4U) | ((x >> 4U) & 0x0F0F0F0FU);
    return x;
}

constexpr auto sm4_tau(uint32_t a) noexcept -> uint32_t {
    return pack_bytes(kSm4SBox[byte_at(a, 0U)], kSm4SBox[byte_at(a, 1U)], kSm4SBox[byte_at(a, 2U)],
                      kSm4SBox[byte_at(a, 3U)]);
}

constexpr auto sm4_l_cipher(uint32_t b) noexcept -> uint32_t {
    return b ^ std::rotl(b, 2) ^ std::rotl(b, 10) ^ std::rotl(b, 18) ^ std::rotl(b, 24);
}

constexpr auto sm4_l_key(uint32_t b) noexcept -> uint32_t {
    return b ^ std::rotl(b, 13) ^ std::rotl(b, 23);
}

constexpr auto sm3_ff(uint32_t x, uint32_t y, uint32_t z, unsigned j) noexcept -> uint32_t {
    return j < 16U ? (x ^ y ^ z) : ((x & y) | (x & z) | (y & z));
}

constexpr auto sm3_gg(uint32_t x, uint32_t y, uint32_t z, unsigned j) noexcept -> uint32_t {
    return j < 16U ? (x ^ y ^ z) : ((x & y) | ((~x) & z));
}

}  // namespace

void vaes_encrypt_mid(uint32_t state[4], const uint32_t rkey[4]) noexcept {
    uint32_t tmp[4];
    aes_sub_shift_fwd(state, tmp);
    for (unsigned c = 0; c < 4U; ++c) state[c] = aes_mix_columns(tmp[c]) ^ rkey[c];
}

void vaes_encrypt_final(uint32_t state[4], const uint32_t rkey[4]) noexcept {
    uint32_t tmp[4];
    aes_sub_shift_fwd(state, tmp);
    for (unsigned c = 0; c < 4U; ++c) state[c] = tmp[c] ^ rkey[c];
}

void vaes_decrypt_mid(uint32_t state[4], const uint32_t rkey[4]) noexcept {
    uint32_t tmp[4];
    aes_inv_shift_sub(state, tmp);
    for (unsigned c = 0; c < 4U; ++c) state[c] = aes_inv_mix_columns(tmp[c]) ^ rkey[c];
}

void vaes_decrypt_final(uint32_t state[4], const uint32_t rkey[4]) noexcept {
    uint32_t tmp[4];
    aes_inv_shift_sub(state, tmp);
    for (unsigned c = 0; c < 4U; ++c) state[c] = tmp[c] ^ rkey[c];
}

void vaes_kf1(const uint32_t prev[4], unsigned rnd, uint32_t out[4]) noexcept {
    const unsigned r = (rnd >= 1U && rnd <= 10U) ? rnd : ((rnd & 0xFU) % 10U + 1U);
    const uint32_t rot = std::rotr(prev[3], 8);
    const uint32_t temp = aes_sub_word(rot) ^ static_cast<uint32_t>(kAesRcon[r - 1U]);
    out[0] = prev[0] ^ temp;
    out[1] = prev[1] ^ out[0];
    out[2] = prev[2] ^ out[1];
    out[3] = prev[3] ^ out[2];
}

void vaes_kf2(const uint32_t prev2[4], const uint32_t prev1[4], unsigned rnd,
              uint32_t out[4]) noexcept {
    const unsigned r = (rnd >= 2U && rnd <= 14U) ? rnd : 2U;
    uint32_t temp = 0;
    if ((r & 1U) == 0U) {
        const uint32_t rot = std::rotr(prev1[3], 8);
        temp = aes_sub_word(rot) ^ static_cast<uint32_t>(kAesRcon[(r >> 1U) - 1U]);
    } else {
        temp = aes_sub_word(prev1[3]);
    }
    out[0] = prev2[0] ^ temp;
    out[1] = prev2[1] ^ out[0];
    out[2] = prev2[2] ^ out[1];
    out[3] = prev2[3] ^ out[2];
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

void zvkg_gmul(const uint32_t a[4], const uint32_t b[4], uint32_t out[4]) noexcept {
    auto load_brev8 = [](const uint32_t w[4]) -> unsigned __int128 {
        unsigned __int128 res = 0;
        for (unsigned i = 0; i < 4U; ++i) {
            res |= static_cast<unsigned __int128>(brev8_u32(w[i])) << (i * 32U);
        }
        return res;
    };
    const unsigned __int128 Y = load_brev8(a);
    unsigned __int128 H = load_brev8(b);
    unsigned __int128 Z = 0;
    for (unsigned i = 0; i < 128U; ++i) {
        if (((Y >> i) & 1U) != 0U) Z ^= H;
        const bool msb = ((H >> 127U) & 1U) != 0U;
        H = (H << 1U) ^
            (msb ? static_cast<unsigned __int128>(0x87U) : static_cast<unsigned __int128>(0U));
    }
    for (unsigned i = 0; i < 4U; ++i) {
        out[i] = brev8_u32(static_cast<uint32_t>(Z >> (i * 32U)));
    }
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

void zvkg_ghsh(uint32_t y[4], const uint32_t h[4], const uint32_t x[4]) noexcept {
    uint32_t sum[4];
    for (unsigned i = 0; i < 4U; ++i) sum[i] = y[i] ^ x[i];
    zvkg_gmul(sum, h, y);
}

void vsha256_ms(uint32_t w0_3[4], const uint32_t w4_11[4], const uint32_t w12_15[4]) noexcept {
    const uint32_t w16 = sha256_sig1(w12_15[2]) + w4_11[1] + sha256_sig0(w0_3[1]) + w0_3[0];
    const uint32_t w17 = sha256_sig1(w12_15[3]) + w4_11[2] + sha256_sig0(w0_3[2]) + w0_3[1];
    const uint32_t w18 = sha256_sig1(w16) + w4_11[3] + sha256_sig0(w0_3[3]) + w0_3[2];
    const uint32_t w19 = sha256_sig1(w17) + w12_15[0] + sha256_sig0(w4_11[0]) + w0_3[3];
    w0_3[0] = w16;
    w0_3[1] = w17;
    w0_3[2] = w18;
    w0_3[3] = w19;
}

void vsha256_compress(uint32_t cdgh[4], const uint32_t abef[4],
                      const uint32_t wk_pair[2]) noexcept {
    uint32_t f = abef[0];
    uint32_t e = abef[1];
    uint32_t b = abef[2];
    uint32_t a = abef[3];
    uint32_t h = cdgh[0];
    uint32_t g = cdgh[1];
    uint32_t d = cdgh[2];
    uint32_t c = cdgh[3];

    for (unsigned r = 0; r < 2U; ++r) {
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t1 = h + sha256_sum1(e) + ch + wk_pair[r];
        const uint32_t t2 = sha256_sum0(a) + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    cdgh[0] = e;
    cdgh[1] = f;
    cdgh[2] = b;
    cdgh[3] = a;
}

void vsha512_ms(uint64_t w0_3[4], const uint64_t w4_11[4], const uint64_t w12_15[4]) noexcept {
    const uint64_t w16 = sha512_sig1(w12_15[2]) + w4_11[1] + sha512_sig0(w0_3[1]) + w0_3[0];
    const uint64_t w17 = sha512_sig1(w12_15[3]) + w4_11[2] + sha512_sig0(w0_3[2]) + w0_3[1];
    const uint64_t w18 = sha512_sig1(w16) + w4_11[3] + sha512_sig0(w0_3[3]) + w0_3[2];
    const uint64_t w19 = sha512_sig1(w17) + w12_15[0] + sha512_sig0(w4_11[0]) + w0_3[3];
    w0_3[0] = w16;
    w0_3[1] = w17;
    w0_3[2] = w18;
    w0_3[3] = w19;
}

void vsha512_compress(uint64_t cdgh[4], const uint64_t abef[4],
                      const uint64_t wk_pair[2]) noexcept {
    uint64_t f = abef[0];
    uint64_t e = abef[1];
    uint64_t b = abef[2];
    uint64_t a = abef[3];
    uint64_t h = cdgh[0];
    uint64_t g = cdgh[1];
    uint64_t d = cdgh[2];
    uint64_t c = cdgh[3];

    for (unsigned r = 0; r < 2U; ++r) {
        const uint64_t ch = (e & f) ^ ((~e) & g);
        const uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint64_t t1 = h + sha512_sum1(e) + ch + wk_pair[r];
        const uint64_t t2 = sha512_sum0(a) + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    cdgh[0] = e;
    cdgh[1] = f;
    cdgh[2] = b;
    cdgh[3] = a;
}

void vsm3_me(const uint32_t w0_7[8], const uint32_t w8_15[8], uint32_t out[8]) noexcept {
    uint32_t w[24];
    for (unsigned i = 0; i < 8U; ++i) {
        w[i] = std::byteswap(w0_7[i]);
        w[8U + i] = std::byteswap(w8_15[i]);
    }
    for (unsigned i = 16U; i < 24U; ++i) {
        const uint32_t tmp = w[i - 16U] ^ w[i - 9U] ^ std::rotl(w[i - 3U], 15);
        w[i] = sm3_p1(tmp) ^ std::rotl(w[i - 13U], 7) ^ w[i - 6U];
        out[i - 16U] = std::byteswap(w[i]);
    }
}

void vsm3_compress(uint32_t state[8], const uint32_t w_eg[8], unsigned rnds) noexcept {
    uint32_t A = std::byteswap(state[0]);
    uint32_t B = std::byteswap(state[1]);
    uint32_t C = std::byteswap(state[2]);
    uint32_t D = std::byteswap(state[3]);
    uint32_t E = std::byteswap(state[4]);
    uint32_t F = std::byteswap(state[5]);
    uint32_t G = std::byteswap(state[6]);
    uint32_t H = std::byteswap(state[7]);

    for (unsigned k = 0; k < 2U; ++k) {
        const unsigned j = 2U * (rnds & 0x1FU) + k;
        const uint32_t t = j < 16U ? 0x79cc4519U : 0x7a879d8aU;
        const uint32_t w_j = std::byteswap(w_eg[k]);
        const uint32_t wp_j = w_j ^ std::byteswap(w_eg[4U + k]);
        const uint32_t ss1 = std::rotl(std::rotl(A, 12) + E + std::rotl(t, j & 31U), 7);
        const uint32_t ss2 = ss1 ^ std::rotl(A, 12);
        const uint32_t tt1 = sm3_ff(A, B, C, j) + D + ss2 + wp_j;
        const uint32_t tt2 = sm3_gg(E, F, G, j) + H + ss1 + w_j;
        D = C;
        C = std::rotl(B, 9);
        B = A;
        A = tt1;
        H = G;
        G = std::rotl(F, 19);
        F = E;
        E = sm3_p0(tt2);
    }

    state[0] = std::byteswap(A);
    state[1] = std::byteswap(B);
    state[2] = std::byteswap(C);
    state[3] = std::byteswap(D);
    state[4] = std::byteswap(E);
    state[5] = std::byteswap(F);
    state[6] = std::byteswap(G);
    state[7] = std::byteswap(H);
}

void vsm4_key_schedule(const uint32_t prev[4], unsigned rnd, uint32_t out[4]) noexcept {
    uint32_t k[4] = {prev[0], prev[1], prev[2], prev[3]};
    const unsigned base = (rnd & 7U) * 4U;
    for (unsigned i = 0; i < 4U; ++i) {
        const uint32_t b =
            k[(i + 1U) & 3U] ^ k[(i + 2U) & 3U] ^ k[(i + 3U) & 3U] ^ kSm4Ck[base + i];
        k[i] ^= sm4_l_key(sm4_tau(b));
        out[i] = k[i];
    }
}

void vsm4_rounds(uint32_t state[4], const uint32_t rk[4]) noexcept {
    uint32_t x[4] = {state[0], state[1], state[2], state[3]};
    for (unsigned i = 0; i < 4U; ++i) {
        const uint32_t b = x[(i + 1U) & 3U] ^ x[(i + 2U) & 3U] ^ x[(i + 3U) & 3U] ^ rk[i];
        x[i] ^= sm4_l_cipher(sm4_tau(b));
        state[i] = x[i];
    }
}

}  // namespace simrv::crypto
