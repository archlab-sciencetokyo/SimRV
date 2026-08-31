#include "simrv/pipeline/BranchPredictor.hpp"

#include <algorithm>
#include <bit>
#include <format>

namespace simrv::pipeline {

namespace {

constexpr uint8_t kSaturatingMax = 3;  // Strongly Taken
constexpr uint8_t kWeaklyNotTaken = 1;
constexpr uint8_t kWeaklyTaken = 2;

[[nodiscard]] constexpr auto saturate_up(uint8_t val) noexcept -> uint8_t {
    return (val < kSaturatingMax) ? static_cast<uint8_t>(val + 1) : kSaturatingMax;
}

[[nodiscard]] constexpr auto saturate_down(uint8_t val) noexcept -> uint8_t {
    return (val > 0) ? static_cast<uint8_t>(val - 1) : 0;
}

[[nodiscard]] constexpr auto is_taken_prediction(uint8_t counter) noexcept -> bool {
    return (counter & 0x2) != 0;
}

}  // namespace

auto parse_branch_predictor_type(std::string_view name) -> std::optional<BranchPredictorType> {
    if (name == "static" || name == "always-not-taken" || name == "btfnt") {
        return BranchPredictorType::Static;
    }
    if (name == "bimodal" || name == "2bit" || name == "local") {
        return BranchPredictorType::Bimodal;
    }
    if (name == "gshare") {
        return BranchPredictorType::GShare;
    }
    if (name == "tournament" || name == "hybrid") {
        return BranchPredictorType::Tournament;
    }
    return std::nullopt;
}

auto to_string(BranchPredictorType type) noexcept -> std::string_view {
    switch (type) {
        case BranchPredictorType::Static:
            return "Static";
        case BranchPredictorType::Bimodal:
            return "Bimodal (2-bit)";
        case BranchPredictorType::GShare:
            return "GShare";
        case BranchPredictorType::Tournament:
            return "Tournament";
    }
    return "Unknown";
}

BranchPredictor::BranchPredictor() { configure(BranchPredictorConfig{}); }

BranchPredictor::BranchPredictor(const BranchPredictorConfig& config) { configure(config); }

void BranchPredictor::configure(const BranchPredictorConfig& config) {
    config_ = config;

    const uint32_t bht_sz = std::bit_ceil(std::max(16u, config_.bht_entries));
    bht_mask_ = bht_sz - 1u;
    bht_.assign(bht_sz, kWeaklyNotTaken);

    if (config_.type == BranchPredictorType::Tournament) {
        bimodal_bht_.assign(bht_sz, kWeaklyNotTaken);
        chooser_table_.assign(bht_sz, kWeaklyTaken);  // Bias slightly toward GShare initially
    } else {
        bimodal_bht_.clear();
        chooser_table_.clear();
    }

    const uint32_t bits = std::clamp(config_.ghr_bits, 1u, 30u);
    ghr_mask_ = (1u << bits) - 1u;
    ghr_ = 0;

    const uint32_t btb_sz = std::bit_ceil(std::max(16u, config_.btb_entries));
    btb_mask_ = btb_sz - 1u;
    btb_.assign(btb_sz, BtbEntry{});

    const size_t ras_sz = std::max<size_t>(4, config_.ras_entries);
    ras_.assign(ras_sz, 0);
    ras_head_ = 0;
    ras_count_ = 0;

    reset();
}

void BranchPredictor::reset() {
    std::fill(bht_.begin(), bht_.end(), kWeaklyNotTaken);
    std::fill(bimodal_bht_.begin(), bimodal_bht_.end(), kWeaklyNotTaken);
    std::fill(chooser_table_.begin(), chooser_table_.end(), kWeaklyTaken);
    std::fill(btb_.begin(), btb_.end(), BtbEntry{});
    std::fill(ras_.begin(), ras_.end(), 0);
    ghr_ = 0;
    ras_head_ = 0;
    ras_count_ = 0;
    stats_ = {};
}

auto BranchPredictor::get_bht_index(Address pc, uint32_t ghr_val) const noexcept -> uint32_t {
    const uint32_t pc_idx = static_cast<uint32_t>(pc >> 1);
    if (config_.type == BranchPredictorType::GShare) {
        return (pc_idx ^ ghr_val) & bht_mask_;
    }
    return pc_idx & bht_mask_;
}

auto BranchPredictor::predict_direction(Address pc, const DecodedInstruction& inst,
                                        uint32_t& bht_idx) -> bool {
    if (config_.type == BranchPredictorType::Static) {
        // BTFNT: Backward Taken, Forward Not Taken
        bht_idx = 0;
        return inst.imm < 0;
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

void BranchPredictor::ras_push(Address return_addr) {
    if (ras_.empty()) return;
    ras_[ras_head_] = return_addr;
    ras_head_ = (ras_head_ + 1) % ras_.size();
    if (ras_count_ < ras_.size()) {
        ++ras_count_;
    }
    ++stats_.ras_pushes;
}

auto BranchPredictor::ras_pop() -> std::optional<Address> {
    if (ras_count_ == 0 || ras_.empty()) return std::nullopt;
    ras_head_ = (ras_head_ + ras_.size() - 1) % ras_.size();
    --ras_count_;
    ++stats_.ras_pops;
    return ras_[ras_head_];
}

auto BranchPredictor::ras_peek() const noexcept -> std::optional<Address> {
    if (ras_count_ == 0 || ras_.empty()) return std::nullopt;
    const size_t top = (ras_head_ + ras_.size() - 1) % ras_.size();
    return ras_[top];
}

auto BranchPredictor::bht_distribution() const noexcept -> std::array<size_t, 4> {
    std::array<size_t, 4> dist{};
    for (uint8_t counter : bht_) {
        if (counter < 4) {
            ++dist[counter];
        }
    }
    return dist;
}

auto BranchPredictor::predict(Address pc, const DecodedInstruction& inst) -> BranchPrediction {
    BranchPrediction pred{};
    const auto opcode = inst.opcode;
    const bool is_branch = (opcode == isa::Opcode::Branch);
    const bool is_jal = (opcode == isa::Opcode::Jal);
    const bool is_jalr = (opcode == isa::Opcode::Jalr);

    pred.is_control = is_branch || is_jal || is_jalr;
    if (!pred.is_control) {
        return pred;
    }

    pred.is_branch = is_branch;
    pred.is_jump = is_jal || is_jalr;
    pred.ghr_snapshot = ghr_;

    const Address inst_len = (inst.cinsn != 0u ? 2 : 4);
    const Address ret_addr = pc + inst_len;

    // Detect RISC-V function calls and returns based on link registers
    if (is_jal || is_jalr) {
        pred.is_call = (inst.rd == RegId::Ra || inst.rd == RegId::T0);
        pred.is_return = is_jalr && (inst.rd != RegId::Ra && inst.rd != RegId::T0) &&
                         (inst.rs1 == RegId::Ra || inst.rs1 == RegId::T0);
    }

    if (is_jal) {
        pred.predicted_taken = true;
        pred.predicted_target = pc + static_cast<Address>(inst.imm);
        pred.btb_hit = true;
        if (pred.is_call && config_.enable_ras) {
            ras_push(ret_addr);
        }
        return pred;
    }

    if (is_jalr) {
        pred.predicted_taken = true;
        if (pred.is_return && config_.enable_ras) {
            if (auto top = ras_pop()) {
                pred.predicted_target = *top;
                pred.ras_hit = true;
                return pred;
            }
        }
        if (pred.is_call && config_.enable_ras) {
            ras_push(ret_addr);
        }
        // Check BTB for JALR target
        if (config_.enable_btb && !btb_.empty()) {
            const uint32_t btb_idx = static_cast<uint32_t>(pc >> 1) & btb_mask_;
            const auto& entry = btb_[btb_idx];
            if (entry.valid && entry.tag == pc) {
                pred.predicted_target = entry.target;
                pred.btb_hit = true;
                return pred;
            }
        }
        pred.predicted_target = 0;  // Unknown target until execute/decode
        pred.btb_hit = false;
        return pred;
    }

    // Conditional Branch
    pred.predicted_taken = predict_direction(pc, inst, pred.bht_index);
    if (pred.predicted_taken) {
        pred.predicted_target = pc + static_cast<Address>(inst.imm);
    } else {
        pred.predicted_target = ret_addr;
    }

    // Speculative GHR shift for conditional branches
    ghr_ = ((ghr_ << 1) | (pred.predicted_taken ? 1u : 0u)) & ghr_mask_;

    return pred;
}

void BranchPredictor::update_direction(const BranchFeedback& feedback) {
    const Address pc = feedback.pc;
    const bool actual_taken = feedback.actual_taken;
    const uint32_t bht_idx = feedback.prediction.bht_index;

    if (config_.type == BranchPredictorType::Static) {
        return;
    }

    if (config_.type == BranchPredictorType::Tournament) {
        const uint32_t gshare_idx =
            (static_cast<uint32_t>(pc >> 1) ^ feedback.prediction.ghr_snapshot) & bht_mask_;
        const uint32_t bimodal_idx = static_cast<uint32_t>(pc >> 1) & bht_mask_;

        const bool gshare_pred = is_taken_prediction(bht_[gshare_idx]);
        const bool bimodal_pred = is_taken_prediction(bimodal_bht_[bimodal_idx]);

        // Update chooser table if one predictor was right and the other was wrong
        if (gshare_pred != bimodal_pred) {
            if (gshare_pred == actual_taken) {
                chooser_table_[gshare_idx] = saturate_up(chooser_table_[gshare_idx]);
            } else {
                chooser_table_[gshare_idx] = saturate_down(chooser_table_[gshare_idx]);
            }
        }

        // Train both underlying predictors
        bht_[gshare_idx] =
            actual_taken ? saturate_up(bht_[gshare_idx]) : saturate_down(bht_[gshare_idx]);
        bimodal_bht_[bimodal_idx] = actual_taken ? saturate_up(bimodal_bht_[bimodal_idx])
                                                 : saturate_down(bimodal_bht_[bimodal_idx]);
        return;
    }

    // Standard Bimodal / GShare
    if (bht_idx < bht_.size()) {
        bht_[bht_idx] = actual_taken ? saturate_up(bht_[bht_idx]) : saturate_down(bht_[bht_idx]);
    }
}

void BranchPredictor::update(const BranchFeedback& feedback) {
    const auto opcode = feedback.opcode;
    const bool is_branch = (opcode == isa::Opcode::Branch);
    const bool is_jal = (opcode == isa::Opcode::Jal);
    const bool is_jalr = (opcode == isa::Opcode::Jalr);

    if (!is_branch && !is_jal && !is_jalr) {
        return;
    }

    ++stats_.total_branches;

    if (feedback.prediction.is_call) ++stats_.function_calls;
    if (feedback.prediction.is_return) ++stats_.function_returns;

    if (is_branch) {
        ++stats_.conditional_branches;
        ++stats_.direction_predictions;

        const bool dir_match = (feedback.actual_taken == feedback.prediction.predicted_taken);
        if (dir_match) {
            ++stats_.direction_hits;
        } else {
            ++stats_.direction_misses;
        }

        update_direction(feedback);

        // Update BTB if branch was taken
        if (feedback.actual_taken && config_.enable_btb && !btb_.empty()) {
            const uint32_t btb_idx = static_cast<uint32_t>(feedback.pc >> 1) & btb_mask_;
            btb_[btb_idx] =
                BtbEntry{.tag = feedback.pc, .target = feedback.actual_target, .valid = true};
        }
    } else if (is_jal) {
        ++stats_.direct_jumps;
    } else if (is_jalr) {
        ++stats_.indirect_jumps;
        ++stats_.target_predictions;

        const bool target_match = (feedback.actual_target == feedback.prediction.predicted_target);
        if (target_match) {
            ++stats_.target_hits;
        } else {
            ++stats_.target_misses;
        }

        if (feedback.prediction.is_return && feedback.prediction.ras_hit) {
            if (target_match) {
                ++stats_.ras_hits;
            } else {
                ++stats_.ras_misses;
            }
        }

        // Update BTB for indirect jump targets
        if (config_.enable_btb && !btb_.empty()) {
            const uint32_t btb_idx = static_cast<uint32_t>(feedback.pc >> 1) & btb_mask_;
            btb_[btb_idx] =
                BtbEntry{.tag = feedback.pc, .target = feedback.actual_target, .valid = true};
        }
    }
}

void BranchPredictor::restore_speculation(const BranchPrediction& prediction) {
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

}  // namespace simrv::pipeline
