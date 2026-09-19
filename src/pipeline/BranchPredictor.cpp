#include "simrv/pipeline/BranchPredictor.hpp"

#include <algorithm>
#include <bit>
#include <format>

namespace simrv::pipeline {

namespace {

constexpr uint8_t kSaturatingMax = 3;  // Strongly Taken
constexpr uint8_t kWeaklyNotTaken = 1;
constexpr uint8_t kWeaklyTaken = 2;

constexpr std::array<uint8_t, 4> kSaturateUp = {1, 2, 3, 3};
constexpr std::array<uint8_t, 4> kSaturateDown = {0, 0, 1, 2};
constexpr std::array<std::array<uint8_t, 4>, 2> kSaturateTable = {{{0, 0, 1, 2}, {1, 2, 3, 3}}};

[[nodiscard]] constexpr auto saturate_up(uint8_t val) noexcept -> uint8_t {
    return kSaturateUp[val & 0x3];
}

[[nodiscard]] constexpr auto saturate_down(uint8_t val) noexcept -> uint8_t {
    return kSaturateDown[val & 0x3];
}

[[nodiscard]] constexpr auto saturate(uint8_t val, bool taken) noexcept -> uint8_t {
    return kSaturateTable[taken ? 1 : 0][val & 0x3];
}

[[nodiscard]] constexpr auto is_taken_prediction(uint8_t counter) noexcept -> bool {
    return (counter & 0x2) != 0;
}

}  // namespace

auto parse_branch_predictor_type(std::string_view name) -> std::optional<BranchPredictorType> {
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "none" || lower == "off" || lower == "disabled") {
        return BranchPredictorType::Disabled;
    }
    if (lower == "static" || lower == "always-not-taken" || lower == "btfnt") {
        return BranchPredictorType::Static;
    }
    if (lower == "bimodal" || lower == "2bit" || lower == "local" || lower.starts_with("bimodal")) {
        return BranchPredictorType::Bimodal;
    }
    if (lower == "gshare") {
        return BranchPredictorType::GShare;
    }
    if (lower == "tournament" || lower == "hybrid") {
        return BranchPredictorType::Tournament;
    }
    return std::nullopt;
}

