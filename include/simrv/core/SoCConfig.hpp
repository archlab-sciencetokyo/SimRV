/**
 * @file SoCConfig.hpp
 * @brief Declarative description of the simulated SoC platform.
 */
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "simrv/core/InterruptTopology.hpp"
#include "simrv/core/PlatformMap.h"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {

enum class SoCDeviceKind : uint8_t {
    Uart,
    Rtc,
    Power,
    AclintMtimer,
    AclintMswi,
    Imsic,
    Aplic,
    Plic,
    Clint,
    VirtioMmioBlock,
    VirtioMmioConsole,
    VirtioMmioRng,
    VirtioMmioGpu,
    VirtioMmioInput,
    VirtioMmioSound,
    VirtioMmioNet,
    VirtioMmioFs,
    DmaController,
    ResetControl,
    RingBufferEthernet,
    DummyMmio,
    CustomMmio,
    PFlash,
};

[[nodiscard]] inline auto soc_device_kind_name(SoCDeviceKind kind) -> std::string_view {
    switch (kind) {
        case SoCDeviceKind::Uart:
            return "uart";
        case SoCDeviceKind::Rtc:
            return "rtc";
        case SoCDeviceKind::Power:
            return "power";
        case SoCDeviceKind::AclintMtimer:
            return "aclint-mtimer";
        case SoCDeviceKind::AclintMswi:
            return "aclint-mswi";
        case SoCDeviceKind::Imsic:
            return "imsic";
        case SoCDeviceKind::Aplic:
            return "aplic";
        case SoCDeviceKind::Plic:
            return "plic";
        case SoCDeviceKind::Clint:
            return "clint";
        case SoCDeviceKind::VirtioMmioBlock:
            return "virtio-block";
        case SoCDeviceKind::VirtioMmioConsole:
            return "virtio-console";
        case SoCDeviceKind::VirtioMmioRng:
            return "virtio-rng";
        case SoCDeviceKind::VirtioMmioGpu:
            return "virtio-gpu";
        case SoCDeviceKind::VirtioMmioInput:
            return "virtio-input";
        case SoCDeviceKind::VirtioMmioSound:
            return "virtio-sound";
        case SoCDeviceKind::VirtioMmioNet:
            return "virtio-net";
        case SoCDeviceKind::VirtioMmioFs:
            return "virtio-fs";
        case SoCDeviceKind::DmaController:
            return "dma";
        case SoCDeviceKind::ResetControl:
            return "reset-control";
        case SoCDeviceKind::RingBufferEthernet:
            return "ring-buffer-ethernet";
        case SoCDeviceKind::DummyMmio:
            return "dummy-mmio";
        case SoCDeviceKind::CustomMmio:
            return "custom-mmio";
        case SoCDeviceKind::PFlash:
            return "pflash";
    }
    return "unknown";
}

struct SoCDeviceRegion {
    std::string name;
    simrv::xlen::Address base = 0;
    simrv::xlen::Address size = 0;
};

struct SoCDeviceConfig {
    SoCDeviceKind kind{};
    std::string name;
    simrv::xlen::Address base = 0;
    simrv::xlen::Address size = 0x1000;
    uint32_t irq = 0;
    bool enabled = true;
    std::vector<SoCDeviceRegion> regions;
    std::string compatible;
    uint32_t read_value = 0;
    std::array<uint8_t, 6> mac_address{0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    uint64_t clock_hz = 0;
    std::optional<uint8_t> pci_bus;
    std::optional<uint8_t> pci_slot;
    std::optional<uint8_t> pci_function;
    std::optional<uint32_t> pci_intx_source;

    SoCDeviceConfig() = default;
    SoCDeviceConfig(SoCDeviceKind device_kind, std::string device_name,
                    simrv::xlen::Address device_base, simrv::xlen::Address device_size = 0x1000,
                    uint32_t device_irq = 0, bool device_enabled = true,
                    std::vector<SoCDeviceRegion> device_regions = {},
                    std::string device_compatible = {}, uint32_t device_read_value = 0,
                    std::array<uint8_t, 6> device_mac = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56})
        : kind(device_kind),
          name(std::move(device_name)),
          base(device_base),
          size(device_size),
          irq(device_irq),
          enabled(device_enabled),
          regions(std::move(device_regions)),
          compatible(std::move(device_compatible)),
          read_value(device_read_value),
          mac_address(device_mac),
          clock_hz(device_kind == SoCDeviceKind::Uart ? SIMRV_PLATFORM_UART_CLOCK_HZ : 0) {}

