/**
 * @file TuiInputRouter.hpp
 * @brief Pure input-routing policy for the interactive TUI.
 */
#pragma once

#include <cstdint>

namespace simrv::tui {

enum class InputRoute : uint8_t {
    ControlSequence,
    Modal,
    Navigation,
    Pause,
    Reboot,
    Quit,
    Guest,
};

struct InputContext {
    bool modal_active = false;
    bool paused = true;
};

[[nodiscard]] constexpr auto route_input(uint8_t byte, InputContext context) -> InputRoute {
    if (byte == 0x11 || byte == 0x03) return InputRoute::Quit;  // Ctrl-Q or Ctrl-C
    if (byte == 0x12) return InputRoute::Reboot;                // Ctrl-R
    if (byte == 0x1B) return InputRoute::ControlSequence;       // Escape / ANSI sequence
    if (context.modal_active) return InputRoute::Modal;
    if (context.paused) return InputRoute::Navigation;
    if (byte == 0x10) return InputRoute::Pause;  // Ctrl-P
    return InputRoute::Guest;
}

/// Convert the host terminal's Enter byte to the line delimiter expected by a guest console.
[[nodiscard]] constexpr auto normalize_guest_terminal_byte(uint8_t byte) -> uint8_t {
    return byte == static_cast<uint8_t>('\r') ? static_cast<uint8_t>('\n') : byte;
}

}  // namespace simrv::tui
