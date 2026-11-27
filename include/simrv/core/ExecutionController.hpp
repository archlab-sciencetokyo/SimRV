/**
 * @file ExecutionController.hpp
 * @brief Abstract execution controller interface separating simulation flow from telemetry.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace simrv::core {

class IExecutionController {
   public:
    virtual ~IExecutionController() = default;

    virtual void pause_loop() = 0;
    virtual void unpause_loop() = 0;
    virtual void set_paused(bool paused) = 0;
    [[nodiscard]] virtual bool is_paused() const = 0;

    [[nodiscard]] virtual uint64_t step_delay_us() const = 0;
    virtual void set_step_delay_us(uint64_t delay_us) = 0;

    virtual void set_status_override(const std::string& status) = 0;
    virtual void set_persistent_status_override(const std::string& status) = 0;

    virtual void start_ui_thread() = 0;
    virtual void stop_ui_thread() = 0;
    [[nodiscard]] virtual bool is_ui_thread_running() const = 0;
    virtual void set_sim_thread_sleeping(bool sleeping) = 0;
};

}  // namespace simrv::core
