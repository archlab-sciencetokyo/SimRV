/**
 * @file MmioDevice.cpp
 * @brief Implementation of MmioDevice typed accessors and DMA mastering.
 */
#include "simrv/memory/MmioDevice.hpp"

#include <cstring>

#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemoryUtil.hpp"

namespace simrv::memory {

MmioDevice::MmioDevice(simrv::core::Machine* machine) : machine_(machine) {}

auto MmioDevice::read8(Address offset) -> uint8_t {
    const Address aligned = offset & ~Address{3};
    const uint32_t val32 = read32(aligned);
    const unsigned shift = static_cast<unsigned>((offset & Address{3}) * 8);
    return static_cast<uint8_t>((val32 >> shift) & 0xffU);
}

auto MmioDevice::read16(Address offset) -> uint16_t {
    const Address aligned = offset & ~Address{3};
    const uint32_t val32 = read32(aligned);
    const unsigned shift = static_cast<unsigned>((offset & Address{2}) * 8);
    return static_cast<uint16_t>((val32 >> shift) & 0xffffU);
}

auto MmioDevice::read32(Address /*offset*/) -> uint32_t { return 0; }

auto MmioDevice::read64(Address offset) -> uint64_t {
    const uint64_t lo = read32(offset);
    const uint64_t hi = read32(offset + 4);
    return lo | (hi << 32);
}

void MmioDevice::write8(Address offset, uint8_t val) {
    const Address aligned = offset & ~Address{3};
    uint32_t val32 = read32(aligned);
    const unsigned shift = static_cast<unsigned>((offset & Address{3}) * 8);
    const uint32_t mask = ~(0xffU << shift);
    val32 = (val32 & mask) | (static_cast<uint32_t>(val) << shift);
    write32(aligned, val32);
}

void MmioDevice::write16(Address offset, uint16_t val) {
    const Address aligned = offset & ~Address{3};
    uint32_t val32 = read32(aligned);
    const unsigned shift = static_cast<unsigned>((offset & Address{2}) * 8);
    const uint32_t mask = ~(0xffffU << shift);
    val32 = (val32 & mask) | (static_cast<uint32_t>(val) << shift);
    write32(aligned, val32);
}

void MmioDevice::write32(Address /*offset*/, uint32_t /*val*/) {}

void MmioDevice::write64(Address offset, uint64_t val) {
    write32(offset, static_cast<uint32_t>(val & 0xffffffffU));
    write32(offset + 4, static_cast<uint32_t>((val >> 32) & 0xffffffffU));
}

auto MmioDevice::handle_request(const TlChannelA& req, TlChannelD& resp) -> bool {
    const Address offset = req.address - base_address();
    resp.source = req.source;
    resp.size = req.size;
    resp.error = false;

    if (req.opcode == TlOpcodeA::Get) {
        resp.opcode = TlOpcodeD::AccessAckData;
        switch (req.size) {
            case 0:
                resp.data = static_cast<Word>(read8(offset));
                break;
            case 1:
                resp.data = static_cast<Word>(read16(offset));
                break;
            case 2:
                resp.data = static_cast<Word>(read32(offset));
                break;
            case 3:
                resp.data = static_cast<Word>(read64(offset));
                break;
            default:
                resp.data = static_cast<Word>(read32(offset));
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
        resp.error = true;
        return false;
    }
    return true;
}

auto MmioDevice::dma_read(Address paddr, std::span<uint8_t> dst) -> bool {
    if (machine_ == nullptr || machine_->ram_data() == nullptr || dst.empty()) {
        return false;
    }
    const auto geometry = machine_->memory_geometry();
    const Address dram_base = geometry.dram_base;
    const uint64_t dram_size = geometry.dram_size;

    if (paddr < dram_base || (paddr + dst.size()) > (dram_base + dram_size)) {
        return false;
    }
    std::memcpy(dst.data(), machine_->ram_data() + (paddr - dram_base), dst.size());
    return true;
}

auto MmioDevice::dma_write(Address paddr, std::span<const uint8_t> src) -> bool {
    if (machine_ == nullptr || machine_->ram_data() == nullptr || src.empty()) {
        return false;
    }
    const auto geometry = machine_->memory_geometry();
    const Address dram_base = geometry.dram_base;
    const uint64_t dram_size = geometry.dram_size;

    if (paddr < dram_base || (paddr + src.size()) > (dram_base + dram_size)) {
        return false;
    }
    std::memcpy(machine_->ram_data() + (paddr - dram_base), src.data(), src.size());
    return true;
}

}  // namespace simrv::memory
