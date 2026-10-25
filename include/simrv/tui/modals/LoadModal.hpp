/**
 * @file LoadModal.hpp
 * @brief Modal dialog handler for LoadBinary and LoadDiskImage dialogs.
 */
#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/tui/TuiTypes.hpp"
#include "simrv/tui/framework/Modal.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {
class TuiModal;
}

namespace simrv::tui::modals {

struct PathCompletionResult {
    std::vector<std::string> matches;
    std::string common_prefix;
};

class LoadModal : public framework::IModal {
   public:
    explicit LoadModal(TuiModal* host, simrv::core::Machine* machine, ModalType type,
                       std::string& input, bool& load_appmode, bool& load_buildroot_mode,
                       std::string& staged_binary_path,
                       std::vector<std::string>& completion_preview)
        : host_(host),
          machine_(machine),
          type_(type),
          input_(input),
          load_appmode_(load_appmode),
          load_buildroot_mode_(load_buildroot_mode),
          staged_binary_path_(staged_binary_path),
          completion_preview_(completion_preview) {}

    LoadModal()
        : host_(nullptr),
          machine_(nullptr),
          type_(ModalType::LoadBinary),
          input_(owned_input_),
          load_appmode_(owned_load_appmode_),
          load_buildroot_mode_(owned_load_buildroot_mode_),
          staged_binary_path_(owned_staged_binary_path_),
          completion_preview_(owned_completion_preview_) {}

    [[nodiscard]] auto title() const -> std::string override;
    [[nodiscard]] auto type() const -> ModalType override { return type_; }
    [[nodiscard]] auto input() const -> std::string override { return input_; }

    void render_content(std::vector<std::string>& content_rows, int width, int height) override;
    auto handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction override;
    auto handle_control_click(int content_row, size_t action_index)
        -> framework::ModalAction override;
    auto handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction override;

    static void open(ModalType type, std::string& input, bool& load_appmode,
                     const simrv::core::Machine& machine);
    static auto submit(ModalType type, const std::string& input, bool load_appmode,
                       bool load_buildroot_mode, simrv::core::Machine& machine,
                       std::string& staged_binary_path, bool& staged_mode_change,
                       bool& staged_target_appmode,
                       const std::function<void(const std::string&)>& set_status_override_cb)
        -> bool;
    static void render(ModalType type, std::vector<std::string>& content_rows,
                       const std::string& input, bool load_appmode, bool load_buildroot_mode,
                       const std::string& staged_binary_path,
                       std::span<const std::string> completion_preview = {});
    [[nodiscard]] static auto inspect_path(std::string_view input) -> PathCompletionResult;
    [[nodiscard]] static auto complete_path(std::string& input) -> bool;

   private:
    std::string owned_input_;
    bool owned_load_appmode_ = true;
    bool owned_load_buildroot_mode_ = false;
    std::string owned_staged_binary_path_;
    std::vector<std::string> owned_completion_preview_;

    TuiModal* host_;
    simrv::core::Machine* machine_;
    ModalType type_;
    std::string& input_;
    bool& load_appmode_;
    bool& load_buildroot_mode_;
    std::string& staged_binary_path_;
    std::vector<std::string>& completion_preview_;
};

}  // namespace simrv::tui::modals
