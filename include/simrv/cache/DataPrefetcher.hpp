/**
 * @file DataPrefetcher.hpp
 * @brief Hardware stream and stride data prefetcher for L1/L2 data caches.
 */
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>

#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::cache {

class DataPrefetcher {
   public:
    static constexpr size_t kTableEntries = 8;
    static constexpr uint8_t kMaxConfidence = 3;
    static constexpr uint8_t kThreshold = 2;

    struct StreamEntry {
        simrv::xlen::Address last_addr = 0;
        int64_t stride = 0;
        uint8_t confidence = 0;
        bool valid = false;
    };

    DataPrefetcher() = default;

    /**
     * @brief Observes a demand memory access and predicts the next prefetch target line.
     * @param addr Demand byte address.
     * @param line_bytes Cache line byte size.
     * @return Prefetch target line address if confidence threshold is met.
     */
    auto observe(simrv::xlen::Address addr, uint32_t line_bytes) noexcept
        -> std::optional<simrv::xlen::Address> {
        if (!enabled_) return std::nullopt;

        const auto line_mask = ~static_cast<simrv::xlen::Address>(line_bytes - 1u);
        const simrv::xlen::Address line_addr = addr & line_mask;

        size_t match_idx = kTableEntries;
        size_t victim_idx = 0;

        for (size_t i = 0; i < kTableEntries; ++i) {
            if (!entries_[i].valid) {
                victim_idx = i;
                break;
            }
            const int64_t diff =
                static_cast<int64_t>(line_addr) - static_cast<int64_t>(entries_[i].last_addr);
            if (diff != 0 && std::abs(diff) <= static_cast<int64_t>(line_bytes * 16u)) {
                match_idx = i;
                break;
            }
        }

        if (match_idx < kTableEntries) {
            auto& entry = entries_[match_idx];
            const int64_t current_stride =
                static_cast<int64_t>(line_addr) - static_cast<int64_t>(entry.last_addr);
            if (current_stride == entry.stride) {
                if (entry.confidence < kMaxConfidence) ++entry.confidence;
            } else {
                entry.stride = current_stride;
                entry.confidence = 1;
            }
            entry.last_addr = line_addr;

            if (entry.confidence >= kThreshold) {
                ++prefetches_issued_;
                return static_cast<simrv::xlen::Address>(static_cast<int64_t>(line_addr) +
                                                         entry.stride);
            }
            return std::nullopt;
        }

        entries_[victim_idx].last_addr = line_addr;
        entries_[victim_idx].stride = 0;
        entries_[victim_idx].confidence = 0;
        entries_[victim_idx].valid = true;
        return std::nullopt;
    }

    void record_prefetch_hit() noexcept { ++prefetch_hits_; }

    void reset() noexcept {
        for (auto& entry : entries_) {
            entry = {};
        }
        prefetches_issued_ = 0;
        prefetch_hits_ = 0;
    }

    [[nodiscard]] auto is_enabled() const noexcept -> bool { return enabled_; }
    void set_enabled(bool enabled) noexcept { enabled_ = enabled; }

    [[nodiscard]] auto prefetches_issued() const noexcept -> uint64_t { return prefetches_issued_; }
    [[nodiscard]] auto prefetch_hits() const noexcept -> uint64_t { return prefetch_hits_; }

   private:
    std::array<StreamEntry, kTableEntries> entries_{};
    uint64_t prefetches_issued_ = 0;
    uint64_t prefetch_hits_ = 0;
    bool enabled_ = true;
};

}  // namespace simrv::cache
