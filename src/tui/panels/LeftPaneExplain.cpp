/**
 * @file LeftPaneExplain.cpp
 * @brief Instruction explainer pane rendering for the TUI register panel.
 */
#include "simrv/tui/panels/LeftPane.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/util/InstructionExplainer.hpp"
#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/xlen/Types.hpp"
#include <format>
#include <string>
#include <vector>
#include <array>

namespace simrv::tui {

using simrv::isa::Opcode;
using simrv::isa::OperationId;
using enum simrv::isa::OperationId;
using simrv::isa::InstFormat;

namespace {

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
    switch (fmt) {
        case InstFormat::R:
            rows.push_back(format_to_width("   31     25 24   20 19   15 14 12 11    7 6       0", width));
            rows.push_back(format_to_width("  +---------+-------+-------+-----+-------+---------+", width));
            rows.push_back(format_to_width("  | funct7  |  rs2  |  rs1  |  f3 |  rd   | opcode  |", width));
            rows.push_back(format_to_width(std::format("  | {:07b} | {:05b} | {:05b} | {:03b} | {:05b} | {:07b} |",
                (ir_org >> 25) & 0x7F, (ir_org >> 20) & 0x1F, (ir_org >> 15) & 0x1F,
                (ir_org >> 12) & 0x07, (ir_org >> 7) & 0x1F, ir_org & 0x7F), width));
            rows.push_back(format_to_width("  +---------+-------+-------+-----+-------+---------+", width));
            break;
        case InstFormat::I:
            rows.push_back(format_to_width("   31          20 19   15 14 12 11    7 6       0", width));
            rows.push_back(format_to_width("  +--------------+-------+-----+-------+---------+", width));
            rows.push_back(format_to_width("  |  immediate   |  rs1  |  f3 |  rd   | opcode  |", width));
            rows.push_back(format_to_width(std::format("  | {:012b} | {:05b} | {:03b} | {:05b} | {:07b} |",
                (ir_org >> 20) & 0xFFF, (ir_org >> 15) & 0x1F, (ir_org >> 12) & 0x07,
                (ir_org >> 7) & 0x1F, ir_org & 0x7F), width));
            rows.push_back(format_to_width("  +--------------+-------+-----+-------+---------+", width));
            break;
        case InstFormat::S:
            rows.push_back(format_to_width("   31     25 24   20 19   15 14 12 11    7 6       0", width));
            rows.push_back(format_to_width("  +---------+-------+-------+-----+-------+---------+", width));
            rows.push_back(format_to_width("  | imm11:5 |  rs2  |  rs1  |  f3 | imm4:0| opcode  |", width));
            rows.push_back(format_to_width(std::format("  | {:07b} | {:05b} | {:05b} | {:03b} | {:05b} | {:07b} |",
                (ir_org >> 25) & 0x7F, (ir_org >> 20) & 0x1F, (ir_org >> 15) & 0x1F,
                (ir_org >> 12) & 0x07, (ir_org >> 7) & 0x1F, ir_org & 0x7F), width));
            rows.push_back(format_to_width("  +---------+-------+-------+-----+-------+---------+", width));
            break;
        case InstFormat::B:
            rows.push_back(format_to_width("   31     25 24   20 19   15 14 12 11    7 6       0", width));
            rows.push_back(format_to_width("  +---------+-------+-------+-----+-------+---------+", width));
            rows.push_back(format_to_width("  | imm12:5 |  rs2  |  rs1  |  f3 |imm11:1| opcode  |", width));
            rows.push_back(format_to_width(std::format("  | {:07b} | {:05b} | {:05b} | {:03b} | {:05b} | {:07b} |",
                (ir_org >> 25) & 0x7F, (ir_org >> 20) & 0x1F, (ir_org >> 15) & 0x1F,
                (ir_org >> 12) & 0x07, (ir_org >> 7) & 0x1F, ir_org & 0x7F), width));
            rows.push_back(format_to_width("  +---------+-------+-------+-----+-------+---------+", width));
            break;
        case InstFormat::U:
        case InstFormat::J:
            rows.push_back(format_to_width("   31                      12 11    7 6       0", width));
            rows.push_back(format_to_width("  +--------------------------+-------+---------+", width));
            rows.push_back(format_to_width("  |        immediate         |  rd   | opcode  |", width));
            rows.push_back(format_to_width(std::format("  |   {:020b}   | {:05b} | {:07b} |",
                (ir_org >> 12) & 0xFFFFF, (ir_org >> 7) & 0x1F, ir_org & 0x7F), width));
            rows.push_back(format_to_width("  +--------------------------+-------+---------+", width));
            break;
        case InstFormat::R4:
            rows.push_back(format_to_width("   31   27 2625      24   20 19   15 14 12 11    7 6       0", width));
            rows.push_back(format_to_width("  +-------+----+----+-------+-------+-----+-------+---------+", width));
            rows.push_back(format_to_width("  |  rs3  |fmt | .. |  rs2  |  rs1  |  f3 |  rd   | opcode  |", width));
            rows.push_back(format_to_width(std::format("  | {:05b} | {:02b} | 00 | {:05b} | {:05b} | {:03b} | {:05b} | {:07b} |",
                (ir_org >> 27) & 0x1F, (ir_org >> 25) & 0x03, (ir_org >> 20) & 0x1F, (ir_org >> 15) & 0x1F,
                (ir_org >> 12) & 0x07, (ir_org >> 7) & 0x1F, ir_org & 0x7F), width));
            rows.push_back(format_to_width("  +-------+----+----+-------+-------+-----+-------+---------+", width));
            break;
        default:
            rows.push_back(format_to_width("  (Unknown instruction format layout)", width));
            break;
    }
    return rows;
}

auto read_reg_str(const simrv::core::ArchState& st, RegId reg, bool is_fp) -> std::string {
    uint32_t r = std::to_underlying(reg);
    if (r == 0 && !is_fp) return std::format("0x{:0{}x}", 0, simrv::xlen::kXLenHexDigits);
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
    switch (fmt) {
        case InstFormat::S: {
            bool const is_store_fp = (ctx.opcode == Opcode::StoreFp);
            return std::format("{} {}, {}({})", mnemonic, get_reg_name(ctx.rs2, is_store_fp), ctx.imm, rs1_name);
        }
        case InstFormat::B:
            return std::format("{} {}, {}, {}", mnemonic, rs1_name, rs2_name, ctx.imm);
        case InstFormat::U:
            return std::format("{} {}, 0x{:X}", mnemonic, rd_name, static_cast<uint32_t>(ctx.imm));
        case InstFormat::J:
            return std::format("jal {}, {}", rd_name, ctx.imm);
        case InstFormat::R4: {
            uint32_t rs3_val = (ctx.ir_org >> 27) & 0x1F;
            std::string rs3_name = kFpRegNames.at(rs3_val);
            return std::format("{} {}, {}, {}, {}", mnemonic, rd_name, rs1_name, rs2_name, rs3_name);
        }
        default:
            return "unknown / illegal instruction";
    }
}

auto format_assembly_vector(
    const simrv::pipeline::PipelineContext& ctx,
    std::string_view mnemonic
) -> std::string {
    auto const op_id = ctx.op_id;
    
    auto vr_name = [](RegId r) -> std::string { return std::format("v{}", std::to_underlying(r)); };
    auto gpr_name = [](RegId r) -> std::string { return {kRegNames.at(std::to_underlying(r))}; };
    auto fpr_name = [](RegId r) -> std::string { return {kFpRegNames.at(std::to_underlying(r))}; };
    
    std::string rd = vr_name(ctx.rd);
    std::string rs1 = gpr_name(ctx.rs1);
    std::string rs2 = vr_name(ctx.rs2);
    
    if (op_id == VSETVLI) {
        return std::format("vsetvli {}, {}, {}", gpr_name(ctx.rd), gpr_name(ctx.rs1), ctx.imm);
    }
    if (op_id == VSETIVLI) {
        uint32_t uimm = std::to_underlying(ctx.rs1);
        return std::format("vsetivli {}, {}, {}", gpr_name(ctx.rd), uimm, ctx.imm);
    }
    if (op_id == VSETVL) {
        return std::format("vsetvl {}, {}, {}", gpr_name(ctx.rd), gpr_name(ctx.rs1), gpr_name(ctx.rs2));
    }
    if (op_id >= VLE8_V && op_id <= VLE64_V) {
        return std::format("{} {}, ({})", mnemonic, rd, rs1);
    }
    if (op_id >= VSE8_V && op_id <= VSE64_V) {
        return std::format("{} {}, ({})", mnemonic, rd, rs1);
    }
    if (op_id >= VLSE8_V && op_id <= VLSE64_V) {
        return std::format("{} {}, ({}), {}", mnemonic, rd, rs1, gpr_name(ctx.rs2));
    }
    if (op_id >= VSSE8_V && op_id <= VSSE64_V) {
        return std::format("{} {}, ({}), {}", mnemonic, rd, rs1, gpr_name(ctx.rs2));
    }
    if ((op_id >= VLUXEI8_V && op_id <= VLUXEI64_V) || (op_id >= VLOXEI8_V && op_id <= VLOXEI64_V)) {
        return std::format("{} {}, ({}), {}", mnemonic, rd, rs1, rs2);
    }
    if ((op_id >= VSUXEI8_V && op_id <= VSUXEI64_V) || (op_id >= VSOXEI8_V && op_id <= VSOXEI64_V)) {
        return std::format("{} {}, ({}), {}", mnemonic, rd, rs1, rs2);
    }
    if (op_id == VID_V) {
        return std::format("vid.v {}", rd);
    }
    if (mnemonic.ends_with(".vv") || mnemonic.ends_with(".vvm")) {
        return std::format("{} {}, {}, {}", mnemonic, rd, rs2, vr_name(ctx.rs1));
    }
    if (mnemonic.ends_with(".vx") || mnemonic.ends_with(".vxm")) {
        return std::format("{} {}, {}, {}", mnemonic, rd, rs2, rs1);
    }
    if (mnemonic.ends_with(".vi") || mnemonic.ends_with(".vim")) {
        auto imm = static_cast<int32_t>(ctx.imm);
        return std::format("{} {}, {}, {}", mnemonic, rd, rs2, imm);
    }
    if (mnemonic.ends_with(".vf")) {
        return std::format("{} {}, {}, {}", mnemonic, rd, rs2, fpr_name(ctx.rs1));
    }
    if (op_id == VMV_X_S) {
        return std::format("vmv.x.s {}, {}", gpr_name(ctx.rd), rs2);
    }
    if (op_id == VMV_S_X) {
        return std::format("vmv.s.x {}, {}", rd, rs1);
    }
    return std::format("{} {}, {}, {}", mnemonic, rd, vr_name(ctx.rs1), rs2);
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
    if (ctx.opcode == Opcode::OpV) {
        return format_assembly_vector(ctx, mnemonic);
    }
    if (fmt == InstFormat::R) {
        return format_assembly_r(ctx, mnemonic, rd_name, rs1_name, rs2_name);
    }
    if (fmt == InstFormat::I) {
        return format_assembly_i(ctx, mnemonic, rd_name, rs1_name);
    }
    return format_assembly_s_b_u_j_r4(ctx, fmt, mnemonic, rd_name, rs1_name, rs2_name);
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
    
    if (ctx.opcode == Opcode::OpV) {
        auto const op_id = ctx.op_id;
        auto const [mnemonic, desc] = simrv::util::get_operation_details(op_id);
        
        bool rd_is_v = !(op_id == VSETVLI || op_id == VSETIVLI || op_id == VSETVL || op_id == VMV_X_S);
        bool rs1_is_v = (mnemonic.ends_with(".vv") || mnemonic.ends_with(".vvm"));
        bool rs1_is_fp = mnemonic.ends_with(".vf");
        bool rs1_is_reg = !(mnemonic.ends_with(".vi") || mnemonic.ends_with(".vim") || op_id == VSETIVLI);
        bool rs2_is_v = !(op_id == VSETVL || op_id == VSETVLI || op_id == VSETIVLI || (op_id >= VLE8_V && op_id <= VSE64_V) || op_id == VID_V);
        bool rs2_is_gpr = (op_id == VSETVL);
        
        if (has_rd(fmt)) {
            if (rd_is_v) {
                rows.push_back(format_to_width(std::format("  rd     : v{}", std::to_underlying(ctx.rd)), width));
            } else {
                rows.push_back(format_to_width(std::format("  rd     : {} = {}", rd_name, read_reg_str(st, ctx.rd, false)), width));
            }
        }
        if (has_rs1(fmt) && rs1_is_reg) {
            if (rs1_is_v) {
                rows.push_back(format_to_width(std::format("  rs1    : v{}", std::to_underlying(ctx.rs1)), width));
            } else {
                rows.push_back(format_to_width(std::format("  rs1    : {} = {}", rs1_name, read_reg_str(st, ctx.rs1, rs1_is_fp)), width));
            }
        }
        if (has_rs2(fmt) && (rs2_is_v || rs2_is_gpr)) {
            if (rs2_is_v) {
                rows.push_back(format_to_width(std::format("  rs2    : v{}", std::to_underlying(ctx.rs2)), width));
            } else {
                rows.push_back(format_to_width(std::format("  rs2    : {} = {}", rs2_name, read_reg_str(st, ctx.rs2, false)), width));
            }
        }
    } else {
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

auto render_dataflow_breakdown(const simrv::core::ArchState& st, const simrv::pipeline::PipelineContext& ctx, InstFormat fmt, const std::string& rd_name, const std::string& rs1_name, const std::string& rs2_name, int width) -> std::vector<std::string> {
    std::vector<std::string> rows;
    bool is_rs1_fpr = simrv::isa::is_rs1_fp(ctx.opcode, ctx.op_id);
    bool is_rs2_fpr = simrv::isa::is_rs2_fp(ctx.opcode, ctx.op_id);
    bool is_dst_fp = simrv::isa::is_destination_fp(ctx.opcode, ctx.op_id);

    std::string op1_str = has_rs1(fmt) ? std::format("{} ({}) = {}", rs1_name, is_rs1_fpr ? "FPR" : "GPR", read_reg_str(st, ctx.rs1, is_rs1_fpr)) : "N/A (No rs1)";
    std::string op2_str;
    if (has_imm(fmt)) {
        op2_str = std::format("imm = {} (0x{:X})", ctx.imm, static_cast<uint64_t>(ctx.imm));
    } else if (has_rs2(fmt)) {
        op2_str = std::format("{} ({}) = {}", rs2_name, is_rs2_fpr ? "FPR" : "GPR", read_reg_str(st, ctx.rs2, is_rs2_fpr));
    } else {
        op2_str = "N/A";
    }

    std::string dst_str = has_rd(fmt) ? std::format("{} ({}) → {}", rd_name, is_dst_fp ? "FPR" : "GPR", read_reg_str(st, ctx.rd, is_dst_fp)) : "None (No rd)";

    rows.push_back(format_to_width(std::format("  {}Operand 1   : {}{}\033[0m", kThemeText, kThemeSky, op1_str), width));
    rows.push_back(format_to_width(std::format("  {}Operand 2   : {}{}\033[0m", kThemeText, kThemeSky, op2_str), width));
    rows.push_back(format_to_width(std::format("  {}Destination : {}{}\033[0m", kThemeText, kThemeMint, dst_str), width));

    if (has_imm(fmt) && fmt != InstFormat::U) {
        bool sign_bit = (ctx.imm < 0);
        rows.push_back(format_to_width(std::format("  {}Sign Ext    : {}\033[0m", kThemeText, sign_bit ? "\033[38;5;203m1 (sign-extended negative)\033[0m" : "\033[38;5;120m0 (positive)\033[0m"), width));
    }

    return rows;
}

} // namespace

auto LeftPane::get_explain_rows(int width) -> std::vector<std::string> {
    bool show_disabled = !paused_;
    if (show_disabled && machine_.tui) {
        uint64_t delay = machine_.tui->step_delay_us_.load(std::memory_order_relaxed);
        if (delay >= 10000) {
            show_disabled = false;
        }
    }
    if (show_disabled) {
        std::vector<std::string> explain_rows;
        explain_rows.push_back(section_line("Instruction Explainer", width));
        explain_rows.push_back(format_to_width("  [Explainer disabled while simulator is running]", width));
        explain_rows.push_back(section_line("End Explainer", width));
        return explain_rows;
    }

    auto& cpu = machine_.cpu;
    auto& st = cpu.state();
    auto& ctx = cpu.pipeline_context;

    Register const target_pc = (explain_pc_ != 0) ? explain_pc_ : st.pc;

    auto hex_addr = [](Register v) -> std::string {
        if constexpr (sizeof(Register) <= 4) {
            return std::format("0x{:08x}", v);
        } else {
            auto val = static_cast<uint64_t>(v);
            if ((val >> 32) == 0) {
                return std::format("0x{:08x}", static_cast<uint32_t>(val));
            }
            return std::format("0x{:016x}", val);
        }
    };

    if (target_pc == 0) {
        std::vector<std::string> explain_rows;
        explain_rows.push_back(section_line("Instruction Explainer", width));
        explain_rows.push_back(format_to_width(std::format("  {}PC     : {}0x00000000\033[0m", kThemeText, kThemeCoral), width));
        explain_rows.push_back(format_to_width("", width));
        explain_rows.push_back(format_to_width(std::format("  {}Status : {}No program image loaded (PC is 0x0)\033[0m", kThemeText, kThemePeach), width));
        explain_rows.push_back(format_to_width(std::format("  {}Action : {}Press [o] to load a RISC-V ELF binary image.\033[0m", kThemeText, kThemeSky), width));
        explain_rows.push_back(format_to_width("", width));
        explain_rows.push_back(section_line("End Explainer", width));
        return explain_rows;
    }

    auto& mutable_cpu = const_cast<simrv::core::CPU&>(cpu);
    auto saved_pc = st.pc;
    st.pc = target_pc;
    bool fetch_success = mutable_cpu.fetch_stage(machine_, target_pc);
    if (fetch_success) {
        (void)mutable_cpu.decode_stage(machine_);
    }
    st.pc = saved_pc;

    if (!fetch_success) {
        std::vector<std::string> explain_rows;
        explain_rows.push_back(section_line("Instruction Explainer", width));
        explain_rows.push_back(format_to_width(std::format("  {}PC     : {}{}\033[0m", kThemeText, kThemeCoral, hex_addr(target_pc)), width));
        explain_rows.push_back(format_to_width("", width));
        explain_rows.push_back(format_to_width(std::format("  {}Status : {}Instruction fetch failed / Unmapped memory address\033[0m", kThemeText, kThemePeach), width));
        explain_rows.push_back(format_to_width(std::format("  {}Action : {}Inspect another address [m] or load a valid binary [o].\033[0m", kThemeText, kThemeSky), width));
        explain_rows.push_back(format_to_width("", width));
        explain_rows.push_back(section_line("End Explainer", width));
        return explain_rows;
    }

    bool const is_compressed = (ctx.ir_org & 0x3) != 0x3;
    uint32_t decompressed_inst = ctx.ir;

    InstFormat const fmt = simrv::isa::get_instruction_format(ctx.opcode);
    auto const [mnemonic, behavior_desc] = simrv::util::get_operation_details(ctx.op_id);

    bool const is_dst_fp = simrv::isa::is_destination_fp(ctx.opcode, ctx.op_id);
    bool const is_rs1_fpr = simrv::isa::is_rs1_fp(ctx.opcode, ctx.op_id);
    bool const is_rs2_fpr = simrv::isa::is_rs2_fp(ctx.opcode, ctx.op_id);

    std::string rd_name = get_reg_name(ctx.rd, is_dst_fp);
    std::string rs1_name = get_reg_name(ctx.rs1, is_rs1_fpr);
    std::string rs2_name = get_reg_name(ctx.rs2, is_rs2_fpr);

    std::string assembly = format_instruction_assembly(ctx, fmt, mnemonic, rd_name, rs1_name, rs2_name);

    std::vector<std::string> explain_rows;
    if (previous_page_.has_value()) {
        explain_rows.push_back(format_to_width(
            std::format("  \033[1;36m← Back [{}]\033[0m  (Inspecting PC: {})", "ESC", hex_addr(target_pc)),
            width));
    }
    explain_rows.push_back(section_line("Instruction Explainer", width));

    std::string sym = machine_.symbols.lookup(target_pc);
    std::string pc_label = sym.empty() ? hex_addr(target_pc)
                                       : std::format("{} <{}>", hex_addr(target_pc), sym);
    explain_rows.push_back(format_to_width(std::format("  {}PC     : {}{}\033[0m", kThemeText, kThemeMint, pc_label), width));

    std::string hex_str;
    if (is_compressed) {
        uint32_t raw_16 = ctx.ir_org & 0xFFFF;
        hex_str = std::format("0x{:04X} (compressed) -> 0x{:08X}", raw_16, decompressed_inst);
    } else {
        hex_str = std::format("0x{:08X}", ctx.ir_org);
    }
    explain_rows.push_back(format_to_width(std::format("  {}Hex    : {}{}\033[0m", kThemeText, kThemeMint, hex_str), width));

    std::string bin_str;
    if (is_compressed) {
        bin_str = std::format("{:016b}", ctx.ir_org & 0xFFFF);
    } else {
        bin_str = std::format("{:032b}", ctx.ir_org);
    }
    explain_rows.push_back(format_to_width(std::format("  {}Bin    : {}{}\033[0m", kThemeText, kThemeVal, bin_str), width));

    explain_rows.push_back(format_to_width(std::format("  {}Asm    : {}{}\033[0m", kThemeText, kThemePeach, assembly), width));
    explain_rows.push_back(format_to_width(std::format("  {}Format : {}{}\033[0m", kThemeText, kThemeVal, simrv::isa::get_instruction_format_name(fmt)), width));
    explain_rows.push_back(format_to_width(std::format("  {}ISA Ext: {}{}\033[0m", kThemeText, kThemeMint, simrv::util::get_isa_extension_name(ctx.op_id)), width));
    explain_rows.push_back(format_to_width("", width));

    explain_rows.push_back(section_line("Visual Bit Fields", width));
    auto visual_fields = render_visual_bitfields(fmt, ctx.ir_org, width);
    explain_rows.insert(explain_rows.end(), visual_fields.begin(), visual_fields.end());
    explain_rows.push_back(format_to_width("", width));

    explain_rows.push_back(section_line("Field Decoded Values", width));
    auto decoded_fields = render_field_decoded_values(st, ctx, fmt, rd_name, rs1_name, rs2_name, width);
    explain_rows.insert(explain_rows.end(), decoded_fields.begin(), decoded_fields.end());
    explain_rows.push_back(format_to_width("", width));

    explain_rows.push_back(section_line("Dataflow & Execution Breakdown", width));
    auto dataflow_fields = render_dataflow_breakdown(st, ctx, fmt, rd_name, rs1_name, rs2_name, width);
    explain_rows.insert(explain_rows.end(), dataflow_fields.begin(), dataflow_fields.end());
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

    if (ctx.pending_exception.has_value() || st.mcause != 0 || st.scause != 0) {
        explain_rows.push_back(format_to_width("", width));
        explain_rows.push_back(section_line("Trap & Exception Diagnostic Analysis", width));
        uint64_t cause = ctx.pending_exception.has_value() ? static_cast<uint64_t>(*ctx.pending_exception) : (st.mcause != 0 ? st.mcause : st.scause);
        uint64_t tval = ctx.pending_exception.has_value() ? ctx.pending_tval : (st.mcause != 0 ? st.mtval : st.stval);
        
        static constexpr std::array<const char*, 16> kCauseNames = {
            "Instruction Address Misaligned", "Instruction Access Fault", "Illegal Instruction", "Breakpoint",
            "Load Address Misaligned", "Load Access Fault", "Store Address Misaligned", "Store Access Fault",
            "Environment Call (U-Mode)", "Environment Call (S-Mode)", "Reserved (10)", "Environment Call (M-Mode)",
            "Instruction Page Fault", "Load Page Fault", "Reserved (14)", "Store Page Fault"
        };
        std::string cause_name = (cause < kCauseNames.size()) ? kCauseNames.at(cause) : std::format("Exception ({})", cause);
        explain_rows.push_back(format_to_width(std::format("  {}Cause      : {}{} (0x{:X})\033[0m", kThemeText, kThemePeach, cause_name, cause), width));
        explain_rows.push_back(format_to_width(std::format("  {}Target VAddr: {}0x{:0{}x}\033[0m", kThemeText, kThemeVal, tval, simrv::xlen::kXLenHexDigits), width));
        std::string priv_str = (st.priv == PrivilegeLevel::Machine) ? "Machine (M)" : ((st.priv == PrivilegeLevel::Supervisor) ? "Supervisor (S)" : "User (U)");
        explain_rows.push_back(format_to_width(std::format("  {}Privilege  : {}{}\033[0m", kThemeText, kThemeMint, priv_str), width));
    }

    explain_rows.push_back(section_line("End Explainer", width));

    return explain_rows;
}

}  // namespace simrv::tui
