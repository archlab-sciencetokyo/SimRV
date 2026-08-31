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
 * @brief Represents a single RISC-V Vector Extension register (VLEN up to 1024 bits).
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
    /// Architectural VLEN in bits; configured to a power of two in the supported 32–1024 range.
    unsigned vlen = 256;

    /// Value exposed by the read-only RISC-V Vector `vlenb` CSR.
    [[nodiscard]] constexpr auto vlen_bytes() const -> unsigned { return vlen / 8; }

    /**
     * @brief WARL mask for `vstart`, sized for VLMAX at LMUL=8 and SEW=8.
     *
     * Vector 1.0 defines maximum VLMAX as VLEN, so a power-of-two VLEN requires writable bits
     * capable of representing element indices 0 through VLEN-1.
     */
    [[nodiscard]] constexpr auto vstart_mask() const -> CSRValue {
        return static_cast<CSRValue>(vlen - 1U);
    }

    [[nodiscard]] constexpr auto operator[](RegId idx) -> RegisterProxy { return {this, idx}; }
    [[nodiscard]] constexpr auto operator[](RegId idx) const -> Register { return read(idx); }

    [[nodiscard]] constexpr auto read(RegId idx) const -> Register {
        return reg_[std::to_underlying(idx)];
    }

    constexpr void write(RegId idx, Register val) {
        if (simrv::compiler::likely(idx != RegId::Zero)) {
            if constexpr (simrv::xlen::kIsXLen64) {
                if (simrv::compiler::unlikely(xlen == 32)) {
                    val = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(val)));
                }
            }
            reg_[std::to_underlying(idx)] = val;
        }
    }

    /// Branchless GPR write: always writes to rd, then re-zeros x0.
    /// In RV64-mode, sign-extends to 64-bit for 32-bit execution mode.
    SIMRV_ALWAYS_INLINE constexpr void write_branchless(RegId idx, Register val) {
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

    [[nodiscard]] constexpr auto read_fp(FpRegId idx) const -> FloatingRegister {
        return freg_[std::to_underlying(idx)];
    }

    constexpr void write_fp(RegId idx, FloatingRegister val) {
        freg_[std::to_underlying(idx)] = val;
    }

    constexpr void write_fp(FpRegId idx, FloatingRegister val) {
        freg_[std::to_underlying(idx)] = val;
    }

    [[nodiscard]] constexpr auto read_vector(RegId idx) const -> const VectorRegister& {
        return vreg_[std::to_underlying(idx)];
    }

    [[nodiscard]] constexpr auto read_vector(VecRegId idx) const -> const VectorRegister& {
        return vreg_[std::to_underlying(idx)];
    }

    [[nodiscard]] constexpr auto read_vector(RegId idx) -> VectorRegister& {
        return vreg_[std::to_underlying(idx)];
    }

    [[nodiscard]] constexpr auto read_vector(VecRegId idx) -> VectorRegister& {
        return vreg_[std::to_underlying(idx)];
    }

    constexpr void write_vector(RegId idx, const VectorRegister& val) {
        vreg_[std::to_underlying(idx)] = val;
    }

    constexpr void write_vector(VecRegId idx, const VectorRegister& val) {
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
