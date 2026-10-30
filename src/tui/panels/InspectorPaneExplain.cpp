/**
 * @file InspectorPaneExplain.cpp
 * @brief Instruction explainer pane rendering for the TUI register panel.
 */
#include <atomic>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/TrapController.hpp"
#include "simrv/explain/InstructionExplainer.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/pipeline/OperationInfo.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

namespace simrv::tui {

using simrv::explain::InstructionExplanation;
using simrv::isa::InstFormat;
using simrv::isa::Opcode;
using simrv::isa::OperationId;
using enum simrv::isa::OperationId;

namespace {

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

auto render_visual_bitfields(const InstructionExplanation& expl, int width)
    -> std::vector<std::string> {
    std::vector<std::string> rows;
    if (expl.bitfields.empty()) {
        rows.push_back(format_to_width(" (Unknown instruction format layout)", width));
        return rows;
    }
    for (const auto& bf : expl.bitfields) {
        rows.push_back(
            format_to_width(std::format(" {}{:<16} [{:02}:{:02}]  {}{:0{}b}\033[0m", kThemeText,
                                        bf.label, bf.high_bit, bf.low_bit, kThemeVal, bf.value,
                                        bf.bit_count),
                            width));
    }
    return rows;
}

auto has_funct3(InstFormat fmt) -> bool {
    return fmt == InstFormat::R || fmt == InstFormat::I || fmt == InstFormat::S ||
           fmt == InstFormat::B || fmt == InstFormat::R4;
}

auto has_funct7(InstFormat fmt) -> bool { return fmt == InstFormat::R || fmt == InstFormat::R4; }

auto render_field_decoded_values(const InstructionExplanation& expl, int width)
    -> std::vector<std::string> {
    using simrv::pipeline::operation::RegBank;
    std::vector<std::string> rows;
    rows.push_back(format_to_width(std::format("  opcode : 0x{:02X} ({:07b})",
                                               std::to_underlying(expl.opcode),
                                               std::to_underlying(expl.opcode)),
                                   width));

    auto format_operand_field = [&](std::string_view label, const simrv::explain::OperandState& op) {
        if (!op.present) return;
        if (op.bank == RegBank::Vector) {
            rows.push_back(format_to_width(std::format("  {:<6} : {}", label, op.name), width));
        } else if (!op.formatted_value.empty()) {
            rows.push_back(format_to_width(
                std::format("  {:<6} : {} = {}", label, op.name, op.formatted_value), width));
        }
    };

    format_operand_field("rd", expl.rd);
    format_operand_field("rs1", expl.rs1);
    format_operand_field("rs2", expl.rs2);
    if (expl.format == InstFormat::R4) {
        format_operand_field("rs3", expl.rs3);
    }

    if (has_funct3(expl.format)) {
        rows.push_back(format_to_width(std::format("  funct3 : 0x{:X} ({:03b})",
                                                   std::to_underlying(expl.funct3),
                                                   std::to_underlying(expl.funct3)),
                                       width));
    }
    if (has_funct7(expl.format)) {
        uint32_t const f7 = static_cast<uint32_t>(expl.funct7) & 0x7FU;
        rows.push_back(format_to_width(std::format("  funct7 : 0x{:02X} ({:07b})", f7, f7), width));
    }
    if (expl.has_imm) {
        rows.push_back(format_to_width(
            std::format("  imm    : {} (0x{:X})", expl.imm, static_cast<uint32_t>(expl.imm)),
            width));
    }
    if (expl.control.has_value() &&
        (expl.format == InstFormat::B || expl.format == InstFormat::J)) {
        rows.push_back(format_to_width(std::format("  target : 0x{:0{}x}", expl.control->target_pc,
                                                   simrv::xlen::kXLenHexDigits),
                                       width));
    }
    return rows;
}

auto render_dataflow_breakdown(const InstructionExplanation& expl, int width)
    -> std::vector<std::string> {
    using simrv::pipeline::operation::RegBank;
    std::vector<std::string> rows;
    auto bank_tag = [](RegBank b) -> const char* {
        if (b == RegBank::Float) return "FPR";
        if (b == RegBank::Vector) return "VR";
        return "GPR";
    };

    std::string op1_str = expl.rs1.present
                              ? std::format("{} ({}) = {}", expl.rs1.name, bank_tag(expl.rs1.bank),
                                            expl.rs1.formatted_value)
                              : "N/A (No rs1)";
    std::string op2_str;
    if (expl.has_imm) {
        op2_str = std::format("imm = {} (0x{:X})", expl.imm, static_cast<uint64_t>(expl.imm));
    } else if (expl.rs2.present) {
        op2_str = std::format("{} ({}) = {}", expl.rs2.name, bank_tag(expl.rs2.bank),
                              expl.rs2.formatted_value);
    } else {
        op2_str = "N/A";
    }

    std::string dst_str = expl.rd.present
                              ? std::format("{} ({}) → {}", expl.rd.name, bank_tag(expl.rd.bank),
                                            expl.rd.formatted_value)
                              : "None (No rd)";

    rows.push_back(
        format_to_width(std::format("  {}Operand 1   : {}{}\033[0m", kThemeText, kThemeSky, op1_str),
                        width));
    rows.push_back(
        format_to_width(std::format("  {}Operand 2   : {}{}\033[0m", kThemeText, kThemeSky, op2_str),
                        width));
    rows.push_back(format_to_width(
        std::format("  {}Destination : {}{}\033[0m", kThemeText, kThemeMint, dst_str), width));

    if (!expl.predicted_effect.effect_equation.empty()) {
        rows.push_back(format_to_width(
            std::format("  {}Predicted WB: {}{}\033[0m", kThemeText, kThemeMint,
                        expl.predicted_effect.effect_equation),
            width));
    }

    if (expl.memory.has_value()) {
        const auto& mem = *expl.memory;
        rows.push_back(format_to_width(
            std::format("  {}Effective Addr: {}0x{:0{}x}\033[0m = {} + {}", kThemeText, kThemeSky,
                        mem.effective_vaddr, simrv::xlen::kXLenHexDigits, expl.rs1.name,
                        mem.offset),
            width));
        if (mem.effective_paddr.has_value()) {
            rows.push_back(format_to_width(
                std::format(
                    "  {}Phys Address  : {}0x{:0{}x}\033[0m ({}-byte, {}, PMP: {})", kThemeText,
                    kThemeVal, *mem.effective_paddr, simrv::xlen::kXLenHexDigits, mem.access_bytes,
                    mem.is_aligned ? "aligned" : "misaligned", mem.pmp_permitted ? "OK" : "FAULT"),
                width));
        }
    }
    if (expl.control.has_value()) {
        const auto& ctrl = *expl.control;
        rows.push_back(format_to_width(
            std::format("  {}Control Target: {}0x{:0{}x}\033[0m", kThemeText, kThemeSky,
                        ctrl.target_pc, simrv::xlen::kXLenHexDigits),
            width));
        if (ctrl.is_conditional && !ctrl.condition_relation.empty()) {
            rows.push_back(format_to_width(
                std::format("  {}Branch Eval : {}{}\033[0m", kThemeText, kThemeMint,
                            ctrl.condition_relation),
                width));
        }
    }

    if (expl.has_imm && expl.format != InstFormat::U) {
        bool sign_bit = (expl.imm < 0);
        rows.push_back(
            format_to_width(std::format("  {}Sign Ext    : {}\033[0m", kThemeText,
                                        sign_bit ? "\033[38;5;203m1 (sign-extended negative)\033[0m"
                                                 : "\033[38;5;120m0 (positive)\033[0m"),
                            width));
    }

    return rows;
}

auto render_microarchitectural_profile(const InstructionExplanation& expl, int w)
    -> std::vector<std::string> {
    using simrv::pipeline::operation::RegBank;
    std::vector<std::string> rows;

    const auto bank_str = [](RegBank b) -> const char* {
        switch (b) {
            case RegBank::Integer:
                return "Int RegFile (X)";
            case RegBank::Float:
                return "FP RegFile (F)";
            case RegBank::Vector:
                return "Vector RegFile (V)";
            default:
                return "None";
        }
    };

    if (expl.rd.bank != RegBank::None) {
        rows.push_back(format_to_width(
            std::format("  {}rd (dest) : {}{:<5}\033[0m  [{}{}\033[0m]", kThemeText, kThemeMint,
                        expl.rd.name, kThemeVal, bank_str(expl.rd.bank)),
            w));
    }
    if (expl.rs1.bank != RegBank::None) {
        rows.push_back(format_to_width(
            std::format("  {}rs1 (src1): {}{:<5}\033[0m  [{}{}\033[0m]", kThemeText, kThemePeach,
                        expl.rs1.name, kThemeVal, bank_str(expl.rs1.bank)),
            w));
    }
    if (expl.rs2.bank != RegBank::None) {
        rows.push_back(format_to_width(
            std::format("  {}rs2 (src2): {}{:<5}\033[0m  [{}{}\033[0m]", kThemeText, kThemePeach,
                        expl.rs2.name, kThemeVal, bank_str(expl.rs2.bank)),
            w));
    }
    if (expl.rs3.bank != RegBank::None) {
        rows.push_back(format_to_width(
            std::format("  {}rs3 (src3): {}{:<5}\033[0m  [{}{}\033[0m] (Addend)", kThemeText,
                        kThemeSky, expl.rs3.name, kThemeVal, bank_str(expl.rs3.bank)),
            w));
    }

    rows.push_back(format_to_width(
        std::format("  {}Unit Class : {}{}\033[0m ({})", kThemeText, kThemeSky,
                    expl.microarch.unit_class, expl.microarch.latency_note),
        w));

    if (!expl.microarch.hazard_tags.empty()) {
        std::string tags;
        for (size_t i = 0; i < expl.microarch.hazard_tags.size(); ++i) {
            if (i > 0) tags += " | ";
            tags += expl.microarch.hazard_tags[i];
        }
        rows.push_back(format_to_width(
            std::format("  {}Hazard Tag : {}{}\033[0m", kThemeText, kThemeCoral, tags), w));
    }

    return rows;
}

}  // namespace

auto InspectorPane::get_explain_rows(int width) -> std::vector<std::string> {
    bool show_disabled = !paused_;
    if (show_disabled) {
        uint64_t delay =
            tui_ ? tui_->step_delay_us_.load(std::memory_order_relaxed)
                 : (machine_.telemetry_sink() ? machine_.telemetry_sink()->step_delay_us() : 0);
        if (delay >=
            ((1'000'000U + Tui::kDetailedExecutionMaxHz - 1) / Tui::kDetailedExecutionMaxHz)) {
            show_disabled = false;
        }
    }
    if (show_disabled) {
        std::vector<std::string> explain_rows;
        explain_rows.push_back(section_line("Instruction Explainer", width));
        explain_rows.push_back(
            format_to_width("  [Explainer disabled while simulator is running]", width));
        explain_rows.push_back(section_line("End Explainer", width));
        return explain_rows;
    }

    auto& cpu = current_cpu();
    auto& st = cpu.state();

    simrv::xlen::Register const target_pc = (explain_pc_ != 0) ? explain_pc_ : st.pc;

    auto hex_addr = [](simrv::xlen::Register v) -> std::string {
        if constexpr (sizeof(simrv::xlen::Register) <= 4) {
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
        explain_rows.push_back(format_to_width(
            std::format("  {}PC     : {}0x00000000\033[0m", kThemeText, kThemeCoral), width));
        explain_rows.push_back(format_to_width("", width));
        explain_rows.push_back(
            format_to_width(std::format("  {}Status : {}No program image loaded (PC is 0x0)\033[0m",
                                        kThemeText, kThemePeach),
                            width));
        explain_rows.push_back(format_to_width(
            std::format(
                "  {}Action : {}Press \033[1m[o]\033[22m to load a RISC-V ELF binary image.\033[0m",
                kThemeText, kThemeSky),
            width));
        explain_rows.push_back(format_to_width("", width));
        explain_rows.push_back(section_line("End Explainer", width));
        return explain_rows;
    }

    const auto expl_opt = simrv::explain::explain_at_pc(machine_, selected_hart_, target_pc);
    if (!expl_opt.has_value()) {
        std::vector<std::string> explain_rows;
        explain_rows.push_back(section_line("Instruction Explainer", width));
        explain_rows.push_back(format_to_width(
            std::format("  {}PC     : {}{}\033[0m", kThemeText, kThemeCoral, hex_addr(target_pc)),
            width));
        explain_rows.push_back(format_to_width("", width));
        explain_rows.push_back(format_to_width(
            std::format("  {}Status : {}Instruction fetch failed / Unmapped memory address\033[0m",
                        kThemeText, kThemePeach),
            width));
        explain_rows.push_back(
            format_to_width(std::format("  {}Action : {}Inspect another address \033[1m[i]\033[22m "
                                        "or load a valid binary \033[1m[o]\033[22m.\033[0m",
                                        kThemeText, kThemeSky),
                            width));
        explain_rows.push_back(format_to_width("", width));
        explain_rows.push_back(section_line("End Explainer", width));
        return explain_rows;
    }

    const InstructionExplanation& expl = *expl_opt;

    std::vector<std::string> explain_rows;
    if (previous_page_.has_value()) {
        explain_rows.push_back(
            format_to_width(std::format("  \033[1;36m← Back [{}]\033[0m  (Inspecting PC: {})",
                                        "ESC", hex_addr(target_pc)),
                            width));
    }
    explain_rows.push_back(section_line("Instruction Explainer", width));

    std::string pc_label = expl.symbol.empty()
                               ? hex_addr(target_pc)
                               : std::format("{} <{}>", hex_addr(target_pc), expl.symbol);
    explain_rows.push_back(format_to_width(
        std::format("  {}PC     : {}{}\033[0m", kThemeText, kThemeMint, pc_label), width));

    std::string hex_str;
    if (expl.is_compressed) {
        uint32_t raw_16 = expl.raw_bits & 0xFFFFU;
        hex_str = std::format("0x{:04X} (compressed) -> 0x{:08X}", raw_16, expl.canonical_bits);
    } else {
        hex_str = std::format("0x{:08X}", expl.raw_bits);
    }
    explain_rows.push_back(format_to_width(
        std::format("  {}Hex    : {}{}\033[0m", kThemeText, kThemeMint, hex_str), width));

    std::string bin_str;
    if (expl.is_compressed) {
        bin_str = std::format("{:016b}", expl.raw_bits & 0xFFFFU);
    } else {
        bin_str = std::format("{:032b}", expl.raw_bits);
    }
    explain_rows.push_back(format_to_width(
        std::format("  {}Bin    : {}{}\033[0m", kThemeText, kThemeVal, bin_str), width));

    explain_rows.push_back(format_to_width(
        std::format("  {}Asm    : {}{}\033[0m", kThemeText, kThemePeach, expl.assembly), width));
    explain_rows.push_back(
        format_to_width(std::format("  {}Format : {}{}\033[0m", kThemeText, kThemeVal,
                                    simrv::isa::get_instruction_format_name(expl.format)),
                        width));
    explain_rows.push_back(
        format_to_width(std::format("  {}ISA Ext: {}{}\033[0m", kThemeText, kThemeMint,
                                    expl.spec.reference.extension),
                        width));
    if (!expl.spec.reference.formula.empty()) {
        explain_rows.push_back(
            format_to_width(std::format("  {}Formula: {}{}\033[0m", kThemeText, kThemeSky,
                                        expl.spec.reference.formula),
                            width));
    }
    explain_rows.push_back(format_to_width("", width));

    explain_rows.push_back(section_line("Visual Bit Fields", width));
    auto visual_fields = render_visual_bitfields(expl, width);
    explain_rows.insert(explain_rows.end(), visual_fields.begin(), visual_fields.end());
    explain_rows.push_back(format_to_width("", width));

    explain_rows.push_back(section_line("Field Decoded Values", width));
    auto decoded_fields = render_field_decoded_values(expl, width);
    explain_rows.insert(explain_rows.end(), decoded_fields.begin(), decoded_fields.end());
    explain_rows.push_back(format_to_width("", width));

    explain_rows.push_back(section_line("Dataflow & Execution Breakdown", width));
    auto dataflow_fields = render_dataflow_breakdown(expl, width);
    explain_rows.insert(explain_rows.end(), dataflow_fields.begin(), dataflow_fields.end());
    explain_rows.push_back(format_to_width("", width));

    if (expl.vector.has_value()) {
        const auto& vec = *expl.vector;
        explain_rows.push_back(section_line("Vector Configuration (RVV 1.0)", width));
        if (vec.vill) {
            explain_rows.push_back(format_to_width(
                std::format("  {}vtype  : {}ILL (Illegal Configuration)\033[0m", kThemeText,
                            kThemePeach),
                width));
        } else {
            explain_rows.push_back(format_to_width(
                std::format("  {}SEW    : {}e{}\033[0m   {}LMUL: {}m{}\033[0m   {}VL: {}{}\033[0m",
                            kThemeText, kThemeMint, vec.vsew_bits, kThemeText, kThemeSky,
                            vec.vlmul_str, kThemeText, kThemeVal, vec.vl),
                width));
        }
        explain_rows.push_back(format_to_width(
            std::format("  {}Mask   : {}{}\033[0m", kThemeText, kThemePeach,
                        vec.unmasked ? "Unmasked (vm=1)" : "Masked by v0.t (vm=0)"),
            width));
        explain_rows.push_back(format_to_width("", width));
    }

    if (expl.csr.has_value()) {
        const auto& csr_df = *expl.csr;
        explain_rows.push_back(section_line("Target CSR Architectural Profile", width));
        explain_rows.push_back(format_to_width(
            std::format("  {}CSR Name    : {}{} (0x{:03X})\033[0m", kThemeText, kThemePeach,
                        csr_df.metadata.name, csr_df.address),
            width));
        std::string priv_req =
            (csr_df.metadata.min_privilege == simrv::xlen::PrivilegeLevel::Machine)
                ? "Machine (M)"
                : ((csr_df.metadata.min_privilege == simrv::xlen::PrivilegeLevel::Supervisor)
                       ? "Supervisor (S)"
                       : "User (U)");
        explain_rows.push_back(format_to_width(
            std::format("  {}Privilege   : {}{}\033[0m", kThemeText, kThemeMint, priv_req), width));
        explain_rows.push_back(format_to_width(
            std::format("  {}Access Type : {}{}\033[0m", kThemeText, kThemeVal,
                        csr_df.metadata.read_only ? "Read-Only (RO)" : "Read/Write (RW)"),
            width));
        if (csr_df.current_value.has_value()) {
            explain_rows.push_back(format_to_width(
                std::format("  {}Current Val : {}0x{:0{}x}\033[0m", kThemeText, kThemeVal,
                            *csr_df.current_value, simrv::xlen::kXLenHexDigits),
                width));
        }
        if (csr_df.predicted_write_value.has_value() && csr_df.attempts_write) {
            explain_rows.push_back(format_to_width(
                std::format("  {}Next CSR Val: {}0x{:0{}x}\033[0m", kThemeText, kThemeMint,
                            *csr_df.predicted_write_value, simrv::xlen::kXLenHexDigits),
                width));
        }
        explain_rows.push_back(
            format_to_width(std::format("  {}Extension   : {}{}\033[0m", kThemeText, kThemeMint,
                                        csr_df.metadata.extension),
                            width));
        if (!csr_df.metadata.description.empty()) {
            auto csr_desc_wrapped = wrap_text(std::string(csr_df.metadata.description), width - 4);
            for (const auto& line : csr_desc_wrapped) {
                explain_rows.push_back(format_to_width("  " + line, width));
            }
        }
        explain_rows.push_back(format_to_width("", width));
    }

    if (expl.cfu.has_value()) {
        const auto& cfu_details = *expl.cfu;
        uint32_t const f3 = cfu_details.funct3;
        uint32_t const f7 = cfu_details.funct7;
        auto const& cfu_unit = current_cpu().cfu_unit;
        bool const has_plugin = cfu_unit.has_plugin();
        uint32_t const latency = cfu_unit.query_latency(f7, f3);
        uint32_t const stalls = cfu_unit.query_stall_cycles(f7, f3);

        explain_rows.push_back(section_line("Custom Function Unit (CFU) Profile", width));
        explain_rows.push_back(format_to_width(
            std::format("  {}CFU Opcode  : {}custom-0 (0x0B)\033[0m", kThemeText, kThemePeach),
            width));
        explain_rows.push_back(format_to_width(
            std::format("  {}Function ID : {}func{} (funct3 = 0b{:03b})\033[0m", kThemeText,
                        kThemeMint, f3, f3),
            width));
        explain_rows.push_back(format_to_width(
            std::format("  {}Sub-Opcode  : {}0x{:02X} (funct7 = 0b{:07b})\033[0m", kThemeText,
                        kThemeMint, f7, f7),
            width));
        std::string backend_str =
            has_plugin ? std::format("Dynamic Plugin ({})", cfu_unit.plugin_path())
                       : "Built-in Model (rvproc_cfu / bitwise OR)";
        explain_rows.push_back(format_to_width(
            std::format("  {}Backend     : {}{}\033[0m", kThemeText, kThemeVal, backend_str),
            width));
        explain_rows.push_back(format_to_width(
            std::format("  {}Model Timing: {}{} cycle(s) ({} issue + {} stall)\033[0m", kThemeText,
                        kThemeVal, latency, latency > 0 ? 1 : 0, stalls),
            width));

        if (!cfu_details.description.empty()) {
            auto cfu_desc_wrapped = wrap_text(cfu_details.description, width - 4);
            for (const auto& line : cfu_desc_wrapped) {
                explain_rows.push_back(format_to_width("  " + line, width));
            }
        }
        explain_rows.push_back(format_to_width("", width));
    }

    if (machine_.runtime_profile.is_cycle_mode()) {
        explain_rows.push_back(section_line("Microarchitectural & Hazard Profile", width));
        auto microarch_fields = render_microarchitectural_profile(expl, width);
        explain_rows.insert(explain_rows.end(), microarch_fields.begin(), microarch_fields.end());
        explain_rows.push_back(format_to_width("", width));
    }

    explain_rows.push_back(section_line("Instruction Description", width));
    std::string full_desc =
        std::format("[ISA: {}] {}", expl.spec.reference.extension, expl.spec.description);
    auto wrapped = wrap_text(full_desc, width - 4);
    for (const auto& line : wrapped) {
        explain_rows.push_back(format_to_width("  " + line, width));
    }
    explain_rows.push_back(format_to_width(
        std::format("  {}Spec Source : {}{} — {}\033[0m", kThemeText, kThemeMuted,
                    expl.spec.reference.document, expl.spec.reference.section),
        width));

    const auto& ctx = cpu.pipeline_context;
    const bool has_trap = expl.predicted_effect.predicted_trap.has_value() ||
                          ctx.pending_exception.has_value() || st.mcause != 0 || st.scause != 0;
    if (has_trap) {
        explain_rows.push_back(format_to_width("", width));
        explain_rows.push_back(section_line("Trap & Exception Diagnostic Analysis", width));
        uint64_t cause = 0;
        uint64_t tval = 0;
        if (ctx.pending_exception.has_value()) {
            cause = static_cast<uint64_t>(*ctx.pending_exception);
            tval = ctx.pending_tval;
        } else if (expl.predicted_effect.predicted_trap.has_value()) {
            cause = static_cast<uint64_t>(*expl.predicted_effect.predicted_trap);
            tval = expl.predicted_effect.predicted_tval;
        } else {
            cause = (st.mcause != 0 ? st.mcause : st.scause);
            tval = (st.mcause != 0 ? st.mtval : st.stval);
        }

        std::string cause_name =
            simrv::core::trap_cause_name(static_cast<simrv::xlen::TrapCause>(cause));
        explain_rows.push_back(format_to_width(
            std::format("  {}Cause      : {}{} (0x{:X})\033[0m", kThemeText, kThemePeach,
                        cause_name, cause),
            width));
        explain_rows.push_back(format_to_width(
            std::format("  {}Target VAddr: {}0x{:0{}x}\033[0m", kThemeText, kThemeVal, tval,
                        simrv::xlen::kXLenHexDigits),
            width));
        std::string priv_str =
            (st.priv == simrv::xlen::PrivilegeLevel::Machine)
                ? "Machine (M)"
                : ((st.priv == simrv::xlen::PrivilegeLevel::Supervisor) ? "Supervisor (S)"
                                                                        : "User (U)");
        explain_rows.push_back(format_to_width(
            std::format("  {}Privilege  : {}{}\033[0m", kThemeText, kThemeMint, priv_str), width));
    }

    explain_rows.push_back(section_line("End Explainer", width));

    return explain_rows;
}

}  // namespace simrv::tui
