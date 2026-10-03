/**
 * @file SoCConfig.hpp
 * @brief Declarative description of the simulated SoC platform.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/xlen/Types.hpp"

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
    DmaController,
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
        case SoCDeviceKind::DmaController:
            return "dma";
    }
    return "unknown";
}

struct SoCDeviceConfig {
    SoCDeviceKind kind{};
    std::string name;
    Address base = 0;
    Address size = 0x1000;
    uint32_t irq = 0;
    bool enabled = true;
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
    if (name == "dma" || name == "dma-controller") return SoCDeviceKind::DmaController;
    return std::nullopt;
}

[[nodiscard]] inline auto soc_device_default(SoCDeviceKind kind) -> SoCDeviceConfig {
    switch (kind) {
        case SoCDeviceKind::Uart:
            return {kind, "uart0", 0x10000000, 0x100, 10};
        case SoCDeviceKind::Rtc:
            return {kind, "rtc0", 0x70000000, 0x1000, 11};
        case SoCDeviceKind::Power:
            return {kind, "power0", 0x00100000, 0x1000, 0};
        case SoCDeviceKind::AclintMtimer:
            return {kind, "timer0", 0x60000000, 0x4000, 7};
        case SoCDeviceKind::AclintMswi:
            return {kind, "mswi0", 0x60000000, 0x4000, 3};
        case SoCDeviceKind::Imsic:
            return {kind, "imsic0", 0x24000000, 0x100000, 0};
        case SoCDeviceKind::Aplic:
            return {kind, "aplic0", 0x0c000000, 0x400000, 0};
        case SoCDeviceKind::Plic:
            return {kind, "plic0", 0x50000000, 0x4000000, 0};
        case SoCDeviceKind::Clint:
            return {kind, "clint0", 0x60000000, 0xc0000, 0};
        case SoCDeviceKind::VirtioMmioBlock:
            return {kind, "disk0", 0x10001000, 0x1000, 2};
        case SoCDeviceKind::VirtioMmioConsole:
            return {kind, "console0", 0x10002000, 0x1000, 1};
        case SoCDeviceKind::VirtioMmioRng:
            return {kind, "rng0", 0x10003000, 0x1000, 4};
        case SoCDeviceKind::VirtioMmioGpu:
            return {kind, "gpu0", 0x10004000, 0x1000, 5};
        case SoCDeviceKind::VirtioMmioInput:
            return {kind, "input0", 0x10005000, 0x1000, 6};
        case SoCDeviceKind::VirtioMmioSound:
            return {kind, "sound0", 0x10006000, 0x1000, 7};
        case SoCDeviceKind::VirtioMmioNet:
            return {kind, "net0", 0x10007000, 0x1000, 8};
        case SoCDeviceKind::DmaController:
            return {kind, "dma0", 0x10009000, 0x1000, 12};
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
    std::optional<Address> dram_base;
    std::optional<Address> dram_size;
    std::optional<Address> reset_pc;
    std::optional<Address> tohost;
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
                .devices = {}};
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
                .devices = {
                    {SoCDeviceKind::VirtioMmioBlock, "disk0", 0x10001000, 0x1000, 2},
                    {SoCDeviceKind::VirtioMmioConsole, "console0", 0x10002000, 0x1000, 1},
                    {SoCDeviceKind::VirtioMmioRng, "rng0", 0x10003000, 0x1000, 4},
                    {SoCDeviceKind::VirtioMmioGpu, "gpu0", 0x10004000, 0x1000, 5},
                    {SoCDeviceKind::VirtioMmioInput, "input0", 0x10005000, 0x1000, 6},
                    {SoCDeviceKind::VirtioMmioSound, "sound0", 0x10006000, 0x1000, 7},
                    {SoCDeviceKind::VirtioMmioNet, "net0", 0x10007000, 0x1000, 8},
                    {SoCDeviceKind::DmaController, "dma0", 0x10009000, 0x1000, 12},
                }};
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
                .devices = {}};
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
        for (size_t i = 0; i < devices.size(); ++i) {
            const auto& device = devices[i];
            if (!device.enabled) continue;
            if (device.size == 0 || device.base > ~Address{0} - device.size) {
                return std::unexpected("SoC device has an invalid address range at index " +
                                       std::to_string(i));
            }
            const Address end = device.base + device.size;
            for (size_t j = 0; j < i; ++j) {
                const auto& other = devices[j];
                if (!other.enabled) continue;
                const Address other_end = other.base + other.size;
                if (device.base < other_end && other.base < end) {
                    return std::unexpected("SoC MMIO device ranges overlap: '" + device.name +
                                           "' and '" + other.name + "'");
                }
            }
        }
        return {};
    }
};

}  // namespace simrv::core
