/**
 * @file CpuFastExecution.cpp
 * @brief Cached single-instruction execution and optimized memory access paths.
 */
#include <array>
#include <shared_mutex>
#include <utility>

#include "CpuFastExecutionInternal.hpp"
#include "simrv/CompilerHints.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Pmp.hpp"
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
        if constexpr (!simrv::xlen::kIsXLen64) {
            tkn = rrs1 == rrs2;
        } else if (simrv::compiler::likely(state_.regs.xlen == 32)) {
            tkn = static_cast<uint32_t>(rrs1) == static_cast<uint32_t>(rrs2);
        } else {
            tkn = rrs1 == rrs2;
        }
    } else if constexpr (kBranchOp == isa::BNE) {
        if constexpr (!simrv::xlen::kIsXLen64) {
            tkn = rrs1 != rrs2;
        } else if (simrv::compiler::likely(state_.regs.xlen == 32)) {
            tkn = static_cast<uint32_t>(rrs1) != static_cast<uint32_t>(rrs2);
        } else {
            tkn = rrs1 != rrs2;
        }
    } else if constexpr (kBranchOp == isa::BLT) {
        if constexpr (!simrv::xlen::kIsXLen64) {
            tkn = static_cast<int32_t>(rrs1) < static_cast<int32_t>(rrs2);
        } else if (simrv::compiler::likely(state_.regs.xlen == 32)) {
            tkn = static_cast<int32_t>(rrs1) < static_cast<int32_t>(rrs2);
        } else {
            tkn = static_cast<simrv::xlen::SignedWord>(rrs1) <
                  static_cast<simrv::xlen::SignedWord>(rrs2);
        }
    } else if constexpr (kBranchOp == isa::BGE) {
        if constexpr (!simrv::xlen::kIsXLen64) {
            tkn = static_cast<int32_t>(rrs1) >= static_cast<int32_t>(rrs2);
        } else if (simrv::compiler::likely(state_.regs.xlen == 32)) {
            tkn = static_cast<int32_t>(rrs1) >= static_cast<int32_t>(rrs2);
        } else {
            tkn = static_cast<simrv::xlen::SignedWord>(rrs1) >=
                  static_cast<simrv::xlen::SignedWord>(rrs2);
        }
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
    simrv::xlen::VirtAddr target_pc = tkn ? simrv::xlen::VirtAddr{state_.pc + op.imm}
                                          : simrv::xlen::VirtAddr{state_.pc + op.len()};
    if (state_.regs.xlen == 32) {
        target_pc = simrv::xlen::VirtAddr{static_cast<simrv::xlen::Register>(
            static_cast<int64_t>(static_cast<int32_t>(target_pc.raw())))};
    }
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

    if (simrv::compiler::likely(eff_priv != kPrivMachine && simrv::xlen::satp_translation_enabled(
                                                                active_satp(), active_xlen))) {
        const simrv::xlen::Word current_asid = simrv::xlen::satp_asid(active_satp(), active_xlen);
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
            if (simrv::compiler::unlikely(
                    state_.num_active_pmp > 0 &&
                    !core::pmp::check_access(state_, simrv::xlen::PhysAddr{paddr}, size_bytes,
                                             core::PmpAccessType::Read, eff_priv))) {
                return false;
            }
            if (simrv::compiler::likely(ram.contains(paddr, size_bytes))) {
                out_val = simrv::memory::ram_read_fast(
                    paddr, static_cast<simrv::xlen::Instruction>(funct3), ram);
                return true;
            }
            return false;
        }
        TLBEntry* tlb_e =
            tlb.lookup_data_r(simrv::xlen::VirtAddr{mem_addr}, current_asid, eff_priv);
        if (simrv::compiler::likely(tlb_e != nullptr &&
                                    tlb_e->pbmt == simrv::memory::PtePbmt::Pma)) {
            simrv::xlen::Address const paddr = (tlb_e->p_addr + (mem_addr & 0xFFF)).raw();
            if (simrv::compiler::unlikely(
                    !core::pmp::check_access(state_, simrv::xlen::PhysAddr{paddr}, size_bytes,
                                             core::PmpAccessType::Read, eff_priv))) {
                return false;
            }
            simrv::xlen::Address const ppage = tlb_e->p_addr.raw();
            simrv::xlen::Byte* host_base =
                (ram.contains(ppage, 4096) &&
                 (state_.num_active_pmp == 0 ||
                  core::pmp::check_access(state_, simrv::xlen::PhysAddr{ppage}, 4096,
                                          core::PmpAccessType::Read, eff_priv)))
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
                                    core::pmp::check_access(state_, simrv::xlen::PhysAddr{mem_addr},
                                                            size_bytes, core::PmpAccessType::Read,
                                                            eff_priv))) {
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

    if (simrv::compiler::likely(eff_priv != kPrivMachine && simrv::xlen::satp_translation_enabled(
                                                                active_satp(), active_xlen))) {
        const simrv::xlen::Word current_asid = simrv::xlen::satp_asid(active_satp(), active_xlen);
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
            if (simrv::compiler::unlikely(
                    state_.num_active_pmp > 0 &&
                    !core::pmp::check_access(state_, simrv::xlen::PhysAddr{paddr}, size_bytes,
                                             core::PmpAccessType::Write, eff_priv))) {
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
        TLBEntry* tlb_e =
            tlb.lookup_data_w(simrv::xlen::VirtAddr{mem_addr}, current_asid, eff_priv);
        if (simrv::compiler::likely(tlb_e != nullptr &&
                                    tlb_e->pbmt == simrv::memory::PtePbmt::Pma)) {
            simrv::xlen::Address const paddr = (tlb_e->p_addr + (mem_addr & 0xFFF)).raw();
            if (simrv::compiler::unlikely(is_tohost_addr(machine, paddr))) {
                return false;
            }
            if (simrv::compiler::unlikely(
                    !core::pmp::check_access(state_, simrv::xlen::PhysAddr{paddr}, size_bytes,
                                             core::PmpAccessType::Write, eff_priv))) {
                return false;
            }
            simrv::xlen::Address const ppage = tlb_e->p_addr.raw();
            simrv::xlen::Byte* host_base =
                (ram.contains(ppage, 4096) &&
                 (state_.num_active_pmp == 0 ||
                  core::pmp::check_access(state_, simrv::xlen::PhysAddr{ppage}, 4096,
                                          core::PmpAccessType::Write, eff_priv)))
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
                core::pmp::check_access(state_, simrv::xlen::PhysAddr{mem_addr}, size_bytes,
                                        core::PmpAccessType::Write, eff_priv))) {
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
    simrv::xlen::Address mem_addr = rrs1 + op.imm;
    mem_addr =
        simrv::memory::MemoryAccess::mask_user_pointer(*this, simrv::xlen::VirtAddr{mem_addr})
            .raw();
    if (simrv::compiler::unlikely(machine.tui_enabled() || machine.branch_trace_enabled())) {
        pipeline_context.mem_addr = mem_addr;
    }
    simrv::xlen::Register mem_rdata = 0;
    constexpr unsigned size_bytes = kSizeBytes;
    constexpr simrv::xlen::Address align_mask = kSizeBytes - 1u;
    bool skip_fast_load = false;
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
                eff_priv != kPrivMachine &&
                simrv::xlen::satp_translation_enabled(active_satp(), active_xlen))) {
            const simrv::xlen::Word current_asid =
                simrv::xlen::satp_asid(active_satp(), active_xlen);
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
            } else if (TLBEntry* tlb_e = tlb.lookup_data_r(simrv::xlen::VirtAddr{lookup_addr},
                                                           current_asid, eff_priv);
                       simrv::compiler::likely(tlb_e != nullptr)) {
                if (simrv::compiler::likely(tlb_e->pbmt == simrv::memory::PtePbmt::Pma)) {
                    const simrv::xlen::Address paddr = (tlb_e->p_addr + page_offset).raw();
                    if (simrv::compiler::likely(
                            state_.num_active_pmp == 0 ||
                            core::pmp::check_access(state_, simrv::xlen::PhysAddr{paddr},
                                                    size_bytes, core::PmpAccessType::Read,
                                                    eff_priv))) {
                        const simrv::xlen::Address ppage = tlb_e->p_addr.raw();
                        simrv::xlen::Byte* const host_base =
                            (ram.contains(ppage, 4096) &&
                             (state_.num_active_pmp == 0 ||
                              core::pmp::check_access(state_, simrv::xlen::PhysAddr{ppage}, 4096,
                                                      core::PmpAccessType::Read, eff_priv)))
                                ? ram.unchecked_ptr(ppage)
                                : nullptr;
                        soft_tlb_read[tlb_idx].set(vpn, current_asid, eff_priv, soft_tlb_epoch,
                                                   ppage, host_base, tlb_e->global);
                        if (simrv::compiler::likely(host_base != nullptr)) {
                            mem_rdata =
                                simrv::memory::host_read_fast(host_base + page_offset, kFunct3);
                            state_.regs.write(op.rd, mem_rdata);
                            advance_cached_pc(op);
                            return true;
                        }
                    }
                } else {
                    skip_fast_load = true;
                }
            } else {
                skip_fast_load = true;
            }
        }
    }

    if (skip_fast_load || !try_fast_load(machine, mem_addr, kFunct3, mem_rdata, ram)) {
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
    simrv::xlen::Address mem_addr = rrs1 + op.imm;
    mem_addr =
        simrv::memory::MemoryAccess::mask_user_pointer(*this, simrv::xlen::VirtAddr{mem_addr})
            .raw();
    if (simrv::compiler::unlikely(machine.tui_enabled() || machine.branch_trace_enabled())) {
        pipeline_context.mem_addr = mem_addr;
    }
    constexpr unsigned size_bytes = kSizeBytes;
    bool skip_fast_store = false;
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
                machine.memory_.reservation_table().invalidate_range(
                    simrv::xlen::PhysAddr{phys_addr}, size_bytes, hart_id());
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
                eff_priv != kPrivMachine &&
                simrv::xlen::satp_translation_enabled(active_satp(), active_xlen))) {
            const simrv::xlen::Word current_asid =
                simrv::xlen::satp_asid(active_satp(), active_xlen);
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
                        machine.memory_.reservation_table().invalidate_range(
                            simrv::xlen::PhysAddr{paddr}, size_bytes, hart_id());
                    }
                    advance_cached_pc(op);
                    return true;
                }
            } else if (TLBEntry* tlb_e = tlb.lookup_data_w(simrv::xlen::VirtAddr{lookup_addr},
                                                           current_asid, eff_priv);
                       simrv::compiler::likely(tlb_e != nullptr)) {
                if (simrv::compiler::likely(tlb_e->pbmt == simrv::memory::PtePbmt::Pma)) {
                    const simrv::xlen::Address paddr = (tlb_e->p_addr + page_offset).raw();
                    if (simrv::compiler::likely(
                            (state_.num_active_pmp == 0 ||
                             core::pmp::check_access(state_, simrv::xlen::PhysAddr{paddr},
                                                     size_bytes, core::PmpAccessType::Write,
                                                     eff_priv)) &&
                            !is_tohost_addr(machine, paddr))) {
                        const simrv::xlen::Address ppage = tlb_e->p_addr.raw();
                        simrv::xlen::Byte* const host_base =
                            (ram.contains(ppage, 4096) &&
                             (state_.num_active_pmp == 0 ||
                              core::pmp::check_access(state_, simrv::xlen::PhysAddr{ppage}, 4096,
                                                      core::PmpAccessType::Write, eff_priv)))
                                ? ram.unchecked_ptr(ppage)
                                : nullptr;
                        soft_tlb_write[tlb_idx].set(vpn, current_asid, eff_priv, soft_tlb_epoch,
                                                    ppage, host_base, tlb_e->global);
                        if (simrv::compiler::likely(host_base != nullptr)) {
                            simrv::memory::host_write_fast(
                                host_base + page_offset, rrs2,
                                static_cast<simrv::xlen::Instruction>(kFunct3));
                            state_.lr_reservation_active = false;
                            if (simrv::compiler::unlikely(
                                    machine.memory_.reservation_table().may_have_reservations() &&
                                    machine.num_harts() > 1)) {
                                machine.memory_.reservation_table().invalidate_range(
                                    simrv::xlen::PhysAddr{paddr}, size_bytes, hart_id());
                            }
                            advance_cached_pc(op);
                            return true;
                        }
                    }
                } else {
                    skip_fast_store = true;
                }
            } else {
                skip_fast_store = true;
            }
        }
    }

    if (skip_fast_store || !try_fast_store(machine, mem_addr, kFunct3, rrs2, ram)) {
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
    pipeline_context.hypervisor_memory_access_fault = false;
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
    simrv::xlen::Word pending_interrupts = state_.mip & state_.mie;
    simrv::xlen::Word hypervisor_interrupt_candidates = 0;
    simrv::xlen::Word virtual_interrupt_candidates = 0;
    if (isa::misa_has_extension(state_.misa, isa::IsaExtension::H)) {
        constexpr std::array<std::pair<simrv::xlen::Word, simrv::xlen::Word>, 3> kVsInterrupts = {
            std::pair{simrv::xlen::Word{1} << 2U, simrv::xlen::Word{1} << 1U},
            std::pair{simrv::xlen::Word{1} << 6U, simrv::xlen::Word{1} << 5U},
            std::pair{simrv::xlen::Word{1} << 10U, simrv::xlen::Word{1} << 9U},
        };
        const auto pending_h =
            state_.hip | state_.hvip | state_.supervisor_guest_external_pending() |
            state_.selected_guest_external_pending() | vs_interrupts_to_h(state_.vsip) |
            (state_.vstip_sstc ? (simrv::xlen::Word{1} << 6U) : 0U);
        for (const auto& [h_bit, vs_bit] : kVsInterrupts) {
            if ((pending_h & h_bit) == 0) continue;
            if (state_.virt && (state_.hideleg & h_bit) != 0) {
                const bool global_enabled =
                    state_.priv != kPrivSupervisor || (state_.vsstatus & MstatusBit::Sie) != 0;
                if (global_enabled && (state_.vsie & vs_bit) != 0) {
                    // In VS mode the virtual supervisor interrupt is reported with
                    // the supervisor cause number (for example, VSEI is cause 9).
                    virtual_interrupt_candidates |= vs_bit;
                }
            } else if ((state_.hie & h_bit) != 0 && (state_.priv < kPrivSupervisor || state_.virt ||
                                                     (state_.mstatus & MstatusBit::Sie) != 0)) {
                hypervisor_interrupt_candidates |= h_bit;
            }
        }
        if (state_.virt) {
            // Interrupts delegated to HS but not to VS remain eligible while a
            // guest is running. They take priority over interrupts delegated to VS.
            hypervisor_interrupt_candidates |=
                state_.mip & state_.mie & state_.mideleg & ~state_.hideleg;
        }
        if ((pending_h & (simrv::xlen::Word{1} << 12U)) != 0 &&
            (state_.hie & (simrv::xlen::Word{1} << 12U)) != 0 &&
            (state_.priv < kPrivSupervisor || state_.virt ||
             (state_.mstatus & MstatusBit::Sie) != 0)) {
            hypervisor_interrupt_candidates |= simrv::xlen::Word{1} << 12U;
        }
        pending_interrupts |= hypervisor_interrupt_candidates;
    }
    if (simrv::compiler::unlikely(pending_interrupts != 0u || virtual_interrupt_candidates != 0u)) {
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
                if (state_.virt) {
                    enable_interrupts |= hypervisor_interrupt_candidates;
                } else if ((state_.mstatus & MstatusBit::Sie) != 0u) {
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
        } else if (virtual_interrupt_candidates != 0u) {
            // Interrupts targeted at HS have priority over interrupts targeted at VS,
            // regardless of the numeric cause priorities within either privilege level.
            simrv::xlen::Word const irq_num =
                select_highest_priority_interrupt(virtual_interrupt_candidates);
            raise_exception(kInterruptCauseBit | irq_num, 0);
        }
    }
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

    std::shared_lock<std::shared_mutex> memory_read_lock;
    std::unique_lock<std::shared_mutex> memory_write_lock;
    if (machine.is_smp_enabled() && machine.runtime_profile.is_instruction_mode()) {
        const auto opcode = op.opcode();
        if (opcode == isa::Opcode::Load || opcode == isa::Opcode::LoadFp ||
            isa::is_hlv_op(op.op_id)) {
            memory_read_lock = std::shared_lock(machine.instruction_memory_mutex());
        } else if (opcode == isa::Opcode::Store || opcode == isa::Opcode::StoreFp ||
                   opcode == isa::Opcode::Amo || opcode == isa::Opcode::OpV ||
                   isa::is_hsv_op(op.op_id) || isa::is_cmo_op(op.op_id)) {
            memory_write_lock = std::unique_lock(machine.instruction_memory_mutex());
        }
    }

    if constexpr (kCopyContext) {
        op.copy_to(pipeline_context);
        pipeline_context.tlb_miss = false;
        pipeline_context.pending_tval = 0;
    }

    if constexpr (kInstMix) {
        e_instmix[static_cast<std::size_t>(op.op_id)]++;
        perf_counters.record_retire(op.op_id, 1);
    }

    // Control, memory, and uncommon instructions retain their precise cached/fallback paths.
    const simrv::xlen::Register rrs1 = state_.regs.read(op.rs1);
    const auto cached_opcode = op.opcode();
    const bool uses_fp_state =
        cached_opcode == isa::Opcode::LoadFp || cached_opcode == isa::Opcode::StoreFp ||
        cached_opcode == isa::Opcode::OpFp || cached_opcode == isa::Opcode::MAdd ||
        cached_opcode == isa::Opcode::MSub || cached_opcode == isa::Opcode::NMSub ||
        cached_opcode == isa::Opcode::NMAdd;
    if (simrv::compiler::unlikely(uses_fp_state &&
                                  !state_.extension_state_enabled(MstatusBit::Fs))) {
        raise_exception(ExceptionCode::IllegalInstruction, op.ir);
        return;
    }
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
        // ---- Custom Function Unit (CFU) ----
        case isa::OperationId::CFU: {
            const auto rrs2 = state_.regs.read(op.rs2);
            const auto res = cfu_unit.execute(static_cast<uint32_t>(op.funct7()),
                                              static_cast<uint32_t>(op.funct3), rrs1, rrs2);
            state_.regs.write(op.rd, res);
            advance_cached_pc(op);
            return;
        }
        // ---- FP Loads ----
        case isa::FLW:
        case isa::FLD: {
            simrv::xlen::Address mem_addr = rrs1 + op.imm;
            mem_addr = simrv::memory::MemoryAccess::mask_user_pointer(
                           *this, simrv::xlen::VirtAddr{mem_addr})
                           .raw();
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
                        eff_priv != kPrivMachine &&
                        simrv::xlen::satp_translation_enabled(active_satp(), active_xlen))) {
                    const simrv::xlen::Word current_asid =
                        simrv::xlen::satp_asid(active_satp(), active_xlen);
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
            state_.mark_extension_dirty(MstatusBit::Fs);
            advance_cached_pc(op);
            return;
        }
        // ---- FP Stores ----
        case isa::FSW:
        case isa::FSD: {
            simrv::xlen::Address mem_addr = rrs1 + op.imm;
            mem_addr = simrv::memory::MemoryAccess::mask_user_pointer(
                           *this, simrv::xlen::VirtAddr{mem_addr})
                           .raw();
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
                        eff_priv != kPrivMachine &&
                        simrv::xlen::satp_translation_enabled(active_satp(), active_xlen))) {
                    const simrv::xlen::Word current_asid =
                        simrv::xlen::satp_asid(active_satp(), active_xlen);
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
                state_.mark_extension_dirty(MstatusBit::Fs);
            }
            if (state_.fcsr != old_fcsr) state_.mark_extension_dirty(MstatusBit::Fs);
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
                state_.mark_extension_dirty(MstatusBit::Fs);
            }
            if (state_.fcsr != old_fcsr) state_.mark_extension_dirty(MstatusBit::Fs);
            advance_cached_pc(op);
            return;
        }
        // WFI deliberately reaches the full path: fetch is the interrupt-delivery boundary, and
        // bypassing it in a cached idle loop can leave an OS waiting past a pending timer IRQ.
        // ---- Everything else: fallback to full pipeline stages ----
        default:
            if (simrv::isa::is_vector_op(op.op_id)) {
                if (simrv::compiler::unlikely(!state_.extension_state_enabled(MstatusBit::Vs))) {
                    raise_exception(ExceptionCode::IllegalInstruction, op.ir);
                    return;
                }
                if constexpr (!kCopyContext) {
                    op.copy_to(pipeline_context);
                    pipeline_context.tlb_miss = false;
                    pipeline_context.pending_tval = 0;
                }
                execute::ExecuteUnit::execute_vector(*this, machine, op.op_id, op.ir);
                state_.mark_extension_dirty(MstatusBit::Vs);
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

template void CPU::execute_cached_op_fast<false, false>(Machine&, CachedOp&,
                                                        const simrv::memory::RamView&);
template void CPU::execute_cached_op_fast<true, false>(Machine&, CachedOp&,
                                                       const simrv::memory::RamView&);
template void CPU::execute_cached_op_fast<false, true>(Machine&, CachedOp&,
                                                       const simrv::memory::RamView&);
template void CPU::execute_cached_op_fast<true, true>(Machine&, CachedOp&,
                                                      const simrv::memory::RamView&);

}  // namespace simrv::core
