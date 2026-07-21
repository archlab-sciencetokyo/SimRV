#pragma once

#include <bit>
#include <algorithm>
#include <type_traits>
#include <limits>
#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"

namespace simrv::execute::vector {

// Helper to check/get active elements under mask
inline bool is_element_active(const core::VectorRegister& mask_reg, uint32_t i, bool vm) {
    if (vm) return true;
    return (mask_reg.u8[i / 8] & (1u << (i % 8))) != 0; // NOLINT(cppcoreguidelines-pro-type-union-access)
}

inline bool get_mask_bit(const core::VectorRegister& mask_reg, uint32_t i) {
    return (mask_reg.u8[i / 8] & (1u << (i % 8))) != 0; // NOLINT(cppcoreguidelines-pro-type-union-access)
}

// Helper to write mask bits
inline void set_mask_bit(core::VectorRegister& dest, uint32_t i, bool val) {
    uint32_t byte_idx = i / 8;
    uint32_t bit_idx = i % 8;
    if (val) {
        dest.u8[byte_idx] |= (1u << bit_idx); // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else {
        dest.u8[byte_idx] &= ~(1u << bit_idx); // NOLINT(cppcoreguidelines-pro-type-union-access)
    }
}

// Helper to read group element
template <typename T>
inline T get_group_element(const core::RegisterFile& regs, RegId base_reg, uint32_t i) {
    const uint32_t elems_per_reg = regs.vlen_bytes() / sizeof(T);
    const uint32_t shift = std::countr_zero(elems_per_reg);
    uint32_t reg_offset = i >> shift;
    uint32_t elem_idx = i & (elems_per_reg - 1);
    auto actual_reg = static_cast<RegId>((std::to_underlying(base_reg) + reg_offset) & 0x1F);
    const auto& vreg = regs.read_vector(actual_reg);

    if constexpr (std::is_same_v<T, float>) {
        return vreg.f32[elem_idx]; // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else if constexpr (std::is_same_v<T, double>) {
        return vreg.f64[elem_idx]; // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else if constexpr (sizeof(T) == 1) {
        return static_cast<T>(vreg.u8[elem_idx]); // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else if constexpr (sizeof(T) == 2) {
        return static_cast<T>(vreg.u16[elem_idx]); // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else if constexpr (sizeof(T) == 4) {
        return static_cast<T>(vreg.u32[elem_idx]); // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else {
        return static_cast<T>(vreg.u64[elem_idx]); // NOLINT(cppcoreguidelines-pro-type-union-access)
    }
}

// Helper to write group element
template <typename T>
inline void set_group_element(core::RegisterFile& regs, RegId base_reg, uint32_t i, T val) {
    const uint32_t elems_per_reg = regs.vlen_bytes() / sizeof(T);
    const uint32_t shift = std::countr_zero(elems_per_reg);
    uint32_t reg_offset = i >> shift;
    uint32_t elem_idx = i & (elems_per_reg - 1);
    auto actual_reg = static_cast<RegId>((std::to_underlying(base_reg) + reg_offset) & 0x1F);
    auto& vreg = regs.read_vector(actual_reg);

    if constexpr (std::is_same_v<T, float>) {
        vreg.f32[elem_idx] = val; // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else if constexpr (std::is_same_v<T, double>) {
        vreg.f64[elem_idx] = val; // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else if constexpr (sizeof(T) == 1) {
        vreg.u8[elem_idx] = static_cast<uint8_t>(val); // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else if constexpr (sizeof(T) == 2) {
        vreg.u16[elem_idx] = static_cast<uint16_t>(val); // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else if constexpr (sizeof(T) == 4) {
        vreg.u32[elem_idx] = static_cast<uint32_t>(val); // NOLINT(cppcoreguidelines-pro-type-union-access)
    } else {
        vreg.u64[elem_idx] = static_cast<uint64_t>(val); // NOLINT(cppcoreguidelines-pro-type-union-access)
    }
}

// Standard templates for vector ALU operations
template <typename T, typename Op>
inline void perform_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, Op op) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        T val1 = get_group_element<T>(cpu.state().regs, rs1, i);
        T val2 = get_group_element<T>(cpu.state().regs, rs2, i);
        set_group_element<T>(cpu.state().regs, rd, i, op(val2, val1));
    }
}

template <typename T, typename Op>
inline void perform_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl, Op op) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(rs1_val);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        T val2 = get_group_element<T>(cpu.state().regs, rs2, i);
        set_group_element<T>(cpu.state().regs, rd, i, op(val2, val1));
    }
}

template <typename T, typename Op>
inline void perform_vi(core::CPU& cpu, RegId rd, int32_t imm, RegId rs2, bool vm, uint32_t vl, Op op) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(imm);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        T val2 = get_group_element<T>(cpu.state().regs, rs2, i);
        set_group_element<T>(cpu.state().regs, rd, i, op(val2, val1));
    }
}

