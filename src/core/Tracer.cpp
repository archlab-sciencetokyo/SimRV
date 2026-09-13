/**
 * @file Tracer.cpp
 * @brief Architectural and simulation tracing facility implementation.
 */
#include "simrv/core/Tracer.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <ostream>
#include <print>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/device/pci/VirtioPciBlock.hpp"
#include "simrv/device/pci/VirtioPciConsole.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/pipeline/OperationInfo.hpp"
#include "simrv/pipeline/PipelineConfig.hpp"
#include "simrv/util/FormatUtil.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

using namespace simrv::isa;

namespace {

constexpr auto D_TRACE_HEX_WIDTH = static_cast<int>(kXLenHexDigits);
constexpr Counter D_TRACEPC_INTERVAL = 1000;

auto categorize_operation(isa::OperationId op) noexcept -> std::string_view {
    const auto op_info = pipeline::operation::info(op);
    if (op_info.control == pipeline::operation::ControlFlowKind::Branch) return "Branch";
    if (op_info.control == pipeline::operation::ControlFlowKind::Jump) return "Jump";
    if (op_info.memory == pipeline::operation::MemoryAccessKind::Load) return "Load";
    if (op_info.memory == pipeline::operation::MemoryAccessKind::Store) return "Store";
    if (op_info.memory == pipeline::operation::MemoryAccessKind::Atomic) return "Atomic";
    if (op_info.execution_class == pipeline::operation::ExecutionClass::Multiply ||
        op_info.execution_class == pipeline::operation::ExecutionClass::DivideOrRemainder)
        return "Mul/Div";
    if (op_info.execution_class == pipeline::operation::ExecutionClass::FpAlu ||
        op_info.execution_class == pipeline::operation::ExecutionClass::FpDivideOrSqrt)
        return "Float";
    if (op_info.operands.rd == pipeline::operation::RegBank::Vector ||
        op_info.operands.rs1 == pipeline::operation::RegBank::Vector ||
        op_info.operands.rs2 == pipeline::operation::RegBank::Vector)
        return "Vector";
    if (op_info.side_effects != pipeline::operation::SideEffectFlags::None) return "System/CSR";
    return "Integer/ALU";
}

}  // namespace

Tracer::Tracer(Machine& machine) : machine_(machine) {}

Tracer::~Tracer() { flush_all(); }

void Tracer::init_trace(bool trace_enabled) {
    fp_trace.close();
    if (trace_enabled) {
        std::error_code ec;
        std::filesystem::create_directories("trace", ec);
        fp_trace.clear();
        fp_trace.open("trace/trace.txt");
    }
}

void Tracer::init_trap_log(bool traplog_mode, const std::string& fn_traplog) {
    fp_traplog.close();
    if (traplog_mode) {
        std::error_code ec;
        const std::filesystem::path path(fn_traplog);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path(), ec);
        }
        fp_traplog.clear();
        fp_traplog.open(fn_traplog, std::ios::out | std::ios::trunc);
    }
}

void Tracer::init_dlog(bool dlog_mode) {
    fp_dlog.close();
    if (dlog_mode) {
        std::error_code ec;
        std::filesystem::create_directories("trace", ec);
        fp_dlog.clear();
        fp_dlog.open("trace/dlog.txt", std::ios::out | std::ios::trunc);
    }
}

auto Tracer::is_trace_enabled() const noexcept -> bool { return fp_trace.is_open(); }
auto Tracer::is_trap_log_enabled() const noexcept -> bool { return fp_traplog.is_open(); }
auto Tracer::is_dlog_enabled() const noexcept -> bool { return fp_dlog.is_open(); }

void Tracer::flush_all() {
    std::lock_guard lock(mutex_);
    if (fp_trace.is_open()) fp_trace.flush();
    if (fp_dlog.is_open()) fp_dlog.flush();
    if (fp_traplog.is_open()) fp_traplog.flush();
    if (fp_tracepc_.is_open()) fp_tracepc_.flush();
    if (fp_bpred_.is_open()) fp_bpred_.flush();
}

void Tracer::log_mmio(std::string_view dev_name, Address addr, uint32_t size, Word data,
                      bool is_write) {
    if (!fp_dlog.is_open()) return;
    const auto mtime = machine_.primary_hart().clint_mmio.mtime.load(std::memory_order_relaxed);
    std::lock_guard lock(mutex_);
    std::println(fp_dlog, "[mtime={:12}] [{:<16}] {:5} addr=0x{:0{}x} size={:2} data=0x{:0{}x}",
                 mtime, dev_name, is_write ? "WRITE" : "READ", addr, kXLenHexDigits, size, data,
                 size * 2);
}

