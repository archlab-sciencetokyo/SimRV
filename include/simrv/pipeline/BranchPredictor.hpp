#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/CompilerHints.hpp"
#include "simrv/isa/InstructionFields.hpp"
#include "simrv/pipeline/DecodedInstruction.hpp"
#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

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
    simrv::xlen::BhtIndex bht_entries = 1024;
    simrv::xlen::BtbIndex btb_entries = 256;
    simrv::xlen::RasIndex ras_entries = 16;
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
    simrv::xlen::Address predicted_target = 0;
    simrv::xlen::BhtIndex bht_index = 0;
    simrv::xlen::GlobalHistory ghr_snapshot = 0;
    bool is_control = false;
    bool is_branch = false;
    bool is_jump = false;
    bool is_call = false;
    bool is_return = false;
    bool predicted_taken = false;
    bool btb_hit = false;
    bool ras_hit = false;
    bool false_control_alias = false;

    constexpr void reset() noexcept {
        predicted_target = 0;
        bht_index = 0;
        ghr_snapshot = 0;
        is_control = false;
        is_branch = false;
        is_jump = false;
        is_call = false;
        is_return = false;
        predicted_taken = false;
        btb_hit = false;
        ras_hit = false;
        false_control_alias = false;
    }

    [[nodiscard]] constexpr auto direction() const noexcept -> simrv::xlen::BranchDirection {
        return predicted_taken ? simrv::xlen::BranchDirection::Taken
                               : simrv::xlen::BranchDirection::NotTaken;
    }
};

inline constexpr BranchPrediction kDefaultBranchPrediction{};

/**
 * @struct BranchFeedback
 * @brief Resolution outcome sent from Execute/Decode stage to train the predictor.
 */
struct BranchFeedback {
    simrv::xlen::Address pc = 0;
    bool actual_taken = false;
    simrv::xlen::Address actual_target = 0;
    isa::Opcode opcode = static_cast<isa::Opcode>(0);
    isa::OperationId op_id = isa::OperationId::ILLEGAL;
    simrv::xlen::RegId rd = static_cast<simrv::xlen::RegId>(0);
    simrv::xlen::RegId rs1 = static_cast<simrv::xlen::RegId>(0);
    const BranchPrediction& prediction = kDefaultBranchPrediction;

    [[nodiscard]] constexpr auto direction() const noexcept -> simrv::xlen::BranchDirection {
        return actual_taken ? simrv::xlen::BranchDirection::Taken
                            : simrv::xlen::BranchDirection::NotTaken;
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
    simrv::xlen::Counter total_branches = 0;
    simrv::xlen::Counter conditional_branches = 0;
    simrv::xlen::Counter direct_jumps = 0;
    simrv::xlen::Counter indirect_jumps = 0;
    simrv::xlen::Counter function_calls = 0;
    simrv::xlen::Counter function_returns = 0;

    simrv::xlen::Counter direction_predictions = 0;
    simrv::xlen::Counter direction_hits = 0;
    simrv::xlen::Counter direction_misses = 0;

    simrv::xlen::Counter target_predictions = 0;
    simrv::xlen::Counter target_hits = 0;
    simrv::xlen::Counter target_misses = 0;

    simrv::xlen::Counter btb_lookups = 0;
    simrv::xlen::Counter btb_hits = 0;
    simrv::xlen::Counter btb_misses = 0;

    simrv::xlen::Counter ras_pushes = 0;
    simrv::xlen::Counter ras_pops = 0;
    simrv::xlen::Counter ras_hits = 0;
    simrv::xlen::Counter ras_misses = 0;

    simrv::xlen::Counter misprediction_flushes = 0;
    simrv::xlen::Counter misprediction_penalty_cycles = 0;

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
        simrv::xlen::Address tag = 0;
        simrv::xlen::Address target = 0;
        bool valid = false;
    };

    BranchPredictor();
    explicit BranchPredictor(const BranchPredictorConfig& config);

    void configure(const BranchPredictorConfig& config);
    void reset();

    [[nodiscard]] constexpr auto has_registered_btb() const noexcept -> bool {
        return config_.registered_btb_read;
    }

    SIMRV_ALWAYS_INLINE void latch_btb_read(simrv::xlen::Address pc) noexcept {
        if (!config_.registered_btb_read || btb_.empty() || bht_.empty()) return;
        const uint32_t btb_idx = static_cast<uint32_t>(pc >> config_.pc_shift) & btb_mask_;
        registered_btb_entry_ = btb_[btb_idx];
        registered_bht_counter_ = bht_[get_bht_index(pc, ghr_)];
    }

