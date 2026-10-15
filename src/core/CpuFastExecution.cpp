/**
 * @file CpuFastExecution.cpp
 * @brief Cached instruction execution and optimized memory access paths.
 */
#include <array>

#include "CpuExecutionHelpers.hpp"
#include "simrv/CompilerHints.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/PlatformTiming.hpp"
#include "simrv/core/Pmp.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/debug/GdbStub.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

namespace simrv::core {
using isa::Funct3;

SIMRV_ALWAYS_INLINE void CPU::execute_cached_jal(CachedOp& op) {
    simrv::xlen::Register const next_pc = state_.pc + op.len();
    pipeline_context.tkn = true;
    pipeline_context.jmp_pc = simrv::xlen::VirtAddr{state_.pc + op.imm};
    if (simrv::compiler::unlikely((pipeline_context.jmp_pc & state_.fetch_align_mask) != 0)) {
        raise_exception(ExceptionCode::MisalignedFetch, pipeline_context.jmp_pc.raw());
        return;
    }
    state_.regs.write_branchless(op.rd, next_pc);
    commit_cached_branch_target(op, pipeline_context.jmp_pc);
}

SIMRV_ALWAYS_INLINE void CPU::execute_cached_jalr(CachedOp& op, simrv::xlen::Register rrs1) {
    simrv::xlen::Register const next_pc = state_.pc + op.len();
    pipeline_context.tkn = true;
    pipeline_context.jmp_pc =
        simrv::xlen::VirtAddr{(rrs1 + op.imm) & ~static_cast<simrv::xlen::Register>(1)};
    if (state_.regs.xlen == 32) {
        pipeline_context.jmp_pc = simrv::xlen::VirtAddr{static_cast<simrv::xlen::Register>(
            static_cast<int64_t>(static_cast<int32_t>(pipeline_context.jmp_pc.raw())))};
    }
    if (simrv::compiler::unlikely((pipeline_context.jmp_pc & state_.fetch_align_mask) != 0)) {
        raise_exception(ExceptionCode::MisalignedFetch, pipeline_context.jmp_pc.raw());
        return;
    }
    state_.regs.write_branchless(op.rd, next_pc);
    commit_cached_branch_target(op, pipeline_context.jmp_pc);
}

SIMRV_ALWAYS_INLINE void CPU::execute_cached_branch(CachedOp& op, simrv::xlen::Register rrs1,
                                                    simrv::xlen::Register rrs2) {
    bool tkn = false;
    switch (op.funct3) {
        case isa::Funct3::Beq:
            tkn = (rrs1 == rrs2);
            break;
        case isa::Funct3::Bne:
            tkn = (rrs1 != rrs2);
            break;
        case isa::Funct3::Blt:
            tkn = (static_cast<simrv::xlen::SignedWord>(rrs1) <
                   static_cast<simrv::xlen::SignedWord>(rrs2));
            break;
        case isa::Funct3::Bge:
            tkn = (static_cast<simrv::xlen::SignedWord>(rrs1) >=
                   static_cast<simrv::xlen::SignedWord>(rrs2));
            break;
        case isa::Funct3::Bltu:
            tkn = (rrs1 < rrs2);
            break;
        case isa::Funct3::Bgeu:
            tkn = (rrs1 >= rrs2);
            break;
        default:
            tkn = execute::ExecuteUnit::branchTaken(rrs1, rrs2, op.funct3, state_.regs.xlen);
            break;
    }
    pipeline_context.tkn = tkn;
    simrv::xlen::VirtAddr const target_pc = tkn ? simrv::xlen::VirtAddr{state_.pc + op.imm}
                                                : simrv::xlen::VirtAddr{state_.pc + op.len()};
    pipeline_context.jmp_pc = target_pc;
    if (simrv::compiler::unlikely(tkn && (target_pc & state_.fetch_align_mask) != 0)) {
        raise_exception(ExceptionCode::MisalignedFetch, target_pc.raw());
        return;
    }
    commit_cached_branch_target(op, target_pc);
}

auto CPU::try_fast_load([[maybe_unused]] Machine& machine, simrv::xlen::Address mem_addr,
                        isa::Funct3 funct3, simrv::xlen::Register& out_val,
                        const simrv::memory::RamView& ram) -> bool {
    if (simrv::compiler::unlikely(!is_aligned_for_funct3(mem_addr, funct3))) {
        return false;
    }

    if (simrv::compiler::likely(state_.priv == kPrivMachine &&
                                (state_.mstatus & MstatusBit::Mprv) == 0)) {
        if (simrv::compiler::unlikely(state_.has_locked_pmp)) return false;
        const unsigned size_bytes = access_size_for_funct3(funct3);
        if (simrv::compiler::likely(ram.contains(mem_addr, size_bytes))) {
            out_val = simrv::memory::ram_read_fast(
                mem_addr, static_cast<simrv::xlen::Instruction>(funct3), ram);
            return true;
        }
        return false;
    }

    const simrv::xlen::PrivilegeLevel eff_priv = effective_data_privilege();
    const unsigned active_xlen = state_.xlen_for_privilege(eff_priv);
    if (active_xlen == 32) mem_addr &= 0xFFFFFFFFULL;
    const unsigned size_bytes = access_size_for_funct3(funct3);

    if (simrv::compiler::likely(eff_priv != kPrivMachine &&
                                simrv::xlen::satp_translation_enabled(state_.satp, active_xlen))) {
        const simrv::xlen::Word current_asid = simrv::xlen::satp_asid(state_.satp, active_xlen);
        const simrv::xlen::Address vpn = mem_addr >> 12;
        const size_t tlb_idx = soft_tlb_index(vpn);
        const auto& entry = soft_tlb_read[tlb_idx];
        if (simrv::compiler::likely(entry.matches(vpn, current_asid, eff_priv, soft_tlb_epoch))) {
            if (simrv::compiler::unlikely(state_.num_active_pmp > 0)) {
                const simrv::xlen::Address paddr = (entry.paddr_base + (mem_addr & 0xFFF)).raw();
                if (simrv::compiler::unlikely(!core::pmp::check_access(
                        state_, paddr, size_bytes, core::PmpAccessType::Read, eff_priv))) {
                    return false;
                }
            }
            if (simrv::compiler::likely(entry.has_host_ptr())) {
                out_val = simrv::memory::host_read_fast(
                    entry.host_ptr(mem_addr), static_cast<simrv::xlen::Instruction>(funct3));
                return true;
            }
            const simrv::xlen::Address paddr = (entry.paddr_base + (mem_addr & 0xFFF)).raw();
            if (simrv::compiler::likely(ram.contains(paddr, size_bytes))) {
                out_val = simrv::memory::ram_read_fast(
                    paddr, static_cast<simrv::xlen::Instruction>(funct3), ram);
                return true;
            }
            return false;
        }
        TLBEntry* tlb_e = tlb.lookup_data_r(mem_addr, current_asid, eff_priv);
        if (simrv::compiler::likely(tlb_e != nullptr)) {
            simrv::xlen::Address const paddr = tlb_e->p_addr + (mem_addr & 0xFFF);
            if (simrv::compiler::unlikely(!core::pmp::check_access(
                    state_, paddr, size_bytes, core::PmpAccessType::Read, eff_priv))) {
                return false;
            }
            simrv::xlen::Address const ppage = tlb_e->p_addr;
            simrv::xlen::Byte* host_base =
                ram.contains(ppage, 4096) ? ram.unchecked_ptr(ppage) : nullptr;
            if (host_base != nullptr) {
                soft_tlb_read[tlb_idx].set(vpn, current_asid, eff_priv, soft_tlb_epoch, ppage,
                                           host_base);
                out_val = simrv::memory::host_read_fast(
                    host_base + (mem_addr & 0xFFF), static_cast<simrv::xlen::Instruction>(funct3));
                return true;
            }
            if (simrv::compiler::likely(ram.contains(paddr, size_bytes))) {
                out_val = simrv::memory::ram_read_fast(
                    paddr, static_cast<simrv::xlen::Instruction>(funct3), ram);
                return true;
            }
        }
    } else {
        if (simrv::compiler::likely(ram.contains(mem_addr, size_bytes) &&
                                    core::pmp::check_access(state_, mem_addr, size_bytes,
                                                            core::PmpAccessType::Read, eff_priv))) {
            out_val = simrv::memory::ram_read_fast(
                mem_addr, static_cast<simrv::xlen::Instruction>(funct3), ram);
            return true;
        }
    }
    return false;
}

auto CPU::try_fast_store(Machine& machine, simrv::xlen::Address mem_addr, isa::Funct3 funct3,
                         simrv::xlen::Register rrs2, const simrv::memory::RamView& ram) -> bool {
    if (simrv::compiler::unlikely(!is_aligned_for_funct3(mem_addr, funct3))) {
        return false;
    }

    if (simrv::compiler::likely(state_.priv == kPrivMachine &&
                                (state_.mstatus & MstatusBit::Mprv) == 0)) {
        if (simrv::compiler::unlikely(state_.has_locked_pmp)) return false;
        const unsigned size_bytes = access_size_for_funct3(funct3);
        if (simrv::compiler::likely(ram.contains(mem_addr, size_bytes) &&
                                    !is_tohost_addr(machine, mem_addr))) {
            simrv::memory::ram_write_fast(mem_addr, rrs2,
                                          static_cast<simrv::xlen::Instruction>(funct3), ram);
            pipeline_context.mem_paddr = mem_addr;
            return true;
        }
        return false;
    }

    const simrv::xlen::PrivilegeLevel eff_priv = effective_data_privilege();
    const unsigned active_xlen = state_.xlen_for_privilege(eff_priv);
    if (active_xlen == 32) mem_addr &= 0xFFFFFFFFULL;
    const unsigned size_bytes = access_size_for_funct3(funct3);

    if (simrv::compiler::likely(eff_priv != kPrivMachine &&
                                simrv::xlen::satp_translation_enabled(state_.satp, active_xlen))) {
        const simrv::xlen::Word current_asid = simrv::xlen::satp_asid(state_.satp, active_xlen);
        const simrv::xlen::Address vpn = mem_addr >> 12;
        const size_t tlb_idx = soft_tlb_index(vpn);
        const auto& entry = soft_tlb_write[tlb_idx];
        if (simrv::compiler::likely(entry.matches(vpn, current_asid, eff_priv, soft_tlb_epoch))) {
            simrv::xlen::PhysAddr const paddr = entry.paddr_base + (mem_addr & 0xFFF);
            if (simrv::compiler::unlikely(state_.num_active_pmp > 0)) {
                if (simrv::compiler::unlikely(!core::pmp::check_access(
                        state_, paddr, size_bytes, core::PmpAccessType::Write, eff_priv))) {
                    return false;
                }
            }
            if (simrv::compiler::unlikely(is_tohost_addr(machine, paddr))) {
                return false;
            }
            if (simrv::compiler::likely(entry.has_host_ptr())) {
                simrv::memory::host_write_fast(entry.host_ptr(mem_addr), rrs2,
                                               static_cast<simrv::xlen::Instruction>(funct3));
                pipeline_context.mem_paddr = paddr;
                return true;
            }
            if (simrv::compiler::likely(ram.contains(paddr, size_bytes))) {
                simrv::memory::ram_write_fast(paddr, rrs2,
                                              static_cast<simrv::xlen::Instruction>(funct3), ram);
                pipeline_context.mem_paddr = paddr;
                return true;
            }
            return false;
        }
        TLBEntry* tlb_e = tlb.lookup_data_w(mem_addr, current_asid, eff_priv);
        if (simrv::compiler::likely(tlb_e != nullptr)) {
            simrv::xlen::Address const paddr = tlb_e->p_addr + (mem_addr & 0xFFF);
            if (simrv::compiler::unlikely(is_tohost_addr(machine, paddr))) {
                return false;
            }
            if (simrv::compiler::unlikely(!core::pmp::check_access(
                    state_, paddr, size_bytes, core::PmpAccessType::Write, eff_priv))) {
                return false;
            }
            simrv::xlen::Address const ppage = tlb_e->p_addr;
            simrv::xlen::Byte* host_base =
                ram.contains(ppage, 4096) ? ram.unchecked_ptr(ppage) : nullptr;
            if (host_base != nullptr) {
                soft_tlb_write[tlb_idx].set(vpn, current_asid, eff_priv, soft_tlb_epoch, ppage,
                                            host_base, tlb_e->global);
                simrv::memory::host_write_fast(host_base + (mem_addr & 0xFFF), rrs2,
                                               static_cast<simrv::xlen::Instruction>(funct3));
                pipeline_context.mem_paddr = paddr;
                return true;
            }
            if (simrv::compiler::likely(ram.contains(paddr, size_bytes))) {
                simrv::memory::ram_write_fast(paddr, rrs2,
                                              static_cast<simrv::xlen::Instruction>(funct3), ram);
                pipeline_context.mem_paddr = paddr;
                return true;
            }
        }
    } else {
        if (simrv::compiler::likely(
                ram.contains(mem_addr, size_bytes) && !is_tohost_addr(machine, mem_addr) &&
                core::pmp::check_access(state_, mem_addr, size_bytes, core::PmpAccessType::Write,
                                        eff_priv))) {
            simrv::memory::ram_write_fast(mem_addr, rrs2,
                                          static_cast<simrv::xlen::Instruction>(funct3), ram);
            pipeline_context.mem_paddr = mem_addr;
            return true;
        }
    }
    return false;
}

SIMRV_ALWAYS_INLINE auto CPU::execute_cached_load(Machine& machine, CachedOp& op,
                                                  simrv::xlen::Register rrs1,
                                                  const simrv::memory::RamView& ram) -> bool {
    simrv::xlen::Address const mem_addr = rrs1 + op.imm;
    if (simrv::compiler::unlikely(machine.tui_enabled() || machine.branch_trace_enabled())) {
        pipeline_context.mem_addr = mem_addr;
    }
    simrv::xlen::Register mem_rdata = 0;
    const unsigned size_bytes = op.mem_size;
    const simrv::xlen::Address align_mask = op.align_mask();
    if (simrv::compiler::likely(state_.priv == kPrivMachine &&
                                (state_.mstatus & MstatusBit::Mprv) == 0 &&
                                !state_.has_locked_pmp)) {
        if (simrv::compiler::likely((mem_addr & align_mask) == 0 &&
                                    ram.contains(mem_addr, size_bytes))) {
            mem_rdata = simrv::memory::ram_read_fast(
                mem_addr, static_cast<simrv::xlen::Instruction>(op.funct3), ram);
            state_.regs.write_branchless(op.rd, mem_rdata);
            advance_cached_pc(op);
            return true;
        }
    } else {
        const simrv::xlen::PrivilegeLevel eff_priv = effective_data_privilege();
        const unsigned active_xlen = state_.xlen_for_privilege(eff_priv);
        simrv::xlen::Address lookup_addr = mem_addr;
        if (active_xlen == 32) lookup_addr &= 0xFFFFFFFFULL;
        const unsigned page_offset = lookup_addr & 0xFFFu;
        if (simrv::compiler::likely(
                (lookup_addr & align_mask) == 0 && page_offset + size_bytes <= 4096u &&
                simrv::xlen::satp_translation_enabled(state_.satp, active_xlen))) {
            const simrv::xlen::Word current_asid = simrv::xlen::satp_asid(state_.satp, active_xlen);
            const simrv::xlen::Address vpn = lookup_addr >> 12;
            const size_t tlb_idx = soft_tlb_index(vpn);
            const auto& entry = soft_tlb_read[tlb_idx];
            if (simrv::compiler::likely(
                    entry.matches(vpn, current_asid, eff_priv, soft_tlb_epoch))) {
                if (simrv::compiler::unlikely(state_.num_active_pmp > 0)) {
                    const simrv::xlen::Address paddr = (entry.paddr_base + page_offset).raw();
                    if (simrv::compiler::unlikely(!core::pmp::check_access(
                            state_, paddr, size_bytes, core::PmpAccessType::Read, eff_priv))) {
                        // Let MemoryAccess raise the architectural access fault.
                    } else if (simrv::compiler::likely(entry.has_host_ptr())) {
                        mem_rdata = simrv::memory::host_read_fast(
                            entry.host_ptr(lookup_addr),
                            static_cast<simrv::xlen::Instruction>(op.funct3));
                        state_.regs.write_branchless(op.rd, mem_rdata);
                        advance_cached_pc(op);
                        return true;
                    }
                } else if (simrv::compiler::likely(entry.has_host_ptr())) {
                    mem_rdata = simrv::memory::host_read_fast(
                        entry.host_ptr(lookup_addr),
                        static_cast<simrv::xlen::Instruction>(op.funct3));
                    state_.regs.write_branchless(op.rd, mem_rdata);
                    advance_cached_pc(op);
                    return true;
                }
            }
        }
    }

    if (!try_fast_load(machine, mem_addr, op.funct3, mem_rdata, ram)) {
        op.copy_to(pipeline_context);
        pipeline_context.mem_addr = mem_addr;
        mem_rdata =
            simrv::memory::MemoryAccess::loadInt(machine.memory_, *this, mem_addr, op.funct3);
        if (pipeline_context.pending_exception.has_value()) {
            raise_exception(
                static_cast<simrv::xlen::TrapCause>(*pipeline_context.pending_exception),
                pipeline_context.pending_tval);
            return false;
        }
    }

    state_.regs.write_branchless(op.rd, mem_rdata);
    advance_cached_pc(op);
    return true;
}

SIMRV_ALWAYS_INLINE auto CPU::execute_cached_store(Machine& machine, CachedOp& op,
                                                   simrv::xlen::Register rrs1,
                                                   simrv::xlen::Register rrs2,
                                                   const simrv::memory::RamView& ram) -> bool {
    simrv::xlen::Address const mem_addr = rrs1 + op.imm;
    if (simrv::compiler::unlikely(machine.tui_enabled() || machine.branch_trace_enabled())) {
        pipeline_context.mem_addr = mem_addr;
    }
    const unsigned size_bytes = op.mem_size;
    const simrv::xlen::Address align_mask = op.align_mask();
    if (simrv::compiler::likely(state_.priv == kPrivMachine &&
                                (state_.mstatus & MstatusBit::Mprv) == 0 &&
                                !state_.has_locked_pmp)) {
        if (simrv::compiler::likely((mem_addr & align_mask) == 0 &&
                                    ram.contains(mem_addr, size_bytes) &&
                                    !is_tohost_addr(machine, mem_addr))) {
            simrv::memory::ram_write_fast(mem_addr, rrs2,
                                          static_cast<simrv::xlen::Instruction>(op.funct3), ram);
            state_.lr_reservation_active = false;
            if (simrv::compiler::unlikely(
                    machine.memory_.reservation_table().may_have_reservations() &&
                    machine.num_harts() > 1)) {
                machine.memory_.reservation_table().invalidate_range(mem_addr, size_bytes,
                                                                     hart_id());
            }
            advance_cached_pc(op);
            return true;
        }
    } else {
        const simrv::xlen::PrivilegeLevel eff_priv = effective_data_privilege();
        const unsigned active_xlen = state_.xlen_for_privilege(eff_priv);
        simrv::xlen::Address lookup_addr = mem_addr;
        if (active_xlen == 32) lookup_addr &= 0xFFFFFFFFULL;
        const unsigned page_offset = lookup_addr & 0xFFFu;
        if (simrv::compiler::likely(
                (lookup_addr & align_mask) == 0 && page_offset + size_bytes <= 4096u &&
                simrv::xlen::satp_translation_enabled(state_.satp, active_xlen))) {
            const simrv::xlen::Word current_asid = simrv::xlen::satp_asid(state_.satp, active_xlen);
            const simrv::xlen::Address vpn = lookup_addr >> 12;
            const size_t tlb_idx = soft_tlb_index(vpn);
            const auto& entry = soft_tlb_write[tlb_idx];
            if (simrv::compiler::likely(
                    entry.matches(vpn, current_asid, eff_priv, soft_tlb_epoch))) {
                const simrv::xlen::Address paddr = (entry.paddr_base + page_offset).raw();
                if (simrv::compiler::unlikely(state_.num_active_pmp > 0) &&
                    simrv::compiler::unlikely(!core::pmp::check_access(
                        state_, paddr, size_bytes, core::PmpAccessType::Write, eff_priv))) {
                    // Let MemoryAccess raise the architectural access fault.
                } else {
                    if (simrv::compiler::likely(entry.has_host_ptr() &&
                                                !is_tohost_addr(machine, paddr))) {
                        simrv::memory::host_write_fast(
                            entry.host_ptr(lookup_addr), rrs2,
                            static_cast<simrv::xlen::Instruction>(op.funct3));
                        state_.lr_reservation_active = false;
                        if (simrv::compiler::unlikely(
                                machine.memory_.reservation_table().may_have_reservations() &&
                                machine.num_harts() > 1)) {
                            machine.memory_.reservation_table().invalidate_range(paddr, size_bytes,
                                                                                 hart_id());
                        }
                        advance_cached_pc(op);
                        return true;
                    }
                }
            }
        }
    }

    if (!try_fast_store(machine, mem_addr, op.funct3, rrs2, ram)) {
        op.copy_to(pipeline_context);
        pipeline_context.mem_addr = mem_addr;
        simrv::memory::MemoryAccess::storeInt(machine.memory_, *this, mem_addr, rrs2, op.funct3);
        if (pipeline_context.pending_exception.has_value()) {
            raise_exception(
                static_cast<simrv::xlen::TrapCause>(*pipeline_context.pending_exception),
                pipeline_context.pending_tval);
            return false;
        }
    }

    state_.lr_reservation_active = false;
    if (simrv::compiler::unlikely(machine.memory_.reservation_table().may_have_reservations() &&
                                  machine.num_harts() > 1)) {
        machine.memory_.reservation_table().invalidate_range(pipeline_context.mem_paddr, size_bytes,
                                                             hart_id());
    }
    advance_cached_pc(op);
    return true;
}

auto CPU::execute_cached_fallback(Machine& machine) -> void {
    // Cached operations can fall back to the staged executor without passing
    // through fetch_stage. Start with a fresh memory-stage latch in that path too.
    pipeline_context.memory_load_complete = false;
    pipeline_context.sc_reservation_checked = false;
    pipeline_context.sc_reservation_succeeded = false;
    pipeline_context.mem_paddr = simrv::xlen::PhysAddr{0};
    fetch_operands(machine);
    bool success = false;
    if (simrv::compiler::likely(state_.priv == kPrivMachine &&
                                (state_.mstatus & MstatusBit::Mprv) == 0)) {
        success = execute_stage(machine);
        if (success) {
            run_memory_stage_baremetal(machine);
            success = !pipeline_context.pending_exception.has_value() && writeback_stage(machine) &&
                      commit_stage(machine);
        }
    } else {
        success = execute_stage(machine) && memory_stage(machine) && writeback_stage(machine) &&
                  commit_stage(machine);
    }
    if (!success) {
        const auto cause =
            pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch);
        raise_exception(static_cast<simrv::xlen::TrapCause>(cause), pipeline_context.pending_tval);
    }
}