    [[nodiscard]] auto find_region(std::string_view region_name) const -> const SoCDeviceRegion* {
        const auto it = std::ranges::find_if(
            regions, [region_name](const auto& region) { return region.name == region_name; });
        return it == regions.end() ? nullptr : &*it;
    }
};

struct PcieHostConfig {
    simrv::xlen::Address ecam_base = SIMRV_PLATFORM_PCIE_ECAM_BASE;
    simrv::xlen::Address ecam_size = SIMRV_PLATFORM_PCIE_ECAM_SIZE;
    simrv::xlen::Address mmio_base = SIMRV_PLATFORM_PCIE_WINDOW_BASE;
    simrv::xlen::Address mmio_size = SIMRV_PLATFORM_PCIE_WINDOW_SIZE;
    uint8_t first_bus = 0;
    uint8_t last_bus = 255;
};

enum class SmpMode : uint8_t {
    SingleThreaded = 0,  ///< Deterministic lockstep execution on the primary simulation thread
    MultiThreaded = 1,   ///< Concurrent execution across background host worker threads

    // Backward-compatible aliases
    CycleAccurate = SingleThreaded,
    CycleOriented = MultiThreaded,
};

struct SharedCacheConfig {
    bool enabled = false;
    uint32_t capacity_bytes = 0;
    uint32_t associativity = 1;
    uint32_t line_bytes = 32;
    uint32_t hit_latency = 1;
    uint32_t miss_latency = 1;
};

struct SmpClusterConfig {
    uint32_t id = 0;
    std::vector<uint32_t> harts;
    SharedCacheConfig l2{};
};

struct SmpTopologyConfig {
    uint32_t hart_count = 1;
    SmpMode mode = SmpMode::CycleAccurate;
    uint32_t synchronization_quantum = 64;
    std::vector<SmpClusterConfig> clusters;
    SharedCacheConfig l3{.enabled = true,
                         .capacity_bytes = 512 * 1024,
                         .associativity = 16,
                         .line_bytes = 32,
                         .hit_latency = 1,
                         .miss_latency = 1};

    void resize_harts(uint32_t count) {
        hart_count = count;
        for (auto& cluster : clusters) {
            std::erase_if(cluster.harts, [count](uint32_t hart) { return hart >= count; });
        }
        std::erase_if(clusters, [](const auto& cluster) { return cluster.harts.empty(); });
        if (clusters.empty()) return;
        for (uint32_t hart = 0; hart < count; ++hart) {
            if (std::ranges::any_of(clusters, [hart](const auto& cluster) {
                    return std::ranges::find(cluster.harts, hart) != cluster.harts.end();
                }))
                continue;
            clusters.front().harts.push_back(hart);
        }
    }

    [[nodiscard]] auto cluster_for_hart(uint32_t hart) const -> uint32_t {
        for (const auto& cluster : clusters) {
            if (std::ranges::find(cluster.harts, hart) != cluster.harts.end()) return cluster.id;
        }
        return 0;
    }

    [[nodiscard]] auto validate() const -> std::expected<void, std::string>;
};

