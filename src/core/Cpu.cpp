/**
 * @file Cpu.cpp
 * @brief CPU core state/control implementation.
 */
#include "simrv/core/Cpu.hpp"

#include "simrv/core/Machine.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

CPU::CPU()
    : TLB_inst_r(tlb.inst_r),
      TLB_data_r(tlb.data_r),
      TLB_data_w(tlb.data_w),
      plic_mmio(*this),
      clint_mmio(*this),
      csr_file(*this),
      sbi(*this) {
    state_.regs.fill(0);
    state_.regs.fill_fp(0);
}

void CPU::TLB_flush() { tlb.flush(); }

void CPU::TLB_flush(bool match_all_vaddr, Address vaddr, bool match_all_asid, Word asid) {
    tlb.flush_selective(match_all_vaddr, vaddr, match_all_asid, asid);
}

auto CPU::get_mstatus(CSRValue mask) const -> CSRValue { return csr_file.getMstatus(mask); }

void CPU::set_mstatus(CSRValue wdata) {
    const CSRValue old_mstatus = state_.mstatus;
    csr_file.setMstatus(wdata);
    if (state_.mstatus != old_mstatus) {
        TLB_flush();
    }
}

auto CPU::read_csr(CSRAddress addr) const -> CSRValue { return csr_file.read(addr); }

void CPU::write_csr(CSRAddress addr, CSRValue wdata) {
    const CSRValue old_mstatus = state_.mstatus;
    const CSRValue old_satp = state_.satp;

    csr_file.write(addr, wdata);

    if (state_.mstatus != old_mstatus || state_.satp != old_satp) {
        TLB_flush();
    }
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
    } else {
        state_.mip &= ~enum_mask(MipBit::Mtip);
    }
}

void CPU::run_cycle(Machine& machine) {
    auto status = fetch_stage(machine, state_.pc)
        .and_then([&](auto&& fetch)  { return decode_stage(machine, fetch);  })
        .and_then([&](auto&& decode) { return execute_stage(machine, decode); })
        .and_then([&](auto&& exec)   { return memory_stage(machine, exec);   })
        .and_then([&](auto&& mem)    { return writeback_stage(machine, mem); })
        .and_then([&](auto&& wb)     { return commit_stage(machine, wb);     });

    if (!status.has_value()) {
        const auto& err = status.error();
        raise_exception(static_cast<TrapCause>(err.code), err.tval);
    }

    clint_mmio.mtime++;
}

auto CPU::fetch_stage(Machine& machine, Address pc) -> std::expected<FetchResult, StageError> {
    state_.pc = pc;
    pipeline_context.pending_exception = std::nullopt;
    pipeline_context.pending_tval = 0;

    run_fetch_stage(machine);

    if (pipeline_context.pending_exception.has_value()) {
        return std::unexpected<StageError>({
            .code = pipeline_context.pending_exception.value(),
            .tval = pipeline_context.pending_tval
        });
    }

    return FetchResult{
        .pc = pc,
        .ir_org = pipeline_context.ir_org,
        .ir = pipeline_context.ir,
        .cinsn = (pipeline_context.cinsn != 0)
    };
}

auto CPU::decode_stage(Machine& machine, const FetchResult& fetch) -> std::expected<DecodeResult, StageError> {
    pipeline_context.ir_org = fetch.ir_org;
    pipeline_context.ir = fetch.ir;
    pipeline_context.cinsn = fetch.cinsn ? 1U : 0U;

    run_decode_stage(machine);

    if (pipeline_context.pending_exception.has_value()) {
        return std::unexpected<StageError>({
            .code = pipeline_context.pending_exception.value(),
            .tval = pipeline_context.pending_tval
        });
    }

    return DecodeResult{
        .fetch = fetch,
        .opcode = static_cast<Opcode>(pipeline_context.opcode),
        .rd = static_cast<RegId>(pipeline_context.rd),
        .rs1 = static_cast<RegId>(pipeline_context.rs1),
        .rs2 = static_cast<RegId>(pipeline_context.rs2),
        .funct3 = static_cast<Funct3>(pipeline_context.funct3),
        .funct5 = static_cast<Funct5Amo>(pipeline_context.funct5),
        .funct7 = pipeline_context.funct7,
        .funct12 = pipeline_context.funct12,
        .imm = pipeline_context.imm,
        .rrs1 = pipeline_context.rrs1,
        .rrs2 = pipeline_context.rrs2,
        .rcsr = pipeline_context.rcsr
    };
}

