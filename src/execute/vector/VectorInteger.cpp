#include "simrv/execute/ExecuteUnit.hpp"
#include "VectorHelpers.hpp"

namespace simrv::execute {

namespace {

// Helpers for carry/borrow calculation
template <typename T>
bool calc_carry_out(T val2, T val1, bool carry_in) {
    using U = std::make_unsigned_t<T>;
    U u2 = static_cast<U>(val2);
    U u1 = static_cast<U>(val1);
    U sum = u2 + u1 + (carry_in ? 1 : 0);
    if (carry_in) {
        return sum <= u2;
    } else {
        return sum < u2;
    }
}

template <typename T>
bool calc_borrow_out(T val2, T val1, bool borrow_in) {
    using U = std::make_unsigned_t<T>;
    U u2 = static_cast<U>(val2);
    U u1 = static_cast<U>(val1);
    if (borrow_in) {
        return u2 <= u1;
    } else {
        return u2 < u1;
    }
}

// Bit manipulation helper functions
template <typename T>
T rotl(T val, uint32_t shift) {
    constexpr uint32_t width = sizeof(T) * 8;
    shift %= width;
    if (shift == 0) return val;
    using U = std::make_unsigned_t<T>;
    U uval = static_cast<U>(val);
    return static_cast<T>((uval << shift) | (uval >> (width - shift)));
}

template <typename T>
T rotr(T val, uint32_t shift) {
    constexpr uint32_t width = sizeof(T) * 8;
    shift %= width;
    if (shift == 0) return val;
    using U = std::make_unsigned_t<T>;
    U uval = static_cast<U>(val);
    return static_cast<T>((uval >> shift) | (uval << (width - shift)));
}

template <typename T>
T bit_reverse(T val) {
    using U = std::make_unsigned_t<T>;
    U uval = static_cast<U>(val);
    U res = 0;
    constexpr int width = sizeof(T) * 8;
    for (int b = 0; b < width; b++) {
        if ((uval & (U{1} << b)) != 0) {
            res |= (U{1} << (width - 1 - b));
        }
    }
    return static_cast<T>(res);
}

template <typename T>
T bit_reverse_bytes(T val) {
    using U = std::make_unsigned_t<T>;
    U uval = static_cast<U>(val);
    U res = 0;
    constexpr int num_bytes = sizeof(T);
    for (int byte_idx = 0; byte_idx < num_bytes; byte_idx++) {
        uint8_t byte_val = (uval >> (byte_idx * 8)) & 0xFF;
        uint8_t rev_byte = 0;
        for (int b = 0; b < 8; b++) {
            if ((byte_val & (1 << b)) != 0) {
                rev_byte |= (1 << (7 - b));
            }
        }
        res |= (static_cast<U>(rev_byte) << (byte_idx * 8));
    }
    return static_cast<T>(res);
}

template <typename T>
T byteswap_element(T val) {
    using U = std::make_unsigned_t<T>;
    U uval = static_cast<U>(val);
    return static_cast<T>(std::byteswap(uval));
}

template <typename T>
T clmul_low(T val2, T val1) {
    using U = std::make_unsigned_t<T>;
    U u2 = static_cast<U>(val2);
    U u1 = static_cast<U>(val1);
    U res = 0;
    constexpr int width = sizeof(T) * 8;
    for (int i = 0; i < width; i++) {
        if ((u1 & (U{1} << i)) != 0) {
            res ^= (u2 << i);
        }
    }
    return static_cast<T>(res);
}

template <typename T>
T clmul_high(T val2, T val1) {
    using U = std::make_unsigned_t<T>;
    U u2 = static_cast<U>(val2);
    U u1 = static_cast<U>(val1);
    U res = 0;
    constexpr int width = sizeof(T) * 8;
    for (int i = 1; i < width; i++) {
        if ((u1 & (U{1} << i)) != 0) {
            res ^= (u2 >> (width - i));
        }
    }
    return static_cast<T>(res);
}

// Vector Sign/Zero Extension
template <typename T_dest, typename T_src>
void execute_vsext(core::CPU& cpu, RegId rd, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    
    // Copy source elements first to handle potential register overlap
    std::vector<T_src> src_vals(vl);
    for (uint32_t i = 0; i < vl; i++) {
        src_vals[i] = vector::get_group_element<T_src>(cpu.state().regs, rs2, i);
    }
    
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T_dest extended = static_cast<T_dest>(src_vals[i]);
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, extended);
    }
}

// Vector Subtract with Borrow
template <typename T>
void execute_vsbc_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, uint32_t vl) {
    const auto& v0 = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        T val1 = vector::get_group_element<T>(cpu.state().regs, rs1, i);
        bool borrow_in = vector::get_mask_bit(v0, i);
        T res = val2 - val1 - (borrow_in ? 1 : 0);
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
    }
}

template <typename T>
void execute_vsbc_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, uint32_t vl) {
    const auto& v0 = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(rs1_val);
    for (uint32_t i = 0; i < vl; i++) {
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        bool borrow_in = vector::get_mask_bit(v0, i);
        T res = val2 - val1 - (borrow_in ? 1 : 0);
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
    }
}

// Widening Add
template <typename T_dest, typename T_src>
void execute_vwadd_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T_src val2 = vector::get_group_element<T_src>(cpu.state().regs, rs2, i);
        T_src val1 = vector::get_group_element<T_src>(cpu.state().regs, rs1, i);
        T_dest res = static_cast<T_dest>(val2) + static_cast<T_dest>(val1);
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

template <typename T_dest, typename T_src>
void execute_vwadd_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T_src val1 = static_cast<T_src>(rs1_val);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T_src val2 = vector::get_group_element<T_src>(cpu.state().regs, rs2, i);
        T_dest res = static_cast<T_dest>(val2) + static_cast<T_dest>(val1);
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

template <typename T_dest, typename T_src>
void execute_vwadd_wv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T_dest val2 = vector::get_group_element<T_dest>(cpu.state().regs, rs2, i);
        T_src val1 = vector::get_group_element<T_src>(cpu.state().regs, rs1, i);
        T_dest res = val2 + static_cast<T_dest>(val1);
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

template <typename T_dest, typename T_src>
void execute_vwadd_wx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T_src val1 = static_cast<T_src>(rs1_val);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T_dest val2 = vector::get_group_element<T_dest>(cpu.state().regs, rs2, i);
        T_dest res = val2 + static_cast<T_dest>(val1);
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

// Widening Subtract
template <typename T_dest, typename T_src>
void execute_vwsub_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T_src val2 = vector::get_group_element<T_src>(cpu.state().regs, rs2, i);
        T_src val1 = vector::get_group_element<T_src>(cpu.state().regs, rs1, i);
        T_dest res = static_cast<T_dest>(val2) - static_cast<T_dest>(val1);
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

template <typename T_dest, typename T_src>
void execute_vwsub_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T_src val1 = static_cast<T_src>(rs1_val);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T_src val2 = vector::get_group_element<T_src>(cpu.state().regs, rs2, i);
        T_dest res = static_cast<T_dest>(val2) - static_cast<T_dest>(val1);
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

template <typename T_dest, typename T_src>
void execute_vwsub_wv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T_dest val2 = vector::get_group_element<T_dest>(cpu.state().regs, rs2, i);
        T_src val1 = vector::get_group_element<T_src>(cpu.state().regs, rs1, i);
        T_dest res = val2 - static_cast<T_dest>(val1);
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

template <typename T_dest, typename T_src>
void execute_vwsub_wx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T_src val1 = static_cast<T_src>(rs1_val);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T_dest val2 = vector::get_group_element<T_dest>(cpu.state().regs, rs2, i);
        T_dest res = val2 - static_cast<T_dest>(val1);
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

// Widening Signed-Unsigned Multiplication
template <typename T_dest, typename T_src_signed, typename T_src_unsigned>
void execute_vwmulsu_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T_src_signed val2 = vector::get_group_element<T_src_signed>(cpu.state().regs, rs2, i);
        T_src_unsigned val1 = vector::get_group_element<T_src_unsigned>(cpu.state().regs, rs1, i);
        T_dest res = static_cast<T_dest>(val2) * static_cast<T_dest>(val1);
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

template <typename T_dest, typename T_src_signed, typename T_src_unsigned>
void execute_vwmulsu_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T_src_unsigned val1 = static_cast<T_src_unsigned>(rs1_val);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T_src_signed val2 = vector::get_group_element<T_src_signed>(cpu.state().regs, rs2, i);
        T_dest res = static_cast<T_dest>(val2) * static_cast<T_dest>(val1);
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

