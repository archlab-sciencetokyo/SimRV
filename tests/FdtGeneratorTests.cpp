/**
 * @file FdtGeneratorTests.cpp
 * @brief Unit tests for programmatic Flattened Device Tree generator.
 */
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/core/MachineConfig.hpp"
#include "simrv/isa/IsaConfig.hpp"
#include "simrv/util/FdtGenerator.hpp"
#include "simrv/util/FdtPatcher.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace {

auto failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

auto blob_contains_string(const std::vector<uint8_t>& blob, std::string_view target) -> bool {
    const std::string_view view(reinterpret_cast<const char*>(blob.data()), blob.size());
    return view.find(target) != std::string_view::npos;
}

auto blob_contains_be64_pair(const std::vector<uint8_t>& blob, uint64_t first, uint64_t second)
    -> bool {
    std::array<uint8_t, 16> encoded{};
    for (unsigned i = 0; i < 8; ++i) {
        encoded[i] = static_cast<uint8_t>(first >> ((7U - i) * 8U));
        encoded[8U + i] = static_cast<uint8_t>(second >> ((7U - i) * 8U));
    }
    return std::search(blob.begin(), blob.end(), encoded.begin(), encoded.end()) != blob.end();
}

auto blob_contains_be32_sequence(const std::vector<uint8_t>& blob, std::span<const uint32_t> values)
    -> bool {
    std::vector<uint8_t> encoded;
    encoded.reserve(values.size() * sizeof(uint32_t));
    for (const auto value : values) {
        encoded.push_back(static_cast<uint8_t>(value >> 24U));
        encoded.push_back(static_cast<uint8_t>(value >> 16U));
        encoded.push_back(static_cast<uint8_t>(value >> 8U));
        encoded.push_back(static_cast<uint8_t>(value));
    }
    return std::search(blob.begin(), blob.end(), encoded.begin(), encoded.end()) != blob.end();
}

auto fdt_property_u32(const std::vector<uint8_t>& blob, std::string_view target,
                      size_t occurrence = 0) -> std::optional<uint32_t> {
    const auto read_be32 = [&blob](size_t offset) {
        return (static_cast<uint32_t>(blob[offset]) << 24U) |
               (static_cast<uint32_t>(blob[offset + 1]) << 16U) |
               (static_cast<uint32_t>(blob[offset + 2]) << 8U) | blob[offset + 3];
    };
    if (blob.size() < 40) return std::nullopt;
    const size_t structure_offset = read_be32(8);
    const size_t structure_end = structure_offset + read_be32(36);
    const size_t strings_offset = read_be32(12);
    const size_t strings_end = strings_offset + read_be32(32);
    if (structure_end > blob.size() || strings_end > blob.size()) return std::nullopt;
    size_t cursor = structure_offset;
    while (cursor + 4 <= structure_end) {
        const uint32_t token = read_be32(cursor);
        cursor += 4;
        if (token == 1) {  // FDT_BEGIN_NODE
            while (cursor < structure_end && blob[cursor++] != 0) {
            }
            cursor = (cursor + 3U) & ~size_t{3};
        } else if (token == 2 || token == 9) {  // FDT_END_NODE / FDT_END
            if (token == 9) break;
        } else if (token == 3) {  // FDT_PROP
            if (cursor + 8 > structure_end) return std::nullopt;
            const size_t length = read_be32(cursor);
            const size_t name_offset = read_be32(cursor + 4);
            cursor += 8;
            if (strings_offset + name_offset >= strings_end || cursor + length > structure_end)
                return std::nullopt;
            const char* name =
                reinterpret_cast<const char*>(blob.data() + strings_offset + name_offset);
            const size_t available = strings_end - (strings_offset + name_offset);
            const auto* terminator = static_cast<const char*>(std::memchr(name, 0, available));
            if (terminator == nullptr) return std::nullopt;
            if (std::string_view(name, static_cast<size_t>(terminator - name)) == target &&
                length == 4) {
                if (occurrence == 0) return read_be32(cursor);
                --occurrence;
            }
            cursor = (cursor + length + 3U) & ~size_t{3};
        } else if (token != 4) {  // FDT_NOP
            return std::nullopt;
        }
    }
    return std::nullopt;
}

