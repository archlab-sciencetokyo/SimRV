#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "simrv/Define.hpp"
#include "simrv/pipeline/OperationInfo.hpp"
#include "simrv/pipeline/OperationTraits.hpp"

namespace simrv::pipeline {

enum class PipelineStage : uint8_t {
    Fetch,
    Decode,
    Execute,
    Memory,
    Writeback,
    Commit,
};

/**
 * @class Scoreboard
 * @brief Register-bank-aware reservation scoreboard tracking producing stages and latencies
 * for integer, floating-point, and vector register files.
 */
class Scoreboard {
   public:
    struct Entry {
        bool busy{false};
        PipelineStage stage{PipelineStage::Execute};
        LatencyCycles latency{0};
        bool can_forward{false};
        Register forwarded_value{0};
    };

    static constexpr size_t kNumIntRegisters = 32;
    static constexpr size_t kNumFpRegisters = 32;
    static constexpr size_t kNumVecRegisters = 32;

    constexpr void reset() noexcept {
        int_registers_.fill(Entry{});
        fp_registers_.fill(Entry{});
        vec_registers_.fill(Entry{});
    }

    constexpr void reserve(operation::RegBank bank, RegId reg, PipelineStage stage,
                           LatencyCycles latency = 0, bool can_forward = false,
                           Register forwarded_value = 0) noexcept {
        auto* entry = get_entry(bank, reg);
        if (entry != nullptr) {
            entry->busy = true;
            entry->stage = stage;
            entry->latency = latency;
            entry->can_forward = can_forward;
            entry->forwarded_value = forwarded_value;
        }
    }

    constexpr void release(operation::RegBank bank, RegId reg) noexcept {
        auto* entry = get_entry(bank, reg);
        if (entry != nullptr) {
            *entry = Entry{};
        }
    }

    [[nodiscard]] constexpr auto is_busy(operation::RegBank bank, RegId reg) const noexcept
        -> bool {
        const auto* entry = get_entry(bank, reg);
        return entry != nullptr && entry->busy;
    }

    [[nodiscard]] constexpr auto can_forward(operation::RegBank bank, RegId reg) const noexcept
        -> bool {
        const auto* entry = get_entry(bank, reg);
        return entry != nullptr && entry->busy && entry->can_forward;
    }

    [[nodiscard]] constexpr auto get_forwarded_value(operation::RegBank bank,
                                                     RegId reg) const noexcept -> Register {
        const auto* entry = get_entry(bank, reg);
        return (entry != nullptr && entry->busy && entry->can_forward) ? entry->forwarded_value : 0;
    }