void CPU::dispatch_pending_interrupts() {
    simrv::xlen::Word const pending_interrupts = state_.mip & state_.mie;
    if (simrv::compiler::unlikely(pending_interrupts != 0u)) {
        simrv::xlen::Word enable_interrupts = 0;
        switch (state_.priv) {
            case kPrivMachine: {
                if ((state_.mstatus & MstatusBit::Mie) != 0u) {
                    enable_interrupts = ~state_.mideleg;
                }
                break;
            }
            case kPrivSupervisor: {
                enable_interrupts = ~state_.mideleg;
                if ((state_.mstatus & MstatusBit::Sie) != 0u) {
                    enable_interrupts |= state_.mideleg;
                }
                break;
            }
            case kPrivUser: {
                enable_interrupts = ~0;
                break;
            }
            default:
                break;
        }
        simrv::xlen::Word const mask = pending_interrupts & enable_interrupts;
        if (mask != 0u) {
            simrv::xlen::Word const irq_num = select_highest_priority_interrupt(mask);
            raise_exception(kInterruptCauseBit | irq_num, 0);
        }
    }
}

template <bool kCopyContext, bool kInstMix>
void CPU::execute_cached_op_fast(Machine& machine, CachedOp& op,
                                 const simrv::memory::RamView& ram) {
    if constexpr (kCopyContext) {
        op.copy_to(pipeline_context);
        pipeline_context.tlb_miss = false;
        pipeline_context.pending_tval = 0;
    } else {
        pipeline_context.pending_exception = std::nullopt;
    }

    if constexpr (kInstMix) {
        e_instmix[static_cast<std::size_t>(op.op_id)]++;
    }

    // 3. Execute, Memory, Writeback, Commit - single flat op_id dispatch (no double switch).
    // ALU arms compute wb_data and break to the shared writeback tail below the switch.
    // Non-ALU arms (branches, jumps, loads, stores, fallback) handle their own commit and return.
    simrv::xlen::Register wb_data = 0;
    const simrv::xlen::Register rrs1 = state_.regs.read(op.rs1);
    switch (op.op_id) {
        // ---- Scalar ALU with two register operands ----
        case isa::ADD: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 + rrs2;
            break;
        }
        case isa::SUB: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 - rrs2;
            break;
        }
        case isa::AND: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 & rrs2;
            break;
        }
        case isa::OR: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 | rrs2;
            break;
        }
        case isa::XOR: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 ^ rrs2;
            break;
        }
        case isa::CFU: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<simrv::xlen::Register>(
                cfu_unit.execute(op.funct7(), std::to_underlying(op.funct3),
                                 static_cast<uint32_t>(rrs1), static_cast<uint32_t>(rrs2)));
            break;
        }
        case isa::SLL: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            if constexpr (!simrv::xlen::kIsXLen64) {
                wb_data = rrs1 << (rrs2 & 0x1f);
            } else {
                const simrv::xlen::Register shift_mask =
                    simrv::compiler::likely(state_.regs.xlen == 64) ? 0x3f : 0x1f;
                wb_data = rrs1 << (rrs2 & shift_mask);
            }
            break;
        }
        case isa::SRL: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            if constexpr (!simrv::xlen::kIsXLen64) {
                wb_data = rrs1 >> (rrs2 & 0x1f);
            } else {
                if (simrv::compiler::likely(state_.regs.xlen == 64)) {
                    wb_data = rrs1 >> (rrs2 & 0x3f);
                } else {
                    wb_data = static_cast<uint32_t>(rrs1) >> (rrs2 & 0x1f);
                }
            }
            break;
        }
        case isa::SRA: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            if constexpr (!simrv::xlen::kIsXLen64) {
                wb_data = static_cast<simrv::xlen::Register>(
                    static_cast<simrv::xlen::SignedWord>(rrs1) >> (rrs2 & 0x1f));
            } else {
                const simrv::xlen::Register shift_mask =
                    simrv::compiler::likely(state_.regs.xlen == 64) ? 0x3f : 0x1f;
                wb_data = static_cast<simrv::xlen::Register>(
                    static_cast<simrv::xlen::SignedWord>(rrs1) >> (rrs2 & shift_mask));
            }
            break;
        }
        case isa::SLT: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data =
                static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(rrs1) <
                                                   static_cast<simrv::xlen::SignedWord>(rrs2));
            break;
        }
        case isa::SLTU: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<simrv::xlen::Register>(rrs1 < rrs2);
            break;
        }
        // ---- 32-bit (W) register ops (RV64 only) ----
        case isa::ADDW: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<simrv::xlen::Register>(
                static_cast<simrv::xlen::SignedWord>(static_cast<int32_t>(rrs1 + rrs2)));
            break;
        }
        case isa::SUBW: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<simrv::xlen::Register>(
                static_cast<simrv::xlen::SignedWord>(static_cast<int32_t>(rrs1 - rrs2)));
            break;
        }
        case isa::SLLW: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
                static_cast<int32_t>(static_cast<uint32_t>(rrs1) << (rrs2 & 0x1f))));
            break;
        }
        case isa::SRLW: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
                static_cast<int32_t>(static_cast<uint32_t>(rrs1) >> (rrs2 & 0x1f))));
            break;
        }
        case isa::SRAW: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<simrv::xlen::Register>(
                static_cast<simrv::xlen::SignedWord>(static_cast<int32_t>(rrs1) >> (rrs2 & 0x1f)));
            break;
        }
        // ---- Scalar ALU with immediate ----
        case isa::ADDI:
            wb_data = rrs1 + op.imm;
            break;
        case isa::ANDI:
            wb_data = rrs1 & op.imm;
            break;
        case isa::ORI:
            wb_data = rrs1 | op.imm;
            break;
        case isa::XORI:
            wb_data = rrs1 ^ op.imm;
            break;
        case isa::SLLI:
            if constexpr (!simrv::xlen::kIsXLen64) {
                wb_data = rrs1 << (op.imm & 0x1f);
            } else {
                const simrv::xlen::Register shift_mask =
                    simrv::compiler::likely(state_.regs.xlen == 64) ? 0x3f : 0x1f;
                wb_data = rrs1 << (op.imm & shift_mask);
            }
            break;
        case isa::SRLI:
            if constexpr (!simrv::xlen::kIsXLen64) {
                wb_data = rrs1 >> (op.imm & 0x1f);
            } else {
                if (simrv::compiler::likely(state_.regs.xlen == 64)) {
                    wb_data = rrs1 >> (op.imm & 0x3f);
                } else {
                    wb_data = static_cast<uint32_t>(rrs1) >> (op.imm & 0x1f);
                }
            }
            break;
        case isa::SRAI:
            if constexpr (!simrv::xlen::kIsXLen64) {
                wb_data = static_cast<simrv::xlen::Register>(
                    static_cast<simrv::xlen::SignedWord>(rrs1) >> (op.imm & 0x1f));
            } else {
                const simrv::xlen::Register shift_mask =
                    simrv::compiler::likely(state_.regs.xlen == 64) ? 0x3f : 0x1f;
                wb_data = static_cast<simrv::xlen::Register>(
                    static_cast<simrv::xlen::SignedWord>(rrs1) >> (op.imm & shift_mask));
            }
            break;
        case isa::SLTI:
            wb_data =
                static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(rrs1) <
                                                   static_cast<simrv::xlen::SignedWord>(op.imm));
            break;
        case isa::SLTIU:
            wb_data = static_cast<simrv::xlen::Register>(
                rrs1 < static_cast<simrv::xlen::Register>(op.imm));
            break;
        // ---- 32-bit immediate ops (RV64 only) ----
        case isa::ADDIW:
            wb_data = static_cast<simrv::xlen::Register>(
                static_cast<simrv::xlen::SignedWord>(static_cast<int32_t>(rrs1 + op.imm)));
            break;
        case isa::SLLIW:
            wb_data = static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
                static_cast<int32_t>(static_cast<uint32_t>(rrs1) << (op.imm & 0x1f))));
            break;
        case isa::SRLIW:
            wb_data = static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
                static_cast<int32_t>(static_cast<uint32_t>(rrs1) >> (op.imm & 0x1f))));
            break;
        case isa::SRAIW:
            wb_data = static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
                static_cast<int32_t>(rrs1) >> (op.imm & 0x1f)));
            break;
        // ---- Upper-immediate (no rs1) ----
        case isa::LUI:
            wb_data = op.imm;
            break;
        case isa::AUIPC:
            wb_data = state_.pc + op.imm;
            break;
        // ---- Branches: handle internally, then return ----
        case isa::BEQ:
        case isa::BNE:
        case isa::BLT:
        case isa::BGE:
        case isa::BLTU:
        case isa::BGEU:
            execute_cached_branch(op, rrs1, state_.regs.read(op.rs2));
            return;
        // ---- Jump instructions ----
        case isa::JAL:
            execute_cached_jal(op);
            return;
        case isa::JALR:
            execute_cached_jalr(op, rrs1);
            return;
        // ---- Loads ----
        case isa::LB:
        case isa::LH:
        case isa::LW:
        case isa::LD:
        case isa::LBU:
        case isa::LHU:
        case isa::LWU:
            execute_cached_load(machine, op, rrs1, ram);
            return;
        // ---- Stores ----
        case isa::SB:
        case isa::SH:
        case isa::SW:
        case isa::SD:
            execute_cached_store(machine, op, rrs1, state_.regs.read(op.rs2), ram);
            return;
        // ---- FP Loads ----
        case isa::FLW:
        case isa::FLD: {
            simrv::xlen::Address const mem_addr = rrs1 + op.imm;
            simrv::xlen::FloatingRegister mem_rdata = 0;
            const unsigned size_bytes = op.mem_size;
            const simrv::xlen::Address align_mask = op.align_mask();
            const bool fast_width_supported =
                simrv::xlen::kIsXLen64 || op.funct3 == isa::Funct3::Flw;
            bool loaded = false;
            if (fast_width_supported &&
                simrv::compiler::likely(state_.priv == kPrivMachine &&
                                        (state_.mstatus & MstatusBit::Mprv) == 0 &&
                                        !state_.has_locked_pmp)) {
                if (simrv::compiler::likely((mem_addr & align_mask) == 0 &&
                                            ram.contains(mem_addr, size_bytes))) {
                    mem_rdata = simrv::memory::ram_read_fast(
                        mem_addr, static_cast<simrv::xlen::Instruction>(op.funct3), ram);
                    loaded = true;
                }
            } else if (fast_width_supported) {
                const simrv::xlen::PrivilegeLevel eff_priv = effective_data_privilege();
                const unsigned active_xlen = state_.xlen_for_privilege(eff_priv);
                simrv::xlen::Address lookup_addr = mem_addr;
                if (active_xlen == 32) lookup_addr &= 0xFFFFFFFFULL;
                const unsigned page_offset = lookup_addr & 0xFFFu;
                if (simrv::compiler::likely(
                        (lookup_addr & align_mask) == 0 && page_offset + size_bytes <= 4096u &&
                        simrv::xlen::satp_translation_enabled(state_.satp, active_xlen))) {
                    const simrv::xlen::Word current_asid =
                        simrv::xlen::satp_asid(state_.satp, active_xlen);
                    const simrv::xlen::Address vpn = lookup_addr >> 12;
                    const size_t tlb_idx = soft_tlb_index(vpn);
                    const auto& entry = soft_tlb_read[tlb_idx];
                    if (simrv::compiler::likely(
                            entry.matches(vpn, current_asid, eff_priv, soft_tlb_epoch))) {
                        const simrv::xlen::Address paddr = (entry.paddr_base + page_offset).raw();
                        if (simrv::compiler::unlikely(state_.num_active_pmp > 0) &&
                            simrv::compiler::unlikely(!core::pmp::check_access(
                                state_, paddr, size_bytes, core::PmpAccessType::Read, eff_priv))) {
                            // Let fallback raise the architectural fault.
                        } else if (simrv::compiler::likely(entry.has_host_ptr())) {
                            mem_rdata = simrv::memory::host_read_fast(
                                entry.host_ptr(lookup_addr),
                                static_cast<simrv::xlen::Instruction>(op.funct3));
                            loaded = true;
                        }
                    }
                }
            }
            if (!loaded) {
                simrv::xlen::Word raw_val = 0;
                if (fast_width_supported &&
                    try_fast_load(machine, mem_addr, op.funct3, raw_val, ram)) {
                    mem_rdata = raw_val;
                } else {
                    op.copy_to(pipeline_context);
                    pipeline_context.mem_addr = mem_addr;
                    mem_rdata = simrv::memory::MemoryAccess::loadFp(machine.memory_, *this,
                                                                    mem_addr, op.funct3);
                    if (pipeline_context.pending_exception.has_value()) {
                        raise_exception(static_cast<simrv::xlen::TrapCause>(
                                            *pipeline_context.pending_exception),
                                        pipeline_context.pending_tval);
                        return;
                    }
                }
            }
            state_.regs.write_fp(
                simrv::xlen::to_fp_reg_id(op.rd),
                (op.funct3 == isa::Funct3::Flw)
                    ? (static_cast<simrv::xlen::FloatingRegister>(simrv::xlen::kF32BoxerBits) |
                       (mem_rdata & 0xFFFFFFFFULL))
                    : mem_rdata);
            state_.mstatus |= MstatusBit::Fs;
            advance_cached_pc(op);
            return;
        }
        // ---- FP Stores ----
        case isa::FSW:
        case isa::FSD: {
            simrv::xlen::Address const mem_addr = rrs1 + op.imm;
            simrv::xlen::FloatingRegister const fp_data =
                state_.regs.read_fp(simrv::xlen::to_fp_reg_id(op.rs2));
            const unsigned size_bytes = op.mem_size;
            const simrv::xlen::Address align_mask = op.align_mask();
            const bool fast_width_supported =
                simrv::xlen::kIsXLen64 || op.funct3 == isa::Funct3::Fsw;
            bool stored = false;
            if (fast_width_supported &&
                simrv::compiler::likely(state_.priv == kPrivMachine &&
                                        (state_.mstatus & MstatusBit::Mprv) == 0 &&
                                        !state_.has_locked_pmp)) {
                if (simrv::compiler::likely((mem_addr & align_mask) == 0 &&
                                            ram.contains(mem_addr, size_bytes) &&
                                            !is_tohost_addr(machine, mem_addr))) {
                    simrv::memory::ram_write_fast(
                        mem_addr, fp_data, static_cast<simrv::xlen::Instruction>(op.funct3), ram);
                    pipeline_context.mem_paddr = mem_addr;
                    stored = true;
                }
            } else if (fast_width_supported) {
                const simrv::xlen::PrivilegeLevel eff_priv = effective_data_privilege();
                const unsigned active_xlen = state_.xlen_for_privilege(eff_priv);
                simrv::xlen::Address lookup_addr = mem_addr;
                if (active_xlen == 32) lookup_addr &= 0xFFFFFFFFULL;
                const unsigned page_offset = lookup_addr & 0xFFFu;
                if (simrv::compiler::likely(
                        (lookup_addr & align_mask) == 0 && page_offset + size_bytes <= 4096u &&
                        simrv::xlen::satp_translation_enabled(state_.satp, active_xlen))) {
                    const simrv::xlen::Word current_asid =
                        simrv::xlen::satp_asid(state_.satp, active_xlen);
                    const simrv::xlen::Address vpn = lookup_addr >> 12;
                    const size_t tlb_idx = soft_tlb_index(vpn);
                    const auto& entry = soft_tlb_write[tlb_idx];
                    if (simrv::compiler::likely(
                            entry.matches(vpn, current_asid, eff_priv, soft_tlb_epoch))) {
                        const simrv::xlen::Address paddr = (entry.paddr_base + page_offset).raw();
                        if (simrv::compiler::unlikely(state_.num_active_pmp > 0) &&
                            simrv::compiler::unlikely(!core::pmp::check_access(
                                state_, paddr, size_bytes, core::PmpAccessType::Write, eff_priv))) {
                            // Let fallback raise the architectural fault.
                        } else if (simrv::compiler::likely(entry.has_host_ptr() &&
                                                           !is_tohost_addr(machine, paddr))) {
                            simrv::memory::host_write_fast(
                                entry.host_ptr(lookup_addr), fp_data,
                                static_cast<simrv::xlen::Instruction>(op.funct3));
                            pipeline_context.mem_paddr = paddr;
                            stored = true;
                        }
                    }
                }
            }
            if (!stored) {
                if (!fast_width_supported ||
                    !try_fast_store(machine, mem_addr, op.funct3, fp_data, ram)) {
                    op.copy_to(pipeline_context);
                    pipeline_context.mem_addr = mem_addr;
                    simrv::memory::MemoryAccess::storeFp(machine.memory_, *this, mem_addr, fp_data,
                                                         op.funct3);
                    if (pipeline_context.pending_exception.has_value()) {
                        raise_exception(static_cast<simrv::xlen::TrapCause>(
                                            *pipeline_context.pending_exception),
                                        pipeline_context.pending_tval);
                        return;
                    }
                }
            }
            state_.lr_reservation_active = false;
            if (simrv::compiler::unlikely(
                    machine.memory_.reservation_table().may_have_reservations() &&
                    machine.num_harts() > 1)) {
                machine.memory_.reservation_table().invalidate_range(pipeline_context.mem_paddr,
                                                                     size_bytes, hart_id());
            }
            advance_cached_pc(op);
            return;
        }
        // ---- FP Fused Operations ----
        case isa::FMADD_D:
        case isa::FMADD_S:
        case isa::FMSUB_D:
        case isa::FMSUB_S:
        case isa::FNMSUB_D:
        case isa::FNMSUB_S:
        case isa::FNMADD_D:
        case isa::FNMADD_S: {
            const isa::RoundingMode rm = isa::resolve_rounding_mode(op.funct3, state_.fcsr);
            if (simrv::compiler::unlikely(!isa::is_valid_rounding_mode(rm))) {
                raise_exception(ExceptionCode::IllegalInstruction, op.ir);
                return;
            }
            const auto rs3 = static_cast<simrv::xlen::FpRegId>((op.ir >> 27) & 0x1F);
            const auto fmt = static_cast<FpFmt>((op.ir >> 25) & 0x3);
            const simrv::xlen::CsrValue old_fcsr = state_.fcsr;
            auto res = execute::ExecuteUnit::fusedFp(
                op.opcode(), fmt, simrv::xlen::to_fp_reg_id(op.rs1),
                simrv::xlen::to_fp_reg_id(op.rs2), rs3, static_cast<isa::RoundingMode>(op.funct3),
                state_.regs.fp_data_ptr(), state_.fcsr);
            if (res.fp_wb_enable) {
                state_.regs.write_fp(simrv::xlen::to_fp_reg_id(op.rd), res.fp_wb_data);
                state_.mstatus |= MstatusBit::Fs;
            }
            if (state_.fcsr != old_fcsr) state_.mstatus |= MstatusBit::Fs;
            advance_cached_pc(op);
            return;
        }
        // ---- FP Scalar Arithmetic ----
        case isa::FADD_D:
        case isa::FADD_S:
        case isa::FSUB_D:
        case isa::FSUB_S:
        case isa::FMUL_D:
        case isa::FMUL_S:
        case isa::FDIV_D:
        case isa::FDIV_S: {
            const isa::RoundingMode rm = isa::resolve_rounding_mode(op.funct3, state_.fcsr);
            if (simrv::compiler::unlikely(!isa::is_valid_rounding_mode(rm))) {
                raise_exception(ExceptionCode::IllegalInstruction, op.ir);
                return;
            }
            const simrv::xlen::CsrValue old_fcsr = state_.fcsr;
            auto res = execute::ExecuteUnit::opFp(
                op.funct7(), op.funct3, simrv::xlen::to_fp_reg_id(op.rs1),
                simrv::xlen::to_fp_reg_id(op.rs2), rrs1, state_.regs.fp_data_ptr(), state_.fcsr);
            if (res.fp_wb_enable) {
                state_.regs.write_fp(simrv::xlen::to_fp_reg_id(op.rd), res.fp_wb_data);
                state_.mstatus |= MstatusBit::Fs;
            }
            if (state_.fcsr != old_fcsr) state_.mstatus |= MstatusBit::Fs;
            advance_cached_pc(op);
            return;
        }
        // WFI deliberately reaches the full path: fetch is the interrupt-delivery boundary, and
        // bypassing it in a cached idle loop can leave an OS waiting past a pending timer IRQ.
        // ---- Everything else: fallback to full pipeline stages ----
        default:
            if (simrv::isa::is_vector_op(op.op_id)) {
                if constexpr (!kCopyContext) {
                    op.copy_to(pipeline_context);
                    pipeline_context.tlb_miss = false;
                    pipeline_context.pending_tval = 0;
                }
                execute::ExecuteUnit::execute_vector(*this, machine, op.op_id, op.ir);
                state_.mstatus |= MstatusBit::Vs;
                if (pipeline_context.pending_exception.has_value()) {
                    raise_exception(
                        static_cast<simrv::xlen::TrapCause>(*pipeline_context.pending_exception),
                        pipeline_context.pending_tval);
                    return;
                }
                advance_cached_pc(op);
                return;
            }
            if constexpr (!kCopyContext) {
                op.copy_to(pipeline_context);
                pipeline_context.tlb_miss = false;
                pipeline_context.pending_tval = 0;
            }
            execute_cached_fallback(machine);
            return;
    }

    // Shared writeback tail - only ALU arms (those that break) reach here.
    // Branchless GPR write: write rd unconditionally, then re-zero x0.
    state_.regs.write_branchless(op.rd, wb_data);
    advance_cached_pc_alu(op);
}

