#include <elf.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "MachineRuntime.hpp"
#include "simrv/Define.hpp"
#include "simrv/core/Boot.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/PlatformBuilder.hpp"
#include "simrv/debug/GdbStub.hpp"
#include "simrv/debug/SpikeLockstep.hpp"
#include "simrv/device/AIA.hpp"
#include "simrv/device/Aclint.hpp"
#include "simrv/device/Power.hpp"
#include "simrv/device/Rtc.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/device/mmio/VirtioMmioBlock.hpp"
#include "simrv/device/mmio/VirtioMmioConsole.hpp"
#include "simrv/device/mmio/VirtioMmioGpu.hpp"
#include "simrv/device/mmio/VirtioMmioInput.hpp"
#include "simrv/device/mmio/VirtioMmioNet.hpp"
#include "simrv/device/mmio/VirtioMmioRng.hpp"
#include "simrv/device/mmio/VirtioMmioSound.hpp"
#include "simrv/device/pci/PcieRootComplex.hpp"
#include "simrv/device/pci/VirtioPciBlock.hpp"
#include "simrv/device/pci/VirtioPciConsole.hpp"
#include "simrv/device/pci/VirtioPciGpu.hpp"
#include "simrv/device/pci/VirtioPciInput.hpp"
#include "simrv/device/pci/VirtioPciNet.hpp"
#include "simrv/device/pci/VirtioPciRng.hpp"
#include "simrv/device/pci/VirtioPciSound.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/util/FdtGenerator.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

namespace {

void resolve_start_pc_and_dram_base(simrv::core::Machine& machine,
                                    const simrv::debug::SymbolTable& symbols) {
    Address start_pc = machine.execution_config().start_pc;
    if (start_pc == simrv::boot::kStartPc || start_pc == 0) {
        Address entry = symbols.entry_point().value_or(
            symbols.lookup_name("_start").value_or(simrv::boot::kStartPc));
        if (entry < simrv::memory::kDramBaseAddress &&
            machine.configuration().cpu_model_profile !=
                simrv::pipeline::CpuModelProfile::CfuProvingGround) {
            entry += simrv::memory::kDramBaseAddress;
        }
        start_pc = entry;
    }
    machine.set_resolved_boot_state(start_pc, symbols.lookup_name("tohost").or_else(
                                                  [&] { return symbols.lookup_name("_tohost"); }));
    machine.primary_hart().state().pc = machine.resolved_start_pc();
    if (machine.primary_hart().state().regs.xlen == 32) {
        machine.primary_hart().state().pc = static_cast<Register>(
            static_cast<int64_t>(static_cast<int32_t>(machine.primary_hart().state().pc)));
    }
}

void load_image_into_ram(std::string& file_path, simrv::memory::RamView ram_view,
                         const char* image_name, bool tuimode,
                         simrv::core::Machine* machine = nullptr) {
    if (machine != nullptr) {
        machine->clear_loaded_segments();
    }
    Byte* const ram = ram_view.data();
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
            if (class_byte == 1) {  // 32-bit ELF
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
                            if (phdr.p_type == PT_LOAD && phdr.p_filesz > 0) {
                                const Address paddr =
                                    phdr.p_paddr != 0 ? phdr.p_paddr : phdr.p_vaddr;
                                const Address dram_base = ram_view.base();
                                Address dest_offset = 0;
                                if (paddr >= dram_base && (paddr - dram_base) < capacity) {
                                    dest_offset = paddr - dram_base;
                                } else if (paddr < capacity) {
                                    dest_offset = paddr;
                                } else {
                                    continue;
                                }
                                const size_t copy_bytes =
                                    std::min<size_t>(phdr.p_filesz, capacity - dest_offset);
                                in.seekg(phdr.p_offset, std::ios::beg);
                                if (in.read(
                                        reinterpret_cast<char*>(ram + dest_offset),
                                        static_cast<std::streamsize>(
                                            copy_bytes))) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                                    loaded_segment = true;
                                    if (machine != nullptr) {
                                        const bool is_exec = (phdr.p_flags & 0x1) != 0;
                                        machine->record_loaded_segment(paddr, phdr.p_memsz,
                                                                       is_exec);
                                    }
                                    if (phdr.p_memsz > phdr.p_filesz &&
                                        (dest_offset + phdr.p_filesz) < capacity) {
                                        const size_t bss_bytes = std::min<size_t>(
                                            phdr.p_memsz - phdr.p_filesz,
                                            capacity - (dest_offset + phdr.p_filesz));
                                        std::memset(ram + dest_offset + phdr.p_filesz, 0,
                                                    bss_bytes);
                                    }
                                }
                            }
                        }
                    }
                }
            } else if (class_byte == 2) {  // 64-bit ELF
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
                            if (phdr.p_type == PT_LOAD && phdr.p_filesz > 0) {
                                const Address paddr =
                                    phdr.p_paddr != 0 ? phdr.p_paddr : phdr.p_vaddr;
                                const Address dram_base = ram_view.base();
                                Address dest_offset = 0;
                                if (paddr >= dram_base && (paddr - dram_base) < capacity) {
                                    dest_offset = paddr - dram_base;
                                } else if (paddr < capacity) {
                                    dest_offset = paddr;
                                } else {
                                    continue;
                                }
                                const size_t copy_bytes =
                                    std::min<size_t>(phdr.p_filesz, capacity - dest_offset);
                                in.seekg(static_cast<std::streamoff>(phdr.p_offset), std::ios::beg);
                                if (in.read(
                                        reinterpret_cast<char*>(ram + dest_offset),
                                        static_cast<std::streamsize>(
                                            copy_bytes))) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                                    loaded_segment = true;
                                    if (machine != nullptr) {
                                        const bool is_exec = (phdr.p_flags & 0x1) != 0;
                                        machine->record_loaded_segment(paddr, phdr.p_memsz,
                                                                       is_exec);
                                    }
                                    if (phdr.p_memsz > phdr.p_filesz &&
                                        (dest_offset + phdr.p_filesz) < capacity) {
                                        const size_t bss_bytes = std::min<size_t>(
                                            phdr.p_memsz - phdr.p_filesz,
                                            capacity - (dest_offset + phdr.p_filesz));
                                        std::memset(ram + dest_offset + phdr.p_filesz, 0,
                                                    bss_bytes);
                                    }
                                }
                            }
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
    return snapshot;
}

