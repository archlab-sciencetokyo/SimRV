/**
 * @file CpuFastExecution.cpp
 * @brief Cached instruction execution and optimized memory access paths.
 */
#include <array>
#include <limits>

#include "CpuExecutionHelpers.hpp"
#include "simrv/CompilerHints.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/PlatformTiming.hpp"
#include "simrv/core/Pmp.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/debug/GdbStub.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/execute/ExecuteUnitZb.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Math.hpp"
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
    state_.regs.write(op.rd, next_pc);
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
    state_.regs.write(op.rd, next_pc);
    commit_cached_branch_target(op, pipeline_context.jmp_pc);
}

template <isa::OperationId kBranchOp, bool kCopyContext>
SIMRV_ALWAYS_INLINE void CPU::execute_cached_branch(CachedOp& op, simrv::xlen::Register rrs1,
                                                    simrv::xlen::Register rrs2) {
    static_assert(kBranchOp == isa::BEQ || kBranchOp == isa::BNE || kBranchOp == isa::BLT ||
                  kBranchOp == isa::BGE || kBranchOp == isa::BLTU || kBranchOp == isa::BGEU);
    bool tkn;
    if constexpr (kBranchOp == isa::BEQ) {
        tkn = rrs1 == rrs2;
    } else if constexpr (kBranchOp == isa::BNE) {
        tkn = rrs1 != rrs2;
    } else if constexpr (kBranchOp == isa::BLT) {
        tkn =
            static_cast<simrv::xlen::SignedWord>(rrs1) < static_cast<simrv::xlen::SignedWord>(rrs2);
    } else if constexpr (kBranchOp == isa::BGE) {
        tkn = static_cast<simrv::xlen::SignedWord>(rrs1) >=
              static_cast<simrv::xlen::SignedWord>(rrs2);
    } else if constexpr (kBranchOp == isa::BLTU) {
        if constexpr (!simrv::xlen::kIsXLen64) {
            tkn = static_cast<uint32_t>(rrs1) < static_cast<uint32_t>(rrs2);
        } else if (simrv::compiler::likely(state_.regs.xlen == 32)) {
            tkn = static_cast<uint32_t>(rrs1) < static_cast<uint32_t>(rrs2);
        } else {
            tkn = rrs1 < rrs2;
        }
    } else {
        if constexpr (!simrv::xlen::kIsXLen64) {
            tkn = static_cast<uint32_t>(rrs1) >= static_cast<uint32_t>(rrs2);
        } else if (simrv::compiler::likely(state_.regs.xlen == 32)) {
            tkn = static_cast<uint32_t>(rrs1) >= static_cast<uint32_t>(rrs2);
        } else {
            tkn = rrs1 >= rrs2;
        }
    }
    simrv::xlen::VirtAddr const target_pc = tkn ? simrv::xlen::VirtAddr{state_.pc + op.imm}
                                                : simrv::xlen::VirtAddr{state_.pc + op.len()};
    if constexpr (kCopyContext) {
        pipeline_context.tkn = tkn;
        pipeline_context.jmp_pc = target_pc;
    }
    if (simrv::compiler::unlikely(tkn && (target_pc & state_.fetch_align_mask) != 0)) {
        if constexpr (!kCopyContext) {
            pipeline_context.tkn = tkn;
            pipeline_context.jmp_pc = target_pc;
        }
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
        if constexpr (simrv::xlen::kIsXLen64) {
            if (simrv::compiler::unlikely(state_.regs.xlen == 32)) mem_addr &= 0xFFFFFFFFULL;
        }
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
            if (simrv::compiler::likely(entry.has_host_ptr())) {
                out_val = simrv::memory::host_read_fast(
                    entry.host_ptr(mem_addr), static_cast<simrv::xlen::Instruction>(funct3));
                return true;
            }
            const simrv::xlen::Address paddr = (entry.paddr_base + (mem_addr & 0xFFF)).raw();
            if (simrv::compiler::unlikely(state_.num_active_pmp > 0 &&
                                          !core::pmp::check_access(state_, paddr, size_bytes,
                                                                   core::PmpAccessType::Read,
                                                                   eff_priv))) {
                return false;
            }
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
                (ram.contains(ppage, 4096) &&
                 (state_.num_active_pmp == 0 ||
                  core::pmp::check_access(state_, ppage, 4096, core::PmpAccessType::Read,
                                          eff_priv)))
                    ? ram.unchecked_ptr(ppage)
                    : nullptr;
            soft_tlb_read[tlb_idx].set(vpn, current_asid, eff_priv, soft_tlb_epoch, ppage,
                                       host_base, tlb_e->global);
            if (host_base != nullptr) {
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
        if constexpr (simrv::xlen::kIsXLen64) {
            if (simrv::compiler::unlikely(state_.regs.xlen == 32)) mem_addr &= 0xFFFFFFFFULL;
        }
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
            if (simrv::compiler::unlikely(is_tohost_addr(machine, paddr))) {
                return false;
            }
            if (simrv::compiler::likely(entry.has_host_ptr())) {
                simrv::memory::host_write_fast(entry.host_ptr(mem_addr), rrs2,
                                               static_cast<simrv::xlen::Instruction>(funct3));
                pipeline_context.mem_paddr = paddr;
                return true;
            }
            if (simrv::compiler::unlikely(state_.num_active_pmp > 0 &&
                                          !core::pmp::check_access(state_, paddr, size_bytes,
                                                                   core::PmpAccessType::Write,
                                                                   eff_priv))) {
                return false;
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
                (ram.contains(ppage, 4096) &&
                 (state_.num_active_pmp == 0 ||
                  core::pmp::check_access(state_, ppage, 4096, core::PmpAccessType::Write,
                                          eff_priv)))
                    ? ram.unchecked_ptr(ppage)
                    : nullptr;
            soft_tlb_write[tlb_idx].set(vpn, current_asid, eff_priv, soft_tlb_epoch, ppage,
                                        host_base, tlb_e->global);
            if (host_base != nullptr) {
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

template <isa::OperationId kLoadOp>
SIMRV_ALWAYS_INLINE auto CPU::execute_cached_load(Machine& machine, CachedOp& op,
                                                  simrv::xlen::Register rrs1,
                                                  const simrv::memory::RamView& ram) -> bool {
    static_assert(kLoadOp == isa::LB || kLoadOp == isa::LH || kLoadOp == isa::LW ||
                  kLoadOp == isa::LD || kLoadOp == isa::LBU || kLoadOp == isa::LHU ||
                  kLoadOp == isa::LWU);
    constexpr isa::Funct3 kFunct3 = [] {
        if constexpr (kLoadOp == isa::LB)
            return isa::Funct3::Lb;
        else if constexpr (kLoadOp == isa::LH)
            return isa::Funct3::Lh;
        else if constexpr (kLoadOp == isa::LW)
            return isa::Funct3::Lw;
        else if constexpr (kLoadOp == isa::LD)
            return isa::Funct3::Ld;
        else if constexpr (kLoadOp == isa::LBU)
            return isa::Funct3::Lbu;
        else if constexpr (kLoadOp == isa::LHU)
            return isa::Funct3::Lhu;
        else
            return isa::Funct3::Lwu;
    }();
    constexpr unsigned kSizeBytes = 1u << (std::to_underlying(kFunct3) & 0x3u);
    simrv::xlen::Address const mem_addr = rrs1 + op.imm;
    if (simrv::compiler::unlikely(machine.tui_enabled() || machine.branch_trace_enabled())) {
        pipeline_context.mem_addr = mem_addr;
    }
    simrv::xlen::Register mem_rdata = 0;
    constexpr unsigned size_bytes = kSizeBytes;
    constexpr simrv::xlen::Address align_mask = kSizeBytes - 1u;
    if (simrv::compiler::likely(state_.priv == kPrivMachine &&
                                (state_.mstatus & MstatusBit::Mprv) == 0 &&
                                !state_.has_locked_pmp)) {
        simrv::xlen::Address phys_addr = mem_addr;
        if constexpr (simrv::xlen::kIsXLen64) {
            if (simrv::compiler::unlikely(state_.regs.xlen == 32)) phys_addr &= 0xFFFFFFFFULL;
        }
        if (simrv::compiler::likely((phys_addr & align_mask) == 0 &&
                                    ram.contains(phys_addr, size_bytes))) {
            mem_rdata = simrv::memory::ram_read_fast(phys_addr, kFunct3, ram);
            state_.regs.write(op.rd, mem_rdata);
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
                if (simrv::compiler::likely(entry.has_host_ptr())) {
                    mem_rdata = simrv::memory::host_read_fast(entry.host_ptr(lookup_addr), kFunct3);
                    state_.regs.write(op.rd, mem_rdata);
                    advance_cached_pc(op);
                    return true;
                }
            } else if (TLBEntry* tlb_e = tlb.lookup_data_r(lookup_addr, current_asid, eff_priv);
                       simrv::compiler::likely(tlb_e != nullptr)) {
                const simrv::xlen::Address paddr = tlb_e->p_addr + page_offset;
                if (simrv::compiler::likely(state_.num_active_pmp == 0 ||
                                            core::pmp::check_access(state_, paddr, size_bytes,
                                                                    core::PmpAccessType::Read,
                                                                    eff_priv))) {
                    const simrv::xlen::Address ppage = tlb_e->p_addr;
                    simrv::xlen::Byte* const host_base =
                        (ram.contains(ppage, 4096) &&
                         (state_.num_active_pmp == 0 ||
                          core::pmp::check_access(state_, ppage, 4096, core::PmpAccessType::Read,
                                                  eff_priv)))
                            ? ram.unchecked_ptr(ppage)
                            : nullptr;
                    soft_tlb_read[tlb_idx].set(vpn, current_asid, eff_priv, soft_tlb_epoch, ppage,
                                               host_base, tlb_e->global);
                    if (simrv::compiler::likely(host_base != nullptr)) {
                        mem_rdata = simrv::memory::host_read_fast(host_base + page_offset, kFunct3);
                        state_.regs.write(op.rd, mem_rdata);
                        advance_cached_pc(op);
                        return true;
                    }
                }
            }
        }
    }

    if (!try_fast_load(machine, mem_addr, kFunct3, mem_rdata, ram)) {
        op.copy_to(pipeline_context);
        pipeline_context.mem_addr = mem_addr;
        mem_rdata = simrv::memory::MemoryAccess::loadInt(machine.memory_, *this, mem_addr, kFunct3);
        if (pipeline_context.pending_exception.has_value()) {
            raise_exception(
                static_cast<simrv::xlen::TrapCause>(*pipeline_context.pending_exception),
                pipeline_context.pending_tval);
            return false;
        }
    }

    state_.regs.write(op.rd, mem_rdata);
    advance_cached_pc(op);
    return true;
}

template <isa::OperationId kStoreOp>
SIMRV_ALWAYS_INLINE auto CPU::execute_cached_store(Machine& machine, CachedOp& op,
                                                   simrv::xlen::Register rrs1,
                                                   simrv::xlen::Register rrs2,
                                                   const simrv::memory::RamView& ram) -> bool {
    static_assert(kStoreOp == isa::SB || kStoreOp == isa::SH || kStoreOp == isa::SW ||
                  kStoreOp == isa::SD);
    constexpr isa::Funct3 kFunct3 = [] {
        if constexpr (kStoreOp == isa::SB)
            return isa::Funct3::Sb;
        else if constexpr (kStoreOp == isa::SH)
            return isa::Funct3::Sh;
        else if constexpr (kStoreOp == isa::SW)
            return isa::Funct3::Sw;
        else
            return isa::Funct3::Sd;
    }();
    constexpr unsigned kSizeBytes = 1u << (std::to_underlying(kFunct3) & 0x3u);
    constexpr simrv::xlen::Address kAlignMask = kSizeBytes - 1u;
    simrv::xlen::Address const mem_addr = rrs1 + op.imm;
    if (simrv::compiler::unlikely(machine.tui_enabled() || machine.branch_trace_enabled())) {
        pipeline_context.mem_addr = mem_addr;
    }
    constexpr unsigned size_bytes = kSizeBytes;
    if (simrv::compiler::likely(state_.priv == kPrivMachine &&
                                (state_.mstatus & MstatusBit::Mprv) == 0 &&
                                !state_.has_locked_pmp)) {
        simrv::xlen::Address phys_addr = mem_addr;
        if constexpr (simrv::xlen::kIsXLen64) {
            if (simrv::compiler::unlikely(state_.regs.xlen == 32)) phys_addr &= 0xFFFFFFFFULL;
        }
        if (simrv::compiler::likely((phys_addr & kAlignMask) == 0 &&
                                    ram.contains(phys_addr, size_bytes) &&
                                    !is_tohost_addr(machine, phys_addr))) {
            simrv::memory::ram_write_fast(phys_addr, rrs2, kFunct3, ram);
            state_.lr_reservation_active = false;
            if (simrv::compiler::unlikely(
                    machine.memory_.reservation_table().may_have_reservations() &&
                    machine.num_harts() > 1)) {
                machine.memory_.reservation_table().invalidate_range(phys_addr, size_bytes,
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
                (lookup_addr & kAlignMask) == 0 && page_offset + size_bytes <= 4096u &&
                simrv::xlen::satp_translation_enabled(state_.satp, active_xlen))) {
            const simrv::xlen::Word current_asid = simrv::xlen::satp_asid(state_.satp, active_xlen);
            const simrv::xlen::Address vpn = lookup_addr >> 12;
            const size_t tlb_idx = soft_tlb_index(vpn);
            const auto& entry = soft_tlb_write[tlb_idx];
            if (simrv::compiler::likely(
                    entry.matches(vpn, current_asid, eff_priv, soft_tlb_epoch))) {
                const simrv::xlen::Address paddr = (entry.paddr_base + page_offset).raw();
                if (simrv::compiler::likely(entry.has_host_ptr() &&
                                            !is_tohost_addr(machine, paddr))) {
                    simrv::memory::host_write_fast(entry.host_ptr(lookup_addr), rrs2,
                                                   static_cast<simrv::xlen::Instruction>(kFunct3));
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
            } else if (TLBEntry* tlb_e = tlb.lookup_data_w(lookup_addr, current_asid, eff_priv);
                       simrv::compiler::likely(tlb_e != nullptr)) {
                const simrv::xlen::Address paddr = tlb_e->p_addr + page_offset;
                if (simrv::compiler::likely(
                        (state_.num_active_pmp == 0 ||
                         core::pmp::check_access(state_, paddr, size_bytes,
                                                 core::PmpAccessType::Write, eff_priv)) &&
                        !is_tohost_addr(machine, paddr))) {
                    const simrv::xlen::Address ppage = tlb_e->p_addr;
                    simrv::xlen::Byte* const host_base =
                        (ram.contains(ppage, 4096) &&
                         (state_.num_active_pmp == 0 ||
                          core::pmp::check_access(state_, ppage, 4096, core::PmpAccessType::Write,
                                                  eff_priv)))
                            ? ram.unchecked_ptr(ppage)
                            : nullptr;
                    soft_tlb_write[tlb_idx].set(vpn, current_asid, eff_priv, soft_tlb_epoch, ppage,
                                                host_base, tlb_e->global);
                    if (simrv::compiler::likely(host_base != nullptr)) {
                        simrv::memory::host_write_fast(
                            host_base + page_offset, rrs2,
                            static_cast<simrv::xlen::Instruction>(kFunct3));
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

    if (!try_fast_store(machine, mem_addr, kFunct3, rrs2, ram)) {
        op.copy_to(pipeline_context);
        pipeline_context.mem_addr = mem_addr;
        simrv::memory::MemoryAccess::storeInt(machine.memory_, *this, mem_addr, rrs2, kFunct3);
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

template <bool kGuestXlen64, bool kCopyContext, bool kInstMix, bool kDeferAluCommit>
SIMRV_ALWAYS_INLINE auto CPU::execute_cached_alu_fast_xlen(CachedOp& op) -> bool {
    return execute_cached_alu_fast_xlen<kGuestXlen64, kCopyContext, kInstMix, kDeferAluCommit>(
        op, op.fast_op);
}

template <bool kGuestXlen64, bool kCopyContext, bool kInstMix, bool kDeferAluCommit>
SIMRV_ALWAYS_INLINE auto CPU::execute_cached_alu_fast_xlen(CachedOp& op, FastOp kind) -> bool {
    simrv::xlen::Register wb_data = 0;
    const simrv::xlen::Register rrs1 = state_.regs.read(op.rs1);

    if (kind == FastAluKind::Add) {
        wb_data = rrs1 + state_.regs.read(op.rs2);
    } else if (kind == FastAluKind::Addi) {
        wb_data = rrs1 + op.imm;
    } else if (kind == FastAluKind::Slli) {
        if constexpr (kGuestXlen64) {
            wb_data = rrs1 << (op.imm & 0x3f);
        } else {
            wb_data = rrs1 << (op.imm & 0x1f);
        }
    } else if (kind == FastAluKind::Srli) {
        if constexpr (kGuestXlen64) {
            wb_data = rrs1 >> (op.imm & 0x3f);
        } else {
            wb_data = static_cast<uint32_t>(rrs1) >> (op.imm & 0x1f);
        }
#if SIMRV_XLEN == 64
    } else if (kind == FastAluKind::Addiw) {
        wb_data = static_cast<simrv::xlen::Register>(
            static_cast<simrv::xlen::SignedWord>(static_cast<int32_t>(rrs1 + op.imm)));
    } else if (kind == FastAluKind::Addw) {
        wb_data = static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
            static_cast<int32_t>(rrs1 + state_.regs.read(op.rs2))));
#endif
    } else if (kind == FastAluKind::Andi) {
        wb_data = rrs1 & op.imm;
    } else {
        switch (kind) {
            case FastAluKind::And:
                wb_data = rrs1 & state_.regs.read(op.rs2);
                break;
            case FastAluKind::Or:
                wb_data = rrs1 | state_.regs.read(op.rs2);
                break;
            case FastAluKind::Sub: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                wb_data = rrs1 - rrs2;
                break;
            }
            case FastAluKind::Xor: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                wb_data = rrs1 ^ rrs2;
                break;
            }
            case FastAluKind::Sll: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                constexpr simrv::xlen::Register shift_mask = kGuestXlen64 ? 0x3f : 0x1f;
                wb_data = rrs1 << (rrs2 & shift_mask);
                break;
            }
            case FastAluKind::Srl: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                if constexpr (kGuestXlen64) {
                    wb_data = rrs1 >> (rrs2 & 0x3f);
                } else {
                    wb_data = static_cast<uint32_t>(rrs1) >> (rrs2 & 0x1f);
                }
                break;
            }
            case FastAluKind::Sra: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                constexpr simrv::xlen::Register shift_mask = kGuestXlen64 ? 0x3f : 0x1f;
                wb_data = static_cast<simrv::xlen::Register>(
                    static_cast<simrv::xlen::SignedWord>(rrs1) >> (rrs2 & shift_mask));
                break;
            }
            case FastAluKind::Slt: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                wb_data =
                    static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(rrs1) <
                                                       static_cast<simrv::xlen::SignedWord>(rrs2));
                break;
            }
            case FastAluKind::Sltu: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                if constexpr (kGuestXlen64) {
                    wb_data = static_cast<simrv::xlen::Register>(rrs1 < rrs2);
                } else {
                    wb_data = static_cast<simrv::xlen::Register>(static_cast<uint32_t>(rrs1) <
                                                                 static_cast<uint32_t>(rrs2));
                }
                break;
            }
            case FastAluKind::Mul: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                wb_data = simrv::xlen::mul_low(rrs1, rrs2);
                break;
            }
            case FastAluKind::Mulh: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                if constexpr (kGuestXlen64) {
                    wb_data = simrv::xlen::mul_high_signed(rrs1, rrs2);
                } else {
                    const int64_t product = static_cast<int64_t>(static_cast<int32_t>(rrs1)) *
                                            static_cast<int64_t>(static_cast<int32_t>(rrs2));
                    wb_data = static_cast<uint32_t>(static_cast<uint64_t>(product) >> 32u);
                }
                break;
            }
            case FastAluKind::Mulhsu: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                if constexpr (kGuestXlen64) {
                    wb_data = simrv::xlen::mul_high_signed_unsigned(rrs1, rrs2);
                } else {
                    const int64_t product = static_cast<int64_t>(static_cast<int32_t>(rrs1)) *
                                            static_cast<int64_t>(static_cast<uint32_t>(rrs2));
                    wb_data = static_cast<uint32_t>(static_cast<uint64_t>(product) >> 32u);
                }
                break;
            }
            case FastAluKind::Mulhu: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                if constexpr (kGuestXlen64) {
                    wb_data = simrv::xlen::mul_high_unsigned(rrs1, rrs2);
                } else {
                    const uint64_t product = static_cast<uint64_t>(static_cast<uint32_t>(rrs1)) *
                                             static_cast<uint64_t>(static_cast<uint32_t>(rrs2));
                    wb_data = static_cast<uint32_t>(product >> 32u);
                }
                break;
            }
            case FastAluKind::Div: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                if constexpr (kGuestXlen64) {
                    const auto lhs = static_cast<simrv::xlen::SignedWord>(rrs1);
                    const auto rhs = static_cast<simrv::xlen::SignedWord>(rrs2);
                    if (rhs == 0) {
                        wb_data = simrv::xlen::kWordAllOnes;
                    } else if (lhs == std::numeric_limits<simrv::xlen::SignedWord>::min() &&
                               rhs == -1) {
                        wb_data = static_cast<simrv::xlen::Register>(lhs);
                    } else {
                        wb_data = static_cast<simrv::xlen::Register>(lhs / rhs);
                    }
                } else {
                    const auto lhs = static_cast<int32_t>(rrs1);
                    const auto rhs = static_cast<int32_t>(rrs2);
                    if (rhs == 0) {
                        wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(-1));
                    } else if (lhs == std::numeric_limits<int32_t>::min() && rhs == -1) {
                        wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(lhs));
                    } else {
                        wb_data =
                            static_cast<simrv::xlen::Register>(static_cast<int64_t>(lhs / rhs));
                    }
                }
                break;
            }
            case FastAluKind::Divu: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                if constexpr (kGuestXlen64) {
                    wb_data = (rrs2 == 0) ? simrv::xlen::kWordAllOnes : (rrs1 / rrs2);
                } else {
                    const auto lhs = static_cast<uint32_t>(rrs1);
                    const auto rhs = static_cast<uint32_t>(rrs2);
                    wb_data = (rhs == 0)
                                  ? static_cast<simrv::xlen::Register>(static_cast<int64_t>(-1))
                                  : static_cast<simrv::xlen::Register>(
                                        static_cast<int64_t>(static_cast<int32_t>(lhs / rhs)));
                }
                break;
            }
            case FastAluKind::Rem: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                if constexpr (kGuestXlen64) {
                    const auto lhs = static_cast<simrv::xlen::SignedWord>(rrs1);
                    const auto rhs = static_cast<simrv::xlen::SignedWord>(rrs2);
                    if (rhs == 0) {
                        wb_data = rrs1;
                    } else if (lhs == std::numeric_limits<simrv::xlen::SignedWord>::min() &&
                               rhs == -1) {
                        wb_data = 0;
                    } else {
                        wb_data = static_cast<simrv::xlen::Register>(lhs % rhs);
                    }
                } else {
                    const auto lhs = static_cast<int32_t>(rrs1);
                    const auto rhs = static_cast<int32_t>(rrs2);
                    if (rhs == 0) {
                        wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(lhs));
                    } else if (lhs == std::numeric_limits<int32_t>::min() && rhs == -1) {
                        wb_data = 0;
                    } else {
                        wb_data =
                            static_cast<simrv::xlen::Register>(static_cast<int64_t>(lhs % rhs));
                    }
                }
                break;
            }
            case FastAluKind::Remu: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                if constexpr (kGuestXlen64) {
                    wb_data = (rrs2 == 0) ? rrs1 : (rrs1 % rrs2);
                } else {
                    const auto lhs = static_cast<uint32_t>(rrs1);
                    const auto rhs = static_cast<uint32_t>(rrs2);
                    wb_data = (rhs == 0) ? static_cast<simrv::xlen::Register>(
                                               static_cast<int64_t>(static_cast<int32_t>(lhs)))
                                         : static_cast<simrv::xlen::Register>(static_cast<int64_t>(
                                               static_cast<int32_t>(lhs % rhs)));
                }
                break;
            }