template <bool kCopyContext, bool kInstMix, bool kPollPause>
SIMRV_ALWAYS_INLINE auto CPU::run_fast_baremetal_kernel(Machine& machine, uint32_t batch_size,
                                                        const simrv::memory::RamView& ram)
    -> uint32_t {
    uint32_t cached_ops = 0;
    simrv::xlen::Counter last_flushed_icount = e_icount;
    uint32_t b = 0;
    while (b < batch_size) {
        auto* cached = decode_cache.lookup(state_.pc);
        if (simrv::compiler::likely(cached != nullptr)) {
            execute_cached_op_fast<kCopyContext, kInstMix>(machine, *cached, ram);
            ++cached_ops;
            ++b;

            // Straight-line instruction chaining: execute consecutive non-trapping,
            // non-control ALU operations in a tight inner loop without re-checking
            // outer boundaries until a 64-instruction boundary or control flow/miss is reached.
            while (cached->is_straight_alu && b < batch_size && ((b & 0x3Fu) != 0) &&
                   simrv::compiler::likely((state_.mip & state_.mie) == 0u)) {
                auto* next_cached = decode_cache.lookup(state_.pc);
                if (simrv::compiler::unlikely(next_cached == nullptr ||
                                              !next_cached->is_straight_alu)) {
                    break;
                }
                cached = next_cached;
                execute_cached_op_fast<kCopyContext, kInstMix>(machine, *cached, ram);
                ++cached_ops;
                ++b;
            }
            if constexpr (simrv::xlen::kIsXLen64) {
                if (simrv::compiler::unlikely(state_.regs.xlen == 32)) {
                    state_.canonicalize_pc();
                }
            }
            handle_cached_interrupts();
        } else {
            const auto delta = e_icount - last_flushed_icount;
            if (delta > 0) {
                machine.record_retired_instructions(delta);
            }
            run_cycle_baremetal_miss(machine);
            last_flushed_icount = e_icount;
            if (simrv::compiler::unlikely(machine.tohost != 0)) {
                break;
            }
            ++b;
        }
        if (simrv::compiler::unlikely((b & 0x3Fu) == 0)) {
            if (machine.tohost != 0 || !machine.is_running() ||
                (kPollPause && (machine.is_paused() || machine.debug_pause_requested()))) {
                break;
            }
        }
    }
    const auto delta = e_icount - last_flushed_icount;
    if (delta > 0) {
        machine.record_retired_instructions(delta);
    }
    if constexpr (simrv::xlen::kIsXLen64) {
        if (simrv::compiler::unlikely(state_.regs.xlen == 32)) {
            state_.canonicalize_pc();
        }
    }
    handle_cached_interrupts();
    return cached_ops;
}

