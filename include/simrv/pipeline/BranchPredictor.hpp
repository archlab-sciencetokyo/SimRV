#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/Define.hpp"
#include "simrv/isa/Base.hpp"
#include "simrv/pipeline/DecodedInstruction.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::pipeline {

/**
 * @enum BranchPredictorType
 * @brief Selectable branch prediction algorithms.
 */
enum class BranchPredictorType : uint8_t {
    Static = 0,      ///< Always Not-Taken / BTFNT
    Bimodal = 1,     ///< 2-bit saturating counter table
    GShare = 2,      ///< Global history XOR PC into 2-bit counters
    Tournament = 3,  ///< Hybrid chooser selecting between Bimodal and GShare
    Disabled = 4     ///< Always fetch sequentially; all taken control transfers mispredict
};

[[nodiscard]] auto parse_branch_predictor_type(std::string_view name)
    -> std::optional<BranchPredictorType>;
[[nodiscard]] auto to_string(BranchPredictorType type) noexcept -> std::string_view;

/**
 * @struct BranchPredictorConfig
 * @brief Parameters for configuring predictor sizes and algorithms.
 */
struct BranchPredictorConfig {
    BranchPredictorType type = BranchPredictorType::GShare;
    BhtIndex bht_entries = 1024;
    BtbIndex btb_entries = 256;
    RasIndex ras_entries = 16;
    uint32_t ghr_bits = 10;
    bool enable_btb = true;
    bool enable_ras = true;
    uint8_t pc_shift = 1;
    bool untagged_btb = false;
    /// Consult the direction table and untagged BTB before decode. This permits aliases to
    /// redirect ordinary instructions, matching RTL predictors that cannot oracle-filter the
    /// lookup using the decoded opcode.
    bool predict_non_control = false;
    bool jump_uses_direction_counter = false;
    bool jump_uses_current_btb = false;
    /// Model a synchronous block-RAM lookup. The cycle kernel latches the read address before
    /// redirect resolution, matching predictors whose output changes on the following edge.
    bool registered_btb_read = false;
    /// Initial 2-bit saturating counter value loaded into every BHT entry on reset.
    /// 0 = Strongly Not Taken (matches CFU-Proving-Ground / RVProc RTL `initial btb[i]=0`).
    /// 1 = Weakly Not Taken (default for all other profiles).
    uint8_t bht_initial_state = 1;
};

/**
 * @struct BranchPrediction
 * @brief Result of a branch prediction lookup at Fetch stage.
 */
struct BranchPrediction {
    bool is_control = false;
    bool is_branch = false;
    bool is_jump = false;
    bool is_call = false;
    bool is_return = false;
    bool predicted_taken = false;
    Address predicted_target = 0;
    BhtIndex bht_index = 0;
    GlobalHistory ghr_snapshot = 0;
    bool btb_hit = false;
    bool ras_hit = false;
    bool false_control_alias = false;

    [[nodiscard]] constexpr auto direction() const noexcept -> BranchDirection {
        return predicted_taken ? BranchDirection::Taken : BranchDirection::NotTaken;
    }
};

inline constexpr BranchPrediction kDefaultBranchPrediction{};

/**
 * @struct BranchFeedback
 * @brief Resolution outcome sent from Execute/Decode stage to train the predictor.
 */
struct BranchFeedback {
    Address pc = 0;
    bool actual_taken = false;
    Address actual_target = 0;
    isa::Opcode opcode = static_cast<isa::Opcode>(0);
    isa::OperationId op_id = isa::OperationId::UNKNOWN;
    RegId rd = static_cast<RegId>(0);
    RegId rs1 = static_cast<RegId>(0);
    const BranchPrediction& prediction = kDefaultBranchPrediction;

    [[nodiscard]] constexpr auto direction() const noexcept -> BranchDirection {
        return actual_taken ? BranchDirection::Taken : BranchDirection::NotTaken;
    }
};

inline constexpr std::array<uint8_t, 8> kSaturateFlat = {0, 0, 1, 2, 1, 2, 3, 3};

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto saturate_up(uint8_t val) noexcept -> uint8_t {
    return kSaturateFlat[4 | (val & 0x3)];
}

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto saturate(uint8_t val, bool taken) noexcept
    -> uint8_t {
    return kSaturateFlat[(static_cast<unsigned>(taken) << 2) | (val & 0x3)];
}

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto is_taken_prediction(uint8_t counter) noexcept
    -> bool {
    return (counter & 0x2) != 0;
}