// Vector comparisons
template <typename T, typename Op>
inline void perform_compare_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, Op op) {
    auto& dest = cpu.state().regs.read_vector(rd);
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        T val1 = get_group_element<T>(cpu.state().regs, rs1, i);
        T val2 = get_group_element<T>(cpu.state().regs, rs2, i);
        bool res = op(val2, val1);
        set_mask_bit(dest, i, res);
    }
}

template <typename T, typename Op>
inline void perform_compare_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl, Op op) {
    auto& dest = cpu.state().regs.read_vector(rd);
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(rs1_val);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        T val2 = get_group_element<T>(cpu.state().regs, rs2, i);
        bool res = op(val2, val1);
        set_mask_bit(dest, i, res);
    }
}

template <typename T, typename Op>
inline void perform_compare_vi(core::CPU& cpu, RegId rd, int32_t imm, RegId rs2, bool vm, uint32_t vl, Op op) {
    auto& dest = cpu.state().regs.read_vector(rd);
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(imm);

    for (uint32_t i = 0; i < vl; i++) {
        if (!is_element_active(mask_reg, i, vm)) continue;
        T val2 = get_group_element<T>(cpu.state().regs, rs2, i);
        bool res = op(val2, val1);
        set_mask_bit(dest, i, res);
    }
}

// IEEE 754 float16 to float32 converter
inline float fp16_to_fp32(uint16_t h) {
    uint32_t h_sign = (h >> 15) & 1;
    uint32_t h_exp = (h >> 10) & 0x1F;
    uint32_t h_mant = h & 0x3FF;
    
    uint32_t f_sign = h_sign << 31;
    uint32_t f_exp = 0;
    uint32_t f_mant = 0;
    
    if (h_exp == 0) {
        if (h_mant == 0) {
            uint32_t f_bits = f_sign;
            return std::bit_cast<float>(f_bits);
        } else {
            while ((h_mant & 0x400) == 0) {
                h_mant <<= 1;
                h_exp--;
            }
            h_exp++;
            h_mant &= 0x3FF;
            f_exp = (h_exp - 15 + 127) & 0xFF;
            f_mant = h_mant << 13;
        }
    } else if (h_exp == 0x1F) {
        f_exp = 0xFF;
        f_mant = (h_mant != 0) ? (h_mant << 13) | 0x400000 : 0;
    } else {
        f_exp = h_exp - 15 + 127;
        f_mant = h_mant << 13;
    }
    
    uint32_t f_bits = f_sign | (f_exp << 23) | f_mant;
    return std::bit_cast<float>(f_bits);
}

// IEEE 754 float32 to float16 converter
inline uint16_t fp32_to_fp16(float f) {
    uint32_t f_bits = std::bit_cast<uint32_t>(f);
    uint32_t f_sign = (f_bits >> 31) & 1;
    uint32_t f_exp = (f_bits >> 23) & 0xFF;
    uint32_t f_mant = f_bits & 0x7FFFFFU;
    
    uint32_t h_sign = f_sign << 15;
    uint32_t h_exp = 0;
    uint32_t h_mant = 0;
    
    if (f_exp == 0) {
        h_exp = 0;
        h_mant = 0;
    } else if (f_exp == 0xFF) {
        h_exp = 0x1F;
        h_mant = (f_mant != 0) ? (f_mant >> 13) | 1 : 0;
    } else {
        int exp = static_cast<int>(f_exp) - 127 + 15;
        if (exp >= 0x1F) {
            h_exp = 0x1F;
            h_mant = 0;
        } else if (exp <= 0) {
            if (14 - exp < 24) {
                uint32_t m = f_mant | 0x800000;
                h_mant = m >> (14 - exp);
                if ((m >> (13 - exp)) & 1) {
                    h_mant++;
                }
            } else {
                h_mant = 0;
            }
            h_exp = 0;
        } else {
            h_exp = static_cast<uint32_t>(exp);
            h_mant = f_mant >> 13;
            if ((f_mant >> 12) & 1) {
                h_mant++;
                if (h_mant & 0x400) {
                    h_mant = 0;
                    h_exp++;
                }
            }
        }
    }
    return static_cast<uint16_t>(h_sign | (h_exp << 10) | h_mant);
}

// Mathematical saturation and scaling helpers
template <typename T>
inline T sat_add_signed(T a, T b, bool& sat) {
    static_assert(std::is_signed_v<T>);
    using Promoted = std::conditional_t<sizeof(T) < 8, int64_t, __int128>;
    Promoted res = static_cast<Promoted>(a) + static_cast<Promoted>(b);
    Promoted min_limit = std::numeric_limits<T>::min();
    Promoted max_limit = std::numeric_limits<T>::max();
    if (res < min_limit) { sat = true; return std::numeric_limits<T>::min(); }
    if (res > max_limit) { sat = true; return std::numeric_limits<T>::max(); }
    return static_cast<T>(res);
}