[[nodiscard]] inline auto soc_device_kind(std::string_view name) -> std::optional<SoCDeviceKind> {
    if (name == "uart") return SoCDeviceKind::Uart;
    if (name == "rtc") return SoCDeviceKind::Rtc;
    if (name == "power") return SoCDeviceKind::Power;
    if (name == "aclint-mtimer" || name == "timer") return SoCDeviceKind::AclintMtimer;
    if (name == "aclint-mswi" || name == "software-interrupt") return SoCDeviceKind::AclintMswi;
    if (name == "imsic") return SoCDeviceKind::Imsic;
    if (name == "aplic") return SoCDeviceKind::Aplic;
    if (name == "plic") return SoCDeviceKind::Plic;
    if (name == "clint") return SoCDeviceKind::Clint;
    if (name == "virtio-block" || name == "block") return SoCDeviceKind::VirtioMmioBlock;
    if (name == "virtio-console" || name == "console") return SoCDeviceKind::VirtioMmioConsole;
    if (name == "virtio-rng" || name == "rng") return SoCDeviceKind::VirtioMmioRng;
    if (name == "virtio-gpu" || name == "gpu") return SoCDeviceKind::VirtioMmioGpu;
    if (name == "virtio-input" || name == "input") return SoCDeviceKind::VirtioMmioInput;
    if (name == "virtio-sound" || name == "sound") return SoCDeviceKind::VirtioMmioSound;
    if (name == "virtio-net" || name == "net") return SoCDeviceKind::VirtioMmioNet;
    if (name == "virtio-fs" || name == "fs") return SoCDeviceKind::VirtioMmioFs;
    if (name == "dma" || name == "dma-controller") return SoCDeviceKind::DmaController;
    if (name == "reset-control") return SoCDeviceKind::ResetControl;
    if (name == "ring-buffer-ethernet") return SoCDeviceKind::RingBufferEthernet;
    if (name == "dummy-mmio") return SoCDeviceKind::DummyMmio;
    if (name == "custom-mmio") return SoCDeviceKind::CustomMmio;
    return std::nullopt;
}

[[nodiscard]] inline auto soc_pci_device_default(SoCDeviceKind kind, std::string name,
                                                 simrv::xlen::Address base,
                                                 simrv::xlen::Address size, uint32_t mmio_irq,
                                                 uint8_t slot, uint32_t intx_source)
    -> SoCDeviceConfig {
    SoCDeviceConfig device{kind, std::move(name), base, size, mmio_irq};
    device.pci_slot = slot;
    device.pci_intx_source = intx_source;
    return device;
}