auto CPU::execute_stage(Machine& machine, const DecodeResult& decode) -> std::expected<ExecuteResult, StageError> {
    pipeline_context.opcode = decode.opcode;
    pipeline_context.rd = decode.rd;
    pipeline_context.rs1 = decode.rs1;
    pipeline_context.rs2 = decode.rs2;
    pipeline_context.funct3 = decode.funct3;
    pipeline_context.funct5 = decode.funct5;
    pipeline_context.funct7 = decode.funct7;
    pipeline_context.funct12 = decode.funct12;
    pipeline_context.imm = decode.imm;
    pipeline_context.rrs1 = decode.rrs1;
    pipeline_context.rrs2 = decode.rrs2;
    pipeline_context.rcsr = decode.rcsr;

    run_execute_stage(machine);

    if (pipeline_context.pending_exception.has_value()) {
        return std::unexpected<StageError>({
            .code = pipeline_context.pending_exception.value(),
            .tval = pipeline_context.pending_tval
        });
    }

    return ExecuteResult{
        .decode = decode,
        .tkn = pipeline_context.tkn,
        .jmp_pc = pipeline_context.jmp_pc,
        .mem_addr = pipeline_context.mem_addr,
        .wb_data = pipeline_context.wb_data,
        .wb_data_csr = pipeline_context.wb_data_csr,
        .fp_wb_data = pipeline_context.fp_wb_data,
        .fp_wb_enable = pipeline_context.fp_wb_enable,
        .int_wb_from_fp = pipeline_context.int_wb_from_fp
    };
}

auto CPU::memory_stage(Machine& machine, const ExecuteResult& exec) -> std::expected<MemoryResult, StageError> {
    pipeline_context.tkn = exec.tkn;
    pipeline_context.jmp_pc = exec.jmp_pc;
    pipeline_context.mem_addr = exec.mem_addr;
    pipeline_context.wb_data = exec.wb_data;
    pipeline_context.wb_data_csr = exec.wb_data_csr;
    pipeline_context.fp_wb_data = exec.fp_wb_data;
    pipeline_context.fp_wb_enable = exec.fp_wb_enable;
    pipeline_context.int_wb_from_fp = exec.int_wb_from_fp;

    run_memory_stage(machine);

    if (pipeline_context.pending_exception.has_value()) {
        return std::unexpected<StageError>({
            .code = pipeline_context.pending_exception.value(),
            .tval = pipeline_context.pending_tval
        });
    }

    return MemoryResult{
        .exec = exec,
        .mem_rdata = pipeline_context.mem_rdata,
        .mem_wdata = pipeline_context.mem_wdata,
        .fp_mem_rdata = pipeline_context.fp_mem_rdata,
        .fp_mem_wdata = pipeline_context.fp_mem_wdata
    };
}

auto CPU::writeback_stage(Machine& machine, const MemoryResult& mem) -> std::expected<WritebackResult, StageError> {
    pipeline_context.mem_rdata = mem.mem_rdata;
    pipeline_context.mem_wdata = mem.mem_wdata;
    pipeline_context.fp_mem_rdata = mem.fp_mem_rdata;
    pipeline_context.fp_mem_wdata = mem.fp_mem_wdata;

    run_writeback_stage(machine);

    if (pipeline_context.pending_exception.has_value()) {
        return std::unexpected<StageError>({
            .code = pipeline_context.pending_exception.value(),
            .tval = pipeline_context.pending_tval
        });
    }

    return WritebackResult{
        .mem = mem
    };
}

auto CPU::commit_stage(Machine& machine, const WritebackResult& wb) -> std::expected<void, StageError> {
    (void)wb;
    run_commit_stage(machine);

    if (pipeline_context.pending_exception.has_value()) {
        return std::unexpected<StageError>({
            .code = pipeline_context.pending_exception.value(),
            .tval = pipeline_context.pending_tval
        });
    }

    return {};
}

}  // namespace simrv::core