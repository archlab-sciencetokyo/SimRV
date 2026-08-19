#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

#include "simrv/pipeline/PipelineContext.hpp"

namespace simrv::pipeline {

/**
 * @struct PipelineTask
 * @brief C++20/C++23 Coroutine generator type for the CPU execution pipeline.
 *
 * Yields std::optional<StageError> on every cycle. Designed to be allocated once and resumed
 * continuously.
 */
struct PipelineTask {
    struct promise_type {
        std::optional<StageError> current_value;

        PipelineTask get_return_object() {
            return PipelineTask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        std::suspend_always yield_value(std::optional<StageError> value) noexcept {
            current_value = value;
            return {};
        }

        [[noreturn]] void unhandled_exception() {
            std::terminate();
            std::unreachable();
        }

        void return_void() noexcept {}
    };

    std::coroutine_handle<promise_type> handle;

    PipelineTask() : handle(nullptr) {}
    explicit PipelineTask(std::coroutine_handle<promise_type> h) : handle(h) {}

    PipelineTask(const PipelineTask&) = delete;
    PipelineTask& operator=(const PipelineTask&) = delete;

    PipelineTask(PipelineTask&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}

    PipelineTask& operator=(PipelineTask&& other) noexcept {
        if (this != &other) {
            if (handle) {
                handle.destroy();
            }
            handle = std::exchange(other.handle, nullptr);
        }
        return *this;
    }

    ~PipelineTask() {
        if (handle) {
            handle.destroy();
        }
    }

    /// Resumes the pipeline coroutine and returns the yielded StageError (if any)
    [[nodiscard]] auto resume() -> std::optional<StageError> {
        handle.resume();
        return handle.promise().current_value;
    }
};

}  // namespace simrv::pipeline
