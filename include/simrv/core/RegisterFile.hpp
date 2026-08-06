/**
 * @file RegisterFile.hpp
 * @brief Architectural register file container.
 */
#pragma once

#include <array>
#include <simrv/Define.hpp>

#include "simrv/xlen/Types.hpp"

namespace simrv::core {

/**
 * @struct VectorRegister
 * @brief Represents a single RISC-V Vector Extension register (VLEN up to 512 bits).
 *
 * Aligned to maximum vector register boundary (kVlenMaxBytes) and union-castable into
 * signed/unsigned integer or floating-point element slices.
 */
struct alignas(kVlenMaxBytes) VectorRegister {
    union {  // NOLINT(cppcoreguidelines-pro-type-union-access)
        std::array<uint8_t, kVlenMaxBytes> u8;
        std::array<uint16_t, kVlenMaxBytes / 2> u16;
        std::array<uint32_t, kVlenMaxBytes / 4> u32;
        std::array<uint64_t, kVlenMaxBytes / 8> u64;
        std::array<int8_t, kVlenMaxBytes> i8;
        std::array<int16_t, kVlenMaxBytes / 2> i16;
        std::array<int32_t, kVlenMaxBytes / 4> i32;
        std::array<int64_t, kVlenMaxBytes / 8> i64;
        std::array<float, kVlenMaxBytes / 4> f32;
        std::array<double, kVlenMaxBytes / 8> f64;
    };
};

/**
 * @class RegisterFile
 * @brief Architectural register file container for RISC-V GPRs, FPRs, and VRs.
 *
 * Encapsulates hardwired x0 == 0 zero-register semantics, sign-extension rules for
 * 32-bit execution mode on RV64, and cache-line aligned storage arrays.
 */
class RegisterFile {
   public:
    /// Number of architectural GPRs, FPRs, and vector registers (32)
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

    [[nodiscard]] constexpr auto raw_regs() const noexcept -> const Register* {
        return reg_.data();
    }
    [[nodiscard]] constexpr auto raw_regs() noexcept -> Register* { return reg_.data(); }

    [[nodiscard]] constexpr auto read(RegId idx) const -> Register {
        return reg_[std::to_underlying(idx)];
    }

    constexpr void write(RegId idx, Register val) {
        if constexpr (simrv::xlen::kIsXLen64) {
            if (simrv::compiler::unlikely(xlen == 32)) {
                val = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(val)));
            }
        }
        reg_[std::to_underlying(idx)] = val;
        reg_[0] = 0;
    }

    [[nodiscard]] constexpr auto read_fp(RegId idx) const -> FloatingRegister {
        return freg_[std::to_underlying(idx)];
    }

    constexpr void write_fp(RegId idx, FloatingRegister val) {
        freg_[std::to_underlying(idx)] = val;
    }

    [[nodiscard]] constexpr auto read_vector(RegId idx) const -> const VectorRegister& {
        return vreg_[std::to_underlying(idx)];
    }

    [[nodiscard]] constexpr auto read_vector(RegId idx) -> VectorRegister& {
        return vreg_[std::to_underlying(idx)];
    }

    constexpr void write_vector(RegId idx, const VectorRegister& val) {
        vreg_[std::to_underlying(idx)] = val;
    }

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
    alignas(64) std::array<Register, kNumRegisters> reg_{};
    alignas(64) std::array<FloatingRegister, kNumRegisters> freg_{};
    alignas(kVlenMaxBytes) std::array<VectorRegister, kNumRegisters> vreg_{};
};

}  // namespace simrv::core