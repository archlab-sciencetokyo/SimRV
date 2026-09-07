/**
 * @file InspectorPaneIo.cpp
 * @brief Implements Comprehensive I/O Bus & Modern Device Inspector for TUI Left Pane.
 */
#include <algorithm>
#include <format>
#include <string>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/device/mmio/VirtioMmioBlock.hpp"
#include "simrv/device/mmio/VirtioMmioConsole.hpp"
#include "simrv/device/mmio/VirtioMmioGpu.hpp"
#include "simrv/device/mmio/VirtioMmioInput.hpp"
#include "simrv/device/mmio/VirtioMmioNet.hpp"
#include "simrv/device/mmio/VirtioMmioRng.hpp"
#include "simrv/device/mmio/VirtioMmioSound.hpp"
#include "simrv/device/pci/VirtioPciBlock.hpp"
#include "simrv/device/pci/VirtioPciConsole.hpp"
#include "simrv/device/pci/VirtioPciGpu.hpp"
#include "simrv/device/pci/VirtioPciInput.hpp"
#include "simrv/device/pci/VirtioPciNet.hpp"
#include "simrv/device/pci/VirtioPciRng.hpp"
#include "simrv/memory/TileLinkBus.hpp"
#include "simrv/memory/TileLinkProtocol.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"
#include "simrv/util/FormatUtil.hpp"

