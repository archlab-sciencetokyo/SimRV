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
    inst_mix_count_.fill(0);
    inst_mix_cycles_.fill(0);
    inst_mix_stalls_.fill(0);
}

auto PerformanceCounters::read_hpmevent(size_t index) const noexcept -> uint64_t {
    if (index < 3 || index >= kNumHpmCounters) return 0;
    return hpmevent_[index];
}

void PerformanceCounters::write_hpmevent(size_t index, uint64_t value) noexcept {
    if (index < 3 || index >= kNumHpmCounters) return;
    hpmevent_[index] = value;
    rebuild_listening_mask();
}

auto PerformanceCounters::read_hpmcounter(size_t index) const noexcept -> uint64_t {
    if (index < 3 || index >= kNumHpmCounters) return 0;
    return hpmcounter_[index];
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
    for (size_t i = 3; i < kNumHpmCounters; ++i) {
        const auto ev = hpmevent_[i];
        if (ev > 0 && ev < static_cast<uint64_t>(HpmEvent::Count)) {
            event_listening_mask_[static_cast<size_t>(ev)] |= (1U << i);
        }
    }
}

}  // namespace simrv::core
