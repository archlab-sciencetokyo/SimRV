/**
 * @file MmioDevice.cpp
 * @brief Implementation of MmioDevice typed accessors and DMA mastering.
 */
#include "simrv/memory/MmioDevice.hpp"

#include <concepts>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/TileLinkProtocol.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::memory {

namespace {
template <typename T>
concept NarrowMmioValue = std::same_as<T, uint8_t> || std::same_as<T, uint16_t>;

template <NarrowMmioValue T>
[[nodiscard]] auto read_narrow(MmioDevice& device, MmioOffset offset) -> T {
    const MmioOffset aligned{offset.raw() & ~MmioOffset{3}.raw()};
    const auto shift = static_cast<unsigned>((offset.raw() & (4u - sizeof(T))) * 8u);
    return static_cast<T>(device.read32(aligned) >> shift);
}

template <NarrowMmioValue T>
void write_narrow(MmioDevice& device, MmioOffset offset, T value) {
    const MmioOffset aligned{offset.raw() & ~MmioOffset{3}.raw()};
    const auto shift = static_cast<unsigned>((offset.raw() & (4u - sizeof(T))) * 8u);
    const auto mask = static_cast<uint32_t>(std::numeric_limits<T>::max()) << shift;
    const auto previous = device.read32(aligned);
    device.write32(aligned, (previous & ~mask) | (static_cast<uint32_t>(value) << shift));
}
}  // namespace

MmioDevice::MmioDevice(simrv::core::Machine* machine) : machine_(machine) {}

auto MmioDevice::read8(MmioOffset offset) -> uint8_t { return read_narrow<uint8_t>(*this, offset); }

auto MmioDevice::read16(MmioOffset offset) -> uint16_t {
    return read_narrow<uint16_t>(*this, offset);
}

auto MmioDevice::read32(MmioOffset /*offset*/) -> uint32_t { return 0; }

auto MmioDevice::read64(MmioOffset offset) -> uint64_t {
    const uint64_t lo = read32(offset);
    const uint64_t hi = read32(offset + 4);
    return lo | (hi << 32);
}

void MmioDevice::write8(MmioOffset offset, uint8_t val) { write_narrow(*this, offset, val); }

void MmioDevice::write16(MmioOffset offset, uint16_t val) { write_narrow(*this, offset, val); }

void MmioDevice::write32(MmioOffset /*offset*/, uint32_t /*val*/) {}

void MmioDevice::write64(MmioOffset offset, uint64_t val) {
    write32(offset, static_cast<uint32_t>(val & 0xffffffffU));
    write32(offset + 4, static_cast<uint32_t>((val >> 32) & 0xffffffffU));
}

auto MmioDevice::handle_request(const TlChannelA& req, TlChannelD& resp) -> bool {
    const MmioOffset offset{req.address.raw() - base_address().raw()};
    resp.source = req.source;
    resp.size = req.size;
    resp.denied = false;
    resp.corrupt = false;

    if (req.opcode == TlOpcodeA::Get) {
        resp.opcode = TlOpcodeD::AccessAckData;
        switch (req.size) {
            case 0:
                resp.data = static_cast<simrv::xlen::Word>(read8(offset));
                break;
            case 1:
                resp.data = static_cast<simrv::xlen::Word>(read16(offset));
                break;
            case 2:
                resp.data = static_cast<simrv::xlen::Word>(read32(offset));
                break;
            case 3:
                resp.data = static_cast<simrv::xlen::Word>(read64(offset));
                break;
            default:
                resp.data = static_cast<simrv::xlen::Word>(read32(offset));
                break;
        }
    } else if (req.opcode == TlOpcodeA::PutFullData || req.opcode == TlOpcodeA::PutPartialData) {
        resp.opcode = TlOpcodeD::AccessAck;
        switch (req.size) {
            case 0:
                write8(offset, static_cast<uint8_t>(req.data & 0xffU));
                break;
            case 1:
                write16(offset, static_cast<uint16_t>(req.data & 0xffffU));
                break;
            case 2:
                write32(offset, static_cast<uint32_t>(req.data & 0xffffffffU));
                break;
            case 3:
                write64(offset, static_cast<uint64_t>(req.data));
                break;
            default:
                write32(offset, static_cast<uint32_t>(req.data));
                break;
        }
    } else {
        resp.denied = true;
        return false;
    }
    return true;
}

auto MmioDevice::dma_read(simrv::xlen::PhysAddr paddr, std::span<uint8_t> dst) -> bool {
    if (machine_ == nullptr || dst.empty()) {
        return false;
    }
    return machine_->memory().dma_read(paddr, dst.data(), dst.size());
}

auto MmioDevice::dma_write(simrv::xlen::PhysAddr paddr, std::span<const uint8_t> src) -> bool {
    if (machine_ == nullptr || src.empty()) {
        return false;
    }
    return machine_->memory().dma_write(paddr, src.data(), src.size());
}

}  // namespace simrv::memory