[[nodiscard]] inline auto soc_device_default(SoCDeviceKind kind) -> SoCDeviceConfig {
    switch (kind) {
        case SoCDeviceKind::Uart: {
            SoCDeviceConfig device{kind, "uart0", SIMRV_PLATFORM_UART_BASE,
                                   SIMRV_PLATFORM_UART_SIZE, 3};
            device.clock_hz = SIMRV_PLATFORM_UART_CLOCK_HZ;
            return device;
        }
        case SoCDeviceKind::Rtc:
            return {kind, "rtc0", 0x70000000, 0x1000, 11};
        case SoCDeviceKind::Power:
            return {kind, "power0", 0x00100000, 0x1000, 0};
        case SoCDeviceKind::AclintMtimer:
            return {kind, "timer0", 0x02000000, 0x00010000, 7};
        case SoCDeviceKind::AclintMswi:
            return {kind, "mswi0", 0x02010000, 0x00010000, 3};
        case SoCDeviceKind::Imsic:
            return {kind,
                    "imsic0",
                    0x24000000,
                    0x00010000,
                    0,
                    true,
                    {{.name = "supervisor", .base = 0x28000000, .size = 0x00010000}}};
        case SoCDeviceKind::Aplic:
            return {kind,
                    "aplic0",
                    0x0c000000,
                    0x00004000,
                    0,
                    true,
                    {{.name = "supervisor", .base = 0x0d000000, .size = 0x00004000}}};
        case SoCDeviceKind::Plic:
            return {kind, "plic0", 0x50000000, 0x4000000, 0};
        case SoCDeviceKind::Clint:
            return {kind, "clint0", 0x60000000, 0xc0000, 0};
        case SoCDeviceKind::VirtioMmioBlock:
            return soc_pci_device_default(kind, "disk0", SIMRV_PLATFORM_VIRTIO_BLOCK_BASE,
                                          SIMRV_PLATFORM_VIRTIO_SLOT_SIZE, 2, 1, 17);
        case SoCDeviceKind::VirtioMmioConsole:
            return soc_pci_device_default(kind, "console0", SIMRV_PLATFORM_VIRTIO_CONSOLE_BASE,
                                          SIMRV_PLATFORM_VIRTIO_SLOT_SIZE, 1, 2, 18);
        case SoCDeviceKind::VirtioMmioRng:
            return soc_pci_device_default(kind, "rng0", SIMRV_PLATFORM_VIRTIO_RNG_BASE,
                                          SIMRV_PLATFORM_VIRTIO_SLOT_SIZE, 4, 3, 19);
        case SoCDeviceKind::VirtioMmioGpu:
            return soc_pci_device_default(kind, "gpu0", SIMRV_PLATFORM_VIRTIO_GPU_BASE,
                                          SIMRV_PLATFORM_VIRTIO_SLOT_SIZE, 5, 4, 20);
        case SoCDeviceKind::VirtioMmioInput:
            return soc_pci_device_default(kind, "input0", SIMRV_PLATFORM_VIRTIO_INPUT_BASE,
                                          SIMRV_PLATFORM_VIRTIO_SLOT_SIZE, 6, 5, 21);
        case SoCDeviceKind::VirtioMmioSound:
            return soc_pci_device_default(kind, "sound0", SIMRV_PLATFORM_VIRTIO_SOUND_BASE,
                                          SIMRV_PLATFORM_VIRTIO_SLOT_SIZE, 7, 6, 22);
        case SoCDeviceKind::VirtioMmioNet:
            return soc_pci_device_default(kind, "net0", SIMRV_PLATFORM_VIRTIO_NET_BASE,
                                          SIMRV_PLATFORM_VIRTIO_SLOT_SIZE, 8, 7, 23);
        case SoCDeviceKind::VirtioMmioFs:
            return soc_pci_device_default(kind, "fs0", SIMRV_PLATFORM_VIRTIO_FS_BASE,
                                          SIMRV_PLATFORM_VIRTIO_SLOT_SIZE, 9, 8, 24);
        case SoCDeviceKind::DmaController:
            return {kind, "dma0", SIMRV_PLATFORM_DMA_BASE, SIMRV_PLATFORM_DMA_SIZE,
                    SIMRV_PLATFORM_DMA_IRQ};
        case SoCDeviceKind::ResetControl:
            return {kind, "reset", 0x10000100, 4, 0};
        case SoCDeviceKind::RingBufferEthernet:
            return {kind,
                    "ethernet",
                    0x14000000,
                    0x4000,
                    2,
                    true,
                    {{.name = "rxbuf", .base = 0x18000000, .size = 0x4000},
                     {.name = "txbuf", .base = 0x1c000000, .size = 0x2000}},
                    {},
                    0,
                    {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}};
        case SoCDeviceKind::DummyMmio:
            return {kind, "dummy", 0, 4, 0, true, {}, "simrv,dummy-mmio", 0};
        case SoCDeviceKind::CustomMmio:
            return {kind, "custom", 0, 0x1000, 0, true, {}, {}, 0};
        case SoCDeviceKind::PFlash:
            return {kind,
                    "pflash0",
                    0x20000000,
                    0x02000000,
                    0,
                    true,
                    {{.name = "pflash1", .base = 0x22000000, .size = 0x02000000}},
                    "cfi-flash",
                    0};
    }
    return {};
}

