/**
 * @file FdtGenerator.cpp
 * @brief Implementation of programmatic in-memory Flattened Device Tree generator.
 */
#include "simrv/util/FdtGenerator.hpp"

#include <cstring>
#include <format>
#include <iterator>
#include <span>

#include "simrv/core/PlatformMap.h"
#include "simrv/core/HpmEventCatalog.hpp"
#include "simrv/core/PlatformTiming.hpp"
#include "simrv/core/SoCDeviceRegistry.hpp"
#include "simrv/memory/Mmio.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::util {

namespace {

constexpr FdtMagic kFdtMagic = 0xd00dfeedU;
constexpr FdtToken kFdtVersion = 17U;
constexpr FdtToken kFdtLastCompVersion = 16U;

constexpr FdtToken kFdtBeginNode = 0x00000001U;
constexpr FdtToken kFdtEndNode = 0x00000002U;
constexpr FdtToken kFdtProp = 0x00000003U;
constexpr FdtToken kFdtEnd = 0x00000009U;

}  // namespace

FdtGenerator::FdtGenerator() = default;

void FdtGenerator::put_be32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 24U) & 0xFFU));
    buf.push_back(static_cast<uint8_t>((val >> 16U) & 0xFFU));
    buf.push_back(static_cast<uint8_t>((val >> 8U) & 0xFFU));
    buf.push_back(static_cast<uint8_t>(val & 0xFFU));
}

void FdtGenerator::put_be64(std::vector<uint8_t>& buf, uint64_t val) {
    put_be32(buf, static_cast<uint32_t>((val >> 32U) & 0xFFFFFFFFU));
    put_be32(buf, static_cast<uint32_t>(val & 0xFFFFFFFFU));
}

void FdtGenerator::pad4(std::vector<uint8_t>& buf) {
    while ((buf.size() & 3U) != 0) {
        buf.push_back(0);
    }
}

auto FdtGenerator::get_string_offset(std::string_view name) -> FdtOffset {
    std::string key(name);
    auto it = string_offsets_.find(key);
    if (it != string_offsets_.end()) {
        return it->second;
    }
    const auto offset = static_cast<FdtOffset>(string_data_.size());
    string_data_.insert(string_data_.end(), name.begin(), name.end());
    string_data_.push_back('\0');
    string_offsets_[std::move(key)] = offset;
    return offset;
}

void FdtGenerator::begin_node(std::string_view name) {
    put_be32(struct_data_, kFdtBeginNode);
    struct_data_.insert(struct_data_.end(), name.begin(), name.end());
    struct_data_.push_back('\0');
    pad4(struct_data_);
}

void FdtGenerator::end_node() { put_be32(struct_data_, kFdtEndNode); }

void FdtGenerator::property_raw(std::string_view name, std::span<const uint8_t> data) {
    const FdtOffset nameoff = get_string_offset(name);
    put_be32(struct_data_, kFdtProp);
    put_be32(struct_data_, static_cast<uint32_t>(data.size()));
    put_be32(struct_data_, nameoff);
    if (!data.empty()) {
        struct_data_.insert(struct_data_.end(), data.begin(), data.end());
        pad4(struct_data_);
    }
}

void FdtGenerator::property_u32(std::string_view name, FdtCell value) {
    std::vector<uint8_t> data;
    put_be32(data, value);
    property_raw(name, data);
}

void FdtGenerator::property_u64(std::string_view name, uint64_t value) {
    std::vector<uint8_t> data;
    put_be64(data, value);
    property_raw(name, data);
}

void FdtGenerator::property_string(std::string_view name, std::string_view value) {
    std::vector<uint8_t> data(value.begin(), value.end());
    data.push_back('\0');
    property_raw(name, data);
}

void FdtGenerator::property_stringlist(std::string_view name,
                                       std::span<const std::string_view> strings) {
    std::vector<uint8_t> data;
    for (const auto s : strings) {
        data.insert(data.end(), s.begin(), s.end());
        data.push_back('\0');
    }
    property_raw(name, data);
}