/**
 * @struct BranchPredictorStats
 * @brief Comprehensive branch prediction telemetry.
 */
struct BranchPredictorStats {
    Counter total_branches = 0;
    Counter conditional_branches = 0;
    Counter direct_jumps = 0;
    Counter indirect_jumps = 0;
    Counter function_calls = 0;
    Counter function_returns = 0;

    Counter direction_predictions = 0;
    Counter direction_hits = 0;
    Counter direction_misses = 0;

    Counter target_predictions = 0;
    Counter target_hits = 0;
    Counter target_misses = 0;

    Counter btb_lookups = 0;
    Counter btb_hits = 0;
    Counter btb_misses = 0;

    Counter ras_pushes = 0;
    Counter ras_pops = 0;
    Counter ras_hits = 0;
    Counter ras_misses = 0;

    Counter misprediction_flushes = 0;
    Counter misprediction_penalty_cycles = 0;

    [[nodiscard]] auto overall_accuracy() const noexcept -> double {
        if (total_branches == 0) return 100.0;
        return (static_cast<double>(direction_hits) / static_cast<double>(total_branches)) * 100.0;
    }

    [[nodiscard]] auto direction_accuracy() const noexcept -> double {
        if (direction_predictions == 0) return 100.0;
        return (static_cast<double>(direction_hits) / static_cast<double>(direction_predictions)) *
               100.0;
    }

    [[nodiscard]] auto target_accuracy() const noexcept -> double {
        if (target_predictions == 0) return 100.0;
        return (static_cast<double>(target_hits) / static_cast<double>(target_predictions)) * 100.0;
    }

    [[nodiscard]] auto btb_hit_rate() const noexcept -> double {
        if (btb_lookups == 0) return 0.0;
        return (static_cast<double>(btb_hits) / static_cast<double>(btb_lookups)) * 100.0;
    }

    [[nodiscard]] auto ras_accuracy() const noexcept -> double {
        if (ras_pops == 0) return 100.0;
        return (static_cast<double>(ras_hits) / static_cast<double>(ras_pops)) * 100.0;
    }
};

/**
 * @class BranchPredictor
 * @brief High-performance, modular cycle-accurate branch predictor.
 */
class BranchPredictor {
   public:
    struct BtbEntry {
        Address tag = 0;
        Address target = 0;
        bool valid = false;
    };

    BranchPredictor();
    explicit BranchPredictor(const BranchPredictorConfig& config);

    void configure(const BranchPredictorConfig& config);
    void reset();

    [[nodiscard]] auto predict(Address pc, const DecodedInstruction& inst) -> BranchPrediction;
    [[nodiscard]] auto predict(VirtAddr pc, const DecodedInstruction& inst) -> BranchPrediction {
        return predict(pc.raw(), inst);
    }
    void latch_btb_read(Address pc);
    void restore_speculation(const BranchPrediction& prediction);

    [[nodiscard]] auto stats() const noexcept -> const BranchPredictorStats& { return stats_; }
    [[nodiscard]] auto config() const noexcept -> const BranchPredictorConfig& { return config_; }
    [[nodiscard]] auto ghr() const noexcept -> uint32_t { return ghr_; }
    [[nodiscard]] auto ras_depth() const noexcept -> size_t { return ras_count_; }
    [[nodiscard]] auto ras_peek() const noexcept -> std::optional<Address>;
    [[nodiscard]] auto bht_distribution() const noexcept -> std::array<size_t, 4>;

    SIMRV_ALWAYS_INLINE void update_btb(Address pc, Address target) noexcept {
        if (config_.enable_btb && !btb_.empty()) {
            const uint32_t btb_idx = static_cast<uint32_t>(pc >> config_.pc_shift) & btb_mask_;
            btb_[btb_idx] = BtbEntry{.tag = pc, .target = target, .valid = true};
        }
    }

    [[nodiscard]] SIMRV_ALWAYS_INLINE auto get_bht_index(Address pc,
                                                         uint32_t ghr_val) const noexcept
        -> uint32_t {
        const uint32_t pc_idx = static_cast<uint32_t>(pc >> config_.pc_shift);
        if (config_.type == BranchPredictorType::GShare) {
            return (pc_idx ^ ghr_val) & bht_mask_;
        }
        return pc_idx & bht_mask_;
    }

