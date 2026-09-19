#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <span>
#include <vector>

#include "simrv/xlen/Types.hpp"

namespace simrv::cache {

/// Tag-only cache used to model a timing level in front of the coherent data cache.
class TimingCache {
   public:
    struct Access {
        bool hit = false;
        uint32_t set = 0;
        uint32_t way = 0;
    };

    void configure(uint32_t capacity_bytes, uint32_t associativity, uint32_t line_bytes) {
        enabled_ = capacity_bytes != 0;
        ways_ = std::max(1u, associativity);
        line_bytes_ = std::max(1u, line_bytes);
        sets_ = enabled_ ? capacity_bytes / (ways_ * line_bytes_) : 0;
        entries_.assign(static_cast<size_t>(sets_) * ways_, Entry{});
        tick_ = 0;
    }

    void flush() {
        std::ranges::fill(entries_, Entry{});
        tick_ = 0;
    }

    [[nodiscard]] auto enabled() const -> bool { return enabled_; }

    [[nodiscard]] auto access(Address address) -> Access {
        if (!enabled_) return {};
        const uint64_t raw = static_cast<uint64_t>(address);
        const uint64_t line = raw / line_bytes_;
        const uint32_t set = static_cast<uint32_t>(line & (sets_ - 1));
        const uint64_t tag = line / sets_;
        Entry* victim = nullptr;
        uint32_t victim_way = 0;
        for (uint32_t way = 0; way < ways_; ++way) {
            auto& entry = entries_[static_cast<size_t>(set) * ways_ + way];
            if (entry.valid && entry.tag == tag) {
                entry.last_used = ++tick_;
                return {.hit = true, .set = set, .way = way};
            }
            if (victim == nullptr || !entry.valid ||
                (victim->valid && entry.last_used < victim->last_used)) {
                victim = &entry;
                victim_way = way;
                if (!entry.valid) break;
            }
        }
        victim->valid = true;
        victim->tag = tag;
        victim->last_used = ++tick_;
        return {.hit = false, .set = set, .way = victim_way};
    }

   private:
    struct Entry {
        uint64_t tag = 0;
        uint64_t last_used = 0;
        bool valid = false;
    };

    std::vector<Entry> entries_{};
    uint64_t tick_ = 0;
    uint32_t sets_ = 0;
    uint32_t ways_ = 1;
    uint32_t line_bytes_ = 1;
    bool enabled_ = false;
};

struct TimingLevelConfig {
    uint32_t capacity_bytes = 0;
    uint32_t associativity = 1;
    uint32_t line_bytes = 16;
    uint32_t hit_latency = 1;
    uint32_t refill_latency = 1;
};

/// Inclusive tag-only hierarchy for timing levels in front of a coherent backing cache.
class TimingCacheHierarchy {
   public:
    struct Access {
        size_t hit_level = 0;
        uint32_t latency = 1;
        bool hit = false;
    };

    void configure(std::span<const TimingLevelConfig> configs, uint32_t backing_refill_latency = 1,
                   std::span<const uint32_t> startup_refill_latencies = {}) {
        levels_.clear();
        configs_.clear();
        for (const auto& config : configs) {
            if (config.capacity_bytes == 0) continue;
            levels_.emplace_back();
            levels_.back().configure(config.capacity_bytes, config.associativity,
                                     config.line_bytes);
            configs_.push_back(config);
        }
        backing_refill_latency_ = std::max(1u, backing_refill_latency);
        startup_refill_latencies_.assign(startup_refill_latencies.begin(),
                                         startup_refill_latencies.end());
        backing_misses_ = 0;
    }

    void flush() {
        for (auto& level : levels_) level.flush();
        backing_misses_ = 0;
    }

    [[nodiscard]] auto enabled() const -> bool { return !levels_.empty(); }

    [[nodiscard]] auto access(Address address, bool backing_miss) -> Access {
        for (size_t index = 0; index < levels_.size(); ++index) {
            if (levels_[index].access(address).hit) {
                return {.hit_level = index, .latency = configs_[index].hit_latency, .hit = true};
            }
        }
        if (levels_.empty()) return {};
        if (!backing_miss) {
            return {.hit_level = levels_.size(),
                    .latency = configs_.front().refill_latency,
                    .hit = false};
        }
        const size_t miss_index = backing_misses_++;
        const uint32_t latency = miss_index < startup_refill_latencies_.size()
                                     ? startup_refill_latencies_[miss_index]
                                     : backing_refill_latency_;
        return {.hit_level = levels_.size(), .latency = std::max(1u, latency), .hit = false};
    }

   private:
    std::vector<TimingCache> levels_{};
    std::vector<TimingLevelConfig> configs_{};
    uint64_t backing_misses_ = 0;
    uint32_t backing_refill_latency_ = 1;
    std::vector<uint32_t> startup_refill_latencies_{};
};

}  // namespace simrv::cache
