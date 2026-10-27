#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "MachineRuntime.hpp"
#include "simrv/core/Boot.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/PlatformBuilder.hpp"
#include "simrv/core/PlatformMap.h"
#include "simrv/core/SoCDeviceRegistry.hpp"
#include "simrv/debug/GdbStub.hpp"
#include "simrv/debug/SpikeLockstep.hpp"
#include "simrv/debug/SymbolTable.hpp"
#include "simrv/device/AIA.hpp"
#include "simrv/device/Aclint.hpp"
#include "simrv/device/DmaController.hpp"
#include "simrv/device/PlatformDeviceRegistry.hpp"
#include "simrv/device/Power.hpp"
#include "simrv/device/ResetControlMmio.hpp"
#include "simrv/device/Rtc.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/device/pci/PcieRootComplex.hpp"
#include "simrv/util/ElfTypes.hpp"
#include "simrv/util/FdtGenerator.hpp"
#include "simrv/util/FdtPatcher.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

namespace simrv::core {

namespace {

using util::elf::Elf32_Ehdr;
using util::elf::Elf32_Phdr;
using util::elf::Elf64_Ehdr;
using util::elf::Elf64_Phdr;
using util::elf::kClass32;
using util::elf::kClass64;
using util::elf::kLoadSegment;

void apply_cpu_model_dram_defaults(MachineConfig& config, const std::string& model_name) {
    const bool low_dram_model = model_name == "cfu-provingground" || model_name == "rvproc";
    const bool rvcomp_model = model_name == "rvcomp";
    if (!low_dram_model && !rvcomp_model) return;

    if (low_dram_model) {
        config.memory.dram_base = 0;
    } else if (!config.soc.dram_base.has_value()) {
        config.memory.dram_base = simrv::memory::kDramBaseAddress;
    }

    constexpr auto kMinimumDramSize = simrv::xlen::Address{512} * 1024 * 1024;
    if (!config.soc.dram_size.has_value() && config.memory.dram_size < kMinimumDramSize) {
        config.memory.dram_size = kMinimumDramSize;
    }
}

void apply_cpu_model_runtime_config(Machine& machine, MachineConfig& config,
                                    const simrv::pipeline::CpuModelConfig& model) {
    machine.primary_hart().apply_cpu_model_config(model);
    const auto& timing = model.interconnect;
    machine.memory().system_bus().configure_timing(
        timing.request_latency, timing.response_latency, timing.data_request_latency,
        timing.data_response_latency, timing.startup_data_response_latency);
    apply_cpu_model_dram_defaults(config, model.name);
}

void resolve_start_pc_and_dram_base(simrv::core::Machine& machine,
                                    const simrv::debug::SymbolTable& symbols) {
    simrv::xlen::Address start_pc = machine.execution_config().start_pc;
    if (start_pc == simrv::boot::kStartPc || start_pc == 0) {
        simrv::xlen::Address entry = symbols.entry_point().value_or(
            symbols.lookup_name("_start").value_or(simrv::boot::kStartPc));
        if (entry < simrv::memory::kDramBaseAddress &&
            !machine.primary_hart().cpu_model_config.is_provingground_or_rvproc) {
            entry += simrv::memory::kDramBaseAddress;
        }
        start_pc = entry;
    }
    machine.set_resolved_boot_state(start_pc, symbols.lookup_name("tohost").or_else(
                                                  [&] { return symbols.lookup_name("_tohost"); }));
    machine.primary_hart().state().pc = machine.resolved_start_pc();
    if (machine.primary_hart().state().regs.xlen == 32) {
        machine.primary_hart().state().pc = static_cast<simrv::xlen::Register>(
            static_cast<int64_t>(static_cast<int32_t>(machine.primary_hart().state().pc)));
    }
}

void load_image_into_ram(std::string& file_path, simrv::memory::RamView ram_view,
                         const char* image_name, bool tuimode,
                         simrv::core::Machine* machine = nullptr) {
    if (machine != nullptr) {
        machine->clear_loaded_segments();
    }
    simrv::xlen::Byte* const ram = ram_view.data();
    const auto capacity = static_cast<std::size_t>(ram_view.size());
    if (ram == nullptr || capacity == 0) {
        simrv::log::error("invalid destination for {} image load", image_name);
        std::exit(EXIT_FAILURE);
    }

    if (file_path.empty()) {
        if (tuimode) {
            return;
        }
        simrv::log::error("No {} image specified", image_name);
        std::exit(EXIT_FAILURE);
    }

    std::ifstream in(file_path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        if (tuimode) {
            simrv::log::warn("{} image file '{}' not found. Launching TUI in idle state.",
                             image_name, file_path);
            file_path.clear();
            return;
        }
        simrv::log::error("image_file {} cannot be found", file_path);
        std::exit(EXIT_FAILURE);
    }

    const auto file_size = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    std::array<char, 4> magic{};
    bool is_elf = false;
    if (file_size >= 4 && in.read(magic.data(), 4)) {
        if (magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') {
            is_elf = true;
        }
    }
    in.seekg(0, std::ios::beg);

    if (is_elf) {
        bool loaded_segment = false;
        std::array<char, 5> ident{};
        if (in.read(ident.data(), 5)) {
            in.seekg(0, std::ios::beg);

            const char class_byte = ident[4];
            auto load_segment = [&](const auto& phdr) {
                if (phdr.p_type != kLoadSegment || phdr.p_filesz == 0) return;
                const simrv::xlen::Address paddr = phdr.p_paddr != 0 ? phdr.p_paddr : phdr.p_vaddr;
                const simrv::xlen::Address dram_base = ram_view.base();
                simrv::xlen::Address dest_offset = 0;
                if (paddr >= dram_base && (paddr - dram_base) < capacity) {
                    dest_offset = paddr - dram_base;
                } else if (paddr < capacity) {
                    dest_offset = paddr;
                } else {
                    return;
                }
                const size_t copy_bytes = std::min<size_t>(phdr.p_filesz, capacity - dest_offset);
                in.seekg(static_cast<std::streamoff>(phdr.p_offset), std::ios::beg);
                if (in.read(
                        reinterpret_cast<char*>(ram + dest_offset),
                        static_cast<std::streamsize>(
                            copy_bytes))) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                    loaded_segment = true;
                    if (machine != nullptr) {
                        const bool is_exec = (phdr.p_flags & 0x1) != 0;
                        machine->record_loaded_segment(paddr, phdr.p_memsz, is_exec);
                    }
                    if (phdr.p_memsz > phdr.p_filesz && (dest_offset + phdr.p_filesz) < capacity) {
                        const size_t bss_bytes = std::min<size_t>(
                            phdr.p_memsz - phdr.p_filesz, capacity - (dest_offset + phdr.p_filesz));
                        std::memset(ram + dest_offset + phdr.p_filesz, 0, bss_bytes);
                    }
                }
            };

            if (class_byte == kClass32) {
                Elf32_Ehdr ehdr{};
                if (in.read(reinterpret_cast<char*>(&ehdr),
                            sizeof(ehdr))) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                    std::vector<Elf32_Phdr> phdrs(ehdr.e_phnum);
                    in.seekg(ehdr.e_phoff, std::ios::beg);
                    if (in.read(
                            reinterpret_cast<char*>(phdrs.data()),
                            static_cast<std::streamsize>(
                                static_cast<size_t>(ehdr.e_phnum) *
                                sizeof(
                                    Elf32_Phdr)))) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                        for (const auto& phdr : phdrs) {
                            load_segment(phdr);
                        }
                    }
                }
            } else if (class_byte == kClass64) {
                Elf64_Ehdr ehdr{};
                if (in.read(reinterpret_cast<char*>(&ehdr),
                            sizeof(ehdr))) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                    std::vector<Elf64_Phdr> phdrs(ehdr.e_phnum);
                    in.seekg(static_cast<std::streamoff>(ehdr.e_phoff), std::ios::beg);
                    if (in.read(
                            reinterpret_cast<char*>(phdrs.data()),
                            static_cast<std::streamsize>(
                                static_cast<size_t>(ehdr.e_phnum) *
                                sizeof(
                                    Elf64_Phdr)))) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                        for (const auto& phdr : phdrs) {
                            load_segment(phdr);
                        }
                    }
                }
            }
        }

        if (!loaded_segment) {
            in.seekg(0, std::ios::beg);
            if (!in.read(reinterpret_cast<char*>(ram),
                         static_cast<std::streamsize>(std::min(
                             file_size,
                             capacity)))) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                simrv::log::error("{} image file {} read failed", image_name, file_path);
                std::exit(EXIT_FAILURE);
            }
            if (machine != nullptr) {
                machine->record_loaded_segment(ram_view.base(), std::min(file_size, capacity),
                                               true);
            }
        }
    } else {
        if (file_size > capacity) {
            simrv::log::error("{} image {} is too large ({} bytes > {} bytes capacity)", image_name,
                              file_path, file_size, capacity);
            std::exit(EXIT_FAILURE);
        }
        if (!in.read(reinterpret_cast<char*>(ram),
                     static_cast<std::streamsize>(
                         file_size))) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
            simrv::log::error("Failed to read {} image {}", image_name, file_path);
            std::exit(EXIT_FAILURE);
        }
        if (machine != nullptr) {
            machine->record_loaded_segment(ram_view.base(), std::min(file_size, capacity), true);
        }
    }
}

}  // namespace