#if SIMRV_XLEN == 64
            case FastAluKind::Subw: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                wb_data = static_cast<simrv::xlen::Register>(
                    static_cast<simrv::xlen::SignedWord>(static_cast<int32_t>(rrs1 - rrs2)));
                break;
            }
            case FastAluKind::Sllw: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                wb_data = static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
                    static_cast<int32_t>(static_cast<uint32_t>(rrs1) << (rrs2 & 0x1f))));
                break;
            }
            case FastAluKind::Srlw: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                wb_data = static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
                    static_cast<int32_t>(static_cast<uint32_t>(rrs1) >> (rrs2 & 0x1f))));
                break;
            }
            case FastAluKind::Sraw: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                wb_data = static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
                    static_cast<int32_t>(rrs1) >> (rrs2 & 0x1f)));
                break;
            }
            case FastAluKind::Mulw: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                const uint32_t product = static_cast<uint32_t>(rrs1) * static_cast<uint32_t>(rrs2);
                wb_data = static_cast<simrv::xlen::Register>(
                    static_cast<simrv::xlen::SignedWord>(static_cast<int32_t>(product)));
                break;
            }
            case FastAluKind::Divw: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                const auto lhs = static_cast<int32_t>(rrs1);
                const auto rhs = static_cast<int32_t>(rrs2);
                if (rhs == 0) {
                    wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(-1));
                } else if (lhs == std::numeric_limits<int32_t>::min() && rhs == -1) {
                    wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(lhs));
                } else {
                    wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(lhs / rhs));
                }
                break;
            }
            case FastAluKind::Divuw: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                const auto lhs = static_cast<uint32_t>(rrs1);
                const auto rhs = static_cast<uint32_t>(rrs2);
                wb_data = (rhs == 0) ? static_cast<simrv::xlen::Register>(static_cast<int64_t>(-1))
                                     : static_cast<simrv::xlen::Register>(
                                           static_cast<int64_t>(static_cast<int32_t>(lhs / rhs)));
                break;
            }
            case FastAluKind::Remw: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                const auto lhs = static_cast<int32_t>(rrs1);
                const auto rhs = static_cast<int32_t>(rrs2);
                if (rhs == 0) {
                    wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(lhs));
                } else if (lhs == std::numeric_limits<int32_t>::min() && rhs == -1) {
                    wb_data = 0;
                } else {
                    wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(lhs % rhs));
                }
                break;
            }
            case FastAluKind::Remuw: {
                const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
                const auto lhs = static_cast<uint32_t>(rrs1);
                const auto rhs = static_cast<uint32_t>(rrs2);
                wb_data = (rhs == 0) ? static_cast<simrv::xlen::Register>(
                                           static_cast<int64_t>(static_cast<int32_t>(lhs)))
                                     : static_cast<simrv::xlen::Register>(
                                           static_cast<int64_t>(static_cast<int32_t>(lhs % rhs)));
                break;
            }