void Tracer::log_trap(Counter mtime, TrapCause cause, Address trap_pc, PrivilegeLevel priv,
                      const ArchState& state, CSRValue tval) {
    if (!fp_traplog.is_open()) return;
    constexpr int kLogHexWidth = static_cast<int>(kXLenHexDigits);
    std::lock_guard lock(mutex_);
    std::println(
        fp_traplog,
        "TRAP mtime={} cause={:0{}x} ({}) pc={:0{}x} priv={} ra={:0{}x} sp={:0{}x} tp={:0{}x} "
        "a0={:0{}x} a1={:0{}x} mtvec={:0{}x} stvec={:0{}x} mepc={:0{}x} sepc={:0{}x} satp={:0{}x} "
        "tval={:0{}x}",
        mtime, static_cast<uint64_t>(cause), kLogHexWidth, trap_cause_name(cause),
        static_cast<uint64_t>(trap_pc), kLogHexWidth, static_cast<unsigned>(priv),
        static_cast<uint64_t>(state.regs.read(RegId::Ra)), kLogHexWidth,
        static_cast<uint64_t>(state.regs.read(RegId::Sp)), kLogHexWidth,
        static_cast<uint64_t>(state.regs.read(RegId::Tp)), kLogHexWidth,
        static_cast<uint64_t>(state.regs.read(RegId::A0)), kLogHexWidth,
        static_cast<uint64_t>(state.regs.read(RegId::A1)), kLogHexWidth,
        static_cast<uint64_t>(state.mtvec), kLogHexWidth, static_cast<uint64_t>(state.stvec),
        kLogHexWidth, static_cast<uint64_t>(state.mepc), kLogHexWidth,
        static_cast<uint64_t>(state.sepc), kLogHexWidth, static_cast<uint64_t>(state.satp),
        kLogHexWidth, static_cast<uint64_t>(tval), kLogHexWidth);
}

void Tracer::log_sbi(Counter mtime, unsigned cause, Word ext_id, Word func_id, Word a0, Word a1,
                     Address pc) {
    if (!fp_traplog.is_open()) return;
    constexpr int kLogHexWidth = static_cast<int>(kXLenHexDigits);
    std::lock_guard lock(mutex_);
    std::println(fp_traplog,
                 "__ SBI ecall mtime={} cause={} ext={:0{}x} fid={:0{}x} a0={:0{}x} a1={:0{}x} "
                 "pc={:0{}x}",
                 mtime, cause, static_cast<uint64_t>(ext_id), kLogHexWidth,
                 static_cast<uint64_t>(func_id), kLogHexWidth, static_cast<uint64_t>(a0),
                 kLogHexWidth, static_cast<uint64_t>(a1), kLogHexWidth, static_cast<uint64_t>(pc),
                 kLogHexWidth);
}

