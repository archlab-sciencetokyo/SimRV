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

struct SoCDeviceConfig {
    SoCDeviceKind kind{};
    std::string name;
    Address base = 0;
    Address size = 0x1000;
    uint32_t irq = 0;
    bool enabled = true;
};

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
