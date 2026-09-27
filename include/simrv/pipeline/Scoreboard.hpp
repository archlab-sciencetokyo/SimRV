#pragma once

#include <array>
#include <bit>
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
        Register forwarded_value{0};
        LatencyCycles latency{0};
        PipelineStage stage{PipelineStage::Execute};
        bool busy{false};
        bool can_forward{false};
    };

    static constexpr size_t kNumIntRegisters = 32;
    static constexpr size_t kNumFpRegisters = 32;
    static constexpr size_t kNumVecRegisters = 32;

    constexpr void reset() noexcept {
        while (int_busy_mask_ != 0) {
            const auto idx = std::countr_zero(int_busy_mask_);
            int_registers_[idx] = Entry{};
            int_busy_mask_ &= int_busy_mask_ - 1;
        }
        while (fp_busy_mask_ != 0) {
            const auto idx = std::countr_zero(fp_busy_mask_);
            fp_registers_[idx] = Entry{};
            fp_busy_mask_ &= fp_busy_mask_ - 1;
        }
        while (vec_busy_mask_ != 0) {
            const auto idx = std::countr_zero(vec_busy_mask_);
            vec_registers_[idx] = Entry{};
            vec_busy_mask_ &= vec_busy_mask_ - 1;
        }
    }

    constexpr void reserve(operation::RegBank bank, RegId reg, PipelineStage stage,
                           LatencyCycles latency = 0, bool can_forward = false,
                           Register forwarded_value = 0) noexcept {
        const auto index = static_cast<size_t>(reg);
        switch (bank) {
            case operation::RegBank::Integer:
                if (index == 0 || index >= kNumIntRegisters) return;
                int_busy_mask_ |= (1U << index);
                int_registers_[index] = Entry{
                    .forwarded_value = forwarded_value,
                    .latency = latency,
                    .stage = stage,
                    .busy = true,
                    .can_forward = can_forward,
                };
                return;
            case operation::RegBank::Float:
                if (index >= kNumFpRegisters) return;
                fp_busy_mask_ |= (1U << index);
                fp_registers_[index] = Entry{
                    .forwarded_value = forwarded_value,
                    .latency = latency,
                    .stage = stage,
                    .busy = true,
                    .can_forward = can_forward,
                };
                return;
            case operation::RegBank::Vector:
                if (index >= kNumVecRegisters) return;
                vec_busy_mask_ |= (1U << index);
                vec_registers_[index] = Entry{
                    .forwarded_value = forwarded_value,
                    .latency = latency,
                    .stage = stage,
                    .busy = true,
                    .can_forward = can_forward,
                };
                return;
            default:
                return;
        }
    }

    constexpr void release(operation::RegBank bank, RegId reg) noexcept {
        const auto index = static_cast<size_t>(reg);
        switch (bank) {
            case operation::RegBank::Integer:
                if (index == 0 || index >= kNumIntRegisters) return;
                int_busy_mask_ &= ~(1U << index);
                int_registers_[index] = Entry{};
                return;
            case operation::RegBank::Float:
                if (index >= kNumFpRegisters) return;
                fp_busy_mask_ &= ~(1U << index);
                fp_registers_[index] = Entry{};
                return;
            case operation::RegBank::Vector:
                if (index >= kNumVecRegisters) return;
                vec_busy_mask_ &= ~(1U << index);
                vec_registers_[index] = Entry{};
                return;
            default:
                return;
        }
    }

    [[nodiscard]] constexpr auto is_busy(operation::RegBank bank, RegId reg) const noexcept
        -> bool {
        const auto index = static_cast<size_t>(reg);
        switch (bank) {
            case operation::RegBank::Integer:
                return (index > 0 && index < kNumIntRegisters) &&
                       ((int_busy_mask_ & (1U << index)) != 0);
            case operation::RegBank::Float:
                return (index < kNumFpRegisters) && ((fp_busy_mask_ & (1U << index)) != 0);
            case operation::RegBank::Vector:
                return (index < kNumVecRegisters) && ((vec_busy_mask_ & (1U << index)) != 0);
            default:
                return false;
        }
    }

    [[nodiscard]] constexpr auto can_forward(operation::RegBank bank, RegId reg) const noexcept
        -> bool {
        if (!is_busy(bank, reg)) return false;
        const auto* entry = get_entry(bank, reg);
        return entry != nullptr && entry->can_forward;
    }

    [[nodiscard]] constexpr auto get_forwarded_value(operation::RegBank bank,
                                                     RegId reg) const noexcept -> Register {
        if (!is_busy(bank, reg)) return 0;
        const auto* entry = get_entry(bank, reg);
        return (entry != nullptr && entry->can_forward) ? entry->forwarded_value : 0;
    }

    [[nodiscard]] constexpr auto get_stage(operation::RegBank bank, RegId reg) const noexcept
        -> std::optional<PipelineStage> {
        if (!is_busy(bank, reg)) return std::nullopt;
        const auto* entry = get_entry(bank, reg);
        if (entry != nullptr) {
            return entry->stage;
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr auto get_latency(operation::RegBank bank, RegId reg) const noexcept
        -> LatencyCycles {
        if (!is_busy(bank, reg)) return 0;
        const auto* entry = get_entry(bank, reg);
        return (entry != nullptr) ? entry->latency : 0;
    }

    [[nodiscard]] constexpr auto get_entry_data(operation::RegBank bank, RegId reg) const noexcept
        -> std::optional<Entry> {
        if (!is_busy(bank, reg)) return std::nullopt;
        const auto* entry = get_entry(bank, reg);
        if (entry != nullptr) {
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
        const auto flush_bank = [stage](auto& array, uint32_t& mask) {
            uint32_t cur = mask;
            while (cur != 0) {
                const auto idx = std::countr_zero(cur);
                auto& entry = array[idx];
                if (static_cast<uint8_t>(entry.stage) <= static_cast<uint8_t>(stage)) {
                    entry = Entry{};
                    mask &= ~(1U << idx);
                }
                cur &= cur - 1;
            }
        };
        flush_bank(int_registers_, int_busy_mask_);
        flush_bank(fp_registers_, fp_busy_mask_);
        flush_bank(vec_registers_, vec_busy_mask_);
    }

   private:
    uint32_t int_busy_mask_{0};
    uint32_t fp_busy_mask_{0};
    uint32_t vec_busy_mask_{0};

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
