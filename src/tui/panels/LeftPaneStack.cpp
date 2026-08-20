/**
 * @file LeftPaneStack.cpp
 * @brief Stack memory view pane rendering for the TUI register panel.
 */
#include <format>
#include <optional>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/Mmu.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/panels/LeftPane.hpp"

namespace simrv::tui {

auto LeftPane::translate_safe(const simrv::core::CPU& cpu, Register vaddr) const
    -> std::optional<Register> {
    const auto eff_priv = cpu.effective_data_privilege();
    const bool translation_enabled =
        (eff_priv != PrivilegeLevel::Machine &&
         simrv::xlen::satp_translation_enabled(cpu.state().satp, cpu.state().regs.xlen));
    if (!translation_enabled) {
        return (cpu.state().regs.xlen == 32) ? (vaddr & 0xFFFFFFFFULL) : vaddr;
    }
    auto* mmu = const_cast<Mmu*>(machine_.memory_.mmu());
    auto res =
        mmu->translate(vaddr, PteAccess::Read, eff_priv, cpu.state().mstatus, cpu.state().satp,
                       cpu.state().regs.xlen, /*update_access_bits=*/false);
    if (res.has_value()) {
        return res.value();
    }
    return std::nullopt;
}

auto LeftPane::render_stack_frame(const simrv::core::CPU& cpu, int logical_row, int col_width,
                                  int right_width) -> std::string {
    int const width = col_width + right_width;

    Register sp = cpu.state().regs.read(RegId::Sp);
    unsigned int xlen = cpu.state().regs.xlen;
    int word_size = static_cast<int>(xlen) / 8;

    // Check if the stack pointer is valid and mapped in DRAM
    auto sp_phys = translate_safe(cpu, sp);
    bool const sp_valid = (sp != 0 && sp_phys.has_value() && simrv::memory::is_dram_addr(*sp_phys));

    // Fallback: Display warning message when sp is invalid or null (e.g. during initial boot)
    if (!sp_valid) {
        if (logical_row == 0) {
            return section_line("Live Guest Stack Watch", width);
        }
        if (logical_row == 4) {
            std::string text = "\033[1;38;5;203m⚠️  STACK POINTER INVALID / NULL\033[0m";
            int spaces = std::max(0, (width - 32) / 2);
            return format_to_width(std::string(spaces, ' ') + text, width);
        }
        if (logical_row == 6) {
            std::string val_str =
                (xlen == 64) ? std::format("0x{:016x}", sp) : std::format("0x{:08x}", sp);
            std::string text = std::format("sp register = {}{}\033[0m", kThemeVal, val_str);
            int spaces = std::max(0, (width - (14 + (xlen == 64 ? 18 : 10))) / 2);
            return format_to_width(std::string(spaces, ' ') + text, width);
        }
        if (logical_row == 8) {
            std::string text = "Stack watch requires a valid DRAM-mapped pointer.";
            int spaces = std::max(0, (width - static_cast<int>(text.length())) / 2);
            return format_to_width(std::string(spaces, ' ') + text, width);
        }
        if (logical_row == 9) {
            std::string text = "It will activate once guest code initializes sp.";
            int spaces = std::max(0, (width - static_cast<int>(text.length())) / 2);
            return format_to_width(std::string(spaces, ' ') + text, width);
        }
        if (logical_row == 14) {
            return section_line("Status: Stack Watch Inactive", width);
        }
        return format_to_width("", width);
    }

    Register aligned_sp = sp & ~(static_cast<Register>(word_size) - 1);

    bool const is_16b_aligned = (sp % 16 == 0);
    if (logical_row == 0) {
        std::string title = std::format("Live Guest Stack Watch ({})",
                                        is_16b_aligned ? "16B ABI Aligned" : "Unaligned sp!");
        return section_line(title, width);
    }
    if (logical_row == 14) {
        std::string footer_desc =
            std::format("Occupancy: sp | active frame | free/scratch  [{}]",
                        is_16b_aligned ? "\033[38;5;120m16B ABI Aligned\033[0m"
                                       : "\033[38;5;203m16B Misaligned\033[0m");
        return section_line(footer_desc, width);
    }
    if (logical_row > 14) {
        return format_to_width("", width);
    }

    // logical_row ranges from 1 to 13 (inclusive). 13 elements.
    // Center it around sp (word_offset = 0 at logical_row = 7)
    int word_offset = logical_row - 7;
    Register target_vaddr = aligned_sp + static_cast<Register>(word_offset * word_size);
    if (xlen == 32) {
        target_vaddr &= 0xFFFFFFFFULL;
    }

    std::string addr_str = std::format("0x{:08x}", target_vaddr);
    if (xlen == 64) {
        addr_str = std::format("0x{:016x}", target_vaddr);
    }

    Register fp = cpu.state().regs.read(RegId::S0);
    Register ra = cpu.state().regs.read(RegId::Ra);

    std::string offset_str = std::format("sp{:+d}", word_offset * word_size);
    if (word_offset == 0) {
        offset_str = "sp";
    } else if (target_vaddr == fp) {
        offset_str = "s0/fp";
    }

    std::string val_str = "????";
    std::string dec_str = "";

    auto paddr_opt = translate_safe(cpu, target_vaddr);
    if (paddr_opt) {
        Register paddr = *paddr_opt;
        if (simrv::memory::is_dram_addr(paddr)) {
            Register raw_val = 0;
            if (xlen == 64) {
                uint64_t data =
                    simrv::memory::ram_read_fast(paddr, static_cast<Instruction>(isa::Funct3::Sd),
                                                 machine_.memory_.mmu()->mmem());
                val_str = std::format("0x{:016x}", data);
                dec_str = std::format("{}", static_cast<int64_t>(data));
                raw_val = data;
            } else {
                uint32_t data =
                    simrv::memory::ram_read_fast(paddr, static_cast<Instruction>(isa::Funct3::Sw),
                                                 machine_.memory_.mmu()->mmem());
                val_str = std::format("0x{:08x}", data);
                dec_str = std::format("{}", static_cast<int32_t>(data));
                raw_val = data;
            }

            if (raw_val != 0) {
                std::string sym = machine_.symbols.lookup(raw_val);
                if (!sym.empty()) {
                    dec_str += std::format(" <{}>", sym);
                } else if (raw_val == ra) {
                    dec_str += " [ra]";
                } else if (raw_val == fp) {
                    dec_str += " [prev fp]";
                }
            }
        } else {
            val_str = "device_mmio";
        }
    } else {
        val_str = "unmapped";
    }

    const char* label_color = nullptr;
    const char* val_color = nullptr;
    if (word_offset == 0) {
        label_color = kThemeMint;
        val_color = kThemeMint;
    } else if (target_vaddr == fp) {
        label_color = kThemeSky;
        val_color = kThemeSky;
    } else if (word_offset > 0) {  // Active frame (addresses above sp)
        label_color = kThemePeach;
        val_color = kThemeVal;
    } else {  // Free/scratch area (addresses below sp)
        label_color = kThemeMuted;
        val_color = kThemeMuted;
    }

    // Formatting: dynamically compute alignment to completely prevent hex value truncation
    std::string left_part = std::format("  \033[38;5;244m{}\033[0m  {}{:<8}\033[0m: {}{}\033[0m",
                                        addr_str, label_color, offset_str, val_color, val_str);
    int printable_len =
        2 + static_cast<int>(addr_str.length()) + 2 + 8 + 2 + static_cast<int>(val_str.length());
    int pad_len = std::max(1, col_width - printable_len);
    std::string padding = std::string(static_cast<std::size_t>(pad_len), ' ');
    std::string full_row = left_part + padding + std::format(" {}{}\033[0m", kThemeText, dec_str);

    return format_to_width(full_row, width);
}

auto LeftPane::get_stack_addr_at_row(int logical_row) const -> std::optional<Register> {
    if (page_ != TuiRegPage::STACK) return std::nullopt;
    const auto& st = current_cpu().state();
    Register sp = st.regs.read(RegId::Sp);
    if (sp == 0) return std::nullopt;
    int word_size = static_cast<int>(st.regs.xlen) / 8;
    if (logical_row >= 1 && logical_row <= 32) {
        int offset = (logical_row - 1) * word_size;
        return sp + static_cast<Register>(offset);
    }
    return std::nullopt;
}

}  // namespace simrv::tui
