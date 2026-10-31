/**
 * @file FdtGenerator.cpp
 * @brief Implementation of programmatic in-memory Flattened Device Tree generator.
 */
#include "simrv/util/FdtGenerator.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <iterator>
#include <span>

#include "simrv/core/GuestCapabilityCatalog.hpp"
#include "simrv/core/HpmEventCatalog.hpp"
#include "simrv/core/PlatformMap.h"
#include "simrv/core/PlatformTiming.hpp"
#include "simrv/core/SoCDeviceRegistry.hpp"
#include "simrv/device/UartTiming.hpp"
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
    const auto devices = core::SoCDeviceRegistry::resolve(config.soc);
    const auto find_device = [&devices](core::SoCDeviceKind kind) -> const core::SoCDeviceConfig* {
        const auto found = std::ranges::find_if(
            devices, [kind](const auto& item) { return item.kind == kind && item.enabled; });
        return found == devices.end() ? nullptr : &*found;
    };
    const auto* uart = find_device(core::SoCDeviceKind::Uart);
    std::string bootargs = config.files.bootargs;
    if (bootargs.empty()) {
        bootargs = uart == nullptr
                       ? "root=/dev/vda rw"
                       : std::format("console=ttyS0 earlycon=uart8250,mmio,{:#x} root=/dev/vda rw",
                                     static_cast<uint64_t>(uart->base));
    }
    fdt.property_string("bootargs", bootargs);
    if (uart != nullptr) {
        const uint32_t baud = simrv::device::uart_baud_rate(
            uart->clock_hz, simrv::device::uart_reset_divisor(uart->clock_hz));
        fdt.property_string("stdout-path", std::format("/soc/uart@{:x}:{}",
                                                       static_cast<uint64_t>(uart->base), baud));
    }
    fdt.end_node();

    // /cpus
    fdt.begin_node("cpus");
    fdt.property_u32("#address-cells", 1);
    fdt.property_u32("#size-cells", 0);
    fdt.property_u32("timebase-frequency", timebase);

    // This is the set SimRV currently implements and exposes to the guest, not the full set of
    // extensions required by the RVA23 profiles. Keep the compliance gap explicit in
    // docs/architecture/compliance.md until the remaining mandatory extensions are implemented.
    std::vector<std::string_view> guest_extensions(core::kBaseGuestExtensions.begin(),
                                                   core::kBaseGuestExtensions.end());
    guest_extensions.insert(guest_extensions.end(), core::kPatchableGuestExtensions.begin(),
                            core::kPatchableGuestExtensions.end());
    if (config.isa.misa_override) {
        // An explicit ISA preset may intentionally be narrower than the build's default
        // architecture profile. Do not advertise optional extensions that the preset omits.
        static constexpr std::string_view kExplicitIsaExtensions[] = {
            "i", "m", "a",      "f",     "d",     "c",        "b",
            "v", "h", "zicntr", "zihpm", "zicsr", "zifencei", "zihintpause"};
        std::erase_if(guest_extensions, [&](std::string_view extension) {
            return std::find(std::begin(kExplicitIsaExtensions), std::end(kExplicitIsaExtensions),
                             extension) == std::end(kExplicitIsaExtensions);
        });
        std::erase_if(guest_extensions, [&](std::string_view extension) {
            if (extension.size() != 1) return false;
            simrv::isa::IsaExtension misa_extension{};
            switch (extension.front()) {
                case 'a':
                    misa_extension = simrv::isa::IsaExtension::A;
                    break;
                case 'b':
                    misa_extension = simrv::isa::IsaExtension::B;
                    break;
                case 'c':
                    misa_extension = simrv::isa::IsaExtension::C;
                    break;
                case 'd':
                    misa_extension = simrv::isa::IsaExtension::D;
                    break;
                case 'f':
                    misa_extension = simrv::isa::IsaExtension::F;
                    break;
                case 'h':
                    misa_extension = simrv::isa::IsaExtension::H;
                    break;
                case 'i':
                    misa_extension = simrv::isa::IsaExtension::I;
                    break;
                case 'm':
                    misa_extension = simrv::isa::IsaExtension::M;
                    break;
                case 'v':
                    misa_extension = simrv::isa::IsaExtension::V;
                    break;
                default:
                    return false;
            }
            return !simrv::isa::misa_has_extension(config.isa.isa_preset, misa_extension);
        });
    }
    if constexpr (simrv::xlen::kIsXLen64) {
        // Present in the shared optional capability catalog.
    } else {
        std::erase_if(guest_extensions, [](std::string_view ext) {
            return ext == "ssu64xl" || ext == "ssnpm" || ext == "supm";
        });
    }
    const bool has_vector =
        simrv::isa::misa_has_extension(config.isa.isa_preset, simrv::isa::IsaExtension::V);
    const bool has_bitmanip =
        simrv::isa::misa_has_extension(config.isa.isa_preset, simrv::isa::IsaExtension::B);
    if (has_vector && has_bitmanip) guest_extensions.emplace_back("zvkb");
    if (simrv::isa::misa_has_extension(config.isa.isa_preset, simrv::isa::IsaExtension::M)) {
        guest_extensions.emplace_back("zmmul");
        guest_extensions.emplace_back("zimop");
    }
    if (simrv::isa::misa_has_extension(config.isa.isa_preset, simrv::isa::IsaExtension::A)) {
        guest_extensions.emplace_back("zaamo");
        guest_extensions.emplace_back("zalrsc");
        guest_extensions.emplace_back("za128rs");
    }
    if (has_bitmanip) {
        guest_extensions.emplace_back("zba");
        guest_extensions.emplace_back("zbb");
        guest_extensions.emplace_back("zbs");
    }
    const bool has_compressed =
        simrv::isa::misa_has_extension(config.isa.isa_preset, simrv::isa::IsaExtension::C);
    if (has_compressed) {
        guest_extensions.emplace_back("zca");
        guest_extensions.emplace_back("zcb");
        guest_extensions.emplace_back("zcmop");
        if (simrv::isa::misa_has_extension(config.isa.isa_preset, simrv::isa::IsaExtension::D)) {
            guest_extensions.emplace_back("zcd");
        }
    }
    if (simrv::isa::misa_has_extension(config.isa.isa_preset, simrv::isa::IsaExtension::H)) {
        guest_extensions.emplace_back("h");
        guest_extensions.emplace_back("ssstateen");
        guest_extensions.emplace_back("shcounterenw");
        guest_extensions.emplace_back("shvstvala");
        guest_extensions.emplace_back("shtvala");
        guest_extensions.emplace_back("shvstvecd");
        guest_extensions.emplace_back("shvsatpa");
        guest_extensions.emplace_back("shgatpa");
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
        fdt.property_u32("riscv,cbom-block-size", core::kGuestCacheBlockBytes);
        fdt.property_u32("riscv,cbop-block-size", core::kGuestCacheBlockBytes);
        fdt.property_u32("riscv,cboz-block-size", core::kGuestCacheBlockBytes);
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

    const auto* plic = find_device(core::SoCDeviceKind::Plic);
    const auto add_irq = [&fdt, plic](const core::SoCDeviceConfig& device) {
        if (plic != nullptr && device.irq != 0) {
            fdt.property_phandle("interrupt-parent", 1U);
            fdt.property_u32("interrupts", device.irq);
        }
    };
    if (uart != nullptr) {
        fdt.begin_node(std::format("uart@{:x}", static_cast<uint64_t>(uart->base)));
        fdt.property_string("compatible", "ns16550a");
        fdt.property_reg("reg", uart->base, uart->size);
        const uint32_t uart_clock = static_cast<uint32_t>(uart->clock_hz);
        const uint32_t baud = simrv::device::uart_baud_rate(
            uart->clock_hz, simrv::device::uart_reset_divisor(uart->clock_hz));
        fdt.property_u32("clock-frequency", uart_clock);
        fdt.property_u32("current-speed", baud);
        add_irq(*uart);
        fdt.end_node();
    }

    if (const auto* rtc = find_device(core::SoCDeviceKind::Rtc); rtc != nullptr) {
        fdt.begin_node(std::format("rtc@{:x}", static_cast<uint64_t>(rtc->base)));
        fdt.property_string("compatible", "google,goldfish-rtc");
        fdt.property_reg("reg", rtc->base, rtc->size);
        add_irq(*rtc);
        fdt.end_node();
    }

    const auto* power = find_device(core::SoCDeviceKind::Power);
    if (power != nullptr) {
        // SiFive test finisher, exposed as a syscon for Linux poweroff/reboot.
        fdt.begin_node(std::format("test@{:x}", static_cast<uint64_t>(power->base)));
        static constexpr std::string_view kTestCompat[] = {"sifive,test1", "sifive,test0",
                                                           "syscon"};
        fdt.property_stringlist("compatible", kTestCompat);
        fdt.property_reg("reg", power->base, power->size);
        fdt.property_phandle("phandle", 2U);
        fdt.end_node();
    }

    // The legacy CLINT node is represented only when that device is in the SoC model.
    if (const auto* clint = find_device(core::SoCDeviceKind::Clint); clint != nullptr) {
        fdt.begin_node(std::format("clint@{:x}", static_cast<uint64_t>(clint->base)));
        static constexpr std::string_view kClintCompat[] = {"riscv,clint0", "sifive,clint0"};
        fdt.property_stringlist("compatible", kClintCompat);
        fdt.property_reg("reg", clint->base, clint->size);
        std::vector<FdtCell> clint_irqs;
        for (uint32_t hart = 0; hart < num_harts; ++hart) {
            clint_irqs.push_back(100U + hart);  // cpu phandle
            clint_irqs.push_back(3U);           // M-software IRQ
            clint_irqs.push_back(100U + hart);
            clint_irqs.push_back(7U);  // M-timer IRQ
        }
        fdt.property_array_u32("interrupts-extended", clint_irqs);
        fdt.end_node();
    }

    // PLIC
    if (plic != nullptr) {
        fdt.begin_node(std::format("plic@{:x}", static_cast<uint64_t>(plic->base)));
        static constexpr std::string_view kPlicCompat[] = {"sifive,plic-1.0.0", "riscv,plic0"};
        fdt.property_stringlist("compatible", kPlicCompat);
        fdt.property_reg("reg", plic->base, plic->size);
        fdt.property_u32("#address-cells", 0);
        fdt.property_u32("riscv,ndev", core::kPlicSourceCount);
        fdt.property_empty("interrupt-controller");
        fdt.property_u32("#interrupt-cells", 1);
        fdt.property_phandle("phandle", 1U);
        std::vector<FdtCell> plic_irqs;
        for (uint32_t hart = 0; hart < num_harts; ++hart) {
            plic_irqs.push_back(100U + hart);
            plic_irqs.push_back(11U);
            plic_irqs.push_back(100U + hart);
            plic_irqs.push_back(9U);
        }
        fdt.property_array_u32("interrupts-extended", plic_irqs);
        fdt.end_node();
    }

    const auto* pflash = find_device(core::SoCDeviceKind::PFlash);
    const bool pflash_enabled = config.files.uefi_enabled || pflash != nullptr;
    if (is_pcie && pflash_enabled) {
        // Parallel NOR Flash (PFlash)
        const auto flash0 =
            pflash == nullptr ? core::soc_device_default(core::SoCDeviceKind::PFlash) : *pflash;
        fdt.begin_node(std::format("flash@{:x}", static_cast<uint64_t>(flash0.base)));
        fdt.property_string("compatible",
                            flash0.compatible.empty() ? "cfi-flash" : flash0.compatible);
        std::vector<uint64_t> flash_regs{static_cast<uint64_t>(flash0.base),
                                         static_cast<uint64_t>(flash0.size)};
        for (const auto& region : flash0.regions) {
            flash_regs.push_back(static_cast<uint64_t>(region.base));
            flash_regs.push_back(static_cast<uint64_t>(region.size));
        }
        fdt.property_array_u64("reg", flash_regs);
        fdt.property_u32("bank-width", 2);
        fdt.end_node();
    }

    if (is_pcie) {
        const auto& pcie = config.soc.pcie;
        // PCIe ECAM Controller
        fdt.begin_node(std::format("pci@{:x}", static_cast<uint64_t>(pcie.ecam_base)));
        fdt.property_string("compatible", "pci-host-ecam-generic");
        fdt.property_string("device_type", "pci");
        fdt.property_reg("reg", pcie.ecam_base, pcie.ecam_size);
        fdt.property_u32("#address-cells", 3);
        fdt.property_u32("#size-cells", 2);
        fdt.property_u32("#interrupt-cells", 1);
        fdt.property_array_u32("bus-range", {pcie.first_bus, pcie.last_bus});
        const auto mmio_end = static_cast<uint64_t>(pcie.mmio_base) + pcie.mmio_size;
        const FdtCell pci_memory_type = mmio_end > (uint64_t{1} << 32U) ? 0x03000000U : 0x02000000U;
        const FdtCell pci_ranges[] = {
            pci_memory_type,
            static_cast<FdtCell>(static_cast<uint64_t>(pcie.mmio_base) >> 32U),
            static_cast<FdtCell>(pcie.mmio_base),
            static_cast<FdtCell>(static_cast<uint64_t>(pcie.mmio_base) >> 32U),
            static_cast<FdtCell>(pcie.mmio_base),
            static_cast<FdtCell>(static_cast<uint64_t>(pcie.mmio_size) >> 32U),
            static_cast<FdtCell>(pcie.mmio_size),
        };
        fdt.property_array_u32("ranges", pci_ranges);
        if (plic != nullptr) {
            static constexpr core::SoCDeviceKind kPciKinds[] = {
                core::SoCDeviceKind::VirtioMmioBlock, core::SoCDeviceKind::VirtioMmioConsole,
                core::SoCDeviceKind::VirtioMmioRng,   core::SoCDeviceKind::VirtioMmioGpu,
                core::SoCDeviceKind::VirtioMmioInput, core::SoCDeviceKind::VirtioMmioSound,
                core::SoCDeviceKind::VirtioMmioNet,   core::SoCDeviceKind::VirtioMmioFs,
            };
            std::vector<FdtCell> interrupt_map;
            for (const auto kind : kPciKinds) {
                const auto* endpoint = find_device(kind);
                if (endpoint == nullptr) continue;
                const FdtCell bdf =
                    (static_cast<FdtCell>(endpoint->pci_bus.value_or(0)) << 16U) |
                    (static_cast<FdtCell>(endpoint->pci_slot.value_or(0)) << 11U) |
                    (static_cast<FdtCell>(endpoint->pci_function.value_or(0)) << 8U);
                interrupt_map.insert(interrupt_map.end(),
                                     {bdf, 0, 0, 1, 1, endpoint->pci_intx_source.value_or(0)});
            }
            if (!interrupt_map.empty()) {
                fdt.property_phandle("interrupt-parent", 1U);
                fdt.property_array_u32("interrupt-map-mask", {0x00ffff00U, 0, 0, 0x7U});
                fdt.property_array_u32("interrupt-map", interrupt_map);
            }
        }
        fdt.end_node();
    } else if (config.platform_profile == core::PlatformProfile::Mmio) {
        // VirtIO MMIO slots for embedded / FPGA target
        static constexpr core::SoCDeviceKind kVirtioKinds[] = {
            core::SoCDeviceKind::VirtioMmioBlock, core::SoCDeviceKind::VirtioMmioConsole,
            core::SoCDeviceKind::VirtioMmioRng,   core::SoCDeviceKind::VirtioMmioGpu,
            core::SoCDeviceKind::VirtioMmioInput, core::SoCDeviceKind::VirtioMmioSound,
            core::SoCDeviceKind::VirtioMmioNet,   core::SoCDeviceKind::VirtioMmioFs,
        };
        for (const auto kind : kVirtioKinds) {
            const auto* slot = find_device(kind);
            if (slot == nullptr) continue;
            fdt.begin_node(std::format("virtio@{:x}", static_cast<uint64_t>(slot->base)));
            fdt.property_string("compatible", "virtio,mmio");
            fdt.property_reg("reg", slot->base, slot->size);
            add_irq(*slot);
            fdt.end_node();
        }
    }

    // Descriptive custom and dummy windows are useful to guest discovery tools when a compatible
    // string is available. A custom factory is optional: without one, runtime uses a dummy MMIO.
    for (const auto& custom : devices) {
        if (custom.kind != core::SoCDeviceKind::CustomMmio &&
            custom.kind != core::SoCDeviceKind::DummyMmio)
            continue;
        if (custom.compatible.empty()) continue;
        fdt.begin_node(std::format("{}@{:x}", custom.name, static_cast<uint64_t>(custom.base)));
        fdt.property_string("compatible", custom.compatible);
        fdt.property_reg("reg", custom.base, custom.size);
        add_irq(custom);
        fdt.end_node();
    }

    fdt.end_node();  // /soc

    if (power != nullptr) {
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
    }

    fdt.end_node();  // root

    return fdt.serialize();
}

}  // namespace simrv::util
