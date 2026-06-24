/**
 * @file RegisterPane.cpp
 * @brief Implements RegisterPane widget rendering.
 */
#include "simrv/tui/RegisterPane.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/util/FormatUtil.hpp"
#include "simrv/util/InstructionExplainer.hpp"
#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include <chrono>
#include <format>
#include <string>
#include <vector>
#include <array>
#include <algorithm>

namespace simrv::tui {

using simrv::isa::Opcode;
using simrv::isa::OperationId;
using enum simrv::isa::OperationId;

namespace {

static constexpr std::array<const char*, 32> kRegNames = {
    "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0/fp", "s1", "a0",
    "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3",    "s4", "s5",
    "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5",    "t6"};

static constexpr std::array<const char*, 32> kFpRegNames = {
    "ft0", "ft1", "ft2", "ft3", "ft4",  "ft5",  "ft6", "ft7", "fs0",  "fs1", "fa0",
    "fa1", "fa2", "fa3", "fa4", "fa5",  "fa6",  "fa7", "fs2", "fs3",  "fs4", "fs5",
    "fs6", "fs7", "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"};

auto make_progress_bar(double ratio, int width, const std::string& color_code) -> std::string {
    int filled = static_cast<int>(ratio * width);
    if (filled < 0) filled = 0;
    if (filled > width) filled = width;
    std::string bar;
    bar += color_code;
    for (int i = 0; i < filled; ++i) {
        bar += "█";
    }
    bar += kSakuraMuted;
    for (int i = filled; i < width; ++i) {
        bar += "░";
    }
    bar += "\033[0m";
    return bar;
}

auto format_compact(uint64_t val) -> std::string {
    if (val >= 1000000000ULL) {
        return std::format("{:5.1f}G", static_cast<double>(val) / 1000000000.0);
    }
    if (val >= 1000000ULL) {
        return std::format("{:5.1f}M", static_cast<double>(val) / 1000000.0);
    }
    if (val >= 1000ULL) {
        return std::format("{:5.1f}K", static_cast<double>(val) / 1000.0);
    }
    return std::format("{:6d}", val);
}

enum class InstFormat : uint8_t {
    R, I, S, B, U, J, R4, Unknown
};

auto get_format(Opcode op) -> InstFormat {
    static constexpr std::array<InstFormat, 128> format_lut = []() -> std::array<InstFormat, 128> {
        std::array<InstFormat, 128> lut{};
        lut.fill(InstFormat::Unknown);
        
        lut[static_cast<size_t>(Opcode::Load)] = InstFormat::I;
        lut[static_cast<size_t>(Opcode::LoadFp)] = InstFormat::I;
        lut[static_cast<size_t>(Opcode::MiscMem)] = InstFormat::I;
        lut[static_cast<size_t>(Opcode::OpImm)] = InstFormat::I;
        lut[static_cast<size_t>(Opcode::OpImm32)] = InstFormat::I;
        lut[static_cast<size_t>(Opcode::Jalr)] = InstFormat::I;
        lut[static_cast<size_t>(Opcode::System)] = InstFormat::I;
        
        lut[static_cast<size_t>(Opcode::Store)] = InstFormat::S;
        lut[static_cast<size_t>(Opcode::StoreFp)] = InstFormat::S;
        
        lut[static_cast<size_t>(Opcode::Branch)] = InstFormat::B;
        
        lut[static_cast<size_t>(Opcode::Auipc)] = InstFormat::U;
        lut[static_cast<size_t>(Opcode::Lui)] = InstFormat::U;
        
        lut[static_cast<size_t>(Opcode::Jal)] = InstFormat::J;
        
        lut[static_cast<size_t>(Opcode::Op)] = InstFormat::R;
        lut[static_cast<size_t>(Opcode::Op32)] = InstFormat::R;
        lut[static_cast<size_t>(Opcode::Amo)] = InstFormat::R;
        lut[static_cast<size_t>(Opcode::OpFp)] = InstFormat::R;
        
        lut[static_cast<size_t>(Opcode::MAdd)] = InstFormat::R4;
        lut[static_cast<size_t>(Opcode::MSub)] = InstFormat::R4;
        lut[static_cast<size_t>(Opcode::NMSub)] = InstFormat::R4;
        lut[static_cast<size_t>(Opcode::NMAdd)] = InstFormat::R4;
        
        return lut;
    }();
    
    auto idx = static_cast<size_t>(op);
    if (idx < format_lut.size()) {
        return format_lut[idx];
    }
    return InstFormat::Unknown;
}

auto get_format_name(InstFormat fmt) -> std::string_view {
    switch (fmt) {
        case InstFormat::R: return "R-Type (Register-Register)";
        case InstFormat::I: return "I-Type (Register-Immediate / Load / Jump)";
        case InstFormat::S: return "S-Type (Store)";
        case InstFormat::B: return "B-Type (Branch)";
        case InstFormat::U: return "U-Type (Upper Immediate)";
        case InstFormat::J: return "J-Type (Unconditional Jump)";
        case InstFormat::R4: return "R4-Type (Fused Multiply-Add)";
        default: return "Unknown / Custom Format";
    }
}

auto get_reg_name(RegId reg, bool is_fp) -> std::string {
    uint32_t r = std::to_underlying(reg);
    if (r >= 32) return std::format("r{}", r);
    return is_fp ? kFpRegNames.at(r) : kRegNames.at(r);
}




auto wrap_text(const std::string& text, int max_len) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::string current_line;
    std::string word;
    for (char c : text) {
        if (c == ' ') {
            if (!word.empty()) {
                if (current_line.length() + word.length() + 1 > static_cast<std::size_t>(max_len)) {
                    lines.push_back(current_line);
                    current_line = word;
                } else {
                    if (!current_line.empty()) {
                        current_line += " ";
                    }
                    current_line += word;
                }
                word.clear();
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        if (current_line.length() + word.length() + 1 > static_cast<std::size_t>(max_len)) {
            lines.push_back(current_line);
            current_line = word;
        } else {
            if (!current_line.empty()) {
                current_line += " ";
            }
            current_line += word;
        }
    }
    if (!current_line.empty()) {
        lines.push_back(current_line);
    }
    return lines;
}

auto render_visual_bitfields(InstFormat fmt, uint32_t ir_org, int width) -> std::vector<std::string> {
    std::vector<std::string> rows;
    if (fmt == InstFormat::R) {
        rows.push_back(format_to_width("   31     25 24   20 19   15 14 12 11    7 6       0", width));
        rows.push_back(format_to_width("  +---------+-------+-------+-----+-------+---------+", width));
        rows.push_back(format_to_width("  | funct7  |  rs2  |  rs1  |  f3 |  rd   | opcode  |", width));
        rows.push_back(format_to_width(std::format("  | {:07b} | {:05b} | {:05b} | {:03b} | {:05b} | {:07b} |",
            (ir_org >> 25) & 0x7F, (ir_org >> 20) & 0x1F, (ir_org >> 15) & 0x1F,
            (ir_org >> 12) & 0x07, (ir_org >> 7) & 0x1F, ir_org & 0x7F), width));
        rows.push_back(format_to_width("  +---------+-------+-------+-----+-------+---------+", width));
    } else if (fmt == InstFormat::I) {
        rows.push_back(format_to_width("   31         20  19  15  1412  11   7   6     0", width));
        rows.push_back(format_to_width("  +--------------+-------+-----+-------+---------+", width));
        rows.push_back(format_to_width("  |  immediate   |  rs1  |  f3 |  rd   | opcode  |", width));
        rows.push_back(format_to_width(std::format("  | {:012b} | {:05b} | {:03b} | {:05b} | {:07b} |",
            (ir_org >> 20) & 0xFFF, (ir_org >> 15) & 0x1F, (ir_org >> 12) & 0x07,
            (ir_org >> 7) & 0x1F, ir_org & 0x7F), width));
        rows.push_back(format_to_width("  +--------------+-------+-----+-------+---------+", width));
    } else if (fmt == InstFormat::S) {
        rows.push_back(format_to_width("   11     5  24  20  19  15  1412  4   0   6     0", width));
        rows.push_back(format_to_width("  +---------+-------+-------+-----+-------+---------+", width));
        rows.push_back(format_to_width("  | imm115  |  rs2  |  rs1  |  f3 | imm40 | opcode  |", width));
        rows.push_back(format_to_width(std::format("  | {:07b} | {:05b} | {:05b} | {:03b} | {:05b} | {:07b} |",
            (ir_org >> 25) & 0x7F, (ir_org >> 20) & 0x1F, (ir_org >> 15) & 0x1F,
            (ir_org >> 12) & 0x07, (ir_org >> 7) & 0x1F, ir_org & 0x7F), width));
        rows.push_back(format_to_width("  +---------+-------+-------+-----+-------+---------+", width));
    } else if (fmt == InstFormat::B) {
        rows.push_back(format_to_width("   12     5  24  20  19  15  1412  4  11   6     0", width));
        rows.push_back(format_to_width("  +---------+-------+-------+-----+-------+---------+", width));
        rows.push_back(format_to_width("  | imm125  |  rs2  |  rs1  |  f3 | imm411| opcode  |", width));
        rows.push_back(format_to_width(std::format("  | {:07b} | {:05b} | {:05b} | {:03b} | {:05b} | {:07b} |",
            (ir_org >> 25) & 0x7F, (ir_org >> 20) & 0x1F, (ir_org >> 15) & 0x1F,
            (ir_org >> 12) & 0x07, (ir_org >> 7) & 0x1F, ir_org & 0x7F), width));
        rows.push_back(format_to_width("  +---------+-------+-------+-----+-------+---------+", width));
    } else if (fmt == InstFormat::U) {
        rows.push_back(format_to_width("     31                 12 11   7   6     0", width));
        rows.push_back(format_to_width("  +--------------------------+-------+---------+", width));
        rows.push_back(format_to_width("  |        immediate         |  rd   | opcode  |", width));
        rows.push_back(format_to_width(std::format("  |   {:020b}   | {:05b} | {:07b} |",
            (ir_org >> 12) & 0xFFFFF, (ir_org >> 7) & 0x1F, ir_org & 0x7F), width));
        rows.push_back(format_to_width("  +--------------------------+-------+---------+", width));
    } else if (fmt == InstFormat::J) {
        rows.push_back(format_to_width("     31                  1 11   7   6     0", width));
        rows.push_back(format_to_width("  +--------------------------+-------+---------+", width));
        rows.push_back(format_to_width("  |        immediate         |  rd   | opcode  |", width));
        rows.push_back(format_to_width(std::format("  |   {:020b}   | {:05b} | {:07b} |",
            (ir_org >> 12) & 0xFFFFF, (ir_org >> 7) & 0x1F, ir_org & 0x7F), width));
        rows.push_back(format_to_width("  +--------------------------+-------+---------+", width));
    } else if (fmt == InstFormat::R4) {
        rows.push_back(format_to_width("   31  27 2625      24  20  19  15  1412  11   7   6     0", width));
        rows.push_back(format_to_width("  +-------+----+----+-------+-------+-----+-------+---------+", width));
        rows.push_back(format_to_width("  |  rs3  |fmt | .. |  rs2  |  rs1  |  f3 |  rd   | opcode  |", width));
        rows.push_back(format_to_width(std::format("  | {:05b} | {:02b} | 00 | {:05b} | {:05b} | {:03b} | {:05b} | {:07b} |",
            (ir_org >> 27) & 0x1F, (ir_org >> 25) & 0x03, (ir_org >> 20) & 0x1F, (ir_org >> 15) & 0x1F,
            (ir_org >> 12) & 0x07, (ir_org >> 7) & 0x1F, ir_org & 0x7F), width));
        rows.push_back(format_to_width("  +-------+----+----+-------+-------+-----+-------+---------+", width));
    } else {
        rows.push_back(format_to_width("  (Unknown instruction format layout)", width));
    }
    return rows;
}

auto read_reg_str(const simrv::core::ArchState& st, RegId reg, bool is_fp) -> std::string {
    uint32_t r = std::to_underlying(reg);
    if (r == 0 && !is_fp) return "0x00000000";
    Register val = is_fp ? st.regs.read_fp(reg) : st.regs.read(reg);
    return std::format("0x{:0{}x}", val, simrv::xlen::kXLenHexDigits);
}

auto has_rd(InstFormat fmt) -> bool {
    return fmt == InstFormat::R || fmt == InstFormat::I || fmt == InstFormat::U || fmt == InstFormat::J || fmt == InstFormat::R4;
}

auto has_rs1(InstFormat fmt) -> bool {
    return fmt == InstFormat::R || fmt == InstFormat::I || fmt == InstFormat::S || fmt == InstFormat::B || fmt == InstFormat::R4;
}

auto has_rs2(InstFormat fmt) -> bool {
    return fmt == InstFormat::R || fmt == InstFormat::S || fmt == InstFormat::B || fmt == InstFormat::R4;
}

auto has_funct3(InstFormat fmt) -> bool {
    return fmt == InstFormat::R || fmt == InstFormat::I || fmt == InstFormat::S || fmt == InstFormat::B || fmt == InstFormat::R4;
}

auto has_funct7(InstFormat fmt) -> bool {
    return fmt == InstFormat::R || fmt == InstFormat::R4;
}

auto has_imm(InstFormat fmt) -> bool {
    return fmt == InstFormat::I || fmt == InstFormat::S || fmt == InstFormat::B || fmt == InstFormat::U || fmt == InstFormat::J;
}

auto has_target(InstFormat fmt) -> bool {
    return fmt == InstFormat::B || fmt == InstFormat::J;
}

auto is_unary_float_op(uint8_t op_id) -> bool {
    static constexpr std::array<bool, 256> unary_float_lut = []() -> std::array<bool, 256> {
        std::array<bool, 256> lut{};
        lut.fill(false);
        lut[FSQRT_S] = true;
        lut[FSQRT_D] = true;
        lut[FCLASS_S] = true;
        lut[FCLASS_D] = true;
        lut[FMV_X_W] = true;
        lut[FMV_X_D] = true;
        lut[FMV_W_X] = true;
        lut[FMV_D_X] = true;
        for (int i = FCVT_W_S; i <= FCVT_LU_S; ++i) lut[i] = true;
        for (int i = FCVT_W_D; i <= FCVT_LU_D; ++i) lut[i] = true;
        for (int i = FCVT_S_W; i <= FCVT_S_LU; ++i) lut[i] = true;
        for (int i = FCVT_D_W; i <= FCVT_D_LU; ++i) lut[i] = true;
        return lut;
    }();
    return unary_float_lut[op_id];
}

auto is_shift_imm_op(uint8_t op_id) -> bool {
    return op_id == SLLI || op_id == SRLI || op_id == SRAI ||
           op_id == SLLIW || op_id == SRLIW || op_id == SRAIW;
}

auto is_csr_imm_op(uint8_t op_id) -> bool {
    return op_id == CSRRWI || op_id == CSRRSI || op_id == CSRRCI;
}

auto format_assembly_r(
    const simrv::pipeline::PipelineContext& ctx,
    std::string_view mnemonic,
    const std::string& rd_name,
    const std::string& rs1_name,
    const std::string& rs2_name
) -> std::string {
    if (ctx.op_id == SFENCE_VMA) {
        return std::format("sfence.vma {}, {}", rs1_name, rs2_name);
    }
    if (ctx.op_id >= LR_W && ctx.op_id <= SC_W) {
        if (ctx.op_id == LR_W) {
            return std::format("lr.w {}, ({})", rd_name, rs1_name);
        }
        return std::format("sc.w {}, {}, ({})", rd_name, rs2_name, rs1_name);
    }
    if (ctx.op_id >= AMOSWAP_W && ctx.op_id <= AMOMAXU_W) {
        return std::format("{}.aqrl {}, {}, ({})", mnemonic, rd_name, rs2_name, rs1_name);
    }
    if (is_unary_float_op(ctx.op_id)) {
        return std::format("{} {}, {}", mnemonic, rd_name, rs1_name);
    }
    return std::format("{} {}, {}, {}", mnemonic, rd_name, rs1_name, rs2_name);
}

auto format_assembly_i(
    const simrv::pipeline::PipelineContext& ctx,
    std::string_view mnemonic,
    const std::string& rd_name,
    const std::string& rs1_name
) -> std::string {
    bool const is_load = (ctx.opcode == Opcode::Load) || (ctx.opcode == Opcode::LoadFp);
    bool const is_csr = (ctx.op_id >= CSRRW && ctx.op_id <= CSRRCI);
    if (is_load) {
        return std::format("{} {}, {}({})", mnemonic, rd_name, ctx.imm, rs1_name);
    }
    if (ctx.op_id == JALR) {
        return std::format("jalr {}, {}({})", rd_name, ctx.imm, rs1_name);
    }
    if (is_csr) {
        std::string csr_str = simrv::util::csr_name(ctx.imm & 0xFFF);
        if (is_csr_imm_op(ctx.op_id)) {
            return std::format("{} {}, {}, {}", mnemonic, rd_name, csr_str, std::to_underlying(ctx.rs1));
        }
        return std::format("{} {}, {}, {}", mnemonic, rd_name, csr_str, rs1_name);
    }
    if (ctx.op_id == ECALL || ctx.op_id == EBREAK) {
        return std::string(mnemonic);
    }
    if (ctx.op_id == FENCE) {
        return "fence";
    }
    if (ctx.op_id == FENCE_I) {
        return "fence.i";
    }
    if (is_shift_imm_op(ctx.op_id)) {
        uint32_t shamt = ctx.imm & 0x3F;
        return std::format("{} {}, {}, {}", mnemonic, rd_name, rs1_name, shamt);
    }
    return std::format("{} {}, {}, {}", mnemonic, rd_name, rs1_name, ctx.imm);
}

auto format_assembly_s_b_u_j_r4(
    const simrv::pipeline::PipelineContext& ctx,
    InstFormat fmt,
    std::string_view mnemonic,
    const std::string& rd_name,
    const std::string& rs1_name,
    const std::string& rs2_name
) -> std::string {
    if (fmt == InstFormat::S) {
        bool const is_store_fp = (ctx.opcode == Opcode::StoreFp);
        return std::format("{} {}, {}({})", mnemonic, get_reg_name(ctx.rs2, is_store_fp), ctx.imm, rs1_name);
    }
    if (fmt == InstFormat::B) {
        return std::format("{} {}, {}, {}", mnemonic, rs1_name, rs2_name, ctx.imm);
    }
    if (fmt == InstFormat::U) {
        return std::format("{} {}, 0x{:X}", mnemonic, rd_name, static_cast<uint32_t>(ctx.imm));
    }
    if (fmt == InstFormat::J) {
        return std::format("jal {}, {}", rd_name, ctx.imm);
    }
    if (fmt == InstFormat::R4) {
        uint32_t rs3_val = (ctx.ir_org >> 27) & 0x1F;
        std::string rs3_name = kFpRegNames.at(rs3_val);
        return std::format("{} {}, {}, {}, {}", mnemonic, rd_name, rs1_name, rs2_name, rs3_name);
    }
    return "unknown / illegal instruction";
}

auto render_field_decoded_values(
    const simrv::core::ArchState& st,
    const simrv::pipeline::PipelineContext& ctx,
    InstFormat fmt,
    const std::string& rd_name,
    const std::string& rs1_name,
    const std::string& rs2_name,
    int width
) -> std::vector<std::string> {
    std::vector<std::string> rows;
    rows.push_back(format_to_width(std::format("  opcode : 0x{:02X} ({:07b})", std::to_underlying(ctx.opcode), std::to_underlying(ctx.opcode)), width));
    
    if (has_rd(fmt)) {
        bool const is_dst_fp = simrv::isa::is_destination_fp(ctx.opcode, ctx.op_id);
        rows.push_back(format_to_width(std::format("  rd     : {} = {}", rd_name, read_reg_str(st, ctx.rd, is_dst_fp)), width));
    }
    if (has_rs1(fmt)) {
        bool const is_rs1_fpr = simrv::isa::is_rs1_fp(ctx.opcode, ctx.op_id);
        rows.push_back(format_to_width(std::format("  rs1    : {} = {}", rs1_name, read_reg_str(st, ctx.rs1, is_rs1_fpr)), width));
    }
    if (has_rs2(fmt)) {
        bool const is_rs2_fpr = simrv::isa::is_rs2_fp(ctx.opcode, ctx.op_id);
        rows.push_back(format_to_width(std::format("  rs2    : {} = {}", rs2_name, read_reg_str(st, ctx.rs2, is_rs2_fpr)), width));
    }
    if (fmt == InstFormat::R4) {
        uint32_t rs3_val = (ctx.ir_org >> 27) & 0x1F;
        std::string rs3_name = kFpRegNames.at(rs3_val);
        rows.push_back(format_to_width(std::format("  rs3    : {} = {}", rs3_name, read_reg_str(st, static_cast<RegId>(rs3_val), true)), width));
    }
    if (has_funct3(fmt)) {
        rows.push_back(format_to_width(std::format("  funct3 : 0x{:X} ({:03b})", std::to_underlying(ctx.funct3), std::to_underlying(ctx.funct3)), width));
    }
    if (has_funct7(fmt)) {
        rows.push_back(format_to_width(std::format("  funct7 : 0x{:02X} ({:07b})", (ctx.ir_org >> 25) & 0x7F, (ctx.ir_org >> 25) & 0x7F), width));
    }
    if (has_imm(fmt)) {
        rows.push_back(format_to_width(std::format("  imm    : {} (0x{:X})", ctx.imm, static_cast<uint32_t>(ctx.imm)), width));
    }
    if (has_target(fmt)) {
        rows.push_back(format_to_width(std::format("  target : 0x{:0{}x}", st.pc + ctx.imm, simrv::xlen::kXLenHexDigits), width));
    }
    return rows;
}

auto format_instruction_assembly(
    const simrv::pipeline::PipelineContext& ctx,
    InstFormat fmt,
    std::string_view mnemonic,
    const std::string& rd_name,
    const std::string& rs1_name,
    const std::string& rs2_name
) -> std::string {
    if (ctx.op_id == UNKNOWN) {
        return "unknown / illegal instruction";
    }
    if (fmt == InstFormat::R) {
        return format_assembly_r(ctx, mnemonic, rd_name, rs1_name, rs2_name);
    }
    if (fmt == InstFormat::I) {
        return format_assembly_i(ctx, mnemonic, rd_name, rs1_name);
    }
    return format_assembly_s_b_u_j_r4(ctx, fmt, mnemonic, rd_name, rs1_name, rs2_name);
}

enum class InstCategory : uint8_t {
    ALU, MEM, CTRL, SYS
};

auto get_inst_category(int i) -> InstCategory {
    static constexpr std::array<InstCategory, 256> category_lut = []() -> std::array<InstCategory, 256> {
        std::array<InstCategory, 256> lut{};
        lut.fill(InstCategory::ALU);
        
        lut[OperationId::JAL] = InstCategory::CTRL;
        lut[OperationId::JALR] = InstCategory::CTRL;
        for (int op = OperationId::BEQ; op <= OperationId::BGEU; ++op) {
            lut[op] = InstCategory::CTRL;
        }
        
        for (int op = OperationId::LB; op <= OperationId::SD; ++op) {
            lut[op] = InstCategory::MEM;
        }
        lut[OperationId::LR_W] = InstCategory::MEM;
        lut[OperationId::SC_W] = InstCategory::MEM;
        for (int op = OperationId::AMOSWAP_W; op <= OperationId::AMOMAXU_W; ++op) {
            lut[op] = InstCategory::MEM;
        }
        
        lut[OperationId::ECALL] = InstCategory::SYS;
        lut[OperationId::EBREAK] = InstCategory::SYS;
        lut[OperationId::URET] = InstCategory::SYS;
        lut[OperationId::SRET] = InstCategory::SYS;
        lut[OperationId::MRET] = InstCategory::SYS;
        lut[OperationId::WFI] = InstCategory::SYS;
        lut[OperationId::SFENCE_VMA] = InstCategory::SYS;
        
        return lut;
    }();
    if (i >= 0 && i < 256) {
        return category_lut[i];
    }
    return InstCategory::ALU;
}

auto has_stage_raw_hazard(const simrv::pipeline::PipelineReg& d_reg, const simrv::pipeline::PipelineReg& stage_reg) -> bool {
    if (!stage_reg.valid || !stage_reg.writes_reg || stage_reg.rd == static_cast<RegId>(0)) {
        return false;
    }
    if (stage_reg.remaining_latency == 0) {
        return false;
    }
    bool reads_rs1 = (d_reg.rs1 != static_cast<RegId>(0));
    bool reads_rs2 = (d_reg.rs2 != static_cast<RegId>(0));
    return (reads_rs1 && d_reg.rs1 == stage_reg.rd) || (reads_rs2 && d_reg.rs2 == stage_reg.rd);
}

auto is_raw_hazard_stalled(const simrv::pipeline::PipelineSim& ps) -> bool {
    if (!ps.d_reg_.valid) {
        return false;
    }
    return has_stage_raw_hazard(ps.d_reg_, ps.e_reg_) || has_stage_raw_hazard(ps.d_reg_, ps.m_reg_);
}

auto get_stage_desc(const simrv::pipeline::PipelineReg& reg, uint32_t stall_rem, const std::string& stall_type, bool raw_stall = false) -> std::string {
    if (!reg.valid) {
        return "\033[38;5;244mbubble/empty\033[0m";
    }
    std::string_view op_name = "UNKNOWN";
    if (static_cast<std::size_t>(reg.op_id) < simrv::pipeline::OPERATION_NAME.size()) {
        op_name = simrv::pipeline::OPERATION_NAME.at(static_cast<std::size_t>(reg.op_id));
    }
    std::string desc = std::format("\033[1;38;5;121m0x{:0{}x}\033[0m (\033[1;38;5;183m{}\033[0m)", 
                                   reg.pc, simrv::xlen::kXLenHexDigits, op_name);
    if (stall_rem > 0) {
        desc += std::format(" \033[38;5;203m[Stall: {} ({} clk)]\033[0m", stall_type, stall_rem);
    } else if (raw_stall) {
        desc += " \033[38;5;203m[Stall: RAW Hazard]\033[0m";
    } else if (reg.remaining_latency > 0) {
        desc += std::format(" \033[38;5;218m[Lat: {} clk]\033[0m", reg.remaining_latency);
    }
    return desc;
}

auto get_stall_status(bool active, const char* active_str = "Active") -> std::pair<const char*, const char*> {
    if (active) {
        return {active_str, kSakuraPeach};
    }
    return {"None", kSakuraMint};
}

auto get_active_branch_pc(const simrv::pipeline::PipelineSim& ps) -> Register {
    if (ps.e_reg_.valid && (ps.e_reg_.is_branch || ps.e_reg_.is_jump)) {
        return ps.e_reg_.pc;
    }
    if (ps.d_reg_.valid && (ps.d_reg_.is_branch || ps.d_reg_.is_jump)) {
        return ps.d_reg_.pc;
    }
    return 0;
}

auto get_bht_string(const simrv::pipeline::PipelineSim& ps, Register pc) -> std::string {
    if (pc == 0) {
        return "N/A";
    }
    const uint32_t bht_idx = (pc >> 1) & 0xFF;
    uint8_t counter = ps.branch_history_table_.at(bht_idx);
    switch (counter) {
        case 0: return "Strongly Not Taken (00)";
        case 1: return "Weakly Not Taken (01)";
        case 2: return "Weakly Taken (10)";
        default: return "Strongly Taken (11)";
    }
}

auto get_btb_string(const simrv::pipeline::PipelineSim& ps, Register pc) -> std::string {
    if (pc == 0) {
        return "N/A";
    }
    const uint32_t btb_idx = (pc >> 1) & 0x7F;
    auto& btb_entry = ps.btb_.at(btb_idx);
    if (btb_entry.valid && btb_entry.pc == pc) {
        return std::format("Hit (Target: 0x{:0{}x})", btb_entry.target, simrv::xlen::kXLenHexDigits);
    }
    return "Miss";
}

}  // namespace


auto RegisterPane::is_single_column(int width) const -> bool {
    bool const is_reg_page = (page_ == TuiRegPage::GPR || page_ == TuiRegPage::FPR || page_ == TuiRegPage::VEC);
    if (!is_reg_page) return false;
    if (page_ == TuiRegPage::GPR) {
        return (simrv::xlen::kIsXLen64 && width < 58) || (!simrv::xlen::kIsXLen64 && width < 42);
    }
    return width < 58;
}

auto RegisterPane::get_total_rows(int width) -> int {
    if (page_ == TuiRegPage::EXPLAIN) {
        return static_cast<int>(get_explain_rows(width).size());
    }
    bool const single_column = is_single_column(width);
    int base_rows = machine_.s_cycle_accurate ? 43 : 35;
    if (single_column) {
        base_rows += 16;
    }
    int debug_rows = machine_.s_debug_mode ? 4 : 0;
    return base_rows + debug_rows;
}

auto RegisterPane::section_line(const std::string& title, int width) -> std::string {
    if (title.starts_with("─") || title.starts_with(" ")) {
        std::string full = title + " ";
        int dash_len = width - get_display_width(full);
        if (dash_len < 0) dash_len = 0;
        return std::format("\033[1;38;5;254m{} \033[0m{}{}", title, kSakuraBorder, make_repeated_string("─", dash_len));
    } else {
        std::string text = " " + title + " ";
        int dash_len = width - get_display_width(text);
        if (dash_len < 0) dash_len = 0;
        int left_dashes = std::min(4, dash_len / 2);
        int right_dashes = dash_len - left_dashes;
        return std::format("{}{} \033[1;38;5;254m{}\033[0m {}{}", 
                           kSakuraBorder, make_repeated_string("─", left_dashes),
                           title,
                           kSakuraBorder, make_repeated_string("─", right_dashes));
    }
}

auto RegisterPane::make_field(const std::string& label, const std::string& value,
                             const char* value_color, int label_pad) -> std::string {
    if (label_pad == 0) {
        return std::format(" {}{}\033[0m: {}{}\033[0m", kSakuraText, label, value_color, value);
    } else {
        return std::format(" {}{:<{}}\033[0m: {}{}\033[0m", kSakuraText, label, label_pad, value_color, value);
    }
}

auto RegisterPane::render_pair(const std::string& l1, const std::string& v1, const char* c1,
                               const std::string& l2, const std::string& v2, const char* c2,
                               int col_width, int right_width, int label_pad) -> std::string {
    return format_to_width(make_field(l1, v1, c1, label_pad), col_width) +
           format_to_width(make_field(l2, v2, c2, label_pad), right_width);
}

auto RegisterPane::render_active_spinner(int logical_row, int width) -> std::string {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    constexpr std::array<const char*, 10> spinner = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    std::string spin = spinner.at((static_cast<std::size_t>(now_ms / 80)) % 10);

    bool const is_reg_page = (page_ == TuiRegPage::GPR || page_ == TuiRegPage::FPR || page_ == TuiRegPage::VEC);
    bool const single_column = is_reg_page && ([&]() -> bool {
        if (page_ == TuiRegPage::GPR) {
            return (simrv::xlen::kIsXLen64 && width < 58) || (!simrv::xlen::kIsXLen64 && width < 42);
        }
        return width < 58;
    }());

    int target_row_offset = single_column ? 8 : 0;
    if (logical_row == 10 + target_row_offset) {
        std::string text = std::format("\033[38;5;218m●\033[0m \033[1;38;5;218mSIMULATOR ACTIVE\033[0m");
        int spaces = std::max(0, (width - 18) / 2);
        std::string line = std::string(spaces, ' ') + text;
        return format_to_width(line, width);
    }
    if (logical_row == 11 + target_row_offset) {
        std::string text = std::format("[  \033[38;5;121m{}\033[0m  Executing instructions... ]", spin);
        int spaces = std::max(0, (width - 33) / 2);
        std::string line = std::string(spaces, ' ') + text;
        return format_to_width(line, width);
    }
    if (logical_row == 12 + target_row_offset) {
        std::string text = std::format("Press \033[38;5;183m[Ctrl-P]\033[0m or \033[38;5;183mClick Mouse\033[0m to pause");
        int spaces = std::max(0, (width - 38) / 2);
        std::string line = std::string(spaces, ' ') + text;
        return format_to_width(line, width);
    }
    return format_to_width("", width);
}

auto RegisterPane::render_registers_single_column(const simrv::core::ArchState& st, int logical_row, int width) -> std::string {
    if (logical_row >= 0 && logical_row < 32) {
        int reg = logical_row;
        if (page_ == TuiRegPage::GPR) {
            auto val = st.regs.read(static_cast<RegId>(reg));
            std::string name = kRegNames.at(static_cast<std::size_t>(reg));
            bool changed = paused_ && (cached_gpr_.at(static_cast<std::size_t>(reg)) != val);
            std::string c = changed ? kSakuraPeach : kSakuraMint;
            std::string col_color = std::format(" {}x{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:0{}x}\033[0m",
                                                kSakuraText, reg, kSakuraVal, name, c, val, simrv::xlen::kXLenHexDigits);
            return format_to_width(col_color, width);
        } else if (page_ == TuiRegPage::FPR) {
            auto val = st.regs.read_fp(static_cast<RegId>(reg));
            std::string name = kFpRegNames.at(static_cast<std::size_t>(reg));
            bool changed = paused_ && (cached_fpr_.at(static_cast<std::size_t>(reg)) != val);
            std::string c = changed ? kSakuraPeach : kSakuraMint;
            std::string col_color = std::format(" {}f{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:016x}\033[0m",
                                                kSakuraText, reg, kSakuraVal, name, c, val);
            return format_to_width(col_color, width);
        } else if (page_ == TuiRegPage::VEC) {
            std::string col_color = std::format(" {}v{:<2}\033[0m       : {}0x0000000000000000\033[0m",
                                                kSakuraText, reg, kSakuraMuted);
            return format_to_width(col_color, width);
        }
    }
    return format_to_width("", width);
}

auto RegisterPane::render_registers_double_column(const simrv::core::ArchState& st, int logical_row, int col_width, int right_width) -> std::string {
    if (logical_row >= 0 && logical_row < 16) {
        int reg1 = logical_row;
        int reg2 = logical_row + 16;

        if (page_ == TuiRegPage::GPR) {
            auto val1 = st.regs.read(static_cast<RegId>(reg1));
            auto val2 = st.regs.read(static_cast<RegId>(reg2));

            std::string name1 = kRegNames.at(static_cast<std::size_t>(reg1));
            std::string name2 = kRegNames.at(static_cast<std::size_t>(reg2));

            bool changed = paused_ && (cached_gpr_.at(static_cast<std::size_t>(reg1)) != val1);
            std::string c1 = changed ? kSakuraPeach : kSakuraMint;
            bool changed2 = paused_ && (cached_gpr_.at(static_cast<std::size_t>(reg2)) != val2);
            std::string c2 = changed2 ? kSakuraPeach : kSakuraMint;

            std::string col1_color =
                std::format(" {}x{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:0{}x}\033[0m",
                            kSakuraText, reg1, kSakuraVal, name1, c1, val1, simrv::xlen::kXLenHexDigits);
            std::string col2_color =
                std::format(" {}x{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:0{}x}\033[0m",
                            kSakuraText, reg2, kSakuraVal, name2, c2, val2, simrv::xlen::kXLenHexDigits);

            return format_to_width(col1_color, col_width) +
                   format_to_width(col2_color, right_width);
        } else if (page_ == TuiRegPage::FPR) {
            auto val1 = st.regs.read_fp(static_cast<RegId>(reg1));
            auto val2 = st.regs.read_fp(static_cast<RegId>(reg2));

            std::string name1 = kFpRegNames.at(static_cast<std::size_t>(reg1));
            std::string name2 = kFpRegNames.at(static_cast<std::size_t>(reg2));

            bool changed = paused_ && (cached_fpr_.at(static_cast<std::size_t>(reg1)) != val1);
            std::string c1 = changed ? kSakuraPeach : kSakuraMint;
            bool changed2 = paused_ && (cached_fpr_.at(static_cast<std::size_t>(reg2)) != val2);
            std::string c2 = changed2 ? kSakuraPeach : kSakuraMint;

            std::string col1_color = std::format(
                " {}f{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:016x}\033[0m", kSakuraText, reg1,
                kSakuraVal, name1, c1, val1);
            std::string col2_color = std::format(
                " {}f{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:016x}\033[0m", kSakuraText, reg2,
                kSakuraVal, name2, c2, val2);

            return format_to_width(col1_color, col_width) +
                   format_to_width(col2_color, right_width);
        } else if (page_ == TuiRegPage::VEC) {
            std::string col1_color = std::format(
                " {}v{:<2}\033[0m       : {}0x0000000000000000\033[0m", kSakuraText, reg1, kSakuraMuted);
            std::string col2_color = std::format(
                " {}v{:<2}\033[0m       : {}0x0000000000000000\033[0m", kSakuraText, reg2, kSakuraMuted);

            return format_to_width(col1_color, col_width) +
                   format_to_width(col2_color, right_width);
        }
    }
    return format_to_width("", col_width + right_width);
}

auto RegisterPane::render_pipeline_stages(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string {
    if (machine_.s_cycle_accurate) {
        return render_pipeline_stages_cycle_accurate(cpu, logical_row, col_width, right_width);
    } else {
        return render_pipeline_stages_functional(cpu, logical_row, col_width, right_width);
    }
}

auto RegisterPane::render_pipeline_stages_cycle_accurate(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string {
    int const val = logical_row - 16;
    if (val >= 0 && val <= 5) {
        return render_pipeline_stages_ca_core(cpu, val, col_width + right_width);
    }
    if (val >= 6 && val <= 9) {
        return render_pipeline_stages_ca_hazards(cpu, val, col_width, right_width);
    }
    return render_pipeline_stages_ca_pred(cpu, val, col_width + right_width);
}

auto RegisterPane::render_pipeline_stages_ca_core(const simrv::core::CPU& cpu, int stage_idx, int width) -> std::string {
    auto& ps = cpu.pipeline_sim;
    bool is_raw_stalled = is_raw_hazard_stalled(ps);

    switch (stage_idx) {
        case 0:
            return section_line("Pipeline Stages (Cycle-Accurate Mode)", width);
        case 1:
            {
                std::string stall_type = ps.tlb_stall_remaining_ > 0 ? "TLB Miss" : "ICache Miss";
                uint32_t stall_rem = ps.tlb_stall_remaining_ > 0 ? ps.tlb_stall_remaining_ : ps.icache_stall_remaining_;
                return format_to_width(std::format("  \033[1;38;5;189mIF\033[0m  : {}", get_stage_desc(ps.f_reg_, stall_rem, stall_type)), width);
            }
        case 2:
            return format_to_width(std::format("  \033[1;38;5;189mID\033[0m  : {}", get_stage_desc(ps.d_reg_, 0, "", is_raw_stalled)), width);
        case 3:
            return format_to_width(std::format("  \033[1;38;5;189mEX\033[0m  : {}", get_stage_desc(ps.e_reg_, ps.div_busy_cycles_remaining_, "Divider")), width);
        case 4:
            return format_to_width(std::format("  \033[1;38;5;189mMEM\033[0m : {}", get_stage_desc(ps.m_reg_, ps.dcache_stall_remaining_, "DCache Miss")), width);
        case 5:
            return format_to_width(std::format("  \033[1;38;5;189mWB\033[0m  : {}", get_stage_desc(ps.w_reg_, 0, "")), width);
        default:
            return format_to_width("", width);
    }
}

auto RegisterPane::render_pipeline_stages_ca_hazards(const simrv::core::CPU& cpu, int stage_idx, int col_width, int right_width) -> std::string {
    auto& ps = cpu.pipeline_sim;
    int const width = col_width + right_width;
    bool is_raw_stalled = is_raw_hazard_stalled(ps);

    auto [raw_status, raw_color] = get_stall_status(is_raw_stalled);
    auto [div_status, div_color] = get_stall_status(ps.div_busy_cycles_remaining_ > 0);
    auto [ic_status, ic_color] = get_stall_status(ps.icache_stall_remaining_ > 0);
    auto [dc_status, dc_color] = get_stall_status(ps.dcache_stall_remaining_ > 0);
    auto [tlb_status, tlb_color] = get_stall_status(ps.tlb_stall_remaining_ > 0);
    auto [ctrl_status, ctrl_color] = get_stall_status(ps.control_bubble_remaining_ > 0, "Redirecting");

    switch (stage_idx) {
        case 6:
            return section_line("Active Stalls & Hazards", width);
        case 7:
            return render_pair("RAW Stall", raw_status, raw_color,
                               "Divider St", div_status, div_color,
                               col_width, right_width, 10);
        case 8:
            return render_pair("ICache St", ic_status, ic_color,
                               "DCache St", dc_status, dc_color,
                               col_width, right_width, 10);
        case 9:
            return render_pair("TLB Stall", tlb_status, tlb_color,
                               "Ctrl St", ctrl_status, ctrl_color,
                               col_width, right_width, 10);
        default:
            return format_to_width("", width);
    }
}

auto RegisterPane::render_pipeline_stages_ca_pred(const simrv::core::CPU& cpu, int stage_idx, int width) -> std::string {
    auto& ps = cpu.pipeline_sim;

    switch (stage_idx) {
        case 10:
            return section_line("Branch Prediction & BTB", width);
        case 11:
            {
                Register pc = get_active_branch_pc(ps);
                std::string bht_str = get_bht_string(ps, pc);
                return format_to_width(std::format("  {}BHT State\033[0m : {}{}\033[0m", kSakuraText, kSakuraVal, bht_str), width);
            }
        case 12:
            {
                Register pc = get_active_branch_pc(ps);
                std::string btb_str = get_btb_string(ps, pc);
                return format_to_width(std::format("  {}BTB State\033[0m : {}{}\033[0m", kSakuraText, kSakuraVal, btb_str), width);
            }
        case 13:
            return section_line("End Pipeline Visualizer", width);
        default:
            return format_to_width("", width);
    }
}

auto RegisterPane::render_pipeline_stages_functional(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string {
    if (logical_row >= 0 && logical_row < 16) {
        return render_pipeline_stages_functional_low(cpu, logical_row, col_width, right_width);
    }
    return render_pipeline_stages_functional_high(cpu, logical_row, col_width, right_width);
}

auto RegisterPane::render_pipeline_stages_functional_low(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string {
    if (logical_row >= 0 && logical_row <= 7) {
        return render_pipeline_stages_functional_low_part1(cpu, logical_row, col_width, right_width);
    }
    if (logical_row >= 8 && logical_row <= 15) {
        return render_pipeline_stages_functional_low_part2(cpu, logical_row, col_width, right_width);
    }
    return format_to_width(std::format(" {}Pipeline page\033[0m", kSakuraMuted), col_width + right_width);
}

auto RegisterPane::render_pipeline_stages_functional_low_part1(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string {
    int const width = col_width + right_width;
    auto& ctx = cpu.pipeline_context;
    switch (logical_row) {
        case 0:
            return section_line("── IF/CVT", width);
        case 1:
            return render_pair(
                "cpc", std::format("0x{:0{}x}", ctx.cpc, simrv::xlen::kXLenHexDigits),
                kSakuraMint, "ir_org", std::format("0x{:08x}", ctx.ir_org), kSakuraVal,
                col_width, right_width, 8);
        case 2:
            return render_pair("ir", std::format("0x{:08x}", ctx.ir), kSakuraVal,
                               "cinsn", std::format("0x{:08x}", ctx.cinsn), kSakuraVal,
                               col_width, right_width, 8);
        case 3:
            return section_line("── ID", width);
        case 4:
            return render_pair(
                "opcode", std::to_string(std::to_underlying(ctx.opcode)), kSakuraVal,
                "funct3", std::to_string(std::to_underlying(ctx.funct3)), kSakuraVal,
                col_width, right_width, 8);
        case 5:
            return render_pair(
                "rd/rs1",
                std::format("{}/{}", std::to_underlying(ctx.rd),
                             std::to_underlying(ctx.rs1)),
                kSakuraVal, "rs2/f7",
                std::format("{}/0x{:x}", std::to_underlying(ctx.rs2), ctx.funct7),
                kSakuraVal, col_width, right_width, 8);
        case 6:
            return render_pair(
                "imm", std::format("0x{:0{}x}", ctx.imm, simrv::xlen::kXLenHexDigits),
                kSakuraVal, "funct12", std::format("0x{:x}", ctx.funct12), kSakuraVal,
                col_width, right_width, 8);
        case 7:
            return section_line("── OF/EX", width);
        default:
            return format_to_width("", width);
    }
}

auto RegisterPane::render_pipeline_stages_functional_low_part2(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string {
    int const width = col_width + right_width;
    auto& ctx = cpu.pipeline_context;
    switch (logical_row) {
        case 8:
            return render_pair(
                "rrs1", std::format("0x{:0{}x}", ctx.rrs1, simrv::xlen::kXLenHexDigits),
                kSakuraMint, "rrs2",
                std::format("0x{:0{}x}", ctx.rrs2, simrv::xlen::kXLenHexDigits),
                kSakuraMint, col_width, right_width, 8);
        case 9:
            return render_pair(
                "jmp_pc",
                std::format("0x{:0{}x}", ctx.jmp_pc, simrv::xlen::kXLenHexDigits),
                kSakuraMint, "taken", ctx.tkn ? "yes" : "no", kSakuraVal,
                col_width, right_width, 8);
        case 10:
            return render_pair(
                "wb_data",
                std::format("0x{:0{}x}", ctx.wb_data, simrv::xlen::kXLenHexDigits),
                kSakuraMint, "wb_csr",
                std::format("0x{:0{}x}", ctx.wb_data_csr, simrv::xlen::kXLenHexDigits),
                kSakuraVal, col_width, right_width, 8);
        case 11:
            return section_line("── MEM/FP", width);
        case 12:
            return render_pair(
                "mem_addr",
                std::format("0x{:0{}x}", ctx.mem_addr, simrv::xlen::kXLenHexDigits),
                kSakuraMint, "mem_w",
                std::format("0x{:0{}x}", ctx.mem_wdata, simrv::xlen::kXLenHexDigits),
                kSakuraMint, col_width, right_width, 8);
        case 13:
            return render_pair(
                "mem_r",
                std::format("0x{:0{}x}", ctx.mem_rdata, simrv::xlen::kXLenHexDigits),
                kSakuraMint, "fp_wb", std::format("0x{:016x}", ctx.fp_wb_data),
                kSakuraVal, col_width, right_width, 8);
        case 14:
            return render_pair("fp_wb_en", ctx.fp_wb_enable ? "on" : "off", kSakuraVal,
                               "int<-fp", ctx.int_wb_from_fp ? "on" : "off",
                               kSakuraVal, col_width, right_width, 8);
        case 15:
            return section_line("── TRAP/TLB", width);
        default:
            return format_to_width("", width);
    }
}

auto RegisterPane::render_pipeline_stages_functional_high(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string {
    int const width = col_width + right_width;
    auto& ctx = cpu.pipeline_context;
    auto exc_text = ctx.pending_exception.has_value()
                        ? std::to_string(std::to_underlying(ctx.pending_exception.value()))
                        : std::string("none");

    switch (logical_row) {
        case 16:
            return render_pair(
                "exc", exc_text, kSakuraPeach, "tval",
                std::format("0x{:0{}x}", ctx.pending_tval, simrv::xlen::kXLenHexDigits),
                kSakuraVal, col_width, right_width, 8);
        case 17:
            return render_pair(
                "padr1", std::format("0x{:0{}x}", ctx.padr1, simrv::xlen::kXLenHexDigits),
                kSakuraVal, "padr2",
                std::format("0x{:0{}x}", ctx.padr2, simrv::xlen::kXLenHexDigits),
                kSakuraVal, col_width, right_width, 8);
        case 18:
            return render_pair(
                "rcsr", std::format("0x{:0{}x}", ctx.rcsr, simrv::xlen::kXLenHexDigits),
                kSakuraVal, "funct5", std::to_string(std::to_underlying(ctx.funct5)),
                kSakuraVal, col_width, right_width, 8);
        case 19:
            return section_line("── End Pipeline Snapshot", width);
        default:
            return format_to_width("", width);
    }
}

auto RegisterPane::render_system_state(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string {
    auto const& st = cpu.state();
    int const width = col_width + right_width;
    int label_pad = (width < 50) ? 0 : ((width < 65) ? 5 : 8);

    if (logical_row == 16) {
        return section_line("CSRs & Privilege State", width);
    }
    if (logical_row == 17) {
        std::string priv_str = (st.priv == PrivilegeLevel::Machine)      ? "Machine"
                               : (st.priv == PrivilegeLevel::Supervisor) ? "Supervisor"
                                                                         : "User";
        return render_pair("pc", std::format("0x{:0{}x}", st.pc, simrv::xlen::kXLenHexDigits),
                           kSakuraMint, "priv", priv_str, kSakuraPink, col_width, right_width, label_pad);
    }
    if (logical_row == 18) {
        std::string misa_str = simrv::xlen::resolve_misa_string(st.misa);
        return render_pair("mstatus",
                           std::format("0x{:0{}x}", st.mstatus, simrv::xlen::kXLenHexDigits),
                           kSakuraVal, "misa", misa_str, kSakuraVal, col_width, right_width, label_pad);
    }
    if (logical_row == 19) {
        return render_pair(
            "mie", std::format("0x{:0{}x}", st.mie, simrv::xlen::kXLenHexDigits), kSakuraVal,
            "mip", std::format("0x{:0{}x}", st.mip, simrv::xlen::kXLenHexDigits), kSakuraVal,
            col_width, right_width, label_pad);
    }
    if (logical_row == 20) {
        return render_pair(
            "mtvec", std::format("0x{:0{}x}", st.mtvec, simrv::xlen::kXLenHexDigits),
            kSakuraVal, "mepc", std::format("0x{:0{}x}", st.mepc, simrv::xlen::kXLenHexDigits),
            kSakuraVal, col_width, right_width, label_pad);
    }
    if (logical_row == 21) {
        return render_pair(
            "stvec", std::format("0x{:0{}x}", st.stvec, simrv::xlen::kXLenHexDigits),
            kSakuraVal, "sepc", std::format("0x{:0{}x}", st.sepc, simrv::xlen::kXLenHexDigits),
            kSakuraVal, col_width, right_width, label_pad);
    }
    if (logical_row == 22) {
        return render_pair(
            "mtval", std::format("0x{:0{}x}", st.mtval, simrv::xlen::kXLenHexDigits),
            kSakuraVal, "satp", std::format("0x{:0{}x}", st.satp, simrv::xlen::kXLenHexDigits),
            kSakuraVal, col_width, right_width, label_pad);
    }
    if (logical_row == 23) {
        return render_pair(
            "scause", std::format("0x{:0{}x}", st.scause, simrv::xlen::kXLenHexDigits),
            kSakuraVal, "stval",
            std::format("0x{:0{}x}", st.stval, simrv::xlen::kXLenHexDigits), kSakuraVal,
            col_width, right_width, label_pad);
    }
    if (logical_row == 24) {
        return render_pair(
            "medeleg", std::format("0x{:0{}x}", st.medeleg, simrv::xlen::kXLenHexDigits),
            kSakuraVal, "mideleg",
            std::format("0x{:0{}x}", st.mideleg, simrv::xlen::kXLenHexDigits), kSakuraVal,
            col_width, right_width, label_pad);
    }
    return format_to_width("", width);
}

auto RegisterPane::render_machine_performance_stats(const simrv::core::CPU& cpu, int adj_logical_row, int width) -> std::string {
    if (adj_logical_row >= 25 && adj_logical_row <= 30) {
        return render_machine_performance_stats_core(cpu, adj_logical_row, width);
    }
    return render_machine_performance_stats_sys(cpu, adj_logical_row, width);
}

auto RegisterPane::render_machine_performance_stats_core(const simrv::core::CPU& cpu, int adj_logical_row, int width) -> std::string {
    if (adj_logical_row == 25) {
        return section_line("Performance & Machine Stats", width);
    }
    if (adj_logical_row == 26) {
        std::string insns = std::format("  Executed Insns : {}{}\033[0m", kSakuraMint, simrv::util::format_with_commas(cpu.e_icount));
        return format_to_width(insns, width);
    }
    if (adj_logical_row == 27) {
        double sim_time_seconds = static_cast<double>(cpu.clint_mmio.mtime) / 10000000.0;
        std::string time = std::format("  Simulated Time : {}{:.6f} s\033[0m {}(0x{:x})\033[0m",
                                       kSakuraMint, sim_time_seconds, kSakuraMuted, cpu.clint_mmio.mtime);
        return format_to_width(time, width);
    }
    if (adj_logical_row == 28) {
        std::string time = std::format("  Active Runtime : {}{:.6f} s\033[0m",
                                       kSakuraMint, active_runtime_);
        return format_to_width(time, width);
    }
    if (adj_logical_row == 29) {
        std::string mode = std::format("  Simulation Mode: {}Functional (High-Perf)\033[0m", kSakuraVal);
        return format_to_width(mode, width);
    }
    if (adj_logical_row == 30) {
        std::string extensions = simrv::xlen::resolve_misa_string(cpu.state().misa);
        std::string isa = std::format("  ISA Extensions : {}{}\033[0m", kSakuraVal, extensions);
        return format_to_width(isa, width);
    }
    return format_to_width("", width);
}

auto RegisterPane::render_machine_performance_stats_sys([[maybe_unused]] const simrv::core::CPU& cpu, int adj_logical_row, int width) -> std::string {
    if (adj_logical_row == 31) {
        std::string mem_name = machine_.s_fn_memimg;
        auto pos = mem_name.find_last_of("/\\");
        if (pos != std::string::npos) mem_name = mem_name.substr(pos + 1);
        if (mem_name.empty()) mem_name = "None";
        std::string img = std::format("  Memory Image   : {}{}\033[0m", kSakuraSky, mem_name);
        return format_to_width(img, width);
    }
    if (adj_logical_row == 32) {
        std::string dsk_name = "Disabled";
        if (machine_.s_use_disk) {
            dsk_name = machine_.s_fn_dskimg;
            auto pos = dsk_name.find_last_of("/\\");
            if (pos != std::string::npos) dsk_name = dsk_name.substr(pos + 1);
            if (dsk_name.empty()) dsk_name = "None";
        }
        std::string dsk = std::format("  Disk Image     : {}{}\033[0m", kSakuraSky, dsk_name);
        return format_to_width(dsk, width);
    }
    if (adj_logical_row == 33) {
        uint64_t max_val = 1;
        for (auto val : kips_history_) {
            if (val > max_val) max_val = val;
        }

        std::string suffix =
            std::format("] {} Max:{}", simrv::util::format_with_commas(kips_), simrv::util::format_with_commas(max_val));
        std::string prefix = "  Speed (KIPS)   : [";
        int spark_width =
            width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length());
        if (spark_width < 5) spark_width = 5;

        std::string spark = get_sparkline_string(spark_width);
        std::string color = std::format(
            "  {}Speed (KIPS)\033[0m   : [{}{}\033[0m] {}{}\033[0m {}{}Max:{}\033[0m",
            kSakuraText, kSakuraMint, spark, kSakuraMint, simrv::util::format_with_commas(kips_), kSakuraMuted, kSakuraMuted, simrv::util::format_with_commas(max_val));

        return format_to_width(color, width);
    }
    if (adj_logical_row == 34) {
        return format_to_width("", width);
    }
    return format_to_width("", width);
}

auto RegisterPane::render_cycle_accurate_stats(const simrv::core::CPU& cpu, int adj_logical_row, int width) -> std::string {
    if (adj_logical_row >= 25 && adj_logical_row <= 29) {
        return render_cycle_accurate_core_stats(cpu, adj_logical_row, width);
    }
    if (adj_logical_row >= 30 && adj_logical_row <= 32) {
        return render_cycle_accurate_hazard_stats(cpu, adj_logical_row, width);
    }
    if (adj_logical_row >= 33 && adj_logical_row <= 34) {
        return render_cycle_accurate_mix_stats(cpu, adj_logical_row, width);
    }
    return render_cycle_accurate_hw_info(cpu, adj_logical_row, width);
}

auto RegisterPane::render_cycle_accurate_core_stats(const simrv::core::CPU& cpu, int adj_logical_row, int width) -> std::string {
    if (adj_logical_row == 25) {
        return section_line("Statistics & Performance", width);
    }

    if (adj_logical_row == 26) {
        uint64_t i_hits = cpu.icache.hit_count();
        uint64_t i_misses = cpu.icache.miss_count();
        uint64_t i_total = i_hits + i_misses;
        double i_ratio =
            (i_total == 0) ? 0.0 : static_cast<double>(i_hits) / static_cast<double>(i_total);

        std::string suffix = std::format(" {:5.1f}% (H:{} M:{})", i_ratio * 100.0,
                                         format_compact(i_hits), format_compact(i_misses));
        std::string prefix = "  L1-I Cache     : [";
        int bar_width =
            width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length()) - 1;
        if (bar_width < 5) bar_width = 5;

        std::string bar = make_progress_bar(i_ratio, bar_width, kSakuraSky);
        std::string color = std::format(
            "  {}L1-I Cache\033[0m     : [{}] {}{:5.1f}%\033[0m {}(H:{} M:{})\033[0m",
            kSakuraText, bar, kSakuraSky, i_ratio * 100.0, kSakuraMuted, format_compact(i_hits), format_compact(i_misses));

        return format_to_width(color, width);
    }

    if (adj_logical_row == 27) {
        uint64_t d_hits = cpu.dcache.hit_count();
        uint64_t d_misses = cpu.dcache.miss_count();
        uint64_t d_total = d_hits + d_misses;
        double d_ratio =
            (d_total == 0) ? 0.0 : static_cast<double>(d_hits) / static_cast<double>(d_total);

        std::string suffix = std::format(" {:5.1f}% (H:{} M:{})", d_ratio * 100.0,
                                         format_compact(d_hits), format_compact(d_misses));
        std::string prefix = "  L1-D Cache     : [";
        int bar_width =
            width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length()) - 1;
        if (bar_width < 5) bar_width = 5;

        std::string bar = make_progress_bar(d_ratio, bar_width, kSakuraPink);
        std::string color = std::format(
            "  {}L1-D Cache\033[0m     : [{}] {}{:5.1f}%\033[0m {}(H:{} M:{})\033[0m",
            kSakuraText, bar, kSakuraPink, d_ratio * 100.0, kSakuraMuted, format_compact(d_hits), format_compact(d_misses));

        return format_to_width(color, width);
    }

    uint64_t cycles = cpu.clint_mmio.mcycle;
    uint64_t icount = cpu.e_icount;
    double ipc = (cycles == 0) ? 0.0 : static_cast<double>(icount) / static_cast<double>(cycles);
    double cpi = (icount == 0) ? 0.0 : static_cast<double>(cycles) / static_cast<double>(icount);

    if (adj_logical_row == 28) {
        std::string color = std::format(
            "  {}IPC / CPI\033[0m      : {}{:.2f} IPC\033[0m  /  {}{:.2f} CPI\033[0m",
            kSakuraText, kSakuraMint, ipc, kSakuraPeach, cpi);
        return format_to_width(color, width);
    }

    uint64_t stalls = cpu.pipeline_sim.stall_cycles();
    uint64_t bubbles = cpu.pipeline_sim.bubble_cycles();
    uint64_t total_stalls_bubbles = stalls + bubbles;
    double stall_pct = (cycles == 0) ? 0.0 : (static_cast<double>(total_stalls_bubbles) * 100.0) / static_cast<double>(cycles);

    if (adj_logical_row == 29) {
        std::string color = std::format(
            "  {}Stall Ratio\033[0m    : {}{:5.1f}%\033[0m (Stall:{} clk, Bubble:{} clk)",
            kSakuraText, kSakuraCoral, stall_pct, format_compact(stalls), format_compact(bubbles));
        return format_to_width(color, width);
    }

    return format_to_width("", width);
}

auto RegisterPane::render_cycle_accurate_hazard_stats(const simrv::core::CPU& cpu, int adj_logical_row, int width) -> std::string {
    uint64_t cycles = cpu.clint_mmio.mcycle;
    uint64_t data_stalls = cpu.pipeline_sim.data_hazard_stalls();
    uint64_t ctrl_bubbles = cpu.pipeline_sim.control_hazard_bubbles();
    uint64_t ic_stalls = cpu.pipeline_sim.icache_stalls();
    uint64_t dc_stalls = cpu.pipeline_sim.dcache_stalls();
    uint64_t cache_stalls = ic_stalls + dc_stalls;

    if (adj_logical_row == 30) {
        double data_pct = (cycles == 0) ? 0.0 : (static_cast<double>(data_stalls) * 100.0) / static_cast<double>(cycles);
        std::string color = std::format(
            "    {}- Data RAW\033[0m   : {}{:>10}\033[0m clk {}({:5.1f}%)\033[0m",
            kSakuraMuted, kSakuraPeach, simrv::util::format_with_commas(data_stalls), kSakuraMuted, data_pct);
        return format_to_width(color, width);
    }

    if (adj_logical_row == 31) {
        double ctrl_pct = (cycles == 0) ? 0.0 : (static_cast<double>(ctrl_bubbles) * 100.0) / static_cast<double>(cycles);
        std::string color = std::format(
            "    {}- Control\033[0m    : {}{:>10}\033[0m clk {}({:5.1f}%)\033[0m",
            kSakuraMuted, kSakuraPeach, simrv::util::format_with_commas(ctrl_bubbles), kSakuraMuted, ctrl_pct);
        return format_to_width(color, width);
    }

    if (adj_logical_row == 32) {
        double cache_pct = (cycles == 0) ? 0.0 : (static_cast<double>(cache_stalls) * 100.0) / static_cast<double>(cycles);
        std::string color = std::format(
            "    {}- Cache\033[0m      : {}{:>10}\033[0m clk {}({:5.1f}%)\033[0m",
            kSakuraMuted, kSakuraPeach, simrv::util::format_with_commas(cache_stalls), kSakuraMuted, cache_pct);
        return format_to_width(color, width);
    }

    return format_to_width("", width);
}

auto RegisterPane::render_cycle_accurate_mix_stats(const simrv::core::CPU& cpu, int adj_logical_row, int width) -> std::string {
    if (adj_logical_row == 33) {
        uint64_t max_val = 1;
        for (auto val : kips_history_) {
            if (val > max_val) max_val = val;
        }

        std::string suffix =
            std::format("] {} Max:{}", simrv::util::format_with_commas(kips_), simrv::util::format_with_commas(max_val));
        std::string prefix = "  Speed (KIPS)   : [";
        int spark_width =
            width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length());
        if (spark_width < 5) spark_width = 5;

        std::string spark = get_sparkline_string(spark_width);
        std::string color = std::format(
            "  {}Speed (KIPS)\033[0m   : [{}{}\033[0m] {}{}\033[0m {}{}Max:{}\033[0m",
            kSakuraText, kSakuraMint, spark, kSakuraMint, simrv::util::format_with_commas(kips_), kSakuraMuted, kSakuraMuted, simrv::util::format_with_commas(max_val));

        return format_to_width(color, width);
    }

    if (adj_logical_row == 34) {
        uint64_t alu_count = 0;
        uint64_t mem_count = 0;
        uint64_t ctrl_count = 0;
        uint64_t sys_count = 0;

        for (int i = 0; i < OperationIdCount; ++i) {
            uint64_t count = cpu.e_instmix.at(static_cast<std::size_t>(i));
            InstCategory cat = get_inst_category(i);
            if (cat == InstCategory::CTRL) {
                ctrl_count += count;
            } else if (cat == InstCategory::MEM) {
                mem_count += count;
            } else if (cat == InstCategory::SYS) {
                sys_count += count;
            } else {
                alu_count += count;
            }
        }

        uint64_t total = alu_count + mem_count + ctrl_count + sys_count;
        double alu_p = (total == 0)
                           ? 0.0
                           : static_cast<double>(alu_count * 100ULL) / static_cast<double>(total);
        double mem_p = (total == 0)
                           ? 0.0
                           : static_cast<double>(mem_count * 100ULL) / static_cast<double>(total);
        double ctrl_p = (total == 0)
                            ? 0.0
                            : static_cast<double>(ctrl_count * 100ULL) / static_cast<double>(total);
        double sys_p = (total == 0)
                           ? 0.0
                           : static_cast<double>(sys_count * 100ULL) / static_cast<double>(total);

        std::string color = std::format(
            "  {}Inst Mix\033[0m       : {}ALU:{:.1f}%\033[0m {}MEM:{:.1f}%\033[0m "
            "{}CTRL:{:.1f}%\033[0m {}SYS:{:.1f}%\033[0m",
            kSakuraText, kSakuraMint, alu_p, kSakuraSky, mem_p, kSakuraPeach, ctrl_p, kSakuraPink, sys_p);

        return format_to_width(color, width);
    }

    return format_to_width("", width);
}

auto RegisterPane::render_cycle_accurate_hw_info(const simrv::core::CPU& cpu, int adj_logical_row, int width) -> std::string {
    if (adj_logical_row == 35) {
        return section_line("Machine & Hardware Info", width);
    }

    if (adj_logical_row == 36) {
        double sim_time_seconds = static_cast<double>(cpu.clint_mmio.mtime) / 10000000.0;
        std::string time = std::format("  Simulated Time : {}{:.6f} s\033[0m {}(0x{:x})\033[0m",
                                       kSakuraMint, sim_time_seconds, kSakuraMuted, cpu.clint_mmio.mtime);
        return format_to_width(time, width);
    }

    if (adj_logical_row == 37) {
        std::string time = std::format("  Active Runtime : {}{:.6f} s\033[0m",
                                       kSakuraMint, active_runtime_);
        return format_to_width(time, width);
    }

    if (adj_logical_row == 38) {
        std::string mode = std::format("  Simulation Mode: {}Cycle-Accurate (CA)\033[0m", kSakuraVal);
        return format_to_width(mode, width);
    }

    if (adj_logical_row == 39) {
        std::string extensions = simrv::xlen::resolve_misa_string(cpu.state().misa);
        std::string isa = std::format("  ISA Extensions : {}{}\033[0m", kSakuraVal, extensions);
        return format_to_width(isa, width);
    }

    if (adj_logical_row == 40) {
        std::string mem_name = machine_.s_fn_memimg;
        auto pos = mem_name.find_last_of("/\\");
        if (pos != std::string::npos) mem_name = mem_name.substr(pos + 1);
        if (mem_name.empty()) mem_name = "None";
        std::string img = std::format("  Memory Image   : {}{}\033[0m", kSakuraSky, mem_name);
        return format_to_width(img, width);
    }

    if (adj_logical_row == 41) {
        std::string dsk_name = "Disabled";
        if (machine_.s_use_disk) {
            dsk_name = machine_.s_fn_dskimg;
            auto pos = dsk_name.find_last_of("/\\");
            if (pos != std::string::npos) dsk_name = dsk_name.substr(pos + 1);
            if (dsk_name.empty()) dsk_name = "None";
        }
        std::string dsk = std::format("  Disk Image     : {}{}\033[0m", kSakuraSky, dsk_name);
        return format_to_width(dsk, width);
    }

    if (adj_logical_row == 42) {
        return format_to_width("", width);
    }

    return format_to_width("", width);
}

auto RegisterPane::render_debug_state(int debug_row, int width) -> std::string {
    auto const& cpu = machine_.cpu;
    auto const& st = cpu.state();
    int col_width = width / 2;
    int right_width = width - col_width;

    if (debug_row == 0) {
        return section_line("Debug Diagnostics", width);
    }
    if (debug_row == 1) {
        std::string sym = machine_.symbols.lookup(st.pc);
        if (sym.empty()) {
            sym = "none";
        } else {
            sym = "<" + sym + ">";
        }
        return format_to_width(std::format(" {}{:<8}\033[0m: {}{}\033[0m", kSakuraText, "symbol", kSakuraPeach, sym), width);
    }
    if (debug_row == 2) {
        std::string gdb_status = "disabled";
        if (machine_.gdb_stub) {
            gdb_status = machine_.gdb_stub->is_connected() ? "connected" : "listening";
        }
        std::string lockstep_status = machine_.spike_lockstep ? "active" : "disabled";
        return render_pair("gdb_stub", gdb_status, machine_.gdb_stub ? kSakuraMint : kSakuraMuted,
                           "lockstep", lockstep_status, machine_.spike_lockstep ? kSakuraMint : kSakuraMuted,
                           col_width, right_width, 8);
    }
    if (debug_row == 3) {
        std::string tohost_str = std::format("0x{:x}", machine_.tohost);
        std::string traplog_status = machine_.s_traplog_mode ? "active" : "disabled";
        return render_pair("tohost", tohost_str, machine_.tohost != 0 ? kSakuraPeach : kSakuraVal,
                           "traplog", traplog_status, machine_.s_traplog_mode ? kSakuraMint : kSakuraMuted,
                           col_width, right_width, 8);
    }
    return format_to_width("", width);
}

auto RegisterPane::render_registers_or_pipeline(const simrv::core::CPU& cpu, const simrv::core::ArchState& st, int logical_row, int col_width, int right_width, int width, bool single_column) -> std::string {
    if (single_column) {
        if (logical_row >= 0 && logical_row < 32) {
            return render_registers_single_column(st, logical_row, width);
        }
    } else {
        if (logical_row >= 0 && logical_row < 16) {
            if (page_ == TuiRegPage::GPR || page_ == TuiRegPage::FPR || page_ == TuiRegPage::VEC) {
                return render_registers_double_column(st, logical_row, col_width, right_width);
            } else if (page_ == TuiRegPage::PIPELINE) {
                return render_pipeline_stages(cpu, logical_row, col_width, right_width);
            }
        }
    }
    return "";
}

auto RegisterPane::render_system_or_pipeline_extended(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width, bool single_column) -> std::string {
    if (logical_row >= 16 && logical_row <= 24) {
        if (page_ == TuiRegPage::PIPELINE) {
            return render_pipeline_stages(cpu, logical_row, col_width, right_width);
        } else if (!single_column) {
            return render_system_state(cpu, logical_row, col_width, right_width);
        }
    }
    return "";
}

auto RegisterPane::render_perf_or_debug(const simrv::core::CPU& cpu, int logical_row, int width, bool single_column) -> std::string {
    int const total_logical_rows = get_total_rows(width);
    int const adj_base_rows = total_logical_rows - (machine_.s_debug_mode ? 4 : 0);

    if (machine_.s_debug_mode && logical_row >= adj_base_rows && logical_row < total_logical_rows) {
        return render_debug_state(logical_row - adj_base_rows, width);
    }

    int const adj_logical_row = (single_column && logical_row >= 32) ? (logical_row - 16) : logical_row;

    if (!machine_.s_cycle_accurate) {
        return render_machine_performance_stats(cpu, adj_logical_row, width);
    } else {
        return render_cycle_accurate_stats(cpu, adj_logical_row, width);
    }
}

auto RegisterPane::get_row_uncached(int logical_row, int width) -> std::string {
    auto const& cpu = machine_.cpu;
    auto const& st = cpu.state();
    int const col_width = width / 2;
    int const right_width = width - col_width;
    bool const single_column = is_single_column(width);

    std::string res = render_registers_or_pipeline(cpu, st, logical_row, col_width, right_width, width, single_column);
    if (!res.empty()) {
        return res;
    }

    res = render_system_or_pipeline_extended(cpu, logical_row, col_width, right_width, single_column);
    if (!res.empty()) {
        return res;
    }

    return render_perf_or_debug(cpu, logical_row, width, single_column);
}

auto RegisterPane::render_row(int row_idx, int width) -> std::string {
    last_width_ = width;
    int const logical_row = row_idx + scroll_offset_;

    if (page_ == TuiRegPage::EXPLAIN) {
        auto explain_rows = get_explain_rows(width);
        int const total_logical_rows = static_cast<int>(explain_rows.size());
        if (logical_row >= total_logical_rows || logical_row < 0) {
            return format_to_width("", width);
        }
        return explain_rows.at(static_cast<std::size_t>(logical_row));
    }

    int const total_logical_rows = get_total_rows(width);
    if (logical_row >= total_logical_rows || logical_row < 0) {
        return format_to_width("", width);
    }

    bool const single_column = is_single_column(width);
    int const max_active_row = single_column ? 40 : 24;

    if (!paused_ && logical_row <= max_active_row) {
        return render_active_spinner(logical_row, width);
    }

    if (logical_row <= max_active_row) {
        std::string res = get_row_uncached(logical_row, width);
        if (static_cast<std::size_t>(logical_row) < cached_left_rows_.size()) {
            cached_left_rows_.at(static_cast<std::size_t>(logical_row)) = res;
        }
        return res;
    }

    return get_row_uncached(logical_row, width);
}

auto RegisterPane::get_sparkline_string(int width) -> std::string {
    if (kips_history_.empty()) {
        std::string res(static_cast<std::size_t>(width), ' ');
        return res;
    }
    uint64_t max_val = 1;
    for (auto val : kips_history_) {
        if (val > max_val) max_val = val;
    }

    std::string s;
    int history_size = static_cast<int>(kips_history_.size());
    int pad = width - history_size;
    if (pad > 0) {
        s += std::string(static_cast<std::size_t>(pad), ' ');
    }

    constexpr std::array<const char*, 8> blocks = {" ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    int start_hist = (history_size > width) ? (history_size - width) : 0;
    for (int i = start_hist; i < history_size; ++i) {
        double ratio = static_cast<double>(kips_history_.at(static_cast<std::size_t>(i))) /
                       static_cast<double>(max_val);
        int block_idx = static_cast<int>(ratio * 7.0);
        if (block_idx < 0) block_idx = 0;
        if (block_idx > 7) block_idx = 7;
        s += blocks.at(static_cast<std::size_t>(block_idx));
    }
    return s;
}

void RegisterPane::update_cache() {
    auto& st = machine_.cpu.state();
    for (int i = 0; i < 32; ++i) {
        cached_gpr_.at(static_cast<std::size_t>(i)) = st.regs.read(static_cast<RegId>(i));
        cached_fpr_.at(static_cast<std::size_t>(i)) = st.regs.read_fp(static_cast<RegId>(i));
    }
}

auto RegisterPane::get_explain_rows(int width) -> std::vector<std::string> {
    auto& cpu = machine_.cpu;
    auto& st = cpu.state();
    auto& ctx = cpu.pipeline_context;

    if (ctx.cpc != st.pc) {
        auto& mutable_cpu = const_cast<simrv::core::CPU&>(cpu);
        auto saved_pc = st.pc;
        bool fetch_success = mutable_cpu.fetch_stage(machine_, st.pc);
        if (fetch_success) {
            (void)mutable_cpu.decode_stage(machine_);
        }
        st.pc = saved_pc;
    }

    bool const is_compressed = (ctx.ir_org & 0x3) != 0x3;
    uint32_t decompressed_inst = ctx.ir;
    
    InstFormat const fmt = get_format(ctx.opcode);
    auto const [mnemonic, behavior_desc] = simrv::util::get_operation_details(ctx.op_id);

    bool const is_dst_fp = simrv::isa::is_destination_fp(ctx.opcode, ctx.op_id);
    bool const is_rs1_fpr = simrv::isa::is_rs1_fp(ctx.opcode, ctx.op_id);
    bool const is_rs2_fpr = simrv::isa::is_rs2_fp(ctx.opcode, ctx.op_id);

    std::string rd_name = get_reg_name(ctx.rd, is_dst_fp);
    std::string rs1_name = get_reg_name(ctx.rs1, is_rs1_fpr);
    std::string rs2_name = get_reg_name(ctx.rs2, is_rs2_fpr);

    std::string assembly = format_instruction_assembly(ctx, fmt, mnemonic, rd_name, rs1_name, rs2_name);

    std::vector<std::string> explain_rows;
    explain_rows.push_back(section_line("Instruction Explainer", width));

    std::string sym = machine_.symbols.lookup(st.pc);
    std::string pc_label = sym.empty() ? std::format("0x{:0{}x}", st.pc, simrv::xlen::kXLenHexDigits)
                                       : std::format("0x{:0{}x} <{}>", st.pc, simrv::xlen::kXLenHexDigits, sym);
    explain_rows.push_back(format_to_width(std::format("  {}PC     : {}{}\033[0m", kSakuraText, kSakuraMint, pc_label), width));

    std::string hex_str;
    if (is_compressed) {
        uint32_t raw_16 = ctx.ir_org & 0xFFFF;
        hex_str = std::format("0x{:04X} (compressed) -> 0x{:08X}", raw_16, decompressed_inst);
    } else {
        hex_str = std::format("0x{:08X}", ctx.ir_org);
    }
    explain_rows.push_back(format_to_width(std::format("  {}Hex    : {}{}\033[0m", kSakuraText, kSakuraMint, hex_str), width));

    std::string bin_str;
    if (is_compressed) {
        bin_str = std::format("{:016b}", ctx.ir_org & 0xFFFF);
    } else {
        bin_str = std::format("{:032b}", ctx.ir_org);
    }
    explain_rows.push_back(format_to_width(std::format("  {}Bin    : {}{}\033[0m", kSakuraText, kSakuraVal, bin_str), width));

    explain_rows.push_back(format_to_width(std::format("  {}Asm    : {}{}\033[0m", kSakuraText, kSakuraPeach, assembly), width));
    explain_rows.push_back(format_to_width(std::format("  {}Format : {}{}\033[0m", kSakuraText, kSakuraVal, get_format_name(fmt)), width));
    explain_rows.push_back(format_to_width(std::format("  {}ISA Ext: {}{}\033[0m", kSakuraText, kSakuraMint, simrv::util::get_isa_extension_name(ctx.op_id)), width));
    explain_rows.push_back(format_to_width("", width));

    explain_rows.push_back(section_line("Visual Bit Fields", width));
    auto visual_fields = render_visual_bitfields(fmt, ctx.ir_org, width);
    explain_rows.insert(explain_rows.end(), visual_fields.begin(), visual_fields.end());
    explain_rows.push_back(format_to_width("", width));

    explain_rows.push_back(section_line("Field Decoded Values", width));
    auto decoded_fields = render_field_decoded_values(st, ctx, fmt, rd_name, rs1_name, rs2_name, width);
    explain_rows.insert(explain_rows.end(), decoded_fields.begin(), decoded_fields.end());
    explain_rows.push_back(format_to_width("", width));

    bool const is_csr = (ctx.op_id >= CSRRW && ctx.op_id <= CSRRCI);
    if (is_csr) {
        uint32_t csr_addr = ctx.imm & 0xFFF;
        std::string csr_nm = simrv::util::csr_name(csr_addr);
        explain_rows.push_back(format_to_width(std::format("  CSR    : {} (addr 0x{:03X})", csr_nm, csr_addr), width));
        explain_rows.push_back(format_to_width("", width));
    }

    explain_rows.push_back(section_line("Instruction Description", width));
    std::string isa_ext = std::string(simrv::util::get_isa_extension_name(ctx.op_id));
    std::string full_desc = std::format("[ISA: {}] {}", isa_ext, behavior_desc);
    auto wrapped = wrap_text(full_desc, width - 4);
    for (const auto& line : wrapped) {
        explain_rows.push_back(format_to_width("  " + line, width));
    }
    explain_rows.push_back(section_line("End Explainer", width));

    return explain_rows;
}

void RegisterPane::scroll(int lines) {
    int w = last_width_ > 0 ? last_width_ : 60;
    int total_logical_rows = get_total_rows(w);
    int max_scroll = std::max(0, total_logical_rows - visible_rows_);
    scroll_offset_ += lines;
    if (scroll_offset_ > max_scroll) {
        scroll_offset_ = max_scroll;
    }
    if (scroll_offset_ < 0) {
        scroll_offset_ = 0;
    }
}

}  // namespace simrv::tui
