/**
 * @file Tracer.cpp
 * @brief Architectural and simulation tracing facility implementation.
 */
#include "simrv/core/Tracer.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <fstream>
#include <ostream>
#include <print>
#include <ranges>
#include <string>
#include <string_view>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/device/Console.hpp"
#include "simrv/device/Disk.hpp"
#include "simrv/device/Virtio.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/util/FormatUtil.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

using namespace simrv::isa;

constexpr auto D_TRACE_HEX_WIDTH = static_cast<int>(kXLenHexDigits);
constexpr Counter D_TRACEPC_INTERVAL = 1000;

Tracer::Tracer(Machine& machine) : machine_(machine) {}

void Tracer::init_trace(bool trace_enabled) {
    fp_trace.close();
    if (trace_enabled) {
        fp_trace.clear();
        fp_trace.open("trace/trace.txt");
    }
}

void Tracer::init_trap_log(bool traplog_mode, const std::string& fn_traplog) {
    fp_traplog.close();
    if (traplog_mode) {
        fp_traplog.clear();
        fp_traplog.open(fn_traplog, std::ios::out | std::ios::trunc);
    }
}

void Tracer::init_dlog(bool dlog_mode) {
    fp_dlog.close();
    if (dlog_mode) {
        fp_dlog.clear();
        fp_dlog.open("init_virtio.txt");
    }
}

