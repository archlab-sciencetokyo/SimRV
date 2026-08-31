/**
 * @file PcieRootComplex.hpp
 * @brief PCIe Host Bridge Root Complex and ECAM/MMIO Interconnect for SimRV.
 */
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "simrv/Define.hpp"
#include "simrv/device/AIA.hpp"
#include "simrv/device/pci/PciDevice.hpp"
#include "simrv/memory/TileLinkNode.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::device {

class PcieRootComplex {
   public:
    static constexpr Address kEcamBaseAddress = 0x30000000ULL;
    static constexpr Address kEcamSize = 0x10000000ULL;  // 256MB ECAM space

    static constexpr Address kMmioBaseAddress = 0x40000000ULL;
    static constexpr Address kMmioSize = 0x10000000ULL;  // 256MB MMIO window

    PcieRootComplex(simrv::core::Machine* machine, Aplic* aplic_s = nullptr,
                    Imsic* imsic_s = nullptr);
    ~PcieRootComplex() = default;

    auto attach_device(PciBdf bdf, std::shared_ptr<PciDevice> device) -> bool;
    auto attach_device(uint8_t bus, uint8_t dev, uint8_t func, std::shared_ptr<PciDevice> device)
        -> bool {
        return attach_device(PciBdf{.bus = bus, .dev = dev, .func = func}, std::move(device));
    }

    [[nodiscard]] auto get_device(PciBdf bdf) -> std::shared_ptr<PciDevice>;
    [[nodiscard]] auto get_device(uint8_t bus, uint8_t dev, uint8_t func)
        -> std::shared_ptr<PciDevice> {
        return get_device(PciBdf{.bus = bus, .dev = dev, .func = func});
    }

    [[nodiscard]] auto ecam_read(Address offset, uint8_t size) -> uint32_t;
    void ecam_write(Address offset, uint32_t val, uint8_t size);

    [[nodiscard]] auto mmio_read(Address addr, uint8_t size) -> uint32_t;
    void mmio_write(Address addr, uint32_t val, uint8_t size);

    void assert_device_irq(PciBdf bdf);
    void assert_device_irq(uint8_t bus, uint8_t dev, uint8_t func) {
        assert_device_irq(PciBdf{.bus = bus, .dev = dev, .func = func});
    }

    class EcamNode : public memory::TileLinkNode {
       public:
        explicit EcamNode(PcieRootComplex* rc) : rc_(rc) {}
        [[nodiscard]] auto name() const -> const char* override { return "pcie-ecam"; }
        [[nodiscard]] auto base_address() const -> Address override { return kEcamBaseAddress; }
        [[nodiscard]] auto size() const -> Address override { return kEcamSize; }
        auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp)
            -> bool override;

       private:
        PcieRootComplex* rc_;
    };

    class MmioNode : public memory::TileLinkNode {
       public:
        explicit MmioNode(PcieRootComplex* rc) : rc_(rc) {}
        [[nodiscard]] auto name() const -> const char* override { return "pcie-mmio"; }
        [[nodiscard]] auto base_address() const -> Address override { return kMmioBaseAddress; }
        [[nodiscard]] auto size() const -> Address override { return kMmioSize; }
        auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp)
            -> bool override;

       private:
        PcieRootComplex* rc_;
    };

    [[nodiscard]] auto ecam_node() -> EcamNode& { return ecam_node_; }
    [[nodiscard]] auto mmio_node() -> MmioNode& { return mmio_node_; }
    [[nodiscard]] auto machine() -> simrv::core::Machine* { return machine_; }

   private:
    simrv::core::Machine* machine_;
    Aplic* aplic_s_;
    [[maybe_unused]] Imsic* imsic_s_;

    EcamNode ecam_node_{this};
    MmioNode mmio_node_{this};

    struct DeviceEntry {
        PciBdf bdf;
        std::shared_ptr<PciDevice> device;
    };
    std::vector<DeviceEntry> attached_devices_;
};

}  // namespace simrv::device
