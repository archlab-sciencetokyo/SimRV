/**
 * @file InputDevice.hpp
 * @brief Separate input device class for keyboard and mouse registers.
 */
#pragma once

#include <queue>
#include <mutex>
#include "simrv/memory/TileLinkNode.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::device {

enum class InputRegister : Address {
    KeyboardRead = 0x0,
    KeyboardStatus = 0x4,
    MouseRead = 0x8,
    MouseStatus = 0xC,
};

class InputDevice : public memory::TileLinkNode {
   public:
    explicit InputDevice(simrv::core::Machine& machine);
    ~InputDevice() override;

    [[nodiscard]] auto name() const -> const char* override { return "input"; }
    [[nodiscard]] auto base_address() const -> Address override { return 0x30000010u; }
    [[nodiscard]] auto size() const -> Address override { return 0x10u; }
    [[nodiscard]] auto contains(Address addr) const -> bool override {
        return addr >= 0x30000010u && addr < 0x30000020u;
    }

    auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool override;

    // Host interface for pushing keyboard/mouse events
    void push_key(Word key_event);
    void push_mouse(int dx, int dy, uint8_t buttons);

   private:
    simrv::core::Machine& machine_;

    // Keyboard event queue
    std::queue<Word> key_queue_;
    std::mutex key_mutex_;

    // Mouse event queue
    struct MouseEvent {
        int dx;
        int dy;
        uint8_t buttons;
    };
    std::queue<MouseEvent> mouse_queue_;
    std::mutex mouse_mutex_;
};

} // namespace simrv::device