/** Normalized platform description shared by composition and generated metadata. */
struct SoCConfig {
    std::string name = "virt-pcie";
    std::string cpu_model;
    bool enable_pcie = true;
    bool enable_mmio = false;
    bool disable_unlisted_devices = false;
    PcieHostConfig pcie{};
    std::optional<simrv::xlen::Address> dram_base;
    std::optional<simrv::xlen::Address> dram_size;
    std::optional<simrv::xlen::Address> reset_pc;
    std::optional<simrv::xlen::Address> tohost;
    SmpTopologyConfig smp{};
    std::vector<SoCDeviceConfig> devices;

    [[nodiscard]] static auto virt_pcie() -> SoCConfig {
        return {.name = "virt-pcie",
                .cpu_model = {},
                .enable_pcie = true,
                .enable_mmio = false,
                .disable_unlisted_devices = false,
                .dram_base = std::nullopt,
                .dram_size = std::nullopt,
                .reset_pc = std::nullopt,
                .tohost = std::nullopt,
                .devices = {soc_device_default(SoCDeviceKind::PFlash)}};
    }

    [[nodiscard]] static auto virt_mmio() -> SoCConfig {
        return {.name = "virt-mmio",
                .cpu_model = {},
                .enable_pcie = false,
                .enable_mmio = true,
                .disable_unlisted_devices = false,
                .dram_base = std::nullopt,
                .dram_size = std::nullopt,
                .reset_pc = std::nullopt,
                .tohost = std::nullopt,
                .devices = {soc_device_default(SoCDeviceKind::VirtioMmioBlock),
                            soc_device_default(SoCDeviceKind::VirtioMmioConsole),
                            soc_device_default(SoCDeviceKind::VirtioMmioRng),
                            soc_device_default(SoCDeviceKind::VirtioMmioGpu),
                            soc_device_default(SoCDeviceKind::VirtioMmioInput),
                            soc_device_default(SoCDeviceKind::VirtioMmioSound),
                            soc_device_default(SoCDeviceKind::VirtioMmioNet),
                            soc_device_default(SoCDeviceKind::VirtioMmioFs),
                            soc_device_default(SoCDeviceKind::DmaController)}};
    }

    [[nodiscard]] static auto rvcomp() -> SoCConfig {
        return {.name = "rvcomp",
                .cpu_model = "rvcomp",
                .enable_pcie = false,
                .enable_mmio = false,
                .disable_unlisted_devices = true,
                .dram_base = std::nullopt,
                .dram_size = std::nullopt,
                .reset_pc = std::nullopt,
                .tohost = std::nullopt,
                .devices = {soc_device_default(SoCDeviceKind::ResetControl),
                            soc_device_default(SoCDeviceKind::RingBufferEthernet)}};
    }

    [[nodiscard]] static auto preset(std::string_view preset_name) -> std::optional<SoCConfig> {
        if (preset_name == "virt-pcie" || preset_name == "pcie") return virt_pcie();
        if (preset_name == "virt-mmio" || preset_name == "mmio") return virt_mmio();
        if (preset_name == "rvcomp") return rvcomp();
        return std::nullopt;
    }

    [[nodiscard]] auto find(SoCDeviceKind kind) const -> const SoCDeviceConfig* {
        const auto it = std::ranges::find_if(
            devices, [kind](const auto& device) { return device.enabled && device.kind == kind; });
        return it == devices.end() ? nullptr : &*it;
    }