void FdtGenerator::property_array_u32(std::string_view name, std::span<const FdtCell> values) {
    std::vector<uint8_t> data;
    data.reserve(values.size() * sizeof(FdtCell));
    for (const auto v : values) {
        put_be32(data, v);
    }
    property_raw(name, data);
}

void FdtGenerator::property_array_u64(std::string_view name, std::span<const uint64_t> values) {
    std::vector<uint8_t> data;
    data.reserve(values.size() * sizeof(uint64_t));
    for (const auto v : values) {
        put_be64(data, v);
    }
    property_raw(name, data);
}

void FdtGenerator::property_empty(std::string_view name) { property_raw(name, {}); }

auto FdtGenerator::serialize() const -> std::vector<uint8_t> {
    const uint32_t header_size = 40U;
    const uint32_t mem_rsv_size = 16U;  // Empty memory reservation block
    const uint32_t off_mem_rsvmap = header_size;
    const uint32_t off_dt_struct = off_mem_rsvmap + mem_rsv_size;
    const uint32_t size_dt_struct = static_cast<uint32_t>(struct_data_.size() + 4U);  // + FDT_END
    const uint32_t off_dt_strings = off_dt_struct + size_dt_struct;
    const uint32_t size_dt_strings = static_cast<uint32_t>(string_data_.size());
    const uint32_t totalsize = off_dt_strings + size_dt_strings;

    std::vector<uint8_t> dtb;
    dtb.reserve(totalsize);

    // Header (40 bytes)
    put_be32(dtb, kFdtMagic);
    put_be32(dtb, totalsize);
    put_be32(dtb, off_dt_struct);
    put_be32(dtb, off_dt_strings);
    put_be32(dtb, off_mem_rsvmap);
    put_be32(dtb, kFdtVersion);
    put_be32(dtb, kFdtLastCompVersion);
    put_be32(dtb, 0U);  // boot_cpuid_phys
    put_be32(dtb, size_dt_strings);
    put_be32(dtb, size_dt_struct);

    // Empty memory reservation block (16 bytes: uint64 0, uint64 0)
    put_be64(dtb, 0ULL);
    put_be64(dtb, 0ULL);

    // Structure block + FDT_END
    dtb.insert(dtb.end(), struct_data_.begin(), struct_data_.end());
    put_be32(dtb, kFdtEnd);

    // Strings block
    dtb.insert(dtb.end(), string_data_.begin(), string_data_.end());

    return dtb;
}