void Tracer::dump_init_artifacts() {
    auto* cpu = &machine_.primary_hart();
    const auto ram = machine_.ram_view();
    std::error_code ec;
    std::filesystem::create_directories("trace", ec);

    {
        std::ofstream out("trace/init_mem.txt");
        const auto dram_base = machine_.memory_geometry().dram_base;
        const auto dram_size = static_cast<uint64_t>(machine_.memory_geometry().dram_size);
        constexpr uint64_t kBlockSize = 16;
        bool skipping = false;

        for (uint64_t offset = 0; offset < dram_size; offset += kBlockSize) {
            const uint64_t current_len = std::min<uint64_t>(kBlockSize, dram_size - offset);
            bool all_zero = true;
            for (uint64_t b = 0; b < current_len; ++b) {
                if (std::to_integer<uint8_t>(*ram.unchecked_ptr(dram_base + offset + b)) != 0) {
                    all_zero = false;
                    break;
                }
            }
            if (all_zero) {
                if (!skipping) {
                    std::println(out, "*");
                    skipping = true;
                }
                continue;
            }
            skipping = false;

            std::print(out, "{:08x}: ", offset);
            for (uint64_t b = 0; b < current_len; ++b) {
                const uint8_t val =
                    std::to_integer<uint8_t>(*ram.unchecked_ptr(dram_base + offset + b));
                std::print(out, "{:02x}{}", val, (b == 7 ? "  " : " "));
            }
            for (uint64_t b = current_len; b < kBlockSize; ++b) {
                std::print(out, "   {}", (b == 7 ? " " : ""));
            }
            std::print(out, " |");
            for (uint64_t b = 0; b < current_len; ++b) {
                const char ch = static_cast<char>(
                    std::to_integer<uint8_t>(*ram.unchecked_ptr(dram_base + offset + b)));
                out.put((ch >= 32 && ch <= 126) ? ch : '.');
            }
            std::println(out, "|");
        }
        simrv::log::info("file trace/init_mem.txt was generated after {} cycle(s)",
                         static_cast<Counter>(cpu->clint_mmio.mtime.load()));
    }

    std::ofstream out("trace/init_reg.txt");
    auto write_xlen = [&out](std::string_view lhs, Word value) -> void {
        std::println(out, "{}={}'h{:0{}x};", lhs, simrv::xlen::kXLenBits, value, kXLenHexDigits);
    };
    auto write_32 = [&out](std::string_view lhs, Word value) -> void {
        std::println(out, "{}=32'h{:08x};", lhs, value);
    };
    auto write_64 = [&out](std::string_view lhs, Counter value) -> void {
        std::println(out, "{}=64'h{:016x};", lhs, value);
    };

    write_xlen("p.pc", cpu->state().pc);
    for (uint8_t i = 1; i < 32; ++i) {
        std::println(out, "p.regs.mem[{}]={}'h{:0{}x};", i, simrv::xlen::kXLenBits,
                     cpu->state().regs.read(static_cast<RegId>(i)), kXLenHexDigits);
    }
    for (uint8_t i = 0; i < 32; ++i) {
        std::println(out, "p.fregs.mem[{}]={}'h{:016x};", i, 64,
                     cpu->state().regs.read_fp(static_cast<RegId>(i)));
    }
    write_xlen("p.fcsr        ", cpu->state().fcsr);
    write_xlen("p.mstatus     ", cpu->state().mstatus);
    write_xlen("p.mtvec       ", cpu->state().mtvec);
    write_xlen("p.mscratch    ", cpu->state().mscratch);
    write_xlen("p.mepc        ", cpu->state().mepc);
    write_xlen("p.mcause      ", cpu->state().mcause);
    write_xlen("p.mtval       ", cpu->state().mtval);
    write_xlen("p.mhartid     ", cpu->state().mhartid);
    write_xlen("p.misa        ", cpu->state().misa);
    write_xlen("p.mie         ", cpu->state().mie);
    write_xlen("p.mip         ", cpu->state().mip);
    write_xlen("p.medeleg     ", cpu->state().medeleg);
    write_xlen("p.mideleg     ", cpu->state().mideleg);
    write_xlen("p.mcounteren  ", cpu->state().mcounteren);
    write_xlen("p.stvec       ", cpu->state().stvec);
    write_xlen("p.sscratch    ", cpu->state().sscratch);
    write_xlen("p.sepc        ", cpu->state().sepc);
    write_xlen("p.scause      ", cpu->state().scause);
    write_xlen("p.stval       ", cpu->state().stval);
    write_xlen("p.satp        ", cpu->state().satp);
    write_xlen("p.scounteren  ", cpu->state().scounteren);
    write_xlen("p.priv        ", std::to_underlying(cpu->state().priv));

    write_64("p.mtime       ", cpu->clint_mmio.mtime);
    write_64("p.mtimecmp    ", cpu->clint_mmio.mtimecmp);

    write_xlen("p.load_res    ", cpu->state().load_res);
    std::println(out, "p.reserved    = 1'h{:x};", cpu->state().reserved);
    write_xlen("p.pending_exception   ",
               cpu->pipeline_context.pending_exception
                   ? std::to_underlying(*cpu->pipeline_context.pending_exception)
                   : simrv::xlen::kWordAllOnes);
    write_xlen("p.pending_tval", cpu->pipeline_context.pending_tval);

    const size_t kNumSets = simrv::core::Tlb::kNumSets;
    for (Word i = 0; i < simrv::memory::kTlbSize; ++i) {
        const auto& entry = cpu->tlb.inst_r.at(i % kNumSets).at(i / kNumSets);
        std::println(out, "mmu.TLB_inst_r.r_valid[{}] ={};", i, static_cast<int>(entry.valid));
        std::println(out, "mmu.TLB_inst_r.mem[{}][39:22] =18'h{:05x};", i, entry.v_addr >> 14);
        std::println(out, "mmu.TLB_inst_r.mem[{}][21:0] =22'h{:06x};", i, entry.p_addr >> 10);
    }
    for (Word i = 0; i < simrv::memory::kTlbSize; ++i) {
        const auto& entry = cpu->tlb.data_r.at(i % kNumSets).at(i / kNumSets);
        std::println(out, "mmu.TLB_data_r.r_valid[{}] ={};", i, static_cast<int>(entry.valid));
        std::println(out, "mmu.TLB_data_r.mem[{}][39:22] =18'h{:05x};", i, entry.v_addr >> 14);
        std::println(out, "mmu.TLB_data_r.mem[{}][21:0] =22'h{:06x};", i, entry.p_addr >> 10);
    }
    for (Word i = 0; i < simrv::memory::kTlbSize; ++i) {
        const auto& entry = cpu->tlb.data_w.at(i % kNumSets).at(i / kNumSets);
        std::println(out, "mmu.TLB_data_w.r_valid[{}] ={};", i, static_cast<int>(entry.valid));
        std::println(out, "mmu.TLB_data_w.mem[{}][39:22] =18'h{:05x};", i, entry.v_addr >> 14);
        std::println(out, "mmu.TLB_data_w.mem[{}][21:0] =22'h{:06x};", i, entry.p_addr >> 10);
    }

    const auto platform = machine_.platform_status();
    write_32("platform.virtio_console.status", platform.console_status);
    write_32("platform.virtio_disk.status   ", platform.disk_status);
    write_32("platform.virtio_disk.isr      ", platform.disk_isr);
    write_64("platform.virtio_disk.capacity ", platform.disk_capacity_sectors);

    simrv::log::info("file trace/init_reg.txt was generated after {} cycle(s)",
                     static_cast<Counter>(cpu->clint_mmio.mtime.load()));
}

