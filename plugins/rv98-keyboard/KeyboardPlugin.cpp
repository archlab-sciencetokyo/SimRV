/**
 * @file KeyboardPlugin.cpp
 * @brief RV98 PS/2 keyboard MMIO device plugin for ArchLab rvcomp / rv98.
 *
 * Emulates the memory-mapped PS/2 Set 2 scan code FIFO accessed by the guest
 * Linux kernel module (rv98_keyboard.ko) at 0x1d000000.
 */
#include <cstdint>
#include <deque>
#include <mutex>
#include <new>
#include <optional>

#include "simrv/device/ExternalDevice.h"
#include "simrv/device/ExternalInputDevice.h"

namespace {

struct Ps2Code {
    uint8_t scancode;
    bool extended;
};

constexpr auto linux_keycode_to_ps2(uint16_t keycode) noexcept -> std::optional<Ps2Code> {
    switch (keycode) {
        // Esc & Numbers
        case 1:
            return Ps2Code{0x76, false};  // KEY_ESC
        case 2:
            return Ps2Code{0x16, false};  // KEY_1
        case 3:
            return Ps2Code{0x1E, false};  // KEY_2
        case 4:
            return Ps2Code{0x26, false};  // KEY_3
        case 5:
            return Ps2Code{0x25, false};  // KEY_4
        case 6:
            return Ps2Code{0x2E, false};  // KEY_5
        case 7:
            return Ps2Code{0x36, false};  // KEY_6
        case 8:
            return Ps2Code{0x3D, false};  // KEY_7
        case 9:
            return Ps2Code{0x3E, false};  // KEY_8
        case 10:
            return Ps2Code{0x46, false};  // KEY_9
        case 11:
            return Ps2Code{0x45, false};  // KEY_0
        case 12:
            return Ps2Code{0x4E, false};  // KEY_MINUS
        case 13:
            return Ps2Code{0x55, false};  // KEY_EQUAL
        case 14:
            return Ps2Code{0x66, false};  // KEY_BACKSPACE
        case 15:
            return Ps2Code{0x0D, false};  // KEY_TAB

        // Row 1 (QWERTY)
        case 16:
            return Ps2Code{0x15, false};  // KEY_Q
        case 17:
            return Ps2Code{0x1D, false};  // KEY_W
        case 18:
            return Ps2Code{0x24, false};  // KEY_E
        case 19:
            return Ps2Code{0x2D, false};  // KEY_R
        case 20:
            return Ps2Code{0x2C, false};  // KEY_T
        case 21:
            return Ps2Code{0x35, false};  // KEY_Y
        case 22:
            return Ps2Code{0x3C, false};  // KEY_U
        case 23:
            return Ps2Code{0x43, false};  // KEY_I
        case 24:
            return Ps2Code{0x44, false};  // KEY_O
        case 25:
            return Ps2Code{0x4D, false};  // KEY_P
        case 26:
            return Ps2Code{0x54, false};  // KEY_LEFTBRACE
        case 27:
            return Ps2Code{0x5B, false};  // KEY_RIGHTBRACE
        case 28:
            return Ps2Code{0x5A, false};  // KEY_ENTER

        // Row 2 (ASDF)
        case 29:
            return Ps2Code{0x14, false};  // KEY_LEFTCTRL
        case 30:
            return Ps2Code{0x1C, false};  // KEY_A
        case 31:
            return Ps2Code{0x1B, false};  // KEY_S
        case 32:
            return Ps2Code{0x23, false};  // KEY_D
        case 33:
            return Ps2Code{0x2B, false};  // KEY_F
        case 34:
            return Ps2Code{0x34, false};  // KEY_G
        case 35:
            return Ps2Code{0x33, false};  // KEY_H
        case 36:
            return Ps2Code{0x3B, false};  // KEY_J
        case 37:
            return Ps2Code{0x42, false};  // KEY_K
        case 38:
            return Ps2Code{0x4B, false};  // KEY_L
        case 39:
            return Ps2Code{0x4C, false};  // KEY_SEMICOLON
        case 40:
            return Ps2Code{0x52, false};  // KEY_APOSTROPHE
        case 41:
            return Ps2Code{0x0E, false};  // KEY_GRAVE

        // Row 3 (ZXCV)
        case 42:
            return Ps2Code{0x12, false};  // KEY_LEFTSHIFT
        case 43:
            return Ps2Code{0x5D, false};  // KEY_BACKSLASH
        case 44:
            return Ps2Code{0x1A, false};  // KEY_Z
        case 45:
            return Ps2Code{0x22, false};  // KEY_X
        case 46:
            return Ps2Code{0x21, false};  // KEY_C
        case 47:
            return Ps2Code{0x2A, false};  // KEY_V
        case 48:
            return Ps2Code{0x32, false};  // KEY_B
        case 49:
            return Ps2Code{0x31, false};  // KEY_N
        case 50:
            return Ps2Code{0x3A, false};  // KEY_M
        case 51:
            return Ps2Code{0x41, false};  // KEY_COMMA
        case 52:
            return Ps2Code{0x49, false};  // KEY_DOT
        case 53:
            return Ps2Code{0x4A, false};  // KEY_SLASH
        case 54:
            return Ps2Code{0x59, false};  // KEY_RIGHTSHIFT

        // Modifiers & Space
        case 56:
            return Ps2Code{0x11, false};  // KEY_LEFTALT
        case 57:
            return Ps2Code{0x29, false};  // KEY_SPACE
        case 58:
            return Ps2Code{0x58, false};  // KEY_CAPSLOCK

        // Function Keys
        case 59:
            return Ps2Code{0x05, false};  // KEY_F1
        case 60:
            return Ps2Code{0x06, false};  // KEY_F2
        case 61:
            return Ps2Code{0x04, false};  // KEY_F3
        case 62:
            return Ps2Code{0x0C, false};  // KEY_F4
        case 63:
            return Ps2Code{0x03, false};  // KEY_F5
        case 64:
            return Ps2Code{0x0B, false};  // KEY_F6
        case 65:
            return Ps2Code{0x83, false};  // KEY_F7
        case 66:
            return Ps2Code{0x0A, false};  // KEY_F8
        case 67:
            return Ps2Code{0x01, false};  // KEY_F9
        case 68:
            return Ps2Code{0x09, false};  // KEY_F10
        case 87:
            return Ps2Code{0x78, false};  // KEY_F11
        case 88:
            return Ps2Code{0x07, false};  // KEY_F12

        // Extended Keys (prefixed with 0xE0)
        case 97:
            return Ps2Code{0x14, true};  // KEY_RIGHTCTRL
        case 100:
            return Ps2Code{0x11, true};  // KEY_RIGHTALT
        case 102:
            return Ps2Code{0x6C, true};  // KEY_HOME
        case 103:
            return Ps2Code{0x75, true};  // KEY_UP
        case 104:
            return Ps2Code{0x7D, true};  // KEY_PAGEUP
        case 105:
            return Ps2Code{0x6B, true};  // KEY_LEFT
        case 106:
            return Ps2Code{0x74, true};  // KEY_RIGHT
        case 107:
            return Ps2Code{0x69, true};  // KEY_END
        case 108:
            return Ps2Code{0x72, true};  // KEY_DOWN
        case 109:
            return Ps2Code{0x7A, true};  // KEY_PAGEDOWN
        case 110:
            return Ps2Code{0x70, true};  // KEY_INSERT
        case 111:
            return Ps2Code{0x71, true};  // KEY_DELETE

        default:
            return std::nullopt;
    }
}

constexpr size_t kMaxQueueSize = 256;

struct KeyboardState {
    std::mutex mutex;
    std::deque<uint8_t> fifo;
};

int init(void** out, const char*) {
    *out = new (std::nothrow) KeyboardState;
    return *out ? 0 : -1;
}

void destroy(void* ctx) { delete static_cast<KeyboardState*>(ctx); }

void reset(void* ctx) {
    auto& kb = *static_cast<KeyboardState*>(ctx);
    std::scoped_lock lock(kb.mutex);
    kb.fifo.clear();
}

int read(void* ctx, uint64_t offset, uint32_t /*size*/, uint64_t* out) {
    if (!out) return -1;
    if (offset != 0) {
        *out = 0;
        return 0;
    }
    auto& kb = *static_cast<KeyboardState*>(ctx);
    std::scoped_lock lock(kb.mutex);
    if (kb.fifo.empty()) {
        *out = 0;
    } else {
        *out = kb.fifo.front();
        kb.fifo.pop_front();
    }
    return 0;
}

int write(void* /*ctx*/, uint64_t /*offset*/, uint32_t /*size*/, uint64_t /*value*/) { return 0; }

void send_key(void* ctx, uint16_t keycode, int pressed) {
    auto* kb = static_cast<KeyboardState*>(ctx);
    if (!kb) return;
    const auto code_opt = linux_keycode_to_ps2(keycode);
    if (!code_opt) return;

    std::scoped_lock lock(kb->mutex);
    if (kb->fifo.size() + 3 >= kMaxQueueSize) return;

    if (code_opt->extended) {
        kb->fifo.push_back(0xE0);
        if (!pressed) {
            kb->fifo.push_back(0xF0);
        }
        kb->fifo.push_back(code_opt->scancode);
    } else {
        if (!pressed) {
            kb->fifo.push_back(0xF0);
        }
        kb->fifo.push_back(code_opt->scancode);
    }
}

const SimRvExternalDevice kDeviceDesc{
    .api_version = SIMRV_EXTERNAL_DEVICE_API_VERSION,
    .name = "rv98-keyboard",
    .compatible = "isct,rv98-keyboard",
    .default_base = 0x1d000000,
    .default_size = 0x1000,
    .init = init,
    .reset = reset,
    .read = read,
    .write = write,
    .destroy = destroy,
    .set_host_services = nullptr,
    .advance_cycles = nullptr,
};

const SimRvExternalInputDevice kInputDesc{
    .api_version = SIMRV_EXTERNAL_INPUT_API_VERSION,
    .send_key = send_key,
};

}  // namespace

extern "C" {

const SimRvExternalDevice* simrv_device_plugin_init() { return &kDeviceDesc; }

const SimRvExternalInputDevice* simrv_input_plugin_init() { return &kInputDesc; }
}
