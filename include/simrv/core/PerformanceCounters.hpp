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
 * @brief Standard architectural event identifiers for RISC-V HPM event selector registers
 * (mhpmevent3..mhpmevent31).
 */
enum class HpmEvent : uint32_t {
    None = 0,
    Cycles = 1,
    Instructions = 2,
    ICacheMiss = 3,
    DCacheMiss = 4,
    DCacheWriteback = 5,
    ITlbMiss = 6,
    DTlbMiss = 7,
    BranchInstruction = 8,
    BranchMispredict = 9,
    StallCycles = 10,
    DataHazardStall = 11,
    StructuralStall = 12,
    InterconnectStall = 13,
    LoadInstruction = 14,
    StoreInstruction = 15,
    IntegerAluInstruction = 16,
    MulDivInstruction = 17,
    FloatInstruction = 18,
    VectorInstruction = 19,
    CompressedInstruction = 20,
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
    void write_hpmevent(size_t index, uint64_t value) noexcept;

    /// Read a 64-bit performance counter CSR (mhpmcounter3..31 / hpmcounter3..31).
    [[nodiscard]] auto read_hpmcounter(size_t index) const noexcept -> uint64_t;
    /// Write a 64-bit performance counter CSR (mhpmcounter3..31).
    void write_hpmcounter(size_t index, uint64_t value) noexcept;

    /**
     * @brief Notify the performance monitor of an architectural event.
     *
     * Uses a precomputed listening bitmask so that unmonitored events cost a single zero-check.
     */
    SIMRV_ALWAYS_INLINE void notify_event(HpmEvent event, uint64_t count = 1,
                                          uint32_t mcountinhibit = 0) noexcept {
        const auto ev_idx = static_cast<size_t>(event);
        if (simrv::compiler::unlikely(ev_idx < static_cast<size_t>(HpmEvent::Count))) {
            const uint32_t mask = event_listening_mask_[ev_idx] & ~mcountinhibit;
            if (simrv::compiler::unlikely(mask != 0)) {
                uint32_t bits = mask;
                while (bits != 0) {
                    const auto idx = static_cast<size_t>(std::countr_zero(bits));
                    hpmcounter_[idx] += count;
                    bits &= bits - 1;
                }
            }
        }
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
    void rebuild_listening_mask() noexcept;

    std::array<uint64_t, kNumHpmCounters> hpmevent_{};
    std::array<uint64_t, kNumHpmCounters> hpmcounter_{};
    std::array<uint32_t, static_cast<size_t>(HpmEvent::Count)> event_listening_mask_{};

    std::array<simrv::xlen::Counter, isa::OperationIdCount> inst_mix_count_{};
    std::array<simrv::xlen::Counter, isa::OperationIdCount> inst_mix_cycles_{};
    std::array<simrv::xlen::Counter, isa::OperationIdCount> inst_mix_stalls_{};
};

}  // namespace simrv::core