void Tracer::write_instruction_mix_report() {
    std::error_code ec;
    std::filesystem::create_directories("trace", ec);
    std::ofstream out("trace/instmix.txt");
    if (!out.is_open()) {
        simrv::log::error("cannot open trace/instmix.txt");
        return;
    }

    struct Entry {
        std::string_view name;
        std::string_view category;
        uint64_t count;
    };

    std::vector<Entry> entries;
    std::map<std::string_view, uint64_t> category_totals;
    uint64_t total = 0;

    for (auto const [i, count] : std::views::enumerate(machine_.primary_hart().e_instmix)) {
        if (count == 0) continue;
        const auto op = static_cast<simrv::isa::OperationId>(i);
        const auto name = simrv::pipeline::operation_name(op);
        const auto category = categorize_operation(op);
        entries.push_back({name, category, count});
        category_totals[category] += count;
        total += count;
    }

    std::ranges::sort(entries, [](const auto& a, const auto& b) {
        if (a.count != b.count) return a.count > b.count;
        return a.name < b.name;
    });

    std::println(
        out, "================================================================================");
    std::println(out, "                         INSTRUCTION MIX REPORT");
    std::println(
        out, "================================================================================");
    std::println(out,
                 " Rank  Instruction             Category              Count      Share  Cumul");
    std::println(
        out, "--------------------------------------------------------------------------------");

    uint64_t cumulative = 0;
    for (size_t rank = 0; rank < entries.size(); ++rank) {
        const auto& e = entries[rank];
        cumulative += e.count;
        const double share =
            total == 0 ? 0.0 : (static_cast<double>(e.count) * 100.0) / static_cast<double>(total);
        const double cumul_pct =
            total == 0 ? 0.0
                       : (static_cast<double>(cumulative) * 100.0) / static_cast<double>(total);
        std::println(out, "{:>5}  {:<22}  {:<16}  {:>12}  {:>5.2f}%  {:>5.2f}%", rank + 1, e.name,
                     e.category, simrv::util::format_with_commas(e.count), share, cumul_pct);
    }

    std::println(
        out, "--------------------------------------------------------------------------------");
    std::println(out, " Total Instructions Retired: {:>16}",
                 simrv::util::format_with_commas(total));
    std::println(
        out, "================================================================================\n");

    std::println(out, "--- Category Summary ---");
    std::vector<std::pair<std::string_view, uint64_t>> cat_sorted(category_totals.begin(),
                                                                  category_totals.end());
    std::ranges::sort(cat_sorted, [](const auto& a, const auto& b) { return a.second > b.second; });
    for (const auto& [cat, cat_cnt] : cat_sorted) {
        const double cat_share =
            total == 0 ? 0.0 : (static_cast<double>(cat_cnt) * 100.0) / static_cast<double>(total);
        std::println(out, "  {:<18} : {:>12}  ({:>5.2f}%)", cat,
                     simrv::util::format_with_commas(cat_cnt), cat_share);
    }

    simrv::log::info("file trace/instmix.txt was generated after {} cycle(s)",
                     static_cast<Counter>(machine_.primary_hart().clint_mmio.mtime.load()));
}