void CPU::run_fast_baremetal_batch(Machine& machine, uint32_t batch_size,
                                   const FastBatchPolicy& policy) {
    apply_hart_control_boundary();
    apply_pending_interrupt_lines(machine);
    CPU* previous_active_cpu = active_execution_cpu_;
    active_execution_cpu_ = this;
    struct BoundaryCommit {
        CPU& cpu;
        CPU* previous;
        ~BoundaryCommit() {
            cpu.apply_hart_control_boundary();
            active_execution_cpu_ = previous;
        }
    } boundary_commit{.cpu = *this, .previous = previous_active_cpu};
    const auto ram = machine.ram_view();
    uint32_t cached_ops = 0;
    if (simrv::compiler::likely(!policy.copy_pipeline_context && !policy.collect_instruction_mix)) {
        cached_ops = policy.poll_pause
                         ? run_fast_baremetal_kernel<false, false, true>(machine, batch_size, ram)
                         : run_fast_baremetal_kernel<false, false, false>(machine, batch_size, ram);
    } else if (policy.copy_pipeline_context && policy.collect_instruction_mix) {
        cached_ops = policy.poll_pause
                         ? run_fast_baremetal_kernel<true, true, true>(machine, batch_size, ram)
                         : run_fast_baremetal_kernel<true, true, false>(machine, batch_size, ram);
    } else if (policy.copy_pipeline_context) {
        cached_ops = policy.poll_pause
                         ? run_fast_baremetal_kernel<true, false, true>(machine, batch_size, ram)
                         : run_fast_baremetal_kernel<true, false, false>(machine, batch_size, ram);
    } else {
        cached_ops = policy.poll_pause
                         ? run_fast_baremetal_kernel<false, true, true>(machine, batch_size, ram)
                         : run_fast_baremetal_kernel<false, true, false>(machine, batch_size, ram);
    }

    if (cached_ops > 0) {
        clint_mmio.mcycle += cached_ops;
        clint_mmio.rtc_divider += cached_ops;
        if (clint_mmio.rtc_divider >= timing::kCyclesPerTimebaseTick) {
            clint_mmio.mtime += clint_mmio.rtc_divider / timing::kCyclesPerTimebaseTick;
            clint_mmio.rtc_divider %= timing::kCyclesPerTimebaseTick;
            evaluate_timer_interrupt();
        }
    }
}

