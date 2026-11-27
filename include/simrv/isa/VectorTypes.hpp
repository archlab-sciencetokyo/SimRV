#pragma once

#include <cstdint>

namespace simrv {

inline constexpr unsigned kVlenMaxBits = 1024;
inline constexpr unsigned kVlenMaxBytes = kVlenMaxBits / 8;

enum class Vsew : uint8_t {
    SEW_8 = 0,
    SEW_16 = 1,
    SEW_32 = 2,
    SEW_64 = 3,
};

enum class Vlmul : uint8_t {
    LMUL_1 = 0,
    LMUL_2 = 1,
    LMUL_4 = 2,
    LMUL_8 = 3,
    LMUL_RESERVED = 4,
    LMUL_F8 = 5,
    LMUL_F4 = 6,
    LMUL_F2 = 7,
};

enum class Vta : uint8_t { Undisturbed = 0, Agnostic = 1 };
enum class Vma : uint8_t { Undisturbed = 0, Agnostic = 1 };

struct VtypeView {
    uint64_t raw{0};
    uint8_t xlen{64};

    [[nodiscard]] constexpr auto vill() const noexcept -> bool {
        uint64_t const vill_mask = 1ULL << (xlen - 1);
        if ((raw & vill_mask) != 0) return true;
        uint64_t const reserved_mask =
            (xlen == 32) ? ~(0xFFULL | vill_mask) & 0xFFFFFFFFULL : ~(0xFFULL | vill_mask);
        if ((raw & reserved_mask) != 0) return true;
        if (sew_field() > 3 || lmul_field() == 4) return true;
        return false;
    }

    [[nodiscard]] constexpr auto sew_field() const noexcept -> uint8_t {
        return static_cast<uint8_t>((raw >> 3) & 0x7);
    }
    [[nodiscard]] constexpr auto lmul_field() const noexcept -> uint8_t {
        return static_cast<uint8_t>(raw & 0x7);
    }
    [[nodiscard]] constexpr auto vsew() const noexcept -> Vsew {
        return static_cast<Vsew>(sew_field());
    }
    [[nodiscard]] constexpr auto vlmul() const noexcept -> Vlmul {
        return static_cast<Vlmul>(lmul_field());
    }
    [[nodiscard]] constexpr auto vta() const noexcept -> Vta {
        return static_cast<Vta>((raw >> 6) & 0x1);
    }
    [[nodiscard]] constexpr auto vma() const noexcept -> Vma {
        return static_cast<Vma>((raw >> 7) & 0x1);
    }

    [[nodiscard]] constexpr auto sew_bits() const noexcept -> uint32_t { return 8u << sew_field(); }
    [[nodiscard]] constexpr auto sew_bytes() const noexcept -> uint32_t {
        return 1u << sew_field();
    }
};

}  // namespace simrv

// Global compatibility aliases per repository guidelines
using simrv::kVlenMaxBits;
using simrv::kVlenMaxBytes;
using simrv::Vlmul;
using simrv::Vma;
using simrv::Vsew;
using simrv::Vta;
using simrv::VtypeView;
