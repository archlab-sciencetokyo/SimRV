/**
 * @file InspectorPaneIo.cpp
 * @brief Implements Comprehensive I/O Bus & Modern Device Inspector for TUI Left Pane.
 */
#include <format>
#include <string>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/Bus.hpp"
#include "simrv/memory/CoherenceHub.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/TileLinkProtocol.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"
#include "simrv/util/FormatUtil.hpp"

namespace simrv::tui {

auto InspectorPane::render_io_stats(const simrv::core::CPU& /*cpu*/, int logical_row, int col_width,
                                    int right_width) -> std::string {
    int const width = col_width + right_width;
    if (page_ == TuiRegPage::BUS) {
        const auto& bus = machine_.memory().system_bus();
        const auto spec = bus.protocol_spec();
        if (logical_row == 0) {
            return section_line(std::format("{} live activity", bus.protocol_display_name()),
                                width);
        }
        if (logical_row == 1) {
            return format_to_width(
                std::format("  {}Spec:\033[0m {} B/beat · max {} outstanding · {} · {}", kThemeText,
                            spec.beat_bytes, spec.max_outstanding,
                            spec.supports_burst ? "bursts" : "single beat",
                            spec.supports_atomics ? "atomics" : "no bus atomics"),
                width);
        }
        if (logical_row == 2) {
            return format_to_width(
                std::format("  {}Reads:\033[0m {}{} │ {}Writes:\033[0m {}{} │ {}Pending:\033[0m {} "
                            "req / {} resp",
                            kThemeText, kThemeMint,
                            simrv::util::format_with_commas(bus.read_count()), kThemeText,
                            kThemePeach, simrv::util::format_with_commas(bus.write_count()),
                            kThemeText, bus.pending_requests(), bus.pending_responses()),
                width);
        }
        if (logical_row == 3) {
            const auto stats = bus.contention_stats();
            return format_to_width(
                std::format("  {}DMA:\033[0m {} │ {}Delayed:\033[0m {} (+{} cyc) │ {}Queue "
                            "waits:\033[0m {} (+{} cyc)",
                            kThemeText, stats.dma_active ? "active" : "idle", kThemeText,
                            simrv::util::format_with_commas(stats.delayed_requests),
                            simrv::util::format_with_commas(stats.added_latency_cycles), kThemeText,
                            simrv::util::format_with_commas(stats.queue_contended_requests),
                            simrv::util::format_with_commas(stats.queue_wait_cycles)),
                width);
        }
        if (logical_row == 4) {
            if (!bus.has_coherence()) {
                return format_to_width("  Coherence: unsupported by this bus protocol.", width);
            }
            const auto& stats = bus.coherence_hub().stats();
            return format_to_width(
                std::format("  {}Coherence:\033[0m {} acquires · {} probes · {} grants · {} "
                            "releases · {} writebacks",
                            kThemeText, simrv::util::format_with_commas(stats.acquire_count),
                            simrv::util::format_with_commas(stats.probe_count),
                            simrv::util::format_with_commas(stats.grant_count),
                            simrv::util::format_with_commas(stats.release_count),
                            simrv::util::format_with_commas(stats.writeback_count)),
                width);
        }
        if (logical_row == 5) {
            return section_line(spec.is_axi() ? "AXI bridge" : "TileLink fabric", width);
        }
        if (logical_row == 6) {
            if (spec.is_axi()) {
                const auto* bridge = machine_.axi_bridge();
                if (bridge == nullptr)
                    return format_to_width("  AXI bridge is unavailable.", width);
                const auto channels = bridge->channel_stats();
                return format_to_width(
                    std::format("  {}AR:\033[0m {} │ {}R:\033[0m {} beats │ {}AW:\033[0m {} │ "
                                "{}W:\033[0m {} beats │ {}B:\033[0m {} responses",
                                kThemeText, channels.ar, kThemeText, channels.r, kThemeText,
                                channels.aw, kThemeText, channels.w, kThemeText, channels.b),
                    width);
            }
            const auto& checker = bus.protocol_checker();
            return format_to_width(
                std::format(
                    "  {}A req:\033[0m {} │ {}B probe:\033[0m {} │ {}C release:\033[0m {} │ "
                    "{}D resp:\033[0m {} │ {}E ack:\033[0m {}",
                    kThemeText, checker.outstanding_sources(), kThemeText,
                    checker.outstanding_probes(), kThemeText, checker.outstanding_releases(),
                    kThemeText, bus.pending_responses(), kThemeText, checker.outstanding_sinks()),
                width);
        }
        if (logical_row == 7) {
            return section_line(
                spec.is_axi() ? "AXI channel trace" : "Recent TileLink transactions", width);
        }
        if (logical_row >= 8 && logical_row <= 11) {
            if (spec.is_axi()) {
                if (logical_row == 8) {
                    const auto* bridge = machine_.axi_bridge();
                    return format_to_width(
                        bridge != nullptr && bridge->tracer().is_enabled()
                            ? "  AXI trace enabled; channel events are written to the configured "
                              "trace path."
                            : "  AXI trace disabled; enable trace_axi to keep a channel event log.",
                        width);
                }
                return format_to_width("", width);
            }
            const auto& history = bus.transaction_history();
            const int tx_index = static_cast<int>(history.size()) - 1 - (logical_row - 8);
            if (tx_index >= 0 && tx_index < static_cast<int>(history.size())) {
                const auto& tx = history[static_cast<std::size_t>(tx_index)];
                return format_to_width(std::format("  cycle {:>7}  [{}] {:<14} src:{} @ {:#x} {}",
                                                   tx.cycle, memory::to_char(tx.channel), tx.opcode,
                                                   tx.source, tx.address.raw(), tx.detail),
                                       width);
            }
            return format_to_width("  -- idle --", width);
        }
        return format_to_width("", width);
    }

    return format_to_width("", width);
}

auto InspectorPane::render_device_tree_stats(int logical_row, int width) -> std::string {
    const auto& config = machine_.configuration().soc;
    const auto& bus = machine_.memory().system_bus();
    const auto platform = machine_.platform_status();
    if (logical_row == 0) {
        return section_line(std::format("Platform device tree · {} devices", config.devices.size()),
                            width);
    }
    if (logical_row == 1) {
        return format_to_width(
            std::format("  {}Bus:\033[0m {} · {} reads / {} writes · {} req pending · DMA {}",
                        kThemeText, bus.protocol_display_name(),
                        simrv::util::format_with_commas(bus.read_count()),
                        simrv::util::format_with_commas(bus.write_count()), bus.pending_requests(),
                        platform.dma_busy ? "busy" : "idle"),
            width);
    }
    if (logical_row == 2) {
        return format_to_width(
            std::format(
                "  {}DEVICE / NODE{}\033[0m   {}MEMORY MAP{}\033[0m   {}IRQ / LIVE STATS{}\033[0m",
                kThemeText, kThemeText, kThemeText, kThemeText, kThemeText, kThemeText),
            width);
    }

    int row = 3;
    for (const auto& device : config.devices) {
        if (row++ == logical_row) {
            const std::string name =
                device.name.empty() ? std::string(simrv::core::soc_device_kind_name(device.kind))
                                    : device.name;
            std::string stats = device.enabled ? "enabled" : "disabled";
            if (name.find("net") != std::string::npos ||
                name.find("ethernet") != std::string::npos) {
                stats = std::format("{} tx packets", platform.network_tx_packets);
            } else if (name.find("block") != std::string::npos ||
                       name.find("disk") != std::string::npos) {
                stats = platform.disk_loaded
                            ? std::format("{} sectors · status {:#x}",
                                          platform.disk_capacity_sectors, platform.disk_status)
                            : "no disk image";
            } else if (name.find("dma") != std::string::npos) {
                stats = platform.dma_busy ? "busy" : "idle";
            } else if (name.find("console") != std::string::npos) {
                stats = std::format("status {:#x}", platform.console_status);
            }
            return format_to_width(
                std::format("  {}{}{}\033[0m  {:#010x}..{:#010x}  irq {} · {}",
                            device.enabled ? kThemeMint : kThemeMuted, name, kThemeText,
                            device.base.raw(), device.base.raw() + device.size - 1, device.irq,
                            stats),
                width);
        }
        for (const auto& region : device.regions) {
            if (row++ == logical_row) {
                return format_to_width(
                    std::format("    {}{}{}\033[0m  {:#010x}..{:#010x}  subregion", kThemeSky,
                                region.name, kThemeText, region.base.raw(),
                                region.base.raw() + region.size - 1),
                    width);
            }
        }
    }
    if (logical_row == row) {
        return format_to_width("  Memory map ranges are configured in the selected SoC profile.",
                               width);
    }
    return format_to_width("", width);
}

}  // namespace simrv::tui
