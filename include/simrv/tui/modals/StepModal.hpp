/**
 * @file StepModal.hpp
 * @brief Modal dialog handler for SetStepSize and SetSpeed dialogs.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "simrv/tui/TuiModal.hpp"

namespace simrv::tui::modals {

class StepModal {
   public:
    static void open(ModalType type, std::string& input, uint64_t step_granularity,
                     uint64_t step_delay_us);
    static auto submit(ModalType type, const std::string& input,
                        std::atomic<uint64_t>& step_granularity,
                        std::atomic<uint64_t>& step_delay_us,
                        const std::function<void()>& on_speed_changed_cb) -> bool;
    static void render(ModalType type, std::vector<std::string>& content_rows,
                       const std::string& input);
};

}  // namespace simrv::tui::modals
