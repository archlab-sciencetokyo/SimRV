/**
 * @file Power.hpp
 * @brief SiFive Test Finisher syscon power device interface.
 */
#pragma once

#include "simrv/memory/TileLinkNode.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::device {

enum class PowerCommand : Word {
    Poweroff = 0x5555U,
    Crash = 0x3333U,
    Reboot = 0x7777U,
};

/**
 * @class PowerMmio
 * @brief SiFive Test Finisher syscon power device emulation.
 *
 * Intercepts writes to trigger cleanly managed guest reboots or shutdowns.
 */
class PowerMmio : public memory::TileLinkNode {
   public:
    /// Construct a Power MMIO device instance.
    explicit PowerMmio(simrv::core::Machine& machine);

    static constexpr Address kBaseAddress = static_cast<Address>(0x00100000u);
    static constexpr Address kSize = static_cast<Address>(0x00001000u);

    [[nodiscard]] auto name() const -> const char* override { return "power"; }
    [[nodiscard]] auto base_address() const -> Address override { return kBaseAddress; }
    [[nodiscard]] auto size() const -> Address override { return kSize; }

    auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool override;

   private:
    simrv::core::Machine& machine_;
};

}  // namespace simrv::device