void Tracer::dump_init_artifacts() {
    auto* cpu = &machine_.cpu;
    auto* ram = machine_.mmem;
    auto* console = machine_.console.get();
    auto* disk = machine_.disk.get();
    auto* sector = disk->sector;

    {
        std::ofstream out("trace/init_mem.txt");
        for (Address i = 0; i < simrv::memory::kDramSize; ++i) {
            out << std::hex << static_cast<unsigned>(std::to_integer<uint8_t>(ram[i])) << '\n';
        }
        simrv::log::info("file init_mem.txt was generated after {} cycle", cpu->clint_mmio.mtime);
    }

    if (sector != nullptr) {
        std::ofstream out("trace/init_dsk.txt");
        for (Word i = 0; i < simrv::virtio::kDiskSize; ++i) {
            out << std::hex << static_cast<unsigned>(std::to_integer<uint8_t>(sector[i])) << '\n';
        }
        simrv::log::info("file init_dsk.txt was generated after {} cycle", cpu->clint_mmio.mtime);
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
        std::println(out, "p.fregs.mem[{}]={}'h{:016x};", i, simrv::xlen::kXLenBits,
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

    write_32("mmu.console.QueueSel       ", console->QueueSel);
    write_32("mmu.console.QueueNum       ", console->QueueNum);
    for (Word i = 0; i < simrv::virtio::kConsoleMaxQueueNum; ++i) {
        std::print(out, "mmu.console.Queue[{}*9+0] =32'h{:08x};\n", i, console->Queue[i].Ready);
        std::print(out, "mmu.console.Queue[{}*9+1] =32'h{:08x};\n", i, console->Queue[i].Notify);
        std::print(out, "mmu.console.Queue[{}*9+2] =32'h{:08x};\n", i, console->Queue[i].DescLow);
        std::print(out, "mmu.console.Queue[{}*9+3] =32'h{:08x};\n", i, console->Queue[i].DescHigh);
        std::print(out, "mmu.console.Queue[{}*9+4] =32'h{:08x};\n", i, console->Queue[i].AvailLow);
        std::print(out, "mmu.console.Queue[{}*9+5] =32'h{:08x};\n", i, console->Queue[i].AvailHigh);
        std::print(out, "mmu.console.Queue[{}*9+6] =32'h{:08x};\n", i, console->Queue[i].UsedLow);
        std::print(out, "mmu.console.Queue[{}*9+7] =32'h{:08x};\n", i, console->Queue[i].UsedHigh);
        std::print(out, "mmu.console.Queue[{}*9+8] =32'h{:08x};\n", i,
                   console->Queue[i].last_avail_idx);
    }
    write_32("mmu.console.InterruptStatus", console->InterruptStatus);
    write_32("mmu.console.Status         ", console->Status);

    write_32("mmu.disk.QueueSel       ", disk->QueueSel);
    write_32("mmu.disk.QueueNum       ", disk->QueueNum);
    for (Word i = 0; i < simrv::virtio::kDiskMaxQueueNum; ++i) {
        std::print(out, "mmu.disk.Queue[{}*9+0] =32'h{:08x};\n", i, disk->Queue[i].Ready);
        std::print(out, "mmu.disk.Queue[{}*9+1] =32'h{:08x};\n", i, disk->Queue[i].Notify);
        std::print(out, "mmu.disk.Queue[{}*9+2] =32'h{:08x};\n", i, disk->Queue[i].DescLow);
        std::print(out, "mmu.disk.Queue[{}*9+3] =32'h{:08x};\n", i, disk->Queue[i].DescHigh);
        std::print(out, "mmu.disk.Queue[{}*9+4] =32'h{:08x};\n", i, disk->Queue[i].AvailLow);
        std::print(out, "mmu.disk.Queue[{}*9+5] =32'h{:08x};\n", i, disk->Queue[i].AvailHigh);
        std::print(out, "mmu.disk.Queue[{}*9+6] =32'h{:08x};\n", i, disk->Queue[i].UsedLow);
        std::print(out, "mmu.disk.Queue[{}*9+7] =32'h{:08x};\n", i, disk->Queue[i].UsedHigh);
        std::print(out, "mmu.disk.Queue[{}*9+8] =32'h{:08x};\n", i, disk->Queue[i].last_avail_idx);
    }
    write_32("mmu.disk.InterruptStatus", disk->InterruptStatus);
    write_32("mmu.disk.Status         ", disk->Status);

    simrv::log::info("file init_reg.txt was generated after {} cycle", cpu->clint_mmio.mtime);
}

void Tracer::write_instruction_mix_report() {
    std::ofstream out("trace/instmix.txt");
    if (!out.is_open()) {
        simrv::log::error("cannot open instmix.txt");
        return;
    }
    std::println(out, "INSTRUCTION MIX");
    uint64_t total = 0;
    for (auto const [i, count] : std::views::enumerate(machine_.cpu.e_instmix)) {
        std::println(out, "{} : {:10}",
                     simrv::pipeline::OPERATION_NAME.at(static_cast<std::size_t>(i)), count);
        total += count;
    }
    std::println(out, "TOTAL      : {:10}", total);
    simrv::log::info("file instmix.txt was generated after {} cycle",
                     machine_.cpu.clint_mmio.mtime);
}

void Tracer::print_summary() {
    using simrv::util::format_scaled;
    using simrv::util::format_with_commas;
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(now - machine_.s_start_time).count();
    const auto etime = static_cast<Counter>(elapsed == 0 ? 1 : elapsed);

    const auto mcycle = machine_.cpu.clint_mmio.mcycle;
    const auto icount = machine_.cpu.e_icount;
    const auto ccount = machine_.cpu.e_ccount;

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
    if (machine_.s_cycle_accurate) {
        simrv::log::info("Simulation Mode          :         Cycle-Accurate");
    } else {
        simrv::log::info("Simulation Mode          :         Functional-Only");
    }
    simrv::log::info("Elapsed cycles (clocks)  : {:>12}  ({})", format_scaled(mcycle),
                     format_with_commas(mcycle));
    simrv::log::info("Executed instructions    : {:>12}  ({})", format_scaled(icount),
                     format_with_commas(icount));
    simrv::log::info("Fetched compressed insns : {:>12}  ({})  [{:.1f}%]", format_scaled(ccount),
                     format_with_commas(ccount), comp_ratio);
    simrv::log::info("Cycles Per Instr (CPI)   : {:>12.3f}", cpi);
    simrv::log::info("Instrs Per Cycle (IPC)   : {:>12.3f}", ipc);

    if (machine_.s_cycle_accurate) {
        const auto& ps = machine_.cpu.pipeline_sim;
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

    if (machine_.s_use_mix) {
        write_instruction_mix_report();
    }
}

void Tracer::emit_periodic_pc_trace(Counter mtime, Register cpc) {
    if ((mtime % D_TRACEPC_INTERVAL) == 0) {
        if (!tracepc_opened_) {
            tracepc_opened_ = true;
            fp_tracepc_.open("trace/tracepc.txt");
            simrv::log::info("generate trace file: tracepc.txt\n");
        }
        std::println(fp_tracepc_, "{:08} {:0{}x}", static_cast<int>(mtime / D_TRACEPC_INTERVAL),
                     cpc, D_TRACE_HEX_WIDTH);
        fp_tracepc_.flush();
    }
}

void Tracer::emit_branch_prediction_trace(Counter mtime, Register cpc, Register jmp_pc,
                                          Opcode r_opcode, bool r_tkn) {
    if (!bpred_opened_) {
        bpred_opened_ = true;
        fp_bpred_.open("trace/bpred.txt");
        simrv::log::info("generate trace file: bpred.txt\n");
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
    fp_bpred_.flush();
}

void Tracer::write_trace_snapshot() {
    if (!fp_trace.is_open()) {
        return;
    }
    const auto& cpu = machine_.cpu;
    const auto& st = cpu.state();

    std::print(fp_trace, "{:08} {:0{}x} {:08x}", cpu.clint_mmio.mtime, cpu.pipeline_context.cpc,
               D_TRACE_HEX_WIDTH, static_cast<uint32_t>(cpu.pipeline_context.ir));
    std::println(fp_trace, "");

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++) {
            std::print(fp_trace, "{:0{}x}{}", st.regs.read(static_cast<RegId>((i * 8) + j)),
                       D_TRACE_HEX_WIDTH, (j != 7 ? " " : "\n"));
        }
    }

    if (!machine_.s_appmode) {
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