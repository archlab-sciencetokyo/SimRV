/**
 * @file CpuFastOs.cpp
 * @brief Translated OS-mode fast-batch and instruction-chaining execution engine.
 */
#include <atomic>
#include <utility>

#include "CpuFastExecutionInternal.hpp"
#include "simrv/CompilerHints.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/PlatformTiming.hpp"
#include "simrv/core/Pmp.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

namespace simrv::core {

template <bool kCopyContext, bool kInstMix, bool kPollPause, int kHpmClassMode>
SIMRV_ALWAYS_INLINE auto CPU::run_fast_os_kernel(Machine& machine, uint32_t batch_size,
                                                 const simrv::memory::RamView& ram) -> uint32_t {
    uint32_t cached_ops = 0;
    simrv::xlen::Counter last_flushed_icount = e_icount;
    uint32_t chunk_cycles = 0;
    const auto tohost_addr = machine.isa_test_tohost();
    auto* const uart_dev = (state_.mhartid == 0) ? machine.uart_device() : nullptr;

    auto flush_clint_and_retired = [&]() {
        if (chunk_cycles > 0) {
            account_arch_cycles(chunk_cycles);
            if (state_.mhartid == 0) {
                machine.advance_device_cycles(chunk_cycles);
                if (machine.clock_divider().advance_fast_cycles(
                        chunk_cycles, clint_mmio.rtc_divider, clint_mmio.mtime) > 0) {
                    evaluate_timer_interrupt();
                }
            } else {
                clint_mmio.mtime.store(
                    machine.primary_hart().clint_mmio.mtime.load(std::memory_order_relaxed),
                    std::memory_order_relaxed);
                evaluate_timer_interrupt();
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
        if (simrv::compiler::unlikely(state_.has_active_triggers)) break;
        auto* cached = decode_cache.lookup_aligned(state_.pc);
        if (simrv::compiler::likely(cached != nullptr)) {
            if constexpr (!kCopyContext && !kInstMix) {
                const auto execute_os_chain = [&]<bool kGuestXlen64>() __attribute__((
                                                  always_inline)) {
                    const uint8_t active_ctx = decode_cache.active_context();
                    const simrv::xlen::PrivilegeLevel eff_priv = effective_data_privilege();
                    const unsigned active_xlen = state_.xlen_for_privilege(eff_priv);
                    const bool satp_en =
                        (eff_priv != kPrivMachine) &&
                        simrv::xlen::satp_translation_enabled(active_satp(), active_xlen);
                    const simrv::xlen::Word current_asid =
                        satp_en ? simrv::xlen::satp_asid(active_satp(), active_xlen) : 0;
                    const uint32_t asid_priv_tag =
                        SoftTlbEntry::make_asid_priv_tag(current_asid, eff_priv);
                    const uint32_t priv_tag = SoftTlbEntry::make_asid_priv_tag(0, eff_priv);
                    const bool fast_m_ram =
                        (state_.priv == kPrivMachine && (state_.mstatus & MstatusBit::Mprv) == 0 &&
                         !state_.has_locked_pmp);
                    const bool check_reservations =
                        machine.memory_.reservation_table().may_have_reservations() &&
                        machine.num_harts() > 1;
                    auto* chained = cached;
                    CachedOp* fallback_op = nullptr;
                    simrv::xlen::Register next_pc = state_.pc.raw();
                    uint32_t chain_size = 0;
                    uint32_t compressed_count = 0;

                    const auto should_break_chain = [&]() __attribute__((always_inline)) -> bool {
                        if (b >= batch_size ||
                            simrv::compiler::unlikely((state_.mip & state_.mie) != 0u)) {
                            return true;
                        }
                        const bool uart_transfer_pending = uart_dev != nullptr &&
                                                           (chunk_cycles + chain_size) >= 4 &&
                                                           uart_dev->has_active_transfer();
                        const uint32_t sync_threshold = uart_transfer_pending ? 4 : 512;
                        if (simrv::compiler::unlikely((chunk_cycles + chain_size) >=
                                                          sync_threshold ||
                                                      uart_transfer_pending)) {
                            cached_ops += chain_size;
                            chunk_cycles += chain_size;
                            account_retirement(chain_size);
                            record_hpm_event(HpmEvent::Instructions, chain_size);
                            record_hpm_event(HpmEvent::Cycles, chain_size);
                            e_ccount += compressed_count;
                            chain_size = 0;
                            compressed_count = 0;
                            account_arch_cycles(chunk_cycles);
                            if (state_.mhartid == 0) {
                                machine.advance_device_cycles(chunk_cycles);
                                if (simrv::compiler::unlikely(
                                        machine.clock_divider().advance_fast_cycles(
                                            chunk_cycles, clint_mmio.rtc_divider,
                                            clint_mmio.mtime) > 0)) {
                                    evaluate_timer_interrupt();
                                }
                            } else {
                                clint_mmio.mtime.store(machine.primary_hart().clint_mmio.mtime.load(
                                                           std::memory_order_relaxed),
                                                       std::memory_order_relaxed);
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
                        record_hpm_instruction_classes<kHpmClassMode>(
                            chained->op_id, chained->opcode(), chained->cinsn != 0);
                        next_pc += chained->op_len;
                        ++chain_size;
                        compressed_count += (chained->op_len == 2);
                        ++b;
                        if (should_break_chain()) {
                            return false;
                        }
                        auto* next_op = decode_cache.op_by_index(chained->next_seq_idx);
                        if (simrv::compiler::likely(next_op->cpc.raw() == next_pc &&
                                                    next_op->ctx == active_ctx)) {
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
                        if (is_int_load_fast_op(op_tag)) {
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
                                    const size_t tlb_idx = soft_tlb_index(vpn);
                                    auto& entry = soft_tlb_read[tlb_idx];
                                    const simrv::xlen::Byte* host_ptr = nullptr;
                                    if (simrv::compiler::likely(
                                            entry.matches_fast(vpn, asid_priv_tag, priv_tag,
                                                               soft_tlb_epoch) &&
                                            entry.has_host_ptr())) {
                                        host_ptr = entry.host_ptr(lookup_addr);
                                    } else if (TLBEntry* tlb_e = tlb.lookup_data_r(
                                                   simrv::xlen::VirtAddr{lookup_addr}, current_asid,
                                                   eff_priv);
                                               simrv::compiler::likely(
                                                   tlb_e != nullptr &&
                                                   tlb_e->pbmt == simrv::memory::PtePbmt::Pma)) {
                                        const simrv::xlen::Address ppage = tlb_e->p_addr.raw();
                                        if (simrv::compiler::likely(
                                                ram.contains(ppage, 4096) &&
                                                (state_.num_active_pmp == 0 ||
                                                 core::pmp::check_access(
                                                     state_, simrv::xlen::PhysAddr{ppage}, 4096,
                                                     core::PmpAccessType::Read, eff_priv)))) {
                                            auto* const host_base = ram.unchecked_ptr(ppage);
                                            entry.set(vpn, current_asid, eff_priv, soft_tlb_epoch,
                                                      ppage, host_base, tlb_e->global);
                                            host_ptr = host_base + page_offset;
                                        }
                                    }
                                    if (simrv::compiler::likely(host_ptr != nullptr)) {
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
                        if (is_branch_fast_op(op_tag)) {
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
                                record_hpm_instruction_classes<kHpmClassMode>(
                                    chained->op_id, chained->opcode(), chained->cinsn != 0);
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
                                if (simrv::compiler::likely(next_op->cpc.raw() == next_pc &&
                                                            next_op->ctx == active_ctx)) {
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
                                record_hpm_instruction_classes<kHpmClassMode>(
                                    chained->op_id, chained->opcode(), chained->cinsn != 0);
                                ++chain_size;
                                compressed_count += (chained->op_len == 2);
                                ++b;
                                if (should_break_chain()) {
                                    break;
                                }
                                auto* next_op = decode_cache.op_by_index(chained->target_idx);
                                if (simrv::compiler::likely(next_op->cpc.raw() == next_pc &&
                                                            next_op->ctx == active_ctx)) {
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
                            const bool is_eq =
                                kGuestXlen64
                                    ? (rrs1 == rrs2)
                                    : (static_cast<uint32_t>(rrs1) == static_cast<uint32_t>(rrs2));
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
                                if constexpr (kGuestXlen64) {
                                    tkn = static_cast<int64_t>(rrs1) >= static_cast<int64_t>(rrs2);
                                } else {
                                    tkn = static_cast<int32_t>(rrs1) >= static_cast<int32_t>(rrs2);
                                }
                            } else if (chained->branch_kind == BranchKind::Bltu) {
                                if constexpr (kGuestXlen64) {
                                    tkn = (rrs1 < rrs2);
                                } else {
                                    tkn =
                                        (static_cast<uint32_t>(rrs1) < static_cast<uint32_t>(rrs2));
                                }
                            } else {
                                if constexpr (kGuestXlen64) {
                                    tkn = static_cast<int64_t>(rrs1) < static_cast<int64_t>(rrs2);
                                } else {
                                    tkn = static_cast<int32_t>(rrs1) < static_cast<int32_t>(rrs2);
                                }
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
                            record_hpm_instruction_classes<kHpmClassMode>(
                                chained->op_id, chained->opcode(), chained->cinsn != 0);
                            ++chain_size;
                            compressed_count += (chained->op_len == 2);
                            ++b;
                            if (should_break_chain()) {
                                break;
                            }
                            auto* next_op = decode_cache.op_by_index(chained->target_idx);
                            if (simrv::compiler::likely(next_op->cpc.raw() == next_pc &&
                                                        next_op->ctx == active_ctx)) {
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
                        if (is_int_store_fast_op(op_tag)) {
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
                                    const size_t tlb_idx = soft_tlb_index(vpn);
                                    auto& entry = soft_tlb_write[tlb_idx];
                                    simrv::xlen::Byte* host_ptr = nullptr;
                                    simrv::xlen::Address paddr = 0;
                                    if (simrv::compiler::likely(
                                            entry.matches_fast(vpn, asid_priv_tag, priv_tag,
                                                               soft_tlb_epoch) &&
                                            entry.has_host_ptr())) {
                                        paddr = (entry.paddr_base + page_offset).raw();
                                        host_ptr = entry.host_ptr(lookup_addr);
                                    } else if (TLBEntry* tlb_e = tlb.lookup_data_w(
                                                   simrv::xlen::VirtAddr{lookup_addr}, current_asid,
                                                   eff_priv);
                                               simrv::compiler::likely(
                                                   tlb_e != nullptr &&
                                                   tlb_e->pbmt == simrv::memory::PtePbmt::Pma)) {
                                        const simrv::xlen::Address ppage = tlb_e->p_addr.raw();
                                        if (simrv::compiler::likely(
                                                ram.contains(ppage, 4096) &&
                                                (state_.num_active_pmp == 0 ||
                                                 core::pmp::check_access(
                                                     state_, simrv::xlen::PhysAddr{ppage}, 4096,
                                                     core::PmpAccessType::Write, eff_priv)))) {
                                            auto* const host_base = ram.unchecked_ptr(ppage);
                                            entry.set(vpn, current_asid, eff_priv, soft_tlb_epoch,
                                                      ppage, host_base, tlb_e->global);
                                            paddr = ppage + page_offset;
                                            host_ptr = host_base + page_offset;
                                        }
                                    }
                                    if (simrv::compiler::likely(
                                            host_ptr != nullptr &&
                                            !is_tohost_addr_fast(tohost_addr, paddr))) {
                                        simrv::memory::host_write_fast_aligned(
                                            host_ptr, state_.regs.read(chained->rs2),
                                            chained->funct3);
                                        state_.lr_reservation_active = false;
                                        if (simrv::compiler::unlikely(check_reservations)) {
                                            machine.memory_.reservation_table().invalidate_range(
                                                simrv::xlen::PhysAddr{paddr}, size_bytes,
                                                hart_id());
                                        }
                                        if (advance_sequential()) {
                                            continue;
                                        }
                                        break;
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
                                                simrv::xlen::PhysAddr{lookup_addr}, size_bytes,
                                                hart_id());
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
                            if (simrv::compiler::unlikely(
                                    !state_.extension_state_enabled(MstatusBit::Fs))) {
                                fallback_op = chained;
                                break;
                            }
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
                                                    entry.matches_fast(vpn, asid_priv_tag, priv_tag,
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
                                                state_.mark_extension_dirty(MstatusBit::Fs);
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
                                                state_.mark_extension_dirty(MstatusBit::Fs);
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
                                                    entry.matches_fast(vpn, asid_priv_tag, priv_tag,
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
                                                            .invalidate_range(
                                                                simrv::xlen::PhysAddr{paddr},
                                                                size_bytes, hart_id());
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
                                                        .invalidate_range(
                                                            simrv::xlen::PhysAddr{lookup_addr},
                                                            size_bytes, hart_id());
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
                    account_retirement(chain_size);
                    record_hpm_event(HpmEvent::Instructions, chain_size);
                    record_hpm_event(HpmEvent::Cycles, chain_size);
                    e_ccount += compressed_count;
                    if (fallback_op != nullptr) {
                        if (chunk_cycles > 0) {
                            flush_clint_and_retired();
                        }
                        const bool is_csr = is_csr_instruction(fallback_op->op_id);
                        execute_cached_op_fast<false, false>(machine, *fallback_op, ram);
                        ++cached_ops;
                        ++chunk_cycles;
                        ++b;
                        if constexpr (kHpmClassMode != 2) {
                            if (is_csr && perf_counters.has_instruction_class_listeners() !=
                                              (kHpmClassMode == 1)) {
                                b = batch_size;
                            }
                        }
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

            const bool uart_transfer_pending =
                uart_dev != nullptr && chunk_cycles >= 4 && uart_dev->has_active_transfer();
            if (simrv::compiler::unlikely(chunk_cycles >= 64 || uart_transfer_pending)) {
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
    handle_cached_interrupts();
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
        const auto run_kernel = [&]<int kHpmClassMode>() {
            if (policy.poll_pause) {
                run_fast_os_kernel<false, false, true, kHpmClassMode>(machine, batch_size, ram);
            } else {
                run_fast_os_kernel<false, false, false, kHpmClassMode>(machine, batch_size, ram);
            }
        };
        if (perf_counters.has_instruction_class_listeners()) {
            run_kernel.template operator()<1>();
        } else {
            run_kernel.template operator()<0>();
        }
    } else if (policy.copy_pipeline_context && policy.collect_instruction_mix) {
        if (policy.poll_pause) {
            run_fast_os_kernel<true, true, true, 2>(machine, batch_size, ram);
        } else {
            run_fast_os_kernel<true, true, false, 2>(machine, batch_size, ram);
        }
    } else if (policy.copy_pipeline_context) {
        if (policy.poll_pause) {
            run_fast_os_kernel<true, false, true, 2>(machine, batch_size, ram);
        } else {
            run_fast_os_kernel<true, false, false, 2>(machine, batch_size, ram);
        }
    } else {
        if (policy.poll_pause) {
            run_fast_os_kernel<false, true, true, 2>(machine, batch_size, ram);
        } else {
            run_fast_os_kernel<false, true, false, 2>(machine, batch_size, ram);
        }
    }
}

}  // namespace simrv::core