void test_fdt_header_and_structure() {
    simrv::util::FdtGenerator fdt;
    fdt.begin_node("");
    fdt.property_u32("#address-cells", 2);
    fdt.property_u32("#size-cells", 2);
    fdt.property_string("compatible", "simrv,test-platform");
    fdt.end_node();

    const auto blob = fdt.serialize();
    expect(blob.size() >= 40, "FDT serialized blob is at least header size");

    // Devicetree magic 0xd00dfeed (big-endian)
    expect(blob[0] == 0xd0 && blob[1] == 0x0d && blob[2] == 0xfe && blob[3] == 0xed,
           "FDT magic header 0xd00dfeed is valid");

    // Version (offset 20): version 17
    const uint32_t version =
        (static_cast<uint32_t>(blob[20]) << 24) | (static_cast<uint32_t>(blob[21]) << 16) |
        (static_cast<uint32_t>(blob[22]) << 8) | static_cast<uint32_t>(blob[23]);
    expect(version == 17, "FDT version is 17");

    expect(blob_contains_string(blob, "simrv,test-platform"),
           "FDT blob contains serialized string property");
}

void test_generate_platform_fdt_pcie() {
    simrv::core::MachineConfig cfg{};
    cfg.soc = simrv::core::SoCConfig::virt_pcie();
    cfg.soc.devices.push_back(simrv::core::soc_device_default(simrv::core::SoCDeviceKind::Uart));
    cfg.platform_profile = simrv::core::PlatformProfile::Pcie;
    cfg.execution.num_harts = 4;
    cfg.memory.dram_base = 0x80000000ULL;
    cfg.memory.dram_size = 1024ULL * 1024ULL * 1024ULL;  // 1 GiB
    cfg.files.bootargs = "console=ttyS0 earlycon root=/dev/vda rw";
    if constexpr (simrv::xlen::kIsXLen64) {
        cfg.isa.isa_preset |= simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::H);
    }

    const auto blob = simrv::util::generate_platform_fdt(cfg, nullptr);
    expect(blob.size() >= 40, "Generated PCIe FDT has non-zero length");
    expect(blob[0] == 0xd0 && blob[1] == 0x0d && blob[2] == 0xfe && blob[3] == 0xed,
           "Generated PCIe FDT has valid magic");

    expect(blob_contains_string(blob, "pci-host-ecam-generic"), "PCIe host controller advertised");
    expect(fdt_property_u32(blob, "clock-frequency", 4) == 3'686'400,
           "UART clock metadata follows the four hart clock properties");
    expect(blob_contains_string(blob, "cfi-flash"), "Parallel NOR Flash advertised");
    expect(blob_contains_string(blob, "flash@20000000"), "PFlash node address present");
    expect(blob_contains_string(blob, "pci@30000000"), "PCIe ECAM node address present");
    expect(blob_contains_string(blob, "zihpm"), "Implemented HPM counter extension advertised");
    expect(blob_contains_string(blob, "zicntr"), "Base counters and timers advertised");
    expect(blob_contains_string(blob, "sstc"), "Supervisor timer extension advertised");
    expect(blob_contains_string(blob, std::string_view{"\0h\0", 3}) == simrv::xlen::kIsXLen64,
           "Hypervisor extension advertisement follows the hart ISA configuration");
    expect(blob_contains_string(blob, "zihintntl"), "Zihintntl extension advertised");
    expect(blob_contains_string(blob, "zic64b"), "Zic64b extension advertised");
    expect(blob_contains_string(blob, "zicclsm"), "Zicclsm extension advertised");
    expect(blob_contains_string(blob, "za64rs"), "Za64rs extension advertised");
    expect(blob_contains_string(blob, "ziccamoa"), "Ziccamoa extension advertised");
    expect(blob_contains_string(blob, "zfhmin") && blob_contains_string(blob, "zvfhmin"),
           "minimal scalar and vector half-precision extensions advertised");
    expect(blob_contains_string(blob, "zfa"), "Zfa scalar floating-point extensions advertised");
    expect(blob_contains_string(blob, "zawrs"), "Zawrs wait-on-reservation-set advertised");
    expect(blob_contains_string(blob, "zvbb"), "Zvbb vector bit-manipulation advertised");
    expect(blob_contains_string(blob, "zvkb"), "Zvkb vector bit-manipulation advertised");
    expect(blob_contains_string(blob, "zvkt"), "Zvkt vector data-independent latency advertised");
    for (const auto extension : {"zmmul", "zimop", "zaamo", "zalrsc", "za128rs", "zba", "zbb",
                                 "zbs", "zca", "zcb", "zcd", "zcmop"}) {
        expect(blob_contains_string(blob, extension),
               std::string(extension) + " mandatory RVA23 extension advertised");
    }
    expect(blob_contains_string(blob, "zkt"), "Zkt data-independent latency advertised");
    expect(blob_contains_string(blob, "svade"), "Svade page-fault behavior advertised");
    expect(blob_contains_string(blob, "ssccptr"), "Ssccptr page-table reads advertised");
    expect(blob_contains_string(blob, "sscounterenw"),
           "Sscounterenw HPM counter enables advertised");
    expect(blob_contains_string(blob, "sstvala"), "Sstvala supervisor trap values advertised");
    expect(blob_contains_string(blob, "sstvecd"), "Sstvecd direct trap vectors advertised");
    for (const auto extension : {"ssstateen", "shcounterenw", "shvstvala", "shtvala", "shvstvecd",
                                 "shvsatpa", "shgatpa"}) {
        expect(blob_contains_string(blob, extension) == simrv::xlen::kIsXLen64,
               std::string(extension) + " Sha component advertisement follows H support");
    }
    expect(blob_contains_string(blob, "sscofpmf"),
           "Sscofpmf overflow interrupt support advertised");
    expect(blob_contains_string(blob, "ziccrse"), "Ziccrse main-memory support advertised");
    expect(blob_contains_string(blob, "ziccif"), "Ziccif atomic fetch support advertised");
    expect(blob_contains_string(blob, "ssnpm") == simrv::xlen::kIsXLen64,
           "Ssnpm pointer masking is advertised only by the RV64 guest");
    expect(blob_contains_string(blob, "supm") == simrv::xlen::kIsXLen64,
           "Supm user pointer masking is advertised only by the RV64 guest");
    expect(blob_contains_string(blob, "ssu64xl") == simrv::xlen::kIsXLen64,
           "Ssu64xl is advertised only by the RV64 guest");
    expect(blob_contains_string(blob, "riscv,pmu"), "RISC-V PMU node present");
    expect(blob_contains_string(blob, "riscv,event-to-mhpmevent"),
           "standard branch-miss mapping present");
    expect(blob_contains_string(blob, "riscv,raw-event-to-mhpmcounters"),
           "SimRV raw HPM selector mappings present");
    expect(fdt_property_u32(blob, "riscv,cbom-block-size") == 64,
           "Zicbom block size is advertised as 64 bytes");
    expect(fdt_property_u32(blob, "riscv,cbop-block-size") == 64,
           "Zicbop block size is advertised as 64 bytes");
    expect(fdt_property_u32(blob, "riscv,cboz-block-size") == 64,
           "Zicboz block size is advertised as 64 bytes");
    expect(fdt_property_u32(blob, "current-speed") == 115'200, "UART reset speed is advertised");
    expect(blob_contains_string(blob, "console=ttyS0 earlycon"),
           "Bootargs string present in /chosen");

    // Hart cpus
    expect(blob_contains_string(blob, "cpu@0"), "cpu@0 node present");
    expect(blob_contains_string(blob, "cpu@1"), "cpu@1 node present");
    expect(blob_contains_string(blob, "cpu@2"), "cpu@2 node present");
    expect(blob_contains_string(blob, "cpu@3"), "cpu@3 node present");
    expect(!blob_contains_string(blob, "cpu@4"), "cpu@4 node absent");
}

