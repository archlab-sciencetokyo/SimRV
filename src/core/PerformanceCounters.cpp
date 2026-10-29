/**
 * @file PerformanceCounters.cpp
 * @brief RISC-V Hardware Performance Monitor (Zihpm) subsystem and profiling implementation.
 */
#include "simrv/core/PerformanceCounters.hpp"

#include <algorithm>

namespace simrv::core {

void PerformanceCounters::reset() noexcept {
    hpmevent_.fill(0);
    hpmcounter_.fill(0);
    event_listening_mask_.fill(0);
    machine_inhibit_mask_ = 0;
    supervisor_inhibit_mask_ = 0;
    user_inhibit_mask_ = 0;
    virtual_supervisor_inhibit_mask_ = 0;
    virtual_user_inhibit_mask_ = 0;
    instruction_class_listeners_ = false;
    inst_mix_count_.fill(0);
    inst_mix_cycles_.fill(0);
    inst_mix_stalls_.fill(0);
}

auto PerformanceCounters::read_hpmevent(size_t index) const noexcept -> uint64_t {
    if (index < 3 || index >= kNumHpmCounters) return 0;
    return hpmevent_[index];
}

void PerformanceCounters::write_hpmevent(size_t index, uint64_t value, bool has_hypervisor) noexcept {
    if (index < 3 || index >= kNumHpmCounters) return;
    // Sscofpmf defines VS/VU filtering when the H extension is implemented.
    constexpr uint64_t kBaseImplemented = (uint64_t{0xF} << 60) | ((uint64_t{1} << 56) - 1);
    constexpr uint64_t kVirtualInhibitMask = uint64_t{0x3} << 58;
    const uint64_t kImplemented =
        kBaseImplemented | (has_hypervisor ? kVirtualInhibitMask : uint64_t{0});
    hpmevent_[index] = value & kImplemented;
    rebuild_listening_mask();
}

auto PerformanceCounters::read_hpmcounter(size_t index) const noexcept -> uint64_t {
    if (index < 3 || index >= kNumHpmCounters) return 0;
    return hpmcounter_[index];
}

auto PerformanceCounters::read_overflow_mask() const noexcept -> uint32_t {
    uint32_t result = 0;
    for (size_t i = 3; i < kNumHpmCounters; ++i) {
        if ((hpmevent_[i] & (uint64_t{1} << 63)) != 0) result |= uint32_t{1} << i;
    }
    return result;
}

void PerformanceCounters::write_hpmcounter(size_t index, uint64_t value) noexcept {
    if (index < 3 || index >= kNumHpmCounters) return;
    hpmcounter_[index] = value;
}

void PerformanceCounters::record_retire(isa::OperationId op_id,
                                        simrv::xlen::Counter cycles) noexcept {
    const auto idx = static_cast<size_t>(op_id);
    if (simrv::compiler::likely(idx < inst_mix_count_.size())) {
        inst_mix_count_[idx]++;
        inst_mix_cycles_[idx] += cycles;
        if (cycles > 1) {
            inst_mix_stalls_[idx] += (cycles - 1);
        }
    }
}

void PerformanceCounters::rebuild_listening_mask() noexcept {
    event_listening_mask_.fill(0);
    machine_inhibit_mask_ = 0;
    supervisor_inhibit_mask_ = 0;
    user_inhibit_mask_ = 0;
    virtual_supervisor_inhibit_mask_ = 0;
    virtual_user_inhibit_mask_ = 0;
    instruction_class_listeners_ = false;
    for (size_t i = 3; i < kNumHpmCounters; ++i) {
        const auto raw = hpmevent_[i];
        const auto ev = raw & ((uint64_t{1} << 56) - 1);
        if (ev > 0 && ev < static_cast<uint64_t>(HpmEvent::Count)) {
            event_listening_mask_[static_cast<size_t>(ev)] |= (1U << i);
            instruction_class_listeners_ |=
                ev >= static_cast<uint64_t>(HpmEvent::BranchInstruction) &&
                ev <= static_cast<uint64_t>(HpmEvent::CompressedInstruction);
        }
        if ((raw & (uint64_t{1} << 62)) != 0) machine_inhibit_mask_ |= (1U << i);
        if ((raw & (uint64_t{1} << 61)) != 0) supervisor_inhibit_mask_ |= (1U << i);
        if ((raw & (uint64_t{1} << 60)) != 0) user_inhibit_mask_ |= (1U << i);
        if ((raw & kVirtualSupervisorInhibit) != 0)
            virtual_supervisor_inhibit_mask_ |= (1U << i);
        if ((raw & kVirtualUserInhibit) != 0) virtual_user_inhibit_mask_ |= (1U << i);
    }
}

}  // namespace simrv::core
