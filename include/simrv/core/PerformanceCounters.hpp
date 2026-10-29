/**
 * @file PerformanceCounters.hpp
 * @brief RISC-V Hardware Performance Monitor (Zihpm) subsystem and profiling counters.
 */
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

#include "simrv/CompilerHints.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {

/**
 * @brief SimRV event identifiers for RISC-V HPM event selector registers
 * (mhpmevent3..mhpmevent31).
 *
 * This enum is the canonical event catalog. Runtime event sites notify this catalog; programmable
 * HPM counters are the only accumulated architectural event totals. IDs 1..20 are SimRV-defined,
 * implementation-specific selector values, not portable standard encodings.
 */
enum class HpmEvent : uint32_t {
    None = 0,
#define SIMRV_HPM_EVENT(name, id, label, description) name = id,
#define SIMRV_HPM_GENERIC_EVENT(code, name)
#include "simrv/core/HpmEventCatalog.def"
#undef SIMRV_HPM_GENERIC_EVENT
#undef SIMRV_HPM_EVENT
    Count
};

/**
 * @class PerformanceCounters
 * @brief Hardware Performance Monitor (HPM) and instruction mix / latency profiling subsystem.
 *
 * Implements the standard RISC-V Zihpm specification supporting 29 configurable 64-bit event
 * counters (indices 3..31) mapped to mhpmcounter* / hpmcounter* CSRs, programmed via mhpmevent*
 * event selectors, and inhibited via mcountinhibit.
 */
class PerformanceCounters {
   public:
    static constexpr size_t kNumHpmCounters = 32;

    PerformanceCounters() = default;

    /// Reset all counters, event selectors, and profiling statistics.
    void reset() noexcept;

    /// Read an event selector CSR (mhpmevent3..31).
    [[nodiscard]] auto read_hpmevent(size_t index) const noexcept -> uint64_t;
    /// Write an event selector CSR (mhpmevent3..31).
    void write_hpmevent(size_t index, uint64_t value, bool has_hypervisor = true) noexcept;

    /// Read a 64-bit performance counter CSR (mhpmcounter3..31 / hpmcounter3..31).
    [[nodiscard]] auto read_hpmcounter(size_t index) const noexcept -> uint64_t;
    /// Return the sticky overflow flags, with bit N describing mhpmcounterN.
    [[nodiscard]] auto read_overflow_mask() const noexcept -> uint32_t;
    /// Write a 64-bit performance counter CSR (mhpmcounter3..31).
    void write_hpmcounter(size_t index, uint64_t value) noexcept;

    /// True when at least one programmable counter selects an instruction-class event.
    [[nodiscard]] auto has_instruction_class_listeners() const noexcept -> bool {
        return instruction_class_listeners_;
    }

    /**
     * @brief Notify the performance monitor of an architectural event.
     *
     * Uses a precomputed listening bitmask so that unmonitored events cost a single zero-check.
     */
    SIMRV_ALWAYS_INLINE auto notify_event(
        HpmEvent event, uint64_t count = 1, uint32_t mcountinhibit = 0,
        simrv::xlen::PrivilegeLevel privilege = simrv::xlen::PrivilegeLevel::Machine,
        bool virtualized = false) noexcept
        -> bool {
        const auto ev_idx = static_cast<size_t>(event);
        if (simrv::compiler::unlikely(ev_idx < static_cast<size_t>(HpmEvent::Count))) {
            uint32_t mask = event_listening_mask_[ev_idx] & ~mcountinhibit;
            if (virtualized && privilege == simrv::xlen::PrivilegeLevel::Supervisor)
                mask &= ~virtual_supervisor_inhibit_mask_;
            else if (virtualized && privilege == simrv::xlen::PrivilegeLevel::User)
                mask &= ~virtual_user_inhibit_mask_;
            else if (privilege == simrv::xlen::PrivilegeLevel::Machine)
                mask &= ~machine_inhibit_mask_;
            else if (privilege == simrv::xlen::PrivilegeLevel::Supervisor)
                mask &= ~supervisor_inhibit_mask_;
            else
                mask &= ~user_inhibit_mask_;
            bool overflow_irq = false;
            if (simrv::compiler::unlikely(mask != 0)) {
                uint32_t bits = mask;
                while (bits != 0) {
                    const auto idx = static_cast<size_t>(std::countr_zero(bits));
                    const auto previous = hpmcounter_[idx];
                    hpmcounter_[idx] += count;
                    if (hpmcounter_[idx] < previous && (hpmevent_[idx] & kOverflowFlag) == 0) {
                        hpmevent_[idx] |= kOverflowFlag;
                        overflow_irq = true;
                    }
                    bits &= bits - 1;
                }
            }
            return overflow_irq;
        }
        return false;
    }