template <bool kCopyContext, bool kInstMix, bool kPollPause>
SIMRV_ALWAYS_INLINE auto CPU::run_fast_os_kernel(Machine& machine, uint32_t batch_size,
                                                 const simrv::memory::RamView& ram) -> uint32_t {
    uint32_t cached_ops = 0;
    simrv::xlen::Counter last_flushed_icount = e_icount;
    uint32_t chunk_cycles = 0;

    auto flush_clint_and_retired = [&]() {
        if (chunk_cycles > 0) {
            clint_mmio.mcycle += chunk_cycles;
            clint_mmio.rtc_divider += chunk_cycles;
            if (clint_mmio.rtc_divider >= timing::kCyclesPerTimebaseTick) {
                clint_mmio.mtime += clint_mmio.rtc_divider / timing::kCyclesPerTimebaseTick;
                clint_mmio.rtc_divider %= timing::kCyclesPerTimebaseTick;
                evaluate_timer_interrupt();
                handle_cached_interrupts();
            }
            chunk_cycles = 0;
        }
        const auto delta = e_icount - last_flushed_icount;
        if (delta > 0) {
            machine.record_retired_instructions(delta);
            last_flushed_icount = e_icount;
        }
    };

    uint32_t b = 0;
    while (b < batch_size) {
        auto* cached = decode_cache.lookup(state_.pc);
        if (simrv::compiler::likely(cached != nullptr)) {
            execute_cached_op_fast<kCopyContext, kInstMix>(machine, *cached, ram);
            ++cached_ops;
            ++chunk_cycles;
            ++b;

            // Straight-line instruction chaining: execute consecutive non-trapping,
            // non-control ALU operations until next 64-cycle chunk or control flow/miss.
            while (cached->is_straight_alu && b < batch_size && chunk_cycles < 64 &&
                   simrv::compiler::likely((state_.mip & state_.mie) == 0u)) {
                auto* next_cached = decode_cache.lookup(state_.pc);
                if (simrv::compiler::unlikely(next_cached == nullptr ||
                                              !next_cached->is_straight_alu)) {
                    break;
                }
                cached = next_cached;
                execute_cached_op_fast<kCopyContext, kInstMix>(machine, *cached, ram);
                ++cached_ops;
                ++chunk_cycles;
                ++b;
            }
            if constexpr (simrv::xlen::kIsXLen64) {
                if (simrv::compiler::unlikely(state_.regs.xlen == 32)) {
                    state_.canonicalize_pc();
                }
            }
            handle_cached_interrupts();

            if (simrv::compiler::unlikely(chunk_cycles >= 64)) {
                flush_clint_and_retired();
            }
        } else {
            flush_clint_and_retired();
            run_fast_cycle_miss(machine);
            last_flushed_icount = e_icount;
            if (simrv::compiler::unlikely(machine.tohost != 0)) {
                break;
            }
            ++b;
        }
        if (simrv::compiler::unlikely((b & 0x3Fu) == 0)) {
            if (machine.tohost != 0 || !machine.is_running() ||
                (kPollPause && (machine.is_paused() || machine.debug_pause_requested()))) {
                break;
            }
        }
    }
    flush_clint_and_retired();
    if constexpr (simrv::xlen::kIsXLen64) {
        if (simrv::compiler::unlikely(state_.regs.xlen == 32)) {
            state_.canonicalize_pc();
        }
    }
    return cached_ops;
}

