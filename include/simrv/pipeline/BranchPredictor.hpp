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
    Static = 0,     ///< Always Not-Taken / BTFNT
    Bimodal = 1,    ///< 2-bit saturating counter table
    GShare = 2,     ///< Global history XOR PC into 2-bit counters
    Tournament = 3  ///< Hybrid chooser selecting between Bimodal and GShare
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

    [[nodiscard]] constexpr auto direction() const noexcept -> BranchDirection {
        return predicted_taken ? BranchDirection::Taken : BranchDirection::NotTaken;
    }
};

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
    BranchPrediction prediction{};

    [[nodiscard]] constexpr auto direction() const noexcept -> BranchDirection {
        return actual_taken ? BranchDirection::Taken : BranchDirection::NotTaken;
    }
};

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
    BranchPredictor();
    explicit BranchPredictor(const BranchPredictorConfig& config);

    void configure(const BranchPredictorConfig& config);
    void reset();

    [[nodiscard]] auto predict(Address pc, const DecodedInstruction& inst) -> BranchPrediction;
    [[nodiscard]] auto predict(VirtAddr pc, const DecodedInstruction& inst) -> BranchPrediction {
        return predict(pc.raw(), inst);
    }
    void update(const BranchFeedback& feedback);
    void restore_speculation(const BranchPrediction& prediction);

    [[nodiscard]] auto stats() const noexcept -> const BranchPredictorStats& { return stats_; }
    [[nodiscard]] auto config() const noexcept -> const BranchPredictorConfig& { return config_; }
    [[nodiscard]] auto ghr() const noexcept -> uint32_t { return ghr_; }
    [[nodiscard]] auto ras_depth() const noexcept -> size_t { return ras_count_; }
    [[nodiscard]] auto ras_peek() const noexcept -> std::optional<Address>;
    [[nodiscard]] auto bht_distribution() const noexcept -> std::array<size_t, 4>;

   private:
    struct BtbEntry {
        Address tag = 0;
        Address target = 0;
        bool valid = false;
    };

    [[nodiscard]] auto predict_direction(Address pc, const DecodedInstruction& inst,
                                         uint32_t& bht_idx) -> bool;
    void update_direction(const BranchFeedback& feedback);
    [[nodiscard]] auto get_bht_index(Address pc, uint32_t ghr_val) const noexcept -> uint32_t;

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

    // RAS
    std::vector<Address> ras_{};
    size_t ras_head_ = 0;
    size_t ras_count_ = 0;
};

}  // namespace simrv::pipeline
