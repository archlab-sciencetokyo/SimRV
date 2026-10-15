#include "simrv/pipeline/BranchPredictor.hpp"

#include <algorithm>
#include <bit>

namespace simrv::pipeline {

namespace {

constexpr uint8_t kWeaklyTaken = 2;

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

auto BranchPredictor::ras_peek() const noexcept -> std::optional<simrv::xlen::Address> {
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

}  // namespace simrv::pipeline