    [[nodiscard]] auto validate() const -> std::expected<void, std::string> {
        if (auto smp_result = smp.validate(); !smp_result) return smp_result;
        struct Region {
            simrv::xlen::Address base;
            simrv::xlen::Address size;
            std::string_view name;
        };
        std::vector<Region> regions;
        if (enable_pcie) {
            constexpr simrv::xlen::Address kEcamBusBytes = 1U << 20U;
            const auto range_valid = [](simrv::xlen::Address base, simrv::xlen::Address size) {
                return base != 0 && size != 0 && base <= ~simrv::xlen::Address{0} - size;
            };
            if (!range_valid(pcie.ecam_base, pcie.ecam_size) ||
                !range_valid(pcie.mmio_base, pcie.mmio_size)) {
                return std::unexpected("PCIe host has an invalid ECAM or MMIO range");
            }
            if (pcie.first_bus > pcie.last_bus) {
                return std::unexpected("PCIe first bus must not exceed last bus");
            }
            const auto bus_count = static_cast<simrv::xlen::Address>(pcie.last_bus) -
                                   static_cast<simrv::xlen::Address>(pcie.first_bus) + 1;
            if (bus_count > ~simrv::xlen::Address{0} / kEcamBusBytes ||
                pcie.ecam_size < bus_count * kEcamBusBytes) {
                return std::unexpected("PCIe ECAM range is too small for its bus range");
            }
            regions.push_back(
                {.base = pcie.ecam_base, .size = pcie.ecam_size, .name = "pcie-ecam"});
            regions.push_back(
                {.base = pcie.mmio_base, .size = pcie.mmio_size, .name = "pcie-mmio"});
        }
        std::vector<std::string_view> names;
        for (size_t i = 0; i < devices.size(); ++i) {
            const auto& device = devices[i];
            if (!device.enabled) continue;
            if (device.name.empty() ||
                std::ranges::find(names, std::string_view(device.name)) != names.end()) {
                return std::unexpected("Enabled SoC devices must have unique, non-empty names");
            }
            names.push_back(device.name);
            if (device.size == 0 || device.base > ~simrv::xlen::Address{0} - device.size) {
                return std::unexpected("SoC device has an invalid address range at index " +
                                       std::to_string(i));
            }
            if ((device.kind == SoCDeviceKind::ResetControl && device.size != 4) ||
                (device.kind == SoCDeviceKind::RingBufferEthernet && device.size < 28)) {
                return std::unexpected("Platform extension has an unsupported MMIO window size");
            }
            if (device.kind == SoCDeviceKind::Uart &&
                (device.clock_hz == 0 || device.clock_hz > std::numeric_limits<uint32_t>::max())) {
                return std::unexpected("UART clock frequency must fit a nonzero FDT value");
            }
            regions.push_back({.base = device.base, .size = device.size, .name = device.name});
            if (device.kind == SoCDeviceKind::DummyMmio && device.compatible.empty()) {
                return std::unexpected("Dummy MMIO device requires a compatible string");
            }
            if (device.kind == SoCDeviceKind::DummyMmio && device.irq != 0) {
                return std::unexpected("Dummy MMIO devices do not generate interrupts");
            }
            if (device.kind == SoCDeviceKind::CustomMmio &&
                (device.compatible.empty() || device.irq != 0)) {
                return std::unexpected("Custom MMIO devices require a compatible string and irq=0");
            }
            if (device.kind == SoCDeviceKind::RingBufferEthernet) {
                const auto* rx = device.find_region("rxbuf");
                const auto* tx = device.find_region("txbuf");
                if (rx == nullptr || tx == nullptr || rx->size < 2048 || tx->size < 2048 ||
                    !std::has_single_bit(rx->size) || !std::has_single_bit(tx->size) ||
                    rx->size > 64 * 1024 * 1024 || tx->size > 64 * 1024 * 1024) {
                    return std::unexpected(
                        "Ring-buffer Ethernet requires power-of-two RX/TX regions");
                }
            }
            const bool pci_endpoint = device.kind == SoCDeviceKind::VirtioMmioBlock ||
                                      device.kind == SoCDeviceKind::VirtioMmioConsole ||
                                      device.kind == SoCDeviceKind::VirtioMmioRng ||
                                      device.kind == SoCDeviceKind::VirtioMmioGpu ||
                                      device.kind == SoCDeviceKind::VirtioMmioInput ||
                                      device.kind == SoCDeviceKind::VirtioMmioSound ||
                                      device.kind == SoCDeviceKind::VirtioMmioNet ||
                                      device.kind == SoCDeviceKind::VirtioMmioFs;
            if ((!pci_endpoint || !enable_pcie) && device.irq > kPlicSourceCount) {
                return std::unexpected("PLIC-routed device IRQ must be in the range 0.." +
                                       std::to_string(kPlicSourceCount));
            }
            for (const auto& extra : device.regions) {
                if (extra.size == 0 || extra.base > ~simrv::xlen::Address{0} - extra.size) {
                    return std::unexpected("SoC device has an invalid auxiliary region: '" +
                                           extra.name + "'");
                }
                regions.push_back({.base = extra.base, .size = extra.size, .name = extra.name});
            }
        }
        if (enable_pcie) {
            constexpr std::array pci_kinds = {
                SoCDeviceKind::VirtioMmioBlock, SoCDeviceKind::VirtioMmioConsole,
                SoCDeviceKind::VirtioMmioRng,   SoCDeviceKind::VirtioMmioGpu,
                SoCDeviceKind::VirtioMmioInput, SoCDeviceKind::VirtioMmioSound,
                SoCDeviceKind::VirtioMmioNet,   SoCDeviceKind::VirtioMmioFs};
            std::vector<std::array<uint8_t, 3>> bdfs;
            for (const auto kind : pci_kinds) {
                const auto configured = std::ranges::find_if(
                    devices, [kind](const auto& item) { return item.kind == kind; });
                const bool selected = configured == devices.end()
                                          ? !disable_unlisted_devices
                                          : configured->enabled || !disable_unlisted_devices;
                if (!selected) continue;
                auto endpoint = configured != devices.end() && configured->enabled
                                    ? *configured
                                    : soc_device_default(kind);
                const auto defaults = soc_device_default(kind);
                endpoint.pci_bus = endpoint.pci_bus.value_or(defaults.pci_bus.value_or(0));
                endpoint.pci_slot = endpoint.pci_slot.value_or(defaults.pci_slot.value_or(0));
                endpoint.pci_function =
                    endpoint.pci_function.value_or(defaults.pci_function.value_or(0));
                endpoint.pci_intx_source =
                    endpoint.pci_intx_source.value_or(defaults.pci_intx_source.value_or(0));
                if (*endpoint.pci_bus < pcie.first_bus || *endpoint.pci_bus > pcie.last_bus ||
                    *endpoint.pci_slot > 31 || *endpoint.pci_function > 7) {
                    return std::unexpected("PCI endpoint BDF is outside the configured topology");
                }
                if (*endpoint.pci_intx_source == 0 ||
                    *endpoint.pci_intx_source > kPlicSourceCount) {
                    return std::unexpected("PCI endpoint INTx source must be in the range 1.." +
                                           std::to_string(kPlicSourceCount));
                }
                const std::array<uint8_t, 3> bdf{*endpoint.pci_bus, *endpoint.pci_slot,
                                                 *endpoint.pci_function};
                if (std::ranges::find(bdfs, bdf) != bdfs.end()) {
                    return std::unexpected("PCI endpoint BDFs must be unique");
                }
                bdfs.push_back(bdf);
            }
        }
        if (dram_base.has_value() && dram_size.has_value()) {
            if (*dram_size == 0 || *dram_base > ~simrv::xlen::Address{0} - *dram_size) {
                return std::unexpected("SoC memory configuration has an invalid DRAM range");
            }
            regions.push_back({.base = *dram_base, .size = *dram_size, .name = "dram"});
        } else if (dram_size.has_value() && *dram_size == 0) {
            return std::unexpected("SoC DRAM size must be non-zero");
        }
        for (size_t i = 0; i < regions.size(); ++i) {
            for (size_t j = 0; j < i; ++j) {
                const auto& region = regions[i];
                const auto& other = regions[j];
                if (region.base < other.base + other.size &&
                    other.base < region.base + region.size) {
                    return std::unexpected("SoC memory/MMIO regions overlap: '" +
                                           std::string(region.name) + "' and '" +
                                           std::string(other.name) + "'");
                }
            }
        }
        return {};
    }
};

}  // namespace simrv::core