    SIMRV_ALWAYS_INLINE void predict(simrv::xlen::Address pc, const DecodedInstruction& inst,
                                     BranchPrediction& pred) noexcept {
        const auto opcode = inst.opcode;
        const bool is_branch = (opcode == isa::Opcode::Branch);
        const bool is_jal = (opcode == isa::Opcode::Jal);
        const bool is_jalr = (opcode == isa::Opcode::Jalr);

        if (simrv::compiler::likely(!is_branch && !is_jal && !is_jalr)) {
            pred.reset();
            if (simrv::compiler::unlikely(config_.predict_non_control &&
                                          config_.type == BranchPredictorType::Bimodal &&
                                          config_.enable_btb && config_.untagged_btb)) {
                pred.bht_index = get_bht_index(pc, ghr_);
                const auto& entry =
                    config_.registered_btb_read
                        ? registered_btb_entry_
                        : btb_[static_cast<uint32_t>(pc >> config_.pc_shift) & btb_mask_];
                const uint8_t counter =
                    config_.registered_btb_read ? registered_bht_counter_ : bht_[pred.bht_index];
                if (entry.valid && is_taken_prediction(counter)) {
                    pred.is_control = true;
                    pred.is_branch = true;
                    pred.predicted_taken = true;
                    pred.predicted_target = entry.target;
                    pred.btb_hit = true;
                    pred.false_control_alias = true;
                }
            }
            return;
        }

        pred.reset();
        pred.is_control = true;
        pred.is_branch = is_branch;
        pred.is_jump = is_jal || is_jalr;
        pred.ghr_snapshot = ghr_;

        if (config_.type == BranchPredictorType::Disabled) {
            pred.predicted_taken = false;
            pred.predicted_target = pc + (inst.cinsn != 0u ? 2 : 4);
            return;
        }

        const simrv::xlen::Address inst_len = (inst.cinsn != 0u ? 2 : 4);
        const simrv::xlen::Address ret_addr = pc + inst_len;

        // Detect RISC-V function calls and returns based on link registers
        if (is_jal || is_jalr) {
            pred.is_call = (inst.rd == simrv::xlen::RegId::Ra || inst.rd == simrv::xlen::RegId::T0);
            pred.is_return =
                is_jalr &&
                (inst.rd != simrv::xlen::RegId::Ra && inst.rd != simrv::xlen::RegId::T0) &&
                (inst.rs1 == simrv::xlen::RegId::Ra || inst.rs1 == simrv::xlen::RegId::T0);
        }

        if (is_jal) {
            pred.predicted_target = pc + static_cast<simrv::xlen::Address>(inst.imm);
            if (config_.untagged_btb) {
                const uint32_t btb_idx = static_cast<uint32_t>(pc >> config_.pc_shift) & btb_mask_;
                const auto& entry = config_.registered_btb_read && !config_.jump_uses_current_btb
                                        ? registered_btb_entry_
                                        : btb_[btb_idx];
                const auto counter = config_.registered_btb_read ? registered_bht_counter_
                                                                 : bht_[get_bht_index(pc, ghr_)];
                pred.predicted_taken = entry.valid && (!config_.jump_uses_direction_counter ||
                                                       is_taken_prediction(counter));
                if (pred.predicted_taken) {
                    pred.predicted_target = entry.target;
                    pred.btb_hit = true;
                } else {
                    pred.predicted_target = ret_addr;
                    pred.btb_hit = false;
                }
            } else {
                pred.predicted_taken = true;
                pred.btb_hit = true;
            }
            if (pred.is_call && config_.enable_ras) {
                ras_push(ret_addr);
            }
            return;
        }

        if (is_jalr) {
            pred.predicted_taken = true;
            if (pred.is_return && config_.enable_ras) {
                if (auto top = ras_pop()) {
                    pred.predicted_target = *top;
                    pred.ras_hit = true;
                    return;
                }
            }
            if (pred.is_call && config_.enable_ras) {
                ras_push(ret_addr);
            }
            // Check BTB for JALR target
            if (config_.enable_btb && !btb_.empty()) {
                const uint32_t btb_idx = static_cast<uint32_t>(pc >> config_.pc_shift) & btb_mask_;
                const auto& entry = btb_[btb_idx];
                if (entry.valid && (config_.untagged_btb || entry.tag == pc)) {
                    pred.predicted_target = entry.target;
                    pred.btb_hit = true;
                    return;
                }
            }
            pred.predicted_target = 0;  // Unknown target until execute/decode
            pred.btb_hit = false;
            return;
        }

        // Conditional Branch
        pred.predicted_taken = predict_direction(pc, inst, pred.bht_index);
        if (pred.predicted_taken) {
            pred.predicted_target = config_.registered_btb_read
                                        ? registered_btb_entry_.target
                                        : pc + static_cast<simrv::xlen::Address>(inst.imm);
        } else {
            pred.predicted_target = ret_addr;
        }

        // Speculative GHR shift for conditional branches
        ghr_ = ((ghr_ << 1) | (pred.predicted_taken ? 1u : 0u)) & ghr_mask_;
    }

    [[nodiscard]] SIMRV_ALWAYS_INLINE auto predict(simrv::xlen::Address pc,
                                                   const DecodedInstruction& inst)
        -> BranchPrediction {
        BranchPrediction pred{};
        predict(pc, inst, pred);
        return pred;
    }

    [[nodiscard]] SIMRV_ALWAYS_INLINE auto predict(simrv::xlen::VirtAddr pc,
                                                   const DecodedInstruction& inst)
        -> BranchPrediction {
        return predict(pc.raw(), inst);
    }