void Tracer::print_summary() {
    using simrv::util::format_scaled;
    using simrv::util::format_with_commas;
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(now - machine_.start_time()).count();
    const auto etime = static_cast<Counter>(elapsed == 0 ? 1 : elapsed);

    const auto mcycle = machine_.primary_hart().clint_mmio.mcycle;
    Counter icount = 0;
    Counter ccount = 0;
    for (size_t hart = 0; hart < machine_.num_harts(); ++hart) {
        icount += machine_.hart(hart).e_icount;
        ccount += machine_.hart(hart).e_ccount;
    }

    const double cpi =
        icount == 0 ? 0.0 : static_cast<double>(mcycle) / static_cast<double>(icount);
    const double ipc =
        mcycle == 0 ? 0.0 : static_cast<double>(icount) / static_cast<double>(mcycle);
    const double comp_ratio =
        icount == 0 ? 0.0 : (static_cast<double>(ccount) * 100.0) / static_cast<double>(icount);
    const double etime_sec = static_cast<double>(etime) / 1000000.0;

    simrv::log::info("--------------------------------------------------");
    simrv::log::info("                Execution Summary                 ");
    simrv::log::info("--------------------------------------------------");
    simrv::log::info("Simulation Engine        : {:>22}",
                     machine_.runtime_profile.execution_name());
    simrv::log::info("Termination reason       : {:>22}",
                     Machine::stop_reason_name(machine_.stop_reason()));
    simrv::log::info("Process exit status      : {:>22}", machine_.exit_code.load());
    simrv::log::info("Target / harts           : {:>13} / {:<3}",
                     std::format("RV{}", simrv::xlen::kXLenBits), machine_.num_harts());
    if (machine_.runtime_profile.is_cycle_mode()) {
        simrv::log::info("CA pipeline              : {:>22}",
                         pipeline::pipeline_type_name(machine_.execution_config().pipeline_type));
    }
    simrv::log::info("Elapsed cycles (clocks)  : {:>12}  ({})", format_scaled(mcycle),
                     format_with_commas(mcycle));
    simrv::log::info("Executed instructions    : {:>12}  ({})", format_scaled(icount),
                     format_with_commas(icount));
    simrv::log::info("Final architectural PC   :   0x{:016x}", machine_.primary_hart().state().pc);
    simrv::log::info("Fetched compressed insns : {:>12}  ({})  [{:.1f}%]", format_scaled(ccount),
                     format_with_commas(ccount), comp_ratio);
    simrv::log::info("Cycles Per Instr (CPI)   : {:>12.3f}", cpi);
    simrv::log::info("Instrs Per Cycle (IPC)   : {:>12.3f}", ipc);

    if (machine_.num_harts() > 1) {
        for (size_t i = 0; i < machine_.num_harts(); ++i) {
            const auto& h = machine_.hart(i);
            const char* priv_str = (h.state().priv == PrivilegeLevel::Machine)      ? "M"
                                   : (h.state().priv == PrivilegeLevel::Supervisor) ? "S"
                                                                                    : "U";
            simrv::log::info("Hart {:<2} [mode {:>1}, PC 0x{:016x}] Executed Insns : {:>12}  ({})",
                             i, priv_str, h.state().pc, format_scaled(h.e_icount),
                             format_with_commas(h.e_icount));
        }
    }

    if (machine_.runtime_profile.is_cycle_mode()) {
        const auto& ps = machine_.primary_hart().pipeline_sim;
        double stall_pct = mcycle == 0 ? 0.0
                                       : (static_cast<double>(ps.stall_cycles()) * 100.0) /
                                             static_cast<double>(mcycle);
        simrv::log::info("Total Stall/Bubble Cycles: {:>12}  [{:.1f}%]",
                         format_with_commas(ps.stall_cycles()), stall_pct);
        simrv::log::info(" - Data RAW Stalls       : {:>12}",
                         format_with_commas(ps.data_hazard_stalls()));
        simrv::log::info(" - Control Bubbles       : {:>12}",
                         format_with_commas(ps.control_hazard_bubbles()));
        simrv::log::info(" - ICache Miss Stalls    : {:>12}",
                         format_with_commas(ps.icache_stalls()));
        simrv::log::info(" - DCache Miss Stalls    : {:>12}",
                         format_with_commas(ps.dcache_stalls()));
        simrv::log::info(" - Page-walk Stalls      : {:>12}", format_with_commas(ps.tlb_stalls()));
        simrv::log::info("L1I hits / misses        : {:>12} / {}",
                         format_with_commas(machine_.primary_hart().icache.hit_count()),
                         format_with_commas(machine_.primary_hart().icache.miss_count()));
        simrv::log::info("L1D hits / misses        : {:>12} / {}",
                         format_with_commas(machine_.primary_hart().dcache.hit_count()),
                         format_with_commas(machine_.primary_hart().dcache.miss_count()));
        const auto& bus = machine_.memory().system_bus();
        simrv::log::info("Bus reads / writes       : {:>12} / {}",
                         format_with_commas(bus.read_count()),
                         format_with_commas(bus.write_count()));
        simrv::log::info("Bus pending req / resp   : {:>12} / {}",
                         format_with_commas(bus.pending_requests()),
                         format_with_commas(bus.pending_responses()));
    }

    simrv::log::info("Elapsed time (real)      : {:>12.3f} sec", etime_sec);

    const auto kips = icount * 1000UL / etime;
    const double mips = static_cast<double>(icount) / static_cast<double>(etime);
    if (mips >= 1.0) {
        simrv::log::info("Simulation speed         : {:>12.2f} MIPS ({}/s)", mips,
                         format_with_commas(kips * 1000));
    } else {
        simrv::log::info("Simulation speed         : {:>12} KIPS", format_with_commas(kips));
    }
    simrv::log::info("--------------------------------------------------");

    if (machine_.instruction_mix_enabled()) {
        write_instruction_mix_report();
    }
    flush_all();
}

