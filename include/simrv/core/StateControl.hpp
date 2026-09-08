/**
 * @file StateControl.hpp
 * @brief Facade header providing backward compatibility for TrapController, PLIC, and CLINT.
 */
#pragma once

#include "simrv/core/TrapController.hpp"
#include "simrv/device/Clint.hpp"
#include "simrv/device/Plic.hpp"

namespace simrv::core {

using PlicMmio = simrv::device::Plic;
using ClintMmio = simrv::device::Clint;

/**
 * @class InterruptController
 * @brief Backward-compatibility adapter for legacy PLIC call sites.
 */
class InterruptController {
   public:
    static void updateMip(PlicMmio& plic, ArchState& /*state*/) { plic.update_mip(); }
    static void setIrq(PlicMmio& plic, IrqNumber irq_num, int state_val) {
        plic.set_irq(irq_num, state_val);
    }
    static void setIrq(PlicMmio& plic, IrqNumber irq_num, bool active) {
        plic.set_irq(irq_num, active ? 1 : 0);
    }
};

}  // namespace simrv::core