void CPU::run_fast_os_batch(Machine& machine, uint32_t batch_size, const FastBatchPolicy& policy) {
    apply_hart_control_boundary();
    apply_pending_interrupt_lines(machine);
    CPU* previous_active_cpu = active_execution_cpu_;
    active_execution_cpu_ = this;
    struct BoundaryCommit {
        CPU& cpu;
        CPU* previous;
        ~BoundaryCommit() {
            cpu.apply_hart_control_boundary();
            active_execution_cpu_ = previous;
        }
    } boundary_commit{.cpu = *this, .previous = previous_active_cpu};

    const auto ram = machine.ram_view();
    if (simrv::compiler::likely(!policy.copy_pipeline_context && !policy.collect_instruction_mix)) {
        if (policy.poll_pause) {
            run_fast_os_kernel<false, false, true>(machine, batch_size, ram);
        } else {
            run_fast_os_kernel<false, false, false>(machine, batch_size, ram);
        }
    } else if (policy.copy_pipeline_context && policy.collect_instruction_mix) {
        if (policy.poll_pause) {
            run_fast_os_kernel<true, true, true>(machine, batch_size, ram);
        } else {
            run_fast_os_kernel<true, true, false>(machine, batch_size, ram);
        }
    } else if (policy.copy_pipeline_context) {
        if (policy.poll_pause) {
            run_fast_os_kernel<true, false, true>(machine, batch_size, ram);
        } else {
            run_fast_os_kernel<true, false, false>(machine, batch_size, ram);
        }
    } else {
        if (policy.poll_pause) {
            run_fast_os_kernel<false, true, true>(machine, batch_size, ram);
        } else {
            run_fast_os_kernel<false, true, false>(machine, batch_size, ram);
        }
    }
}

