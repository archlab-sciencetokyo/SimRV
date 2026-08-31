#include <elf.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

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
#include "simrv/tui/Tui.hpp"
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
        if (entry < simrv::memory::kDramBaseAddress) {
            entry += simrv::memory::kDramBaseAddress;
        }
        start_pc = entry;
    }
    machine.set_resolved_boot_state(start_pc, symbols.lookup_name("tohost"));
    machine.primary_hart().state().pc = machine.resolved_start_pc();
    if (machine.primary_hart().state().regs.xlen == 32) {
        machine.primary_hart().state().pc = static_cast<Register>(
            static_cast<int64_t>(static_cast<int32_t>(machine.primary_hart().state().pc)));
    }
}

void load_image_into_ram(std::string& file_path, simrv::memory::RamView ram_view,
                         const char* image_name, bool tuimode) {
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
    }
}

}  // namespace

auto Machine::platform_status() const -> PlatformStatusSnapshot {
    PlatformStatusSnapshot snapshot{.profile = platform_profile(),
                                    .has_pcie = pcie != nullptr,
                                    .has_mmio = mmio_disk != nullptr};
    if (pci_disk) {
        snapshot.disk_loaded = pci_disk->is_disk_loaded();
        snapshot.disk_status = pci_disk->device_status();
        snapshot.disk_isr = pci_disk->isr_status();
        snapshot.disk_capacity_sectors = pci_disk->capacity_sectors();
    } else if (mmio_disk) {
        snapshot.disk_loaded = mmio_disk->is_disk_loaded();
        snapshot.disk_status = mmio_disk->device_status();
        snapshot.disk_isr = mmio_disk->isr_status();
        snapshot.disk_capacity_sectors = mmio_disk->capacity_sectors();
    }
    if (pci_net) {
        snapshot.network_status = pci_net->device_status();
        snapshot.network_tx_packets = pci_net->backend().tx_packet_count();
    } else if (mmio_net) {
        snapshot.network_status = mmio_net->device_status();
        snapshot.network_tx_packets = mmio_net->backend().tx_packet_count();
    }
    if (pci_console)
        snapshot.console_status = pci_console->device_status();
    else if (mmio_console)
        snapshot.console_status = mmio_console->device_status();
    snapshot.rng_status =
        pci_rng ? pci_rng->device_status() : (mmio_rng ? mmio_rng->device_status() : 0);
    snapshot.gpu_status =
        pci_gpu ? pci_gpu->device_status() : (mmio_gpu ? mmio_gpu->device_status() : 0);
    return snapshot;
}