// Widening Reduction Sum
template <typename T_dest, typename T_src>
void execute_vwredsum_vs(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T_dest sum = vector::get_group_element<T_dest>(cpu.state().regs, rs1, 0);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T_src val2 = vector::get_group_element<T_src>(cpu.state().regs, rs2, i);
        sum += static_cast<T_dest>(val2);
    }
    vector::set_group_element<T_dest>(cpu.state().regs, rd, 0, sum);
}

// Vector Single-Width Integer Sum Reduction
template <typename T>
void execute_vredsum_vs(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    T sum = vector::get_group_element<T>(cpu.state().regs, rs1, 0);
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        sum += val;
    }
    vector::set_group_element<T>(cpu.state().regs, rd, 0, sum);
}

// Vector Widening Integer Multiply (Vector-Vector)
template <typename T_dest, typename T_src>
void execute_vwmul_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        auto val1 = static_cast<T_dest>(vector::get_group_element<T_src>(cpu.state().regs, rs1, i));
        auto val2 = static_cast<T_dest>(vector::get_group_element<T_src>(cpu.state().regs, rs2, i));
        T_dest res = val2 * val1;
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

// Vector Widening Integer Multiply (Vector-Scalar)
template <typename T_dest, typename T_src>
void execute_vwmul_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    auto val1 = static_cast<T_dest>(static_cast<T_src>(rs1_val));
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        auto val2 = static_cast<T_dest>(vector::get_group_element<T_src>(cpu.state().regs, rs2, i));
        T_dest res = val2 * val1;
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

template <typename T>
void perform_mac_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, bool overwrite_acc, bool subtract) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val1 = vector::get_group_element<T>(cpu.state().regs, rs1, i);
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        T dest_val = vector::get_group_element<T>(cpu.state().regs, rd, i);

        T prod = val1 * val2;
        T res = 0;
        if (overwrite_acc) {
            res = subtract ? (dest_val - prod) : (dest_val + prod);
        } else {
            T term1 = val1 * dest_val;
            res = subtract ? (val2 - term1) : (val2 + term1);
        }
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
    }
}

template <typename T>
void perform_mac_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl, bool overwrite_acc, bool subtract) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(rs1_val);

    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        T dest_val = vector::get_group_element<T>(cpu.state().regs, rd, i);

        T prod = val1 * val2;
        T res = 0;
        if (overwrite_acc) {
            res = subtract ? (dest_val - prod) : (dest_val + prod);
        } else {
            T term1 = val1 * dest_val;
            res = subtract ? (val2 - term1) : (val2 + term1);
        }
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
    }
}

template <typename T_dest, typename T_src1, typename T_src2>
void perform_widening_mac_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, bool subtract = false) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        auto val1 = vector::get_group_element<T_src1>(cpu.state().regs, rs1, i);
        auto val2 = vector::get_group_element<T_src2>(cpu.state().regs, rs2, i);
        auto dest_val = vector::get_group_element<T_dest>(cpu.state().regs, rd, i);

        T_dest prod = static_cast<T_dest>(val1) * static_cast<T_dest>(val2);
        T_dest res = subtract ? (dest_val - prod) : (dest_val + prod);
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

template <typename T_dest, typename T_src1, typename T_src2>
void perform_widening_mac_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl, bool subtract = false) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    auto val1 = static_cast<T_src1>(rs1_val);

    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        auto val2 = vector::get_group_element<T_src2>(cpu.state().regs, rs2, i);
        auto dest_val = vector::get_group_element<T_dest>(cpu.state().regs, rd, i);

        T_dest prod = static_cast<T_dest>(val1) * static_cast<T_dest>(val2);
        T_dest res = subtract ? (dest_val - prod) : (dest_val + prod);
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

// Mask instruction helper implementations
void execute_vmsbf_m(core::CPU& cpu, RegId rd, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    const auto& src_reg = cpu.state().regs.read_vector(rs2);
    auto& dest_reg = cpu.state().regs.read_vector(rd);
    bool found_first = false;
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        bool src_bit = vector::get_mask_bit(src_reg, i);
        if (found_first) {
            vector::set_mask_bit(dest_reg, i, false);
        } else {
            if (src_bit) {
                found_first = true;
                vector::set_mask_bit(dest_reg, i, false);
            } else {
                vector::set_mask_bit(dest_reg, i, true);
            }
        }
    }
}

void execute_vmsif_m(core::CPU& cpu, RegId rd, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    const auto& src_reg = cpu.state().regs.read_vector(rs2);
    auto& dest_reg = cpu.state().regs.read_vector(rd);
    bool found_first = false;
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        bool src_bit = vector::get_mask_bit(src_reg, i);
        if (found_first) {
            vector::set_mask_bit(dest_reg, i, false);
        } else {
            vector::set_mask_bit(dest_reg, i, true);
            if (src_bit) {
                found_first = true;
            }
        }
    }
}

void execute_vmsof_m(core::CPU& cpu, RegId rd, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    const auto& src_reg = cpu.state().regs.read_vector(rs2);
    auto& dest_reg = cpu.state().regs.read_vector(rd);
    bool found_first = false;
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        bool src_bit = vector::get_mask_bit(src_reg, i);
        if (src_bit && !found_first) {
            vector::set_mask_bit(dest_reg, i, true);
            found_first = true;
        } else {
            vector::set_mask_bit(dest_reg, i, false);
        }
    }
}

void execute_vfirst_m(core::CPU& cpu, RegId rd, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    const auto& src_reg = cpu.state().regs.read_vector(rs2);
    int32_t first_idx = -1;
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        if (vector::get_mask_bit(src_reg, i)) {
            first_idx = static_cast<int32_t>(i);
            break;
        }
    }
    cpu.state().regs.write(rd, static_cast<Register>(first_idx));
}

void execute_vcpop_m(core::CPU& cpu, RegId rd, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    const auto& src_reg = cpu.state().regs.read_vector(rs2);
    uint32_t count = 0;
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        if (vector::get_mask_bit(src_reg, i)) {
            count++;
        }
    }
    cpu.state().regs.write(rd, count);
}

template <typename T>
void execute_viota_m(core::CPU& cpu, RegId rd, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    const auto& src_reg = cpu.state().regs.read_vector(rs2);
    T count = 0;
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        vector::set_group_element<T>(cpu.state().regs, rd, i, count);
        if (vector::get_mask_bit(src_reg, i)) {
            count++;
        }
    }
}

void execute_mask_logical(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, uint32_t vl, isa::OperationId op_id) {
    const auto& src1_reg = cpu.state().regs.read_vector(rs1);
    const auto& src2_reg = cpu.state().regs.read_vector(rs2);
    auto& dest_reg = cpu.state().regs.read_vector(rd);
    for (uint32_t i = 0; i < vl; i++) {
        bool bit1 = vector::get_mask_bit(src1_reg, i);
        bool bit2 = vector::get_mask_bit(src2_reg, i);
        bool res = false;
        switch (op_id) {
            case isa::OperationId::VMAND_MM:   res = bit2 & bit1; break;
            case isa::OperationId::VMNAND_MM:  res = !(bit2 & bit1); break;
            case isa::OperationId::VMANDN_MM:  res = bit2 & !bit1; break;
            case isa::OperationId::VMXOR_MM:   res = bit2 ^ bit1; break;
            case isa::OperationId::VMOR_MM:    res = bit2 | bit1; break;
            case isa::OperationId::VMNOR_MM:   res = !(bit2 | bit1); break;
            case isa::OperationId::VMORN_MM:   res = bit2 | !bit1; break;
            case isa::OperationId::VMXNOR_MM:  res = !(bit2 ^ bit1); break;
            default: break;
        }
        vector::set_mask_bit(dest_reg, i, res);
    }
}


// Unary bitmanip implementations
template <typename T>
void execute_vclz(core::CPU& cpu, RegId rd, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        using U = std::make_unsigned_t<T>;
        U uval = static_cast<U>(val);
        int count = 0;
        if (uval == 0) {
            count = sizeof(T) * 8;
        } else {
            count = std::countl_zero(uval);
        }
        vector::set_group_element<T>(cpu.state().regs, rd, i, static_cast<T>(count));
    }
}

template <typename T>
void execute_vctz(core::CPU& cpu, RegId rd, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        using U = std::make_unsigned_t<T>;
        U uval = static_cast<U>(val);
        int count = 0;
        if (uval == 0) {
            count = sizeof(T) * 8;
        } else {
            count = std::countr_zero(uval);
        }
        vector::set_group_element<T>(cpu.state().regs, rd, i, static_cast<T>(count));
    }
}