auto Machine::platform_status() const -> PlatformStatusSnapshot {
    PlatformStatusSnapshot snapshot{.profile = platform_profile(),
                                    .has_pcie = runtime_->pcie != nullptr,
                                    .has_mmio = runtime_->mmio_disk != nullptr};
    if (runtime_->pci_disk) {
        snapshot.disk_loaded = runtime_->pci_disk->is_disk_loaded();
        snapshot.disk_status = runtime_->pci_disk->device_status();
        snapshot.disk_isr = runtime_->pci_disk->isr_status();
        snapshot.disk_capacity_sectors = runtime_->pci_disk->capacity_sectors();
    } else if (runtime_->mmio_disk) {
        snapshot.disk_loaded = runtime_->mmio_disk->is_disk_loaded();
        snapshot.disk_status = runtime_->mmio_disk->device_status();
        snapshot.disk_isr = runtime_->mmio_disk->isr_status();
        snapshot.disk_capacity_sectors = runtime_->mmio_disk->capacity_sectors();
    }
    if (runtime_->pci_net) {
        snapshot.network_status = runtime_->pci_net->device_status();
        snapshot.network_tx_packets = runtime_->pci_net->backend().tx_packet_count();
    } else if (runtime_->mmio_net) {
        snapshot.network_status = runtime_->mmio_net->device_status();
        snapshot.network_tx_packets = runtime_->mmio_net->backend().tx_packet_count();
    }
    if (runtime_->pci_console)
        snapshot.console_status = runtime_->pci_console->device_status();
    else if (runtime_->mmio_console)
        snapshot.console_status = runtime_->mmio_console->device_status();
    snapshot.rng_status = runtime_->pci_rng
                              ? runtime_->pci_rng->device_status()
                              : (runtime_->mmio_rng ? runtime_->mmio_rng->device_status() : 0);
    snapshot.gpu_status = runtime_->pci_gpu
                              ? runtime_->pci_gpu->device_status()
                              : (runtime_->mmio_gpu ? runtime_->mmio_gpu->device_status() : 0);
    if (runtime_->pci_sound) {
        snapshot.sound_status = runtime_->pci_sound->device_status();
        snapshot.sound_active = runtime_->pci_sound->backend().is_active();
        snapshot.sound_sample_rate = runtime_->pci_sound->backend().sample_rate();
        snapshot.sound_channels = runtime_->pci_sound->backend().channels();
    } else if (runtime_->mmio_sound) {
        snapshot.sound_status = runtime_->mmio_sound->device_status();
        snapshot.sound_active = runtime_->mmio_sound->backend().is_active();
        snapshot.sound_sample_rate = runtime_->mmio_sound->backend().sample_rate();
        snapshot.sound_channels = runtime_->mmio_sound->backend().channels();
    }
    if (runtime_->dma_controller) {
        snapshot.dma_busy = runtime_->dma_controller->is_busy();
    }
    return snapshot;
}

