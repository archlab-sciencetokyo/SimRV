/**
 * @file LeftPaneIo.cpp
 * @brief Implements I/O Bus & Peripheral MMIO Inspector for TUI Left Pane.
 */
#include <format>
#include <string>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/device/Console.hpp"
#include "simrv/device/Disk.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/panels/LeftPane.hpp"
#include "simrv/util/FormatUtil.hpp"

namespace simrv::tui {

auto LeftPane::render_io_stats(const simrv::core::CPU& cpu, int logical_row, int col_width,
                               int right_width) -> std::string {
    int const width = col_width + right_width;

    if (logical_row == 0) {
        return section_line("Memory Interconnect & MMIO Inspector", width);
    }
    if (logical_row == 1) {
        if (width < 50) {
            return format_to_width(
                std::format("  {}Space:\033[0m {}DRAM\033[0m │ {}CLINT\033[0m", kThemeText,
                            kThemeMint, kThemeSky),
                width);
        }
        return format_to_width(
            std::format("  {}Space:\033[0m {}DRAM 0x80000000\033[0m │ {}CLINT 0x02000000\033[0m",
                        kThemeText, kThemeMint, kThemeSky),
            width);
    }
    if (logical_row == 2) {
        if (width < 50) {
            return format_to_width(
                std::format("  {}MMIO:\033[0m  {}UART\033[0m │ {}Disk\033[0m", kThemeText,
                            kThemeVal, kThemeMint),
                width);
        }
        return format_to_width(
            std::format("  {}MMIO:\033[0m  {}UART 0x10000000\033[0m │ {}Disk 0x10001000\033[0m",
                        kThemeText, kThemeVal, kThemeMint),
            width);
    }
    if (logical_row == 3) {
        return section_line("VirtIO Disk Controller (MMIO 0x10001000, IRQ 2)", width);
    }

    bool has_disk = (machine_.disk != nullptr);
    if (logical_row == 4) {
        if (!has_disk) {
            return format_to_width(
                std::format(
                    "  {}VirtIO Disk:\033[0m \033[38;5;244mOFFLINE (No image loaded)\033[0m",
                    kThemeText),
                width);
        }
        uint32_t status = machine_.disk->Status;
        uint32_t irq_stat = machine_.disk->InterruptStatus;
        std::string status_str = (status == 0x0f) ? "\033[38;5;120m0x0f (DRIVER_OK)\033[0m"
                                                  : std::format("0x{:02x}", status);
        if (width < 50) {
            return format_to_width(
                std::format("  {}Status:\033[0m {} │ {}IRQ:\033[0m 0x{:02x}", kThemeText,
                            status_str, kThemeText, irq_stat),
                width);
        }
        return format_to_width(
            std::format("  {}Status:\033[0m {} │ {}IRQ Stat:\033[0m 0x{:02x} │ {}Magic:\033[0m "
                        "{}0x74726976\033[0m",
                        kThemeText, status_str, kThemeText, irq_stat, kThemeText, kThemeVal),
            width);
    }
    if (logical_row == 5) {
        if (!has_disk || machine_.disk->Queue == nullptr) {
            return format_to_width(
                std::format(
                    "  {}VRING:\033[0m  \033[38;5;244mQueue Uninitialized by OS Kernel\033[0m",
                    kThemeText),
                width);
        }
        const auto& q = machine_.disk->Queue[0];
        uint32_t desc = static_cast<uint32_t>(q.DescLow & 0xFFFFFFFFULL);
        uint32_t avail = static_cast<uint32_t>(q.AvailLow & 0xFFFFFFFFULL);
        uint32_t used = static_cast<uint32_t>(q.UsedLow & 0xFFFFFFFFULL);
        if (width < 50) {
            return format_to_width(
                std::format("  {}VRING:\033[0m D:0x{:04x} A:0x{:04x} U:0x{:04x}", kThemeText,
                            desc & 0xFFFF, avail & 0xFFFF, used & 0xFFFF),
                width);
        }
        return format_to_width(
            std::format("  {}VRING0:\033[0m {}Desc 0x{:08x}\033[0m │ {}Avail 0x{:08x}\033[0m │ "
                        "{}Used 0x{:08x}\033[0m",
                        kThemeText, kThemeMint, desc, kThemeSky, avail, kThemePeach, used),
            width);
    }
    if (logical_row == 6) {
        return section_line("VirtIO Console & NS16550A UART (MMIO 0x10000000, IRQ 1)", width);
    }
    if (logical_row == 7) {
        bool has_console = (machine_.console != nullptr);
        uint32_t c_status = has_console ? machine_.console->Status : 0;
        std::string c_str = (c_status == 0x0f) ? "\033[38;5;120m0x0f (DRIVER_OK)\033[0m"
                                               : std::format("0x{:02x}", c_status);
        if (width < 50) {
            return format_to_width(
                std::format("  {}Console:\033[0m {} │ {}UART:\033[0m \033[38;5;120m115200\033[0m",
                            kThemeText, c_str, kThemeText),
                width);
        }
        return format_to_width(std::format("  {}Console Status:\033[0m {} │ {}UART:\033[0m "
                                           "\033[38;5;120mNS16550A 115200 8N1\033[0m",
                                           kThemeText, c_str, kThemeText),
                               width);
    }
    if (logical_row == 8) {
        return section_line("System Execution Counters & CPI / IPC Stats", width);
    }

    uint64_t icount = cpu.e_icount;
    uint64_t cycles = (machine_.s_cycle_accurate) ? cpu.pipeline_sim.cycle_count() : icount;

    if (logical_row == 9) {
        return format_to_width(
            std::format("  {}Insts:\033[0m {}{:<10}\033[0m │ {}Cycles:\033[0m {}{}\033[0m",
                        kThemeText, kThemeMint, simrv::util::format_with_commas(icount), kThemeText,
                        kThemeVal, simrv::util::format_with_commas(cycles)),
            width);
    }
    if (logical_row == 10) {
        double cpi =
            (icount == 0) ? 1.0 : static_cast<double>(cycles) / static_cast<double>(icount);
        double ipc =
            (cycles == 0) ? 1.0 : static_cast<double>(icount) / static_cast<double>(cycles);
        return format_to_width(
            std::format("  {}CPI:\033[0m {}{:5.2f}\033[0m │ {}IPC:\033[0m {}{:5.2f}\033[0m │ "
                        "{}Mode:\033[0m {}{}\033[0m",
                        kThemeText, kThemePeach, cpi, kThemeText, kThemeMint, ipc, kThemeText,
                        kThemeSky, (machine_.s_cycle_accurate ? "5-Stage Pipeline" : "Functional")),
            width);
    }
    if (logical_row == 11) {
        return section_line("TileLink-C Directory Coherence Hub (5-Channel MSI)", width);
    }
    if (logical_row == 12) {
        const auto& c_stats = machine_.memory().system_bus().coherence_hub().stats();
        return format_to_width(
            std::format("  {}Acquire:\033[0m {}{:<7}\033[0m │ {}Probe:\033[0m {}{:<7}\033[0m │ "
                        "{}Grant:\033[0m {}{}\033[0m",
                        kThemeText, kThemeMint, simrv::util::format_with_commas(c_stats.acquire_count),
                        kThemeText, kThemeSky, simrv::util::format_with_commas(c_stats.probe_count),
                        kThemeText, kThemePeach, simrv::util::format_with_commas(c_stats.grant_count)),
            width);
    }
    if (logical_row == 13) {
        const auto& c_stats = machine_.memory().system_bus().coherence_hub().stats();
        return format_to_width(
            std::format("  {}Release:\033[0m {}{:<7}\033[0m │ {}Inval:\033[0m {}{:<7}\033[0m │ "
                        "{}WBack:\033[0m {}{}\033[0m",
                        kThemeText, kThemeCoral, simrv::util::format_with_commas(c_stats.release_count),
                        kThemeText, kThemeVal, simrv::util::format_with_commas(c_stats.invalidation_count),
                        kThemeText, kThemePink, simrv::util::format_with_commas(c_stats.writeback_count)),
            width);
    }

    return format_to_width("", width);
}

}  // namespace simrv::tui