void test_uart_clock_capability() {
    simrv::core::MachineConfig cfg{};
    cfg.soc = simrv::core::SoCConfig::virt_pcie();
    cfg.soc.devices.push_back(simrv::core::soc_device_default(simrv::core::SoCDeviceKind::Uart));
    auto uart = std::ranges::find_if(
        cfg.soc.devices, [](const auto& d) { return d.kind == simrv::core::SoCDeviceKind::Uart; });
    expect(uart != cfg.soc.devices.end(), "virt-pcie UART descriptor exists");
    if (uart == cfg.soc.devices.end()) return;
    uart->clock_hz = 1'843'200;
    const auto blob = simrv::util::generate_platform_fdt(cfg, nullptr);
    expect(fdt_property_u32(blob, "clock-frequency", 1) == 1'843'200,
           "FDT reports the configured UART input clock");
    expect(fdt_property_u32(blob, "current-speed") == 115'200,
           "FDT reports the UART speed from its reset divisor");
}

void test_generate_platform_fdt_custom_pcie_topology() {
    simrv::core::MachineConfig cfg{};
    cfg.soc = simrv::core::SoCConfig::virt_pcie();
    cfg.platform_profile = simrv::core::PlatformProfile::Pcie;
    cfg.soc.disable_unlisted_devices = true;
    cfg.soc.pcie.ecam_base = 0x34000000;
    cfg.soc.pcie.ecam_size = 1U << 20U;
    cfg.soc.pcie.first_bus = 4;
    cfg.soc.pcie.last_bus = 4;
    cfg.soc.pcie.mmio_base = 0x44000000;
    cfg.soc.pcie.mmio_size = 16U << 20U;
    auto block = simrv::core::soc_device_default(simrv::core::SoCDeviceKind::VirtioMmioBlock);
    block.pci_bus = 4;
    block.pci_slot = 9;
    block.pci_function = 2;
    block.pci_intx_source = 30;
    cfg.soc.devices = {simrv::core::soc_device_default(simrv::core::SoCDeviceKind::Plic), block};

    const auto blob = simrv::util::generate_platform_fdt(cfg, nullptr);
    expect(blob_contains_string(blob, "pci@34000000"),
           "PCIe host node name follows configured ECAM base");
    expect(blob_contains_be64_pair(blob, 0x34000000, 1U << 20U),
           "PCIe host reg follows configured ECAM range");
    const std::array<uint32_t, 2> bus_range{4, 4};
    expect(blob_contains_be32_sequence(blob, bus_range),
           "PCIe bus range follows configured first and last bus");
    const std::array<uint32_t, 7> mmio_range{0x02000000U, 0, 0x44000000U, 0,
                                             0x44000000U, 0, 16U << 20U};
    expect(blob_contains_be32_sequence(blob, mmio_range),
           "PCIe MMIO aperture is emitted with identity translation");
    const std::array<uint32_t, 6> intx_route{0x00044a00U, 0, 0, 1, 1, 30};
    expect(blob_contains_be32_sequence(blob, intx_route),
           "PCIe interrupt map follows endpoint BDF and configured INTx source");
}