namespace simrv::tui {

namespace {

auto format_dev_status(uint32_t status) -> std::string {
    if (status == 0x0f || status == 0x8f || (status & 0x07) == 0x07) {
        return "\033[38;5;120mDRIVER_OK\033[0m";
    }
    if (status == 0x03) {
        return "\033[38;5;215mACKNOWLEDGED\033[0m";
    }
    if (status == 0x01) {
        return "\033[38;5;117mRESET\033[0m";
    }
    if (status == 0x00) {
        return "\033[38;5;244mSTANDBY\033[0m";
    }
    return std::format("\033[38;5;215m0x{:02x}\033[0m", status);
}

}  // namespace

auto InspectorPane::render_io_stats(const simrv::core::CPU& cpu, int logical_row, int col_width,
                                    int right_width) -> std::string {
    int const width = col_width + right_width;
    const auto platform = machine_.platform_status();
    const bool is_mmio = platform.profile == simrv::core::PlatformProfile::Mmio;

    if (logical_row == 0) {
        return section_line(is_mmio ? "VirtIO-MMIO v2 Interconnect & SoC Bus Inspector"
                                    : "PCIe Interconnect & Modern MMIO Inspector",
                            width);
    }
    if (logical_row == 1) {
        if (width < 50) {
            return format_to_width(
                std::format("  {}Space:\033[0m {}DRAM\033[0m │ {}{}\033[0m", kThemeText, kThemeMint,
                            kThemeSky, is_mmio ? "MMIO" : "ECAM"),
                width);
        }
        return format_to_width(
            std::format("  {}Space:\033[0m {}DRAM 0x80000000\033[0m │ {}\033[0m", kThemeText,
                        kThemeMint,
                        is_mmio ? "\033[38;5;117mMMIO 0x10001000..0x10007000"
                                : "\033[38;5;117mECAM 0x30000000 │ BAR 0x40000000"),
            width);
    }
    if (logical_row == 2) {
        return section_line(is_mmio ? "Storage & Networking (VirtIO-MMIO)"
                                    : "Storage & Networking (PCIe Endpoints)",
                            width);
    }

    const bool has_disk = platform.disk_loaded;

    if (logical_row == 3) {
        if (!has_disk) {
            return format_to_width(
                std::format("  {}Block:\033[0m \033[38;5;244mSTANDBY (No disk attached)\033[0m",
                            kThemeText),
                width);
        }
        uint32_t status = platform.disk_status;
        uint32_t irq_stat = platform.disk_isr;
        uint64_t cap_sec = platform.disk_capacity_sectors;
        auto status_str = format_dev_status(status);
        if (width < 50) {
            return format_to_width(
                std::format("  {}Blk:\033[0m {} │ {}Cap:\033[0m {}{}MB\033[0m", kThemeText,
                            status_str, kThemeText, kThemeVal, (cap_sec * 512) / (1024 * 1024)),
                width);
        }
        return format_to_width(
            std::format("  {}VirtIO-Blk:\033[0m {} │ {}ISR:\033[0m 0x{:02x} │ {}Capacity:\033[0m "
                        "{}{} MB\033[0m",
                        kThemeText, status_str, kThemeText, irq_stat, kThemeText, kThemeVal,
                        (cap_sec * 512) / (1024 * 1024)),
            width);
    }
    if (logical_row == 4) {
        uint32_t net_status = platform.network_status;
        std::string mac_str = "52:54:00:12:34:56";
        size_t tx_pkts = platform.network_tx_packets;
        auto status_str = format_dev_status(net_status);
        if (width < 50) {
            return format_to_width(
                std::format("  {}Net:\033[0m {} │ {}Tx:\033[0m {}{}\033[0m", kThemeText, status_str,
                            kThemeText, kThemeMint, tx_pkts),
                width);
        }
        return format_to_width(std::format("  {}VirtIO-Net:\033[0m {} │ {}MAC:\033[0m {}{}\033[0m "
                                           "│ {}Tx Pkts:\033[0m {}{}\033[0m",
                                           kThemeText, status_str, kThemeText, kThemeSky, mac_str,
                                           kThemeText, kThemeMint, tx_pkts),
                               width);
    }
    if (logical_row == 5) {
        return section_line("Console, UART & Peripheral Endpoints", width);
    }
    if (logical_row == 6) {
        uint32_t c_status = platform.console_status;
        auto c_str = format_dev_status(c_status);

        if (width < 50) {
            return format_to_width(
                std::format("  {}Console:\033[0m {} │ {}UART:\033[0m \033[38;5;120m115200\033[0m",
                            kThemeText, c_str, kThemeText),
                width);
        }
        return format_to_width(std::format("  {}VirtIO-Console:\033[0m {} │ {}16550 UART:\033[0m "
                                           "\033[38;5;120m115200 8N1\033[0m",
                                           kThemeText, c_str, kThemeText),
                               width);
    }
    if (logical_row == 7) {
        uint32_t rng_status = platform.rng_status;
        uint32_t gpu_status = platform.gpu_status;
        auto rng_str = format_dev_status(rng_status);
        auto gpu_str = format_dev_status(gpu_status);

        return format_to_width(std::format("  {}RNG:\033[0m {} │ {}GPU:\033[0m {} │ {}RTC:\033[0m "
                                           "\033[38;5;120mGoldfish\033[0m",
                                           kThemeText, rng_str, kThemeText, gpu_str, kThemeText),
                               width);
    }
    if (logical_row == 8) {
        return section_line("Advanced Interrupt Architecture (AIA) & ACLINT", width);
    }
    if (logical_row == 9) {
        return format_to_width(
            std::format(
                "  {}APLIC-M:\033[0m {}0x0C000000\033[0m │ {}APLIC-S:\033[0m {}0x0D000000\033[0m │ "
                "{}IMSIC:\033[0m {}MSI Active\033[0m",
                kThemeText, kThemeMint, kThemeText, kThemeSky, kThemeText, kThemePeach),
            width);
    }
    if (logical_row == 10) {
        return section_line("System Execution Counters & CPI / IPC Stats", width);
    }

    uint64_t icount = cpu.e_icount;
    uint64_t cycles =
        machine_.runtime_profile.is_cycle_mode() ? cpu.pipeline_sim.cycle_count() : icount;

    if (logical_row == 11) {
        return format_to_width(
            std::format("  {}Insts:\033[0m {}{:<10}\033[0m │ {}Cycles:\033[0m {}{}\033[0m",
                        kThemeText, kThemeMint, simrv::util::format_with_commas(icount), kThemeText,
                        kThemeVal, simrv::util::format_with_commas(cycles)),
            width);
    }
    if (logical_row == 12) {
        double cpi =
            (icount == 0) ? 1.0 : static_cast<double>(cycles) / static_cast<double>(icount);
        double ipc =
            (cycles == 0) ? 1.0 : static_cast<double>(icount) / static_cast<double>(cycles);
        return format_to_width(
            std::format("  {}CPI:\033[0m {}{:5.2f}\033[0m │ {}IPC:\033[0m {}{:5.2f}\033[0m │ "
                        "{}Mode:\033[0m {}{}\033[0m",
                        kThemeText, kThemePeach, cpi, kThemeText, kThemeMint, ipc, kThemeText,
                        kThemeSky,
                        (machine_.runtime_profile.is_cycle_mode() ? "Pipeline" : "Functional")),
            width);
    }
    if (logical_row == 13) {
        return section_line("TileLink-C Directory Coherence Hub (Illinois MESI)", width);
    }
    if (logical_row == 14) {
        const auto& c_stats = machine_.memory().system_bus().coherence_hub().stats();
        if (width < 50) {
            return format_to_width(
                std::format(
                    "  {}Acq:\033[0m {}{:<5}\033[0m │ {}Prb:\033[0m {}{:<5}\033[0m │ "
                    "{}Grnt:\033[0m {}{}\033[0m",
                    kThemeText, kThemeMint, simrv::util::format_with_commas(c_stats.acquire_count),
                    kThemeText, kThemeSky, simrv::util::format_with_commas(c_stats.probe_count),
                    kThemeText, kThemePeach, simrv::util::format_with_commas(c_stats.grant_count)),
                width);
        }
        return format_to_width(
            std::format(
                "  {}Acquire:\033[0m {}{:<7}\033[0m │ {}Probe:\033[0m {}{:<7}\033[0m │ "
                "{}Grant:\033[0m {}{:<7}\033[0m │ {}Prefetch:\033[0m {}{}\033[0m",
                kThemeText, kThemeMint, simrv::util::format_with_commas(c_stats.acquire_count),
                kThemeText, kThemeSky, simrv::util::format_with_commas(c_stats.probe_count),
                kThemeText, kThemePeach, simrv::util::format_with_commas(c_stats.grant_count),
                kThemeText, kThemeVal, simrv::util::format_with_commas(c_stats.prefetch_count)),
            width);
    }
    if (logical_row == 15) {
        const auto& c_stats = machine_.memory().system_bus().coherence_hub().stats();
        if (width < 50) {
            return format_to_width(
                std::format("  {}Rel:\033[0m {}{:<5}\033[0m │ {}Inv:\033[0m {}{:<5}\033[0m │ "
                            "{}WB:\033[0m {}{}\033[0m",
                            kThemeText, kThemeCoral,
                            simrv::util::format_with_commas(c_stats.release_count), kThemeText,
                            kThemeVal, simrv::util::format_with_commas(c_stats.invalidation_count),
                            kThemeText, kThemePink,
                            simrv::util::format_with_commas(c_stats.writeback_count)),
                width);
        }
        return format_to_width(
            std::format(
                "  {}Release:\033[0m {}{:<7}\033[0m │ {}Inval:\033[0m {}{:<7}\033[0m │ "
                "{}WBack:\033[0m {}{}\033[0m",
                kThemeText, kThemeCoral, simrv::util::format_with_commas(c_stats.release_count),
                kThemeText, kThemeVal, simrv::util::format_with_commas(c_stats.invalidation_count),
                kThemeText, kThemePink, simrv::util::format_with_commas(c_stats.writeback_count)),
            width);
    }
    if (logical_row == 16) {
        return section_line("TileLink-C Channels (A-E) & Fabric State", width);
    }
    if (logical_row == 17) {
        auto const& bus = machine_.memory().system_bus();
        auto const& checker = bus.protocol_checker();
        if (width < 50) {
            return format_to_width(
                std::format("  {}A:\033[0m{}{}\033[0m │ {}B:\033[0m{}{}\033[0m │ "
                            "{}C:\033[0m{}{}\033[0m │ {}D:\033[0m{}{}\033[0m │ "
                            "{}E:\033[0m{}{}\033[0m",
                            kThemeText, kThemeMint, checker.outstanding_sources(), kThemeText,
                            kThemeSky, checker.outstanding_probes(), kThemeText, kThemeCoral,
                            checker.outstanding_releases(), kThemeText, kThemePeach,
                            bus.pending_responses(), kThemeText, kThemeVal,
                            checker.outstanding_sinks()),
                width);
        }
        return format_to_width(
            std::format("  {}A(Req):\033[0m {}{:<3}\033[0m │ {}B(Probe):\033[0m {}{:<3}\033[0m │ "
                        "{}C(Rel):\033[0m {}{:<3}\033[0m │ {}D(Resp):\033[0m {}{:<3}\033[0m │ "
                        "{}E(Ack):\033[0m {}{}\033[0m",
                        kThemeText, kThemeMint, checker.outstanding_sources(), kThemeText,
                        kThemeSky, checker.outstanding_probes(), kThemeText, kThemeCoral,
                        checker.outstanding_releases(), kThemeText, kThemePeach,
                        bus.pending_responses(), kThemeText, kThemeVal,
                        checker.outstanding_sinks()),
            width);
    }
    if (logical_row == 18) {
        return section_line("Recent Bus Transactions", width);
    }
    if (logical_row >= 19 && logical_row <= 24) {
        auto const& history = machine_.memory().system_bus().transaction_history();
        int const tx_index = static_cast<int>(history.size()) - 1 - (logical_row - 19);
        if (tx_index >= 0 && tx_index < static_cast<int>(history.size())) {
            auto const& tx = history[tx_index];
            const char* ch_color = kThemeMint;
            switch (tx.channel) {
                case TileLinkChannel::A:
                    ch_color = kThemeMint;
                    break;
                case TileLinkChannel::B:
                    ch_color = kThemeSky;
                    break;
                case TileLinkChannel::C:
                    ch_color = kThemeCoral;
                    break;
                case TileLinkChannel::D:
                    ch_color = kThemePeach;
                    break;
                case TileLinkChannel::E:
                    ch_color = kThemeVal;
                    break;
            }
            std::string src_sink;
            if (tx.channel == TileLinkChannel::A || tx.channel == TileLinkChannel::C) {
                src_sink = std::format("src:{}", tx.source);
            } else if (tx.channel == TileLinkChannel::D) {
                src_sink = std::format("src:{} snk:{}", tx.source, tx.sink);
            } else if (tx.channel == TileLinkChannel::E) {
                src_sink = std::format("snk:{}", tx.sink);
            }
            std::string addr_str = (tx.address != 0) ? std::format("@{:#x}", tx.address) : "";
            std::string detail_str = tx.detail.empty() ? "" : std::format("({})", tx.detail);

            return format_to_width(
                std::format("  \033[2m{:>6}\033[0m {}[{}]\033[0m {:<13} {:<10} {:<12} {}", tx.cycle,
                            ch_color, memory::to_char(tx.channel), tx.opcode, src_sink, addr_str,
                            detail_str),
                width);
        }
        return format_to_width("  \033[2m-- idle --\033[0m", width);
    }

    return format_to_width("", width);
}

}  // namespace simrv::tui
