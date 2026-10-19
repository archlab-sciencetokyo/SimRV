#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "simrv/CompilerHints.hpp"
#include "simrv/pipeline/OperationInfo.hpp"
#include "simrv/pipeline/OperationTraits.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

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
        simrv::xlen::Register forwarded_value{0};
        simrv::xlen::LatencyCycles latency{0};
        PipelineStage stage{PipelineStage::Fetch};
        bool can_forward{false};
    };

    static constexpr size_t kNumIntRegisters = 32;
    static constexpr size_t kNumFpRegisters = 32;
    static constexpr size_t kNumVecRegisters = 32;

    constexpr void reset() noexcept {
        int_busy_mask_ = 0;
        int_forwardable_mask_ = 0;
        fp_busy_mask_ = 0;
        vec_busy_mask_ = 0;
    }

    constexpr void reserve(operation::RegBank bank, simrv::xlen::RegId reg, PipelineStage stage,
                           simrv::xlen::LatencyCycles latency = 0, bool can_forward = false,
                           simrv::xlen::Register forwarded_value = 0) noexcept {
        const auto index = static_cast<size_t>(reg);
        switch (bank) {
            case operation::RegBank::Integer:
                if (index == 0 || index >= kNumIntRegisters) return;
                int_busy_mask_ |= (1U << index);
                if (can_forward) {
                    int_forwardable_mask_ |= (1U << index);
                } else {
                    int_forwardable_mask_ &= ~(1U << index);
                }
                int_registers_[index] = Entry{
                    .forwarded_value = forwarded_value,
                    .latency = latency,
                    .stage = stage,
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
                    .can_forward = can_forward,
                };
                return;
            default:
                return;
        }
    }

    constexpr void release(operation::RegBank bank, simrv::xlen::RegId reg) noexcept {
        const auto index = static_cast<size_t>(reg);
        switch (bank) {
            case operation::RegBank::Integer:
                if (index == 0 || index >= kNumIntRegisters) return;
                int_busy_mask_ &= ~(1U << index);
                int_forwardable_mask_ &= ~(1U << index);
                return;
            case operation::RegBank::Float:
                if (index >= kNumFpRegisters) return;
                fp_busy_mask_ &= ~(1U << index);
                return;
            case operation::RegBank::Vector:
                if (index >= kNumVecRegisters) return;
                vec_busy_mask_ &= ~(1U << index);
                return;
            default:
                return;
        }
    }

    [[nodiscard]] constexpr auto is_busy(operation::RegBank bank,
                                         simrv::xlen::RegId reg) const noexcept -> bool {
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

    [[nodiscard]] constexpr auto has_forwardable_int() const noexcept -> bool {
        return int_forwardable_mask_ != 0;
    }

    [[nodiscard]] constexpr auto can_forward_int(simrv::xlen::RegId reg) const noexcept -> bool {
        const auto index = static_cast<size_t>(reg);
        return (index > 0 && index < kNumIntRegisters) &&
               ((int_forwardable_mask_ & (1U << index)) != 0);
    }

    [[nodiscard]] constexpr auto get_forwarded_int_unchecked(simrv::xlen::RegId reg) const noexcept
        -> simrv::xlen::Register {
        return int_registers_[static_cast<size_t>(reg)].forwarded_value;
    }

    [[nodiscard]] constexpr auto can_forward(operation::RegBank bank,
                                             simrv::xlen::RegId reg) const noexcept -> bool {
        const auto index = static_cast<size_t>(reg);
        if (bank == operation::RegBank::Integer) {
            return (index > 0 && index < kNumIntRegisters) &&
                   ((int_forwardable_mask_ & (1U << index)) != 0);
        }
        if (!is_busy(bank, reg)) return false;
        const auto* entry = get_entry(bank, reg);
        return entry != nullptr && entry->can_forward;
    }

    [[nodiscard]] constexpr auto get_forwarded_value(operation::RegBank bank,
                                                     simrv::xlen::RegId reg) const noexcept
        -> simrv::xlen::Register {
        const auto index = static_cast<size_t>(reg);
        if (bank == operation::RegBank::Integer) {
            return (index > 0 && index < kNumIntRegisters &&
                    (int_forwardable_mask_ & (1U << index)) != 0)
                       ? int_registers_[index].forwarded_value
                       : 0;
        }
        if (!is_busy(bank, reg)) return 0;
        const auto* entry = get_entry(bank, reg);
        return (entry != nullptr && entry->can_forward) ? entry->forwarded_value : 0;
    }

    [[nodiscard]] constexpr auto get_stage(operation::RegBank bank,
                                           simrv::xlen::RegId reg) const noexcept
        -> std::optional<PipelineStage> {
        if (!is_busy(bank, reg)) return std::nullopt;
        const auto* entry = get_entry(bank, reg);
        if (entry != nullptr) {
            return entry->stage;
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr auto get_latency(operation::RegBank bank,
                                             simrv::xlen::RegId reg) const noexcept
        -> simrv::xlen::LatencyCycles {
        if (!is_busy(bank, reg)) return 0;
        const auto* entry = get_entry(bank, reg);
        return (entry != nullptr) ? entry->latency : 0;
    }

    [[nodiscard]] constexpr auto get_entry_data(operation::RegBank bank,
                                                simrv::xlen::RegId reg) const noexcept
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

        // Integer RAW dependencies: bitwise check against consumer rs1/rs2 mask (excluding x0)
        const uint32_t read_mask =
            ((traits.reads_rs1_int ? (1U << static_cast<size_t>(consumer.rs1)) : 0U) |
             (traits.reads_rs2_int ? (1U << static_cast<size_t>(consumer.rs2)) : 0U)) &
            ~1U;

        if (read_mask != 0) {
            const uint32_t blocking =
                forwarding_enabled ? (int_busy_mask_ & ~int_forwardable_mask_) : int_busy_mask_;
            if (simrv::compiler::unlikely((blocking & read_mask) != 0)) {
                return true;
            }
        }

        // Floating-point RAW dependencies (conservative: stall until retired and released)
        if (simrv::compiler::unlikely(fp_busy_mask_ != 0)) {
            uint32_t fp_read_mask = 0;
            if (traits.reads_rs1_fp) fp_read_mask |= (1U << static_cast<size_t>(consumer.rs1));
            if (traits.reads_rs2_fp) fp_read_mask |= (1U << static_cast<size_t>(consumer.rs2));
            if (traits.reads_rs3_fp) {
                const auto rs3 = static_cast<size_t>((consumer.ir >> 27U) & 0x1FU);
                fp_read_mask |= (1U << rs3);
            }
            if ((fp_busy_mask_ & fp_read_mask) != 0) {
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
            if (slot->writes_int && slot->wb_dest != simrv::xlen::RegId::Zero) {
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
        const auto flush_bank = [stage](const auto& array, uint32_t& mask) {
            uint32_t cur = mask;
            while (cur != 0) {
                const auto idx = std::countr_zero(cur);
                const auto& entry = array[idx];
                if (static_cast<uint8_t>(entry.stage) <= static_cast<uint8_t>(stage)) {
                    mask &= ~(1U << idx);
                }
                cur &= cur - 1;
            }
        };
        flush_bank(int_registers_, int_busy_mask_);
        int_forwardable_mask_ &= int_busy_mask_;
        flush_bank(fp_registers_, fp_busy_mask_);
        flush_bank(vec_registers_, vec_busy_mask_);
    }

   private:
    uint32_t int_busy_mask_{0};
    uint32_t int_forwardable_mask_{0};
    uint32_t fp_busy_mask_{0};
    uint32_t vec_busy_mask_{0};

    std::array<Entry, kNumIntRegisters> int_registers_{};
    std::array<Entry, kNumFpRegisters> fp_registers_{};
    std::array<Entry, kNumVecRegisters> vec_registers_{};

    [[nodiscard]] constexpr auto get_entry(operation::RegBank bank, simrv::xlen::RegId reg) noexcept
        -> Entry* {
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

    [[nodiscard]] constexpr auto get_entry(operation::RegBank bank,
                                           simrv::xlen::RegId reg) const noexcept -> const Entry* {
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