    [[nodiscard]] constexpr auto get_stage(operation::RegBank bank, RegId reg) const noexcept
        -> std::optional<PipelineStage> {
        const auto* entry = get_entry(bank, reg);
        if (entry != nullptr && entry->busy) {
            return entry->stage;
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr auto get_latency(operation::RegBank bank, RegId reg) const noexcept
        -> LatencyCycles {
        const auto* entry = get_entry(bank, reg);
        return (entry != nullptr && entry->busy) ? entry->latency : 0;
    }

    [[nodiscard]] constexpr auto get_entry_data(operation::RegBank bank, RegId reg) const noexcept
        -> std::optional<Entry> {
        const auto* entry = get_entry(bank, reg);
        if (entry != nullptr && entry->busy) {
            return *entry;
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr auto has_raw_hazard(const DecodedInstruction& consumer,
                                                bool forwarding_enabled) const noexcept -> bool {
        const auto& traits = consumer.traits;

        // Integer RAW dependencies
        if (traits.reads_rs1_int && consumer.rs1 != RegId::Zero) {
            if (is_busy(operation::RegBank::Integer, consumer.rs1)) {
                if (!forwarding_enabled ||
                    !can_forward(operation::RegBank::Integer, consumer.rs1)) {
                    return true;
                }
            }
        }
        if (traits.reads_rs2_int && consumer.rs2 != RegId::Zero) {
            if (is_busy(operation::RegBank::Integer, consumer.rs2)) {
                if (!forwarding_enabled ||
                    !can_forward(operation::RegBank::Integer, consumer.rs2)) {
                    return true;
                }
            }
        }

        // Floating-point RAW dependencies (conservative: stall until retired and released)
        if (traits.reads_rs1_fp) {
            if (is_busy(operation::RegBank::Float, consumer.rs1)) {
                return true;
            }
        }
        if (traits.reads_rs2_fp) {
            if (is_busy(operation::RegBank::Float, consumer.rs2)) {
                return true;
            }
        }
        if (traits.reads_rs3_fp) {
            const RegId rs3 = static_cast<RegId>((consumer.ir >> 27U) & 0x1FU);
            if (is_busy(operation::RegBank::Float, rs3)) {
                return true;
            }
        }

        return false;
    }

    template <typename PipelineStateLike>
    constexpr void sync_from_pipeline(const PipelineStateLike& pipe,
                                      bool include_decode = true) noexcept {
        reset();
        auto reserve_slot = [this](const auto* slot, PipelineStage stage) {
            if (!slot || !slot->valid) return;
            if (slot->writes_int && slot->wb_dest != RegId::Zero) {
                reserve(operation::RegBank::Integer, slot->wb_dest, stage, slot->remaining_latency,
                        slot->wb_valid, slot->wb_val);
            } else if (slot->writes_fp) {
                reserve(operation::RegBank::Float, slot->wb_dest, stage, slot->remaining_latency,
                        slot->wb_valid);
            }
        };
        // Process oldest stage to youngest stage:
        // Writeback -> Memory -> Execute -> (Decode if requested)
        // Younger stage overwrites reservation for the same register
        reserve_slot(pipe.writeback, PipelineStage::Writeback);
        reserve_slot(pipe.memory, PipelineStage::Memory);
        reserve_slot(pipe.execute, PipelineStage::Execute);
        if (include_decode) {
            reserve_slot(pipe.decode, PipelineStage::Decode);
        }
    }

    constexpr void flush_from_stage(PipelineStage stage) noexcept {
        const auto flush_bank = [stage](auto& array) {
            for (auto& entry : array) {
                if (entry.busy &&
                    static_cast<uint8_t>(entry.stage) <= static_cast<uint8_t>(stage)) {
                    entry = Entry{};
                }
            }
        };
        flush_bank(int_registers_);
        flush_bank(fp_registers_);
        flush_bank(vec_registers_);
    }

   private:
    std::array<Entry, kNumIntRegisters> int_registers_{};
    std::array<Entry, kNumFpRegisters> fp_registers_{};
    std::array<Entry, kNumVecRegisters> vec_registers_{};

    [[nodiscard]] constexpr auto get_entry(operation::RegBank bank, RegId reg) noexcept -> Entry* {
        const auto index = static_cast<size_t>(reg);
        switch (bank) {
            case operation::RegBank::Integer:
                if (index == 0 || index >= kNumIntRegisters) return nullptr;
                return &int_registers_[index];
            case operation::RegBank::Float:
                if (index >= kNumFpRegisters) return nullptr;
                return &fp_registers_[index];
            case operation::RegBank::Vector:
                if (index >= kNumVecRegisters) return nullptr;
                return &vec_registers_[index];
            case operation::RegBank::None:
            default:
                return nullptr;
        }
    }

    [[nodiscard]] constexpr auto get_entry(operation::RegBank bank, RegId reg) const noexcept
        -> const Entry* {
        const auto index = static_cast<size_t>(reg);
        switch (bank) {
            case operation::RegBank::Integer:
                if (index == 0 || index >= kNumIntRegisters) return nullptr;
                return &int_registers_[index];
            case operation::RegBank::Float:
                if (index >= kNumFpRegisters) return nullptr;
                return &fp_registers_[index];
            case operation::RegBank::Vector:
                if (index >= kNumVecRegisters) return nullptr;
                return &vec_registers_[index];
            case operation::RegBank::None:
            default:
                return nullptr;
        }
    }
};

}  // namespace simrv::pipeline
