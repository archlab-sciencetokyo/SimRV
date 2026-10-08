#pragma once

#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device {

// Packet transport is shared by standards-based and platform-specific MAC frontends.
using NetworkBackend = virtio::NetBackend;

}  // namespace simrv::device