template <typename T>
void execute_vcpop_v(core::CPU& cpu, RegId rd, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        T val = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        using U = std::make_unsigned_t<T>;
        U uval = static_cast<U>(val);
        int count = std::popcount(uval);
        vector::set_group_element<T>(cpu.state().regs, rd, i, static_cast<T>(count));
    }
}

// vmadc and vmsbc template implementations
template <typename T>
void execute_vmadc_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    const auto& v0 = cpu.state().regs.read_vector(RegId::Zero);
    auto& dest_reg = cpu.state().regs.read_vector(rd);
    for (uint32_t i = 0; i < vl; i++) {
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        T val1 = vector::get_group_element<T>(cpu.state().regs, rs1, i);
        bool carry_in = !vm && vector::get_mask_bit(v0, i);
        bool carry_out = calc_carry_out(val2, val1, carry_in);
        vector::set_mask_bit(dest_reg, i, carry_out);
    }
}

template <typename T>
void execute_vmadc_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl) {
    const auto& v0 = cpu.state().regs.read_vector(RegId::Zero);
    auto& dest_reg = cpu.state().regs.read_vector(rd);
    T val1 = static_cast<T>(rs1_val);
    for (uint32_t i = 0; i < vl; i++) {
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        bool carry_in = !vm && vector::get_mask_bit(v0, i);
        bool carry_out = calc_carry_out(val2, val1, carry_in);
        vector::set_mask_bit(dest_reg, i, carry_out);
    }
}

template <typename T>
void execute_vmadc_vi(core::CPU& cpu, RegId rd, int32_t simm5, RegId rs2, bool vm, uint32_t vl) {
    const auto& v0 = cpu.state().regs.read_vector(RegId::Zero);
    auto& dest_reg = cpu.state().regs.read_vector(rd);
    T val1 = static_cast<T>(simm5);
    for (uint32_t i = 0; i < vl; i++) {
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        bool carry_in = !vm && vector::get_mask_bit(v0, i);
        bool carry_out = calc_carry_out(val2, val1, carry_in);
        vector::set_mask_bit(dest_reg, i, carry_out);
    }
}

template <typename T>
void execute_vmsbc_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    const auto& v0 = cpu.state().regs.read_vector(RegId::Zero);
    auto& dest_reg = cpu.state().regs.read_vector(rd);
    for (uint32_t i = 0; i < vl; i++) {
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        T val1 = vector::get_group_element<T>(cpu.state().regs, rs1, i);
        bool borrow_in = !vm && vector::get_mask_bit(v0, i);
        bool borrow_out = calc_borrow_out(val2, val1, borrow_in);
        vector::set_mask_bit(dest_reg, i, borrow_out);
    }
}

template <typename T>
void execute_vmsbc_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl) {
    const auto& v0 = cpu.state().regs.read_vector(RegId::Zero);
    auto& dest_reg = cpu.state().regs.read_vector(rd);
    T val1 = static_cast<T>(rs1_val);
    for (uint32_t i = 0; i < vl; i++) {
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        bool borrow_in = !vm && vector::get_mask_bit(v0, i);
        bool borrow_out = calc_borrow_out(val2, val1, borrow_in);
        vector::set_mask_bit(dest_reg, i, borrow_out);
    }
}

// vadc template implementations
template <typename T>
void execute_vadc_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, uint32_t vl) {
    const auto& v0 = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = 0; i < vl; i++) {
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        T val1 = vector::get_group_element<T>(cpu.state().regs, rs1, i);
        bool carry_in = vector::get_mask_bit(v0, i);
        T res = val2 + val1 + (carry_in ? 1 : 0);
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
    }
}

template <typename T>
void execute_vadc_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, uint32_t vl) {
    const auto& v0 = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(rs1_val);
    for (uint32_t i = 0; i < vl; i++) {
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        bool carry_in = vector::get_mask_bit(v0, i);
        T res = val2 + val1 + (carry_in ? 1 : 0);
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
    }
}

template <typename T>
void execute_vadc_vi(core::CPU& cpu, RegId rd, int32_t simm5, RegId rs2, uint32_t vl) {
    const auto& v0 = cpu.state().regs.read_vector(RegId::Zero);
    T val1 = static_cast<T>(simm5);
    for (uint32_t i = 0; i < vl; i++) {
        T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
        bool carry_in = vector::get_mask_bit(v0, i);
        T res = val2 + val1 + (carry_in ? 1 : 0);
        vector::set_group_element<T>(cpu.state().regs, rd, i, res);
    }
}

// vwsll template implementations
template <typename T_dest, typename T_src>
void execute_vwsll_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    constexpr uint32_t shift_mask = (sizeof(T_dest) * 8) - 1;
    std::vector<T_src> src2_vals(vl);
    std::vector<T_src> src1_vals(vl);
    for (uint32_t i = 0; i < vl; i++) {
        src2_vals[i] = vector::get_group_element<T_src>(cpu.state().regs, rs2, i);
        src1_vals[i] = vector::get_group_element<T_src>(cpu.state().regs, rs1, i);
    }
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        using U_dest = std::make_unsigned_t<T_dest>;
        U_dest u2 = static_cast<U_dest>(static_cast<std::make_unsigned_t<T_src>>(src2_vals[i]));
        uint32_t shift = static_cast<uint32_t>(src1_vals[i]) & shift_mask;
        T_dest res = static_cast<T_dest>(u2 << shift);
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

template <typename T_dest, typename T_src>
void execute_vwsll_vx(core::CPU& cpu, RegId rd, Register rs1_val, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    constexpr uint32_t shift_mask = (sizeof(T_dest) * 8) - 1;
    std::vector<T_src> src2_vals(vl);
    for (uint32_t i = 0; i < vl; i++) {
        src2_vals[i] = vector::get_group_element<T_src>(cpu.state().regs, rs2, i);
    }
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        using U_dest = std::make_unsigned_t<T_dest>;
        U_dest u2 = static_cast<U_dest>(static_cast<std::make_unsigned_t<T_src>>(src2_vals[i]));
        uint32_t shift = static_cast<uint32_t>(rs1_val) & shift_mask;
        T_dest res = static_cast<T_dest>(u2 << shift);
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

template <typename T_dest, typename T_src>
void execute_vwsll_vi(core::CPU& cpu, RegId rd, int32_t simm5, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    constexpr uint32_t shift_mask = (sizeof(T_dest) * 8) - 1;
    std::vector<T_src> src2_vals(vl);
    for (uint32_t i = 0; i < vl; i++) {
        src2_vals[i] = vector::get_group_element<T_src>(cpu.state().regs, rs2, i);
    }
    for (uint32_t i = 0; i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        using U_dest = std::make_unsigned_t<T_dest>;
        U_dest u2 = static_cast<U_dest>(static_cast<std::make_unsigned_t<T_src>>(src2_vals[i]));
        uint32_t shift = static_cast<uint32_t>(simm5 & 0x1F) & shift_mask;
        T_dest res = static_cast<T_dest>(u2 << shift);
        vector::set_group_element<T_dest>(cpu.state().regs, rd, i, res);
    }
}

} // namespace