template void CPU::execute_cached_op_fast<false, false>(Machine&, CachedOp&,
                                                        const simrv::memory::RamView&);
template void CPU::execute_cached_op_fast<true, false>(Machine&, CachedOp&,
                                                       const simrv::memory::RamView&);
template void CPU::execute_cached_op_fast<false, true>(Machine&, CachedOp&,
                                                       const simrv::memory::RamView&);
template void CPU::execute_cached_op_fast<true, true>(Machine&, CachedOp&,
                                                      const simrv::memory::RamView&);
template auto CPU::run_fast_baremetal_kernel<false, false, false>(Machine&, uint32_t,
                                                                  const simrv::memory::RamView&)
    -> uint32_t;
template auto CPU::run_fast_baremetal_kernel<false, false, true>(Machine&, uint32_t,
                                                                 const simrv::memory::RamView&)
    -> uint32_t;
template auto CPU::run_fast_baremetal_kernel<true, false, false>(Machine&, uint32_t,
                                                                 const simrv::memory::RamView&)
    -> uint32_t;
template auto CPU::run_fast_baremetal_kernel<true, false, true>(Machine&, uint32_t,
                                                                const simrv::memory::RamView&)
    -> uint32_t;
template auto CPU::run_fast_baremetal_kernel<false, true, false>(Machine&, uint32_t,
                                                                 const simrv::memory::RamView&)
    -> uint32_t;