auto generate_platform_fdt(const core::MachineConfig& config,
                           [[maybe_unused]] const device::PlatformDeviceRegistry* registry)
    -> std::vector<uint8_t> {
    FdtGenerator fdt;

    const bool is_pcie = (config.platform_profile == core::PlatformProfile::Pcie);
    const uint32_t num_harts = std::max(1U, static_cast<uint32_t>(config.execution.num_harts));
    const simrv::xlen::Address dram_base = config.memory.dram_base;
    const simrv::xlen::Address dram_size = config.memory.dram_size;
    const auto timebase =
        static_cast<uint32_t>(config.execution.timebase_hz != 0 ? config.execution.timebase_hz
                                                                : core::timing::kTimebaseHz);
    const auto clock_hz =
        static_cast<uint32_t>(config.execution.cpu_clock_hz != 0 ? config.execution.cpu_clock_hz
                                                                 : core::timing::kCpuClockHz);

    // Root node
    fdt.begin_node("");
    fdt.property_string("compatible", "simrv,virt");
    fdt.property_string("model", "SimRV RISC-V Virtual Platform");
    fdt.property_u32("#address-cells", 2);
    fdt.property_u32("#size-cells", 2);

    // /chosen
    fdt.begin_node("chosen");
    const std::string bootargs =
        !config.files.bootargs.empty()
            ? config.files.bootargs
            : "console=ttyS0 earlycon=uart8250,mmio,0x10000000 root=/dev/vda rw";
    fdt.property_string("bootargs", bootargs);
    fdt.property_string("stdout-path", "/soc/uart@10000000:115200");
    fdt.end_node();

    // /cpus
    fdt.begin_node("cpus");
    fdt.property_u32("#address-cells", 1);
    fdt.property_u32("#size-cells", 0);
    fdt.property_u32("timebase-frequency", timebase);

    // This is the set SimRV currently implements and exposes to the guest, not the full set of
    // extensions required by the RVA23 profiles. Keep the compliance gap explicit in
    // docs/architecture/compliance.md until the remaining mandatory extensions are implemented.
    static constexpr std::string_view kSupportedGuestExts[] = {
        "i",      "m",      "a",       "f",        "d",           "c",      "b",
        "v",      "zvbb",   "zvkt",   "zicntr", "zihpm",    "zicsr",      "zifencei", "zihintpause",
        "zihintntl", "zkt",
        "zic64b", "zicclsm", "za64rs", "ziccamoa", "zfhmin", "zvfhmin", "zfa", "zicond", "zicboz",
        "zicbom", "zicbop", "zawrs",   "svinval", "svpbmt",     "svnapot", "sstc",
        "sscofpmf", "ziccrse", "ziccif",
    };
    std::vector<std::string_view> guest_extensions(std::begin(kSupportedGuestExts),
                                                   std::end(kSupportedGuestExts));
    if constexpr (simrv::xlen::kIsXLen64) {
        guest_extensions.emplace_back("ssu64xl");
        guest_extensions.emplace_back("ssnpm");
        guest_extensions.emplace_back("supm");
    }

    for (uint32_t hart = 0; hart < num_harts; ++hart) {
        fdt.begin_node(std::format("cpu@{}", hart));
        fdt.property_string("device_type", "cpu");
        fdt.property_string("compatible", "riscv");
        fdt.property_u32("reg", hart);
        fdt.property_string("status", "okay");
        fdt.property_u32("clock-frequency", clock_hz);
        fdt.property_string("riscv,isa-base", simrv::xlen::kIsXLen64 ? "rv64i" : "rv32i");
        fdt.property_stringlist("riscv,isa-extensions", guest_extensions);
        fdt.property_u32("riscv,cbom-block-size", 64);
        fdt.property_u32("riscv,cbop-block-size", 64);
        fdt.property_u32("riscv,cboz-block-size", 64);
        fdt.property_string("mmu-type", simrv::xlen::kIsXLen64 ? "riscv,sv39" : "riscv,sv32");

        // CPU Interrupt Controller
        fdt.begin_node("interrupt-controller");
        fdt.property_u32("#interrupt-cells", 1);
        fdt.property_u32("#address-cells", 0);
        fdt.property_empty("interrupt-controller");
        fdt.property_string("compatible", "riscv,cpu-intc");
        fdt.property_phandle("phandle", 100U + hart);
        fdt.end_node();  // interrupt-controller

        fdt.end_node();  // cpu@hart
    }
    fdt.end_node();  // cpus

    // PMU selector IDs and raw event support are kept in the HPM catalog. The only generic
    // mapping advertised here is branch-misses, whose meaning matches selector 9.
    fdt.begin_node("pmu");
    fdt.property_string("compatible", "riscv,pmu");
    std::vector<FdtCell> generic_selectors;
    std::vector<FdtCell> generic_counter_ranges;
    for (const auto& mapping : core::kSbiGenericHpmMappings) {
        generic_selectors.insert(generic_selectors.end(),
                                 {mapping.event_code, 0U, std::to_underlying(mapping.event)});
        generic_counter_ranges.insert(generic_counter_ranges.end(),
                                      {mapping.event_code, mapping.event_code, 0xfffffff8U});
    }
    fdt.property_array_u32("riscv,event-to-mhpmevent", generic_selectors);
    fdt.property_array_u32("riscv,event-to-mhpmcounters", generic_counter_ranges);
    std::vector<FdtCell> raw_events;
    raw_events.reserve(core::kHpmEventCatalog.size() * 5);
    for (const auto& event : core::kHpmEventCatalog) {
        raw_events.push_back(0U);
        raw_events.push_back(std::to_underlying(event.id));
        raw_events.push_back(0xffffffffU);
        raw_events.push_back(0xffffffffU);
        raw_events.push_back(0xfffffff8U);
    }
    fdt.property_array_u32("riscv,raw-event-to-mhpmcounters", raw_events);
    fdt.end_node();

    // /memory
    fdt.begin_node(std::format("memory@{:x}", dram_base));
    fdt.property_string("device_type", "memory");
    fdt.property_reg("reg", dram_base, dram_size);
    fdt.end_node();

    // /soc
    fdt.begin_node("soc");
    fdt.property_u32("#address-cells", 2);
    fdt.property_u32("#size-cells", 2);
    fdt.property_string("compatible", "simple-bus");
    fdt.property_empty("ranges");

    // UART
    fdt.begin_node("uart@10000000");
    fdt.property_string("compatible", "ns16550a");
    fdt.property_reg("reg", 0x10000000ULL, 0x100ULL);
    fdt.property_u32("clock-frequency", 3686400);
    fdt.property_phandle("interrupt-parent", 1U);  // PLIC phandle
    fdt.property_u32("interrupts", 10U);
    fdt.end_node();

    // RTC
    fdt.begin_node("rtc@70000000");
    fdt.property_string("compatible", "google,goldfish-rtc");
    fdt.property_reg("reg", 0x70000000ULL, 0x1000ULL);
    fdt.property_phandle("interrupt-parent", 1U);
    fdt.property_u32("interrupts", 11U);
    fdt.end_node();

    // SiFive test finisher, exposed as a syscon so Linux can resolve the
    // separate syscon-poweroff and syscon-reboot nodes below.
    fdt.begin_node("test@100000");
    static constexpr std::string_view kTestCompat[] = {"sifive,test1", "sifive,test0", "syscon"};
    fdt.property_stringlist("compatible", kTestCompat);
    fdt.property_reg("reg", 0x100000ULL, 0x1000ULL);
    fdt.property_phandle("phandle", 2U);
    fdt.end_node();

    // CLINT
    fdt.begin_node("clint@60000000");
    static constexpr std::string_view kClintCompat[] = {"riscv,clint0", "sifive,clint0"};
    fdt.property_stringlist("compatible", kClintCompat);
    fdt.property_reg("reg", 0x60000000ULL, 0x10000ULL);
    std::vector<FdtCell> clint_irqs;
    for (uint32_t hart = 0; hart < num_harts; ++hart) {
        clint_irqs.push_back(100U + hart);  // cpu phandle
        clint_irqs.push_back(3U);           // M-software IRQ
        clint_irqs.push_back(100U + hart);
        clint_irqs.push_back(7U);  // M-timer IRQ
    }
    fdt.property_array_u32("interrupts-extended", clint_irqs);
    fdt.end_node();

    // PLIC
    fdt.begin_node("plic@50000000");
    static constexpr std::string_view kPlicCompat[] = {"sifive,plic-1.0.0", "riscv,plic0"};
    fdt.property_stringlist("compatible", kPlicCompat);
    fdt.property_reg("reg", 0x50000000ULL, 0x00400000ULL);
    fdt.property_u32("#address-cells", 0);
    fdt.property_u32("riscv,ndev", 31);
    fdt.property_empty("interrupt-controller");
    fdt.property_u32("#interrupt-cells", 1);
    fdt.property_phandle("phandle", 1U);
    std::vector<FdtCell> plic_irqs;
    for (uint32_t hart = 0; hart < num_harts; ++hart) {
        plic_irqs.push_back(100U + hart);
        plic_irqs.push_back(11U);  // M-external IRQ
        plic_irqs.push_back(100U + hart);
        plic_irqs.push_back(9U);  // S-external IRQ
    }
    fdt.property_array_u32("interrupts-extended", plic_irqs);
    fdt.end_node();

    const bool pflash_enabled =
        config.files.uefi_enabled ||
        core::SoCDeviceRegistry::enabled(config.soc, core::SoCDeviceKind::PFlash);
    if (is_pcie && pflash_enabled) {
        // Parallel NOR Flash (PFlash)
        fdt.begin_node("flash@20000000");
        fdt.property_string("compatible", "cfi-flash");
        fdt.property_array_u64("reg", {SIMRV_PLATFORM_PFLASH0_BASE, SIMRV_PLATFORM_PFLASH0_SIZE,
                                       SIMRV_PLATFORM_PFLASH1_BASE, SIMRV_PLATFORM_PFLASH1_SIZE});
        fdt.property_u32("bank-width", 2);
        fdt.end_node();
    }

    if (is_pcie) {
        // PCIe ECAM Controller
        fdt.begin_node("pci@30000000");
        fdt.property_string("compatible", "pci-host-ecam-generic");
        fdt.property_string("device_type", "pci");
        fdt.property_reg("reg", SIMRV_PLATFORM_PCIE_ECAM_BASE, SIMRV_PLATFORM_PCIE_ECAM_SIZE);
        fdt.property_u32("#address-cells", 3);
        fdt.property_u32("#size-cells", 2);
        fdt.property_u32("#interrupt-cells", 1);
        fdt.property_array_u32("bus-range", {0, 255});
        // 32-bit MMIO aperture: flags=0x02000000 (32-bit non-prefetchable memory)
        // pci_addr=0x40000000, cpu_addr=0x40000000, size=0x10000000
        const FdtCell pci_ranges[] = {0x02000000U, 0x00000000U, 0x40000000U, 0x00000000U,
                                      0x40000000U, 0x00000000U, 0x10000000U};
        fdt.property_array_u32("ranges", pci_ranges);
        fdt.property_phandle("interrupt-parent", 1U);
        fdt.property_array_u32("interrupt-map-mask", {0xf800U, 0, 0, 0x7U});
        fdt.property_array_u32("interrupt-map", {0x0800U, 0, 0, 1, 1, 17, 0x1000U, 0, 0, 1, 1, 18,
                                                 0x1800U, 0, 0, 1, 1, 19, 0x2000U, 0, 0, 1, 1, 20,
                                                 0x2800U, 0, 0, 1, 1, 21, 0x3000U, 0, 0, 1, 1, 22,
                                                 0x3800U, 0, 0, 1, 1, 23});
        fdt.end_node();
    } else {
        // VirtIO MMIO slots for embedded / FPGA target
        static constexpr struct {
            simrv::xlen::Address addr;
            uint32_t irq;
        } kVirtioSlots[] = {
            {0x10001000ULL, 1}, {0x10002000ULL, 2}, {0x10003000ULL, 3}, {0x10004000ULL, 4},
            {0x10005000ULL, 5}, {0x10006000ULL, 6}, {0x10007000ULL, 7}, {0x10008000ULL, 8},
        };
        for (const auto& slot : kVirtioSlots) {
            fdt.begin_node(std::format("virtio@{:x}", slot.addr));
            fdt.property_string("compatible", "virtio,mmio");
            fdt.property_reg("reg", slot.addr, 0x1000ULL);
            fdt.property_phandle("interrupt-parent", 1U);
            fdt.property_u32("interrupts", slot.irq);
            fdt.end_node();
        }
    }

    fdt.end_node();  // /soc

    fdt.begin_node("poweroff");
    fdt.property_string("compatible", "syscon-poweroff");
    fdt.property_phandle("regmap", 2U);
    fdt.property_u32("offset", 0);
    fdt.property_u32("value", 0x5555U);
    fdt.end_node();

    fdt.begin_node("reboot");
    fdt.property_string("compatible", "syscon-reboot");
    fdt.property_phandle("regmap", 2U);
    fdt.property_u32("offset", 0);
    fdt.property_u32("value", 0x7777U);
    fdt.end_node();

    fdt.end_node();  // root

    return fdt.serialize();
}

}  // namespace simrv::util
