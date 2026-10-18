/**
 * @file RegisterFile.hpp
 * @brief Architectural register file container.
 */
#pragma once

#include <array>
#include <simrv/CompilerHints.hpp>

#include "simrv/isa/VectorTypes.hpp"
#include "simrv/xlen/RegisterTypes.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {

class CPU;

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
    /// Storage size for GPRs: 32 architectural registers plus slot 32 as a discard sink for x0
    /// writes
    static constexpr std::size_t kGprStorageSize = 33;

    struct RegisterProxy {
        RegisterFile* rf;
        simrv::xlen::RegId idx;
        constexpr auto operator=(simrv::xlen::Register val) -> RegisterProxy& {
            rf->write(idx, val);
            return *this;
        }
        constexpr operator simrv::xlen::Register() const { return rf->read(idx); }
    };

    struct FpRegisterProxy {
        RegisterFile* rf;
        simrv::xlen::FpRegId idx;
        constexpr auto operator=(simrv::xlen::FloatingRegister val) -> FpRegisterProxy& {
            rf->write_fp(idx, val);
            return *this;
        }
        constexpr operator simrv::xlen::FloatingRegister() const { return rf->read_fp(idx); }
    };

    struct VecRegisterProxy {
        RegisterFile* rf;
        simrv::xlen::VecRegId idx;
        constexpr auto operator=(const VectorRegister& val) -> VecRegisterProxy& {
            rf->write_vector(idx, val);
            return *this;
        }
        constexpr operator const VectorRegister&() const { return rf->read_vector(idx); }
        constexpr operator VectorRegister&() { return rf->read_vector(idx); }
        constexpr auto operator->() -> VectorRegister* { return &rf->read_vector(idx); }
        constexpr auto operator->() const -> const VectorRegister* { return &rf->read_vector(idx); }
    };

    unsigned xlen = 64;
    /// Architectural VLEN in bits; configured to a power of two in the supported 128-1024 range.
    unsigned vlen = 256;

    /// Value exposed by the read-only RISC-V Vector `vlenb` CSR.
    [[nodiscard]] constexpr auto vlen_bytes() const -> unsigned { return vlen / 8; }

    /**
     * @brief WARL mask for `vstart`, sized for VLMAX at LMUL=8 and SEW=8.
     *
     * Vector 1.0 defines maximum VLMAX as VLEN, so a power-of-two VLEN requires writable bits
     * capable of representing element indices 0 through VLEN-1.
     */
    [[nodiscard]] constexpr auto vstart_mask() const -> simrv::xlen::CsrValue {
        return static_cast<simrv::xlen::CsrValue>(vlen - 1U);
    }

    [[nodiscard]] constexpr auto operator[](simrv::xlen::RegId idx) -> RegisterProxy {
        return {.rf = this, .idx = idx};
    }
    [[nodiscard]] constexpr auto operator[](simrv::xlen::RegId idx) const -> simrv::xlen::Register {
        return read(idx);
    }
    [[nodiscard]] constexpr auto operator[](simrv::xlen::FpRegId idx) -> FpRegisterProxy {
        return {.rf = this, .idx = idx};
    }
    [[nodiscard]] constexpr auto operator[](simrv::xlen::FpRegId idx) const
        -> simrv::xlen::FloatingRegister {
        return read_fp(idx);
    }
    [[nodiscard]] constexpr auto operator[](simrv::xlen::VecRegId idx) -> VecRegisterProxy {
        return {.rf = this, .idx = idx};
    }
    [[nodiscard]] constexpr auto operator[](simrv::xlen::VecRegId idx) const
        -> const VectorRegister& {
        return read_vector(idx);
    }

    [[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto read(uint8_t idx) const noexcept
        -> simrv::xlen::Register {
        return reg_[idx];
    }

    [[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto read(simrv::xlen::RegId idx) const noexcept
        -> simrv::xlen::Register {
        return reg_[std::to_underlying(idx)];
    }

    SIMRV_ALWAYS_INLINE constexpr void write(simrv::xlen::RegId idx,
                                             simrv::xlen::Register val) noexcept {
        if constexpr (simrv::xlen::kIsXLen64) {
            if (simrv::compiler::unlikely(xlen == 32)) {
                val = static_cast<simrv::xlen::Register>(
                    static_cast<int64_t>(static_cast<int32_t>(val)));
            }
        }
        const uint8_t u_idx = (idx == simrv::xlen::RegId::Zero) ? 32 : std::to_underlying(idx);
        reg_[u_idx] = val;
    }

    /// Branchless GPR write: writes to rd (redirecting Zero to discard slot).
    /// In RV64-mode, sign-extends to 64-bit for 32-bit execution mode.
    SIMRV_ALWAYS_INLINE constexpr void write_branchless(simrv::xlen::RegId idx,
                                                        simrv::xlen::Register val) {
        write(idx, val);
    }

    [[nodiscard]] constexpr auto read_fp(simrv::xlen::FpRegId idx) const
        -> simrv::xlen::FloatingRegister {
        return freg_[std::to_underlying(idx)];
    }

    constexpr void write_fp(simrv::xlen::FpRegId idx, simrv::xlen::FloatingRegister val) {
        freg_[std::to_underlying(idx)] = val;
    }

    [[nodiscard]] constexpr auto read_vector(simrv::xlen::VecRegId idx) const
        -> const VectorRegister& {
        return vreg_[std::to_underlying(idx)];
    }

    [[nodiscard]] constexpr auto read_vector(simrv::xlen::VecRegId idx) -> VectorRegister& {
        return vreg_[std::to_underlying(idx)];
    }

    constexpr void write_vector(simrv::xlen::VecRegId idx, const VectorRegister& val) {
        vreg_[std::to_underlying(idx)] = val;
    }

    constexpr void fill(simrv::xlen::Register val) {
        reg_.fill(val);
        reg_[0] = 0;
    }

    constexpr void fill_fp(simrv::xlen::FloatingRegister val) { freg_.fill(val); }

    constexpr void fill_vector(const VectorRegister& val) { vreg_.fill(val); }

    [[nodiscard]] constexpr auto fp_data_ptr() const -> const simrv::xlen::FloatingRegister* {
        return freg_.data();
    }

   private:
    friend class CPU;

    /// Branchless GPR write used by a fast kernel whose guest XLEN is fixed for the current ALU
    /// chain.
    SIMRV_ALWAYS_INLINE constexpr void write_xlen64(uint8_t idx,
                                                    simrv::xlen::Register val) noexcept {
        reg_[idx] = val;
    }
    SIMRV_ALWAYS_INLINE constexpr void write_xlen64(simrv::xlen::RegId idx,
                                                    simrv::xlen::Register val) noexcept {
        write_xlen64(std::to_underlying(idx), val);
    }

    /// Branchless GPR write used by a fast kernel whose guest XLEN is fixed for the current ALU
    /// chain.
    SIMRV_ALWAYS_INLINE constexpr void write_xlen32(uint8_t idx,
                                                    simrv::xlen::Register val) noexcept {
        if constexpr (simrv::xlen::kIsXLen64) {
            val =
                static_cast<simrv::xlen::Register>(static_cast<int64_t>(static_cast<int32_t>(val)));
        }
        reg_[idx] = val;
    }
    SIMRV_ALWAYS_INLINE constexpr void write_xlen32(simrv::xlen::RegId idx,
                                                    simrv::xlen::Register val) noexcept {
        write_xlen32(std::to_underlying(idx), val);
    }

    alignas(64) std::array<simrv::xlen::Register, kGprStorageSize> reg_{};
    alignas(64) std::array<simrv::xlen::FloatingRegister, kNumRegisters> freg_{};
    alignas(kVlenMaxBytes) std::array<VectorRegister, kNumRegisters> vreg_{};
};

}  // namespace simrv::core