auto Machine::initialize() -> int {
    if (!config.files.cpuconfig_path.empty()) {
        auto model = cpu.cpu_model_config;
        if (!simrv::core::load_cpu_config(config.files.cpuconfig_path, model)) {
            simrv::log::error("Failed to load CPU configuration file: {}",
                              config.files.cpuconfig_path);
            return 1;
        }
        cpu.apply_cpu_model_config(model);
        memory_.system_bus().configure_timing(model.interconnect.request_latency,
                                              model.interconnect.response_latency);
    }

    rtc = std::make_unique<simrv::Rtc>(*this);
    uart = std::make_unique<simrv::device::Uart>(*this);
    power = std::make_unique<simrv::device::PowerMmio>(*this);
    if (tui_enabled()) {
        tui = std::make_unique<simrv::tui::Tui>(*this);
    }
    const auto ram = ram_view();
    const size_t effective_dram_size = static_cast<size_t>(ram.size());
    config.memory.dram_size = static_cast<Address>(effective_dram_size);
    if (!allocate_ram(effective_dram_size)) {
        simrv::log::error("Failed to allocate main memory ({} bytes)", effective_dram_size);
        return 1;
    }

    memory_.initialize_mmu();

    aclint_mtimer = std::make_unique<simrv::device::AclintMtimer>(this);
    aclint_mswi = std::make_unique<simrv::device::AclintMswi>(this);
    imsic_m = std::make_unique<simrv::device::Imsic>(this, simrv::device::Imsic::Privilege::Machine,
                                                     simrv::mmio::kImsicMBaseAddress,
                                                     simrv::mmio::kImsicMSize);
    imsic_s = std::make_unique<simrv::device::Imsic>(
        this, simrv::device::Imsic::Privilege::Supervisor, simrv::mmio::kImsicSBaseAddress,
        simrv::mmio::kImsicSSize);
    aplic_m = std::make_unique<simrv::device::Aplic>(this, simrv::device::Aplic::Privilege::Machine,
                                                     simrv::mmio::kAplicMBaseAddress,
                                                     simrv::mmio::kAplicMSize, imsic_m.get());
    aplic_s = std::make_unique<simrv::device::Aplic>(
        this, simrv::device::Aplic::Privilege::Supervisor, simrv::mmio::kAplicSBaseAddress,
        simrv::mmio::kAplicSSize, imsic_s.get());
    PlatformBuilder::compose(*this);

    const std::array<simrv::memory::TileLinkNode*, 9> base_nodes = {
        aclint_mtimer.get(), aclint_mswi.get(), imsic_m.get(), imsic_s.get(), aplic_m.get(),
        aplic_s.get(),       rtc.get(),         uart.get(),    power.get(),
    };
    for (auto* node : base_nodes) {
        if (node != nullptr) memory_.system_bus().add_node(node);
    }

    if (pcie) {
        memory_.system_bus().add_node(&pcie->ecam_node());
        memory_.system_bus().add_node(&pcie->mmio_node());
    }

    const std::array<std::shared_ptr<simrv::device::VirtioMmioDevice>, 7> mmio_devs = {
        mmio_disk, mmio_console, mmio_rng, mmio_gpu, mmio_input, mmio_sound, mmio_net,
    };
    for (const auto& dev : mmio_devs) {
        if (dev) memory_.system_bus().add_node(dev.get());
    }

    memory_.system_bus().add_node(&cpu.plic_mmio);
    memory_.system_bus().add_node(&cpu.clint_mmio);
    const bool linux_boot = !config.execution.appmode;
    if (linux_boot && effective_dram_size < static_cast<size_t>(0x00100000U)) {
        simrv::log::error("DRAM must be at least 1 MiB for an OS device tree");
        return 1;
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
    cpu.state().pc = resolved_start_pc_;
    cpu.state().regs.write(static_cast<RegId>(10), 0);  // a0 = hartid
    cpu.state().regs.write(static_cast<RegId>(11),
                           linux_boot ? (simrv::boot::kStartPc + dtb_offset) : 0);  // a1 = dtb
    cpu.state().misa = initial_misa;
    cpu.state().priv = kPrivMachine;
    cpu.state().regs.vlen = config.isa.vlen ? config.isa.vlen : 256;
    cpu.state().initialize_lower_xlen_fields();
    if (cpu.state().regs.xlen == 32) {
        cpu.state().pc =
            static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(cpu.state().pc)));
    }
    cpu.TLB_flush();

    load_image_into_ram(config.files.binary_path, ram_view(), "memory", tui_enabled());
    symbols.load_from_elf(
        config.debug.spike_elf.empty() ? config.files.binary_path : config.debug.spike_elf, true,
        runtime_profile.interaction == InteractionMode::Tui
            ? simrv::debug::SymbolLoadMode::FullDebug
            : simrv::debug::SymbolLoadMode::RuntimeEssentials);

    resolve_start_pc_and_dram_base(*this, symbols);

    secondary_harts_.clear();
    if (config.execution.num_harts > 1) {
        secondary_harts_.reserve(config.execution.num_harts - 1);
        for (uint32_t i = 1; i < config.execution.num_harts; ++i) {
            auto sec_cpu = std::make_unique<simrv::core::CPU>();
            sec_cpu->machine_ = this;
            sec_cpu->apply_cpu_model_config(cpu.cpu_model_config);
            sec_cpu->state().mhartid = i;
            sec_cpu->state().misa = initial_misa;
            sec_cpu->state().initialize_lower_xlen_fields();
            const bool sec_started = appmode_enabled();
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
            secondary_harts_.push_back(std::move(sec_cpu));
        }
    }

    // If launched without a binary in TUI mode, skip image-dependent init —
    // the TUI will open the LoadBinary modal and call load_program_binary() later.
    if (config.files.binary_path.empty() && tui_enabled()) {
        if (tui) {
            tui->initialize();
        }
        return 0;
    }

    if (linux_boot) {
        if (!config.files.dvtree_path.empty()) {
            if (dtb_offset >= effective_dram_size) {
                simrv::log::error("device-tree load offset is outside DRAM");
                return 1;
            }
            const auto dt_cap = static_cast<std::size_t>(effective_dram_size - dtb_offset);
            // `ram` above captures geometry before allocation. Reacquire the view so the DTB
            // loader receives the live backing pointer as well as the resolved runtime geometry.
            const auto initialized_ram = ram_view();
            const Address dtb_address = initialized_ram.base() + dtb_offset;
            if (!initialized_ram.contains(dtb_address, dt_cap)) {
                simrv::log::error("device-tree region is outside DRAM");
                return 1;
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
                std::memcpy(mmem + dtb_offset, fdt_blob.data(), fdt_blob.size());
            }
        }
    }

    if (config.files.disk_enabled) {
        if (pci_disk) pci_disk->load_disk(config.files.disk_path);
        if (mmio_disk) mmio_disk->load_disk(config.files.disk_path);
    }

    if (instruction_mix_enabled()) {
        cpu.e_instmix.fill(0);
    }

    // ---- GDB stub initialization ----
    if (debugger_enabled()) {
        try {
            gdb_stub = std::make_unique<simrv::debug::GdbStub>(debugger_port());
            simrv::log::info("GDB stub listening on port {} — waiting for connection…",
                             debugger_port());
            gdb_stub->wait_for_connection();
            simrv::log::info("GDB client connected");
        } catch (const std::exception& ex) {
            simrv::log::error("GDB stub init failed: {}", ex.what());
            return 1;
        }
    }

    // ---- Spike lockstep initialization ----
    if (lockstep_enabled()) {
        // Derive the ISA string from the active MISA profile and compile-time XLEN
        const std::string isa_str = simrv::debug::spike_isa_string(cpu.state().misa);
        const std::string spike_img = spike_elf().empty() ? binary_path() : spike_elf();
        spike_lockstep = std::make_unique<simrv::debug::SpikeLockstep>(
            spike_binary(), spike_img, disk_path(), config.files.dvtree_path, isa_str);
        simrv::log::info("Spike lockstep co-simulation active (isa={})", isa_str);
        if (!spike_lockstep->start()) {
            simrv::log::error("Failed to launch Spike for lockstep verification");
            return 1;
        }
    }

    if (tui_enabled() && tui) {
        tui->initialize();
    }

    return 0;
}

auto Machine::load_program_binary(const std::string& filepath) -> bool {
    if (filepath.empty()) {
        return false;
    }
    auto next = configuration();
    next.files.binary_path = filepath;
    return stage_reconfiguration(std::move(next)).has_value();
}

auto Machine::load_disk_image(const std::string& filepath) -> bool {
    if (filepath.empty()) {
        return false;
    }
    auto next = configuration();
    next.files.disk_path = filepath;
    next.files.disk_enabled = true;
    return stage_reconfiguration(std::move(next)).has_value();
}

}  // namespace simrv::core
