/**
 * @file Cpu.cpp
 * @brief CPU lifecycle, architectural state, and interrupt-line management.
 */
#include "simrv/core/Cpu.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <string_view>

#include "simrv/CompilerHints.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Tracer.hpp"

namespace simrv::core {
CPU::CPU() : plic_mmio(*this), clint_mmio(*this), csr_file(*this), sbi(*this) { reset(); }

void CPU::apply_cpu_model_config(const simrv::pipeline::CpuModelConfig& config) {
    // Validation is performed by the parser/editor.  Keeping this operation atomic at the CPU
    // boundary prevents partially-applied timing models from leaking into a live CA pipeline.
    cpu_model_config = config;
    cpu_model_config.is_provingground_or_rvproc =
        (config.name == "cfu-provingground" || config.name == "rvproc");
    cfu_unit.set_default_latency(config.cfu.default_latency);
    pipeline_sim.config = config.pipeline;
    cycle_counter_start_delay_ = pipeline_sim.config.cycle_counter_start_delay;
    // BaseCache has bounded 16 KiB / 8-way backing, enough for the shipped FPGA profiles.
    // A validated model is required to fit before this point.
    (void)icache.configure(config.instruction_cache.capacity_bytes,
                           config.instruction_cache.associativity);
    (void)dcache.configure(config.data_cache.capacity_bytes, config.data_cache.associativity);
    const simrv::cache::TimingLevelConfig instruction_level{
        .capacity_bytes = config.instruction_front_cache.capacity_bytes,
        .associativity = config.instruction_front_cache.associativity,
        .line_bytes = config.instruction_front_cache.line_bytes,
        .hit_latency = config.instruction_front_cache.hit_latency,
        .refill_latency = config.instruction_front_cache.refill_latency,
    };
    instruction_cache_timing.configure(
        std::span<const simrv::cache::TimingLevelConfig>(&instruction_level, 1),
        config.instruction_front_cache.backing_refill_latency,
        config.instruction_front_cache.startup_refill_latencies);
    ca_state.reset_instruction();
    ca_pipeline.reset();
    branch_predictor.configure(pipeline_sim.config.branch_predictor);
    branch_predictor.reset();
    const unsigned int target_xlen =
        (config.supported_xlen != 0) ? config.supported_xlen : simrv::xlen::kXLenBits;
    state_.misa = isa::misa_with_mxl(isa::isa_preset_bits(config.isa_preset), target_xlen);
    state_.refresh_misa_status();
    state_.initialize_lower_xlen_fields();
    sync_decode_context();
    if (machine_ != nullptr) {
        machine_->dma_engine().set_config(config.dma);
    }
}

void CPU::reset() {
    pending_interrupt_lines_.store(0, std::memory_order_relaxed);
    next_logical_issue_sequence_ = 0;
    state_ = ArchState{};
    prev_state_ = ArchState{};
    pipeline_context = simrv::pipeline::PipelineContext{};
    plic_mmio.reset();
    clint_mmio.reset();
    state_.regs.fill(0);
    state_.regs.fill_fp(0);
    state_.regs.fill_vector(VectorRegister{});
    // Set mstatus.VS to Clean (2 << 9)
    state_.mstatus = static_cast<simrv::xlen::CsrValue>(2) << 9;
    if constexpr (simrv::xlen::kIsXLen64) {
        state_.mstatus |= (static_cast<simrv::xlen::CsrValue>(2) << 34) |
                          (static_cast<simrv::xlen::CsrValue>(2) << 32);
    }
    state_.initialize_lower_xlen_fields();
    dcache.flush();
    TLB_flush();
    e_icount = 0;
    e_ccount = 0;
    cycle_counter_start_delay_ = pipeline_sim.config.cycle_counter_start_delay;
    e_instmix.fill(0);
    ca_state.reset_instruction();
    ca_pipeline.reset();
    branch_predictor.configure(pipeline_sim.config.branch_predictor);
    branch_predictor.reset();
}

void CPU::TLB_flush() {
    tlb.flush();
    decode_cache.flush();
    if (!machine_ || machine_->runtime_profile.is_cycle_mode()) {
        icache.flush();
        instruction_cache_timing.flush();
    }
    soft_tlb_flush();
    sync_decode_context();
}

void CPU::flush_data_tlb() {
    tlb.flush_data();
    soft_tlb_flush();
}

void CPU::TLB_flush(const core::TlbFlushFilter& filter) {
    tlb.flush_selective(filter);
    if (filter.vaddr) {
        decode_cache.flush_page(*filter.vaddr & ~simrv::xlen::Address{0xFFF});
    } else {
        decode_cache.flush();
    }
    soft_tlb_flush_selective(filter);
}

void CPU::soft_tlb_flush() {
    if (simrv::compiler::unlikely(++soft_tlb_epoch == 0)) {
        soft_tlb_epoch = 1;
        for (auto& entry : soft_tlb_read) {
            entry.invalidate();
        }
        for (auto& entry : soft_tlb_write) {
            entry.invalidate();
        }
        for (auto& entry : soft_tlb_inst) {
            entry.invalidate();
        }
    }
}

void CPU::soft_tlb_flush_selective(const core::TlbFlushFilter& filter) {
    if (!filter.vaddr && !filter.asid) {
        soft_tlb_flush();
        return;
    }
    if (filter.vaddr) {
        const simrv::xlen::Address vpn = *filter.vaddr >> 12;
        const size_t tlb_idx = soft_tlb_index(vpn);
        auto invalidate_if_match = [&](SoftTlbEntry& entry) {
            if (entry.valid(soft_tlb_epoch)) {
                const uint64_t entry_vpn = (entry.tag >> 18) & 0xFFFFFFFFFFULL;
                if (entry_vpn == (vpn & 0xFFFFFFFFFFULL)) {
                    if (filter.asid) {
                        if (!entry.global && ((entry.tag >> 2) & 0xFFFFu) == *filter.asid) {
                            entry.invalidate();
                        }
                    } else {
                        entry.invalidate();
                    }
                }
            }
        };
        invalidate_if_match(soft_tlb_read[tlb_idx]);
        invalidate_if_match(soft_tlb_write[tlb_idx]);
        invalidate_if_match(soft_tlb_inst[tlb_idx]);
        return;
    }
    if (filter.asid) {
        auto invalidate_asid = [&](SoftTlbEntry& entry) {
            if (entry.valid(soft_tlb_epoch) && !entry.global) {
                const uint64_t entry_asid = (entry.tag >> 2) & 0xFFFFu;
                if (entry_asid == *filter.asid) {
                    entry.invalidate();
                }
            }
        };
        for (size_t i = 0; i < 2048; ++i) {
            invalidate_asid(soft_tlb_read[i]);
            invalidate_asid(soft_tlb_write[i]);
            invalidate_asid(soft_tlb_inst[i]);
        }
        return;
    }
}

auto CPU::get_mstatus(simrv::xlen::CsrValue mask) const -> simrv::xlen::CsrValue {
    return csr_file.getMstatus(mask);
}

void CPU::set_mstatus(simrv::xlen::CsrValue wdata) { csr_file.setMstatus(wdata); }

auto CPU::read_csr(simrv::xlen::CsrNumber addr) const
    -> std::expected<simrv::xlen::CsrValue, ExceptionCode> {
    return csr_file.read(addr);
}

auto CPU::write_csr(simrv::xlen::CsrNumber addr, simrv::xlen::CsrValue wdata)
    -> std::expected<void, ExceptionCode> {
    return csr_file.write(addr, wdata);
}

void CPU::mret() {
    const auto old_privilege = state_.priv;
    const auto old_mstatus = state_.mstatus;
    TrapController::mret(state_);
    if (state_.priv != old_privilege || state_.mstatus != old_mstatus) {
        TLB_flush();
    }
}

void CPU::sret() {
    const auto old_privilege = state_.priv;
    const auto old_mstatus = state_.mstatus;
    TrapController::sret(state_);
    if (state_.priv != old_privilege || state_.mstatus != old_mstatus) {
        TLB_flush();
    }
}

void CPU::plic_update_mip() { plic_mmio.update_mip(); }

void CPU::plic_set_irq(simrv::xlen::IrqNumber irq_num, simrv::xlen::IrqLevel state) {
    plic_mmio.set_irq(irq_num, state == simrv::xlen::IrqLevel::Asserted ? 1 : 0);
}

void CPU::plic_set_irq(simrv::xlen::IrqNumber irq_num, bool asserted) {
    plic_mmio.set_irq(irq_num, asserted ? 1 : 0);
}

void CPU::plic_set_irq(simrv::xlen::IrqNumber irq_num, int state) {
    plic_mmio.set_irq(irq_num, state != 0 ? 1 : 0);
}

void CPU::raise_exception(simrv::xlen::TrapCause cause, simrv::xlen::CsrValue tval) {
    const auto old_privilege = state_.priv;
    const auto old_mstatus = state_.mstatus;
    TrapController::raise_exception(*this, cause, tval);
    if (state_.priv != old_privilege || state_.mstatus != old_mstatus) {
        TLB_flush();
    }
}

void CPU::evaluate_timer_interrupt() {
    const simrv::xlen::CsrValue pending_before = state_.mip;
    simrv::xlen::Counter cur_mtime = 0;
    simrv::xlen::Counter cur_mtimecmp = 0;
    bool is_supervisor_timer = false;

    if (machine_ != nullptr) {
        auto& primary_clint = machine_->primary_hart().clint_mmio;
        cur_mtime = primary_clint.mtime.load(std::memory_order_relaxed);
        const auto hid = static_cast<size_t>(state_.mhartid);
        if (hid == 0) {
            cur_mtimecmp = primary_clint.mtimecmp.load(std::memory_order_relaxed);
            is_supervisor_timer = primary_clint.supervisor_timer.load(std::memory_order_relaxed);
        } else if (hid < simrv::device::Clint::kMaxClintHarts) {
            cur_mtimecmp = primary_clint.hart_mtimecmp.at(hid).load(std::memory_order_relaxed);
            is_supervisor_timer =
                primary_clint.hart_supervisor_timer.at(hid).load(std::memory_order_relaxed);
        }
    } else {
        cur_mtime = clint_mmio.mtime.load(std::memory_order_relaxed);
        cur_mtimecmp = clint_mmio.mtimecmp.load(std::memory_order_relaxed);
        is_supervisor_timer = clint_mmio.supervisor_timer.load(std::memory_order_relaxed);
    }

    const bool pending = (cur_mtime >= cur_mtimecmp);
    if (is_supervisor_timer) {
        state_.mip &= ~MipBit::Mtip;
        state_.stip_timer = pending;
    } else {
        state_.stip_timer = false;
        if (pending)
            state_.mip |= MipBit::Mtip;
        else
            state_.mip &= ~MipBit::Mtip;
    }
    bool stce = false;
    if constexpr (simrv::xlen::kIsXLen64) {
        stce = (state_.menvcfg & (1ULL << 63U)) != 0;
    } else {
        stce = (state_.menvcfgh & (1UL << 31U)) != 0;
    }
    state_.stip_sstc = stce && (cur_mtime >= state_.stimecmp);
    state_.refresh_supervisor_pending();
    if (machine_ != nullptr) {
        const auto log_transition = [&](MipBit bit, simrv::xlen::TrapCause code,
                                        std::string_view source) {
            const bool before = (pending_before & bit) != 0;
            const bool after = (state_.mip & bit) != 0;
            if (before != after) {
                machine_->trace().log_interrupt_signal(*this, kInterruptCauseBit | code, after,
                                                       source);
                machine_->notify_interrupt_transition(state_.mhartid, static_cast<uint32_t>(code),
                                                      after, source, clint_mmio.mcycle);
            }
        };
        log_transition(MipBit::Mtip, 7, "aclint_mtimer");
        log_transition(MipBit::Stip, 5, "aclint_mtimer");
    }
}

void CPU::queue_interrupt_line(core::MipBit bit, bool asserted) noexcept {
    constexpr std::array<core::MipBit, 6> lines{core::MipBit::Mtip, core::MipBit::Msip,
                                                core::MipBit::Meip, core::MipBit::Stip,
                                                core::MipBit::Ssip, core::MipBit::Seip};
    const auto it = std::ranges::find(lines, bit);
    if (it == lines.end()) return;
    const auto index = static_cast<unsigned>(std::distance(lines.begin(), it));
    const auto dirty = static_cast<uint16_t>(1U << index);
    const auto value = static_cast<uint16_t>(1U << (index + lines.size()));
    uint16_t current = pending_interrupt_lines_.load(std::memory_order_relaxed);
    uint16_t desired = 0;
    do {
        desired = static_cast<uint16_t>((current | dirty) & ~value);
        if (asserted) desired |= value;
    } while (!pending_interrupt_lines_.compare_exchange_weak(
        current, desired, std::memory_order_release, std::memory_order_relaxed));
}

void CPU::apply_pending_interrupt_lines(Machine& machine) {
    if (simrv::compiler::likely(!has_pending_interrupt_lines())) return;
    const uint16_t pending = pending_interrupt_lines_.exchange(0, std::memory_order_acquire);
    if (pending == 0) return;
    constexpr std::array<core::MipBit, 6> lines{core::MipBit::Mtip, core::MipBit::Msip,
                                                core::MipBit::Meip, core::MipBit::Stip,
                                                core::MipBit::Ssip, core::MipBit::Seip};
    constexpr std::array<simrv::xlen::TrapCause, 6> causes{7, 3, 11, 5, 1, 9};
    constexpr std::array<std::string_view, 6> sources{
        "aclint_mtimer", "aclint_mswi", "external_interrupt_controller",
        "aclint_mtimer", "aclint_mswi", "external_interrupt_controller"};
    for (size_t i = 0; i < lines.size(); ++i) {
        if ((pending & (1U << i)) == 0) continue;
        const auto line = lines[i];
        const bool before = (state_.mip & line) != 0;
        const bool asserted = (pending & (1U << (i + lines.size()))) != 0;
        if (i == 3)
            state_.stip_timer = asserted;
        else if (i == 4)
            state_.stip_software = asserted;
        else if (i == 5)
            state_.seip_external = asserted;
        else if (asserted)
            state_.mip |= line;
        else
            state_.mip &= ~line;
        if (i >= 3) state_.refresh_supervisor_pending();
        const bool after = (state_.mip & line) != 0;
        if (before != after) {
            machine.trace().log_interrupt_signal(*this, kInterruptCauseBit | causes[i], after,
                                                 sources[i]);
            machine.notify_interrupt_transition(state_.mhartid, static_cast<uint32_t>(causes[i]),
                                                after, sources[i], clint_mmio.mcycle);
        }
    }
}

void CPU::apply_hart_control_boundary() noexcept {
    const auto current = hart_status.load(std::memory_order_relaxed);
    if (simrv::compiler::likely(current == HartStatus::Started)) {
        return;
    }
    switch (current) {
        case HartStatus::StartPending:
        case HartStatus::ResumePending:
            hart_status.store(HartStatus::Started, std::memory_order_release);
            break;
        case HartStatus::StopPending:
            hart_status.store(HartStatus::Stopped, std::memory_order_release);
            break;
        case HartStatus::SuspendPending:
            hart_status.store(HartStatus::Suspended, std::memory_order_release);
            break;
        default:
            break;
    }
}

}  // namespace simrv::core