void ExecuteUnit::execute_vector_integer(core::CPU& cpu, isa::OperationId op_id,
                                         RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, uint32_t sew,
                                         Register rs1_val, int32_t simm5) {
    // Arithmetic lambda operations
    auto add_f = []<typename T>(T a, T b) -> T { return a + b; };
    auto sub_f = []<typename T>(T a, T b) -> T { return a - b; };
    auto mul_f = []<typename T>(T a, T b) -> T { return a * b; };
    auto div_f = []<typename T>(T a, T b) -> T {
        using S = std::make_signed_t<T>;
        if (b == 0) return static_cast<T>(-1);
        if (static_cast<S>(a) == std::numeric_limits<S>::min() && static_cast<S>(b) == -1) return a;
        return static_cast<T>(static_cast<S>(a) / static_cast<S>(b));
    };
    auto divu_f = []<typename T>(T a, T b) -> T { return b == 0 ? static_cast<T>(-1) : a / b; };
    auto and_f = []<typename T>(T a, T b) -> T { return a & b; };
    auto or_f = []<typename T>(T a, T b) -> T { return a | b; };
    auto xor_f = []<typename T>(T a, T b) -> T { return a ^ b; };
    auto sll_f = []<typename T>(T a, T b) -> T { return static_cast<T>(a << (b & (sizeof(T) * 8 - 1))); };
    auto srl_f = []<typename T>(T a, T b) -> T { return static_cast<T>(a >> (b & (sizeof(T) * 8 - 1))); };
    auto sra_f = []<typename T>(T a, T b) -> T { return static_cast<T>(static_cast<std::make_signed_t<T>>(a) >> (b & (sizeof(T) * 8 - 1))); };
    auto minu_f = []<typename T>(T a, T b) -> T { return std::min(a, b); };
    auto min_f = []<typename T>(T a, T b) -> T {
        using S = std::make_signed_t<T>;
        return static_cast<T>(std::min(static_cast<S>(a), static_cast<S>(b)));
    };
    auto maxu_f = []<typename T>(T a, T b) -> T { return std::max(a, b); };
    auto max_f = []<typename T>(T a, T b) -> T {
        using S = std::make_signed_t<T>;
        return static_cast<T>(std::max(static_cast<S>(a), static_cast<S>(b)));
    };

    auto eq_f = []<typename T>(T a, T b) -> bool { return a == b; };
    auto ne_f = []<typename T>(T a, T b) -> bool { return a != b; };
    auto lt_f = []<typename T>(T a, T b) -> bool { return static_cast<std::make_signed_t<T>>(a) < static_cast<std::make_signed_t<T>>(b); };
    auto ltu_f = []<typename T>(T a, T b) -> bool { return a < b; };
    auto le_f = []<typename T>(T a, T b) -> bool { return static_cast<std::make_signed_t<T>>(a) <= static_cast<std::make_signed_t<T>>(b); };
    auto leu_f = []<typename T>(T a, T b) -> bool { return a <= b; };
    auto gt_f = []<typename T>(T a, T b) -> bool { return static_cast<std::make_signed_t<T>>(a) > static_cast<std::make_signed_t<T>>(b); };
    auto gtu_f = []<typename T>(T a, T b) -> bool { return a > b; };
    auto andn_f = []<typename T>(T a, T b) -> T { return a & ~b; };
    auto rol_f = []<typename T>(T a, T b) -> T { return rotl<T>(a, b); };
    auto ror_f = []<typename T>(T a, T b) -> T { return rotr<T>(a, b); };
    auto clmul_f = []<typename T>(T a, T b) -> T { return clmul_low<T>(a, b); };
    auto clmulh_f = []<typename T>(T a, T b) -> T { return clmul_high<T>(a, b); };

    switch (op_id) {
        // VADD
        case isa::OperationId::VADD_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, add_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, add_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, add_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, add_f);
            return;
        case isa::OperationId::VADD_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, add_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, add_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, add_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, add_f);
            return;
        case isa::OperationId::VADD_VI:
            if (sew == 8) vector::perform_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl, add_f);
            else if (sew == 16) vector::perform_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl, add_f);
            else if (sew == 32) vector::perform_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl, add_f);
            else vector::perform_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl, add_f);
            return;

        // VSUB
        case isa::OperationId::VSUB_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, sub_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, sub_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, sub_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, sub_f);
            return;
        case isa::OperationId::VSUB_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, sub_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, sub_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, sub_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, sub_f);
            return;

        // VMUL
        case isa::OperationId::VMUL_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, mul_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, mul_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, mul_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, mul_f);
            return;
        case isa::OperationId::VMUL_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, mul_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, mul_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, mul_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, mul_f);
            return;

        // VDIV
        case isa::OperationId::VDIV_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, div_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, div_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, div_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, div_f);
            return;
        case isa::OperationId::VDIV_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, div_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, div_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, div_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, div_f);
            return;

        // VDIVU
        case isa::OperationId::VDIVU_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, divu_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, divu_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, divu_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, divu_f);
            return;
        case isa::OperationId::VDIVU_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, divu_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, divu_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, divu_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, divu_f);
            return;

        // VAND
        case isa::OperationId::VAND_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, and_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, and_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, and_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, and_f);
            return;
        case isa::OperationId::VAND_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, and_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, and_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, and_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, and_f);
            return;
        case isa::OperationId::VAND_VI:
            if (sew == 8) vector::perform_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl, and_f);
            else if (sew == 16) vector::perform_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl, and_f);
            else if (sew == 32) vector::perform_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl, and_f);
            else vector::perform_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl, and_f);
            return;

        // VOR
        case isa::OperationId::VOR_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, or_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, or_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, or_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, or_f);
            return;
        case isa::OperationId::VOR_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, or_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, or_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, or_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, or_f);
            return;
        case isa::OperationId::VOR_VI:
            if (sew == 8) vector::perform_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl, or_f);
            else if (sew == 16) vector::perform_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl, or_f);
            else if (sew == 32) vector::perform_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl, or_f);
            else vector::perform_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl, or_f);
            return;

        // VXOR
        case isa::OperationId::VXOR_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, xor_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, xor_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, xor_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, xor_f);
            return;
        case isa::OperationId::VXOR_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, xor_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, xor_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, xor_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, xor_f);
            return;
        case isa::OperationId::VXOR_VI:
            if (sew == 8) vector::perform_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl, xor_f);
            else if (sew == 16) vector::perform_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl, xor_f);
            else if (sew == 32) vector::perform_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl, xor_f);
            else vector::perform_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl, xor_f);
            return;

        // VSLL
        case isa::OperationId::VSLL_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, sll_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, sll_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, sll_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, sll_f);
            return;
        case isa::OperationId::VSLL_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, sll_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, sll_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, sll_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, sll_f);
            return;
        case isa::OperationId::VSLL_VI:
            if (sew == 8) vector::perform_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl, sll_f);
            else if (sew == 16) vector::perform_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl, sll_f);
            else if (sew == 32) vector::perform_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl, sll_f);
            else vector::perform_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl, sll_f);
            return;

        // VSRL
        case isa::OperationId::VSRL_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, srl_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, srl_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, srl_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, srl_f);
            return;
        case isa::OperationId::VSRL_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, srl_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, srl_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, srl_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, srl_f);
            return;
        case isa::OperationId::VSRL_VI:
            if (sew == 8) vector::perform_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl, srl_f);
            else if (sew == 16) vector::perform_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl, srl_f);
            else if (sew == 32) vector::perform_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl, srl_f);
            else vector::perform_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl, srl_f);
            return;

        // VSRA
        case isa::OperationId::VSRA_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, sra_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, sra_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, sra_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, sra_f);
            return;
        case isa::OperationId::VSRA_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, sra_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, sra_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, sra_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, sra_f);
            return;
        case isa::OperationId::VSRA_VI:
            if (sew == 8) vector::perform_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl, sra_f);
            else if (sew == 16) vector::perform_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl, sra_f);
            else if (sew == 32) vector::perform_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl, sra_f);
            else vector::perform_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl, sra_f);
            return;

        // VMIN
        case isa::OperationId::VMIN_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, min_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, min_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, min_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, min_f);
            return;
        case isa::OperationId::VMIN_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, min_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, min_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, min_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, min_f);
            return;

        // VMINU
        case isa::OperationId::VMINU_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, minu_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, minu_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, minu_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, minu_f);
            return;
        case isa::OperationId::VMINU_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, minu_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, minu_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, minu_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, minu_f);
            return;

        // VMAX
        case isa::OperationId::VMAX_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, max_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, max_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, max_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, max_f);
            return;
        case isa::OperationId::VMAX_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, max_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, max_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, max_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, max_f);
            return;

        // VMAXU
        case isa::OperationId::VMAXU_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, maxu_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, maxu_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, maxu_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, maxu_f);
            return;
        case isa::OperationId::VMAXU_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, maxu_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, maxu_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, maxu_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, maxu_f);
            return;

        // VMSEQ
        case isa::OperationId::VMSEQ_VV:
            if (sew == 8) vector::perform_compare_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, eq_f);
            else if (sew == 16) vector::perform_compare_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, eq_f);
            else if (sew == 32) vector::perform_compare_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, eq_f);
            else vector::perform_compare_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, eq_f);
            return;
        case isa::OperationId::VMSEQ_VX:
            if (sew == 8) vector::perform_compare_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, eq_f);
            else if (sew == 16) vector::perform_compare_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, eq_f);
            else if (sew == 32) vector::perform_compare_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, eq_f);
            else vector::perform_compare_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, eq_f);
            return;
        case isa::OperationId::VMSEQ_VI:
            if (sew == 8) vector::perform_compare_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl, eq_f);
            else if (sew == 16) vector::perform_compare_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl, eq_f);
            else if (sew == 32) vector::perform_compare_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl, eq_f);
            else vector::perform_compare_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl, eq_f);
            return;

        // VMSNE
        case isa::OperationId::VMSNE_VV:
            if (sew == 8) vector::perform_compare_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, ne_f);
            else if (sew == 16) vector::perform_compare_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, ne_f);
            else if (sew == 32) vector::perform_compare_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, ne_f);
            else vector::perform_compare_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, ne_f);
            return;
        case isa::OperationId::VMSNE_VX:
            if (sew == 8) vector::perform_compare_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, ne_f);
            else if (sew == 16) vector::perform_compare_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, ne_f);
            else if (sew == 32) vector::perform_compare_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, ne_f);
            else vector::perform_compare_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, ne_f);
            return;
        case isa::OperationId::VMSNE_VI:
            if (sew == 8) vector::perform_compare_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl, ne_f);
            else if (sew == 16) vector::perform_compare_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl, ne_f);
            else if (sew == 32) vector::perform_compare_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl, ne_f);
            else vector::perform_compare_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl, ne_f);
            return;

        // VMSLT
        case isa::OperationId::VMSLT_VV:
            if (sew == 8) vector::perform_compare_vv<int8_t>(cpu, rd, rs1, rs2, vm, vl, lt_f);
            else if (sew == 16) vector::perform_compare_vv<int16_t>(cpu, rd, rs1, rs2, vm, vl, lt_f);
            else if (sew == 32) vector::perform_compare_vv<int32_t>(cpu, rd, rs1, rs2, vm, vl, lt_f);
            else vector::perform_compare_vv<int64_t>(cpu, rd, rs1, rs2, vm, vl, lt_f);
            return;
        case isa::OperationId::VMSLT_VX:
            if (sew == 8) vector::perform_compare_vx<int8_t>(cpu, rd, rs1_val, rs2, vm, vl, lt_f);
            else if (sew == 16) vector::perform_compare_vx<int16_t>(cpu, rd, rs1_val, rs2, vm, vl, lt_f);
            else if (sew == 32) vector::perform_compare_vx<int32_t>(cpu, rd, rs1_val, rs2, vm, vl, lt_f);
            else vector::perform_compare_vx<int64_t>(cpu, rd, rs1_val, rs2, vm, vl, lt_f);
            return;

        // VMSLTU
        case isa::OperationId::VMSLTU_VV:
            if (sew == 8) vector::perform_compare_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, ltu_f);
            else if (sew == 16) vector::perform_compare_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, ltu_f);
            else if (sew == 32) vector::perform_compare_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, ltu_f);
            else vector::perform_compare_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, ltu_f);
            return;
        case isa::OperationId::VMSLTU_VX:
            if (sew == 8) vector::perform_compare_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, ltu_f);
            else if (sew == 16) vector::perform_compare_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, ltu_f);
            else if (sew == 32) vector::perform_compare_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, ltu_f);
            else vector::perform_compare_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, ltu_f);
            return;

        // VMSLE
        case isa::OperationId::VMSLE_VV:
            if (sew == 8) vector::perform_compare_vv<int8_t>(cpu, rd, rs1, rs2, vm, vl, le_f);
            else if (sew == 16) vector::perform_compare_vv<int16_t>(cpu, rd, rs1, rs2, vm, vl, le_f);
            else if (sew == 32) vector::perform_compare_vv<int32_t>(cpu, rd, rs1, rs2, vm, vl, le_f);
            else vector::perform_compare_vv<int64_t>(cpu, rd, rs1, rs2, vm, vl, le_f);
            return;
        case isa::OperationId::VMSLE_VX:
            if (sew == 8) vector::perform_compare_vx<int8_t>(cpu, rd, rs1_val, rs2, vm, vl, le_f);
            else if (sew == 16) vector::perform_compare_vx<int16_t>(cpu, rd, rs1_val, rs2, vm, vl, le_f);
            else if (sew == 32) vector::perform_compare_vx<int32_t>(cpu, rd, rs1_val, rs2, vm, vl, le_f);
            else vector::perform_compare_vx<int64_t>(cpu, rd, rs1_val, rs2, vm, vl, le_f);
            return;
        case isa::OperationId::VMSLE_VI:
            if (sew == 8) vector::perform_compare_vi<int8_t>(cpu, rd, simm5, rs2, vm, vl, le_f);
            else if (sew == 16) vector::perform_compare_vi<int16_t>(cpu, rd, simm5, rs2, vm, vl, le_f);
            else if (sew == 32) vector::perform_compare_vi<int32_t>(cpu, rd, simm5, rs2, vm, vl, le_f);
            else vector::perform_compare_vi<int64_t>(cpu, rd, simm5, rs2, vm, vl, le_f);
            return;

        // VMSLEU
        case isa::OperationId::VMSLEU_VV:
            if (sew == 8) vector::perform_compare_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, leu_f);
            else if (sew == 16) vector::perform_compare_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, leu_f);
            else if (sew == 32) vector::perform_compare_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, leu_f);
            else vector::perform_compare_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, leu_f);
            return;
        case isa::OperationId::VMSLEU_VX:
            if (sew == 8) vector::perform_compare_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, leu_f);
            else if (sew == 16) vector::perform_compare_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, leu_f);
            else if (sew == 32) vector::perform_compare_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, leu_f);
            else vector::perform_compare_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, leu_f);
            return;
        case isa::OperationId::VMSLEU_VI:
            if (sew == 8) vector::perform_compare_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl, leu_f);
            else if (sew == 16) vector::perform_compare_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl, leu_f);
            else if (sew == 32) vector::perform_compare_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl, leu_f);
            else vector::perform_compare_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl, leu_f);
            return;

        // VMSGT
        case isa::OperationId::VMSGT_VX:
            if (sew == 8) vector::perform_compare_vx<int8_t>(cpu, rd, rs1_val, rs2, vm, vl, gt_f);
            else if (sew == 16) vector::perform_compare_vx<int16_t>(cpu, rd, rs1_val, rs2, vm, vl, gt_f);
            else if (sew == 32) vector::perform_compare_vx<int32_t>(cpu, rd, rs1_val, rs2, vm, vl, gt_f);
            else vector::perform_compare_vx<int64_t>(cpu, rd, rs1_val, rs2, vm, vl, gt_f);
            return;
        case isa::OperationId::VMSGT_VI:
            if (sew == 8) vector::perform_compare_vi<int8_t>(cpu, rd, simm5, rs2, vm, vl, gt_f);
            else if (sew == 16) vector::perform_compare_vi<int16_t>(cpu, rd, simm5, rs2, vm, vl, gt_f);
            else if (sew == 32) vector::perform_compare_vi<int32_t>(cpu, rd, simm5, rs2, vm, vl, gt_f);
            else vector::perform_compare_vi<int64_t>(cpu, rd, simm5, rs2, vm, vl, gt_f);
            return;

        // VMSGTU
        case isa::OperationId::VMSGTU_VX:
            if (sew == 8) vector::perform_compare_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, gtu_f);
            else if (sew == 16) vector::perform_compare_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, gtu_f);
            else if (sew == 32) vector::perform_compare_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, gtu_f);
            else vector::perform_compare_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, gtu_f);
            return;
        case isa::OperationId::VMSGTU_VI:
            if (sew == 8) vector::perform_compare_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl, gtu_f);
            else if (sew == 16) vector::perform_compare_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl, gtu_f);
            else if (sew == 32) vector::perform_compare_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl, gtu_f);
            else vector::perform_compare_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl, gtu_f);
            return;

        // VMACC / VMADD / VNMSAC / VNSUB (VV & VX)
        case isa::OperationId::VMACC_VV:
        case isa::OperationId::VMADD_VV:
        case isa::OperationId::VNMSAC_VV:
        case isa::OperationId::VNSUB_VV: {
            bool overwrite_acc = (op_id == isa::OperationId::VMACC_VV || op_id == isa::OperationId::VNMSAC_VV);
            bool subtract = (op_id == isa::OperationId::VNMSAC_VV || op_id == isa::OperationId::VNSUB_VV);
            if (sew == 8) perform_mac_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, overwrite_acc, subtract);
            else if (sew == 16) perform_mac_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, overwrite_acc, subtract);
            else if (sew == 32) perform_mac_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, overwrite_acc, subtract);
            else perform_mac_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, overwrite_acc, subtract);
            return;
        }
        case isa::OperationId::VMACC_VX:
        case isa::OperationId::VMADD_VX:
        case isa::OperationId::VNMSAC_VX:
        case isa::OperationId::VNSUB_VX: {
            bool overwrite_acc = (op_id == isa::OperationId::VMACC_VX || op_id == isa::OperationId::VNMSAC_VX);
            bool subtract = (op_id == isa::OperationId::VNMSAC_VX || op_id == isa::OperationId::VNSUB_VX);
            if (sew == 8) perform_mac_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, overwrite_acc, subtract);
            else if (sew == 16) perform_mac_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, overwrite_acc, subtract);
            else if (sew == 32) perform_mac_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, overwrite_acc, subtract);
            else perform_mac_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, overwrite_acc, subtract);
            return;
        }

        // VWMACCU / VWMACC / VWMACCUS / VWMACCSU
        case isa::OperationId::VWMACCU_VV:
            if (sew == 8) perform_widening_mac_vv<uint16_t, uint8_t, uint8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) perform_widening_mac_vv<uint32_t, uint16_t, uint16_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 32) perform_widening_mac_vv<uint64_t, uint32_t, uint32_t>(cpu, rd, rs1, rs2, vm, vl);
            return;
        case isa::OperationId::VWMACCU_VX:
            if (sew == 8) perform_widening_mac_vx<uint16_t, uint8_t, uint8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) perform_widening_mac_vx<uint32_t, uint16_t, uint16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 32) perform_widening_mac_vx<uint64_t, uint32_t, uint32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            return;
        case isa::OperationId::VWMACC_VV:
            if (sew == 8) perform_widening_mac_vv<int16_t, int8_t, int8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) perform_widening_mac_vv<int32_t, int16_t, int16_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 32) perform_widening_mac_vv<int64_t, int32_t, int32_t>(cpu, rd, rs1, rs2, vm, vl);
            return;
        case isa::OperationId::VWMACC_VX:
            if (sew == 8) perform_widening_mac_vx<int16_t, int8_t, int8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) perform_widening_mac_vx<int32_t, int16_t, int16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 32) perform_widening_mac_vx<int64_t, int32_t, int32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            return;
        case isa::OperationId::VWMACCUS_VX:
            if (sew == 8) perform_widening_mac_vx<int16_t, uint8_t, int8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) perform_widening_mac_vx<int32_t, uint16_t, int16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 32) perform_widening_mac_vx<int64_t, uint32_t, int32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            return;
        case isa::OperationId::VWMACCSU_VV:
            if (sew == 8) perform_widening_mac_vv<int16_t, int8_t, uint8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) perform_widening_mac_vv<int32_t, int16_t, uint16_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 32) perform_widening_mac_vv<int64_t, int32_t, uint32_t>(cpu, rd, rs1, rs2, vm, vl);
            return;
        case isa::OperationId::VWMACCSU_VX:
            if (sew == 8) perform_widening_mac_vx<int16_t, int8_t, uint8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) perform_widening_mac_vx<int32_t, int16_t, uint16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 32) perform_widening_mac_vx<int64_t, int32_t, uint32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            return;

        // VREDSUM
        case isa::OperationId::VREDSUM_VS:
            if (sew == 8) execute_vredsum_vs<uint8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) execute_vredsum_vs<uint16_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 32) execute_vredsum_vs<uint32_t>(cpu, rd, rs1, rs2, vm, vl);
            else execute_vredsum_vs<uint64_t>(cpu, rd, rs1, rs2, vm, vl);
            return;

        // VWMUL
        case isa::OperationId::VWMUL_VV:
            if (sew == 8) execute_vwmul_vv<int16_t, int8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) execute_vwmul_vv<int32_t, int16_t>(cpu, rd, rs1, rs2, vm, vl);
            else execute_vwmul_vv<int64_t, int32_t>(cpu, rd, rs1, rs2, vm, vl);
            return;
        case isa::OperationId::VWMUL_VX:
            if (sew == 8) execute_vwmul_vx<int16_t, int8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) execute_vwmul_vx<int32_t, int16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else execute_vwmul_vx<int64_t, int32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            return;

        // VSBC
        case isa::OperationId::VSBC_VVM:
            if (sew == 8) execute_vsbc_vv<uint8_t>(cpu, rd, rs1, rs2, vl);
            else if (sew == 16) execute_vsbc_vv<uint16_t>(cpu, rd, rs1, rs2, vl);
            else if (sew == 32) execute_vsbc_vv<uint32_t>(cpu, rd, rs1, rs2, vl);
            else execute_vsbc_vv<uint64_t>(cpu, rd, rs1, rs2, vl);
            return;
        case isa::OperationId::VSBC_VXM:
            if (sew == 8) execute_vsbc_vx<uint8_t>(cpu, rd, rs1_val, rs2, vl);
            else if (sew == 16) execute_vsbc_vx<uint16_t>(cpu, rd, rs1_val, rs2, vl);
            else if (sew == 32) execute_vsbc_vx<uint32_t>(cpu, rd, rs1_val, rs2, vl);
            else execute_vsbc_vx<uint64_t>(cpu, rd, rs1_val, rs2, vl);
            return;

        // VSEXT
        case isa::OperationId::VSEXT_VF2:
            if (sew == 16) execute_vsext<int16_t, int8_t>(cpu, rd, rs2, vm, vl);
            else if (sew == 32) execute_vsext<int32_t, int16_t>(cpu, rd, rs2, vm, vl);
            else if (sew == 64) execute_vsext<int64_t, int32_t>(cpu, rd, rs2, vm, vl);
            return;
        case isa::OperationId::VSEXT_VF4:
            if (sew == 32) execute_vsext<int32_t, int8_t>(cpu, rd, rs2, vm, vl);
            else if (sew == 64) execute_vsext<int64_t, int16_t>(cpu, rd, rs2, vm, vl);
            return;
        case isa::OperationId::VSEXT_VF8:
            if (sew == 64) execute_vsext<int64_t, int8_t>(cpu, rd, rs2, vm, vl);
            return;

        // VZEXT
        case isa::OperationId::VZEXT_VF2:
            if (sew == 16) execute_vsext<uint16_t, uint8_t>(cpu, rd, rs2, vm, vl);
            else if (sew == 32) execute_vsext<uint32_t, uint16_t>(cpu, rd, rs2, vm, vl);
            else if (sew == 64) execute_vsext<uint64_t, uint32_t>(cpu, rd, rs2, vm, vl);
            return;
        case isa::OperationId::VZEXT_VF4:
            if (sew == 32) execute_vsext<uint32_t, uint8_t>(cpu, rd, rs2, vm, vl);
            else if (sew == 64) execute_vsext<uint64_t, uint16_t>(cpu, rd, rs2, vm, vl);
            return;
        case isa::OperationId::VZEXT_VF8:
            if (sew == 64) execute_vsext<uint64_t, uint8_t>(cpu, rd, rs2, vm, vl);
            return;

        // VWADD
        case isa::OperationId::VWADD_VV:
            if (sew == 8) execute_vwadd_vv<int16_t, int8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) execute_vwadd_vv<int32_t, int16_t>(cpu, rd, rs1, rs2, vm, vl);
            else execute_vwadd_vv<int64_t, int32_t>(cpu, rd, rs1, rs2, vm, vl);
            return;
        case isa::OperationId::VWADD_VX:
            if (sew == 8) execute_vwadd_vx<int16_t, int8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) execute_vwadd_vx<int32_t, int16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else execute_vwadd_vx<int64_t, int32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            return;
        case isa::OperationId::VWADD_WV:
            if (sew == 8) execute_vwadd_wv<int16_t, int8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) execute_vwadd_wv<int32_t, int16_t>(cpu, rd, rs1, rs2, vm, vl);
            else execute_vwadd_wv<int64_t, int32_t>(cpu, rd, rs1, rs2, vm, vl);
            return;
        case isa::OperationId::VWADD_WX:
            if (sew == 8) execute_vwadd_wx<int16_t, int8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) execute_vwadd_wx<int32_t, int16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else execute_vwadd_wx<int64_t, int32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            return;

        // VWADDU
        case isa::OperationId::VWADDU_VV:
            if (sew == 8) execute_vwadd_vv<uint16_t, uint8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) execute_vwadd_vv<uint32_t, uint16_t>(cpu, rd, rs1, rs2, vm, vl);
            else execute_vwadd_vv<uint64_t, uint32_t>(cpu, rd, rs1, rs2, vm, vl);
            return;
        case isa::OperationId::VWADDU_VX:
            if (sew == 8) execute_vwadd_vx<uint16_t, uint8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) execute_vwadd_vx<uint32_t, uint16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else execute_vwadd_vx<uint64_t, uint32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            return;
        case isa::OperationId::VWADDU_WV:
            if (sew == 8) execute_vwadd_wv<uint16_t, uint8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) execute_vwadd_wv<uint32_t, uint16_t>(cpu, rd, rs1, rs2, vm, vl);
            else execute_vwadd_wv<uint64_t, uint32_t>(cpu, rd, rs1, rs2, vm, vl);
            return;
        case isa::OperationId::VWADDU_WX:
            if (sew == 8) execute_vwadd_wx<uint16_t, uint8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) execute_vwadd_wx<uint32_t, uint16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else execute_vwadd_wx<uint64_t, uint32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            return;

        // VWSUB
        case isa::OperationId::VWSUB_VV:
            if (sew == 8) execute_vwsub_vv<int16_t, int8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) execute_vwsub_vv<int32_t, int16_t>(cpu, rd, rs1, rs2, vm, vl);
            else execute_vwsub_vv<int64_t, int32_t>(cpu, rd, rs1, rs2, vm, vl);
            return;
        case isa::OperationId::VWSUB_VX:
            if (sew == 8) execute_vwsub_vx<int16_t, int8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) execute_vwsub_vx<int32_t, int16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else execute_vwsub_vx<int64_t, int32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            return;
        case isa::OperationId::VWSUB_WV:
            if (sew == 8) execute_vwsub_wv<int16_t, int8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) execute_vwsub_wv<int32_t, int16_t>(cpu, rd, rs1, rs2, vm, vl);
            else execute_vwsub_wv<int64_t, int32_t>(cpu, rd, rs1, rs2, vm, vl);
            return;
        case isa::OperationId::VWSUB_WX:
            if (sew == 8) execute_vwsub_wx<int16_t, int8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) execute_vwsub_wx<int32_t, int16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else execute_vwsub_wx<int64_t, int32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            return;

        // VWSUBU
        case isa::OperationId::VWSUBU_VV:
            if (sew == 8) execute_vwsub_vv<uint16_t, uint8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) execute_vwsub_vv<uint32_t, uint16_t>(cpu, rd, rs1, rs2, vm, vl);
            else execute_vwsub_vv<uint64_t, uint32_t>(cpu, rd, rs1, rs2, vm, vl);
            return;
        case isa::OperationId::VWSUBU_VX:
            if (sew == 8) execute_vwsub_vx<uint16_t, uint8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) execute_vwsub_vx<uint32_t, uint16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else execute_vwsub_vx<uint64_t, uint32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            return;
        case isa::OperationId::VWSUBU_WV:
            if (sew == 8) execute_vwsub_wv<uint16_t, uint8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) execute_vwsub_wv<uint32_t, uint16_t>(cpu, rd, rs1, rs2, vm, vl);
            else execute_vwsub_wv<uint64_t, uint32_t>(cpu, rd, rs1, rs2, vm, vl);
            return;
        case isa::OperationId::VWSUBU_WX:
            if (sew == 8) execute_vwsub_wx<uint16_t, uint8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) execute_vwsub_wx<uint32_t, uint16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else execute_vwsub_wx<uint64_t, uint32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            return;

        // VWMULU
        case isa::OperationId::VWMULU_VV:
            if (sew == 8) execute_vwmul_vv<uint16_t, uint8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) execute_vwmul_vv<uint32_t, uint16_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 64) execute_vwmul_vv<uint64_t, uint32_t>(cpu, rd, rs1, rs2, vm, vl);
            return;
        case isa::OperationId::VWMULU_VX:
            if (sew == 8) execute_vwmul_vx<uint16_t, uint8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) execute_vwmul_vx<uint32_t, uint16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 64) execute_vwmul_vx<uint64_t, uint32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            return;

        // VWMULSU
        case isa::OperationId::VWMULSU_VV:
            if (sew == 8) execute_vwmulsu_vv<int16_t, int8_t, uint8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) execute_vwmulsu_vv<int32_t, int16_t, uint16_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 64) execute_vwmulsu_vv<int64_t, int32_t, uint32_t>(cpu, rd, rs1, rs2, vm, vl);
            return;
        case isa::OperationId::VWMULSU_VX:
            if (sew == 8) execute_vwmulsu_vx<int16_t, int8_t, uint8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) execute_vwmulsu_vx<int32_t, int16_t, uint16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 64) execute_vwmulsu_vx<int64_t, int32_t, uint32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            return;

        // VWREDSUM
        case isa::OperationId::VWREDSUM_VS:
            if (sew == 8) execute_vwredsum_vs<int16_t, int8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) execute_vwredsum_vs<int32_t, int16_t>(cpu, rd, rs1, rs2, vm, vl);
            else execute_vwredsum_vs<int64_t, int32_t>(cpu, rd, rs1, rs2, vm, vl);
            return;
        case isa::OperationId::VWREDSUMU_VS:
            if (sew == 8) execute_vwredsum_vs<uint16_t, uint8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) execute_vwredsum_vs<uint32_t, uint16_t>(cpu, rd, rs1, rs2, vm, vl);
            else execute_vwredsum_vs<uint64_t, uint32_t>(cpu, rd, rs1, rs2, vm, vl);
            return;

        // VADC
        case isa::OperationId::VADC_VVM:
            if (sew == 8) execute_vadc_vv<uint8_t>(cpu, rd, rs1, rs2, vl);
            else if (sew == 16) execute_vadc_vv<uint16_t>(cpu, rd, rs1, rs2, vl);
            else if (sew == 32) execute_vadc_vv<uint32_t>(cpu, rd, rs1, rs2, vl);
            else execute_vadc_vv<uint64_t>(cpu, rd, rs1, rs2, vl);
            return;
        case isa::OperationId::VADC_VXM:
            if (sew == 8) execute_vadc_vx<uint8_t>(cpu, rd, rs1_val, rs2, vl);
            else if (sew == 16) execute_vadc_vx<uint16_t>(cpu, rd, rs1_val, rs2, vl);
            else if (sew == 32) execute_vadc_vx<uint32_t>(cpu, rd, rs1_val, rs2, vl);
            else execute_vadc_vx<uint64_t>(cpu, rd, rs1_val, rs2, vl);
            return;
        case isa::OperationId::VADC_VIM:
            if (sew == 8) execute_vadc_vi<uint8_t>(cpu, rd, simm5, rs2, vl);
            else if (sew == 16) execute_vadc_vi<uint16_t>(cpu, rd, simm5, rs2, vl);
            else if (sew == 32) execute_vadc_vi<uint32_t>(cpu, rd, simm5, rs2, vl);
            else execute_vadc_vi<uint64_t>(cpu, rd, simm5, rs2, vl);
            return;

        // VMADC
        case isa::OperationId::VMADC_VV:
        case isa::OperationId::VMADC_VVM:
            if (sew == 8) execute_vmadc_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) execute_vmadc_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 32) execute_vmadc_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl);
            else execute_vmadc_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl);
            return;
        case isa::OperationId::VMADC_VX:
        case isa::OperationId::VMADC_VXM:
            if (sew == 8) execute_vmadc_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) execute_vmadc_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 32) execute_vmadc_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else execute_vmadc_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl);
            return;
        case isa::OperationId::VMADC_VI:
        case isa::OperationId::VMADC_VIM:
            if (sew == 8) execute_vmadc_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl);
            else if (sew == 16) execute_vmadc_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl);
            else if (sew == 32) execute_vmadc_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl);
            else execute_vmadc_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl);
            return;

        // VMSBC
        case isa::OperationId::VMSBC_VV:
        case isa::OperationId::VMSBC_VVM:
            if (sew == 8) execute_vmsbc_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) execute_vmsbc_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 32) execute_vmsbc_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl);
            else execute_vmsbc_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl);
            return;
        case isa::OperationId::VMSBC_VX:
        case isa::OperationId::VMSBC_VXM:
            if (sew == 8) execute_vmsbc_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) execute_vmsbc_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 32) execute_vmsbc_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else execute_vmsbc_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl);
            return;

        // Mask instructions
        case isa::OperationId::VMSBF_M:
            execute_vmsbf_m(cpu, rd, rs2, vm, vl);
            return;
        case isa::OperationId::VMSIF_M:
            execute_vmsif_m(cpu, rd, rs2, vm, vl);
            return;
        case isa::OperationId::VMSOF_M:
            execute_vmsof_m(cpu, rd, rs2, vm, vl);
            return;
        case isa::OperationId::VFIRST_M:
            execute_vfirst_m(cpu, rd, rs2, vm, vl);
            return;
        case isa::OperationId::VCPOP_M:
            execute_vcpop_m(cpu, rd, rs2, vm, vl);
            return;
        case isa::OperationId::VMAND_MM:
        case isa::OperationId::VMNAND_MM:
        case isa::OperationId::VMANDN_MM:
        case isa::OperationId::VMXOR_MM:
        case isa::OperationId::VMOR_MM:
        case isa::OperationId::VMNOR_MM:
        case isa::OperationId::VMORN_MM:
        case isa::OperationId::VMXNOR_MM:
            execute_mask_logical(cpu, rd, rs1, rs2, vl, op_id);
            return;
        case isa::OperationId::VIOTA_M:
            if (sew == 8) execute_viota_m<uint8_t>(cpu, rd, rs2, vm, vl);
            else if (sew == 16) execute_viota_m<uint16_t>(cpu, rd, rs2, vm, vl);
            else if (sew == 32) execute_viota_m<uint32_t>(cpu, rd, rs2, vm, vl);
            else execute_viota_m<uint64_t>(cpu, rd, rs2, vm, vl);
            return;

        // VANDN
        case isa::OperationId::VANDN_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, andn_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, andn_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, andn_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, andn_f);
            return;
        case isa::OperationId::VANDN_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, andn_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, andn_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, andn_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, andn_f);
            return;

        // VROL
        case isa::OperationId::VROL_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, rol_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, rol_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, rol_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, rol_f);
            return;
        case isa::OperationId::VROL_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, rol_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, rol_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, rol_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, rol_f);
            return;

        // VROR
        case isa::OperationId::VROR_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, ror_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, ror_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, ror_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, ror_f);
            return;
        case isa::OperationId::VROR_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, ror_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, ror_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, ror_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, ror_f);
            return;
        case isa::OperationId::VROR_VI:
            if (sew == 8) vector::perform_vi<uint8_t>(cpu, rd, simm5, rs2, vm, vl, ror_f);
            else if (sew == 16) vector::perform_vi<uint16_t>(cpu, rd, simm5, rs2, vm, vl, ror_f);
            else if (sew == 32) vector::perform_vi<uint32_t>(cpu, rd, simm5, rs2, vm, vl, ror_f);
            else vector::perform_vi<uint64_t>(cpu, rd, simm5, rs2, vm, vl, ror_f);
            return;

        // VCLMUL
        case isa::OperationId::VCLMUL_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, clmul_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, clmul_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, clmul_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, clmul_f);
            return;
        case isa::OperationId::VCLMUL_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, clmul_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, clmul_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, clmul_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, clmul_f);
            return;

        // VCLMULH
        case isa::OperationId::VCLMULH_VV:
            if (sew == 8) vector::perform_vv<uint8_t>(cpu, rd, rs1, rs2, vm, vl, clmulh_f);
            else if (sew == 16) vector::perform_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl, clmulh_f);
            else if (sew == 32) vector::perform_vv<uint32_t>(cpu, rd, rs1, rs2, vm, vl, clmulh_f);
            else vector::perform_vv<uint64_t>(cpu, rd, rs1, rs2, vm, vl, clmulh_f);
            return;
        case isa::OperationId::VCLMULH_VX:
            if (sew == 8) vector::perform_vx<uint8_t>(cpu, rd, rs1_val, rs2, vm, vl, clmulh_f);
            else if (sew == 16) vector::perform_vx<uint16_t>(cpu, rd, rs1_val, rs2, vm, vl, clmulh_f);
            else if (sew == 32) vector::perform_vx<uint32_t>(cpu, rd, rs1_val, rs2, vm, vl, clmulh_f);
            else vector::perform_vx<uint64_t>(cpu, rd, rs1_val, rs2, vm, vl, clmulh_f);
            return;

        // VCLZ
        case isa::OperationId::VCLZ_V:
            if (sew == 8) execute_vclz<uint8_t>(cpu, rd, rs2, vm, vl);
            else if (sew == 16) execute_vclz<uint16_t>(cpu, rd, rs2, vm, vl);
            else if (sew == 32) execute_vclz<uint32_t>(cpu, rd, rs2, vm, vl);
            else execute_vclz<uint64_t>(cpu, rd, rs2, vm, vl);
            return;

        // VCTZ
        case isa::OperationId::VCTZ_V:
            if (sew == 8) execute_vctz<uint8_t>(cpu, rd, rs2, vm, vl);
            else if (sew == 16) execute_vctz<uint16_t>(cpu, rd, rs2, vm, vl);
            else if (sew == 32) execute_vctz<uint32_t>(cpu, rd, rs2, vm, vl);
            else execute_vctz<uint64_t>(cpu, rd, rs2, vm, vl);
            return;

        // VCPOP.V
        case isa::OperationId::VCPOP_V:
            if (sew == 8) execute_vcpop_v<uint8_t>(cpu, rd, rs2, vm, vl);
            else if (sew == 16) execute_vcpop_v<uint16_t>(cpu, rd, rs2, vm, vl);
            else if (sew == 32) execute_vcpop_v<uint32_t>(cpu, rd, rs2, vm, vl);
            else execute_vcpop_v<uint64_t>(cpu, rd, rs2, vm, vl);
            return;

        // VBREV
        case isa::OperationId::VBREV_V:
            if (sew == 8) {
                auto f = []<typename T>(T a, T) -> T { return bit_reverse<T>(a); };
                vector::perform_vv<uint8_t>(cpu, rd, rs2, rs2, vm, vl, f);
            } else if (sew == 16) {
                auto f = []<typename T>(T a, T) -> T { return bit_reverse<T>(a); };
                vector::perform_vv<uint16_t>(cpu, rd, rs2, rs2, vm, vl, f);
            } else if (sew == 32) {
                auto f = []<typename T>(T a, T) -> T { return bit_reverse<T>(a); };
                vector::perform_vv<uint32_t>(cpu, rd, rs2, rs2, vm, vl, f);
            } else {
                auto f = []<typename T>(T a, T) -> T { return bit_reverse<T>(a); };
                vector::perform_vv<uint64_t>(cpu, rd, rs2, rs2, vm, vl, f);
            }
            return;

        // VBREV8
        case isa::OperationId::VBREV8_V:
            if (sew == 8) {
                auto f = []<typename T>(T a, T) -> T { return bit_reverse_bytes<T>(a); };
                vector::perform_vv<uint8_t>(cpu, rd, rs2, rs2, vm, vl, f);
            } else if (sew == 16) {
                auto f = []<typename T>(T a, T) -> T { return bit_reverse_bytes<T>(a); };
                vector::perform_vv<uint16_t>(cpu, rd, rs2, rs2, vm, vl, f);
            } else if (sew == 32) {
                auto f = []<typename T>(T a, T) -> T { return bit_reverse_bytes<T>(a); };
                vector::perform_vv<uint32_t>(cpu, rd, rs2, rs2, vm, vl, f);
            } else {
                auto f = []<typename T>(T a, T) -> T { return bit_reverse_bytes<T>(a); };
                vector::perform_vv<uint64_t>(cpu, rd, rs2, rs2, vm, vl, f);
            }
            return;

        // VREV8
        case isa::OperationId::VREV8_V:
            if (sew == 8) {
                auto f = []<typename T>(T a, T) -> T { return byteswap_element<T>(a); };
                vector::perform_vv<uint8_t>(cpu, rd, rs2, rs2, vm, vl, f);
            } else if (sew == 16) {
                auto f = []<typename T>(T a, T) -> T { return byteswap_element<T>(a); };
                vector::perform_vv<uint16_t>(cpu, rd, rs2, rs2, vm, vl, f);
            } else if (sew == 32) {
                auto f = []<typename T>(T a, T) -> T { return byteswap_element<T>(a); };
                vector::perform_vv<uint32_t>(cpu, rd, rs2, rs2, vm, vl, f);
            } else {
                auto f = []<typename T>(T a, T) -> T { return byteswap_element<T>(a); };
                vector::perform_vv<uint64_t>(cpu, rd, rs2, rs2, vm, vl, f);
            }
            return;

        // VWSLL
        case isa::OperationId::VWSLL_VV:
            if (sew == 8) execute_vwsll_vv<int16_t, int8_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 16) execute_vwsll_vv<int32_t, int16_t>(cpu, rd, rs1, rs2, vm, vl);
            else execute_vwsll_vv<int64_t, int32_t>(cpu, rd, rs1, rs2, vm, vl);
            return;
        case isa::OperationId::VWSLL_VX:
            if (sew == 8) execute_vwsll_vx<int16_t, int8_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else if (sew == 16) execute_vwsll_vx<int32_t, int16_t>(cpu, rd, rs1_val, rs2, vm, vl);
            else execute_vwsll_vx<int64_t, int32_t>(cpu, rd, rs1_val, rs2, vm, vl);
            return;
        case isa::OperationId::VWSLL_VI:
            if (sew == 8) execute_vwsll_vi<int16_t, int8_t>(cpu, rd, simm5, rs2, vm, vl);
            else if (sew == 16) execute_vwsll_vi<int32_t, int16_t>(cpu, rd, simm5, rs2, vm, vl);
            else execute_vwsll_vi<int64_t, int32_t>(cpu, rd, simm5, rs2, vm, vl);
            return;

        default:
            break;
    }
}

} // namespace simrv::execute