#endif
            case FastAluKind::Ori:
                wb_data = rrs1 | op.imm;
                break;
            case FastAluKind::Xori:
                wb_data = rrs1 ^ op.imm;
                break;
            case FastAluKind::Srai:
                if constexpr (kGuestXlen64) {
                    wb_data = static_cast<simrv::xlen::Register>(
                        static_cast<simrv::xlen::SignedWord>(rrs1) >> (op.imm & 0x3f));
                } else {
                    wb_data = static_cast<simrv::xlen::Register>(
                        static_cast<simrv::xlen::SignedWord>(rrs1) >> (op.imm & 0x1f));
                }
                break;
            case FastAluKind::Slti:
                wb_data = static_cast<simrv::xlen::Register>(
                    static_cast<simrv::xlen::SignedWord>(rrs1) <
                    static_cast<simrv::xlen::SignedWord>(op.imm));
                break;
            case FastAluKind::Sltiu:
                if constexpr (kGuestXlen64) {
                    wb_data = static_cast<simrv::xlen::Register>(
                        rrs1 < static_cast<simrv::xlen::Register>(op.imm));
                } else {
                    wb_data = static_cast<simrv::xlen::Register>(static_cast<uint32_t>(rrs1) <
                                                                 static_cast<uint32_t>(op.imm));
                }
                break;
#if SIMRV_XLEN == 64
            case FastAluKind::Slliw:
                wb_data = static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
                    static_cast<int32_t>(static_cast<uint32_t>(rrs1) << (op.imm & 0x1f))));
                break;
            case FastAluKind::Srliw:
                wb_data = static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
                    static_cast<int32_t>(static_cast<uint32_t>(rrs1) >> (op.imm & 0x1f))));
                break;
            case FastAluKind::Sraiw:
                wb_data = static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
                    static_cast<int32_t>(rrs1) >> (op.imm & 0x1f)));
                break;
#endif
            case FastAluKind::Lui:
                wb_data = op.imm;
                break;
            case FastAluKind::Auipc:
                wb_data = op.cpc.raw() + op.imm;
                break;
            default: {
                if (kind != FastOp::Bitmanip) {
                    __builtin_unreachable();
                }
                const auto opc = op.opcode();
                const simrv::xlen::Register in2 =
                    (opc == isa::Opcode::OpImm || opc == isa::Opcode::OpImm32)
                        ? static_cast<simrv::xlen::Register>(op.imm)
                        : state_.regs.read(op.rs2);
#if SIMRV_XLEN == 64
                if (opc == isa::Opcode::Op32 || opc == isa::Opcode::OpImm32) {
                    wb_data = execute::ExecuteUnitZb::aluIntZbW(rrs1, in2, op.op_id);
                    break;
                }
#endif
                wb_data =
                    execute::ExecuteUnitZb::aluIntZb(rrs1, in2, op.op_id, kGuestXlen64 ? 64 : 32);
                break;
            }
        }
    }
    if constexpr (kCopyContext) {
        op.copy_to(pipeline_context);
        pipeline_context.tlb_miss = false;
        pipeline_context.pending_tval = 0;
    }
    if constexpr (kInstMix) {
        e_instmix[static_cast<std::size_t>(op.op_id)]++;
    }
    if constexpr (simrv::xlen::kIsXLen64) {
        if constexpr (kGuestXlen64) {
            state_.regs.write_xlen64(op.rd, wb_data);
        } else {
            state_.regs.write_xlen32(op.rd, wb_data);
        }
    } else {
        state_.regs.write_xlen32(op.rd, wb_data);
    }
    if constexpr (!kDeferAluCommit) {
        advance_cached_pc_alu(op);
    }
    return true;
}

template <bool kCopyContext, bool kInstMix, bool kDeferAluCommit>
SIMRV_ALWAYS_INLINE auto CPU::execute_cached_alu_fast(CachedOp& op) -> bool {
    if constexpr (simrv::xlen::kIsXLen64) {
        if (simrv::compiler::likely(state_.regs.xlen == 64)) {
            return execute_cached_alu_fast_xlen<true, kCopyContext, kInstMix, kDeferAluCommit>(op);
        }
    }
    return execute_cached_alu_fast_xlen<false, kCopyContext, kInstMix, kDeferAluCommit>(op);
}