void Tracer::emit_periodic_pc_trace(Counter mtime, Register cpc) {
    if ((mtime % D_TRACEPC_INTERVAL) == 0) {
        std::lock_guard lock(mutex_);
        if (!tracepc_opened_) {
            tracepc_opened_ = true;
            std::error_code ec;
            std::filesystem::create_directories("trace", ec);
            fp_tracepc_.open("trace/tracepc.txt");
            simrv::log::info("generate trace file: trace/tracepc.txt");
        }
        std::println(fp_tracepc_, "{:08} {:0{}x}", static_cast<int>(mtime / D_TRACEPC_INTERVAL),
                     cpc, D_TRACE_HEX_WIDTH);
    }
}

void Tracer::emit_branch_prediction_trace(Counter mtime, Register cpc, Register jmp_pc,
                                          Opcode r_opcode, bool r_tkn) {
    std::lock_guard lock(mutex_);
    if (!bpred_opened_) {
        bpred_opened_ = true;
        std::error_code ec;
        std::filesystem::create_directories("trace", ec);
        fp_bpred_.open("trace/bpred.txt");
        simrv::log::info("generate trace file: trace/bpred.txt");
    }

    const auto opcode = r_opcode;
    const int ir_jb = static_cast<int>((opcode == Opcode::Jal) || (opcode == Opcode::Jalr) ||
                                       (opcode == Opcode::Branch));
    const int ir_jump = (opcode == Opcode::Jal) ? 2 : (opcode == Opcode::Jalr) ? 3 : 0;
    const int ir_branch = static_cast<int>(opcode == Opcode::Branch);

    const Word targ = ((ir_jump | ir_branch) != 0) ? jmp_pc : 0;
    std::println(fp_bpred_, "{:08} {:0{}x} {:0{}x} {} {} {} {}", static_cast<int>(mtime), cpc,
                 D_TRACE_HEX_WIDTH, targ, D_TRACE_HEX_WIDTH, ir_jb, static_cast<int>(r_tkn),
                 ir_jump, ir_branch);
}

