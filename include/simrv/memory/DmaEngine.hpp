/**
 * @file DmaEngine.hpp
 * @brief Cycle-accurate DMA transfer timing, scheduling, and bus contention modeling.
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

#include "simrv/pipeline/CpuModel.hpp"

namespace simrv::memory {

class DmaEngine {
   public:
    using Cycle = uint64_t;

    struct DmaTask {
        Cycle completion_cycle{0};
        std::function<void()> on_complete;
    };

    explicit DmaEngine(const pipeline::DmaTimingConfig& config = {}) : config_(config) {}

    void set_config(const pipeline::DmaTimingConfig& config) noexcept {
        const std::lock_guard lock(mutex_);
        config_ = config;
    }

    [[nodiscard]] auto config() const noexcept -> pipeline::DmaTimingConfig {
        const std::lock_guard lock(mutex_);
        return config_;
    }

    [[nodiscard]] auto is_enabled() const noexcept -> bool {
        const std::lock_guard lock(mutex_);
        return config_.enabled;
    }

    /// Calculate transfer latency in cycles for the given byte count
    [[nodiscard]] auto calculate_latency(std::size_t byte_count) const noexcept -> uint64_t {
        const std::lock_guard lock(mutex_);
        if (!config_.enabled) return 0;
        const uint64_t bw = std::max(1u, config_.bandwidth_bytes_per_cycle);
        const uint64_t transfer_cycles = (static_cast<uint64_t>(byte_count) + bw - 1) / bw;
        return static_cast<uint64_t>(config_.setup_latency) +
               std::max<uint64_t>(1, transfer_cycles);
    }

    /// Schedule a DMA completion task at current_cycle + transfer_duration
    auto schedule_transfer(std::size_t byte_count, Cycle current_cycle,
                           std::function<void()> on_complete) -> Cycle {
        const std::lock_guard lock(mutex_);
        if (!config_.enabled) {
            if (on_complete) on_complete();
            return current_cycle;
        }

        const uint64_t bw = std::max(1u, config_.bandwidth_bytes_per_cycle);
        const uint64_t transfer_cycles = (static_cast<uint64_t>(byte_count) + bw - 1) / bw;
        const Cycle duration =
            static_cast<uint64_t>(config_.setup_latency) + std::max<uint64_t>(1, transfer_cycles);

        Cycle start_cycle = current_cycle;
        if (!tasks_.empty()) {
            start_cycle = std::max(start_cycle, tasks_.back().completion_cycle);
        }
        const Cycle completion_cycle = start_cycle + duration;
        tasks_.push_back(DmaTask{
            .completion_cycle = completion_cycle,
            .on_complete = std::move(on_complete),
        });
        return completion_cycle;
    }

    /// Advance cycle and trigger any completed tasks
    void advance_cycle(Cycle current_cycle) {
        std::vector<std::function<void()>> ready_callbacks;
        {
            const std::lock_guard lock(mutex_);
            if (tasks_.empty()) return;
            for (auto it = tasks_.begin(); it != tasks_.end();) {
                if (it->completion_cycle <= current_cycle) {
                    if (it->on_complete) {
                        ready_callbacks.push_back(std::move(it->on_complete));
                    }
                    it = tasks_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto& cb : ready_callbacks) {
            cb();
        }
    }

    /// Returns true if any DMA transfer is currently active on the interconnect
    [[nodiscard]] auto is_transfer_active(Cycle current_cycle) const noexcept -> bool {
        const std::lock_guard lock(mutex_);
        if (!config_.enabled || tasks_.empty()) return false;
        for (const auto& task : tasks_) {
            if (task.completion_cycle >= current_cycle) return true;
        }
        return false;
    }

    [[nodiscard]] auto pending_tasks() const noexcept -> std::size_t {
        const std::lock_guard lock(mutex_);
        return tasks_.size();
    }

    void clear() {
        const std::lock_guard lock(mutex_);
        tasks_.clear();
    }

   private:
    mutable std::mutex mutex_;
    pipeline::DmaTimingConfig config_{};
    std::vector<DmaTask> tasks_;
};

}  // namespace simrv::memory
