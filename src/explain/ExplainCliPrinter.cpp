/**
 * @file ExplainCliPrinter.cpp
 * @brief Educational CLI formatter and ASCII datapath diagram printer for --explain.
 */
#include <unistd.h>

#include <format>
#include <print>
#include <string>
#include <string_view>
#include <utility>

#include "simrv/explain/InstructionExplainer.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/isa/RegisterNames.hpp"
#include "simrv/pipeline/OperationInfo.hpp"
#include "simrv/util/FormatUtil.hpp"

namespace simrv::explain {

using namespace simrv::util::ansi;
using simrv::isa::InstFormat;
using simrv::isa::Opcode;
using simrv::isa::OperationId;
using enum simrv::isa::OperationId;

namespace {

constexpr auto& ABI_NAMES = simrv::isa::kGprPedagogicalNames;
constexpr auto& FP_ABI_NAMES = simrv::isa::kFprAbiNames;

auto reg_name(uint32_t r, bool is_fp = false) -> std::string {
    if (r >= 32) return std::format("r{}", r);
    if (is_fp) {
        return std::format("f{} ({})", r, FP_ABI_NAMES[r]);
    } else {
        return std::format("x{} ({})", r, ABI_NAMES[r]);
    }
}

auto c_code(std::string_view ansi_code, bool use_color) -> std::string_view {
    return use_color ? ansi_code : "";
}

auto print_r_format(uint32_t funct7_val, uint32_t rs2_val, uint32_t rs1_val, uint32_t funct3_val,
                    uint32_t rd_val, Opcode op, bool use_color) -> void {
    auto c = [use_color](std::string_view code) -> std::string_view {
        return c_code(code, use_color);
    };
    std::println("Visual Bit Fields Breakdown (R-Type format):");
    std::println("  31         25 24      20 19      15 1412 11       7 6           0");
    std::println("  +------------+----------+----------+----+----------+-------------+");
    std::println("  |   funct7   |   rs2    |   rs1    | f3 |    rd    |   opcode    |");
    std::println("  +------------+----------+----------+----+----------+-------------+");
    std::print(
        "  |   {}{:07b}{}  |  {}{:05b}{}   |  {}{:05b}{}   |{}{:03b}{} |  {}{:05b}{}   |   "
        "{}{:07b}{}   |\n",
        c(kBrightRed), funct7_val, c(kReset), c(kBrightMagenta), rs2_val, c(kReset),
        c(kBrightYellow), rs1_val, c(kReset), c(kBrightCyan), funct3_val, c(kReset),
        c(kBrightGreen), rd_val, c(kReset), c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  +------------+----------+----------+----+----------+-------------+");

    std::println("\nField Decoded Meanings:");
    std::println("  opcode  : {}0x{:02X}{} ({:07b}) -> Major Opcode", c(kBrightBlue),
                 std::to_underlying(op), c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  rd      : {}x{:<2}{} ({:05b}) -> Destination Register: {}", c(kBrightGreen),
                 rd_val, c(kBrightGreen), rd_val, reg_name(rd_val), c(kReset));
    std::println("  funct3  : {}0x{:01X}{}  ({:03b}) -> Sub-function selector", c(kBrightCyan),
                 funct3_val, c(kBrightCyan), funct3_val, c(kReset));
    std::println("  rs1     : {}x{:<2}{} ({:05b}) -> Source Register 1: {}", c(kBrightYellow),
                 rs1_val, c(kBrightYellow), rs1_val, reg_name(rs1_val), c(kReset));
    std::println("  rs2     : {}x{:<2}{} ({:05b}) -> Source Register 2: {}", c(kBrightMagenta),
                 rs2_val, c(kBrightMagenta), rs2_val, reg_name(rs2_val), c(kReset));
    std::println("  funct7  : {}0x{:02X}{} ({:07b}) -> Operations modifier", c(kBrightRed),
                 funct7_val, c(kBrightRed), funct7_val, c(kReset));
}

auto print_i_format(uint32_t imm_bits, int32_t imm_val, uint32_t rs1_val, uint32_t funct3_val,
                    uint32_t rd_val, Opcode op, bool use_color) -> void {
    auto c = [use_color](std::string_view code) -> std::string_view {
        return c_code(code, use_color);
    };
    std::println("Visual Bit Fields Breakdown (I-Type format):");
    std::println("  31                   20 19      15 1412 11       7 6           0");
    std::println("  +----------------------+----------+----+----------+-------------+");
    std::println("  |      immediate       |   rs1    | f3 |    rd    |   opcode    |");
    std::println("  +----------------------+----------+----+----------+-------------+");
    std::print(
        "  |     {}{:012b}{}     |  {}{:05b}{}   |{}{:03b}{} |  {}{:05b}{}   |   {}{:07b}{}   |\n",
        c(kBrightRed), imm_bits, c(kReset), c(kBrightYellow), rs1_val, c(kReset), c(kBrightCyan),
        funct3_val, c(kReset), c(kBrightGreen), rd_val, c(kReset), c(kBrightBlue),
        std::to_underlying(op), c(kReset));
    std::println("  +----------------------+----------+----+----------+-------------+");

    std::println("\nField Decoded Meanings:");
    std::println("  opcode  : {}0x{:02X}{} ({:07b}) -> Major Opcode", c(kBrightBlue),
                 std::to_underlying(op), c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  rd      : {}x{:<2}{} ({:05b}) -> Destination Register: {}", c(kBrightGreen),
                 rd_val, c(kBrightGreen), rd_val, reg_name(rd_val), c(kReset));
    std::println("  funct3  : {}0x{:01X}{}  ({:03b}) -> Sub-function selector", c(kBrightCyan),
                 funct3_val, c(kBrightCyan), funct3_val, c(kReset));
    std::println("  rs1     : {}x{:<2}{} ({:05b}) -> Source Register 1: {}", c(kBrightYellow),
                 rs1_val, c(kBrightYellow), rs1_val, reg_name(rs1_val), c(kReset));
    std::println("  imm     : {}0x{:03X}{}  ({:012b}) -> Sign-extended 12-bit Immediate",
                 c(kBrightRed), imm_bits, c(kBrightRed), imm_bits, c(kReset));

    std::println("\nImmediate Reconstruction:");
    std::println("  imm[11:0] = inst[31:20] = {:012b}", imm_bits);
    if constexpr (simrv::xlen::kIsXLen64) {
        std::println("  Sign-extended to 64 bits: {}{} (0x{:016X}){}", c(kBrightRed), imm_val,
                     static_cast<uint64_t>(imm_val), c(kReset));
        std::println("  Binary (64-bit view)    : {}{:064b}{}", c(kBrightRed),
                     static_cast<uint64_t>(imm_val), c(kReset));
    } else {
        std::println("  Sign-extended to 32 bits: {}{} (0x{:08X}){}", c(kBrightRed), imm_val,
                     static_cast<uint32_t>(imm_val), c(kReset));
        std::println("  Binary (32-bit view)    : {}{:032b}{}", c(kBrightRed),
                     static_cast<uint32_t>(imm_val), c(kReset));
    }
}

auto print_s_format(uint32_t imm_hi, uint32_t imm_lo, int32_t imm_val, uint32_t rs1_val,
                    uint32_t rs2_val, uint32_t funct3_val, Opcode op, bool use_color) -> void {
    auto c = [use_color](std::string_view code) -> std::string_view {
        return c_code(code, use_color);
    };
    std::println("Visual Bit Fields Breakdown (S-Type format):");
    std::println("  31         25 24      20 19      15 1412 11       7 6           0");
    std::println("  +------------+----------+----------+----+----------+-------------+");
    std::println("  |  imm[11:5] |   rs2    |   rs1    | f3 | imm[4:0] |   opcode    |");
    std::println("  +------------+----------+----------+----+----------+-------------+");
    std::print(
        "  |   {}{:07b}{}  |  {}{:05b}{}   |  {}{:05b}{}   |{}{:03b}{} |  {}{:05b}{}   |   "
        "{}{:07b}{}   |\n",
        c(kBrightRed), imm_hi, c(kReset), c(kBrightMagenta), rs2_val, c(kReset), c(kBrightYellow),
        rs1_val, c(kReset), c(kBrightCyan), funct3_val, c(kReset), c(kBrightRed), imm_lo, c(kReset),
        c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  +------------+----------+----------+----+----------+-------------+");

    std::println("\nField Decoded Meanings:");
    std::println("  opcode  : {}0x{:02X}{} ({:07b}) -> Major Opcode", c(kBrightBlue),
                 std::to_underlying(op), c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  imm_lo  : {}0x{:02X}{}   ({:05b}) -> Lower bits of immediate", c(kBrightRed),
                 imm_lo, c(kBrightRed), imm_lo, c(kReset));
    std::println("  funct3  : {}0x{:01X}{}  ({:03b}) -> Sub-function selector", c(kBrightCyan),
                 funct3_val, c(kBrightCyan), funct3_val, c(kReset));
    std::println("  rs1     : {}x{:<2}{} ({:05b}) -> Base Address Register: {}", c(kBrightYellow),
                 rs1_val, c(kBrightYellow), rs1_val, reg_name(rs1_val), c(kReset));
    std::println("  rs2     : {}x{:<2}{} ({:05b}) -> Source Register (Value to store): {}",
                 c(kBrightMagenta), rs2_val, c(kBrightMagenta), rs2_val, reg_name(rs2_val),
                 c(kReset));
    std::println("  imm_hi  : {}0x{:02X}{}   ({:07b}) -> Upper bits of immediate", c(kBrightRed),
                 imm_hi, c(kBrightRed), imm_hi, c(kReset));

    std::println("\nImmediate Reconstruction:");
    std::println("  imm[11:5] = inst[31:25] = {:07b}", imm_hi);
    std::println("  imm[4:0]  = inst[11:7]  = {:05b}", imm_lo);
    std::println("  Combined  = {:012b}", (imm_hi << 5) | imm_lo);
    if constexpr (simrv::xlen::kIsXLen64) {
        std::println("  Sign-extended to 64 bits: {}{} (0x{:016X}){}", c(kBrightRed), imm_val,
                     static_cast<uint64_t>(imm_val), c(kReset));
        std::println("  Binary (64-bit view)    : {}{:064b}{}", c(kBrightRed),
                     static_cast<uint64_t>(imm_val), c(kReset));
    } else {
        std::println("  Sign-extended to 32 bits: {}{} (0x{:08X}){}", c(kBrightRed), imm_val,
                     static_cast<uint32_t>(imm_val), c(kReset));
        std::println("  Binary (32-bit view)    : {}{:032b}{}", c(kBrightRed),
                     static_cast<uint32_t>(imm_val), c(kReset));
    }
}

auto print_b_format(uint32_t inst, uint32_t imm_hi, uint32_t imm_lo, int32_t imm_val,
                    uint32_t rs1_val, uint32_t rs2_val, uint32_t funct3_val, Opcode op,
                    bool use_color) -> void {
    auto c = [use_color](std::string_view code) -> std::string_view {
        return c_code(code, use_color);
    };
    std::println("Visual Bit Fields Breakdown (B-Type format):");
    std::println("  31         25 24      20 19      15 1412 11       7 6           0");
    std::println("  +------------+----------+----------+----+----------+-------------+");
    std::println("  |imm[12|10:5]|   rs2    |   rs1    | f3 |imm[4:1|11]|   opcode    |");
    std::println("  +------------+----------+----------+----+----------+-------------+");
    std::print(
        "  |   {}{:07b}{}  |  {}{:05b}{}   |  {}{:05b}{}   |{}{:03b}{} |  {}{:05b}{}   |   "
        "{}{:07b}{}   |\n",
        c(kBrightRed), imm_hi, c(kReset), c(kBrightMagenta), rs2_val, c(kReset), c(kBrightYellow),
        rs1_val, c(kReset), c(kBrightCyan), funct3_val, c(kReset), c(kBrightRed), imm_lo, c(kReset),
        c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  +------------+----------+----------+----+----------+-------------+");

    std::println("\nField Decoded Meanings:");
    std::println("  opcode  : {}0x{:02X}{} ({:07b}) -> Major Opcode", c(kBrightBlue),
                 std::to_underlying(op), c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  imm_lo  : {}0x{:02X}{}   ({:05b}) -> Branch offset bits [4:1, 11]",
                 c(kBrightRed), imm_lo, c(kBrightRed), imm_lo, c(kReset));
    std::println("  funct3  : {}0x{:01X}{}  ({:03b}) -> Branch condition selector", c(kBrightCyan),
                 funct3_val, c(kBrightCyan), funct3_val, c(kReset));
    std::println("  rs1     : {}x{:<2}{} ({:05b}) -> Source Register 1: {}", c(kBrightYellow),
                 rs1_val, c(kBrightYellow), rs1_val, reg_name(rs1_val), c(kReset));
    std::println("  rs2     : {}x{:<2}{} ({:05b}) -> Source Register 2: {}", c(kBrightMagenta),
                 rs2_val, c(kBrightMagenta), rs2_val, reg_name(rs2_val), c(kReset));
    std::println("  imm_hi  : {}0x{:02X}{}   ({:07b}) -> Branch offset bits [12, 10:5]",
                 c(kBrightRed), imm_hi, c(kBrightRed), imm_hi, c(kReset));

    std::println("\nImmediate Reconstruction:");
    uint32_t const b31 = (inst >> 31) & 1;
    uint32_t const b7 = (inst >> 7) & 1;
    uint32_t const b30_25 = (inst >> 25) & 0x3F;
    uint32_t const b11_8 = (inst >> 8) & 0xF;
    std::println("  imm[12]   = inst[31]    = {}", b31);
    std::println("  imm[11]   = inst[7]     = {}", b7);
    std::println("  imm[10:5] = inst[30:25] = {:06b}", b30_25);
    std::println("  imm[4:1]  = inst[11:8]  = {:04b}", b11_8);
    std::println("  imm[0]    = 0           = 0 (Implicitly zero for 2-byte alignment)");
    std::println("  Combined  = {:013b}", (b31 << 12) | (b7 << 11) | (b30_25 << 5) | (b11_8 << 1));
    if constexpr (simrv::xlen::kIsXLen64) {
        std::println("  Sign-extended to 64 bits: {}{} bytes (0x{:016X}){}", c(kBrightRed), imm_val,
                     static_cast<uint64_t>(imm_val), c(kReset));
        std::println("  Binary (64-bit view)    : {}{:064b}{}", c(kBrightRed),
                     static_cast<uint64_t>(imm_val), c(kReset));
    } else {
        std::println("  Sign-extended to 32 bits: {}{} bytes (0x{:08X}){}", c(kBrightRed), imm_val,
                     static_cast<uint32_t>(imm_val), c(kReset));
        std::println("  Binary (32-bit view)    : {}{:032b}{}", c(kBrightRed),
                     static_cast<uint32_t>(imm_val), c(kReset));
    }
}

auto print_u_format(uint32_t imm_bits, int32_t imm_val, uint32_t rd_val, Opcode op, bool use_color)
    -> void {
    auto c = [use_color](std::string_view code) -> std::string_view {
        return c_code(code, use_color);
    };
    std::println("Visual Bit Fields Breakdown (U-Type format):");
    std::println("  31                                   12 11       7 6           0");
    std::println("  +--------------------------------------+----------+-------------+");
    std::println("  |              immediate               |    rd    |   opcode    |");
    std::println("  +--------------------------------------+----------+-------------+");
    std::print("  |         {}{:020b}{}         |  {}{:05b}{}   |   {}{:07b}{}   |\n",
               c(kBrightRed), imm_bits, c(kReset), c(kBrightGreen), rd_val, c(kReset),
               c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  +--------------------------------------+----------+-------------+");

    std::println("\nField Decoded Meanings:");
    std::println("  opcode  : {}0x{:02X}{} ({:07b}) -> Major Opcode", c(kBrightBlue),
                 std::to_underlying(op), c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  rd      : {}x{:<2}{} ({:05b}) -> Destination Register: {}", c(kBrightGreen),
                 rd_val, c(kBrightGreen), rd_val, reg_name(rd_val), c(kReset));
    std::println("  imm     : {}0x{:05X}{} ({:020b}) -> Upper 20-bit Immediate", c(kBrightRed),
                 imm_bits, c(kBrightRed), imm_bits, c(kReset));

    std::println("\nImmediate Reconstruction:");
    std::println("  imm[31:12] = inst[31:12] = {:020b}", imm_bits);
    std::println("  imm[11:0]  = 0            = 000000000000");
    if constexpr (simrv::xlen::kIsXLen64) {
        std::println("  Combined Value           : {}{} (0x{:016X}){}", c(kBrightRed), imm_val,
                     static_cast<uint64_t>(imm_val), c(kReset));
        std::println("  Binary (64-bit view)     : {}{:064b}{}", c(kBrightRed),
                     static_cast<uint64_t>(imm_val), c(kReset));
    } else {
        std::println("  Combined Value           : {}{} (0x{:08X}){}", c(kBrightRed), imm_val,
                     static_cast<uint32_t>(imm_val), c(kReset));
        std::println("  Binary (32-bit view)     : {}{:032b}{}", c(kBrightRed),
                     static_cast<uint32_t>(imm_val), c(kReset));
    }
}

auto print_j_format(uint32_t inst, uint32_t imm_bits, int32_t imm_val, uint32_t rd_val, Opcode op,
                    bool use_color) -> void {
    auto c = [use_color](std::string_view code) -> std::string_view {
        return c_code(code, use_color);
    };
    std::println("Visual Bit Fields Breakdown (J-Type format):");
    std::println("  31                                   12 11       7 6           0");
    std::println("  +--------------------------------------+----------+-------------+");
    std::println("  |              immediate               |    rd    |   opcode    |");
    std::println("  +--------------------------------------+----------+-------------+");
    std::print("  |         {}{:020b}{}         |  {}{:05b}{}   |   {}{:07b}{}   |\n",
               c(kBrightRed), imm_bits, c(kReset), c(kBrightGreen), rd_val, c(kReset),
               c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  +--------------------------------------+----------+-------------+");

    std::println("\nField Decoded Meanings:");
    std::println("  opcode  : {}0x{:02X}{} ({:07b}) -> Major Opcode", c(kBrightBlue),
                 std::to_underlying(op), c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  rd      : {}x{:<2}{} ({:05b}) -> Destination Register: {}", c(kBrightGreen),
                 rd_val, c(kBrightGreen), rd_val, reg_name(rd_val), c(kReset));
    std::println("  imm_bits: {}0x{:05X}{} ({:020b}) -> Raw scrambled immediate bits",
                 c(kBrightRed), imm_bits, c(kBrightRed), imm_bits, c(kReset));

    std::println("\nImmediate Reconstruction:");
    uint32_t const j31 = (inst >> 31) & 1;
    uint32_t const j19_12 = (inst >> 12) & 0xFF;
    uint32_t const j20 = (inst >> 20) & 1;
    uint32_t const j30_21 = (inst >> 21) & 0x3FF;
    std::println("  imm[20]    = inst[31]     = {}", j31);
    std::println("  imm[19:12] = inst[19:12]  = {:08b}", j19_12);
    std::println("  imm[11]    = inst[20]     = {}", j20);
    std::println("  imm[10:1]  = inst[30:21]  = {:010b}", j30_21);
    std::println("  imm[0]     = 0            = 0 (Implicitly zero for 2-byte alignment)");
    std::println("  Combined   = {:021b}",
                 (j31 << 20) | (j19_12 << 12) | (j20 << 11) | (j30_21 << 1));
    if constexpr (simrv::xlen::kIsXLen64) {
        std::println("  Sign-extended to 64 bits: {}{} bytes (0x{:016X}){}", c(kBrightRed), imm_val,
                     static_cast<uint64_t>(imm_val), c(kReset));
        std::println("  Binary (64-bit view)    : {}{:064b}{}", c(kBrightRed),
                     static_cast<uint64_t>(imm_val), c(kReset));
    } else {
        std::println("  Sign-extended to 32 bits: {}{} bytes (0x{:08X}){}", c(kBrightRed), imm_val,
                     static_cast<uint32_t>(imm_val), c(kReset));
        std::println("  Binary (32-bit view)    : {}{:032b}{}", c(kBrightRed),
                     static_cast<uint32_t>(imm_val), c(kReset));
    }
}

auto print_r4_format(uint32_t funct7_val, uint32_t rs3_val, uint32_t rs2_val, uint32_t rs1_val,
                     uint32_t funct3_val, uint32_t rd_val, Opcode op, bool use_color) -> void {
    auto c = [use_color](std::string_view code) -> std::string_view {
        return c_code(code, use_color);
    };
    std::println("Visual Bit Fields Breakdown (R4-Type format):");
    std::println("  31   27 2625    24      20 19      15 1412 11       7 6           0");
    std::println("  +-----+----+--+----------+----------+----+----------+-------------+");
    std::println("  | rs3 |fmt |..|   rs2    |   rs1    | f3 |    rd    |   opcode    |");
    std::println("  +-----+----+--+----------+----------+----+----------+-------------+");
    std::print(
        "  |{}{:05b}{} | {:02b} |00|  {}{:05b}{}   |  {}{:05b}{}   |{}{:03b}{} |  {}{:05b}{}   |   "
        "{}{:07b}{}   |\n",
        c(kBrightWhite), rs3_val, c(kReset), funct7_val & 3, c(kBrightMagenta), rs2_val, c(kReset),
        c(kBrightYellow), rs1_val, c(kReset), c(kBrightCyan), funct3_val, c(kReset),
        c(kBrightGreen), rd_val, c(kReset), c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  +-----+----+--+----------+----------+----+----------+-------------+");

    std::println("\nField Decoded Meanings:");
    std::println("  opcode  : {}0x{:02X}{} ({:07b}) -> Major Opcode", c(kBrightBlue),
                 std::to_underlying(op), c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  rd      : {}f{:<2}{} ({:05b}) -> FP Destination Register: {}", c(kBrightGreen),
                 rd_val, c(kBrightGreen), rd_val, reg_name(rd_val, true), c(kReset));
    std::println("  funct3  : {}0x{:01X}{}  ({:03b}) -> FP rounding mode or sub-function",
                 c(kBrightCyan), funct3_val, c(kBrightCyan), funct3_val, c(kReset));
    std::println("  rs1     : {}f{:<2}{} ({:05b}) -> FP Source Register 1: {}", c(kBrightYellow),
                 rs1_val, c(kBrightYellow), rs1_val, reg_name(rs1_val, true), c(kReset));
    std::println("  rs2     : {}f{:<2}{} ({:05b}) -> FP Source Register 2: {}", c(kBrightMagenta),
                 rs2_val, c(kBrightMagenta), rs2_val, reg_name(rs2_val, true), c(kReset));
    std::println("  rs3     : {}f{:<2}{} ({:05b}) -> FP Source Register 3: {}", c(kBrightWhite),
                 rs3_val, c(kBrightWhite), rs3_val, reg_name(rs3_val, true), c(kReset));
}

auto get_pedagogical_reg_name(uint32_t idx, bool is_fp) -> std::string {
    if (is_fp) {
        if (idx < FP_ABI_NAMES.size()) {
            return std::format("f{} ({})", idx, FP_ABI_NAMES[idx]);
        }
        return std::format("f{}", idx);
    }
    if (idx == 0) {
        return "x0 (zero)";
    }
    if (idx < ABI_NAMES.size()) {
        return std::format("x{} ({})", idx, ABI_NAMES[idx]);
    }
    return std::format("x{}", idx);
}

void explain_pipeline_hazards(const InstructionExplanation& expl, bool use_color) {
    using simrv::pipeline::operation::RegBank;
    auto c = [use_color](std::string_view ansi_code) -> std::string_view {
        return use_color ? ansi_code : "";
    };
    constexpr std::string_view kItalic = "\033[3m";

    const uint32_t rd_val = std::to_underlying(expl.rd.reg_id);
    const uint32_t rs1_val = std::to_underlying(expl.rs1.reg_id);
    const uint32_t rs2_val = std::to_underlying(expl.rs2.reg_id);
    const uint32_t rs3_val = std::to_underlying(expl.rs3.reg_id);

    const bool writes_rd = expl.rd.bank != RegBank::None;
    const bool rd_is_fp = expl.rd.bank == RegBank::Float;
    const bool reads_rs1 = expl.rs1.bank != RegBank::None;
    const bool rs1_is_fp = expl.rs1.bank == RegBank::Float;
    const bool reads_rs2 = expl.rs2.bank != RegBank::None;
    const bool rs2_is_fp = expl.rs2.bank == RegBank::Float;
    const bool reads_rs3 = expl.rs3.bank != RegBank::None;
    const bool rs3_is_fp = expl.rs3.bank == RegBank::Float;

    std::println("{}Pipeline Hazard Analysis:{}", c(kBold), c(kReset));

    bool has_registers = false;
    if (reads_rs1 || reads_rs2 || reads_rs3) {
        std::println("  Register Reads (Data Dependencies):");
        if (reads_rs1) {
            std::println("    - rs1: {} ({})", get_pedagogical_reg_name(rs1_val, rs1_is_fp),
                         rs1_is_fp ? "FP" : "GPR");
        }
        if (reads_rs2) {
            std::println("    - rs2: {} ({})", get_pedagogical_reg_name(rs2_val, rs2_is_fp),
                         rs2_is_fp ? "FP" : "GPR");
        }
        if (reads_rs3) {
            std::println("    - rs3: {} ({})", get_pedagogical_reg_name(rs3_val, rs3_is_fp),
                         rs3_is_fp ? "FP" : "GPR");
        }
        has_registers = true;
    }

    if (writes_rd) {
        std::println("  Register Writes (Data Production):");
        std::println("    - rd : {} ({})", get_pedagogical_reg_name(rd_val, rd_is_fp),
                     rd_is_fp ? "FP" : "GPR");
        has_registers = true;
    }

    if (!has_registers) {
        std::println("    (No register operands accessed by this instruction)");
    }

    bool has_read_hazard = false;
    std::string read_hazards_str;

    if (reads_rs1 && !(rs1_val == 0 && !rs1_is_fp)) {
        read_hazards_str +=
            std::format("\n       * '{}'", get_pedagogical_reg_name(rs1_val, rs1_is_fp));
        has_read_hazard = true;
    }
    if (reads_rs2 && !(rs2_val == 0 && !rs2_is_fp)) {
        read_hazards_str +=
            std::format("\n       * '{}'", get_pedagogical_reg_name(rs2_val, rs2_is_fp));
        has_read_hazard = true;
    }
    if (reads_rs3 && !(rs3_val == 0 && !rs3_is_fp)) {
        read_hazards_str +=
            std::format("\n       * '{}'", get_pedagogical_reg_name(rs3_val, rs3_is_fp));
        has_read_hazard = true;
    }

    if (has_read_hazard) {
        std::println("\n  Read-After-Write (RAW) Data Hazards (Consumer Side):");
        std::print("    - If a preceding instruction writes to any of:{}", read_hazards_str);
        std::println("\n      it creates a RAW hazard requiring resolution:");
        std::println("      * {}Standard 5-stage pipeline without forwarding:{}", c(kBold),
                     c(kReset));
        std::println("        - Stall for 2 cycles if the producer is 1 cycle ahead.");
        std::println("        - Stall for 1 cycle if the producer is 2 cycles ahead.");
        std::println("      * {}Pipeline with operand forwarding:{}", c(kBold), c(kReset));
        std::println(
            "        - 0 stall cycles for most ALU-to-ALU dependencies (forwarded from EX/MEM or "
            "MEM/WB).");
        std::println(
            "        - 1 stall cycle ('load-use' delay) if the producer is a LOAD instruction "
            "immediately preceding this one.");
    }

    if (writes_rd && !(rd_val == 0 && !rd_is_fp)) {
        std::println("\n  Read-After-Write (RAW) Data Hazards (Producer Side):");
        std::println("    - This instruction writes to '{}'. Any subsequent instruction",
                     get_pedagogical_reg_name(rd_val, rd_is_fp));
        std::println("      reading this register within 1-2 cycles will face a RAW hazard:");
        std::println("      * {}Standard 5-stage pipeline without forwarding:{}", c(kBold),
                     c(kReset));
        std::println(
            "        - The subsequent instruction will stall 2 cycles if it immediately follows "
            "this one.");
        std::println(
            "        - The subsequent instruction will stall 1 cycle if it is 2 cycles later.");
        std::println("      * {}Pipeline with operand forwarding:{}", c(kBold), c(kReset));
        if (expl.opcode == Opcode::Load || expl.opcode == Opcode::LoadFp) {
            std::println(
                "        - Since this is a LOAD, it produces the data in the MEM stage. An "
                "immediately");
            std::println(
                "          following instruction that uses this data will stall for 1 cycle.");
        } else {
            std::println(
                "        - Forwarding path from EX/MEM or MEM/WB will eliminate stalls (0 cycles) "
                "for ALU consumers.");
        }
    }

    if ((reads_rs1 && rs1_val == 0 && !rs1_is_fp) || (reads_rs2 && rs2_val == 0 && !rs2_is_fp) ||
        (writes_rd && rd_val == 0 && !rd_is_fp)) {
        std::println("\n  {}Note on register x0 (zero):{}", c(kItalic), c(kReset));
        std::println(
            "    - The 'x0' GPR is hardwired to zero. Reading it always returns 0, and writing");
        std::println(
            "      to it has no effect. Thus, accesses to 'x0' never cause pipeline data hazards.");
    }

    const bool is_branch_jump =
        (expl.opcode == Opcode::Branch) || (expl.op_id == JALR) || (expl.op_id == JAL);
    const bool is_exception_flush =
        (expl.op_id == SRET || expl.op_id == MRET || expl.op_id == SFENCE_VMA);

    if (is_branch_jump || is_exception_flush) {
        std::println("\n  Control Hazards (Branch/Jump Penalties):");
        if (is_branch_jump) {
            std::println("    - This instruction alters the control flow (Program Counter).");
            std::println("    - In a pipelined CPU:");
            std::println("      * {}Without Branch Prediction:{}", c(kBold), c(kReset));
            std::println(
                "        - The processor stalls/flushes (typically 1-2 bubble cycles) when a "
                "branch is taken.");
            std::println("      * {}With Branch Prediction:{}", c(kBold), c(kReset));
            std::println(
                "        - 0 stall cycles if the branch outcome and target are correctly "
                "predicted.");
            std::println(
                "        - 2 to 3 penalty cycles (flushing speculative instructions) if "
                "mispredicted.");
        } else if (is_exception_flush) {
            std::println(
                "    - This instruction returns from an exception or invalidates TLB page "
                "translations.");
            std::println(
                "    - Resolution requires flushing speculatively fetched instructions from the "
                "pipeline");
            std::println("      to ensure correct architectural state and TLB consistency.");
        }
    }
}

void explain_datapath_diagram(OperationId op_id, InstFormat fmt, Opcode op, bool use_color) {
    auto c = [use_color](std::string_view ansi_code) -> std::string_view {
        return use_color ? ansi_code : "";
    };

    std::println("\n{}Hardware Data-Path Routing:{}", c(kBold), c(kReset));

    bool const is_load = (op == Opcode::Load) || (op == Opcode::LoadFp);
    bool const is_store = (op == Opcode::Store) || (op == Opcode::StoreFp);
    bool const is_csr = (op_id >= CSRRW && op_id <= CSRRCI);
    bool const is_branch = (op == Opcode::Branch);
    bool const is_jal = (op_id == JAL);
    bool const is_jalr = (op_id == JALR);

    if (is_load) {
        std::println(
            "  [rs1 (Base Reg)] ──► [Read Data 1] ──────────────┐\n"
            "                                                  ▼\n"
            "                                             [ ALU Addr ] ──► [ Data Memory ] ──► [rd "
            "(Dest Reg)]\n"
            "                                             [   Calc   ]     [ (Read Address) ]\n"
            "                                                  ▲\n"
            "  [Immediate (I)]  ──► [Sign-Extend Unit] ────────┘");
    } else if (is_store) {
        std::println(
            "  [rs1 (Base Reg)] ──► [Read Data 1] ──────────────┐\n"
            "                                                  ▼\n"
            "                                             [ ALU Addr ] ──► [ Data Memory ]\n"
            "                                             [   Calc   ]     [ (Write Address) ]\n"
            "                                                  ▲                    ▲\n"
            "  [Immediate (S)]  ──► [Sign-Extend Unit] ────────┘                    │\n"
            "                                                                       │\n"
            "  [rs2 (Src Reg)]  ──► [Read Data 2] ──────────────────────────────────┘\n"
            "                                                                  (Write Data)");
    } else if (is_branch) {
        std::println(
            "  [rs1 (Src Reg 1)] ──► [Read Data 1] ─────────────┐\n"
            "                                                   ▼\n"
            "                                              [ ALU / Comp ] ──► [ Branch Control ]\n"
            "                                              [ (Evaluate) ]     [   Taken / NT   ] "
            "──► [PC Target]\n"
            "                                                   ▲                    ▲\n"
            "  [rs2 (Src Reg 2)] ──► [Read Data 2] ─────────────┘                    │\n"
            "                                                                        │\n"
            "  [Immediate (B)]   ──► [Sign-Extend Unit] ─────────────────────────────┘\n"
            "                                                                  (Target Offset)");
    } else if (is_jal) {
        std::println(
            "  [Current PC] ───────────────────► [ Adder (+4) ] ────────────────────► [rd (Dest "
            "Reg)]\n"
            "        │\n"
            "        ▼\n"
            "  [Current PC] ─────────┐\n"
            "                        ▼\n"
            "                   [ ALU Target ] ◄── [Sign-Extend Unit] ◄── [Immediate (J)]\n"
            "                   [   Adder    ]\n"
            "                        │\n"
            "                        ▼\n"
            "                 [New PC Target]");
    } else if (is_jalr) {
        std::println(
            "  [Current PC] ───────────────────► [ Adder (+4) ] ────────────────────► [rd (Dest "
            "Reg)]\n"
            "\n"
            "  [rs1 (Target Reg)] ──► [Read Data 1] ────────────┐\n"
            "                                                   ▼\n"
            "                                              [ ALU Target ] ──► [New PC Target]\n"
            "                                              [   Adder    ]\n"
            "                                                   ▲\n"
            "  [Immediate (I)]    ──► [Sign-Extend Unit] ───────┘");
    } else if (is_csr) {
        std::println(
            "  [rs1 / uimm] ────────► [ Read Data 1 / uimm ] ───┐\n"
            "                                                   ▼\n"
            "                                              [ CSR Logic  ] ──► [rd (Dest Reg)] (Old "
            "Value)\n"
            "                                              [ (ALU/Mux)  ]\n"
            "                                                   ▲\n"
            "  [CSR Address] ───────► [Read CSR State] ─────────┴───────────► [Write New CSR "
            "State]");
    } else if (fmt == InstFormat::R) {
        std::println(
            "  [rs1 (Src Reg 1)] ──► [Read Data 1] ────────────────────────────► [   ALU    ]\n"
            "                                                                    [ (Op: Math) ] "
            "──► [rd (Dest Reg)]\n"
            "  [rs2 (Src Reg 2)] ──► [Read Data 2] ──► [Mux: RS2 Select] ──────► [          ]\n"
            "                                                 ▲\n"
            "                                           (Selects RS2)");
    } else if (fmt == InstFormat::I) {
        std::println(
            "  [rs1 (Src Reg 1)] ──► [Read Data 1] ────────────────────────────► [   ALU    ]\n"
            "                                                                    [ (Op: Math) ] "
            "──► [rd (Dest Reg)]\n"
            "  [Immediate (I)]   ──► [Sign-Extend] ───► [Mux: RS2 Select] ──────► [          ]\n"
            "                                                 ▲\n"
            "                                           (Selects Imm)");
    } else {
        std::println("  (Data-path layout not defined for this instruction format)");
    }
}

}  // namespace

void explain_instruction(uint32_t raw_inst) {
    bool const use_color = simrv::util::terminal_color_enabled(STDOUT_FILENO);
    auto c = [use_color](std::string_view ansi_code) -> std::string_view {
        return use_color ? ansi_code : "";
    };

    const InstructionExplanation expl = explain_static(raw_inst);

    std::println("\n{}=== SimRV Educational Instruction Explainer ==={}\n", c(kBoldFgBrightBlue),
                 c(kReset));

    uint32_t const inst = expl.canonical_bits;
    if (expl.is_compressed) {
        uint32_t const raw_16 = raw_inst & 0xFFFF;
        std::println("Compressed Instruction Detected (16-bit):");
        std::println("  Hex Value: {}0x{:04X}{}", c(kBrightYellow), raw_16, c(kReset));
        std::println("  Binary   : {}{:016b}{}", c(kBrightYellow), raw_16, c(kReset));

        std::println("\nDecompression Translation Step:");
        std::println("  16-bit compressed instruction translated to 32-bit canonical instruction:");
        std::println("  Hex Value: {}0x{:08X}{}", c(kBrightGreen), inst, c(kReset));
        std::println("  Binary   : {}{:032b}{}", c(kBrightGreen), inst, c(kReset));
        std::println(
            "--------------------------------------------------------------------------------");
    } else {
        std::println("Standard 32-bit Instruction Word:");
        std::println("  Hex Value: {}0x{:08X}{}", c(kBrightGreen), inst, c(kReset));
        std::println("  Binary   : {}{:032b}{}", c(kBrightGreen), inst, c(kReset));
        std::println(
            "--------------------------------------------------------------------------------");
    }

    auto const op = expl.opcode;
    uint32_t const rd_val = std::to_underlying(expl.rd.reg_id);
    uint32_t const funct3_val = std::to_underlying(expl.funct3);
    uint32_t const rs1_val = std::to_underlying(expl.rs1.reg_id);
    uint32_t const rs2_val = std::to_underlying(expl.rs2.reg_id);
    uint32_t const funct7_val = static_cast<uint32_t>(expl.funct7);
    uint32_t const rs3_val = std::to_underlying(expl.rs3.reg_id);

    InstFormat const fmt = expl.format;
    std::println("Instruction Format: {}{}{}", c(kBold),
                 simrv::isa::get_instruction_format_name(fmt), c(kReset));

    std::println("");
    switch (fmt) {
        case InstFormat::R:
            print_r_format(funct7_val, rs2_val, rs1_val, funct3_val, rd_val, op, use_color);
            break;
        case InstFormat::I: {
            uint32_t const imm_bits = (inst >> 20) & 0xFFF;
            print_i_format(imm_bits, static_cast<int32_t>(expl.imm), rs1_val, funct3_val, rd_val,
                           op, use_color);
            break;
        }
        case InstFormat::S: {
            uint32_t const imm_hi = (inst >> 25) & 0x7F;
            uint32_t const imm_lo = (inst >> 7) & 0x1F;
            print_s_format(imm_hi, imm_lo, static_cast<int32_t>(expl.imm), rs1_val, rs2_val,
                           funct3_val, op, use_color);
            break;
        }
        case InstFormat::B: {
            uint32_t const imm_hi = (inst >> 25) & 0x7F;
            uint32_t const imm_lo = (inst >> 7) & 0x1F;
            print_b_format(inst, imm_hi, imm_lo, static_cast<int32_t>(expl.imm), rs1_val, rs2_val,
                           funct3_val, op, use_color);
            break;
        }
        case InstFormat::U: {
            uint32_t const imm_bits = (inst >> 12) & 0xFFFFF;
            print_u_format(imm_bits, static_cast<int32_t>(expl.imm), rd_val, op, use_color);
            break;
        }
        case InstFormat::J: {
            uint32_t const imm_bits = ((inst >> 12) & 0xFFFFF);
            print_j_format(inst, imm_bits, static_cast<int32_t>(expl.imm), rd_val, op, use_color);
            break;
        }
        case InstFormat::R4:
            print_r4_format(funct7_val, rs3_val, rs2_val, rs1_val, funct3_val, rd_val, op,
                            use_color);
            break;
        default:
            std::println("Visual Bit Fields Breakdown: Format unrecognized.");
            break;
    }

    std::println(
        "--------------------------------------------------------------------------------");
    std::println("{}Decoded Instruction Detail:{}", c(kBold), c(kReset));
    if (expl.op_id != ILLEGAL) {
        std::println("  Assembly Mnemonic: {}{}{}", c(kBoldFgBrightGreen), expl.spec.mnemonic,
                     c(kReset));
        std::println("  Assembly Rep     : {}# {}{}", c(kBrightBlack), expl.assembly, c(kReset));
        if (!expl.spec.reference.formula.empty()) {
            std::println("  Spec Formula     : {}{}{}", c(kBrightCyan), expl.spec.reference.formula,
                         c(kReset));
        }
        std::println("  Spec Source      : {} — {}", expl.spec.reference.document,
                     expl.spec.reference.section);
        std::println("\nDescription (Behavior):");
        std::println("  [ISA: {}] {}", expl.spec.reference.extension, expl.spec.description);

        std::println(
            "\n--------------------------------------------------------------------------------");
        explain_pipeline_hazards(expl, use_color);
        std::println(
            "\n--------------------------------------------------------------------------------");
        explain_datapath_diagram(expl.op_id, fmt, op, use_color);
    } else {
        std::println("  Mnemonic         : {}ILLEGAL / RESERVED{}", c(kBoldFgRed), c(kReset));
        std::println(
            "  This opcode is either reserved, or part of an unsupported custom extension.");
    }
    std::println("\n{}================================================={}\n", c(kBoldFgBrightBlue),
                 c(kReset));
}

}  // namespace simrv::explain
