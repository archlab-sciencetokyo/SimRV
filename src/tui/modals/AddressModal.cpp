/**
 * @file AddressModal.cpp
 * @brief Implementation of Memory Address inspection modal dialog handling.
 */
#include "simrv/tui/modals/AddressModal.hpp"

#include <algorithm>
#include <charconv>
#include <format>

#include "simrv/core/Machine.hpp"
#include "simrv/debug/SymbolTable.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"

namespace simrv::tui::modals {

void AddressModal::render_content(std::vector<std::string>& content_rows, int /*width*/,
                                  int /*height*/) {
    render(content_rows, input_);
}

auto AddressModal::handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction {
    if (byte == 27 || key == simrv::tui::TuiKey::Esc) {
        return framework::ModalAction::Close;
    }
    if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline) {
        return framework::ModalAction::Submit;
    }
    if (byte == 8 || byte == 127 || key == simrv::tui::TuiKey::Backspace) {
        if (!input_.empty()) input_.pop_back();
        return framework::ModalAction::Continue;
    }
    if (byte >= 32 && byte <= 126) {
        input_.push_back(static_cast<char>(byte));
        return framework::ModalAction::Continue;
    }
    return framework::ModalAction::Continue;
}

auto AddressModal::handle_control_click(int /*content_row*/, size_t action_index)
    -> framework::ModalAction {
    return action_index == 0 ? framework::ModalAction::Submit : framework::ModalAction::Close;
}

auto AddressModal::handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction {
    (void)local_pt;
    return framework::ModalAction::Continue;
}

void AddressModal::open(std::string& input, InspectorPane* inspector_pane) {
    input = std::format("0x{:08x}", inspector_pane ? inspector_pane->get_inspect_addr() : 0);
}

auto AddressModal::submit(const std::string& input, simrv::core::Machine& machine,
                          InspectorPane* inspector_pane,
                          const std::function<void(const std::string&)>& set_status_override_cb)
    -> bool {
    if (input.empty()) return false;

    if (input == "sp" || input == "SP" || input == "0" || input == "0x0") {
        if (inspector_pane) {
            inspector_pane->reset_inspect_addr();
        }
        set_status_override_cb("Reset memory view to Stack Watch (sp)");
        return true;
    }

    simrv::xlen::Address addr = 0;
    bool ok = false;
    if (input.starts_with("0x") || input.starts_with("0X")) {
        auto result = std::from_chars(input.data() + 2, input.data() + input.size(), addr, 16);
        ok = (result.ec == std::errc{});
    } else if (std::all_of(input.begin(), input.end(), ::isxdigit)) {
        auto result = std::from_chars(input.data(), input.data() + input.size(), addr, 16);
        ok = (result.ec == std::errc{});
    } else {
        auto sym_opt = machine.symbol_table().lookup_name(input);
        if (sym_opt.has_value()) {
            addr = *sym_opt;
            ok = true;
        }
    }

    if (ok) {
        if (inspector_pane) {
            inspector_pane->set_inspect_addr(addr);
        }
        set_status_override_cb(std::format("Inspecting address 0x{:08x}", addr));
        return true;
    }

    set_status_override_cb(std::format("Symbol/address not found: {}", input));
    return false;
}

void AddressModal::render(std::vector<std::string>& content_rows, const std::string& input) {
    build_text_input_rows(content_rows, "Enter Address (hex), Symbol, or 'sp' to reset:", input);
    content_rows.push_back("");
    content_rows.push_back(build_modal_footer(
        {{.key = "[Enter]", .action = "Inspect"}, {.key = "[Esc]", .action = "Cancel"}}));
}

}  // namespace simrv::tui::modals