auto Machine::initialize() -> std::expected<void, std::string> {
    if (config.cpu_model_profile.has_value()) {
        auto model = simrv::pipeline::make_cpu_model_profile(*config.cpu_model_profile);
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
        primary_hart().apply_cpu_model_config(model);
        memory().system_bus().configure_timing(model.interconnect.request_latency,
                                               model.interconnect.response_latency);
        if (*config.cpu_model_profile == simrv::pipeline::CpuModelProfile::CfuProvingGround) {
            config.memory.dram_base = 0x00000000;
            if (config.memory.dram_size < 512 * 1024 * 1024) {
                config.memory.dram_size = 512 * 1024 * 1024;
            }
        }
    }
    if (!config.files.cpuconfig_path.empty()) {
        auto model = primary_hart().cpu_model_config;
        if (!simrv::core::load_cpu_config(config.files.cpuconfig_path, model)) {
            const std::string err =
                "Failed to load CPU configuration file: " + config.files.cpuconfig_path;
            simrv::log::error("{}", err);
            return std::unexpected(err);
        }
        primary_hart().apply_cpu_model_config(model);
        memory().system_bus().configure_timing(model.interconnect.request_latency,
                                               model.interconnect.response_latency);
    }
    if (!config.files.cfu_plugin_path.empty()) {
        if (!primary_hart().cfu_unit.load_plugin(config.files.cfu_plugin_path)) {
            const std::string err = "Failed to load CFU plugin: " + config.files.cfu_plugin_path;
            simrv::log::error("{}", err);
            return std::unexpected(err);
        }
    }

    runtime_->rtc = std::make_unique<simrv::Rtc>(*this);
    runtime_->uart = std::make_unique<simrv::device::Uart>(*this);
    runtime_->power = std::make_unique<simrv::device::PowerMmio>(*this);
    if (tui_enabled() || debugger_enabled()) {
        execution_state_.store(ExecutionState::Paused, std::memory_order_release);
    }
    const auto ram = ram_view();
    const size_t effective_dram_size = static_cast<size_t>(ram.size());
    config.memory.dram_size = static_cast<Address>(effective_dram_size);
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

    const std::array<simrv::memory::TileLinkNode*, 9> base_nodes = {
        runtime_->aclint_mtimer.get(), runtime_->aclint_mswi.get(), runtime_->imsic_m.get(),
        runtime_->imsic_s.get(),       runtime_->aplic_m.get(),     runtime_->aplic_s.get(),
        runtime_->rtc.get(),           runtime_->uart.get(),        runtime_->power.get(),
    };
    for (auto* node : base_nodes) {
        if (node != nullptr) memory().system_bus().add_node(node);
    }

    if (runtime_->pcie) {
        memory().system_bus().add_node(&runtime_->pcie->ecam_node());
        memory().system_bus().add_node(&runtime_->pcie->mmio_node());
    }

    const std::array<std::shared_ptr<simrv::device::VirtioMmioDevice>, 7> mmio_devs = {
        runtime_->mmio_disk,  runtime_->mmio_console, runtime_->mmio_rng, runtime_->mmio_gpu,
        runtime_->mmio_input, runtime_->mmio_sound,   runtime_->mmio_net,
    };
    for (const auto& dev : mmio_devs) {
        if (dev) memory().system_bus().add_node(dev.get());
    }

    memory().system_bus().add_node(&primary_hart().plic_mmio);
    memory().system_bus().add_node(&primary_hart().clint_mmio);
    const bool linux_boot = !config.execution.appmode;
    if (linux_boot && effective_dram_size < static_cast<size_t>(0x00100000U)) {
        simrv::log::error("DRAM must be at least 1 MiB for an OS device tree");
        return std::unexpected("DRAM must be at least 1 MiB for an OS device tree");
    }
    const Address dtb_offset =
        linux_boot ? static_cast<Address>(effective_dram_size - static_cast<size_t>(0x00100000U))
                   : simrv::boot::kInitDataAddress;

    CSRValue initial_misa =
        isa::misa_with_mxl(config.isa.misa_override ? config.isa.misa_profile : isa::kMisaDefault);
    if constexpr (simrv::xlen::kIsXLen64) {
        bool is_32bit = false;
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
                            if (header[4] == 1) {  // ELFCLASS32
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
            initial_misa = (initial_misa & ~(3ull << 62)) | (1ull << 62);
        }
    }
    primary_hart().state().pc = resolved_start_pc_;
    primary_hart().state().regs.write(static_cast<RegId>(10), 0);  // a0 = hartid
    primary_hart().state().regs.write(
        static_cast<RegId>(11),
        linux_boot ? (simrv::boot::kStartPc + dtb_offset) : 0);  // a1 = dtb
    primary_hart().state().misa = initial_misa;
    primary_hart().state().priv = kPrivMachine;
    primary_hart().state().regs.vlen = config.isa.vlen ? config.isa.vlen : 256;
    primary_hart().state().initialize_lower_xlen_fields();
    if (primary_hart().state().regs.xlen == 32) {
        primary_hart().state().pc = static_cast<Register>(
            static_cast<int64_t>(static_cast<int32_t>(primary_hart().state().pc)));
    }
    primary_hart().TLB_flush();

    load_image_into_ram(config.files.binary_path, ram_view(), "memory", tui_enabled(), this);
    symbol_table().load_from_elf(
        config.debug.spike_elf.empty() ? config.files.binary_path : config.debug.spike_elf, true,
        runtime_profile.interaction == InteractionMode::Tui
            ? simrv::debug::SymbolLoadMode::FullDebug
            : simrv::debug::SymbolLoadMode::RuntimeEssentials);

    resolve_start_pc_and_dram_base(*this, symbol_table());

    runtime_->secondary_harts.clear();
    if (config.execution.num_harts > 1) {
        runtime_->secondary_harts.reserve(config.execution.num_harts - 1);
        for (uint32_t i = 1; i < config.execution.num_harts; ++i) {
            auto sec_cpu = std::make_unique<simrv::core::CPU>();
            sec_cpu->machine_ = this;
            sec_cpu->apply_cpu_model_config(primary_hart().cpu_model_config);
            if (!config.files.cfu_plugin_path.empty()) {
                sec_cpu->cfu_unit.load_plugin(config.files.cfu_plugin_path);
            }
            sec_cpu->state().mhartid = i;
            sec_cpu->state().misa = initial_misa;
            sec_cpu->state().initialize_lower_xlen_fields();
            // OpenSBI owns HSM for Linux boots.  Secondary harts must execute its M-mode wait
            // loop so its IPI can release them into the Linux entry point; a stopped simulator
            // hart cannot observe that firmware event.
            const bool sec_started = appmode_enabled() || linux_boot;
            sec_cpu->hart_status.store(sec_started ? HartStatus::Started : HartStatus::Stopped,
                                       std::memory_order_relaxed);
            for (std::size_t r = 0; r < 32; ++r) {
                sec_cpu->state().regs.write(static_cast<RegId>(r), 0);
            }
            sec_cpu->state().pc = resolved_start_pc_;
            if (sec_cpu->state().regs.xlen == 32) {
                sec_cpu->state().pc = static_cast<Register>(
                    static_cast<int64_t>(static_cast<int32_t>(sec_cpu->state().pc)));
            }
            sec_cpu->state().priv = kPrivMachine;
            sec_cpu->state().regs.write(static_cast<RegId>(10), i);
            sec_cpu->state().regs.write(static_cast<RegId>(11),
                                        linux_boot ? (simrv::boot::kStartPc + dtb_offset) : 0);
            sec_cpu->soft_tlb_flush();
            sec_cpu->TLB_flush();
            runtime_->secondary_harts.push_back(std::move(sec_cpu));
        }
    }

    if (config.bram_prewarm) {
        prewarm_bram_caches();
    }

    // If launched without a binary in TUI mode, skip image-dependent init —
    // the TUI will open the LoadBinary modal and call load_program_binary() later.
    if (config.files.binary_path.empty() && tui_enabled()) {
        execution_state_.store(ExecutionState::Paused, std::memory_order_release);
        return {};
    }

    if (linux_boot) {
        if (!config.files.dvtree_path.empty()) {
            if (dtb_offset >= effective_dram_size) {
                simrv::log::error("device-tree load offset is outside DRAM");
                return std::unexpected("device-tree load offset is outside DRAM");
            }
            const auto dt_cap = static_cast<std::size_t>(effective_dram_size - dtb_offset);
            // `ram` above captures geometry before allocation. Reacquire the view so the DTB
            // loader receives the live backing pointer as well as the resolved runtime geometry.
            const auto initialized_ram = ram_view();
            const Address dtb_address = initialized_ram.base() + dtb_offset;
            if (!initialized_ram.contains(dtb_address, dt_cap)) {
                simrv::log::error("device-tree region is outside DRAM");
                return std::unexpected("device-tree region is outside DRAM");
            }
            load_image_into_ram(config.files.dvtree_path,
                                {initialized_ram.unchecked_ptr(dtb_address), dtb_address,
                                 static_cast<Address>(dt_cap)},
                                "device-tree", tui_enabled());
        } else {
            const auto composition = platform_composition(config.platform_profile);
            simrv::util::FdtConfig const fdt_cfg{
                .num_harts = config.execution.num_harts,
                .dram_base = config.memory.dram_base,
                .dram_size = effective_dram_size,
                .xlen = simrv::xlen::kXLenBits,
                .enable_pcie = composition.pcie,
                .enable_mmio = composition.mmio,
            };
            auto fdt_blob = simrv::util::FdtGenerator::generate(fdt_cfg);
            if (fdt_blob.size() <= static_cast<std::size_t>(0x00100000U)) {
                std::memcpy(ram_data() + dtb_offset, fdt_blob.data(), fdt_blob.size());
            }
        }
    }

    if (config.files.disk_enabled) {
        if (runtime_->pci_disk) runtime_->pci_disk->load_disk(config.files.disk_path);
        if (runtime_->mmio_disk) runtime_->mmio_disk->load_disk(config.files.disk_path);
    }

    if (instruction_mix_enabled()) {
        primary_hart().e_instmix.fill(0);
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
        // Derive the ISA string from the active MISA profile and compile-time XLEN
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
