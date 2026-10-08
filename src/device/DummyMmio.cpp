#include "simrv/device/DummyMmio.hpp"

namespace simrv::device {

DummyMmio::DummyMmio(std::string name, Address base, Address size, uint32_t read_value)
    : name_(std::move(name)),
      base_(base),
      size_(size),
      read_value_(read_value) {}

}  // namespace simrv::device
