/**
 * @file LeftPaneTlb.cpp
 * @brief Implements TLB (Translation Lookaside Buffer) Inspector for TUI Left Pane.
 */
#include "simrv/tui/panels/LeftPane.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Tlb.hpp"
#include "simrv/xlen/Types.hpp"
#include <format>
#include <string>
#include <vector>

namespace simrv::tui {

auto LeftPane::render_tlb_stats(const simrv::core::CPU& cpu, int logical_row, int col_width,
                                int right_width) -> std::string {
    int const width = col_width + right_width;
    const auto& st = cpu.state();
    const auto& tlb = cpu.tlb;

    // Parse satp details
    Register satp_val = st.satp;
    std::string mode_str = "Bare (Off)";
    uint64_t asid = 0;
    uint64_t ppn = 0;

    if constexpr (simrv::xlen::kIsXLen64) {
        uint64_t mode = (satp_val >> 60) & 0xFULL;
        asid = (satp_val >> 44) & 0xFFFFULL;
        ppn = satp_val & 0xFFFFFFFFFFFULL;
        if (mode == 8) mode_str = "Sv39";
        else if (mode == 9) mode_str = "Sv48";
        else if (mode != 0) mode_str = std::format("Mode {}", mode);
    } else {
        uint32_t mode = (satp_val >> 31) & 0x1U;
        asid = (satp_val >> 22) & 0x1FFU;
        ppn = satp_val & 0x3FFFFFU;
        if (mode == 1) mode_str = "Sv32";
    }

    // Collect active valid entries from ITLB, DTLB-R, DTLB-W
    struct RenderTlbEntry {
        const char* type;
        size_t set;
        int way;
        Address vaddr;
        Address paddr;
        Word asid;
        const char* priv_str;
    };
    std::vector<RenderTlbEntry> active_entries;

    auto priv_to_str = [](auto priv) -> const char* {
        switch (priv) {
            case PrivilegeLevel::User: return "User";
            case PrivilegeLevel::Supervisor: return "Super";
            case PrivilegeLevel::Machine: return "Mach";
            default: return "?";
        }
    };

    for (size_t set = 0; set < simrv::core::Tlb::kNumSets; ++set) {
        for (int way = 0; way < 2; ++way) {
            const auto& ie = tlb.inst_r[set][way];
            if (ie.valid) {
                active_entries.push_back(RenderTlbEntry{"ITLB", set, way, ie.v_addr, ie.p_addr, ie.asid, priv_to_str(ie.priv)});
            }
            const auto& dr = tlb.data_r[set][way];
            if (dr.valid) {
                active_entries.push_back(RenderTlbEntry{"DTLB-R", set, way, dr.v_addr, dr.p_addr, dr.asid, priv_to_str(dr.priv)});
            }
            const auto& dw = tlb.data_w[set][way];
            if (dw.valid) {
                active_entries.push_back(RenderTlbEntry{"DTLB-W", set, way, dw.v_addr, dw.p_addr, dw.asid, priv_to_str(dw.priv)});
            }
        }
    }

    if (logical_row == 0) {
        return section_line("Virtual Memory & TLB Inspector", width);
    }
    if (logical_row == 1) {
        return format_to_width(
            std::format("  {}SATP Mode:\033[0m {}{:<5}\033[0m │ {}ASID:\033[0m {}{:<2}\033[0m │ {}Root PPN:\033[0m {}0x{:x}\033[0m",
                        kThemeText, kThemeMint, mode_str,
                        kThemeText, kThemeVal, asid,
                        kThemeText, kThemeSky, ppn),
            width);
    }
    if (logical_row == 2) {
        return section_line("Active TLB Translation Entries", width);
    }

    if (active_entries.empty()) {
        if (logical_row == 3) {
            return format_to_width(
                std::format("  {}<No active TLB entries cached (flush or direct physical mapping)>\033[0m",
                            kThemeMuted),
                width);
        }
        return format_to_width("", width);
    }

    int entry_idx = logical_row - 3;
    if (entry_idx >= 0 && entry_idx < static_cast<int>(active_entries.size())) {
        const auto& e = active_entries[static_cast<size_t>(entry_idx)];
        uint32_t v_trunc = static_cast<uint32_t>(e.vaddr & 0xFFFFFFFFULL);
        uint32_t p_trunc = static_cast<uint32_t>(e.paddr & 0xFFFFFFFFULL);
        return format_to_width(
            std::format("  {}Set{:02d}/Way{}\033[0m \033[1m{:<6}\033[0m {:08x} ➔ {:08x}  {}ASID:{:<2}\033[0m {}[{}]\033[0m",
                        kThemeMuted, e.set, e.way,
                        e.type,
                        v_trunc, p_trunc,
                        kThemeText, e.asid,
                        kThemePink, e.priv_str),
            width);
    }

    return format_to_width("", width);
}

} // namespace simrv::tui
