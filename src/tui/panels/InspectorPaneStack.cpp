/**
 * @file InspectorPaneStack.cpp
 * @brief Stack and live memory view pane rendering for the TUI register panel.
 */
#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <optional>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/Mmu.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"

namespace simrv::tui {

auto InspectorPane::translate_safe(const simrv::core::CPU& cpu, Register vaddr) const
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
        return res.value().raw();
    }
    return std::nullopt;
}

auto InspectorPane::render_stack_frame(const simrv::core::CPU& cpu, int logical_row, int col_width,
                                       int right_width) -> std::string {
    int const width = col_width + right_width;

    const bool is_mem_watch = is_custom_memory_inspect();
    const unsigned int xlen = cpu.state().regs.xlen;
    const int word_size = static_cast<int>(xlen) / 8;
    const Register sp = cpu.state().regs.read(RegId::Sp);
    const Register base_addr = is_mem_watch ? inspect_addr_ : sp;

    // Check if the base address is valid and mapped in DRAM
    auto base_phys = translate_safe(cpu, base_addr);
    const bool base_valid = (base_addr != 0 && base_phys.has_value() &&
                             machine_.memory_geometry().contains(*base_phys));

    // Fallback: Display warning message when address is invalid or inactive
    if (!base_valid) {
        int const block_width = std::min(52, std::max(1, width - 4));
        int const block_left = std::max(2, (width - block_width) / 2);
        auto inactive_row = [&](const std::string& text) {
            return format_to_width(std::string(static_cast<std::size_t>(block_left), ' ') + text,
                                   width);
        };
        if (logical_row == 0) {
            return section_line(is_mem_watch ? "Live Guest Memory Watch" : "Live Guest Stack Watch",
                                width);
        }
        if (logical_row == 3) {
            return inactive_row(
                is_mem_watch
                    ? std::format("\033[1;31mTARGET ADDRESS UNMAPPED\033[0m  {}not in ram\033[0m",
                                  kThemeMuted)
                    : std::format("\033[1;31mSTACK POINTER INACTIVE\033[0m  {}waiting\033[0m",
                                  kThemeMuted));
        }
        if (logical_row == 4) {
            std::string val_str = (xlen == 64) ? std::format("0x{:016x}", base_addr)
                                               : std::format("0x{:08x}", base_addr);
            return inactive_row(is_mem_watch ? std::format("{}target addr\033[0m · {}{}\033[0m",
                                                           kThemeText, kThemeVal, val_str)
                                             : std::format("{}sp register\033[0m · {}{}\033[0m",
                                                           kThemeText, kThemeVal, val_str));
        }
        if (logical_row == 5) {
            std::string const text =
                is_mem_watch   ? "Address not in DRAM range. Press 'm' to choose another."
                : (width < 48) ? "Activates after guest initializes sp."
                               : "Stack watch activates after guest initializes sp.";
            return inactive_row(std::format("{}{}\033[0m", kThemeMuted, text));
        }
        if (logical_row == 14) {
            return section_line(
                is_mem_watch ? "Status: Memory Location Inactive" : "Status: Stack Watch Inactive",
                width);
        }
        return format_to_width("", width);
    }

    const Register aligned_base = base_addr & ~(static_cast<Register>(word_size) - 1);
    const bool is_16b_aligned = (base_addr % 16 == 0);

    if (logical_row == 0) {
        if (is_mem_watch) {
            std::string sym = machine_.symbol_table().lookup(aligned_base);
            std::string title;
            if (!sym.empty() && width >= 56) {
                title = (xlen == 64)
                            ? std::format("Memory Watch: <{}> [0x{:016x}]", sym, aligned_base)
                            : std::format("Memory Watch: <{}> [0x{:08x}]", sym, aligned_base);
            } else {
                title = (xlen == 64) ? std::format("Memory Watch: 0x{:016x}", aligned_base)
                                     : std::format("Memory Watch: 0x{:08x}", aligned_base);
            }
            return section_line(title, width);
        }
        std::string title = width >= 50 ? std::format("Stack Watch ({})",
                                                      is_16b_aligned ? "16B aligned" : "unaligned")
                                        : "Stack Watch";
        return section_line(title, width);
    }
    if (logical_row == 14) {
        std::string footer_desc;
        if (is_mem_watch) {
            footer_desc = (width >= 60) ? "Memory View · [m] Set Addr · [sp] Stack · [↑/↓] Scroll"
                                        : "Memory · [m] Addr · [sp] Stack";
        } else {
            if (width >= 60) {
                footer_desc = std::format("Occupancy: sp · frame · scratch ({}) · [m] Memory",
                                          is_16b_aligned ? "16B aligned" : "unaligned");
            } else {
                footer_desc = "sp · frame · [m] Memory";
            }
        }
        return section_line(footer_desc, width);
    }
    if (logical_row > 14) {
        return format_to_width("", width);
    }

    // logical_row ranges from 1 to 13 (inclusive). 13 elements.
    // Center it around base_addr (word_offset = 0 at logical_row = 7).
    // Note: logical_row already incorporates vertical scroll offset from ScrollView.
    const int word_offset = logical_row - 7;
    Register target_vaddr = aligned_base + static_cast<Register>(word_offset * word_size);
    if (xlen == 32) {
        target_vaddr &= 0xFFFFFFFFULL;
    }

    std::string addr_str = (xlen == 64) ? std::format("0x{:016x}", target_vaddr)
                                        : std::format("0x{:08x}", target_vaddr);

    const Register fp = cpu.state().regs.read(RegId::S0);
    const Register ra = cpu.state().regs.read(RegId::Ra);

    std::string offset_str;
    if (is_mem_watch) {
        offset_str = (word_offset == 0) ? "target" : std::format("{:+d}", word_offset * word_size);
    } else {
        if (word_offset == 0) {
            offset_str = "sp";
        } else if (target_vaddr == fp) {
            offset_str = "s0/fp";
        } else {
            offset_str = std::format("sp{:+d}", word_offset * word_size);
        }
    }

    std::string val_str = "????";
    std::string ascii_str = "";
    std::string sym_str = "";

    auto paddr_opt = translate_safe(cpu, target_vaddr);
    if (paddr_opt) {
        Register paddr = *paddr_opt;
        if (machine_.memory_geometry().contains(paddr)) {
            Register raw_val = 0;
            std::array<uint8_t, 8> bytes{};
            if (xlen == 64) {
                uint64_t data = simrv::memory::ram_read_fast(
                    paddr, static_cast<Instruction>(isa::Funct3::Sd), machine_.ram_view());
                val_str = std::format("0x{:016x}", data);
                raw_val = data;
                std::memcpy(bytes.data(), &data, 8);
                for (int b = 0; b < 8; ++b) {
                    const char c = static_cast<char>(bytes[b]);
                    ascii_str += (c >= 32 && c <= 126) ? c : '.';
                }
            } else {
                uint32_t data = simrv::memory::ram_read_fast(
                    paddr, static_cast<Instruction>(isa::Funct3::Sw), machine_.ram_view());
                val_str = std::format("0x{:08x}", data);
                raw_val = data;
                std::memcpy(bytes.data(), &data, 4);
                for (int b = 0; b < 4; ++b) {
                    const char c = static_cast<char>(bytes[b]);
                    ascii_str += (c >= 32 && c <= 126) ? c : '.';
                }
            }

            if (raw_val != 0) {
                std::string sym = machine_.symbol_table().lookup(raw_val);
                if (!sym.empty()) {
                    sym_str = std::format(" <{}>", sym);
                } else if (!is_mem_watch) {
                    if (raw_val == ra) {
                        sym_str = " [ra]";
                    } else if (raw_val == fp) {
                        sym_str = " [prev fp]";
                    }
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
    } else if (!is_mem_watch && target_vaddr == fp) {
        label_color = kThemeSky;
        val_color = kThemeSky;
    } else if (word_offset > 0) {
        label_color = kThemePeach;
        val_color = kThemeVal;
    } else {
        label_color = kThemeMuted;
        val_color = kThemeMuted;
    }

    // Formatting: dynamically compute alignment to completely prevent hex value truncation
    std::string left_part = std::format("  \033[38;5;244m{}\033[0m  {}{:<8}\033[0m: {}{}\033[0m",
                                        addr_str, label_color, offset_str, val_color, val_str);
    int printable_len =
        2 + static_cast<int>(addr_str.length()) + 2 + 8 + 2 + static_cast<int>(val_str.length());

    std::string ascii_part;
    if (!ascii_str.empty() && width >= 60) {
        ascii_part = std::format(" \033[38;5;240m|\033[0m{}\033[38;5;240m|\033[0m", ascii_str);
        printable_len += static_cast<int>(ascii_str.length()) + 3;
    }

    int pad_len = std::max(1, col_width - printable_len);
    std::string padding = std::string(static_cast<std::size_t>(pad_len), ' ');
    std::string full_row =
        left_part + ascii_part + padding + std::format(" {}{}\033[0m", kThemeText, sym_str);

    return format_to_width(full_row, width);
}

auto InspectorPane::get_stack_addr_at_row(int logical_row) const -> std::optional<Register> {
    if (page_ != TuiRegPage::STACK) return std::nullopt;
    const auto& st = current_cpu().state();
    const Register base = is_custom_memory_inspect() ? inspect_addr_ : st.regs.read(RegId::Sp);
    if (base == 0) return std::nullopt;
    const int word_size = static_cast<int>(st.regs.xlen) / 8;
    if (logical_row >= 1 && logical_row <= 13) {
        const int word_offset = logical_row - 7;
        return base + static_cast<Register>(word_offset * word_size);
    }
    return std::nullopt;
}

}  // namespace simrv::tui
