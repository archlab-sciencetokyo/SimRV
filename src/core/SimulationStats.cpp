/**
 * @file SimulationStats.cpp
 * @brief Implementation of unified simulation statistics and telemetry snapshot.
 */
#include "simrv/core/SimulationStats.hpp"

#include <algorithm>
#include <format>
#include <sstream>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/CoherenceHub.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/TileLinkBus.hpp"

namespace simrv::core {

auto take_stats_snapshot(const Machine& machine) -> MachineStatsSnapshot {
    MachineStatsSnapshot snapshot{};
    snapshot.total_retired_instructions = machine.retired_instruction_count();

    const size_t num_harts = machine.num_harts();
    snapshot.harts.reserve(num_harts);

    for (size_t h = 0; h < num_harts; ++h) {
        const auto& cpu = machine.hart(h);
        HartStatsSnapshot hs{};
        hs.hart_id = static_cast<uint32_t>(h);
        hs.retired_instructions = cpu.e_icount;
        hs.cycles = cpu.clint_mmio.mcycle;
        if (hs.cycles > 0) {
            hs.ipc = static_cast<double>(hs.retired_instructions) / static_cast<double>(hs.cycles);
        }

        hs.icache_hits = cpu.icache.hit_count();
        hs.icache_misses = cpu.icache.miss_count();
        const uint64_t icache_total = hs.icache_hits + hs.icache_misses;
        if (icache_total > 0) {
            hs.icache_hit_rate =
                (static_cast<double>(hs.icache_hits) / static_cast<double>(icache_total)) * 100.0;
        }

        hs.dcache_hits = cpu.dcache.hit_count();
        hs.dcache_misses = cpu.dcache.miss_count();
        const uint64_t dcache_total = hs.dcache_hits + hs.dcache_misses;
        if (dcache_total > 0) {
            hs.dcache_hit_rate =
                (static_cast<double>(hs.dcache_hits) / static_cast<double>(dcache_total)) * 100.0;
        }

        const auto& bp_stats = cpu.branch_predictor.stats();
        hs.branches = bp_stats.total_branches;
        hs.branch_hits = bp_stats.direction_hits;
        hs.branch_misses = bp_stats.direction_misses;
        hs.branch_accuracy = bp_stats.overall_accuracy();

        snapshot.total_cycles = std::max(snapshot.total_cycles, hs.cycles);
        snapshot.harts.push_back(hs);
    }

    if (snapshot.total_cycles > 0) {
        snapshot.aggregate_ipc = static_cast<double>(snapshot.total_retired_instructions) /
                                 static_cast<double>(snapshot.total_cycles);
    }

    const auto& bus = machine.memory().system_bus();
    const auto& coh = bus.coherence_hub().stats();
    snapshot.coherence_acq = coh.acquire_count;
    snapshot.coherence_probe = coh.probe_count;
    snapshot.coherence_release = coh.release_count;
    snapshot.coherence_invalidation = coh.invalidation_count;

    const auto& contention = bus.contention_stats();
    snapshot.bus_delayed_requests = contention.delayed_requests;
    snapshot.bus_contention_penalty = contention.added_latency_cycles;

    return snapshot;
}

auto MachineStatsSnapshot::to_json() const -> std::string {
    std::ostringstream ss;
    ss << "{\n";
    ss << std::format("  \"total_retired_instructions\": {},\n", total_retired_instructions);
    ss << std::format("  \"total_cycles\": {},\n", total_cycles);
    ss << std::format("  \"aggregate_ipc\": {:.4f},\n", aggregate_ipc);
    ss << "  \"coherence\": {\n";
    ss << std::format("    \"acquire\": {},\n", coherence_acq);
    ss << std::format("    \"probe\": {},\n", coherence_probe);
    ss << std::format("    \"release\": {},\n", coherence_release);
    ss << std::format("    \"invalidation\": {}\n", coherence_invalidation);
    ss << "  },\n";
    ss << "  \"bus\": {\n";
    ss << std::format("    \"delayed_requests\": {},\n", bus_delayed_requests);
    ss << std::format("    \"contention_penalty_cycles\": {}\n", bus_contention_penalty);
    ss << "  },\n";
    ss << "  \"harts\": [\n";
    for (size_t i = 0; i < harts.size(); ++i) {
        const auto& h = harts[i];
        ss << "    {\n";
        ss << std::format("      \"hart_id\": {},\n", h.hart_id);
        ss << std::format("      \"retired_instructions\": {},\n", h.retired_instructions);
        ss << std::format("      \"cycles\": {},\n", h.cycles);
        ss << std::format("      \"ipc\": {:.4f},\n", h.ipc);
        ss << "      \"icache\": {\n";
        ss << std::format("        \"hits\": {},\n", h.icache_hits);
        ss << std::format("        \"misses\": {},\n", h.icache_misses);
        ss << std::format("        \"hit_rate_pct\": {:.2f}\n", h.icache_hit_rate);
        ss << "      },\n";
        ss << "      \"dcache\": {\n";
        ss << std::format("        \"hits\": {},\n", h.dcache_hits);
        ss << std::format("        \"misses\": {},\n", h.dcache_misses);
        ss << std::format("        \"hit_rate_pct\": {:.2f}\n", h.dcache_hit_rate);
        ss << "      },\n";
        ss << "      \"branch_predictor\": {\n";
        ss << std::format("        \"branches\": {},\n", h.branches);
        ss << std::format("        \"hits\": {},\n", h.branch_hits);
        ss << std::format("        \"misses\": {},\n", h.branch_misses);
        ss << std::format("        \"accuracy_pct\": {:.2f}\n", h.branch_accuracy);
        ss << "      }\n";
        ss << "    }" << (i + 1 < harts.size() ? "," : "") << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";
    return ss.str();
}

auto MachineStatsSnapshot::to_string() const -> std::string {
    std::ostringstream ss;
    ss << "=== SimRV Simulation Statistics ===\n";
    ss << std::format("Total Instructions: {}\n", total_retired_instructions);
    ss << std::format("Total Cycles:       {}\n", total_cycles);
    ss << std::format("Aggregate IPC:      {:.4f}\n", aggregate_ipc);
    ss << std::format("Coherence Hub:      {} acq, {} probe, {} rel, {} inv\n", coherence_acq,
                      coherence_probe, coherence_release, coherence_invalidation);
    ss << std::format("Bus Contention:     {} delayed, {} penalty cycles\n", bus_delayed_requests,
                      bus_contention_penalty);
    for (const auto& h : harts) {
        ss << std::format("\n--- Hart {} ---\n", h.hart_id);
        ss << std::format("  Instructions: {} | Cycles: {} | IPC: {:.4f}\n", h.retired_instructions,
                          h.cycles, h.ipc);
        ss << std::format("  I-Cache:      {} hits, {} misses ({:.2f}%)\n", h.icache_hits,
                          h.icache_misses, h.icache_hit_rate);
        ss << std::format("  D-Cache:      {} hits, {} misses ({:.2f}%)\n", h.dcache_hits,
                          h.dcache_misses, h.dcache_hit_rate);
        ss << std::format("  Branch Pred:  {} branches, {} hits ({:.2f}%)\n", h.branches,
                          h.branch_hits, h.branch_accuracy);
    }
    return ss.str();
}

}  // namespace simrv::core

