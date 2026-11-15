#pragma once

#include <array>
#include <cstdint>

#include "simrv/memory/Mmu.hpp"
#include "simrv/memory/TileLinkProtocol.hpp"
#include "simrv/pipeline/BranchPredictor.hpp"
#include "simrv/pipeline/PipelineContext.hpp"
#include "simrv/pipeline/Scoreboard.hpp"
#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

namespace simrv::pipeline {

struct InstructionFillState {
    static constexpr size_t kLineBytes = 64;
    simrv::xlen::PhysAddr line_base{0};
    uint32_t next_offset = 0;
    simrv::memory::TlSourceId source = 0;
    bool active = false;
    bool request_pending = false;
    std::array<simrv::xlen::Byte, kLineBytes> line_data{};

    constexpr void reset() noexcept { *this = {}; }
};

struct InstructionPrefetchState {
    static constexpr size_t kLineBytes = 64;
    simrv::xlen::PhysAddr line_base{0};
    simrv::memory::TlSourceId source = 0;
    bool active = false;
    bool request_pending = false;

    constexpr void reset() noexcept { *this = {}; }
};

struct DataTransferState {
    simrv::xlen::PhysAddr address{};
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
    PipelineStage stage = PipelineStage::Fetch;
    bool instruction_active = false;
    bool retired_this_cycle = false;
    bool icache_miss = false;
    bool dcache_miss = false;
    bool tlb_miss = false;
    bool waiting_for_interconnect = false;
    bool memory_complete = false;
    InstructionFillState instruction_fill{};
    InstructionPrefetchState instruction_prefetch{};
    DataTransferState data_transfer{};
    TimedPageWalkState instruction_walk{};
    TimedPageWalkState data_walk{};
    uint64_t pending_host_value = 0;
    uint8_t pending_host_write_kind = 0;
    simrv::xlen::LatencyCycles host_write_remaining = 0;
    bool host_write_pending = false;

    constexpr void reset_instruction() noexcept {
        stage = PipelineStage::Fetch;
        instruction_active = false;
        retired_this_cycle = false;
        icache_miss = false;
        dcache_miss = false;
        tlb_miss = false;
        waiting_for_interconnect = false;
        memory_complete = false;
        instruction_fill.reset();
        instruction_prefetch.reset();
        data_transfer.reset();
        instruction_walk.reset();
        data_walk.reset();
        pending_host_value = 0;
        pending_host_write_kind = 0;
        host_write_remaining = 0;
        host_write_pending = false;
    }
};

struct CycleInstructionSlot {
    PipelineContext context{};
    simrv::xlen::LatencyCycles remaining_latency = 0;
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
    simrv::xlen::RegId wb_dest = static_cast<simrv::xlen::RegId>(0);
    simrv::xlen::Register wb_val = 0;
    BranchPrediction prediction{};

    constexpr void clear() noexcept {
        valid = false;
        remaining_latency = 0;
        serializing = false;
        executed = false;
        memory_complete = false;
        icache_miss = false;
        dcache_miss = false;
        tlb_miss = false;
        writes_int = false;
        writes_fp = false;
        wb_valid = false;
        wb_dest = static_cast<simrv::xlen::RegId>(0);
        wb_val = 0;
        context.pending_exception.reset();
        context.memory_load_complete = false;
        context.mem_paddr = simrv::xlen::PhysAddr{0};
        context.sc_reservation_checked = false;
        context.sc_reservation_succeeded = false;
        context.ir = 0;
        context.ir_org = 0;
        context.cpc = simrv::xlen::VirtAddr{0};
        prediction = {};
    }
    constexpr void invalidate() noexcept {
        valid = false;
        remaining_latency = 0;
        executed = false;
        memory_complete = false;
        wb_valid = false;
        context.memory_load_complete = false;
    }
};

struct HartPipelineState {
    std::array<CycleInstructionSlot, 9> storage_{};
    CycleInstructionSlot* fetch = &storage_[0];
    CycleInstructionSlot* fetch2 = &storage_[1];
    CycleInstructionSlot* decode = &storage_[2];
    CycleInstructionSlot* issue = &storage_[3];
    CycleInstructionSlot* execute = &storage_[4];
    CycleInstructionSlot* memory = &storage_[5];
    CycleInstructionSlot* memory2 = &storage_[6];
    CycleInstructionSlot* writeback = &storage_[7];
    CycleInstructionSlot* retired = &storage_[8];
    simrv::xlen::VirtAddr fetch_pc{};
    bool initialized = false;
    bool frontend_blocked = false;
    bool retired_this_cycle = false;
    bool data_hazard_stall = false;
    bool control_flush = false;
    uint8_t control_recovery_bubbles = 0;
    simrv::xlen::LatencyCycles frontend_refill_stall = 0;
    Scoreboard scoreboard{};

    [[nodiscard]] constexpr auto slot(PipelineStage s) noexcept -> CycleInstructionSlot& {
        return storage_[std::to_underlying(s)];
    }
    [[nodiscard]] constexpr auto slot(PipelineStage s) const noexcept
        -> const CycleInstructionSlot& {
        return storage_[std::to_underlying(s)];
    }

    constexpr void sync_scoreboard(bool include_decode = true) noexcept {
        scoreboard.sync_from_pipeline(*this, include_decode);
    }

    constexpr HartPipelineState() noexcept = default;

    constexpr HartPipelineState(const HartPipelineState& other) noexcept { *this = other; }

    constexpr auto operator=(const HartPipelineState& other) noexcept -> HartPipelineState& {
        if (this != &other) {
            storage_ = other.storage_;
            const auto rebind = [this, &other](const CycleInstructionSlot* ptr) noexcept {
                return &storage_[ptr - other.storage_.data()];
            };
            fetch = rebind(other.fetch);
            fetch2 = rebind(other.fetch2);
            decode = rebind(other.decode);
            issue = rebind(other.issue);
            execute = rebind(other.execute);
            memory = rebind(other.memory);
            memory2 = rebind(other.memory2);
            writeback = rebind(other.writeback);
            retired = rebind(other.retired);
            fetch_pc = other.fetch_pc;
            initialized = other.initialized;
            frontend_blocked = other.frontend_blocked;
            retired_this_cycle = other.retired_this_cycle;
            data_hazard_stall = other.data_hazard_stall;
            control_flush = other.control_flush;
            control_recovery_bubbles = other.control_recovery_bubbles;
            frontend_refill_stall = other.frontend_refill_stall;
            scoreboard = other.scoreboard;
        }
        return *this;
    }

    constexpr void flush_younger() noexcept {
        fetch->invalidate();
        fetch2->invalidate();
        decode->invalidate();
        issue->invalidate();
        scoreboard.flush_from_stage(PipelineStage::Issue);
    }
    constexpr void reset() noexcept {
        for (auto& slot : storage_) slot.clear();
        fetch = &storage_[0];
        fetch2 = &storage_[1];
        decode = &storage_[2];
        issue = &storage_[3];
        execute = &storage_[4];
        memory = &storage_[5];
        memory2 = &storage_[6];
        writeback = &storage_[7];
        retired = &storage_[8];
        fetch_pc = simrv::xlen::VirtAddr{};
        initialized = false;
        frontend_blocked = false;
        retired_this_cycle = false;
        data_hazard_stall = false;
        control_flush = false;
        control_recovery_bubbles = 0;
        frontend_refill_stall = 0;
        scoreboard.reset();
    }
};

}  // namespace simrv::pipeline
