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
 * @class RegisterFile
 * @brief Manages integer and floating-point architectural registers.
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

    constexpr void fill(Register val) {
        reg_.fill(val);
        reg_[0] = 0;
    }

    constexpr void fill_fp(FloatingRegister val) { freg_.fill(val); }

    [[nodiscard]] constexpr auto fp_data_ptr() const -> const FloatingRegister* {
        return freg_.data();
    }

   private:
    std::array<Register, kNumRegisters> reg_{};
    std::array<FloatingRegister, kNumRegisters> freg_{};
};

}  // namespace simrv::core