void Tracer::write_trace_snapshot() {
    if (!fp_trace.is_open()) {
        return;
    }
    std::lock_guard lock(mutex_);
    const auto& cpu = machine_.primary_hart();
    const auto& st = cpu.state();

    std::print(fp_trace, "{:08} {:0{}x} {:08x}", static_cast<Counter>(cpu.clint_mmio.mtime.load()),
               cpu.pipeline_context.cpc.raw(), D_TRACE_HEX_WIDTH,
               static_cast<uint32_t>(cpu.pipeline_context.ir));
    std::println(fp_trace, "");

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++) {
            std::print(fp_trace, "{:0{}x}{}", st.regs.read(static_cast<RegId>((i * 8) + j)),
                       D_TRACE_HEX_WIDTH, (j != 7 ? " " : "\n"));
        }
    }

    if (!machine_.appmode_enabled()) {
        std::println(fp_trace, "{:0{}x} {:0{}x} {:0{}x} {:0{}x} {:0{}x} {:0{}x} {:0{}x} {:0{}x}",
                     st.mstatus, D_TRACE_HEX_WIDTH, st.mtvec, D_TRACE_HEX_WIDTH, st.mscratch,
                     D_TRACE_HEX_WIDTH, st.mepc, D_TRACE_HEX_WIDTH, st.mcause, D_TRACE_HEX_WIDTH,
                     st.mtval, D_TRACE_HEX_WIDTH, st.mhartid, D_TRACE_HEX_WIDTH, st.misa,
                     D_TRACE_HEX_WIDTH);

        std::print(fp_trace, "{:0{}x} {:0{}x} {:0{}x} {:0{}x} {:0{}x} ", st.mie, D_TRACE_HEX_WIDTH,
                   st.mip, D_TRACE_HEX_WIDTH, st.medeleg, D_TRACE_HEX_WIDTH, st.mideleg,
                   D_TRACE_HEX_WIDTH, st.mcounteren, D_TRACE_HEX_WIDTH);

        std::println(fp_trace, "{:0{}x} {:0{}x} {:0{}x}", st.stvec, D_TRACE_HEX_WIDTH, st.sscratch,
                     D_TRACE_HEX_WIDTH, st.sepc, D_TRACE_HEX_WIDTH);

        std::print(fp_trace, "{:0{}x} {:0{}x} {:0{}x} {:0{}x} {:0{}x} ", st.scause,
                   D_TRACE_HEX_WIDTH, st.stval, D_TRACE_HEX_WIDTH, st.satp, D_TRACE_HEX_WIDTH,
                   st.scounteren, D_TRACE_HEX_WIDTH, st.load_res, D_TRACE_HEX_WIDTH);
        std::println(fp_trace, "{:0{}x} {:0{}x} {:0{}x}",
                     cpu.pipeline_context.pending_exception
                         ? std::to_underlying(*cpu.pipeline_context.pending_exception)
                         : simrv::xlen::kWordAllOnes,
                     D_TRACE_HEX_WIDTH, cpu.pipeline_context.pending_tval, D_TRACE_HEX_WIDTH,
                     std::to_underlying(st.priv), D_TRACE_HEX_WIDTH);

        for (int i = 0; i < 4; i++) {
            std::print(
                fp_trace, "{:0{}x} {:0{}x} ",
                cpu.tlb.inst_r.at(static_cast<std::size_t>(i)).at(0).v_addr, D_TRACE_HEX_WIDTH,
                cpu.tlb.inst_r.at(static_cast<std::size_t>(i)).at(0).p_addr, D_TRACE_HEX_WIDTH);
        }
        std::println(fp_trace, "");
        for (int i = 0; i < 4; i++) {
            std::print(
                fp_trace, "{:0{}x} {:0{}x} ",
                cpu.tlb.data_r.at(static_cast<std::size_t>(i)).at(0).v_addr, D_TRACE_HEX_WIDTH,
                cpu.tlb.data_r.at(static_cast<std::size_t>(i)).at(0).p_addr, D_TRACE_HEX_WIDTH);
        }
        std::println(fp_trace, "");
        for (int i = 0; i < 4; i++) {
            std::print(
                fp_trace, "{:0{}x} {:0{}x} ",
                cpu.tlb.data_w.at(static_cast<std::size_t>(i)).at(0).v_addr, D_TRACE_HEX_WIDTH,
                cpu.tlb.data_w.at(static_cast<std::size_t>(i)).at(0).p_addr, D_TRACE_HEX_WIDTH);
        }
        std::println(fp_trace, "");
    }
}

}  // namespace simrv::core
