#pragma once

#include <string>

#include "simrv/memory/MmioDevice.hpp"

namespace simrv::device {

/** Configurable inert MMIO placeholder for SoC maps without a device model. */
class DummyMmio final : public memory::MmioDevice {
   public:
    DummyMmio(std::string name, Address base, Address size, uint32_t read_value);

    [[nodiscard]] auto name() const -> const char* override { return name_.c_str(); }
    [[nodiscard]] auto base_address() const -> Address override { return base_; }
    [[nodiscard]] auto size() const -> Address override { return size_; }
    [[nodiscard]] auto read32(Address /*offset*/) -> uint32_t override { return read_value_; }
    void write32(Address /*offset*/, uint32_t /*value*/) override {}
   private:
    std::string name_;
    Address base_;
    Address size_;
    uint32_t read_value_;
};

}  // namespace simrv::device
