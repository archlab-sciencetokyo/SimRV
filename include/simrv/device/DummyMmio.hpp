#pragma once

#include <string>

#include "simrv/memory/MmioDevice.hpp"

namespace simrv::device {

/** Configurable inert MMIO placeholder for SoC maps without a device model. */
class DummyMmio final : public memory::MmioDevice {
   public:
    DummyMmio(std::string name, simrv::xlen::Address base, simrv::xlen::Address size,
              uint32_t read_value);

    [[nodiscard]] auto name() const -> const char* override { return name_.c_str(); }
    [[nodiscard]] auto base_address() const -> simrv::xlen::Address override { return base_; }
    [[nodiscard]] auto size() const -> simrv::xlen::Address override { return size_; }
    [[nodiscard]] auto read32(simrv::xlen::Address /*offset*/) -> uint32_t override {
        return read_value_;
    }
    void write32(simrv::xlen::Address /*offset*/, uint32_t /*value*/) override {}

   private:
    std::string name_;
    simrv::xlen::Address base_;
    simrv::xlen::Address size_;
    uint32_t read_value_;
};

}  // namespace simrv::device
