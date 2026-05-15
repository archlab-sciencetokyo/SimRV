/**
 * @file Tracer.cpp
 * @brief Architectural and simulation tracing facility implementation.
 */
#include "simrv/core/Tracer.hpp"

#include <chrono>
#include <cstddef>
#include <fstream>
#include <ostream>
#include <print>
#include <string>
#include <string_view>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/decode/Decoder.hpp"
#include "simrv/device/Console.hpp"
#include "simrv/device/Disk.hpp"
#include "simrv/device/Virtio.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

constexpr auto D_TRACE_HEX_WIDTH = static_cast<int>(kXLenHexDigits);
constexpr Counter D_TRACEPC_INTERVAL = 1000;

Tracer::Tracer(Machine& machine) : machine_(machine) {}

void Tracer::init_trace(bool trace_enabled) {
    fp_trace.close();
    if (trace_enabled) {
        fp_trace.clear();
        fp_trace.open("trace.txt");
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
        std::ofstream out("init_mem.txt");
        for (Address i = 0; i < simrv::memory::kDramSize; ++i) {
            out << std::hex << static_cast<unsigned>(std::to_integer<uint8_t>(ram[i])) << '\n';
        }
        std::println("__ file init_mem.txt was generated after {} cycle", cpu->clint_mmio.mtime);
    }

    {
        std::ofstream out("init_dsk.txt");
        for (Word i = 0; i < simrv::virtio::kDiskSize; ++i) {
            out << std::hex << static_cast<unsigned>(std::to_integer<uint8_t>(sector[i])) << '\n';
        }
        std::println("__ file init_dsk.txt was generated after {} cycle", cpu->clint_mmio.mtime);
    }

    std::ofstream out("init_reg.txt");
    auto write_xlen = [&out](std::string_view lhs, Word value) {
        std::println(out, "{}={}'h{:0{}x};", lhs, kXLenBits, value, kXLenHexDigits);
    };
    auto write_32 = [&out](std::string_view lhs, Word value) {
        std::println(out, "{}=32'h{:08x};", lhs, value);
    };
    auto write_64 = [&out](std::string_view lhs, Counter value) {
        std::println(out, "{}=64'h{:016x};", lhs, value);
    };

    write_xlen("p.pc", cpu->state().pc);
    for (int i = 1; i < 32; ++i) {
        std::println(out, "p.regs.mem[{}]={}'h{:0{}x};", i, kXLenBits, cpu->state().regs.read(i),
                     kXLenHexDigits);
    }
    for (int i = 0; i < 32; ++i) {
        std::println(out, "p.fregs.mem[{}]={}'h{:016x};", i, kXLenBits,
                     cpu->state().regs.read_fp(i));
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
    write_xlen("p.priv        ", cpu->state().priv);

    write_64("p.mtime       ", cpu->clint_mmio.mtime);
    write_64("p.mtimecmp    ", cpu->clint_mmio.mtimecmp);

    write_xlen("p.load_res    ", cpu->state().load_res);
    std::println(out, "p.reserved    = 1'h{:x};", cpu->state().reserved);
    write_xlen("p.pending_exception   ", cpu->pipeline_context.pending_exception);
    write_xlen("p.pending_tval", cpu->pipeline_context.pending_tval);

    for (Word i = 0; i < simrv::memory::kTlbSize; ++i) {
        std::println(out, "mmu.TLB_inst_r.r_valid[{}] ={};", i,
                     static_cast<int>(cpu->TLB_inst_r.at(i).valid));
        std::println(out, "mmu.TLB_inst_r.mem[{}][39:22] =18'h{:05x};", i,
                     cpu->TLB_inst_r.at(i).v_addr >> 14);
        std::println(out, "mmu.TLB_inst_r.mem[{}][21:0] =22'h{:06x};", i,
                     cpu->TLB_inst_r.at(i).p_addr >> 10);
    }
    for (Word i = 0; i < simrv::memory::kTlbSize; ++i) {
        std::println(out, "mmu.TLB_data_r.r_valid[{}] ={};", i,
                     static_cast<int>(cpu->TLB_data_r.at(i).valid));
        std::println(out, "mmu.TLB_data_r.mem[{}][39:22] =18'h{:05x};", i,
                     cpu->TLB_data_r.at(i).v_addr >> 14);
        std::println(out, "mmu.TLB_data_r.mem[{}][21:0] =22'h{:06x};", i,
                     cpu->TLB_data_r.at(i).p_addr >> 10);
    }
    for (Word i = 0; i < simrv::memory::kTlbSize; ++i) {
        std::println(out, "mmu.TLB_data_w.r_valid[{}] ={};", i,
                     static_cast<int>(cpu->TLB_data_w.at(i).valid));
        std::println(out, "mmu.TLB_data_w.mem[{}][39:22] =18'h{:05x};", i,
                     cpu->TLB_data_w.at(i).v_addr >> 14);
        std::println(out, "mmu.TLB_data_w.mem[{}][21:0] =22'h{:06x};", i,
                     cpu->TLB_data_w.at(i).p_addr >> 10);
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

    std::println("__ file init_reg.txt was generated after {} cycle", cpu->clint_mmio.mtime);
}

void Tracer::write_instruction_mix_report() {
    std::ofstream out("instmix.txt");
    if (!out.is_open()) {
        std::println(stderr, "__ Error: cannot open instmix.txt");
        return;
    }
    std::println(out, "INSTRUCTION MIX");
    int total = 0;
    for (int i = 0; i < OperationIdCount; i++) {
        std::println(out, "{} : {:10}", simrv::decode::OPERATION_NAME.at(i),
                     machine_.cpu.e_instmix.at(i));
        total += machine_.cpu.e_instmix.at(i);
    }
    std::println(out, "TOTAL_____ : {:10}", total);
    std::println("__ file instmix.txt was generated after {} cycle", machine_.cpu.clint_mmio.mtime);
}

void Tracer::print_summary() {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(now - machine_.s_start_time).count();
    const auto etime = static_cast<Counter>(elapsed == 0 ? 1 : elapsed);
    std::println("__ Elapsed clocks (mtime)   : {:11}", machine_.cpu.clint_mmio.mtime);
    std::println("__ Executed instructions    : {:11}", machine_.cpu.e_icount);
    std::println("__ Fetched compressed insns : {:11}", machine_.cpu.e_ccount);
    std::println("__ Elapsed time (usec)      : {:11}", etime);
    std::println("__ Simulation speed (KIPS)  : {:11}", machine_.cpu.e_icount * 1000UL / etime);
    if (machine_.s_use_mix) {
        write_instruction_mix_report();
    }
}

void Tracer::emit_periodic_pc_trace(Counter mtime, Register cpc) {
    if ((mtime % D_TRACEPC_INTERVAL) == 0) {
        if (!tracepc_opened_) {
            tracepc_opened_ = true;
            fp_tracepc_.open("tracepc.txt");
            std::println("__ generate trace file: tracepc.txt\n");
        }
        std::println(fp_tracepc_, "{:08} {:0{}x}", static_cast<int>(mtime / D_TRACEPC_INTERVAL),
                     cpc, D_TRACE_HEX_WIDTH);
        fp_tracepc_.flush();
    }
}

void Tracer::emit_branch_prediction_trace(Counter mtime, Register cpc, Register jmp_pc,
                                          Word r_opcode, Word r_tkn) {
    if (!bpred_opened_) {
        bpred_opened_ = true;
        fp_bpred_.open("bpred.txt");
        std::println("__ generate trace file: bpred.txt\n");
    }

    const auto opcode = static_cast<Opcode>(r_opcode);
    const int ir_jb = static_cast<const int>((opcode == Opcode::Jal) || (opcode == Opcode::Jalr) ||
                                             (opcode == Opcode::Branch));
    const int ir_jump = (opcode == Opcode::Jal) ? 2 : (opcode == Opcode::Jalr) ? 3 : 0;
    const int ir_branch = static_cast<const int>(opcode == Opcode::Branch);

    const Word targ = ((ir_jump | ir_branch) != 0) ? jmp_pc : 0;
    std::println(fp_bpred_, "{:08} {:0{}x} {:0{}x} {} {} {} {}", static_cast<int>(mtime), cpc,
                 D_TRACE_HEX_WIDTH, targ, D_TRACE_HEX_WIDTH, ir_jb, r_tkn, ir_jump, ir_branch);
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
    if (machine_.s_rtosmode) {
        std::print(fp_trace, " {:08}", cpu.clint_mmio.mtimecmp);
    }
    std::println(fp_trace, "");

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++) {
            std::print(fp_trace, "{:0{}x}{}", st.regs.read((i * 8) + j), D_TRACE_HEX_WIDTH,
                       (j != 7 ? " " : "\n"));
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

        if (!machine_.s_rtosmode) {
            std::println(fp_trace, "{:0{}x} {:0{}x} {:0{}x}", st.stvec, D_TRACE_HEX_WIDTH,
                         st.sscratch, D_TRACE_HEX_WIDTH, st.sepc, D_TRACE_HEX_WIDTH);

            std::print(fp_trace, "{:0{}x} {:0{}x} {:0{}x} {:0{}x} {:0{}x} ", st.scause,
                       D_TRACE_HEX_WIDTH, st.stval, D_TRACE_HEX_WIDTH, st.satp, D_TRACE_HEX_WIDTH,
                       st.scounteren, D_TRACE_HEX_WIDTH, st.load_res, D_TRACE_HEX_WIDTH);
        }
        std::println(fp_trace, "{:0{}x} {:0{}x} {:0{}x}", cpu.pipeline_context.pending_exception,
                     D_TRACE_HEX_WIDTH, cpu.pipeline_context.pending_tval, D_TRACE_HEX_WIDTH,
                     st.priv, D_TRACE_HEX_WIDTH);

        if (!machine_.s_rtosmode) {
            for (int i = 0; i < 4; i++) {
                std::print(fp_trace, "{:0{}x} {:0{}x} ", cpu.TLB_inst_r.at(i).v_addr,
                           D_TRACE_HEX_WIDTH, cpu.TLB_inst_r.at(i).p_addr, D_TRACE_HEX_WIDTH);
            }
            std::println(fp_trace, "");
            for (int i = 0; i < 4; i++) {
                std::print(fp_trace, "{:0{}x} {:0{}x} ", cpu.TLB_data_r.at(i).v_addr,
                           D_TRACE_HEX_WIDTH, cpu.TLB_data_r.at(i).p_addr, D_TRACE_HEX_WIDTH);
            }
            std::println(fp_trace, "");
            for (int i = 0; i < 4; i++) {
                std::print(fp_trace, "{:0{}x} {:0{}x} ", cpu.TLB_data_w.at(i).v_addr,
                           D_TRACE_HEX_WIDTH, cpu.TLB_data_w.at(i).p_addr, D_TRACE_HEX_WIDTH);
            }
            std::println(fp_trace, "");
        }
    }
}

}  // namespace simrv::core