template <bool kCopyContext, bool kInstMix, bool kDeferAluCommit>
void CPU::execute_cached_op_fast(Machine& machine, CachedOp& op,
                                 const simrv::memory::RamView& ram) {
    if (simrv::compiler::unlikely(!op.has_fast_alu() && op.is_straight_alu)) {
        op.set_fast_alu_kind(CachedOp::classify_fast_alu(op.op_id));
    }
    if (op.has_fast_alu() && execute_cached_alu_fast<kCopyContext, kInstMix, kDeferAluCommit>(op)) {
        return;
    }

    if constexpr (kCopyContext) {
        op.copy_to(pipeline_context);
        pipeline_context.tlb_miss = false;
        pipeline_context.pending_tval = 0;
    }

    if constexpr (kInstMix) {
        e_instmix[static_cast<std::size_t>(op.op_id)]++;
    }

    // Control, memory, and uncommon instructions retain their precise cached/fallback paths.
    const simrv::xlen::Register rrs1 = state_.regs.read(op.rs1);
    switch (op.op_id) {
        // ---- Branches: handle internally, then return ----
        case isa::BEQ:
            execute_cached_branch<isa::BEQ, kCopyContext>(op, rrs1, state_.regs.read(op.rs2));
            return;
        case isa::BNE:
            execute_cached_branch<isa::BNE, kCopyContext>(op, rrs1, state_.regs.read(op.rs2));
            return;
        case isa::BLT:
            execute_cached_branch<isa::BLT, kCopyContext>(op, rrs1, state_.regs.read(op.rs2));
            return;
        case isa::BGE:
            execute_cached_branch<isa::BGE, kCopyContext>(op, rrs1, state_.regs.read(op.rs2));
            return;
        case isa::BLTU:
            execute_cached_branch<isa::BLTU, kCopyContext>(op, rrs1, state_.regs.read(op.rs2));
            return;
        case isa::BGEU:
            execute_cached_branch<isa::BGEU, kCopyContext>(op, rrs1, state_.regs.read(op.rs2));
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
            execute_cached_load<isa::LB>(machine, op, rrs1, ram);
            return;
        case isa::LH:
            execute_cached_load<isa::LH>(machine, op, rrs1, ram);
            return;
        case isa::LW:
            execute_cached_load<isa::LW>(machine, op, rrs1, ram);
            return;
        case isa::LD:
            execute_cached_load<isa::LD>(machine, op, rrs1, ram);
            return;
        case isa::LBU:
            execute_cached_load<isa::LBU>(machine, op, rrs1, ram);
            return;
        case isa::LHU:
            execute_cached_load<isa::LHU>(machine, op, rrs1, ram);
            return;
        case isa::LWU:
            execute_cached_load<isa::LWU>(machine, op, rrs1, ram);
            return;
        // ---- Stores ----
        case isa::SB:
            execute_cached_store<isa::SB>(machine, op, rrs1, state_.regs.read(op.rs2), ram);
            return;
        case isa::SH:
            execute_cached_store<isa::SH>(machine, op, rrs1, state_.regs.read(op.rs2), ram);
            return;
        case isa::SW:
            execute_cached_store<isa::SW>(machine, op, rrs1, state_.regs.read(op.rs2), ram);
            return;
        case isa::SD:
            execute_cached_store<isa::SD>(machine, op, rrs1, state_.regs.read(op.rs2), ram);
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
                simrv::xlen::Address phys_addr = mem_addr;
                if constexpr (simrv::xlen::kIsXLen64) {
                    if (simrv::compiler::unlikely(state_.regs.xlen == 32))
                        phys_addr &= 0xFFFFFFFFULL;
                }
                if (simrv::compiler::likely((phys_addr & align_mask) == 0 &&
                                            ram.contains(phys_addr, size_bytes))) {
                    mem_rdata = simrv::memory::ram_read_fast(
                        phys_addr, static_cast<simrv::xlen::Instruction>(op.funct3), ram);
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
                            entry.matches(vpn, current_asid, eff_priv, soft_tlb_epoch) &&
                            entry.has_host_ptr())) {
                        mem_rdata = simrv::memory::host_read_fast(
                            entry.host_ptr(lookup_addr),
                            static_cast<simrv::xlen::Instruction>(op.funct3));
                        loaded = true;
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
                simrv::xlen::Address phys_addr = mem_addr;
                if constexpr (simrv::xlen::kIsXLen64) {
                    if (simrv::compiler::unlikely(state_.regs.xlen == 32))
                        phys_addr &= 0xFFFFFFFFULL;
                }
                if (simrv::compiler::likely((phys_addr & align_mask) == 0 &&
                                            ram.contains(phys_addr, size_bytes) &&
                                            !is_tohost_addr(machine, phys_addr))) {
                    simrv::memory::ram_write_fast(
                        phys_addr, fp_data, static_cast<simrv::xlen::Instruction>(op.funct3), ram);
                    pipeline_context.mem_paddr = phys_addr;
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
                            entry.matches(vpn, current_asid, eff_priv, soft_tlb_epoch) &&
                            entry.has_host_ptr())) {
                        const simrv::xlen::Address paddr = (entry.paddr_base + page_offset).raw();
                        if (simrv::compiler::likely(!is_tohost_addr(machine, paddr))) {
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
}

auto CPU::advance_sequential_miss_baremetal(CachedOp* chained,
                                            simrv::xlen::Register next_pc) noexcept -> CachedOp* {
    const auto* current_set = DecodeCache::set_of(chained);
    const auto* candidate = ((next_pc & 2) != 0 && chained->op_len == 2)
                                ? current_set
                                : decode_cache.next_set(current_set);
    auto* hit = decode_cache.lookup_sequential_baremetal(candidate, next_pc);
    if (simrv::compiler::likely(hit != nullptr)) {
        chained->next_seq_idx = decode_cache.index_of(hit);
        return hit;
    }
    hit = decode_cache.lookup_baremetal_aligned(next_pc);
    if (simrv::compiler::likely(hit != nullptr)) {
        chained->next_seq_idx = decode_cache.index_of(hit);
        return hit;
    }
    return nullptr;
}

auto CPU::resolve_branch_target_baremetal(CachedOp* chained,
                                          simrv::xlen::Register target_pc) noexcept -> CachedOp* {
    auto* hit = decode_cache.lookup_baremetal_aligned(target_pc);
    if (simrv::compiler::likely(hit != nullptr)) {
        chained->target_idx = decode_cache.index_of(hit);
        return hit;
    }
    return nullptr;
}

template <bool kCopyContext, bool kInstMix, bool kPollPause>
SIMRV_ALWAYS_INLINE auto CPU::run_fast_baremetal_kernel(Machine& machine, uint32_t batch_size,
                                                        const simrv::memory::RamView& ram)
    -> uint32_t {
    uint32_t cached_ops = 0;
    simrv::xlen::Counter last_flushed_icount = e_icount;
    const auto tohost_addr = machine.isa_test_tohost();
    uint32_t b = 0;
    while (b < batch_size) {
        auto* cached = decode_cache.lookup_aligned(state_.pc);
        if (simrv::compiler::likely(cached != nullptr)) {
            if constexpr (!kCopyContext && !kInstMix) {
                const auto execute_fast_chain =
                    [&]<bool kGuestXlen64, bool kFastMRam, bool kMultiHart>() __attribute__((
                        always_inline)) {
                        auto* const ram_data = ram.data();
                        const simrv::xlen::Address ram_base = ram.base();
                        const simrv::xlen::Address ram_size = ram.size();
                        auto* chained = cached;
                        CachedOp* fallback_op = nullptr;
                        simrv::xlen::Register next_pc = state_.pc;
                        const uint32_t start_b = b;
                        simrv::xlen::Register seg_start_pc = next_pc;
                        uint32_t seg_start_b = b;
                        uint32_t compressed_count = 0;

                        const auto commit_linear_segment =
                            [&](simrv::xlen::Register end_pc) noexcept {
                                const uint32_t seg_ops = b - seg_start_b;
                                if (seg_ops != 0) {
                                    compressed_count +=
                                        (2 * seg_ops) -
                                        static_cast<uint32_t>((end_pc - seg_start_pc) >> 1);
                                }
                            };

                        const auto advance_sequential = [&]()
                                                            __attribute__((always_inline)) -> bool {
                            next_pc += chained->op_len;
                            ++b;
                            if (simrv::compiler::unlikely(b >= batch_size)) {
                                return false;
                            }
                            auto* next_op = decode_cache.op_by_index(chained->next_seq_idx);
                            if (simrv::compiler::likely(next_op->cpc.raw() == next_pc)) {
                                chained = next_op;
                                return true;
                            }
                            auto* hit = advance_sequential_miss_baremetal(chained, next_pc);
                            if (simrv::compiler::likely(hit != nullptr)) {
                                chained = hit;
                                return true;
                            }
                            return false;
                        };

                        while (true) {
                            auto op_tag = chained->fast_op;
                            if (simrv::compiler::likely(op_tag < FastOp::kFirstNonAlu)) {
                                do {
                                    execute_cached_alu_fast_xlen<kGuestXlen64, false, false, true>(
                                        *chained, op_tag);
                                    if (simrv::compiler::unlikely(!advance_sequential())) {
                                        goto chain_exit;
                                    }
                                    op_tag = chained->fast_op;
                                } while (op_tag < FastOp::kFirstNonAlu);
                            }
                            if (op_tag == FastOp::IntLoad) {
                                if constexpr (kFastMRam) {
                                    simrv::xlen::Address mem_addr =
                                        state_.regs.read(chained->rs1) + chained->imm;
                                    if constexpr (simrv::xlen::kIsXLen64 && !kGuestXlen64) {
                                        mem_addr &= 0xFFFFFFFFULL;
                                    }
                                    const unsigned size_bytes = chained->mem_size;
                                    const auto mem_offset = mem_addr - ram_base;
                                    if (simrv::compiler::likely(
                                            mem_offset <= (ram_size - size_bytes) &&
                                            (mem_addr & chained->align_mask()) == 0)) {
                                        const auto* const host_ptr = ram_data + mem_offset;
                                        const simrv::xlen::Register mem_rdata =
                                            simrv::memory::host_read_fast_aligned(host_ptr,
                                                                                  chained->funct3);
                                        if constexpr (simrv::xlen::kIsXLen64) {
                                            if constexpr (kGuestXlen64) {
                                                state_.regs.write_xlen64(chained->rd, mem_rdata);
                                            } else {
                                                state_.regs.write_xlen32(chained->rd, mem_rdata);
                                            }
                                        } else {
                                            state_.regs.write_xlen32(chained->rd, mem_rdata);
                                        }
                                        if (advance_sequential()) {
                                            continue;
                                        }
                                        goto chain_exit;
                                    }
                                }
                                fallback_op = chained;
                                goto chain_exit;
                            } else if (op_tag == FastOp::Branch) {
                                if (chained->branch_kind == BranchKind::Jal) {
                                    const simrv::xlen::Register target_pc =
                                        static_cast<simrv::xlen::Register>(chained->cpc.raw() +
                                                                           chained->imm);
                                    if (simrv::compiler::unlikely(
                                            (target_pc & state_.fetch_align_mask) != 0)) {
                                        fallback_op = chained;
                                        goto chain_exit;
                                    }
                                    const simrv::xlen::Register link_pc =
                                        static_cast<simrv::xlen::Register>(chained->cpc.raw() +
                                                                           chained->op_len);
                                    if constexpr (simrv::xlen::kIsXLen64) {
                                        if constexpr (kGuestXlen64) {
                                            state_.regs.write_xlen64(chained->rd, link_pc);
                                        } else {
                                            state_.regs.write_xlen32(chained->rd, link_pc);
                                        }
                                    } else {
                                        state_.regs.write_xlen32(chained->rd, link_pc);
                                    }
                                    ++b;
                                    commit_linear_segment(link_pc);
                                    next_pc = target_pc;
                                    if constexpr (simrv::xlen::kIsXLen64 && !kGuestXlen64) {
                                        next_pc = static_cast<simrv::xlen::Register>(
                                            static_cast<int64_t>(static_cast<int32_t>(next_pc)));
                                    }
                                    seg_start_pc = next_pc;
                                    seg_start_b = b;
                                    if (simrv::compiler::unlikely(b >= batch_size)) {
                                        goto chain_exit;
                                    }
                                    auto* next_op = decode_cache.op_by_index(chained->target_idx);
                                    if (simrv::compiler::likely(next_op->cpc.raw() == next_pc)) {
                                        chained = next_op;
                                        continue;
                                    }
                                    chained = resolve_branch_target_baremetal(chained, next_pc);
                                    if (simrv::compiler::unlikely(chained == nullptr)) {
                                        goto chain_exit;
                                    }
                                    continue;
                                }
                                if (chained->branch_kind == BranchKind::Jalr) {
                                    const simrv::xlen::Register rrs1 =
                                        state_.regs.read(chained->rs1);
                                    simrv::xlen::Register target_pc =
                                        (rrs1 + chained->imm) &
                                        ~static_cast<simrv::xlen::Register>(1);
                                    if constexpr (simrv::xlen::kIsXLen64 && !kGuestXlen64) {
                                        target_pc = static_cast<simrv::xlen::Register>(
                                            static_cast<int64_t>(static_cast<int32_t>(target_pc)));
                                    }
                                    if (simrv::compiler::unlikely(
                                            (target_pc & state_.fetch_align_mask) != 0)) {
                                        fallback_op = chained;
                                        goto chain_exit;
                                    }
                                    const simrv::xlen::Register link_pc =
                                        static_cast<simrv::xlen::Register>(chained->cpc.raw() +
                                                                           chained->op_len);
                                    if constexpr (simrv::xlen::kIsXLen64) {
                                        if constexpr (kGuestXlen64) {
                                            state_.regs.write_xlen64(chained->rd, link_pc);
                                        } else {
                                            state_.regs.write_xlen32(chained->rd, link_pc);
                                        }
                                    } else {
                                        state_.regs.write_xlen32(chained->rd, link_pc);
                                    }
                                    ++b;
                                    commit_linear_segment(link_pc);
                                    next_pc = target_pc;
                                    seg_start_pc = next_pc;
                                    seg_start_b = b;
                                    if (simrv::compiler::unlikely(b >= batch_size)) {
                                        goto chain_exit;
                                    }
                                    auto* next_op = decode_cache.op_by_index(chained->target_idx);
                                    if (simrv::compiler::likely(next_op->cpc.raw() == next_pc)) {
                                        chained = next_op;
                                        continue;
                                    }
                                    chained = resolve_branch_target_baremetal(chained, next_pc);
                                    if (simrv::compiler::unlikely(chained == nullptr)) {
                                        goto chain_exit;
                                    }
                                    continue;
                                }
                                const simrv::xlen::Register rrs1 = state_.regs.read(chained->rs1);
                                const simrv::xlen::Register rrs2 = state_.regs.read(chained->rs2);
                                const bool is_eq = (rrs1 == rrs2);
                                bool tkn = false;
                                if (chained->branch_kind == BranchKind::Bne) {
                                    tkn = !is_eq;
                                } else if (chained->branch_kind == BranchKind::Beq) {
                                    tkn = is_eq;
                                } else if (chained->branch_kind == BranchKind::Bgeu) {
                                    if constexpr (kGuestXlen64) {
                                        tkn = (rrs1 >= rrs2);
                                    } else {
                                        tkn = (static_cast<uint32_t>(rrs1) >=
                                               static_cast<uint32_t>(rrs2));
                                    }
                                } else if (chained->branch_kind == BranchKind::Bge) {
                                    tkn = static_cast<simrv::xlen::SignedWord>(rrs1) >=
                                          static_cast<simrv::xlen::SignedWord>(rrs2);
                                } else if (chained->branch_kind == BranchKind::Bltu) {
                                    if constexpr (kGuestXlen64) {
                                        tkn = (rrs1 < rrs2);
                                    } else {
                                        tkn = (static_cast<uint32_t>(rrs1) <
                                               static_cast<uint32_t>(rrs2));
                                    }
                                } else {
                                    tkn = static_cast<simrv::xlen::SignedWord>(rrs1) <
                                          static_cast<simrv::xlen::SignedWord>(rrs2);
                                }
                                if (!tkn) {
                                    if (advance_sequential()) {
                                        continue;
                                    }
                                    goto chain_exit;
                                }
                                simrv::xlen::Register target_pc =
                                    static_cast<simrv::xlen::Register>(chained->cpc.raw() +
                                                                       chained->imm);
                                if (simrv::compiler::unlikely(
                                        (target_pc & state_.fetch_align_mask) != 0)) {
                                    fallback_op = chained;
                                    goto chain_exit;
                                }
                                if constexpr (simrv::xlen::kIsXLen64 && !kGuestXlen64) {
                                    target_pc = static_cast<simrv::xlen::Register>(
                                        static_cast<int64_t>(static_cast<int32_t>(target_pc)));
                                }
                                ++b;
                                commit_linear_segment(chained->cpc.raw() + chained->op_len);
                                next_pc = target_pc;
                                seg_start_pc = next_pc;
                                seg_start_b = b;
                                if (simrv::compiler::unlikely(b >= batch_size)) {
                                    goto chain_exit;
                                }
                                auto* next_op = decode_cache.op_by_index(chained->target_idx);
                                if (simrv::compiler::likely(next_op->cpc.raw() == next_pc)) {
                                    chained = next_op;
                                    continue;
                                }
                                chained = resolve_branch_target_baremetal(chained, next_pc);
                                if (simrv::compiler::unlikely(chained == nullptr)) {
                                    goto chain_exit;
                                }
                                continue;
                            } else if (op_tag == FastOp::IntStore) {
                                if constexpr (kFastMRam) {
                                    simrv::xlen::Address mem_addr =
                                        state_.regs.read(chained->rs1) + chained->imm;
                                    if constexpr (simrv::xlen::kIsXLen64 && !kGuestXlen64) {
                                        mem_addr &= 0xFFFFFFFFULL;
                                    }
                                    const unsigned size_bytes = chained->mem_size;
                                    const auto mem_offset = mem_addr - ram_base;
                                    if (simrv::compiler::likely(
                                            mem_offset <= (ram_size - size_bytes) &&
                                            (mem_addr & chained->align_mask()) == 0 &&
                                            !is_tohost_addr_fast(tohost_addr, mem_addr))) {
                                        auto* const host_ptr = ram_data + mem_offset;
                                        simrv::memory::host_write_fast_aligned(
                                            host_ptr, state_.regs.read(chained->rs2),
                                            chained->funct3);
                                        state_.lr_reservation_active = false;
                                        if constexpr (kMultiHart) {
                                            if (simrv::compiler::unlikely(
                                                    machine.memory_.reservation_table()
                                                        .may_have_reservations())) {
                                                machine.memory_.reservation_table()
                                                    .invalidate_range(mem_addr, size_bytes,
                                                                      hart_id());
                                            }
                                        }
                                        if (advance_sequential()) {
                                            continue;
                                        }
                                        goto chain_exit;
                                    }
                                }
                                fallback_op = chained;
                                goto chain_exit;
                            } else if (op_tag == FastOp::FpMem) {
                                if constexpr (kFastMRam) {
                                    if (chained->mem_class == FastMemClass::FpLoad) {
                                        if (simrv::xlen::kIsXLen64 ||
                                            chained->funct3 == isa::Funct3::Flw) {
                                            simrv::xlen::Address mem_addr =
                                                state_.regs.read(chained->rs1) + chained->imm;
                                            if constexpr (simrv::xlen::kIsXLen64 && !kGuestXlen64) {
                                                mem_addr &= 0xFFFFFFFFULL;
                                            }
                                            const unsigned size_bytes = chained->mem_size;
                                            const auto mem_offset = mem_addr - ram_base;
                                            if (simrv::compiler::likely(
                                                    mem_offset <= (ram_size - size_bytes) &&
                                                    (mem_addr & chained->align_mask()) == 0)) {
                                                const auto* const host_ptr = ram_data + mem_offset;
                                                const simrv::xlen::Word mem_rdata =
                                                    simrv::memory::host_read_fast(
                                                        host_ptr,
                                                        static_cast<simrv::xlen::Instruction>(
                                                            chained->funct3));
                                                state_.regs.write_fp(
                                                    simrv::xlen::to_fp_reg_id(chained->rd),
                                                    (chained->funct3 == isa::Funct3::Flw)
                                                        ? (static_cast<
                                                               simrv::xlen::FloatingRegister>(
                                                               simrv::xlen::kF32BoxerBits) |
                                                           (static_cast<
                                                                simrv::xlen::FloatingRegister>(
                                                                mem_rdata) &
                                                            0xFFFFFFFFULL))
                                                        : static_cast<
                                                              simrv::xlen::FloatingRegister>(
                                                              mem_rdata));
                                                state_.mstatus |= MstatusBit::Fs;
                                                if (advance_sequential()) {
                                                    continue;
                                                }
                                                goto chain_exit;
                                            }
                                        }
                                        fallback_op = chained;
                                        goto chain_exit;
                                    }
                                    if (chained->mem_class == FastMemClass::FpStore) {
                                        if (simrv::xlen::kIsXLen64 ||
                                            chained->funct3 == isa::Funct3::Fsw) {
                                            simrv::xlen::Address mem_addr =
                                                state_.regs.read(chained->rs1) + chained->imm;
                                            if constexpr (simrv::xlen::kIsXLen64 && !kGuestXlen64) {
                                                mem_addr &= 0xFFFFFFFFULL;
                                            }
                                            const unsigned size_bytes = chained->mem_size;
                                            const auto mem_offset = mem_addr - ram_base;
                                            if (simrv::compiler::likely(
                                                    mem_offset <= (ram_size - size_bytes) &&
                                                    (mem_addr & chained->align_mask()) == 0 &&
                                                    !is_tohost_addr_fast(tohost_addr, mem_addr))) {
                                                auto* const host_ptr = ram_data + mem_offset;
                                                const simrv::xlen::FloatingRegister fp_data =
                                                    state_.regs.read_fp(
                                                        simrv::xlen::to_fp_reg_id(chained->rs2));
                                                simrv::memory::host_write_fast(
                                                    host_ptr,
                                                    static_cast<simrv::xlen::Word>(fp_data),
                                                    static_cast<simrv::xlen::Instruction>(
                                                        chained->funct3));
                                                state_.lr_reservation_active = false;
                                                if constexpr (kMultiHart) {
                                                    if (simrv::compiler::unlikely(
                                                            machine.memory_.reservation_table()
                                                                .may_have_reservations())) {
                                                        machine.memory_.reservation_table()
                                                            .invalidate_range(mem_addr, size_bytes,
                                                                              hart_id());
                                                    }
                                                }
                                                if (advance_sequential()) {
                                                    continue;
                                                }
                                                goto chain_exit;
                                            }
                                        }
                                        fallback_op = chained;
                                        goto chain_exit;
                                    }
                                }
                                fallback_op = chained;
                                goto chain_exit;
                            } else {
                                fallback_op = chained;
                                goto chain_exit;
                            }
                        }
                    chain_exit:
                        commit_linear_segment(next_pc);
                        state_.pc = next_pc;
                        const uint32_t ops_executed = b - start_b;
                        cached_ops += ops_executed;
                        e_icount += ops_executed;
                        e_ccount += compressed_count;
                        if (fallback_op != nullptr) {
                            execute_cached_op_fast<false, false>(machine, *fallback_op, ram);
                            ++cached_ops;
                            ++b;
                        }
                    };
                const bool fast_m_ram =
                    (state_.priv == kPrivMachine && (state_.mstatus & MstatusBit::Mprv) == 0 &&
                     !state_.has_locked_pmp && ram.data() != nullptr && ram.size() >= 8);
                const bool multi_hart = machine.num_harts() > 1;
                // Chain operations do not change MIP or MIE, and device interrupt lines are
                // applied at the enclosing batch boundary. If one is already pending, execute
                // a single instruction and return to the precise interrupt boundary instead of
                // branching on the same pending bit after every operation in the chain.
                if (simrv::compiler::unlikely((state_.mip & state_.mie) != 0u)) {
                    execute_cached_op_fast<false, false>(machine, *cached, ram);
                    ++cached_ops;
                    ++b;
                } else if constexpr (simrv::xlen::kIsXLen64) {
                    if (simrv::compiler::likely(state_.regs.xlen == 64)) {
                        if (simrv::compiler::likely(fast_m_ram)) {
                            if (simrv::compiler::likely(!multi_hart)) {
                                execute_fast_chain.template operator()<true, true, false>();
                            } else {
                                execute_fast_chain.template operator()<true, true, true>();
                            }
                        } else {
                            if (simrv::compiler::likely(!multi_hart)) {
                                execute_fast_chain.template operator()<true, false, false>();
                            } else {
                                execute_fast_chain.template operator()<true, false, true>();
                            }
                        }
                    } else {
                        if (simrv::compiler::likely(fast_m_ram)) {
                            if (simrv::compiler::likely(!multi_hart)) {
                                execute_fast_chain.template operator()<false, true, false>();
                            } else {
                                execute_fast_chain.template operator()<false, true, true>();
                            }
                        } else {
                            if (simrv::compiler::likely(!multi_hart)) {
                                execute_fast_chain.template operator()<false, false, false>();
                            } else {
                                execute_fast_chain.template operator()<false, false, true>();
                            }
                        }
                    }
                } else {
                    if (simrv::compiler::likely(fast_m_ram)) {
                        if (simrv::compiler::likely(!multi_hart)) {
                            execute_fast_chain.template operator()<false, true, false>();
                        } else {
                            execute_fast_chain.template operator()<false, true, true>();
                        }
                    } else {
                        if (simrv::compiler::likely(!multi_hart)) {
                            execute_fast_chain.template operator()<false, false, false>();
                        } else {
                            execute_fast_chain.template operator()<false, false, true>();
                        }
                    }
                }
            } else {
                if (cached->has_fast_alu()) {
                    execute_cached_alu_fast<kCopyContext, kInstMix, false>(*cached);
                } else {
                    execute_cached_op_fast<kCopyContext, kInstMix>(machine, *cached, ram);
                }
                ++cached_ops;
                ++b;

                // Preserve per-instruction commits when tracing or execution context is requested.
                while (cached->has_fast_alu() && b < batch_size && ((b & 0x3Fu) != 0) &&
                       simrv::compiler::likely((state_.mip & state_.mie) == 0u)) {
                    auto* next_cached = decode_cache.lookup_next_aligned(*cached, state_.pc);
                    if (simrv::compiler::unlikely(next_cached == nullptr ||
                                                  !next_cached->has_fast_alu())) {
                        break;
                    }
                    cached = next_cached;
                    execute_cached_alu_fast<kCopyContext, kInstMix, false>(*cached);
                    ++cached_ops;
                    ++b;
                }
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
    sync_decode_context();
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
    const auto tohost_addr = machine.isa_test_tohost();

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
        auto* cached = decode_cache.lookup_aligned(state_.pc);
        if (simrv::compiler::likely(cached != nullptr)) {
            if constexpr (!kCopyContext && !kInstMix) {
                const auto execute_os_chain = [&]<bool kGuestXlen64>() __attribute__((
                                                  always_inline)) {
                    const simrv::xlen::PrivilegeLevel eff_priv = effective_data_privilege();
                    const unsigned active_xlen = state_.xlen_for_privilege(eff_priv);
                    const bool satp_en =
                        (eff_priv != kPrivMachine) &&
                        simrv::xlen::satp_translation_enabled(state_.satp, active_xlen);
                    const simrv::xlen::Word current_asid =
                        satp_en ? simrv::xlen::satp_asid(state_.satp, active_xlen) : 0;
                    const bool fast_m_ram =
                        (state_.priv == kPrivMachine && (state_.mstatus & MstatusBit::Mprv) == 0 &&
                         !state_.has_locked_pmp);
                    const bool check_reservations =
                        machine.memory_.reservation_table().may_have_reservations() &&
                        machine.num_harts() > 1;
                    auto* chained = cached;
                    CachedOp* fallback_op = nullptr;
                    simrv::xlen::Register next_pc = state_.pc;
                    uint32_t chain_size = 0;
                    uint32_t compressed_count = 0;

                    const auto should_break_chain = [&]() __attribute__((always_inline)) -> bool {
                        if (b >= batch_size ||
                            simrv::compiler::unlikely((state_.mip & state_.mie) != 0u)) {
                            return true;
                        }
                        if (simrv::compiler::unlikely((chunk_cycles + chain_size) >= 64)) {
                            cached_ops += chain_size;
                            chunk_cycles += chain_size;
                            e_icount += chain_size;
                            e_ccount += compressed_count;
                            chain_size = 0;
                            compressed_count = 0;
                            clint_mmio.mcycle += chunk_cycles;
                            clint_mmio.rtc_divider += chunk_cycles;
                            if (simrv::compiler::unlikely(clint_mmio.rtc_divider >=
                                                          timing::kCyclesPerTimebaseTick)) {
                                clint_mmio.mtime +=
                                    clint_mmio.rtc_divider / timing::kCyclesPerTimebaseTick;
                                clint_mmio.rtc_divider %= timing::kCyclesPerTimebaseTick;
                                evaluate_timer_interrupt();
                            }
                            chunk_cycles = 0;
                            const auto delta = e_icount - last_flushed_icount;
                            if (delta > 0) {
                                machine.record_retired_instructions(delta);
                                last_flushed_icount = e_icount;
                            }
                            if (simrv::compiler::unlikely(
                                    (state_.mip & state_.mie) != 0u || machine.tohost != 0 ||
                                    !machine.is_running() ||
                                    (kPollPause &&
                                     (machine.is_paused() || machine.debug_pause_requested())))) {
                                return true;
                            }
                        }
                        return false;
                    };

                    const auto advance_sequential = [&]() __attribute__((always_inline)) -> bool {
                        next_pc += chained->op_len;
                        ++chain_size;
                        compressed_count += (chained->op_len == 2);
                        ++b;
                        if (should_break_chain()) {
                            return false;
                        }
                        auto* next_op = decode_cache.op_by_index(chained->next_seq_idx);
                        if (simrv::compiler::likely(next_op->cpc.raw() == next_pc)) {
                            chained = next_op;
                            return true;
                        }
                        const auto* current_set = DecodeCache::set_of(chained);
                        const auto* candidate = ((next_pc & 2) != 0 && chained->op_len == 2)
                                                    ? current_set
                                                    : decode_cache.next_set(current_set);
                        auto* hit = decode_cache.lookup_sequential_aligned(candidate, next_pc);
                        if (simrv::compiler::likely(hit != nullptr)) {
                            chained->next_seq_idx = decode_cache.index_of(hit);
                            chained = hit;
                            return true;
                        }
                        hit = decode_cache.lookup_next_aligned(*chained, next_pc);
                        if (simrv::compiler::unlikely(hit == nullptr)) {
                            return false;
                        }
                        chained->next_seq_idx = decode_cache.index_of(hit);
                        chained = hit;
                        return true;
                    };

                    while (true) {
                        const auto op_tag = chained->fast_op;
                        if (simrv::compiler::likely(op_tag < FastOp::kFirstNonAlu)) {
                            execute_cached_alu_fast_xlen<kGuestXlen64, false, false, true>(*chained,
                                                                                           op_tag);
                            if (advance_sequential()) {
                                continue;
                            }
                            break;
                        }
                        if (op_tag == FastOp::IntLoad) {
                            simrv::xlen::Address lookup_addr =
                                state_.regs.read(chained->rs1) + chained->imm;
                            if constexpr (simrv::xlen::kIsXLen64) {
                                if (simrv::compiler::unlikely(active_xlen == 32)) {
                                    lookup_addr &= 0xFFFFFFFFULL;
                                }
                            }
                            const unsigned size_bytes = chained->mem_size;
                            if (simrv::compiler::likely((lookup_addr & chained->align_mask()) ==
                                                        0)) {
                                const unsigned page_offset = lookup_addr & 0xFFFu;
                                if (simrv::compiler::likely(satp_en &&
                                                            page_offset + size_bytes <= 4096u)) {
                                    const simrv::xlen::Address vpn = lookup_addr >> 12;
                                    const auto& entry = soft_tlb_read[soft_tlb_index(vpn)];
                                    if (simrv::compiler::likely(entry.matches(vpn, current_asid,
                                                                              eff_priv,
                                                                              soft_tlb_epoch) &&
                                                                entry.has_host_ptr())) {
                                        const simrv::xlen::Register mem_rdata =
                                            simrv::memory::host_read_fast_aligned(
                                                entry.host_ptr(lookup_addr), chained->funct3);
                                        if constexpr (simrv::xlen::kIsXLen64) {
                                            if constexpr (kGuestXlen64) {
                                                state_.regs.write_xlen64(chained->rd, mem_rdata);
                                            } else {
                                                state_.regs.write_xlen32(chained->rd, mem_rdata);
                                            }
                                        } else {
                                            state_.regs.write_xlen32(chained->rd, mem_rdata);
                                        }
                                        if (advance_sequential()) {
                                            continue;
                                        }
                                        break;
                                    }
                                } else if (fast_m_ram) {
                                    auto* host_ptr = ram.try_get_ptr_fast(lookup_addr, size_bytes);
                                    if (host_ptr != nullptr) {
                                        const simrv::xlen::Register mem_rdata =
                                            simrv::memory::host_read_fast_aligned(host_ptr,
                                                                                  chained->funct3);
                                        if constexpr (simrv::xlen::kIsXLen64) {
                                            if constexpr (kGuestXlen64) {
                                                state_.regs.write_xlen64(chained->rd, mem_rdata);
                                            } else {
                                                state_.regs.write_xlen32(chained->rd, mem_rdata);
                                            }
                                        } else {
                                            state_.regs.write_xlen32(chained->rd, mem_rdata);
                                        }
                                        if (advance_sequential()) {
                                            continue;
                                        }
                                        break;
                                    }
                                }
                            }
                            fallback_op = chained;
                            break;
                        }
                        if (op_tag == FastOp::Branch) {
                            if (chained->branch_kind == BranchKind::Jal) {
                                const simrv::xlen::Register target_pc =
                                    static_cast<simrv::xlen::Register>(chained->cpc.raw() +
                                                                       chained->imm);
                                if (simrv::compiler::unlikely(
                                        (target_pc & state_.fetch_align_mask) != 0)) {
                                    fallback_op = chained;
                                    break;
                                }
                                const simrv::xlen::Register link_pc =
                                    static_cast<simrv::xlen::Register>(chained->cpc.raw() +
                                                                       chained->op_len);
                                if constexpr (simrv::xlen::kIsXLen64) {
                                    if constexpr (kGuestXlen64) {
                                        state_.regs.write_xlen64(chained->rd, link_pc);
                                    } else {
                                        state_.regs.write_xlen32(chained->rd, link_pc);
                                    }
                                } else {
                                    state_.regs.write_xlen32(chained->rd, link_pc);
                                }
                                next_pc = target_pc;
                                if constexpr (simrv::xlen::kIsXLen64 && !kGuestXlen64) {
                                    next_pc = static_cast<simrv::xlen::Register>(
                                        static_cast<int64_t>(static_cast<int32_t>(next_pc)));
                                }
                                ++chain_size;
                                compressed_count += (chained->op_len == 2);
                                ++b;
                                if (should_break_chain()) {
                                    break;
                                }
                                auto* next_op = decode_cache.op_by_index(chained->target_idx);
                                if (simrv::compiler::likely(next_op->cpc.raw() == next_pc)) {
                                    chained = next_op;
                                    continue;
                                }
                                auto* hit = decode_cache.lookup_target_aligned(*chained, next_pc);
                                if (simrv::compiler::unlikely(hit == nullptr)) {
                                    break;
                                }
                                chained->target_idx = decode_cache.index_of(hit);
                                chained = hit;
                                continue;
                            }
                            if (chained->branch_kind == BranchKind::Jalr) {
                                const simrv::xlen::Register rrs1 = state_.regs.read(chained->rs1);
                                simrv::xlen::Register target_pc =
                                    (rrs1 + chained->imm) & ~static_cast<simrv::xlen::Register>(1);
                                if constexpr (simrv::xlen::kIsXLen64 && !kGuestXlen64) {
                                    target_pc = static_cast<simrv::xlen::Register>(
                                        static_cast<int64_t>(static_cast<int32_t>(target_pc)));
                                }
                                if (simrv::compiler::unlikely(
                                        (target_pc & state_.fetch_align_mask) != 0)) {
                                    fallback_op = chained;
                                    break;
                                }
                                const simrv::xlen::Register link_pc =
                                    static_cast<simrv::xlen::Register>(chained->cpc.raw() +
                                                                       chained->op_len);
                                if constexpr (simrv::xlen::kIsXLen64) {
                                    if constexpr (kGuestXlen64) {
                                        state_.regs.write_xlen64(chained->rd, link_pc);
                                    } else {
                                        state_.regs.write_xlen32(chained->rd, link_pc);
                                    }
                                } else {
                                    state_.regs.write_xlen32(chained->rd, link_pc);
                                }
                                next_pc = target_pc;
                                ++chain_size;
                                compressed_count += (chained->op_len == 2);
                                ++b;
                                if (should_break_chain()) {
                                    break;
                                }
                                auto* next_op = decode_cache.op_by_index(chained->target_idx);
                                if (simrv::compiler::likely(next_op->cpc.raw() == next_pc)) {
                                    chained = next_op;
                                    continue;
                                }
                                auto* hit = decode_cache.lookup_aligned(next_pc);
                                if (simrv::compiler::unlikely(hit == nullptr)) {
                                    break;
                                }
                                chained->target_idx = decode_cache.index_of(hit);
                                chained = hit;
                                continue;
                            }
                            const simrv::xlen::Register rrs1 = state_.regs.read(chained->rs1);
                            const simrv::xlen::Register rrs2 = state_.regs.read(chained->rs2);
                            const bool is_eq = (rrs1 == rrs2);
                            bool tkn = false;
                            if (chained->branch_kind == BranchKind::Bne) {
                                tkn = !is_eq;
                            } else if (chained->branch_kind == BranchKind::Beq) {
                                tkn = is_eq;
                            } else if (chained->branch_kind == BranchKind::Bgeu) {
                                if constexpr (kGuestXlen64) {
                                    tkn = (rrs1 >= rrs2);
                                } else {
                                    tkn = (static_cast<uint32_t>(rrs1) >=
                                           static_cast<uint32_t>(rrs2));
                                }
                            } else if (chained->branch_kind == BranchKind::Bge) {
                                tkn = static_cast<simrv::xlen::SignedWord>(rrs1) >=
                                      static_cast<simrv::xlen::SignedWord>(rrs2);
                            } else if (chained->branch_kind == BranchKind::Bltu) {
                                if constexpr (kGuestXlen64) {
                                    tkn = (rrs1 < rrs2);
                                } else {
                                    tkn =
                                        (static_cast<uint32_t>(rrs1) < static_cast<uint32_t>(rrs2));
                                }
                            } else {
                                tkn = static_cast<simrv::xlen::SignedWord>(rrs1) <
                                      static_cast<simrv::xlen::SignedWord>(rrs2);
                            }
                            if (!tkn) {
                                if (advance_sequential()) {
                                    continue;
                                }
                                break;
                            }
                            simrv::xlen::Register target_pc = static_cast<simrv::xlen::Register>(
                                chained->cpc.raw() + chained->imm);
                            if (simrv::compiler::unlikely((target_pc & state_.fetch_align_mask) !=
                                                          0)) {
                                fallback_op = chained;
                                break;
                            }
                            if constexpr (simrv::xlen::kIsXLen64 && !kGuestXlen64) {
                                target_pc = static_cast<simrv::xlen::Register>(
                                    static_cast<int64_t>(static_cast<int32_t>(target_pc)));
                            }
                            next_pc = target_pc;
                            ++chain_size;
                            compressed_count += (chained->op_len == 2);
                            ++b;
                            if (should_break_chain()) {
                                break;
                            }
                            auto* next_op = decode_cache.op_by_index(chained->target_idx);
                            if (simrv::compiler::likely(next_op->cpc.raw() == next_pc)) {
                                chained = next_op;
                                continue;
                            }
                            auto* hit = decode_cache.lookup_target_aligned(*chained, next_pc);
                            if (simrv::compiler::unlikely(hit == nullptr)) {
                                break;
                            }
                            chained->target_idx = decode_cache.index_of(hit);
                            chained = hit;
                            continue;
                        }
                        if (op_tag == FastOp::IntStore) {
                            simrv::xlen::Address lookup_addr =
                                state_.regs.read(chained->rs1) + chained->imm;
                            if constexpr (simrv::xlen::kIsXLen64) {
                                if (simrv::compiler::unlikely(active_xlen == 32)) {
                                    lookup_addr &= 0xFFFFFFFFULL;
                                }
                            }
                            const unsigned size_bytes = chained->mem_size;
                            if (simrv::compiler::likely((lookup_addr & chained->align_mask()) ==
                                                        0)) {
                                const unsigned page_offset = lookup_addr & 0xFFFu;
                                if (simrv::compiler::likely(satp_en &&
                                                            page_offset + size_bytes <= 4096u)) {
                                    const simrv::xlen::Address vpn = lookup_addr >> 12;
                                    const auto& entry = soft_tlb_write[soft_tlb_index(vpn)];
                                    if (simrv::compiler::likely(entry.matches(vpn, current_asid,
                                                                              eff_priv,
                                                                              soft_tlb_epoch) &&
                                                                entry.has_host_ptr())) {
                                        const simrv::xlen::Address paddr =
                                            (entry.paddr_base + page_offset).raw();
                                        if (simrv::compiler::likely(
                                                !is_tohost_addr_fast(tohost_addr, paddr))) {
                                            simrv::memory::host_write_fast_aligned(
                                                entry.host_ptr(lookup_addr),
                                                state_.regs.read(chained->rs2), chained->funct3);
                                            state_.lr_reservation_active = false;
                                            if (simrv::compiler::unlikely(check_reservations)) {
                                                machine.memory_.reservation_table()
                                                    .invalidate_range(paddr, size_bytes, hart_id());
                                            }
                                            if (advance_sequential()) {
                                                continue;
                                            }
                                            break;
                                        }
                                    }
                                } else if (fast_m_ram &&
                                           !is_tohost_addr_fast(tohost_addr, lookup_addr)) {
                                    auto* host_ptr = ram.try_get_ptr_fast(lookup_addr, size_bytes);
                                    if (host_ptr != nullptr) {
                                        simrv::memory::host_write_fast_aligned(
                                            host_ptr, state_.regs.read(chained->rs2),
                                            chained->funct3);
                                        state_.lr_reservation_active = false;
                                        if (simrv::compiler::unlikely(check_reservations)) {
                                            machine.memory_.reservation_table().invalidate_range(
                                                lookup_addr, size_bytes, hart_id());
                                        }
                                        if (advance_sequential()) {
                                            continue;
                                        }
                                        break;
                                    }
                                }
                            }
                            fallback_op = chained;
                            break;
                        }
                        if (op_tag == FastOp::FpMem) {
                            if (chained->mem_class == FastMemClass::FpLoad) {
                                if (simrv::xlen::kIsXLen64 || chained->funct3 == isa::Funct3::Flw) {
                                    simrv::xlen::Address lookup_addr =
                                        state_.regs.read(chained->rs1) + chained->imm;
                                    if constexpr (simrv::xlen::kIsXLen64) {
                                        if (simrv::compiler::unlikely(active_xlen == 32)) {
                                            lookup_addr &= 0xFFFFFFFFULL;
                                        }
                                    }
                                    const unsigned size_bytes = chained->mem_size;
                                    if (simrv::compiler::likely(
                                            (lookup_addr & chained->align_mask()) == 0)) {
                                        const unsigned page_offset = lookup_addr & 0xFFFu;
                                        if (simrv::compiler::likely(
                                                satp_en && page_offset + size_bytes <= 4096u)) {
                                            const simrv::xlen::Address vpn = lookup_addr >> 12;
                                            const auto& entry = soft_tlb_read[soft_tlb_index(vpn)];
                                            if (simrv::compiler::likely(
                                                    entry.matches(vpn, current_asid, eff_priv,
                                                                  soft_tlb_epoch) &&
                                                    entry.has_host_ptr())) {
                                                const simrv::xlen::Word mem_rdata =
                                                    simrv::memory::host_read_fast(
                                                        entry.host_ptr(lookup_addr),
                                                        static_cast<simrv::xlen::Instruction>(
                                                            chained->funct3));
                                                state_.regs.write_fp(
                                                    simrv::xlen::to_fp_reg_id(chained->rd),
                                                    (chained->funct3 == isa::Funct3::Flw)
                                                        ? (static_cast<
                                                               simrv::xlen::FloatingRegister>(
                                                               simrv::xlen::kF32BoxerBits) |
                                                           (static_cast<
                                                                simrv::xlen::FloatingRegister>(
                                                                mem_rdata) &
                                                            0xFFFFFFFFULL))
                                                        : static_cast<
                                                              simrv::xlen::FloatingRegister>(
                                                              mem_rdata));
                                                state_.mstatus |= MstatusBit::Fs;
                                                if (advance_sequential()) {
                                                    continue;
                                                }
                                                break;
                                            }
                                        } else if (fast_m_ram) {
                                            auto* host_ptr =
                                                ram.try_get_ptr_fast(lookup_addr, size_bytes);
                                            if (host_ptr != nullptr) {
                                                const simrv::xlen::Word mem_rdata =
                                                    simrv::memory::host_read_fast(
                                                        host_ptr,
                                                        static_cast<simrv::xlen::Instruction>(
                                                            chained->funct3));
                                                state_.regs.write_fp(
                                                    simrv::xlen::to_fp_reg_id(chained->rd),
                                                    (chained->funct3 == isa::Funct3::Flw)
                                                        ? (static_cast<
                                                               simrv::xlen::FloatingRegister>(
                                                               simrv::xlen::kF32BoxerBits) |
                                                           (static_cast<
                                                                simrv::xlen::FloatingRegister>(
                                                                mem_rdata) &
                                                            0xFFFFFFFFULL))
                                                        : static_cast<
                                                              simrv::xlen::FloatingRegister>(
                                                              mem_rdata));
                                                state_.mstatus |= MstatusBit::Fs;
                                                if (advance_sequential()) {
                                                    continue;
                                                }
                                                break;
                                            }
                                        }
                                    }
                                }
                                fallback_op = chained;
                                break;
                            }
                            if (chained->mem_class == FastMemClass::FpStore) {
                                if (simrv::xlen::kIsXLen64 || chained->funct3 == isa::Funct3::Fsw) {
                                    simrv::xlen::Address lookup_addr =
                                        state_.regs.read(chained->rs1) + chained->imm;
                                    if constexpr (simrv::xlen::kIsXLen64) {
                                        if (simrv::compiler::unlikely(active_xlen == 32)) {
                                            lookup_addr &= 0xFFFFFFFFULL;
                                        }
                                    }
                                    const unsigned size_bytes = chained->mem_size;
                                    if (simrv::compiler::likely(
                                            (lookup_addr & chained->align_mask()) == 0)) {
                                        const unsigned page_offset = lookup_addr & 0xFFFu;
                                        if (simrv::compiler::likely(
                                                satp_en && page_offset + size_bytes <= 4096u)) {
                                            const simrv::xlen::Address vpn = lookup_addr >> 12;
                                            const auto& entry = soft_tlb_write[soft_tlb_index(vpn)];
                                            if (simrv::compiler::likely(
                                                    entry.matches(vpn, current_asid, eff_priv,
                                                                  soft_tlb_epoch) &&
                                                    entry.has_host_ptr())) {
                                                const simrv::xlen::Address paddr =
                                                    (entry.paddr_base + page_offset).raw();
                                                if (simrv::compiler::likely(
                                                        !is_tohost_addr_fast(tohost_addr, paddr))) {
                                                    const simrv::xlen::FloatingRegister fp_data =
                                                        state_.regs.read_fp(
                                                            simrv::xlen::to_fp_reg_id(
                                                                chained->rs2));
                                                    simrv::memory::host_write_fast(
                                                        entry.host_ptr(lookup_addr),
                                                        static_cast<simrv::xlen::Word>(fp_data),
                                                        static_cast<simrv::xlen::Instruction>(
                                                            chained->funct3));
                                                    state_.lr_reservation_active = false;
                                                    if (simrv::compiler::unlikely(
                                                            check_reservations)) {
                                                        machine.memory_.reservation_table()
                                                            .invalidate_range(paddr, size_bytes,
                                                                              hart_id());
                                                    }
                                                    if (advance_sequential()) {
                                                        continue;
                                                    }
                                                    break;
                                                }
                                            }
                                        } else if (fast_m_ram &&
                                                   !is_tohost_addr_fast(tohost_addr, lookup_addr)) {
                                            auto* host_ptr =
                                                ram.try_get_ptr_fast(lookup_addr, size_bytes);
                                            if (host_ptr != nullptr) {
                                                const simrv::xlen::FloatingRegister fp_data =
                                                    state_.regs.read_fp(
                                                        simrv::xlen::to_fp_reg_id(chained->rs2));
                                                simrv::memory::host_write_fast(
                                                    host_ptr,
                                                    static_cast<simrv::xlen::Word>(fp_data),
                                                    static_cast<simrv::xlen::Instruction>(
                                                        chained->funct3));
                                                state_.lr_reservation_active = false;
                                                if (simrv::compiler::unlikely(check_reservations)) {
                                                    machine.memory_.reservation_table()
                                                        .invalidate_range(lookup_addr, size_bytes,
                                                                          hart_id());
                                                }
                                                if (advance_sequential()) {
                                                    continue;
                                                }
                                                break;
                                            }
                                        }
                                    }
                                }
                                fallback_op = chained;
                                break;
                            }
                            fallback_op = chained;
                            break;
                        }
                        fallback_op = chained;
                        break;
                    }

                    state_.pc = next_pc;
                    cached_ops += chain_size;
                    chunk_cycles += chain_size;
                    e_icount += chain_size;
                    e_ccount += compressed_count;
                    if (fallback_op != nullptr) {
                        execute_cached_op_fast<false, false>(machine, *fallback_op, ram);
                        ++cached_ops;
                        ++chunk_cycles;
                        ++b;
                    }
                };
                if constexpr (simrv::xlen::kIsXLen64) {
                    if (simrv::compiler::likely(state_.regs.xlen == 64)) {
                        execute_os_chain.template operator()<true>();
                    } else {
                        execute_os_chain.template operator()<false>();
                    }
                } else {
                    execute_os_chain.template operator()<false>();
                }
            } else {
                if (cached->has_fast_alu()) {
                    execute_cached_alu_fast<kCopyContext, kInstMix, false>(*cached);
                } else {
                    execute_cached_op_fast<kCopyContext, kInstMix>(machine, *cached, ram);
                }
                ++cached_ops;
                ++chunk_cycles;
                ++b;

                // Straight-line instruction chaining: execute consecutive non-trapping,
                // non-control ALU operations until next 64-cycle chunk or control flow/miss.
                while (cached->has_fast_alu() && b < batch_size && chunk_cycles < 64 &&
                       simrv::compiler::likely((state_.mip & state_.mie) == 0u)) {
                    auto* next_cached = decode_cache.lookup_next_aligned(*cached, state_.pc);
                    if (simrv::compiler::unlikely(next_cached == nullptr ||
                                                  !next_cached->has_fast_alu())) {
                        break;
                    }
                    cached = next_cached;
                    execute_cached_alu_fast<kCopyContext, kInstMix, false>(*cached);
                    ++cached_ops;
                    ++chunk_cycles;
                    ++b;
                }
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
    sync_decode_context();
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
