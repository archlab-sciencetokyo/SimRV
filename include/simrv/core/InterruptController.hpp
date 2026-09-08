/**
 * @file InterruptController.hpp
 * @brief Unified interface for platform and architectural interrupt controllers.
 */
#pragma once

#include "simrv/xlen/Types.hpp"

namespace simrv::core {

enum class InterruptType : uint8_t {
    Software,
    Timer,
    External,
};

/**
 * @class IInterruptController
 * @brief Decouples platform devices (PLIC, ACLINT, AIA) from direct CPU state mutation.
 */
class IInterruptController {
   public:
    virtual ~IInterruptController() = default;

    /// Signal a platform-level interrupt source (e.g. from UART, VirtIO, RTC)
    virtual void set_platform_irq(IrqNumber irq, bool asserted) = 0;

    /// Signal a hart-specific architectural interrupt (MSIP, MTIP, MEIP, SEIP)
    virtual void set_hart_irq(HartId hart, InterruptType type, PrivilegeLevel priv,
                              bool asserted) = 0;
};

}  // namespace simrv::core
