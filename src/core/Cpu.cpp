/**
 * @file Cpu.cpp
 * @brief CPU core state/control implementation.
 */
#include "simrv/core/Cpu.hpp"

#include "simrv/core/Machine.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/tui/Tui.hpp"

namespace simrv::core {

CPU::CPU() : plic_mmio(*this), clint_mmio(*this), csr_file(*this), sbi(*this) {
    state_.regs.fill(0);
    state_.regs.fill_fp(0);
    if constexpr (simrv::xlen::kIsXLen64) {
        state_.mstatus = (static_cast<CSRValue>(2) << 34) | (static_cast<CSRValue>(2) << 32);
    }
    state_.update_xlen();
}

void CPU::TLB_flush() {
    tlb.flush();
    decode_cache.flush();
    soft_tlb_flush();
}

void CPU::TLB_flush(bool match_all_vaddr, Address vaddr, bool match_all_asid, Word asid) {
    tlb.flush_selective(match_all_vaddr, vaddr, match_all_asid, asid);
    decode_cache.flush();
    soft_tlb_flush();
}

void CPU::soft_tlb_flush() {
    for (auto& entry : soft_tlb_read) {
        entry.valid = false;
    }
    for (auto& entry : soft_tlb_write) {
        entry.valid = false;
    }
}

auto CPU::get_mstatus(CSRValue mask) const -> CSRValue { return csr_file.getMstatus(mask); }

void CPU::set_mstatus(CSRValue wdata) {
    const CSRValue old_mstatus = state_.mstatus;
    csr_file.setMstatus(wdata);
    if (state_.mstatus != old_mstatus) {
        TLB_flush();
    }
}

auto CPU::read_csr(CSRAddress addr) const -> std::expected<CSRValue, ExceptionCode> { return csr_file.read(addr); }

auto CPU::write_csr(CSRAddress addr, CSRValue wdata) -> std::expected<void, ExceptionCode> {
    const CSRValue old_mstatus = state_.mstatus;
    const CSRValue old_satp = state_.satp;

    auto result = csr_file.write(addr, wdata);
    if (!result) {
        return result;
    }

    if (state_.mstatus != old_mstatus || state_.satp != old_satp) {
        TLB_flush();
    }
    return {};
}

void CPU::mret() { TrapController::mret(state_, tlb); }

void CPU::sret() { TrapController::sret(state_, tlb); }

void CPU::plic_update_mip() { InterruptController::updateMip(plic_mmio, state_); }

void CPU::plic_set_irq(int irq_num, int state) {
    InterruptController::setIrq(plic_mmio, irq_num, state);
}

void CPU::raise_exception(TrapCause cause, CSRValue tval) {
    TrapController::raiseException(*this, cause, tval);
}

void CPU::evaluate_timer_interrupt() {
    if (clint_mmio.mtime >= clint_mmio.mtimecmp) {
        state_.mip |= enum_mask(MipBit::Mtip);
        state_.mip |= enum_mask(MipBit::Stip);
    } else {
        state_.mip &= ~enum_mask(MipBit::Mtip);
        state_.mip &= ~enum_mask(MipBit::Stip);
    }
}

void CPU::run_cycle(Machine& machine) {
    uint32_t step_cycles = 1;

    if (machine.s_cycle_accurate) {
        uint64_t prev_imiss = icache.miss_count();
        uint64_t prev_dmiss = dcache.miss_count();

        if (!pipeline_task.handle) {
            pipeline_task = run_pipeline_coroutine(&machine);
        }

        auto err = pipeline_task.resume();
        if (err.has_value()) {
            raise_exception(static_cast<TrapCause>(err->code), err->tval);
        }

        bool icache_miss = (icache.miss_count() > prev_imiss);
        bool dcache_miss = (dcache.miss_count() > prev_dmiss);

        bool is_branch = (pipeline_context.opcode == Opcode::Branch);
        bool is_jump = (pipeline_context.opcode == Opcode::Jal || pipeline_context.opcode == Opcode::Jalr);
        bool branched = pipeline_context.tkn;

        step_cycles = pipeline_sim.step_instruction(
            pipeline_context.cpc, pipeline_context.opcode, pipeline_context.rd,
            pipeline_context.rs1, pipeline_context.rs2, pipeline_context.op_id, branched,
            is_branch, is_jump, icache_miss, dcache_miss, pipeline_context.tlb_miss, pipeline_context.jmp_pc
        );
    } else {
        if (machine.s_high_performance) {
            auto* cached = decode_cache.lookup(state_.pc);
            bool success = true;
            if (simrv::compiler::likely(cached != nullptr)) {
                static_cast<pipeline::DecodedInstruction&>(pipeline_context) = *cached;
                pipeline_context.tlb_miss = false;

                fetch_operands(machine);
                success = execute_stage(machine) &&
                          memory_stage(machine) &&
                          writeback_stage(machine) &&
                          commit_stage(machine);
            } else {
                success = fetch_stage(machine, state_.pc) &&
                          decode_stage(machine);
                
                if (success && !pipeline_context.pending_exception.has_value()) {
                    CachedOp op;
                    static_cast<pipeline::DecodedInstruction&>(op) = pipeline_context;
                    decode_cache.insert(state_.pc, op);
                }

                if (success) {
                    success = execute_stage(machine) &&
                              memory_stage(machine) &&
                              writeback_stage(machine) &&
                              commit_stage(machine);
                }
            }

            if (!success) {
                const auto cause = pipeline_context.pending_exception.value_or(static_cast<ExceptionCode>(0));
                raise_exception(static_cast<TrapCause>(cause), pipeline_context.pending_tval);
            }
        } else {
            if (!pipeline_task.handle) {
                pipeline_task = run_pipeline_coroutine(&machine);
            }

            auto err = pipeline_task.resume();
            if (err.has_value()) {
                raise_exception(static_cast<TrapCause>(err->code), err->tval);
            }
        }
    }

    clint_mmio.mcycle += step_cycles;
    clint_mmio.rtc_divider += static_cast<int>(step_cycles);
    if (clint_mmio.rtc_divider >= 10) {
        clint_mmio.mtime += clint_mmio.rtc_divider / 10;
        clint_mmio.rtc_divider %= 10;
    }

    if (machine.s_tuimode && machine.uart && machine.uart->tui() &&
        (machine.uart->tui()->get_right_panel_mode() == simrv::tui::TuiRightPanelMode::LiveTrace ||
         machine.uart->tui()->is_trace_enabled())) {
        auto op_id = static_cast<uint8_t>(pipeline_context.op_id);
        auto rd = static_cast<uint8_t>(pipeline_context.rd);
        auto rs1 = static_cast<uint8_t>(pipeline_context.rs1);
        auto rs2 = static_cast<uint8_t>(pipeline_context.rs2);
        
        Register rd_val = 0;
        Register rs1_val = 0;
        Register rs2_val = 0;
        
        std::string op_name;
        if (op_id < simrv::pipeline::OPERATION_NAME.size()) {
            std::string_view name_sv = simrv::pipeline::OPERATION_NAME.at(op_id);
            for (char c : name_sv) {
                op_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
        }
        bool is_fp = op_name.starts_with("f") && !op_name.starts_with("fence");
        
        bool rd_fp = is_fp;
        bool rs1_fp = is_fp;
        bool rs2_fp = is_fp;
        
        if (op_name == "fcvt.w.s" || op_name == "fcvt.wu.s" || op_name == "fcvt.w.d" || op_name == "fcvt.wu.d" ||
            op_name == "fcvt.l.s" || op_name == "fcvt.lu.s" || op_name == "fcvt.l.d" || op_name == "fcvt.lu.d" ||
            op_name == "fmv.x.w" || op_name == "fmv.x.d" || op_name.starts_with("feq") ||
            op_name.starts_with("flt") || op_name.starts_with("fle") || op_name.starts_with("fclass")) {
            rd_fp = false;
            rs1_fp = true;
            rs2_fp = true;
        } else if (op_name == "fcvt.s.w" || op_name == "fcvt.s.wu" || op_name == "fcvt.d.w" || op_name == "fcvt.d.wu" ||
                   op_name == "fcvt.s.l" || op_name == "fcvt.s.lu" || op_name == "fcvt.d.l" || op_name == "fcvt.d.lu" ||
                   op_name == "fmv.w.x" || op_name == "fmv.d.x") {
            rd_fp = true;
            rs1_fp = false;
            rs2_fp = false;
        } else if (op_name.starts_with("l") && !op_name.starts_with("lui")) {
            rd_fp = is_fp;
            rs1_fp = false;
        } else if (op_name.starts_with("s") && !op_name.starts_with("slt") && !op_name.starts_with("sll") &&
                   !op_name.starts_with("sra") && !op_name.starts_with("srl") && !op_name.starts_with("sub") &&
                   !op_name.starts_with("sret") && !op_name.starts_with("sfence") && !op_name.starts_with("sc")) {
            rs2_fp = is_fp;
            rs1_fp = false;
        }
        
        if (rd_fp) {
            rd_val = state_.regs.read_fp(static_cast<RegId>(rd));
        } else {
            rd_val = state_.regs.read(static_cast<RegId>(rd));
        }
        
        if (rs1_fp) {
            rs1_val = state_.regs.read_fp(static_cast<RegId>(rs1));
        } else {
            rs1_val = state_.regs.read(static_cast<RegId>(rs1));
        }
        
        if (rs2_fp) {
            rs2_val = state_.regs.read_fp(static_cast<RegId>(rs2));
        } else {
            rs2_val = state_.regs.read(static_cast<RegId>(rs2));
        }

        machine.uart->tui()->record_instruction(
            pipeline_context.cpc,
            op_id,
            rd,
            rd_val,
            rs1,
            rs1_val,
            rs2,
            rs2_val,
            pipeline_context.imm
        );
    }
}

void CPU::run_cycle_baremetal(Machine& machine) {
    pipeline_context.pending_exception = std::nullopt;
    pipeline_context.pending_tval = 0;
    
    run_fetch_stage_baremetal(machine);
    const bool success = !pipeline_context.pending_exception.has_value() &&
                         decode_stage(machine) &&
                         execute_stage(machine) &&
                         (run_memory_stage_baremetal(machine), !pipeline_context.pending_exception.has_value()) &&
                         writeback_stage(machine) &&
                         (run_commit_stage_baremetal(machine), !pipeline_context.pending_exception.has_value());

    if (simrv::compiler::unlikely(!success)) {
        const auto cause = pipeline_context.pending_exception.value_or(static_cast<ExceptionCode>(0));
        raise_exception(static_cast<TrapCause>(cause), pipeline_context.pending_tval);
    }

    clint_mmio.mcycle++;
}

void CPU::run_memory_stage_baremetal(Machine& machine) {
    auto& ctx = pipeline_context;
    if (ctx.pending_exception.has_value()) {
        return;
    }

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct5 = static_cast<Funct5Amo>(ctx.funct5);
    const auto addr = ctx.mem_addr;

    // Load Phase
    if (opcode == Opcode::Load || (opcode == Opcode::Amo && funct5 != Funct5Amo::Sc)) {
        if (simrv::compiler::likely(addr < simrv::memory::kDramSize)) {
            ctx.mem_rdata = simrv::memory::ram_read_fast(addr, static_cast<Instruction>(ctx.funct3), machine.mmem);
        } else {
            ctx.mem_rdata = simrv::memory::MemoryAccess::loadInt(machine.memory_, *this, addr, ctx.funct3);
        }
    }

    if (opcode == Opcode::LoadFp) {
        if (simrv::compiler::likely(addr < simrv::memory::kDramSize)) {
            const auto f3 = ctx.funct3;
            if (f3 == Funct3::Flw) {
                const Word lo = simrv::memory::ram_read_fast(addr, static_cast<Instruction>(Funct3::Lw), machine.mmem);
                ctx.fp_mem_rdata = static_cast<uint64_t>(kF32BoxerBits) | static_cast<uint64_t>(lo & kLower32Mask);
            } else if (f3 == Funct3::Fld) {
                if constexpr (simrv::xlen::kIsXLen64) {
                    ctx.fp_mem_rdata = static_cast<FloatingRegister>(
                        simrv::memory::ram_read_fast(addr, static_cast<Instruction>(Funct3::Ld), machine.mmem));
                } else {
                    const Word lo = simrv::memory::ram_read_fast(addr, static_cast<Instruction>(Funct3::Lw), machine.mmem);
                    const Word hi = simrv::memory::ram_read_fast(addr + 4, static_cast<Instruction>(Funct3::Lw), machine.mmem);
                    ctx.fp_mem_rdata = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
                }
            }
        } else {
            ctx.fp_mem_rdata = simrv::memory::MemoryAccess::loadFp(machine.memory_, *this, addr, ctx.funct3);
        }
    }

    if (opcode == Opcode::Amo && funct5 == Funct5Amo::Lr) {
        state_.load_res = addr;
        state_.reserved = 1;
    }

    // Prepare Store Data
    ctx.mem_wdata = (opcode != Opcode::Amo || funct5 == Funct5Amo::Sc)
                        ? ctx.rrs2
                        : execute::ExecuteUnit::aluAmo(ctx.rrs2, ctx.mem_rdata, funct5, ctx.funct3);

    if (opcode == Opcode::StoreFp) {
        ctx.mem_wdata =
            static_cast<Register>(ctx.fp_mem_wdata & static_cast<FloatingRegister>(kLower32Mask));
    }

    // Store Phase
    if ((opcode == Opcode::Store) ||
        (opcode == Opcode::Amo &&
          (funct5 == Funct5Amo::Sc && (ctx.wb_data == 0u) && (state_.reserved != 0u))) ||
        (opcode == Opcode::Amo && funct5 != Funct5Amo::Lr && funct5 != Funct5Amo::Sc)) {
        if (simrv::compiler::likely(addr < simrv::memory::kDramSize)) {
            // Check tohost writes
            if (simrv::compiler::unlikely(addr == machine.s_isatest_tohost || addr == 0x80001000 || addr == 0x40008000)) {
                const bool is_tohost_write =
                    simrv::xlen::kIsXLen64 ? (ctx.funct3 == Funct3::Sw ||
                                               ctx.funct3 == Funct3::Sd)
                                          : (ctx.funct3 == Funct3::Sw);
                if (is_tohost_write) {
                    machine.tohost = simrv::xlen::kIsXLen64
                                          ? ctx.mem_wdata
                                          : ((machine.tohost & 0xFFFFFFFF00000000ULL) | ctx.mem_wdata);
                }
            } else if (simrv::compiler::unlikely(!simrv::xlen::kIsXLen64 &&
                                                 (addr == machine.s_isatest_tohost + 4 || addr == 0x80001004 || addr == 0x40008004))) {
                const bool is_tohost_write = (ctx.funct3 == Funct3::Sw);
                if (is_tohost_write) {
                    machine.tohost = (machine.tohost & 0x00000000FFFFFFFFULL) |
                                      (static_cast<uint64_t>(ctx.mem_wdata) << 32);
                }
            }
            simrv::memory::ram_write_fast(addr, ctx.mem_wdata, static_cast<Instruction>(ctx.funct3), machine.mmem);
        } else {
            simrv::memory::MemoryAccess::storeInt(machine.memory_, *this, addr, ctx.mem_wdata, ctx.funct3);
        }
    }

    if (opcode == Opcode::StoreFp) {
        if (simrv::compiler::likely(addr < simrv::memory::kDramSize)) {
            const auto f3 = ctx.funct3;
            if (f3 == Funct3::Fsw) {
                simrv::memory::ram_write_fast(addr, static_cast<Word>(ctx.fp_mem_wdata & static_cast<FloatingRegister>(kLower32Mask)),
                                             static_cast<Instruction>(Funct3::Sw), machine.mmem);
            } else if (f3 == Funct3::Fsd) {
                if constexpr (simrv::xlen::kIsXLen64) {
                    simrv::memory::ram_write_fast(addr, static_cast<Word>(ctx.fp_mem_wdata),
                                                 static_cast<Instruction>(Funct3::Sd), machine.mmem);
                } else {
                    simrv::memory::ram_write_fast(addr, static_cast<Word>(ctx.fp_mem_wdata & static_cast<FloatingRegister>(kLower32Mask)),
                                                 static_cast<Instruction>(Funct3::Sw), machine.mmem);
                    simrv::memory::ram_write_fast(addr + 4, static_cast<Word>((ctx.fp_mem_wdata >> 32) & static_cast<FloatingRegister>(kLower32Mask)),
                                                 static_cast<Instruction>(Funct3::Sw), machine.mmem);
                }
            }
        } else {
            simrv::memory::MemoryAccess::storeFp(machine.memory_, *this, addr, ctx.fp_mem_wdata, ctx.funct3);
        }
    }

    if ((opcode == Opcode::Store) || (opcode == Opcode::StoreFp) ||
        (opcode == Opcode::Amo && funct5 != Funct5Amo::Lr)) {
        if (!ctx.pending_exception.has_value()) {
            state_.reserved = 0;
        }
    }
}

auto CPU::run_pipeline_coroutine(Machine* machine_ptr) -> simrv::pipeline::PipelineTask {
    Machine& machine = *machine_ptr;
    while (true) {
        if (!fetch_stage(machine, state_.pc)) {
            co_yield simrv::pipeline::StageError{.code = pipeline_context.pending_exception.value_or(static_cast<ExceptionCode>(0)),
                                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!decode_stage(machine)) {
            co_yield simrv::pipeline::StageError{.code = pipeline_context.pending_exception.value_or(static_cast<ExceptionCode>(0)),
                                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!execute_stage(machine)) {
            co_yield simrv::pipeline::StageError{.code = pipeline_context.pending_exception.value_or(static_cast<ExceptionCode>(0)),
                                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!memory_stage(machine)) {
            co_yield simrv::pipeline::StageError{.code = pipeline_context.pending_exception.value_or(static_cast<ExceptionCode>(0)),
                                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!writeback_stage(machine)) {
            co_yield simrv::pipeline::StageError{.code = pipeline_context.pending_exception.value_or(static_cast<ExceptionCode>(0)),
                                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!commit_stage(machine)) {
            co_yield simrv::pipeline::StageError{.code = pipeline_context.pending_exception.value_or(static_cast<ExceptionCode>(0)),
                                .tval = pipeline_context.pending_tval};
            continue;
        }
        co_yield std::nullopt;
    }
}

auto CPU::fetch_stage(Machine& machine, Address pc) -> bool {
    state_.pc = pc;
    pipeline_context.pending_exception = std::nullopt;
    pipeline_context.pending_tval = 0;

    run_fetch_stage(machine);

    return !pipeline_context.pending_exception.has_value();
}

auto CPU::decode_stage(Machine& machine) -> bool {
    run_decode_stage(machine);
    return !pipeline_context.pending_exception.has_value();
}

auto CPU::execute_stage(Machine& machine) -> bool {
    run_execute_stage(machine);
    return !pipeline_context.pending_exception.has_value();
}

auto CPU::memory_stage(Machine& machine) -> bool {
    run_memory_stage(machine);
    return !pipeline_context.pending_exception.has_value();
}

auto CPU::writeback_stage(Machine& machine) -> bool {
    run_writeback_stage(machine);
    return !pipeline_context.pending_exception.has_value();
}

auto CPU::commit_stage(Machine& machine) -> bool {
    run_commit_stage(machine);
    return !pipeline_context.pending_exception.has_value();
}

}  // namespace simrv::core