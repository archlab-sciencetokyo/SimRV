/**
 * @file InputDevice.cpp
 * @brief Separate input device class for keyboard and mouse registers.
 */
#include "simrv/device/InputDevice.hpp"
#include <mutex>
#include "simrv/core/Machine.hpp"

namespace simrv::device {

InputDevice::InputDevice(simrv::core::Machine& machine) : machine_(machine) {}

InputDevice::~InputDevice() = default;

auto InputDevice::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool {
    resp.error = false;
    resp.data = 0;

    const bool is_write = (req.opcode == memory::TlOpcodeA::PutFullData ||
                           req.opcode == memory::TlOpcodeA::PutPartialData);
    const Address offset = req.address - 0x30000010u;

    if (is_write) {
        // Input device registers are read-only for guest
        resp.error = true;
        return true;
    }

    if (req.opcode == memory::TlOpcodeA::Get) {
        switch (static_cast<InputRegister>(offset)) {
            case InputRegister::KeyboardRead: // Keyboard read (0x30000010)
            {
                std::scoped_lock lock(key_mutex_);
                if (!key_queue_.empty()) {
                    resp.data = key_queue_.front();
                    key_queue_.pop();
                } else {
                    resp.data = 0;
                }
            } break;
            case InputRegister::KeyboardStatus: // Keyboard status (0x30000014)
            {
                std::scoped_lock lock(key_mutex_);
                resp.data = key_queue_.empty() ? 0 : 1;
            } break;
            case InputRegister::MouseRead: // Mouse read (0x30000018)
            {
                std::scoped_lock lock(mouse_mutex_);
                if (!mouse_queue_.empty()) {
                    auto ev = mouse_queue_.front();
                    mouse_queue_.pop();
                    // Pack mouse buttons, dx, dy
                    uint32_t buttons = ev.buttons & 0xFF;
                    uint32_t dx = static_cast<uint32_t>(ev.dx) & 0xFFF;
                    uint32_t dy = static_cast<uint32_t>(ev.dy) & 0xFFF;
                    resp.data = buttons | (dx << 8) | (dy << 20);
                } else {
                    resp.data = 0;
                }
            } break;
            case InputRegister::MouseStatus: // Mouse status (0x3000001C)
            {
                std::scoped_lock lock(mouse_mutex_);
                resp.data = mouse_queue_.empty() ? 0 : 1;
            } break;
            default:
                resp.data = 0;
                break;
        }
    }
    return true;
}

void InputDevice::push_key(Word key_event) {
    std::scoped_lock lock(key_mutex_);
    if (key_queue_.size() < 256) {
        key_queue_.push(key_event);
    }
}

void InputDevice::push_mouse(int dx, int dy, uint8_t buttons) {
    std::scoped_lock lock(mouse_mutex_);
    if (mouse_queue_.size() < 256) {
        mouse_queue_.push({.dx = dx, .dy = dy, .buttons = buttons});
    }
}

} // namespace simrv::device
