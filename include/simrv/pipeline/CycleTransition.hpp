#pragma once

#include <array>
#include <cstdint>

#include "simrv/Define.hpp"
#include "simrv/memory/Mmu.hpp"
#include "simrv/pipeline/BranchPredictor.hpp"
#include "simrv/pipeline/PipelineContext.hpp"

namespace simrv::pipeline {

/// Authoritative progress of one instruction through the semantic CA kernel.
enum class CycleStage : uint8_t { Fetch, Decode, Execute, Memory, Writeback, Commit };

struct InstructionFillState {
    static constexpr size_t kLineBytes = 32;
    Address line_base = 0;
    uint32_t next_offset = 0;
    uint8_t source = 0;
    bool active = false;
    bool request_pending = false;
    std::array<Byte, kLineBytes> line_data{};

    constexpr void reset() noexcept { *this = {}; }
};

struct DataTransferState {
    Address address = 0;
    uint8_t source = 0;
    bool active = false;
    bool is_write = false;
    bool line_fill = false;

    constexpr void reset() noexcept { *this = {}; }
};

struct TimedPageWalkState {
    PageWalkState walk{};
    uint8_t source = 0;
    bool active = false;
    bool request_pending = false;

    constexpr void reset() noexcept { *this = {}; }
};

struct HartCycleState {
    CycleStage stage = CycleStage::Fetch;
    bool instruction_active = false;
    bool retired_this_cycle = false;
    bool icache_miss = false;
    bool dcache_miss = false;
    bool tlb_miss = false;
    bool waiting_for_interconnect = false;
    bool memory_complete = false;
    InstructionFillState instruction_fill{};
    DataTransferState data_transfer{};
    TimedPageWalkState instruction_walk{};
    TimedPageWalkState data_walk{};

    constexpr void reset_instruction() noexcept {
        stage = CycleStage::Fetch;
        instruction_active = false;
        retired_this_cycle = false;
        icache_miss = false;
        dcache_miss = false;
        tlb_miss = false;
        waiting_for_interconnect = false;
        memory_complete = false;
        instruction_fill.reset();
        data_transfer.reset();
        instruction_walk.reset();
        data_walk.reset();
    }
};

struct CycleInstructionSlot {
    PipelineContext context{};
    uint32_t remaining_latency = 0;
    bool valid = false;
    bool serializing = false;
    bool executed = false;
    bool memory_complete = false;
    bool icache_miss = false;
    bool dcache_miss = false;
    bool tlb_miss = false;
    bool writes_int = false;
    bool wb_valid = false;
    RegId wb_dest = static_cast<RegId>(0);
    Register wb_val = 0;
    BranchPrediction prediction{};

    constexpr void clear() noexcept { *this = {}; }
    /// Mark a transferred slot unavailable without touching its diagnostic payload.  Every
    /// consumer gates architectural work on valid; retaining the payload avoids a bulk clear on
    /// the CA critical path while still preventing stale latency from being reported as a stall.
    constexpr void invalidate() noexcept {
        valid = false;
        remaining_latency = 0;
    }
};

struct HartPipelineState {
    CycleInstructionSlot fetch{};
    CycleInstructionSlot decode{};
    CycleInstructionSlot execute{};
    CycleInstructionSlot memory{};
    CycleInstructionSlot writeback{};
    CycleInstructionSlot retired{};
    Address fetch_pc = 0;
    bool initialized = false;
    bool frontend_blocked = false;
    bool retired_this_cycle = false;
    bool data_hazard_stall = false;
    bool control_flush = false;

    constexpr void flush_younger() noexcept {
        fetch.invalidate();
        decode.invalidate();
    }
    constexpr void reset() noexcept { *this = {}; }
};

}  // namespace simrv::pipeline
