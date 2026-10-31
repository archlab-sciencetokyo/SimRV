/**
 * @file PcieRootComplex.hpp
 * @brief PCIe Host Bridge Root Complex and ECAM/MMIO Interconnect for SimRV.
 */
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "simrv/core/SoCConfig.hpp"
#include "simrv/device/AIA.hpp"
#include "simrv/device/pci/PciDevice.hpp"
#include "simrv/device/pci/PciTypes.hpp"
#include "simrv/memory/TileLinkNode.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::device {

class PcieRootComplex {
   public:
    static constexpr simrv::xlen::Address kEcamBaseAddress =
        static_cast<simrv::xlen::Address>(SIMRV_PLATFORM_PCIE_ECAM_BASE);
    static constexpr simrv::xlen::Address kEcamSize =
        static_cast<simrv::xlen::Address>(SIMRV_PLATFORM_PCIE_ECAM_SIZE);

    static constexpr simrv::xlen::Address kMmioBaseAddress =
        static_cast<simrv::xlen::Address>(SIMRV_PLATFORM_PCIE_WINDOW_BASE);
    static constexpr simrv::xlen::Address kMmioSize =
        static_cast<simrv::xlen::Address>(SIMRV_PLATFORM_PCIE_WINDOW_SIZE);

    PcieRootComplex(simrv::core::Machine* machine, Aplic* aplic_s = nullptr,
                    Imsic* imsic_s = nullptr, simrv::core::PcieHostConfig config = {});
    ~PcieRootComplex() = default;

    auto attach_device(PciBdf bdf, std::shared_ptr<PciDevice> device, uint32_t intx_source = 0)
        -> bool;
    auto attach_device(PciBusNumber bus, PciDeviceNumber dev, PciFunctionNumber func,
                       std::shared_ptr<PciDevice> device) -> bool {
        return attach_device(PciBdf{.bus = bus, .dev = dev, .func = func}, std::move(device));
    }

    [[nodiscard]] auto get_device(PciBdf bdf) const noexcept -> PciDevice*;
    [[nodiscard]] auto get_device(PciBusNumber bus, PciDeviceNumber dev,
                                  PciFunctionNumber func) const noexcept -> PciDevice* {
        return get_device(PciBdf{.bus = bus, .dev = dev, .func = func});
    }

    [[nodiscard]] auto ecam_read(simrv::xlen::Address offset, PciAccessBytes size) -> PciConfigData;
    void ecam_write(simrv::xlen::Address offset, PciConfigData val, PciAccessBytes size);

    [[nodiscard]] auto mmio_read(simrv::xlen::Address addr, PciAccessBytes size) -> PciMmioData;
    void mmio_write(simrv::xlen::Address addr, PciMmioData val, PciAccessBytes size);

    void assert_device_irq(PciBdf bdf);
    void assert_device_irq(PciBusNumber bus, PciDeviceNumber dev, PciFunctionNumber func) {
        assert_device_irq(PciBdf{.bus = bus, .dev = dev, .func = func});
    }
    void deassert_device_irq(PciBdf bdf);
    void deassert_device_irq(PciBusNumber bus, PciDeviceNumber dev, PciFunctionNumber func) {
        deassert_device_irq(PciBdf{.bus = bus, .dev = dev, .func = func});
    }

    void send_msi(PciBdf bdf, simrv::xlen::Address addr, uint32_t message_data);
    void send_msi(PciBusNumber bus, PciDeviceNumber dev, PciFunctionNumber func,
                  simrv::xlen::Address addr, uint32_t message_data) {
        send_msi(PciBdf{.bus = bus, .dev = dev, .func = func}, addr, message_data);
    }

    void rebuild_bar_mappings();

    struct BarMapping {
        simrv::xlen::Address base = 0;
        simrv::xlen::Address end = 0;
        PciDevice* device = nullptr;
        PciBarIndex bar_idx = 0;
    };

    class EcamNode : public memory::TileLinkNode {
       public:
        explicit EcamNode(PcieRootComplex* rc) : rc_(rc) {}
        [[nodiscard]] auto name() const -> const char* override { return "pcie-ecam"; }
        [[nodiscard]] auto base_address() const -> simrv::xlen::Address override {
            return rc_->config_.ecam_base;
        }
        [[nodiscard]] auto size() const -> simrv::xlen::Address override {
            return rc_->config_.ecam_size;
        }
        auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp)
            -> bool override;

       private:
        PcieRootComplex* rc_;
    };

    class MmioNode : public memory::TileLinkNode {
       public:
        explicit MmioNode(PcieRootComplex* rc) : rc_(rc) {}
        [[nodiscard]] auto name() const -> const char* override { return "pcie-mmio"; }
        [[nodiscard]] auto base_address() const -> simrv::xlen::Address override {
            return rc_->config_.mmio_base;
        }
        [[nodiscard]] auto size() const -> simrv::xlen::Address override {
            return rc_->config_.mmio_size;
        }
        auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp)
            -> bool override;

       private:
        PcieRootComplex* rc_;
    };

    [[nodiscard]] auto ecam_node() -> EcamNode& { return ecam_node_; }
    [[nodiscard]] auto mmio_node() -> MmioNode& { return mmio_node_; }
    [[nodiscard]] auto machine() -> simrv::core::Machine* { return machine_; }
    [[nodiscard]] auto active_bar_count() const noexcept -> size_t { return active_bars_.size(); }

   private:
    simrv::core::Machine* machine_;
    Aplic* aplic_s_;
    Imsic* imsic_s_;
    simrv::core::PcieHostConfig config_;

    struct IntxRoute {
        PciBdf bdf;
        uint32_t source;
    };
    std::vector<IntxRoute> intx_routes_;

    EcamNode ecam_node_{this};
    MmioNode mmio_node_{this};

    static constexpr size_t kMaxSlots = 32;
    std::array<PciDevice*, kMaxSlots> devices_by_slot_{{nullptr}};
    std::vector<std::shared_ptr<PciDevice>> owned_devices_;

    static constexpr size_t kBarMruCapacity = 4;
    std::vector<BarMapping> active_bars_;
    mutable std::array<BarMapping, kBarMruCapacity> mru_bars_{};
    mutable size_t mru_bar_count_{0};
};

}  // namespace simrv::device