template <typename T>
inline T sat_add_unsigned(T a, T b, bool& sat) {
    static_assert(std::is_unsigned_v<T>);
    T res = a + b;
    if (res < a) { sat = true; return std::numeric_limits<T>::max(); }
    return res;
}

template <typename T>
inline T sat_sub_signed(T a, T b, bool& sat) {
    static_assert(std::is_signed_v<T>);
    using Promoted = std::conditional_t<sizeof(T) < 8, int64_t, __int128>;
    Promoted res = static_cast<Promoted>(a) - static_cast<Promoted>(b);
    Promoted min_limit = std::numeric_limits<T>::min();
    Promoted max_limit = std::numeric_limits<T>::max();
    if (res < min_limit) { sat = true; return std::numeric_limits<T>::min(); }
    if (res > max_limit) { sat = true; return std::numeric_limits<T>::max(); }
    return static_cast<T>(res);
}

template <typename T>
inline T sat_sub_unsigned(T a, T b, bool& sat) {
    static_assert(std::is_unsigned_v<T>);
    if (a < b) { sat = true; return 0; }
    return a - b;
}

// Fixed-Point round and clip helper
template <typename T_dest, typename T_src>
inline T_dest round_and_clip(T_src v, uint32_t d, uint32_t vxrm, bool& saturated) {
    T_src rounded = v;
    if (d > 0) {
        T_src mask = (static_cast<T_src>(1) << d) - 1;
        T_src round_bit = static_cast<T_src>(1) << (d - 1);
        T_src fractional = v & mask;
        
        switch (vxrm) {
            case 0: // rnu
                rounded = (v + round_bit) >> d;
                break;
            case 1: // rne
                if (fractional == round_bit) {
                    rounded = (v >> d) + ((v >> d) & 1);
                } else {
                    rounded = (v + round_bit) >> d;
                }
                break;
            case 2: // rdown
                rounded = v >> d;
                break;
            case 3: // rod
                rounded = (v >> d) | (fractional != 0 ? 1 : 0);
                break;
            default:
                break;
        }
    }
    
    constexpr T_src min_limit = std::numeric_limits<T_dest>::min();
    constexpr T_src max_limit = std::numeric_limits<T_dest>::max();
    
    if (rounded < min_limit) {
        saturated = true;
        return static_cast<T_dest>(min_limit);
    } else if (rounded > max_limit) {
        saturated = true;
        return static_cast<T_dest>(max_limit);
    }
    return static_cast<T_dest>(rounded);
}

template <typename T_dest, typename T_src>
inline T_dest round_shift(T_src v, uint32_t d, uint32_t vxrm) {
    bool saturated = false;
    return round_and_clip<T_dest, T_src>(v, d, vxrm, saturated);
}

template <typename T>
inline T execute_vsmul_element(T vs2, T vs1, uint32_t vxrm, bool& saturated) {
    static_assert(std::is_signed_v<T>);
    using DoubleWidth = std::conditional_t<sizeof(T) == 1, int16_t,
                        std::conditional_t<sizeof(T) == 2, int32_t,
                        std::conditional_t<sizeof(T) == 4, int64_t, __int128>>>;
    
    DoubleWidth d = static_cast<DoubleWidth>(vs2) * static_cast<DoubleWidth>(vs1);
    uint32_t shift = sizeof(T) * 8 - 1;
    DoubleWidth rounded = d;
    if (shift > 0) {
        DoubleWidth mask = (static_cast<DoubleWidth>(1) << shift) - 1;
        DoubleWidth round_bit = static_cast<DoubleWidth>(1) << (shift - 1);
        DoubleWidth fractional = d & mask;
        
        switch (vxrm) {
            case 0: // rnu
                rounded = (d + round_bit) >> shift;
                break;
            case 1: // rne
                if (fractional == round_bit) {
                    rounded = (d >> shift) + ((d >> shift) & 1);
                } else {
                    rounded = (d + round_bit) >> shift;
                }
                break;
            case 2: // rdown
                rounded = d >> shift;
                break;
            case 3: // rod
                rounded = (d >> shift) | (fractional != 0 ? 1 : 0);
                break;
            default:
                break;
        }
    }
    DoubleWidth min_val = std::numeric_limits<T>::min();
    DoubleWidth max_val = std::numeric_limits<T>::max();
    if (rounded < min_val) {
        saturated = true;
        return std::numeric_limits<T>::min();
    } else if (rounded > max_val) {
        saturated = true;
        return std::numeric_limits<T>::max();
    }
    return static_cast<T>(rounded);
}

} // namespace simrv::execute::vector