void test_patch_fdt_advertises_zihintntl_and_64_byte_cmo() {
    simrv::util::FdtGenerator fdt;
    fdt.begin_node("");
    fdt.property_u32("#address-cells", 2);
    fdt.property_u32("#size-cells", 2);
    fdt.begin_node("memory@80000000");
    fdt.property_string("device_type", "memory");
    fdt.property_array_u32("reg", {0, 0x80000000U, 0, 0x10000000U});
    fdt.end_node();
    fdt.begin_node("cpus");
    fdt.property_u32("#address-cells", 1);
    fdt.property_u32("#size-cells", 0);
    fdt.begin_node("cpu@0");
    fdt.property_u32("reg", 0);
    constexpr std::array<std::string_view, 9> isa_extensions{"i", "m", "a", "f", "d",
                                                             "c", "b", "v", "h"};
    fdt.property_stringlist("riscv,isa-extensions", isa_extensions);
    fdt.end_node();
    fdt.end_node();
    fdt.end_node();

    const auto original = fdt.serialize();
    const auto incompatible_isa = simrv::util::validate_fdt_isa(
        original, simrv::isa::isa_preset_bits(simrv::isa::IsaPreset::GC));
    expect(!incompatible_isa && incompatible_isa.error().find("extension 'b'") != std::string::npos,
           "FDT ISA validation rejects base extensions disabled by the selected ISA");
    const auto patched = simrv::util::patch_fdt(original, 0x80000000ULL, 0x10000000ULL, 1);
    expect(patched.has_value(), "minimal FDT patches successfully for Zihintntl check");
    if (!patched) return;
    expect(blob_contains_string(*patched, "zihintntl"),
           "patched CPU node advertises Zihintntl support");
    expect(
        blob_contains_string(*patched, "zic64b") && blob_contains_string(*patched, "zicclsm") &&
            blob_contains_string(*patched, "za64rs") &&
            blob_contains_string(*patched, "ziccamoa") &&
            blob_contains_string(*patched, "zfhmin") && blob_contains_string(*patched, "zvfhmin") &&
            blob_contains_string(*patched, "zfa") && blob_contains_string(*patched, "zawrs") &&
            blob_contains_string(*patched, "zvbb") && blob_contains_string(*patched, "zvkb") &&
            blob_contains_string(*patched, "zvkt") && blob_contains_string(*patched, "zmmul") &&
            blob_contains_string(*patched, "zimop") && blob_contains_string(*patched, "zaamo") &&
            blob_contains_string(*patched, "zalrsc") && blob_contains_string(*patched, "za128rs") &&
            blob_contains_string(*patched, "zba") && blob_contains_string(*patched, "zbb") &&
            blob_contains_string(*patched, "zbs") && blob_contains_string(*patched, "zca") &&
            blob_contains_string(*patched, "zcb") && blob_contains_string(*patched, "zcd") &&
            blob_contains_string(*patched, "zcmop") &&
            blob_contains_string(*patched, "ssstateen") &&
            blob_contains_string(*patched, "shcounterenw") &&
            blob_contains_string(*patched, "shvstvala") &&
            blob_contains_string(*patched, "shtvala") &&
            blob_contains_string(*patched, "shvstvecd") &&
            blob_contains_string(*patched, "shvsatpa") &&
            blob_contains_string(*patched, "shgatpa") && blob_contains_string(*patched, "zkt") &&
            blob_contains_string(*patched, "sscofpmf") &&
            blob_contains_string(*patched, "ziccrse") && blob_contains_string(*patched, "ziccif") &&
            blob_contains_string(*patched, "ssnpm") == simrv::xlen::kIsXLen64 &&
            blob_contains_string(*patched, "supm") == simrv::xlen::kIsXLen64 &&
            blob_contains_string(*patched, "ssu64xl") == simrv::xlen::kIsXLen64,
        "patched CPU node advertises supported RVA23 memory extensions");
    expect(fdt_property_u32(*patched, "riscv,cbom-block-size") == 64,
           "patched CPU node reports 64-byte Zicbom blocks");
    expect(fdt_property_u32(*patched, "riscv,cbop-block-size") == 64,
           "patched CPU node reports 64-byte Zicbop blocks");
    expect(fdt_property_u32(*patched, "riscv,cboz-block-size") == 64,
           "patched CPU node reports 64-byte Zicboz blocks");
}

