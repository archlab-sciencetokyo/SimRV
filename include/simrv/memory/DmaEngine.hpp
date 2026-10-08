/**
 * @file DmaEngine.hpp
 * @brief Cycle-accurate DMA transfer timing, scheduling, and bus contention modeling.
 */
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/pipeline/CpuModel.hpp"

namespace simrv::memory {

struct DmaTransferTrace {
    uint64_t id = 0;
    std::size_t byte_count = 0;
    uint64_t request_cycle = 0;
    uint64_t start_cycle = 0;
    uint64_t completion_cycle = 0;
    std::string component;
    bool complete = false;
    bool cancelled = false;
};

class DmaEngine {
   public:
    using Cycle = uint64_t;

    using TraceObserver = std::function<void(const DmaTransferTrace&)>;

    struct DmaTask {
        uint64_t id{0};
        std::size_t byte_count{0};
        Cycle request_cycle{0};
        Cycle start_cycle{0};
        Cycle completion_cycle{0};
        std::string component;
        std::function<void()> on_complete;
    };

    explicit DmaEngine(const pipeline::DmaTimingConfig& config = {}) : config_(config) {}

    void set_config(const pipeline::DmaTimingConfig& config) noexcept {
        const std::lock_guard lock(mutex_);
        config_ = config;
    }

    void set_trace_observer(TraceObserver observer) {
        const std::lock_guard lock(mutex_);
        trace_observer_ = std::move(observer);
    }

    /// Install the machine-level observer without replacing the optional trace observer.
    void set_event_observer(TraceObserver observer) {
        const std::lock_guard lock(mutex_);
        event_observer_ = std::move(observer);
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
                           std::function<void()> on_complete, std::string_view component = "dma")
        -> Cycle {
        TraceObserver observer;
        TraceObserver event_observer;
        std::function<void()> immediate_callback;
        DmaTransferTrace trace;
        bool immediate = false;
        {
            const std::lock_guard lock(mutex_);
            trace.id = next_transfer_id_++;
            trace.byte_count = byte_count;
            trace.request_cycle = current_cycle;
            trace.component = component;
            observer = trace_observer_;
            event_observer = event_observer_;
            if (!config_.enabled) {
                trace.start_cycle = current_cycle;
                trace.completion_cycle = current_cycle;
                immediate_callback = std::move(on_complete);
                immediate = true;
            } else {
                const uint64_t bw = std::max(1u, config_.bandwidth_bytes_per_cycle);
                const uint64_t transfer_cycles = (static_cast<uint64_t>(byte_count) + bw - 1) / bw;
                const Cycle duration = static_cast<uint64_t>(config_.setup_latency) +
                                       std::max<uint64_t>(1, transfer_cycles);

                trace.start_cycle = current_cycle;
                if (!tasks_.empty()) {
                    trace.start_cycle = std::max(trace.start_cycle, tasks_.back().completion_cycle);
                }
                trace.completion_cycle = trace.start_cycle + duration;
                tasks_.push_back(DmaTask{.id = trace.id,
                                         .byte_count = byte_count,
                                         .request_cycle = current_cycle,
                                         .start_cycle = trace.start_cycle,
                                         .completion_cycle = trace.completion_cycle,
                                         .component = trace.component,
                                         .on_complete = std::move(on_complete)});
                has_pending_tasks_.store(true, std::memory_order_relaxed);
            }
        }
        if (observer) observer(trace);
        if (event_observer) event_observer(trace);
        if (immediate) {
            if (immediate_callback) immediate_callback();
            trace.complete = true;
            if (event_observer) event_observer(trace);
            if (observer) {
                observer(trace);
            }
        }
        return trace.completion_cycle;
    }

    /// Advance cycle and trigger any completed tasks
    void advance_cycle(Cycle current_cycle) {
        // This method runs once per CA cycle. Most cycles have no DMA work, so avoid
        // contending on the task mutex until a transfer has actually been queued.
        if (!has_pending_tasks_.load(std::memory_order_relaxed)) return;

        std::vector<DmaTask> ready_tasks;
        TraceObserver observer;
        TraceObserver event_observer;
        {
            const std::lock_guard lock(mutex_);
            if (tasks_.empty()) return;
            observer = trace_observer_;
            event_observer = event_observer_;
            for (auto it = tasks_.begin(); it != tasks_.end();) {
                if (it->completion_cycle <= current_cycle) {
                    ready_tasks.push_back(std::move(*it));
                    it = tasks_.erase(it);
                } else {
                    ++it;
                }
            }
            has_pending_tasks_.store(!tasks_.empty(), std::memory_order_relaxed);
        }
        for (auto& task : ready_tasks) {
            if (task.on_complete) task.on_complete();
            const DmaTransferTrace trace{.id = task.id,
                                         .byte_count = task.byte_count,
                                         .request_cycle = task.request_cycle,
                                         .start_cycle = task.start_cycle,
                                         .completion_cycle = task.completion_cycle,
                                         .component = task.component,
                                         .complete = true};
            if (event_observer) event_observer(trace);
            if (observer) {
                observer(trace);
            }
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
        std::vector<DmaTask> cancelled;
        TraceObserver observer;
        TraceObserver event_observer;
        {
            const std::lock_guard lock(mutex_);
            cancelled.swap(tasks_);
            observer = trace_observer_;
            event_observer = event_observer_;
            has_pending_tasks_.store(false, std::memory_order_relaxed);
        }
        for (const auto& task : cancelled) {
            const DmaTransferTrace trace{.id = task.id,
                                         .byte_count = task.byte_count,
                                         .request_cycle = task.request_cycle,
                                         .start_cycle = task.start_cycle,
                                         .completion_cycle = task.completion_cycle,
                                         .component = task.component,
                                         .complete = false,
                                         .cancelled = true};
            if (observer) observer(trace);
            if (event_observer) event_observer(trace);
        }
    }

   private:
    mutable std::mutex mutex_;
    pipeline::DmaTimingConfig config_{};
    std::vector<DmaTask> tasks_;
    std::atomic<bool> has_pending_tasks_{false};
    TraceObserver trace_observer_;
    TraceObserver event_observer_;
    uint64_t next_transfer_id_ = 1;
};

}  // namespace simrv::memory
