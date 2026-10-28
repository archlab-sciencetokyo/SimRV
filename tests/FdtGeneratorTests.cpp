/**
 * @file FdtGeneratorTests.cpp
 * @brief Unit tests for programmatic Flattened Device Tree generator.
 */
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/core/MachineConfig.hpp"
#include "simrv/util/FdtGenerator.hpp"
#include "simrv/xlen/ScalarTypes.hpp"
#include "simrv/util/FdtPatcher.hpp"

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

auto fdt_property_u32(const std::vector<uint8_t>& blob, std::string_view target)
    -> std::optional<uint32_t> {
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
            while (cursor < structure_end && blob[cursor++] != 0) {}
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
            const char* name = reinterpret_cast<const char*>(blob.data() + strings_offset + name_offset);
            const size_t available = strings_end - (strings_offset + name_offset);
            const auto* terminator = static_cast<const char*>(std::memchr(name, 0, available));
            if (terminator == nullptr) return std::nullopt;
            if (std::string_view(name, static_cast<size_t>(terminator - name)) == target &&
                length == 4) {
                return read_be32(cursor);
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
    expect(blob_contains_string(blob, "zihpm"), "Implemented HPM counter extension advertised");
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
    expect(blob_contains_string(blob, "zkt"), "Zkt data-independent latency advertised");
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
    expect(blob_contains_string(blob, "console=ttyS0 earlycon"),
           "Bootargs string present in /chosen");

    // Hart cpus
    expect(blob_contains_string(blob, "cpu@0"), "cpu@0 node present");
    expect(blob_contains_string(blob, "cpu@1"), "cpu@1 node present");
    expect(blob_contains_string(blob, "cpu@2"), "cpu@2 node present");
    expect(blob_contains_string(blob, "cpu@3"), "cpu@3 node present");
    expect(!blob_contains_string(blob, "cpu@4"), "cpu@4 node absent");
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
    constexpr std::array<std::string_view, 1> isa_extensions{"i"};
    fdt.property_stringlist("riscv,isa-extensions", isa_extensions);
    fdt.end_node();
    fdt.end_node();
    fdt.end_node();

    const auto original = fdt.serialize();
    const auto patched = simrv::util::patch_fdt(original, 0x80000000ULL, 0x10000000ULL, 1);
    expect(patched.has_value(), "minimal FDT patches successfully for Zihintntl check");
    if (!patched) return;
    expect(blob_contains_string(*patched, "zihintntl"),
           "patched CPU node advertises Zihintntl support");
    expect(blob_contains_string(*patched, "zic64b") &&
               blob_contains_string(*patched, "zicclsm") &&
               blob_contains_string(*patched, "za64rs") &&
               blob_contains_string(*patched, "ziccamoa") &&
               blob_contains_string(*patched, "zfhmin") &&
               blob_contains_string(*patched, "zvfhmin") &&
               blob_contains_string(*patched, "zfa") &&
               blob_contains_string(*patched, "zawrs") &&
               blob_contains_string(*patched, "zvbb") &&
               blob_contains_string(*patched, "zkt") &&
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
    test_patch_fdt_advertises_zihintntl_and_64_byte_cmo();
    test_generate_platform_fdt_mmio();

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All FDT generator unit tests passed.\n";
    return EXIT_SUCCESS;
}