void test_generate_platform_fdt_mmio() {
    simrv::core::MachineConfig cfg{};
    cfg.platform_profile = simrv::core::PlatformProfile::Mmio;
    cfg.execution.num_harts = 2;
    cfg.memory.dram_base = 0x80000000ULL;
    cfg.memory.dram_size = 256ULL * 1024ULL * 1024ULL;  // 256 MiB

    const auto blob = simrv::util::generate_platform_fdt(cfg, nullptr);
    expect(blob.size() >= 40, "Generated MMIO FDT has non-zero length");
    expect(blob[0] == 0xd0 && blob[1] == 0x0d && blob[2] == 0xfe && blob[3] == 0xed,
           "Generated MMIO FDT has valid magic");

    expect(blob_contains_string(blob, "virtio,mmio"), "VirtIO MMIO slots present");
    expect(blob_contains_string(blob, "ns16550a"), "UART present");
    expect(blob_contains_string(blob, "google,goldfish-rtc"), "RTC present");
    expect(blob_contains_string(blob, "sifive,clint0"), "CLINT present");
    expect(blob_contains_string(blob, "sifive,plic-1.0.0"), "PLIC present");
    expect(fdt_property_u32(blob, "riscv,ndev") == simrv::core::kPlicSourceCount,
           "PLIC FDT reports the implemented source count");

    // Hart cpus
    expect(blob_contains_string(blob, "cpu@0"), "cpu@0 node present");
    expect(blob_contains_string(blob, "cpu@1"), "cpu@1 node present");
    expect(!blob_contains_string(blob, "cpu@2"), "cpu@2 node absent");

    // PCIe and PFlash should not be present on pure MMIO profile
    expect(!blob_contains_string(blob, "pci-host-ecam-generic"), "PCIe absent on MMIO profile");
    expect(!blob_contains_string(blob, "cfi-flash"), "PFlash absent on MMIO profile");
}

