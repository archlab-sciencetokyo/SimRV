/**
 * @file InputDevice.hpp
 * @brief Abstract interface for host input injection into machine endpoints.
 */
#pragma once

#include <cstdint>

namespace simrv::device {

class InputDevice {
   public:
    virtual ~InputDevice() = default;
    virtual void send_key(uint16_t code, bool pressed) = 0;
};

}  // namespace simrv::device
