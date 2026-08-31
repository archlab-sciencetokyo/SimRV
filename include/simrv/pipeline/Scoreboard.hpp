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
                           LatencyCycles latency = 0, bool can_forward = false) noexcept {
        auto* entry = get_entry(bank, reg);
        if (entry != nullptr) {
            entry->busy = true;
            entry->stage = stage;
            entry->latency = latency;
            entry->can_forward = can_forward;
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