void test_generated_fdt_uses_explicit_soc_devices() {
    simrv::core::MachineConfig cfg{};
    cfg.platform_profile = simrv::core::PlatformProfile::Mmio;
    cfg.soc.name = "custom-mmio";
    cfg.soc.enable_pcie = false;
    cfg.soc.enable_mmio = true;
    cfg.soc.disable_unlisted_devices = true;
    cfg.soc.devices = {
        {simrv::core::SoCDeviceKind::Uart, "serial", 0x12345000, 0x2000, 3},
        simrv::core::soc_device_default(simrv::core::SoCDeviceKind::Plic),
        {simrv::core::SoCDeviceKind::VirtioMmioBlock, "storage", 0x12348000, 0x4000, 5},
        {simrv::core::SoCDeviceKind::CustomMmio,
         "watchdog",
         0x1234a000,
         0x1000,
         0,
         true,
         {},
         "acme,watchdog"},
    };

    const auto blob = simrv::util::generate_platform_fdt(cfg, nullptr);
    expect(blob_contains_string(blob, "uart@12345000"), "configured UART address is used");
    expect(blob_contains_string(blob, "virtio@12348000"), "configured VirtIO-MMIO address is used");
    expect(blob_contains_be64_pair(blob, 0x12345000, 0x2000),
           "configured UART base and size are emitted");
    expect(fdt_property_u32(blob, "interrupts") == 3, "configured UART interrupt is emitted");
    expect(blob_contains_string(blob, "watchdog@1234a000") &&
               blob_contains_string(blob, "acme,watchdog"),
           "custom compatible device is emitted from its descriptor");
    expect(!blob_contains_string(blob, "google,goldfish-rtc"),
           "unlisted RTC is omitted by explicit device policy");
    expect(!blob_contains_string(blob, "virtio@10002000"),
           "unlisted VirtIO-MMIO devices are omitted");
    expect(blob_contains_string(blob, "/soc/uart@12345000:115200"),
           "chosen stdout path follows the configured UART address");

    cfg.platform_profile = simrv::core::PlatformProfile::Pcie;
    cfg.soc.devices = {
        simrv::core::soc_device_default(simrv::core::SoCDeviceKind::PFlash),
    };
    const auto no_plic_blob = simrv::util::generate_platform_fdt(cfg, nullptr);
    expect(blob_contains_string(no_plic_blob, "pci-host-ecam-generic"),
           "PCIe remains available without a PLIC");
    expect(!blob_contains_string(no_plic_blob, "interrupt-map"),
           "PCIe omits interrupt routing when its PLIC is absent");
}

void test_explicit_gc_fdt_does_not_advertise_rva23_extensions() {
    simrv::core::MachineConfig cfg{};
    cfg.isa.misa_override = true;
    cfg.isa.isa_preset = simrv::isa::isa_preset_bits(simrv::isa::IsaPreset::GC);

    const auto blob = simrv::util::generate_platform_fdt(cfg, nullptr);
    const auto compatible_isa = simrv::util::validate_fdt_isa(blob, cfg.isa.isa_preset);
    expect(compatible_isa.has_value(), "explicit RV64GC FDT matches its selected ISA");
    const std::string_view view(reinterpret_cast<const char*>(blob.data()), blob.size());
    expect(view.find(std::string_view("\0v\0", 3)) == std::string_view::npos,
           "explicit RV64GC FDT does not advertise vector support");
    expect(view.find("zicond") == std::string_view::npos,
           "explicit RV64GC FDT does not advertise RVA23 optional extensions");
    expect(blob_contains_string(blob, "zicntr") && blob_contains_string(blob, "zicsr") &&
               blob_contains_string(blob, "zifencei"),
           "explicit RV64GC FDT retains its baseline unprivileged extensions");
}

}  // namespace

auto main() -> int {
    test_fdt_header_and_structure();
    test_generate_platform_fdt_pcie();
    test_uart_clock_capability();
    test_generate_platform_fdt_custom_pcie_topology();
    test_patch_fdt_advertises_zihintntl_and_64_byte_cmo();
    test_generate_platform_fdt_mmio();
    test_generated_fdt_uses_explicit_soc_devices();
    test_explicit_gc_fdt_does_not_advertise_rva23_extensions();

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All FDT generator unit tests passed.\n";
    return EXIT_SUCCESS;
}
