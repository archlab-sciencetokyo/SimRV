#pragma once

#include <array>
#include <cstdint>

#include "simrv/Define.hpp"
#include "simrv/memory/Mmu.hpp"
#include "simrv/memory/TileLinkProtocol.hpp"
#include "simrv/pipeline/BranchPredictor.hpp"
#include "simrv/pipeline/PipelineContext.hpp"
#include "simrv/pipeline/Scoreboard.hpp"

namespace simrv::pipeline {

/// Authoritative progress of one instruction through the semantic CA kernel.
using CycleStage = PipelineStage;

struct InstructionFillState {
    static constexpr size_t kLineBytes = 32;
    Address line_base = 0;
    uint32_t next_offset = 0;
    simrv::memory::TlSourceId source = 0;
    bool active = false;
    bool request_pending = false;
    std::array<Byte, kLineBytes> line_data{};

    constexpr void reset() noexcept { *this = {}; }
};

struct DataTransferState {
    Address address = 0;
    simrv::memory::TlSourceId source = 0;
    bool active = false;
    bool is_write = false;
    bool line_fill = false;

    constexpr void reset() noexcept { *this = {}; }
};

struct TimedPageWalkState {
    PageWalkState walk{};
    simrv::memory::TlSourceId source = 0;
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
    LatencyCycles remaining_latency = 0;
    bool valid = false;
    bool serializing = false;
    bool executed = false;
    bool memory_complete = false;
    bool icache_miss = false;
    bool dcache_miss = false;
    bool tlb_miss = false;
    bool writes_int = false;
    bool writes_fp = false;
    bool wb_valid = false;
    RegId wb_dest = static_cast<RegId>(0);
    Register wb_val = 0;
    BranchPrediction prediction{};

    constexpr void clear() noexcept { *this = {}; }
    constexpr void invalidate() noexcept {
        valid = false;
        remaining_latency = 0;
        executed = false;
        memory_complete = false;
        wb_valid = false;
    }
};

struct HartPipelineState {
    std::array<CycleInstructionSlot, 6> storage_{};
    CycleInstructionSlot* fetch = &storage_[0];
    CycleInstructionSlot* decode = &storage_[1];
    CycleInstructionSlot* execute = &storage_[2];
    CycleInstructionSlot* memory = &storage_[3];
    CycleInstructionSlot* writeback = &storage_[4];
    CycleInstructionSlot* retired = &storage_[5];
    Address fetch_pc = 0;
    bool initialized = false;
    bool frontend_blocked = false;
    bool retired_this_cycle = false;
    bool data_hazard_stall = false;
    bool control_flush = false;

    [[nodiscard]] constexpr auto slot(PipelineStage s) noexcept -> CycleInstructionSlot& {
        return storage_[std::to_underlying(s)];
    }
    [[nodiscard]] constexpr auto slot(PipelineStage s) const noexcept
        -> const CycleInstructionSlot& {
        return storage_[std::to_underlying(s)];
    }

    constexpr HartPipelineState() noexcept = default;

    constexpr HartPipelineState(const HartPipelineState& other) noexcept {
        storage_ = other.storage_;
        fetch = &storage_[other.fetch - other.storage_.data()];
        decode = &storage_[other.decode - other.storage_.data()];
        execute = &storage_[other.execute - other.storage_.data()];
        memory = &storage_[other.memory - other.storage_.data()];
        writeback = &storage_[other.writeback - other.storage_.data()];
        retired = &storage_[other.retired - other.storage_.data()];
        fetch_pc = other.fetch_pc;
        initialized = other.initialized;
        frontend_blocked = other.frontend_blocked;
        retired_this_cycle = other.retired_this_cycle;
        data_hazard_stall = other.data_hazard_stall;
        control_flush = other.control_flush;
    }

    constexpr auto operator=(const HartPipelineState& other) noexcept -> HartPipelineState& {
        if (this != &other) {
            storage_ = other.storage_;
            fetch = &storage_[other.fetch - other.storage_.data()];
            decode = &storage_[other.decode - other.storage_.data()];
            execute = &storage_[other.execute - other.storage_.data()];
            memory = &storage_[other.memory - other.storage_.data()];
            writeback = &storage_[other.writeback - other.storage_.data()];
            retired = &storage_[other.retired - other.storage_.data()];
            fetch_pc = other.fetch_pc;
            initialized = other.initialized;
            frontend_blocked = other.frontend_blocked;
            retired_this_cycle = other.retired_this_cycle;
            data_hazard_stall = other.data_hazard_stall;
            control_flush = other.control_flush;
        }
        return *this;
    }

    constexpr void flush_younger() noexcept {
        fetch->invalidate();
        decode->invalidate();
    }
    constexpr void reset() noexcept {
        for (auto& slot : storage_) slot.clear();
        fetch = &storage_[0];
        decode = &storage_[1];
        execute = &storage_[2];
        memory = &storage_[3];
        writeback = &storage_[4];
        retired = &storage_[5];
        fetch_pc = 0;
        initialized = false;
        frontend_blocked = false;
        retired_this_cycle = false;
        data_hazard_stall = false;
        control_flush = false;
    }
};

}  // namespace simrv::pipeline