    /**
     * @brief Record instruction retirement with elapsed machine cycles.
     * @param op_id Retired instruction operation ID.
     * @param cycles Elapsed machine cycles attributed to this instruction.
     */
    void record_retire(isa::OperationId op_id, simrv::xlen::Counter cycles) noexcept;

    /// Instruction count per operation ID.
    [[nodiscard]] auto inst_count(isa::OperationId op_id) const noexcept -> simrv::xlen::Counter {
        const auto idx = static_cast<size_t>(op_id);
        return idx < inst_mix_count_.size() ? inst_mix_count_[idx] : 0;
    }

    /// Cumulative execution cycles per operation ID.
    [[nodiscard]] auto inst_cycles(isa::OperationId op_id) const noexcept -> simrv::xlen::Counter {
        const auto idx = static_cast<size_t>(op_id);
        return idx < inst_mix_cycles_.size() ? inst_mix_cycles_[idx] : 0;
    }

    /// Cumulative stall cycles attributed to operation ID.
    [[nodiscard]] auto inst_stalls(isa::OperationId op_id) const noexcept -> simrv::xlen::Counter {
        const auto idx = static_cast<size_t>(op_id);
        return idx < inst_mix_stalls_.size() ? inst_mix_stalls_[idx] : 0;
    }

    [[nodiscard]] const auto& inst_mix_counts() const noexcept { return inst_mix_count_; }
    [[nodiscard]] const auto& inst_mix_cycles() const noexcept { return inst_mix_cycles_; }
    [[nodiscard]] const auto& inst_mix_stalls() const noexcept { return inst_mix_stalls_; }

   private:
    static constexpr uint64_t kOverflowFlag = uint64_t{1} << 63;
    static constexpr uint64_t kMachineInhibit = uint64_t{1} << 62;
    static constexpr uint64_t kSupervisorInhibit = uint64_t{1} << 61;
    static constexpr uint64_t kUserInhibit = uint64_t{1} << 60;
    static constexpr uint64_t kVirtualSupervisorInhibit = uint64_t{1} << 59;
    static constexpr uint64_t kVirtualUserInhibit = uint64_t{1} << 58;

    void rebuild_listening_mask() noexcept;

    std::array<uint64_t, kNumHpmCounters> hpmevent_{};
    std::array<uint64_t, kNumHpmCounters> hpmcounter_{};
    std::array<uint32_t, static_cast<size_t>(HpmEvent::Count)> event_listening_mask_{};
    uint32_t machine_inhibit_mask_{};
    uint32_t supervisor_inhibit_mask_{};
    uint32_t user_inhibit_mask_{};
    uint32_t virtual_supervisor_inhibit_mask_{};
    uint32_t virtual_user_inhibit_mask_{};
    bool instruction_class_listeners_{};

    std::array<simrv::xlen::Counter, isa::OperationIdCount> inst_mix_count_{};
    std::array<simrv::xlen::Counter, isa::OperationIdCount> inst_mix_cycles_{};
    std::array<simrv::xlen::Counter, isa::OperationIdCount> inst_mix_stalls_{};
};

}  // namespace simrv::core
