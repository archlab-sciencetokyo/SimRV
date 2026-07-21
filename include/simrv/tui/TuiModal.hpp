/**
 * @file TuiModal.hpp
 * @brief Modal dialog manager and overlay renderer for SimRV TUI.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {

class LeftPane;
enum class TuiRegPage : uint8_t;

enum class ModalType : uint8_t {
    None,
    SetBreakpoint,
    SetStepSize,
    SetSpeed,
    InspectAddress,
    Help
};

class TuiModal {
   public:
    explicit TuiModal(simrv::core::Machine& machine);

    [[nodiscard]] auto is_active() const -> bool { return active_modal_ != ModalType::None; }
    [[nodiscard]] auto get_type() const -> ModalType { return active_modal_; }
    [[nodiscard]] auto get_input() const -> const std::string& { return input_; }

    void open(ModalType type, LeftPane* left_pane, uint64_t step_granularity, uint64_t step_delay_us);
    void close();
    void submit(LeftPane* left_pane,
                std::atomic<uint64_t>& step_granularity,
                std::atomic<uint64_t>& step_delay_us,
                const std::function<void(TuiRegPage)>& set_reg_page_cb,
                const std::function<void(const std::string&)>& set_status_override_cb);

    void push_char(char c) { input_.push_back(c); }
    void pop_char() { if (!input_.empty()) input_.pop_back(); }

    void render_overlay(std::vector<std::string>& lines, int term_width, int term_height) const;

   private:
    simrv::core::Machine& machine_;
    ModalType active_modal_ = ModalType::None;
    std::string input_;
};

}  // namespace simrv::tui
