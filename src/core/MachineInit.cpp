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
    simrv::memory::g_dram_base = simrv::memory::kDramBaseAddress;

    if (machine.s_start_pc == simrv::boot::kStartPc || machine.s_start_pc == 0) {
        Address entry = symbols.entry_point().value_or(
            symbols.lookup_name("_start").value_or(simrv::boot::kStartPc));
        if (entry < simrv::memory::kDramBaseAddress) {
            entry += simrv::memory::kDramBaseAddress;
        }
        machine.s_start_pc = entry;
    }

    if (auto tohost_sym = symbols.lookup_name("tohost"); tohost_sym.has_value()) {
        machine.s_isatest_tohost = *tohost_sym;
    }

    machine.cpu.state().pc = machine.s_start_pc;
    if (machine.cpu.state().regs.xlen == 32) {
        machine.cpu.state().pc = static_cast<Register>(
            static_cast<int64_t>(static_cast<int32_t>(machine.cpu.state().pc)));
    }
}

void load_image_into_ram(std::string& file_path, Byte* ram, std::size_t capacity,
                         const char* image_name, bool tuimode) {
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
                                const Address dram_base = simrv::memory::kDramBaseAddress;
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
                                const Address dram_base = simrv::memory::kDramBaseAddress;
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

auto Machine::initialize() -> int {
    if (!s_fn_cpuconfig.empty()) {
        if (!simrv::core::load_cpu_config(s_fn_cpuconfig, cpu.pipeline_sim.config)) {
            simrv::log::error("Failed to load CPU configuration file: {}", s_fn_cpuconfig);
            return 1;
        }
    }

    rtc = std::make_unique<simrv::Rtc>(*this);
    uart = std::make_unique<simrv::device::Uart>(*this);
    power = std::make_unique<simrv::device::PowerMmio>(*this);
    if (s_tuimode) {
        tui = std::make_unique<simrv::tui::Tui>(*this);
    }
    const size_t effective_dram_size = (s_dram_size != 0)
                                           ? static_cast<size_t>(s_dram_size)
                                           : static_cast<size_t>(simrv::memory::kDramSize);
    mmem_owner_.reset(static_cast<Byte*>(std::calloc(
        effective_dram_size,
        sizeof(Byte))));  // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    if (mmem_owner_ == nullptr) {
        simrv::log::error("Failed to allocate main memory ({} bytes)", effective_dram_size);
        return 1;
    }
    mmem = mmem_owner_.get();

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
    const bool enable_pcie = (s_platform_profile == PlatformProfile::Pcie ||
                              s_platform_profile == PlatformProfile::Hybrid);
    const bool enable_mmio = (s_platform_profile == PlatformProfile::Mmio ||
                              s_platform_profile == PlatformProfile::Hybrid);

    if (enable_pcie) {
        pcie = std::make_unique<simrv::device::PcieRootComplex>(this, aplic_s.get(), imsic_s.get());
        pci_disk = std::make_shared<simrv::device::VirtioPciBlock>(s_fn_dskimg);
        pci_console = std::make_shared<simrv::device::VirtioPciConsole>();
        pci_rng = std::make_shared<simrv::device::VirtioPciRng>();
        pci_gpu = std::make_shared<simrv::device::VirtioPciGpu>();
        pci_input = std::make_shared<simrv::device::VirtioPciInput>();
        pci_sound = std::make_shared<simrv::device::VirtioPciSound>();
        pci_net = std::make_shared<simrv::device::VirtioPciNet>();

        pcie->attach_device(0, 1, 0, pci_disk);
        pcie->attach_device(0, 2, 0, pci_console);
        pcie->attach_device(0, 3, 0, pci_rng);
        pcie->attach_device(0, 4, 0, pci_gpu);
        pcie->attach_device(0, 5, 0, pci_input);
        pcie->attach_device(0, 6, 0, pci_sound);
        pcie->attach_device(0, 7, 0, pci_net);
    }

    if (enable_mmio) {
        mmio_disk =
            std::make_shared<simrv::device::VirtioMmioBlock>(0x10001000, 2, this, s_fn_dskimg);
        mmio_console = std::make_shared<simrv::device::VirtioMmioConsole>(0x10002000, 1, this);
        mmio_rng = std::make_shared<simrv::device::VirtioMmioRng>(0x10003000, 4, this);
        mmio_gpu = std::make_shared<simrv::device::VirtioMmioGpu>(0x10004000, 5, this);
        mmio_input = std::make_shared<simrv::device::VirtioMmioInput>(0x10005000, 6, this);
        mmio_sound = std::make_shared<simrv::device::VirtioMmioSound>(0x10006000, 7, this);
        mmio_net = std::make_shared<simrv::device::VirtioMmioNet>(0x10007000, 8, this);
    }

    memory_.system_bus().add_node(aclint_mtimer.get());
    memory_.system_bus().add_node(aclint_mswi.get());
    memory_.system_bus().add_node(imsic_m.get());
    memory_.system_bus().add_node(imsic_s.get());
    memory_.system_bus().add_node(aplic_m.get());
    memory_.system_bus().add_node(aplic_s.get());

    if (pcie) {
        memory_.system_bus().add_node(&pcie->ecam_node());
        memory_.system_bus().add_node(&pcie->mmio_node());
    }

    if (mmio_disk) memory_.system_bus().add_node(mmio_disk.get());
    if (mmio_console) memory_.system_bus().add_node(mmio_console.get());
    if (mmio_rng) memory_.system_bus().add_node(mmio_rng.get());
    if (mmio_gpu) memory_.system_bus().add_node(mmio_gpu.get());
    if (mmio_input) memory_.system_bus().add_node(mmio_input.get());
    if (mmio_sound) memory_.system_bus().add_node(mmio_sound.get());
    if (mmio_net) memory_.system_bus().add_node(mmio_net.get());

    memory_.system_bus().add_node(rtc.get());
    memory_.system_bus().add_node(uart.get());
    memory_.system_bus().add_node(power.get());
    memory_.system_bus().add_node(&cpu.plic_mmio);
    memory_.system_bus().add_node(&cpu.clint_mmio);
    const bool linux_boot = !s_appmode;
    if (s_appmode) {
        s_enabletimer = 0;
    }
    const Address dtb_offset =
        linux_boot
            ? static_cast<Address>(simrv::memory::kDramSize - static_cast<Address>(0x00100000U))
            : simrv::boot::kInitDataAddress;

    CSRValue initial_misa =
        isa::misa_with_mxl(s_misa_override ? s_misa_profile : isa::kMisaDefault);
    if constexpr (simrv::xlen::kIsXLen64) {
        bool is_32bit = false;
        if (s_misa_override && s_misa_xlen == 32) {
            is_32bit = true;
        } else if (!s_misa_override || s_misa_xlen == 0) {
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
            check_elf(s_fn_memimg);
            check_elf(s_spike_elf);
            if (!is_32bit && !s_fn_memimg.empty()) {
                std::string base_path = s_fn_memimg;
                size_t last_dot = base_path.find_last_of('.');
                size_t last_slash = base_path.find_last_of("/\\");
                if (last_dot != std::string::npos &&
                    (last_slash == std::string::npos || last_dot > last_slash)) {
                    base_path = base_path.substr(0, last_dot);
                }
                if (base_path != s_fn_memimg) {
                    check_elf(base_path + ".elf");
                    check_elf(base_path + ".ELF");
                    check_elf(base_path + ".out");
                    check_elf(base_path + ".OUT");
                    check_elf(base_path + ".axf");
                    check_elf(base_path + ".AXF");
                    check_elf(base_path);
                }
            }
            if (!is_32bit) {
                if (s_fn_memimg.find("rv32") != std::string::npos ||
                    s_spike_elf.find("rv32") != std::string::npos) {
                    is_32bit = true;
                }
            }
        }
        if (is_32bit) {
            initial_misa = (initial_misa & ~(3ull << 62)) | (1ull << 62);
        }
    }
    cpu.state().pc = s_start_pc;
    cpu.state().regs.write(static_cast<RegId>(10), 0);  // a0 = hartid
    cpu.state().regs.write(static_cast<RegId>(11),
                           linux_boot ? (simrv::boot::kStartPc + dtb_offset) : 0);  // a1 = dtb
    cpu.state().misa = initial_misa;
    cpu.state().priv = kPrivMachine;
    cpu.state().regs.vlen = s_vlen ? s_vlen : 256;
    cpu.state().initialize_lower_xlen_fields();
    if (cpu.state().regs.xlen == 32) {
        cpu.state().pc =
            static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(cpu.state().pc)));
    }
    cpu.TLB_flush();

    load_image_into_ram(s_fn_memimg, mmem, static_cast<std::size_t>(simrv::memory::kDramSize),
                        "memory", s_tuimode);
    symbols.load_from_elf(s_spike_elf.empty() ? s_fn_memimg : s_spike_elf, true,
                          runtime_profile.interaction == InteractionMode::Tui
                              ? simrv::debug::SymbolLoadMode::FullDebug
                              : simrv::debug::SymbolLoadMode::RuntimeEssentials);

    resolve_start_pc_and_dram_base(*this, symbols);

    secondary_harts_.clear();
    if (s_num_harts > 1) {
        secondary_harts_.reserve(s_num_harts - 1);
        for (uint32_t i = 1; i < s_num_harts; ++i) {
            auto sec_cpu = std::make_unique<simrv::core::CPU>();
            sec_cpu->machine_ = this;
            sec_cpu->pipeline_sim.config = cpu.pipeline_sim.config;
            sec_cpu->state().mhartid = i;
            sec_cpu->state().misa = initial_misa;
            sec_cpu->state().initialize_lower_xlen_fields();
            sec_cpu->use_opensbi = cpu.use_opensbi;
            sec_cpu->hart_status.store(HartStatus::Started, std::memory_order_relaxed);
            for (std::size_t r = 0; r < 32; ++r) {
                sec_cpu->state().regs.write(static_cast<RegId>(r), 0);
            }
            sec_cpu->state().pc = s_start_pc;
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
    if (s_fn_memimg.empty() && s_tuimode) {
        if (tui) {
            tui->initialize();
        }
        return 0;
    }

    if (linux_boot) {
        if (!s_fn_dvtree.empty()) {
            if (dtb_offset >= simrv::memory::kDramSize) {
                simrv::log::error("device-tree load offset is outside DRAM");
                return 1;
            }
            const auto dt_cap = static_cast<std::size_t>(simrv::memory::kDramSize - dtb_offset);
            load_image_into_ram(s_fn_dvtree, mmem + dtb_offset, dt_cap, "device-tree", s_tuimode);
        } else {
            const bool enable_pcie = (s_platform_profile == PlatformProfile::Pcie ||
                                      s_platform_profile == PlatformProfile::Hybrid);
            const bool enable_mmio = (s_platform_profile == PlatformProfile::Mmio ||
                                      s_platform_profile == PlatformProfile::Hybrid);
            simrv::util::FdtConfig const fdt_cfg{
                .num_harts = s_num_harts,
                .dram_base = simrv::memory::g_dram_base,
                .dram_size = (s_dram_size != 0) ? s_dram_size : simrv::memory::kDramSize,
                .xlen = simrv::xlen::kXLenBits,
                .enable_pcie = enable_pcie,
                .enable_mmio = enable_mmio,
            };
            auto fdt_blob = simrv::util::FdtGenerator::generate(fdt_cfg);
            if (fdt_blob.size() <= static_cast<std::size_t>(0x00100000U)) {
                std::memcpy(mmem + dtb_offset, fdt_blob.data(), fdt_blob.size());
            }
        }
    }

    if (s_use_disk) {
        if (pci_disk) pci_disk->load_disk(s_fn_dskimg);
        if (mmio_disk) mmio_disk->load_disk(s_fn_dskimg);
    }

    if (s_use_mix) {
        cpu.e_instmix.fill(0);
    }

    // ---- GDB stub initialization ----
    if (s_gdb_mode) {
        try {
            gdb_stub = std::make_unique<simrv::debug::GdbStub>(s_gdb_port);
            simrv::log::info("GDB stub listening on port {} — waiting for connection…", s_gdb_port);
            gdb_stub->wait_for_connection();
            simrv::log::info("GDB client connected");
        } catch (const std::exception& ex) {
            simrv::log::error("GDB stub init failed: {}", ex.what());
            return 1;
        }
    }

    // ---- Spike lockstep initialization ----
    if (s_lockstep_mode) {
        // Derive the ISA string from the active MISA profile and compile-time XLEN
        const std::string isa_str = simrv::debug::spike_isa_string(cpu.state().misa);
        const std::string spike_img = s_spike_elf.empty() ? s_fn_memimg : s_spike_elf;
        spike_lockstep = std::make_unique<simrv::debug::SpikeLockstep>(
            s_spike_bin, spike_img, s_fn_dskimg, s_fn_dvtree, isa_str);
        simrv::log::info("Spike lockstep co-simulation active (isa={})", isa_str);
        if (!spike_lockstep->start()) {
            simrv::log::error("Failed to launch Spike for lockstep verification");
            return 1;
        }
    }

    if (s_tuimode && tui) {
        tui->initialize();
    }

    return 0;
}

auto Machine::load_program_binary(const std::string& filepath) -> bool {
    if (filepath.empty()) {
        return false;
    }
    s_fn_memimg = filepath;
    load_image_into_ram(s_fn_memimg, mmem, static_cast<std::size_t>(simrv::memory::kDramSize),
                        "memory", s_tuimode);
    symbols.load_from_elf(s_spike_elf.empty() ? s_fn_memimg : s_spike_elf, true,
                          runtime_profile.interaction == InteractionMode::Tui
                              ? simrv::debug::SymbolLoadMode::FullDebug
                              : simrv::debug::SymbolLoadMode::RuntimeEssentials);

    CSRValue initial_misa =
        isa::misa_with_mxl(s_misa_override ? s_misa_profile : isa::kMisaDefault);
    if constexpr (simrv::xlen::kIsXLen64) {
        bool is_32bit = false;
        if (s_misa_override && s_misa_xlen == 32) {
            is_32bit = true;
        } else if (!s_misa_override || s_misa_xlen == 0) {
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
            check_elf(s_fn_memimg);
            check_elf(s_spike_elf);
            if (!is_32bit && !s_fn_memimg.empty()) {
                std::string base_path = s_fn_memimg;
                size_t last_dot = base_path.find_last_of('.');
                size_t last_slash = base_path.find_last_of("/\\");
                if (last_dot != std::string::npos &&
                    (last_slash == std::string::npos || last_dot > last_slash)) {
                    base_path = base_path.substr(0, last_dot);
                }
                if (base_path != s_fn_memimg) {
                    check_elf(base_path + ".elf");
                    check_elf(base_path + ".ELF");
                    check_elf(base_path + ".out");
                    check_elf(base_path + ".OUT");
                    check_elf(base_path + ".axf");
                    check_elf(base_path + ".AXF");
                    check_elf(base_path);
                }
            }
            if (!is_32bit) {
                if (s_fn_memimg.find("rv32") != std::string::npos ||
                    s_spike_elf.find("rv32") != std::string::npos) {
                    is_32bit = true;
                }
            }
        }
        if (is_32bit) {
            initial_misa = (initial_misa & ~(3ull << 62)) | (1ull << 62);
        }
    }

    cpu.state().misa = initial_misa;
    cpu.state().initialize_lower_xlen_fields();

    resolve_start_pc_and_dram_base(*this, symbols);

    cpu.soft_tlb_flush();
    cpu.TLB_flush();
    cpu.icache.flush(true);
    cpu.dcache.flush(true);
    cpu.decode_cache.flush();
    cpu.pipeline_context = simrv::pipeline::PipelineContext{};
    cpu.undo_stack.clear();
    cpu.trace_history_head_ = 0;
    cpu.trace_history_size_ = 0;
    cpu.state().load_res = 0;

    for (std::size_t r = 0; r < 32; ++r) {
        cpu.state().regs.write(static_cast<RegId>(r), 0);
    }
    cpu.state().pc = s_start_pc;
    if (cpu.state().regs.xlen == 32) {
        cpu.state().pc =
            static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(cpu.state().pc)));
    }
    cpu.state().priv = kPrivMachine;
    cpu.state().regs.write(static_cast<RegId>(10), 0);
    const bool linux_boot = !s_appmode;
    const Address dtb_offset =
        linux_boot
            ? static_cast<Address>(simrv::memory::kDramSize - static_cast<Address>(0x00100000U))
            : simrv::boot::kInitDataAddress;
    cpu.state().regs.write(static_cast<RegId>(11),
                           linux_boot ? (simrv::boot::kStartPc + dtb_offset) : 0);

    tohost = 0;
    exit_code = 0;
    is_shutdown_ = false;
    is_running_ = true;

    if (linux_boot) {
        if (!s_fn_dvtree.empty()) {
            const auto dt_cap = static_cast<std::size_t>(simrv::memory::kDramSize - dtb_offset);
            load_image_into_ram(s_fn_dvtree, mmem + dtb_offset, dt_cap, "device-tree", s_tuimode);
        } else {
            const bool enable_pcie = (s_platform_profile == PlatformProfile::Pcie ||
                                      s_platform_profile == PlatformProfile::Hybrid);
            const bool enable_mmio = (s_platform_profile == PlatformProfile::Mmio ||
                                      s_platform_profile == PlatformProfile::Hybrid);
            simrv::util::FdtConfig const fdt_cfg{
                .num_harts = s_num_harts,
                .dram_base = simrv::memory::g_dram_base,
                .dram_size = (s_dram_size != 0) ? s_dram_size : simrv::memory::kDramSize,
                .xlen = simrv::xlen::kXLenBits,
                .enable_pcie = enable_pcie,
                .enable_mmio = enable_mmio,
            };
            auto fdt_blob = simrv::util::FdtGenerator::generate(fdt_cfg);
            if (fdt_blob.size() <= static_cast<std::size_t>(0x00100000U)) {
                std::memcpy(mmem + dtb_offset, fdt_blob.data(), fdt_blob.size());
            }
        }
    }
    secondary_harts_.clear();
    if (s_num_harts > 1) {
        secondary_harts_.reserve(s_num_harts - 1);
        for (uint32_t i = 1; i < s_num_harts; ++i) {
            auto sec_cpu = std::make_unique<simrv::core::CPU>();
            sec_cpu->machine_ = this;
            sec_cpu->pipeline_sim.config = cpu.pipeline_sim.config;
            sec_cpu->state().mhartid = i;
            sec_cpu->state().misa = initial_misa;
            sec_cpu->state().initialize_lower_xlen_fields();
            sec_cpu->hart_status.store(s_appmode ? HartStatus::Started : HartStatus::Stopped,
                                       std::memory_order_relaxed);
            for (std::size_t r = 0; r < 32; ++r) {
                sec_cpu->state().regs.write(static_cast<RegId>(r), 0);
            }
            sec_cpu->state().pc = s_start_pc;
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
    cpu.soft_tlb_flush();
    cpu.TLB_flush();
    return true;
}

auto Machine::load_disk_image(const std::string& filepath) -> bool {
    if (filepath.empty()) {
        return false;
    }
    s_fn_dskimg = filepath;
    s_use_disk = true;
    bool ok = true;
    if (pci_disk) {
        ok &= pci_disk->load_disk(filepath);
    }
    if (mmio_disk) {
        ok &= mmio_disk->load_disk(filepath);
    }
    return ok;
}

}  // namespace simrv::core
