/**
 * @file RegisterFile.hpp
 * @brief Architectural register file container.
 */
#pragma once

#include <array>

#include <simrv/Define.hpp>

#include "simrv/xlen/Types.hpp"

namespace simrv::core {

struct alignas(kVlenMaxBytes) VectorRegister {
    union {
        uint8_t  u8[kVlenMaxBytes];
        uint16_t u16[kVlenMaxBytes / 2];
        uint32_t u32[kVlenMaxBytes / 4];
        uint64_t u64[kVlenMaxBytes / 8];
        int8_t   i8[kVlenMaxBytes];
        int16_t  i16[kVlenMaxBytes / 2];
        int32_t  i32[kVlenMaxBytes / 4];
        int64_t  i64[kVlenMaxBytes / 8];
        float    f32[kVlenMaxBytes / 4];
        double   f64[kVlenMaxBytes / 8];
    };
};

/**
 * @class RegisterFile
 * @brief Manages integer, floating-point, and vector architectural registers.
 *
 * Encapsulates the hardwired x0 == 0 behavior and provides a unified
 * state block for the register arrays.
 */
class RegisterFile {
   public:
    static constexpr std::size_t kNumRegisters = 32;

    struct RegisterProxy {
        RegisterFile* rf;
        RegId idx;
        constexpr auto operator=(Register val) -> RegisterProxy& {
            rf->write(idx, val);
            return *this;
        }
        constexpr operator Register() const { return rf->read(idx); }
    };

    unsigned xlen = 64;
    unsigned vlen = 256;

    [[nodiscard]] constexpr auto vlen_bytes() const -> unsigned { return vlen / 8; }

    [[nodiscard]] constexpr auto operator[](RegId idx) -> RegisterProxy { return {this, idx}; }
    [[nodiscard]] constexpr auto operator[](RegId idx) const -> Register { return read(idx); }

    [[nodiscard]] constexpr auto read(RegId idx) const -> Register { return reg_[std::to_underlying(idx)]; }

    constexpr void write(RegId idx, Register val) {
        if (idx != RegId::Zero) {
            if (xlen == 32) {
                val = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(val)));
            }
            reg_[std::to_underlying(idx)] = val;
        }
    }

    [[nodiscard]] constexpr auto read_fp(RegId idx) const -> FloatingRegister { return freg_[std::to_underlying(idx)]; }

    constexpr void write_fp(RegId idx, FloatingRegister val) { freg_[std::to_underlying(idx)] = val; }

    [[nodiscard]] constexpr auto read_vector(RegId idx) const -> const VectorRegister& { return vreg_[std::to_underlying(idx)]; }

    [[nodiscard]] constexpr auto read_vector(RegId idx) -> VectorRegister& { return vreg_[std::to_underlying(idx)]; }

    constexpr void write_vector(RegId idx, const VectorRegister& val) { vreg_[std::to_underlying(idx)] = val; }

    constexpr void fill(Register val) {
        reg_.fill(val);
        reg_[0] = 0;
    }

    constexpr void fill_fp(FloatingRegister val) { freg_.fill(val); }

    constexpr void fill_vector(const VectorRegister& val) { vreg_.fill(val); }

    [[nodiscard]] constexpr auto fp_data_ptr() const -> const FloatingRegister* {
        return freg_.data();
    }

   private:
    std::array<Register, kNumRegisters> reg_{};
    std::array<FloatingRegister, kNumRegisters> freg_{};
    std::array<VectorRegister, kNumRegisters> vreg_{};
};

}  // namespace simrv::core