auto to_string(BranchPredictorType type) noexcept -> std::string_view {
    switch (type) {
        case BranchPredictorType::Disabled:
            return "Disabled";
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
    bht_.assign(bht_sz, config_.bht_initial_state);

    if (config_.type == BranchPredictorType::Tournament) {
        bimodal_bht_.assign(bht_sz, config_.bht_initial_state);
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
    std::fill(bht_.begin(), bht_.end(), config_.bht_initial_state);
    std::fill(bimodal_bht_.begin(), bimodal_bht_.end(), config_.bht_initial_state);
    std::fill(chooser_table_.begin(), chooser_table_.end(), kWeaklyTaken);
    std::fill(btb_.begin(), btb_.end(), BtbEntry{});
    registered_btb_entry_ = {};
    registered_bht_counter_ = config_.bht_initial_state;
    std::fill(ras_.begin(), ras_.end(), 0);
    ghr_ = 0;
    ras_head_ = 0;
    ras_count_ = 0;
    stats_ = {};
}

void BranchPredictor::latch_btb_read(Address pc) {
    if (!config_.registered_btb_read || btb_.empty() || bht_.empty()) return;
    const uint32_t btb_idx = static_cast<uint32_t>(pc >> config_.pc_shift) & btb_mask_;
    registered_btb_entry_ = btb_[btb_idx];
    registered_bht_counter_ = bht_[get_bht_index(pc, ghr_)];
}

auto BranchPredictor::get_bht_index(Address pc, uint32_t ghr_val) const noexcept -> uint32_t {
    const uint32_t pc_idx = static_cast<uint32_t>(pc >> config_.pc_shift);
    if (config_.type == BranchPredictorType::GShare) {
        return (pc_idx ^ ghr_val) & bht_mask_;
    }
    return pc_idx & bht_mask_;
}

auto BranchPredictor::predict_direction(Address pc, const DecodedInstruction& inst,
                                        uint32_t& bht_idx) -> bool {
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
        if (config_.predict_non_control && config_.type == BranchPredictorType::Bimodal &&
            config_.enable_btb && config_.untagged_btb) {
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
        return pred;
    }

    pred.is_branch = is_branch;
    pred.is_jump = is_jal || is_jalr;
    pred.ghr_snapshot = ghr_;

    if (config_.type == BranchPredictorType::Disabled) {
        pred.predicted_taken = false;
        pred.predicted_target = pc + (inst.cinsn != 0u ? 2 : 4);
        return pred;
    }

    const Address inst_len = (inst.cinsn != 0u ? 2 : 4);
    const Address ret_addr = pc + inst_len;

    // Detect RISC-V function calls and returns based on link registers
    if (is_jal || is_jalr) {
        pred.is_call = (inst.rd == RegId::Ra || inst.rd == RegId::T0);
        pred.is_return = is_jalr && (inst.rd != RegId::Ra && inst.rd != RegId::T0) &&
                         (inst.rs1 == RegId::Ra || inst.rs1 == RegId::T0);
    }

    if (is_jal) {
        pred.predicted_target = pc + static_cast<Address>(inst.imm);
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
            const uint32_t btb_idx = static_cast<uint32_t>(pc >> config_.pc_shift) & btb_mask_;
            const auto& entry = btb_[btb_idx];
            if (entry.valid && (config_.untagged_btb || entry.tag == pc)) {
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
        pred.predicted_target = config_.registered_btb_read ? registered_btb_entry_.target
                                                            : pc + static_cast<Address>(inst.imm);
    } else {
        pred.predicted_target = ret_addr;
    }

    // Speculative GHR shift for conditional branches
    ghr_ = ((ghr_ << 1) | (pred.predicted_taken ? 1u : 0u)) & ghr_mask_;

    return pred;
}

void BranchPredictor::update_direction(const BranchFeedback& feedback) {
    if (config_.type == BranchPredictorType::Disabled ||
        config_.type == BranchPredictorType::Static) {
        return;
    }

    const Address pc = feedback.pc;
    const bool actual_taken = feedback.actual_taken;

    if (config_.type == BranchPredictorType::Tournament) {
        const uint32_t gshare_idx =
            (static_cast<uint32_t>(pc >> 1) ^ feedback.prediction.ghr_snapshot) & bht_mask_;
        const uint32_t bimodal_idx = static_cast<uint32_t>(pc >> 1) & bht_mask_;

        const bool gshare_pred = is_taken_prediction(bht_[gshare_idx]);
        const bool bimodal_pred = is_taken_prediction(bimodal_bht_[bimodal_idx]);

        // Update chooser table if one predictor was right and the other was wrong
        if (gshare_pred != bimodal_pred) {
            chooser_table_[gshare_idx] =
                saturate(chooser_table_[gshare_idx], gshare_pred == actual_taken);
        }

        // Train both underlying predictors
        bht_[gshare_idx] = saturate(bht_[gshare_idx], actual_taken);
        bimodal_bht_[bimodal_idx] = saturate(bimodal_bht_[bimodal_idx], actual_taken);
        return;
    }

    // Standard Bimodal / GShare
    const uint32_t bht_idx = feedback.prediction.bht_index;
    if (bht_idx < bht_.size()) {
        bht_[bht_idx] = saturate(bht_[bht_idx], actual_taken);
    }
}

void BranchPredictor::update(const BranchFeedback& feedback) {
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
        stats_.direction_hits += dir_match ? 1 : 0;
        stats_.direction_misses += dir_match ? 0 : 1;

        update_direction(feedback);

        if (feedback.prediction.false_control_alias) {
            return;
        }

        // Update BTB if branch was taken
        if (feedback.actual_taken && config_.enable_btb && !btb_.empty()) {
            const uint32_t btb_idx =
                static_cast<uint32_t>(feedback.pc >> config_.pc_shift) & btb_mask_;
            btb_[btb_idx] =
                BtbEntry{.tag = feedback.pc, .target = feedback.actual_target, .valid = true};
        }
    } else if (feedback.opcode == isa::Opcode::Jal) {
        ++stats_.direct_jumps;
        if (config_.jump_uses_direction_counter && !bht_.empty()) {
            const auto index = get_bht_index(feedback.pc, feedback.prediction.ghr_snapshot);
            bht_[index] = saturate_up(bht_[index]);
        }
        if (config_.enable_btb && !btb_.empty()) {
            const uint32_t btb_idx =
                static_cast<uint32_t>(feedback.pc >> config_.pc_shift) & btb_mask_;
            btb_[btb_idx] =
                BtbEntry{.tag = feedback.pc, .target = feedback.actual_target, .valid = true};
        }
    } else if (feedback.opcode == isa::Opcode::Jalr) {
        ++stats_.indirect_jumps;
        ++stats_.target_predictions;

        const bool target_match = (feedback.actual_target == feedback.prediction.predicted_target);
        stats_.target_hits += target_match ? 1 : 0;
        stats_.target_misses += target_match ? 0 : 1;

        if (feedback.prediction.is_return && feedback.prediction.ras_hit) {
            stats_.ras_hits += target_match ? 1 : 0;
            stats_.ras_misses += target_match ? 0 : 1;
        }

        // Update BTB for indirect jump targets
        if (config_.enable_btb && !btb_.empty()) {
            const uint32_t btb_idx =
                static_cast<uint32_t>(feedback.pc >> config_.pc_shift) & btb_mask_;
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