template auto CPU::run_fast_baremetal_kernel<false, true, true>(Machine&, uint32_t,
                                                                const simrv::memory::RamView&)
    -> uint32_t;
template auto CPU::run_fast_baremetal_kernel<true, true, false>(Machine&, uint32_t,
                                                                const simrv::memory::RamView&)
    -> uint32_t;
template auto CPU::run_fast_baremetal_kernel<true, true, true>(Machine&, uint32_t,
                                                               const simrv::memory::RamView&)
    -> uint32_t;
template auto CPU::run_fast_os_kernel<false, false, false>(Machine&, uint32_t,
                                                           const simrv::memory::RamView&)
    -> uint32_t;
template auto CPU::run_fast_os_kernel<false, false, true>(Machine&, uint32_t,
                                                          const simrv::memory::RamView&)
    -> uint32_t;
template auto CPU::run_fast_os_kernel<true, false, false>(Machine&, uint32_t,
                                                          const simrv::memory::RamView&)
    -> uint32_t;
template auto CPU::run_fast_os_kernel<true, false, true>(Machine&, uint32_t,
                                                         const simrv::memory::RamView&) -> uint32_t;
template auto CPU::run_fast_os_kernel<false, true, false>(Machine&, uint32_t,
                                                          const simrv::memory::RamView&)
    -> uint32_t;
template auto CPU::run_fast_os_kernel<false, true, true>(Machine&, uint32_t,
                                                         const simrv::memory::RamView&) -> uint32_t;
template auto CPU::run_fast_os_kernel<true, true, false>(Machine&, uint32_t,
                                                         const simrv::memory::RamView&) -> uint32_t;
template auto CPU::run_fast_os_kernel<true, true, true>(Machine&, uint32_t,
                                                        const simrv::memory::RamView&) -> uint32_t;

}  // namespace simrv::core