auto Machine::initialize() -> std::expected<void, std::string> {
    if (const auto valid = config.validate(); !valid) {
        simrv::log::error("Invalid machine configuration: {}", valid.error());
        return std::unexpected(valid.error());
    }
    auto smp_topology = config.soc.smp;
    if (smp_topology.clusters.empty()) smp_topology.hart_count = config.execution.num_harts;
    if (auto topology_result =
            memory().system_bus().coherence_hub().configure_topology(smp_topology);
        !topology_result) {
        return std::unexpected(topology_result.error());
    }
    if (config.cpu_model_preset.has_value()) {
        auto model = simrv::pipeline::make_cpu_model_preset(*config.cpu_model_preset);
        const auto profile_name = simrv::pipeline::cpu_model_preset_name(*config.cpu_model_preset);
        if (const auto resolved = simrv::core::resolve_cpu_model_path(profile_name)) {
            (void)simrv::core::load_cpu_config(*resolved, model);
        }
        if (config.branch_predictor_type.has_value()) {
            model.pipeline.branch_predictor.type = *config.branch_predictor_type;
        }
        if (config.bht_entries != 0) {
            model.pipeline.branch_predictor.bht_entries = config.bht_entries;
        }
        if (config.btb_entries != 0) {
            model.pipeline.branch_predictor.btb_entries = config.btb_entries;
        }
        if (config.ras_entries != 0) {
            model.pipeline.branch_predictor.ras_entries = config.ras_entries;
        }
        apply_cpu_model_runtime_config(*this, config, model);
    }
    if (!config.files.config_path.empty()) {
        auto model = primary_hart().cpu_model_config;
        if (!simrv::core::load_cpu_config(config.files.config_path, model)) {
            const std::string err =
                "Failed to load CPU configuration file: " + config.files.config_path;
            simrv::log::error("{}", err);
            return std::unexpected(err);
        }
        apply_cpu_model_runtime_config(*this, config, model);
    }
    if (config.cpu_model_config_override) {
        apply_cpu_model_runtime_config(*this, config, *config.cpu_model_config_override);
    }
    std::string cfu_plugin_path = config.files.cfu_plugin_path;
    if (cfu_plugin_path.empty()) cfu_plugin_path = primary_hart().cpu_model_config.cfu.plugin;
    if (!cfu_plugin_path.empty()) {
        if (!primary_hart().cfu_unit.load_plugin(cfu_plugin_path)) {
            const std::string err = "Failed to load CFU plugin: " + cfu_plugin_path;
            simrv::log::error("{}", err);
            return std::unexpected(err);
        }
    }

    runtime_->rtc = std::make_unique<simrv::Rtc>(*this);
    runtime_->uart = std::make_unique<simrv::device::Uart>(*this);
    runtime_->power = std::make_unique<simrv::device::PowerMmio>(*this);
    if (!runtime_->dma_controller) {
        runtime_->dma_controller = std::make_unique<simrv::device::DmaController>(this);
    }
    if (!runtime_->axi_bridge) {
        runtime_->axi_bridge =
            std::make_unique<simrv::memory::Axi4Bridge>(this, primary_hart().cpu_model_config.axi);
    } else {
        runtime_->axi_bridge->set_config(primary_hart().cpu_model_config.axi);
    }
    if (tui_enabled() || debugger_enabled()) {
        execution_state_.store(ExecutionState::Paused, std::memory_order_release);
    }
    const auto ram = ram_view();
    const auto effective_dram_size = static_cast<size_t>(ram.size());
    config.memory.dram_size = static_cast<simrv::xlen::Address>(effective_dram_size);
    if (!allocate_ram(effective_dram_size)) {
        const std::string err =
            "Failed to allocate main memory (" + std::to_string(effective_dram_size) + " bytes)";
        simrv::log::error("{}", err);
        return std::unexpected(err);
    }

    memory().initialize_mmu();

    runtime_->aclint_mtimer = std::make_unique<simrv::device::AclintMtimer>(this);
    runtime_->aclint_mswi = std::make_unique<simrv::device::AclintMswi>(this);
    runtime_->imsic_m = std::make_unique<simrv::device::Imsic>(
        this, simrv::device::Imsic::Privilege::Machine, simrv::mmio::kImsicMBaseAddress,
        simrv::mmio::kImsicMSize);
    runtime_->imsic_s = std::make_unique<simrv::device::Imsic>(
        this, simrv::device::Imsic::Privilege::Supervisor, simrv::mmio::kImsicSBaseAddress,
        simrv::mmio::kImsicSSize);
    runtime_->aplic_m = std::make_unique<simrv::device::Aplic>(
        this, simrv::device::Aplic::Privilege::Machine, simrv::mmio::kAplicMBaseAddress,
        simrv::mmio::kAplicMSize, runtime_->imsic_m.get());
    runtime_->aplic_s = std::make_unique<simrv::device::Aplic>(
        this, simrv::device::Aplic::Privilege::Supervisor, simrv::mmio::kAplicSBaseAddress,
        simrv::mmio::kAplicSSize, runtime_->imsic_s.get());
    PlatformBuilder::compose(*this);
    for (const auto& device : simrv::core::SoCDeviceRegistry::resolve(config.soc)) {
        if (device.enabled && device.kind == SoCDeviceKind::DummyMmio) {
            runtime_->dummy_mmios.push_back(std::make_unique<simrv::device::DummyMmio>(
                device.name, device.base, device.size, device.read_value));
        } else if (device.enabled && (device.kind == SoCDeviceKind::CustomMmio ||
                                      device.kind == SoCDeviceKind::ResetControl)) {
            auto extension = device_registry().create_mmio(*this, device);
            if (extension) {
                runtime_->extension_mmios.push_back(std::move(extension));
            } else if (device.kind == SoCDeviceKind::CustomMmio) {
                runtime_->dummy_mmios.push_back(std::make_unique<simrv::device::DummyMmio>(
                    device.name, device.base, device.size, device.read_value));
            }
        }
    }

    const auto device_enabled = [&](simrv::core::SoCDeviceKind kind) {
        return simrv::core::SoCDeviceRegistry::enabled(config.soc, kind);
    };
    const auto add_base_node = [&](simrv::core::SoCDeviceKind kind,
                                   simrv::memory::TileLinkNode* node) -> bool {
        if (device_enabled(kind) && node != nullptr) {
            return memory().system_bus().add_node(node);
        }
        return true;
    };
    if (!add_base_node(SoCDeviceKind::AclintMtimer, runtime_->aclint_mtimer.get()) ||
        !add_base_node(SoCDeviceKind::AclintMswi, runtime_->aclint_mswi.get()) ||
        !add_base_node(SoCDeviceKind::Imsic, runtime_->imsic_m.get()) ||
        !add_base_node(SoCDeviceKind::Imsic, runtime_->imsic_s.get()) ||
        !add_base_node(SoCDeviceKind::Aplic, runtime_->aplic_m.get()) ||
        !add_base_node(SoCDeviceKind::Aplic, runtime_->aplic_s.get()) ||
        !add_base_node(SoCDeviceKind::Rtc, runtime_->rtc.get()) ||
        !add_base_node(SoCDeviceKind::Uart, runtime_->uart.get()) ||
        !add_base_node(SoCDeviceKind::Power, runtime_->power.get()) ||
        !add_base_node(SoCDeviceKind::DmaController, runtime_->dma_controller.get())) {
        return std::unexpected("Failed to register core SoC devices on system bus");
    }
    if (runtime_->ring_buffer_ethernet) {
        if (!memory().system_bus().add_node(runtime_->ring_buffer_ethernet->csr_node()) ||
            !memory().system_bus().add_node(runtime_->ring_buffer_ethernet->rx_node()) ||
            !memory().system_bus().add_node(runtime_->ring_buffer_ethernet->tx_node())) {
            return std::unexpected("Failed to register ring buffer ethernet on system bus");
        }
    }
    for (const auto& device : runtime_->dummy_mmios) {
        if (!memory().system_bus().add_node(device.get())) {
            return std::unexpected("Failed to register dummy MMIO device on system bus");
        }
    }
    for (const auto& device : runtime_->extension_mmios) {
        if (!memory().system_bus().add_node(device.get())) {
            return std::unexpected("Failed to register custom MMIO device on system bus");
        }
    }

    if (runtime_->pcie) {
        if (!memory().system_bus().add_node(&runtime_->pcie->ecam_node()) ||
            !memory().system_bus().add_node(&runtime_->pcie->mmio_node())) {
            return std::unexpected("Failed to register PCIe nodes on system bus");
        }
    }

    if (runtime_->pflash0 && !memory().system_bus().add_node(runtime_->pflash0.get())) {
        return std::unexpected("Failed to register PFlash0 on system bus");
    }
    if (runtime_->pflash1 && !memory().system_bus().add_node(runtime_->pflash1.get())) {
        return std::unexpected("Failed to register PFlash1 on system bus");
    }

    const std::array<std::shared_ptr<simrv::device::VirtioMmioDevice>, 8> mmio_devs = {
        runtime_->mmio_disk,  runtime_->mmio_console, runtime_->mmio_rng, runtime_->mmio_gpu,
        runtime_->mmio_input, runtime_->mmio_sound,   runtime_->mmio_net, runtime_->mmio_fs,
    };
    for (const auto& dev : mmio_devs) {
        if (dev && !memory().system_bus().add_node(dev.get())) {
            return std::unexpected("Failed to register VirtIO MMIO device on system bus");
        }
    }

    if (!add_base_node(SoCDeviceKind::Plic, &primary_hart().plic_mmio) ||
        !add_base_node(SoCDeviceKind::Clint, &primary_hart().clint_mmio)) {
        return std::unexpected("Failed to register interrupt controller on system bus");
    }
    const bool linux_boot = !config.execution.appmode;
    if (linux_boot && effective_dram_size < static_cast<size_t>(0x00100000U)) {
        simrv::log::error("DRAM must be at least 1 MiB for an OS device tree");
        return std::unexpected("DRAM must be at least 1 MiB for an OS device tree");
    }
    const simrv::xlen::Address dtb_offset =
        linux_boot ? static_cast<simrv::xlen::Address>(effective_dram_size -
                                                       static_cast<size_t>(0x00100000U))
                   : simrv::boot::kInitDataAddress;

    auto effective_isa_preset_bits = [&]() -> simrv::xlen::CsrValue {
        if (config.cpu_model_config_override) {
            const auto& cfg = *config.cpu_model_config_override;
            auto bits = isa::isa_preset_bits(cfg.isa_preset);
            if (cfg.has_isa_extension("zba") || cfg.has_isa_extension("zbb") ||
                cfg.has_isa_extension("zbc") || cfg.has_isa_extension("zbs")) {
                bits |= isa::misa_extension_bit(isa::IsaExtension::B);
            }
            return bits;
        }
        if (!config.files.config_path.empty()) {
            simrv::pipeline::CpuModelConfig cfg{};
            if (simrv::core::load_cpu_config(config.files.config_path, cfg)) {
                auto bits = isa::isa_preset_bits(cfg.isa_preset);
                if (cfg.has_isa_extension("zba") || cfg.has_isa_extension("zbb") ||
                    cfg.has_isa_extension("zbc") || cfg.has_isa_extension("zbs")) {
                    bits |= isa::misa_extension_bit(isa::IsaExtension::B);
                }
                return bits;
            }
        }
        if (config.cpu_model_preset.has_value()) {
            const auto profile_name =
                simrv::pipeline::cpu_model_preset_name(*config.cpu_model_preset);
            simrv::pipeline::CpuModelConfig cfg{};
            if (const auto resolved = simrv::core::resolve_cpu_model_path(profile_name)) {
                if (simrv::core::load_cpu_config(*resolved, cfg)) {
                    auto bits = isa::isa_preset_bits(cfg.isa_preset);
                    if (cfg.has_isa_extension("zba") || cfg.has_isa_extension("zbb") ||
                        cfg.has_isa_extension("zbc") || cfg.has_isa_extension("zbs")) {
                        bits |= isa::misa_extension_bit(isa::IsaExtension::B);
                    }
                    return bits;
                }
            }
            cfg = pipeline::make_cpu_model_preset(*config.cpu_model_preset);
            auto bits = isa::isa_preset_bits(cfg.isa_preset);
            if (cfg.has_isa_extension("zba") || cfg.has_isa_extension("zbb") ||
                cfg.has_isa_extension("zbc") || cfg.has_isa_extension("zbs")) {
                bits |= isa::misa_extension_bit(isa::IsaExtension::B);
            }
            return bits;
        }
        return config.isa.isa_preset;
    };

    const unsigned int model_xlen = primary_hart().cpu_model_config.supported_xlen;
    const unsigned int target_xlen = (model_xlen != 0) ? model_xlen : simrv::xlen::kXLenBits;
    simrv::xlen::CsrValue initial_misa = isa::misa_with_mxl(
        config.isa.misa_override ? config.isa.isa_preset : effective_isa_preset_bits(),
        target_xlen);
    initial_misa &= ~isa::misa_extension_bit(isa::IsaExtension::X);
    config.isa.isa_preset = initial_misa;
    if constexpr (simrv::xlen::kIsXLen64) {
        bool is_32bit = (target_xlen == 32);
        if (config.isa.misa_override && config.isa.misa_xlen == 32) {
            is_32bit = true;
        } else if (!config.isa.misa_override || config.isa.misa_xlen == 0) {
            auto check_elf = [&](const std::string& path) -> void {
                if (path.empty()) return;
                std::ifstream file(path, std::ios::binary);
                if (file.is_open()) {
                    std::array<char, 5> header{};
                    if (file.read(header.data(), 5)) {
                        if (header[0] == 0x7f && header[1] == 'E' && header[2] == 'L' &&
                            header[3] == 'F') {
                            if (header[4] == kClass32) {
                                is_32bit = true;
                            }
                        }
                    }
                }
            };
            check_elf(config.files.binary_path);
            check_elf(config.debug.spike_elf);
            if (!is_32bit && !config.files.binary_path.empty()) {
                std::string base_path = config.files.binary_path;
                size_t last_dot = base_path.find_last_of('.');
                size_t last_slash = base_path.find_last_of("/\\");
                if (last_dot != std::string::npos &&
                    (last_slash == std::string::npos || last_dot > last_slash)) {
                    base_path = base_path.substr(0, last_dot);
                }
                if (base_path != config.files.binary_path) {
                    static constexpr std::array kSuffixes = {".elf", ".ELF", ".out", ".OUT",
                                                             ".axf", ".AXF", ""};
                    for (const auto* suffix : kSuffixes) {
                        check_elf(base_path + suffix);
                    }
                }
            }
            if (!is_32bit) {
                if (config.files.binary_path.find("rv32") != std::string::npos ||
                    config.debug.spike_elf.find("rv32") != std::string::npos) {
                    is_32bit = true;
                }
            }
        }
        if (is_32bit) {
            initial_misa = isa::misa_with_mxl(initial_misa, 32);
        }
    }
    primary_hart().state().pc = resolved_start_pc_;
    primary_hart().state().regs.write(static_cast<simrv::xlen::RegId>(10), 0);  // a0 = hartid
    primary_hart().state().regs.write(
        static_cast<simrv::xlen::RegId>(11),
        linux_boot ? static_cast<simrv::xlen::Register>(config.memory.dram_base + dtb_offset)
                   : 0);  // a1 = dtb
    primary_hart().state().misa = initial_misa;
    primary_hart().state().refresh_misa_status();
    primary_hart().state().priv = kPrivMachine;
    primary_hart().state().regs.vlen = config.isa.vlen ? config.isa.vlen : 256;
    primary_hart().state().initialize_lower_xlen_fields();
    if (primary_hart().state().regs.xlen == 32) {
        primary_hart().state().pc = static_cast<simrv::xlen::Register>(
            static_cast<int64_t>(static_cast<int32_t>(primary_hart().state().pc)));
    }
    primary_hart().dcache.flush();
    primary_hart().TLB_flush();

    if (config.files.uefi_enabled) {
        if (!config.files.uefi_code_path.empty() && runtime_->pflash0) {
            if (!runtime_->pflash0->load_image(config.files.uefi_code_path)) {
                simrv::log::warn("failed to load UEFI code image from {}",
                                 config.files.uefi_code_path);
            }
        }
        if (!config.files.uefi_vars_path.empty() && runtime_->pflash1) {
            runtime_->pflash1->load_image(config.files.uefi_vars_path);
            runtime_->pflash1->set_backing_file(config.files.uefi_vars_path);
        }
        set_resolved_boot_state(SIMRV_PLATFORM_PFLASH0_BASE, std::nullopt);
        primary_hart().state().pc = SIMRV_PLATFORM_PFLASH0_BASE;
    }

    if (!config.files.binary_path.empty()) {
        load_image_into_ram(config.files.binary_path, ram_view(), "memory", tui_enabled(), this);
        symbol_table().load_from_elf(
            config.debug.spike_elf.empty() ? config.files.binary_path : config.debug.spike_elf,
            true,
            (runtime_profile.interaction == InteractionMode::Tui ||
             !config.debug.architecture_trace_path.empty() || !config.debug.trace_function.empty())
                ? simrv::debug::SymbolLoadMode::FullDebug
                : simrv::debug::SymbolLoadMode::RuntimeEssentials);
    }

    resolve_start_pc_and_dram_base(*this, symbol_table());
    if (config.files.uefi_enabled) {
        set_resolved_boot_state(SIMRV_PLATFORM_PFLASH0_BASE, std::nullopt);
        primary_hart().state().pc = SIMRV_PLATFORM_PFLASH0_BASE;
    }

    runtime_->secondary_harts.clear();
    if (config.execution.num_harts > 1) {
        runtime_->secondary_harts.reserve(config.execution.num_harts - 1);
        for (uint32_t i = 1; i < config.execution.num_harts; ++i) {
            auto sec_cpu = std::make_unique<simrv::core::CPU>();
            sec_cpu->machine_ = this;
            sec_cpu->apply_cpu_model_config(primary_hart().cpu_model_config);
            if (!cfu_plugin_path.empty()) sec_cpu->cfu_unit.load_plugin(cfu_plugin_path);
            sec_cpu->state().mhartid = i;
            sec_cpu->state().misa = initial_misa;
            sec_cpu->state().refresh_misa_status();
            sec_cpu->state().regs.vlen = config.isa.vlen ? config.isa.vlen : 256;
            sec_cpu->state().initialize_lower_xlen_fields();
            // When OpenSBI firmware is active, secondary harts must execute its M-mode wait
            // loop so its IPI can release them into the Linux entry point. When direct-SBI is
            // active, secondary harts must start Stopped so Sbi::handle_hsm(HartStart) can claim
            // and launch them.
            // Keep recognizing older FW_PAYLOAD bundles by their original filename.
            const bool is_fw_payload =
                (config.files.binary_path.find("fw_payload") != std::string::npos ||
                 config.files.binary_path.find("opensbi") != std::string::npos);
            const bool opensbi_active =
                primary_hart().use_opensbi || !config.files.dvtree_path.empty() || is_fw_payload;
            sec_cpu->use_opensbi = opensbi_active;
            const bool sec_started = appmode_enabled() || (linux_boot && opensbi_active);
            sec_cpu->hart_status.store(sec_started ? HartStatus::Started : HartStatus::Stopped,
                                       std::memory_order_relaxed);
            for (std::size_t r = 0; r < 32; ++r) {
                sec_cpu->state().regs.write(static_cast<simrv::xlen::RegId>(r), 0);
            }
            sec_cpu->state().pc = resolved_start_pc_;
            if (sec_cpu->state().regs.xlen == 32) {
                sec_cpu->state().pc = static_cast<simrv::xlen::Register>(
                    static_cast<int64_t>(static_cast<int32_t>(sec_cpu->state().pc)));
            }
            sec_cpu->state().priv = kPrivMachine;
            sec_cpu->state().regs.write(static_cast<simrv::xlen::RegId>(10), i);
            sec_cpu->state().regs.write(static_cast<simrv::xlen::RegId>(11),
                                        linux_boot ? static_cast<simrv::xlen::Register>(
                                                         config.memory.dram_base + dtb_offset)
                                                   : 0);
            sec_cpu->soft_tlb_flush();
            sec_cpu->TLB_flush();
            runtime_->secondary_harts.push_back(std::move(sec_cpu));
        }
        runtime_->refresh_secondary_hart_ptrs();
    }

    if (config.bram_prewarm) {
        prewarm_bram_caches();
    }

    // If launched without a binary in TUI mode, skip image-dependent init --
    // the TUI will open the LoadBinary modal and call load_program_binary() later.
    if (config.files.binary_path.empty() && !config.files.uefi_enabled && tui_enabled()) {
        execution_state_.store(ExecutionState::Paused, std::memory_order_release);
        return {};
    }

    if (linux_boot) {
        std::vector<uint8_t> final_blob;
        simrv::util::FdtFramebufferInfo fb_info{};
        if (config.files.auto_fdt || config.files.dvtree_path.empty()) {
            if (config.files.dvtree_path.empty() && !config.files.auto_fdt) {
                return std::unexpected("Linux boot requires a device tree blob");
            }
            final_blob = simrv::util::generate_platform_fdt(config, device_registry_.get());
        } else {
            std::ifstream input(config.files.dvtree_path, std::ios::binary);
            if (!input)
                return std::unexpected("cannot open device tree: " + config.files.dvtree_path);
            std::vector<uint8_t> input_blob{std::istreambuf_iterator<char>(input),
                                            std::istreambuf_iterator<char>()};
            auto patched =
                simrv::util::patch_fdt(input_blob, config.memory.dram_base, effective_dram_size,
                                       config.execution.num_harts, &fb_info,
                                       config.execution.timebase_hz, config.execution.cpu_clock_hz);
            if (!patched) return std::unexpected(patched.error());
            final_blob = std::move(*patched);
        }

        if (dtb_offset >= effective_dram_size)
            return std::unexpected("device-tree load offset is outside DRAM");

        if (fb_info.width > 0 && fb_info.height > 0) {
            framebuffer_geometry_.width = fb_info.width;
            framebuffer_geometry_.height = fb_info.height;
            if (fb_info.base != 0) {
                framebuffer_geometry_.base = fb_info.base;
            }
        }
        const auto dt_cap = static_cast<std::size_t>(effective_dram_size - dtb_offset);
        if (final_blob.size() > dt_cap)
            return std::unexpected("device tree does not fit in guest DRAM");
        const auto initialized_ram = ram_view();
        const simrv::xlen::Address dtb_address = initialized_ram.base() + dtb_offset;
        if (!initialized_ram.contains(dtb_address, final_blob.size()))
            return std::unexpected("device-tree region is outside DRAM");
        std::memcpy(initialized_ram.unchecked_ptr(dtb_address), final_blob.data(),
                    final_blob.size());
    }

    if (config.files.disk_enabled) {
        if (runtime_->pci_disk) runtime_->pci_disk->load_disk(config.files.disk_path);
        if (runtime_->mmio_disk) runtime_->mmio_disk->load_disk(config.files.disk_path);
    }

    if (instruction_mix_enabled()) {
        primary_hart().e_instmix.fill(0);
        primary_hart().perf_counters.reset();
        primary_hart().last_retire_cycle_ = 0;
    }

    // ---- GDB stub initialization ----
    if (debugger_enabled()) {
        try {
            runtime_->gdb_stub = std::make_unique<simrv::debug::GdbStub>(debugger_port());
            execution_state_.store(ExecutionState::Paused, std::memory_order_release);
            runtime_->gdb_stub->start([this]() { notify_control_event(); });
            simrv::log::info("GDB server listening on port {}; target paused",
                             runtime_->gdb_stub->bound_port());
        } catch (const std::exception& ex) {
            const std::string err = std::string("GDB stub init failed: ") + ex.what();
            simrv::log::error("{}", err);
            return std::unexpected(err);
        }
    }

    // ---- Spike lockstep initialization ----
    if (lockstep_enabled()) {
        // Derive the ISA string from the active ISA preset and compile-time XLEN
        const std::string isa_str = simrv::debug::spike_isa_string(primary_hart().state().misa);
        const std::string spike_img = spike_elf().empty() ? binary_path() : spike_elf();
        runtime_->spike_lockstep = std::make_unique<simrv::debug::SpikeLockstep>(
            spike_binary(), spike_img, disk_path(), config.files.dvtree_path, isa_str);
        simrv::log::info("Spike lockstep co-simulation active (isa={})", isa_str);
        if (!runtime_->spike_lockstep->start()) {
            simrv::log::error("Failed to launch Spike for lockstep verification");
            return std::unexpected("Failed to launch Spike for lockstep verification");
        }
    }

    if (tui_enabled()) {
        execution_state_.store(ExecutionState::Paused, std::memory_order_release);
    }

    publish_lifecycle_event(LifecycleEventKind::Initialized);
    return {};
}

auto Machine::load_program_binary(const std::string& filepath) -> std::expected<void, std::string> {
    if (filepath.empty()) {
        return std::unexpected("Binary file path is empty");
    }
    auto next = configuration();
    next.files.binary_path = filepath;
    return stage_reconfiguration(std::move(next));
}

auto Machine::load_disk_image(const std::string& filepath) -> std::expected<void, std::string> {
    if (filepath.empty()) {
        return std::unexpected("Disk image file path is empty");
    }
    auto next = configuration();
    next.files.disk_path = filepath;
    next.files.disk_enabled = true;
    return stage_reconfiguration(std::move(next));
}

}  // namespace simrv::core
