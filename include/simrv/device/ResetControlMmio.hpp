#pragma once

#include <string>

#include "simrv/memory/MmioDevice.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::device {

/** Write-triggered reset endpoint, reusable by platforms with this register convention. */
class ResetControlMmio final : public memory::MmioDevice {
   public:
    explicit ResetControlMmio(core::Machine& machine, Address base, Address size = 4,
                              std::string name = "reset-control");

    [[nodiscard]] auto name() const -> const char* override { return name_.c_str(); }
    [[nodiscard]] auto base_address() const -> Address override { return base_; }
    [[nodiscard]] auto size() const -> Address override { return size_; }
    [[nodiscard]] auto read32(Address offset) -> uint32_t override;
    void write32(Address offset, uint32_t value) override;

   private:
    core::Machine& machine_;
    Address base_;
    Address size_;
    std::string name_;
};

}  // namespace simrv::device