    SIMRV_ALWAYS_INLINE void update_direction(const BranchFeedback& feedback) {
        if (config_.type == BranchPredictorType::Disabled ||
            config_.type == BranchPredictorType::Static) {
            return;
        }

        const Address pc = feedback.pc;
        const bool actual_taken = feedback.actual_taken;

        if (simrv::compiler::unlikely(config_.type == BranchPredictorType::Tournament)) {
            const uint32_t gshare_idx =
                (static_cast<uint32_t>(pc >> 1) ^ feedback.prediction.ghr_snapshot) & bht_mask_;
            const uint32_t bimodal_idx = static_cast<uint32_t>(pc >> 1) & bht_mask_;

            const bool gshare_pred = is_taken_prediction(bht_[gshare_idx]);
            const bool bimodal_pred = is_taken_prediction(bimodal_bht_[bimodal_idx]);

            if (gshare_pred != bimodal_pred) {
                chooser_table_[gshare_idx] =
                    saturate(chooser_table_[gshare_idx], gshare_pred == actual_taken);
            }

            bht_[gshare_idx] = saturate(bht_[gshare_idx], actual_taken);
            bimodal_bht_[bimodal_idx] = saturate(bimodal_bht_[bimodal_idx], actual_taken);
            return;
        }

        const uint32_t bht_idx = feedback.prediction.bht_index;
        if (simrv::compiler::likely(bht_idx < bht_.size())) {
            bht_[bht_idx] = saturate(bht_[bht_idx], actual_taken);
        }
    }

    SIMRV_ALWAYS_INLINE void update(const BranchFeedback& feedback) {
        if (!feedback.prediction.is_control) {
            return;
        }

        ++stats_.total_branches;

        if (feedback.prediction.is_call) ++stats_.function_calls;
        if (feedback.prediction.is_return) ++stats_.function_returns;

        if (feedback.prediction.is_branch) {
            ++stats_.conditional_branches;
            ++stats_.direction_predictions;

            const bool dir_match = (feedback.actual_taken == feedback.prediction.predicted_taken);
            stats_.direction_hits += dir_match;
            stats_.direction_misses += !dir_match;

            update_direction(feedback);

            if (feedback.prediction.false_control_alias) {
                return;
            }

            if (feedback.actual_taken) {
                update_btb(feedback.pc, feedback.actual_target);
            }
        } else if (feedback.opcode == isa::Opcode::Jal) {
            ++stats_.direct_jumps;
            if (config_.jump_uses_direction_counter && !bht_.empty()) {
                const auto index = get_bht_index(feedback.pc, feedback.prediction.ghr_snapshot);
                bht_[index] = saturate_up(bht_[index]);
            }
            update_btb(feedback.pc, feedback.actual_target);
        } else if (feedback.opcode == isa::Opcode::Jalr) {
            ++stats_.indirect_jumps;
            ++stats_.target_predictions;

            const bool target_match =
                (feedback.actual_target == feedback.prediction.predicted_target);
            stats_.target_hits += target_match;
            stats_.target_misses += !target_match;

            if (feedback.prediction.is_return && feedback.prediction.ras_hit) {
                stats_.ras_hits += target_match;
                stats_.ras_misses += !target_match;
            }

            update_btb(feedback.pc, feedback.actual_target);
        }
    }

   private:
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto predict_direction(Address pc,
                                                             const DecodedInstruction& inst,
                                                             uint32_t& bht_idx) -> bool;

    void ras_push(Address return_addr);
    auto ras_pop() -> std::optional<Address>;

    BranchPredictorConfig config_{};
    BranchPredictorStats stats_{};

    // Direction tables: 2-bit counters (0=SN, 1=WN, 2=WT, 3=ST)
    std::vector<uint8_t> bht_{};
    std::vector<uint8_t> bimodal_bht_{};    // For tournament mode
    std::vector<uint8_t> chooser_table_{};  // For tournament mode (0,1 = Bimodal, 2,3 = GShare)

    uint32_t ghr_ = 0;
    uint32_t ghr_mask_ = 0;
    uint32_t bht_mask_ = 0;

    // BTB
    std::vector<BtbEntry> btb_{};
    uint32_t btb_mask_ = 0;
    BtbEntry registered_btb_entry_{};
    uint8_t registered_bht_counter_ = 0;

    // RAS
    std::vector<Address> ras_{};
    size_t ras_head_ = 0;
    size_t ras_count_ = 0;
};

}  // namespace simrv::pipeline
