#include "simrv/device/ResetControlMmio.hpp"

#include "simrv/core/Machine.hpp"

namespace simrv::device {

ResetControlMmio::ResetControlMmio(core::Machine& machine, simrv::xlen::Address base,
                                   simrv::xlen::Address size, std::string name)
    : memory::MmioDevice(&machine),
      machine_(machine),
      base_(base),
      size_(size),
      name_(std::move(name)) {}

auto ResetControlMmio::read32(simrv::xlen::Address /*offset*/) -> uint32_t { return 0; }

void ResetControlMmio::write32(simrv::xlen::Address offset, uint32_t /*value*/) {
    if (offset == 0) machine_.request_reboot();
}

}  // namespace simrv::device
