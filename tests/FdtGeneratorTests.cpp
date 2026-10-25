/**
 * @file FdtGeneratorTests.cpp
 * @brief Unit tests for programmatic Flattened Device Tree generator.
 */
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/core/MachineConfig.hpp"
#include "simrv/util/FdtGenerator.hpp"

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
    cfg.platform_profile = simrv::core::PlatformProfile::Pcie;
    cfg.execution.num_harts = 4;
    cfg.memory.dram_base = 0x80000000ULL;
    cfg.memory.dram_size = 1024ULL * 1024ULL * 1024ULL;  // 1 GiB
    cfg.files.bootargs = "console=ttyS0 earlycon root=/dev/vda rw";

    const auto blob = simrv::util::generate_platform_fdt(cfg, nullptr);
    expect(blob.size() >= 40, "Generated PCIe FDT has non-zero length");
    expect(blob[0] == 0xd0 && blob[1] == 0x0d && blob[2] == 0xfe && blob[3] == 0xed,
           "Generated PCIe FDT has valid magic");

    expect(blob_contains_string(blob, "pci-host-ecam-generic"), "PCIe host controller advertised");
    expect(blob_contains_string(blob, "cfi-flash"), "Parallel NOR Flash advertised");
    expect(blob_contains_string(blob, "flash@20000000"), "PFlash node address present");
    expect(blob_contains_string(blob, "pci@30000000"), "PCIe ECAM node address present");
    expect(blob_contains_string(blob, "console=ttyS0 earlycon"),
           "Bootargs string present in /chosen");

    // Hart cpus
    expect(blob_contains_string(blob, "cpu@0"), "cpu@0 node present");
    expect(blob_contains_string(blob, "cpu@1"), "cpu@1 node present");
    expect(blob_contains_string(blob, "cpu@2"), "cpu@2 node present");
    expect(blob_contains_string(blob, "cpu@3"), "cpu@3 node present");
    expect(!blob_contains_string(blob, "cpu@4"), "cpu@4 node absent");
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

    // Hart cpus
    expect(blob_contains_string(blob, "cpu@0"), "cpu@0 node present");
    expect(blob_contains_string(blob, "cpu@1"), "cpu@1 node present");
    expect(!blob_contains_string(blob, "cpu@2"), "cpu@2 node absent");

    // PCIe and PFlash should not be present on pure MMIO profile
    expect(!blob_contains_string(blob, "pci-host-ecam-generic"), "PCIe absent on MMIO profile");
    expect(!blob_contains_string(blob, "cfi-flash"), "PFlash absent on MMIO profile");
}

}  // namespace

auto main() -> int {
    test_fdt_header_and_structure();
    test_generate_platform_fdt_pcie();
    test_generate_platform_fdt_mmio();

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All FDT generator unit tests passed.\n";
    return EXIT_SUCCESS;
}