    SIMRV_ALWAYS_INLINE void predict(simrv::xlen::VirtAddr pc, const DecodedInstruction& inst,
                                     BranchPrediction& pred) noexcept {
        predict(pc.raw(), inst, pred);
    }

    SIMRV_ALWAYS_INLINE void restore_speculation(const BranchPrediction& prediction) noexcept {
        if (!prediction.is_control) return;

        // Restore GHR
        ghr_ = prediction.ghr_snapshot;

        // Revert speculative RAS state
        if (prediction.is_call && config_.enable_ras && ras_count_ > 0) {
            ras_head_ = (ras_head_ + ras_.size() - 1) % ras_.size();
            --ras_count_;
        } else if (prediction.is_return && config_.enable_ras && prediction.ras_hit) {
            // Re-push the return address popped speculatively
            ras_[ras_head_] = prediction.predicted_target;
            ras_head_ = (ras_head_ + 1) % ras_.size();
            if (ras_count_ < ras_.size()) {
                ++ras_count_;
            }
        }
    }

    [[nodiscard]] auto stats() const noexcept -> const BranchPredictorStats& { return stats_; }
    [[nodiscard]] auto config() const noexcept -> const BranchPredictorConfig& { return config_; }
    [[nodiscard]] auto ghr() const noexcept -> uint32_t { return ghr_; }
    [[nodiscard]] auto ras_depth() const noexcept -> size_t { return ras_count_; }
    [[nodiscard]] auto ras_peek() const noexcept -> std::optional<simrv::xlen::Address>;
    [[nodiscard]] auto bht_distribution() const noexcept -> std::array<size_t, 4>;
    [[nodiscard]] auto get_counter_for_pc(simrv::xlen::Address pc) const noexcept -> uint8_t {
        if (bht_.empty()) return 0;
        const auto idx = get_bht_index(pc, ghr_);
        return (idx < bht_.size()) ? bht_[idx] : 0;
    }

    SIMRV_ALWAYS_INLINE void update_btb(simrv::xlen::Address pc,
                                        simrv::xlen::Address target) noexcept {
        if (config_.enable_btb && !btb_.empty()) {
            const uint32_t btb_idx = static_cast<uint32_t>(pc >> config_.pc_shift) & btb_mask_;
            btb_[btb_idx] = BtbEntry{.tag = pc, .target = target, .valid = true};
        }
    }

    [[nodiscard]] SIMRV_ALWAYS_INLINE auto get_bht_index(simrv::xlen::Address pc,
                                                         uint32_t ghr_val) const noexcept
        -> uint32_t {
        const auto pc_idx = static_cast<uint32_t>(pc >> config_.pc_shift);
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

        const simrv::xlen::Address pc = feedback.pc;
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
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto predict_direction(simrv::xlen::Address pc,
                                                             const DecodedInstruction& inst,
                                                             uint32_t& bht_idx) noexcept -> bool {
        if (config_.type == BranchPredictorType::Disabled) {
            bht_idx = 0;
            return false;
        }
        if (config_.type == BranchPredictorType::Static) {
            // BTFNT: Backward Taken, Forward Not Taken
            bht_idx = 0;
            return inst.imm < 0;
        }

        if (config_.registered_btb_read && config_.type == BranchPredictorType::Bimodal) {
            bht_idx = get_bht_index(pc, ghr_);
            return is_taken_prediction(registered_bht_counter_);
        }

        if (config_.type == BranchPredictorType::Tournament) {
            const uint32_t gshare_idx = (static_cast<uint32_t>(pc >> 1) ^ ghr_) & bht_mask_;
            const uint32_t bimodal_idx = static_cast<uint32_t>(pc >> 1) & bht_mask_;
            const bool use_gshare = is_taken_prediction(chooser_table_[gshare_idx]);
            bht_idx = use_gshare ? gshare_idx : bimodal_idx;
            return is_taken_prediction(use_gshare ? bht_[gshare_idx] : bimodal_bht_[bimodal_idx]);
        }

        bht_idx = get_bht_index(pc, ghr_);
        return is_taken_prediction(bht_[bht_idx]);
    }

    SIMRV_ALWAYS_INLINE void ras_push(simrv::xlen::Address return_addr) noexcept {
        if (ras_.empty()) return;
        ras_[ras_head_] = return_addr;
        ras_head_ = (ras_head_ + 1) % ras_.size();
        if (ras_count_ < ras_.size()) {
            ++ras_count_;
        }
        ++stats_.ras_pushes;
    }

    [[nodiscard]] SIMRV_ALWAYS_INLINE auto ras_pop() noexcept
        -> std::optional<simrv::xlen::Address> {
        if (ras_count_ == 0 || ras_.empty()) return std::nullopt;
        ras_head_ = (ras_head_ + ras_.size() - 1) % ras_.size();
        --ras_count_;
        ++stats_.ras_pops;
        return ras_[ras_head_];
    }

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
    std::vector<simrv::xlen::Address> ras_{};
    size_t ras_head_ = 0;
    size_t ras_count_ = 0;
};

}  // namespace simrv::pipeline
