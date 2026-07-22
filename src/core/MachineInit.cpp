/**
 * @file MachineInit.cpp
 * @brief Machine initialization and image loading routines.
 */
#include "simrv/core/Logger.hpp"
#include "simrv/debug/GdbStub.hpp"
#include "simrv/debug/SpikeLockstep.hpp"
#include <elf.h>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <string>

#include "simrv/Define.hpp"
#include "simrv/core/Boot.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/device/Console.hpp"
#include "simrv/device/Disk.hpp"
#include "simrv/device/Rtc.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/device/Virtio.hpp"
#include "simrv/device/Power.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/device/InputDevice.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/Types.hpp"
#include "simrv/core/CpuConfigParser.hpp"

namespace simrv::core {

namespace {

constexpr size_t D_SIZE_DRAM = (size_t{9} * 1024U * 1024U);   // 9MB of bbl + kernel
constexpr size_t D_SIZE_DEVT = (size_t{4} * 1024U);           // 4KB of device tree
constexpr size_t D_SIZE_DISK = (size_t{16} * 1024U * 1024U);  // 16MB of disk image
constexpr Address D_DEVT_OFFSET = static_cast<Address>(16U * 1024U * 1024U);

void resolve_start_pc_and_dram_base(simrv::core::Machine& machine,
                                     const simrv::debug::SymbolTable& symbols) {
    simrv::memory::g_dram_base = simrv::memory::kDramBaseAddress;

    if (machine.s_start_pc == simrv::boot::kStartPc || machine.s_start_pc == 0) {
        if (symbols.entry_point().has_value()) {
            const Address entry = *symbols.entry_point();
            if (entry >= simrv::memory::kDramBaseAddress &&
                entry < simrv::memory::kDramBaseAddress + simrv::memory::kDramSize) {
                machine.s_start_pc = entry;
            } else if (entry < simrv::memory::kDramSize) {
                machine.s_start_pc = simrv::memory::kDramBaseAddress + entry;
            }
        } else if (auto start_sym = symbols.lookup_name("_start"); start_sym.has_value()) {
            const Address entry = *start_sym;
            if (entry >= simrv::memory::kDramBaseAddress &&
                entry < simrv::memory::kDramBaseAddress + simrv::memory::kDramSize) {
                machine.s_start_pc = entry;
            } else if (entry < simrv::memory::kDramSize) {
                machine.s_start_pc = simrv::memory::kDramBaseAddress + entry;
            }
        } else {
            machine.s_start_pc = simrv::boot::kStartPc;
        }
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
            simrv::log::warn("{} image file '{}' not found. Launching TUI in idle state.", image_name, file_path);
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
        std::array<char, 5> ident{};
        if (in.read(ident.data(), 5)) {
            const auto elf_class = static_cast<uint8_t>(ident[4]);
            constexpr uint8_t expected_class = simrv::xlen::kXLenBits == 32 ? 1 : 2;
            if (elf_class != expected_class) {
                simrv::log::warn("Loaded ELF image {} is {}-bit but SimRV is compiled for {}-bit!", 
                             file_path, elf_class == 1 ? 32 : 64, simrv::xlen::kXLenBits);
            }
        }
        in.seekg(0, std::ios::beg);

        bool loaded_segment = false;
        const char class_byte = ident[4];
        if (class_byte == 1) { // 32-bit ELF
            Elf32_Ehdr ehdr{};
            if (in.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr))) {
                std::vector<Elf32_Phdr> phdrs(ehdr.e_phnum);
                in.seekg(ehdr.e_phoff, std::ios::beg);
                if (in.read(reinterpret_cast<char*>(phdrs.data()), static_cast<std::streamsize>(static_cast<size_t>(ehdr.e_phnum) * sizeof(Elf32_Phdr)))) {
                    for (const auto& phdr : phdrs) {
                        if (phdr.p_type == PT_LOAD && phdr.p_filesz > 0) {
                            const Address paddr = phdr.p_paddr != 0 ? phdr.p_paddr : phdr.p_vaddr;
                            const Address dram_base = simrv::memory::kDramBaseAddress;
                            Address dest_offset = 0;
                            if (paddr >= dram_base && (paddr - dram_base) < capacity) {
                                dest_offset = paddr - dram_base;
                            } else if (paddr < capacity) {
                                dest_offset = paddr;
                            } else {
                                continue;
                            }
                            const size_t copy_bytes = std::min<size_t>(phdr.p_filesz, capacity - dest_offset);
                            in.seekg(phdr.p_offset, std::ios::beg);
                            if (in.read(reinterpret_cast<char*>(ram + dest_offset), static_cast<std::streamsize>(copy_bytes))) {
                                loaded_segment = true;
                                if (phdr.p_memsz > phdr.p_filesz && (dest_offset + phdr.p_filesz) < capacity) {
                                    const size_t bss_bytes = std::min<size_t>(phdr.p_memsz - phdr.p_filesz, capacity - (dest_offset + phdr.p_filesz));
                                    std::memset(ram + dest_offset + phdr.p_filesz, 0, bss_bytes);
                                }
                            }
                        }
                    }
                }
            }
        } else if (class_byte == 2) { // 64-bit ELF
            Elf64_Ehdr ehdr{};
            if (in.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr))) {
                std::vector<Elf64_Phdr> phdrs(ehdr.e_phnum);
                in.seekg(static_cast<std::streamoff>(ehdr.e_phoff), std::ios::beg);
                if (in.read(reinterpret_cast<char*>(phdrs.data()), static_cast<std::streamsize>(static_cast<size_t>(ehdr.e_phnum) * sizeof(Elf64_Phdr)))) {
                    for (const auto& phdr : phdrs) {
                        if (phdr.p_type == PT_LOAD && phdr.p_filesz > 0) {
                            const Address paddr = phdr.p_paddr != 0 ? phdr.p_paddr : phdr.p_vaddr;
                            const Address dram_base = simrv::memory::kDramBaseAddress;
                            Address dest_offset = 0;
                            if (paddr >= dram_base && (paddr - dram_base) < capacity) {
                                dest_offset = paddr - dram_base;
                            } else if (paddr < capacity) {
                                dest_offset = paddr;
                            } else {
                                continue;
                            }
                            const size_t copy_bytes = std::min<size_t>(phdr.p_filesz, capacity - dest_offset);
                            in.seekg(static_cast<std::streamoff>(phdr.p_offset), std::ios::beg);
                            if (in.read(reinterpret_cast<char*>(ram + dest_offset), static_cast<std::streamsize>(copy_bytes))) {
                                loaded_segment = true;
                                if (phdr.p_memsz > phdr.p_filesz && (dest_offset + phdr.p_filesz) < capacity) {
                                    const size_t bss_bytes = std::min<size_t>(phdr.p_memsz - phdr.p_filesz, capacity - (dest_offset + phdr.p_filesz));
                                    std::memset(ram + dest_offset + phdr.p_filesz, 0, bss_bytes);
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!loaded_segment) {
            in.seekg(0, std::ios::beg);
            if (!in.read(reinterpret_cast<char*>(ram), static_cast<std::streamsize>(std::min(file_size, capacity)))) {
                simrv::log::error("Failed to read {} image {}", image_name, file_path);
                std::exit(EXIT_FAILURE);
            }
        }
    } else {
        if (file_size > capacity) {
            simrv::log::error("{} image {} is too large ({} bytes > {} bytes capacity)",
                         image_name, file_path, file_size, capacity);
            std::exit(EXIT_FAILURE);
        }
        if (!in.read(reinterpret_cast<char*>(ram), static_cast<std::streamsize>(file_size))) {
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

    disk = std::make_unique<simrv::device::Disk>(*this);
    console = std::make_unique<simrv::device::Console>(*this);
    rtc = std::make_unique<simrv::Rtc>(*this);
    uart = std::make_unique<simrv::device::Uart>(*this);
    power = std::make_unique<simrv::device::PowerMmio>(*this);
    framebuffer = std::make_unique<simrv::device::Framebuffer>(*this);
    input_device = std::make_unique<simrv::device::InputDevice>(*this);
    sdl_display = std::make_unique<simrv::util::SdlDisplay>(*this);
    if (s_gui_mode) {
        sdl_display->init();
    }
    audio = std::make_unique<simrv::device::Audio>(*this);
    sdl_audio = std::make_unique<simrv::util::SdlAudio>(*this);
    if (s_gui_mode) {
        sdl_audio->init_audio();
    }
    if (s_tuimode) {
        tui = std::make_unique<simrv::tui::Tui>(*this);
        tui->initialize();
    }
    mmem_owner_.reset(static_cast<Byte*>(std::calloc(simrv::memory::kDramSize, sizeof(Byte)))); // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    if (mmem_owner_ == nullptr) {
        simrv::log::error("Failed to allocate main memory ({} bytes)",
                     static_cast<std::size_t>(simrv::memory::kDramSize));
        return 1;
    }
    mmem = mmem_owner_.get();
    console->mmem = mmem;
    disk->mmem = mmem;

    console_queue_owner_.assign(simrv::virtio::kConsoleMaxQueueNum, simrv::virtio::QueueState{});
    disk_queue_owner_.assign(simrv::virtio::kDiskMaxQueueNum, simrv::virtio::QueueState{});
    console->Queue = console_queue_owner_.data();
    console->QueueSel = 0;
    console->QueueNum = 0;
    console->InterruptStatus = 0;
    console->Status = 0;

    disk->Queue = disk_queue_owner_.data();
    disk->QueueSel = 0;
    disk->QueueNum = 0;
    disk->InterruptStatus = 0;
    disk->Status = 0;

    memory_.initialize_mmu();

    memory_.system_bus().add_node(console.get());
    memory_.system_bus().add_node(disk.get());
    memory_.system_bus().add_node(rtc.get());
    memory_.system_bus().add_node(uart.get());
    memory_.system_bus().add_node(power.get());
    memory_.system_bus().add_node(framebuffer.get());
    memory_.system_bus().add_node(input_device.get());
    memory_.system_bus().add_node(audio.get());
    memory_.system_bus().add_node(&cpu.plic_mmio);
    memory_.system_bus().add_node(&cpu.clint_mmio);
    const bool linux_boot = !s_appmode;
    const Address dtb_offset =
        linux_boot
            ? static_cast<Address>(simrv::memory::kDramSize - static_cast<Address>(0x00100000U))
            : simrv::boot::kInitDataAddress;

    CSRValue initial_misa = isa::misa_with_mxl(s_misa_override ? s_misa_profile
                                                                : isa::kMisaDefault);
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
                        if (header[0] == 0x7f && header[1] == 'E' && header[2] == 'L' && header[3] == 'F') {
                            if (header[4] == 1) { // ELFCLASS32
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
                if (last_dot != std::string::npos && (last_slash == std::string::npos || last_dot > last_slash)) {
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
    cpu.state().regs.write(static_cast<RegId>(11), linux_boot ? (simrv::boot::kStartPc + dtb_offset) : 0);  // a1 = dtb
    cpu.state().misa = initial_misa;
    cpu.state().priv = kPrivMachine;
    cpu.state().regs.vlen = s_vlen ? s_vlen : 256;
    cpu.state().update_xlen();
    if (cpu.state().regs.xlen == 32) {
        cpu.state().pc = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(cpu.state().pc)));
    }
    cpu.TLB_flush();

    load_image_into_ram(s_fn_memimg, mmem, static_cast<std::size_t>(simrv::memory::kDramSize),
                        "memory", s_tuimode);
    symbols.load_from_elf(s_spike_elf.empty() ? s_fn_memimg : s_spike_elf);

    resolve_start_pc_and_dram_base(*this, symbols);

    // If launched without a binary in TUI mode, skip image-dependent init —
    // the TUI will open the LoadBinary modal and call load_program_binary() later.
    if (s_fn_memimg.empty() && s_tuimode) {
        return 0;
    }

    if (s_fn_dvtree.empty() && linux_boot) {
        std::string dtb_candidate = std::format("linux-images/rv{}/devicetree.dtb", simrv::xlen::kXLenBits);
        if (std::filesystem::exists(dtb_candidate)) {
            s_fn_dvtree = dtb_candidate;
        } else if (!s_fn_memimg.empty()) {
            std::filesystem::path bin_p(s_fn_memimg);
            if (bin_p.has_parent_path()) {
                auto dir_dtb = bin_p.parent_path() / "devicetree.dtb";
                if (std::filesystem::exists(dir_dtb)) {
                    s_fn_dvtree = dir_dtb.string();
                }
            }
        }
    }

    if (s_fn_dvtree.empty()) {
        if (linux_boot) {
            if (s_tuimode) {
                simrv::log::warn(
                    "No device-tree file (-c) specified for Linux/RTOS boot mode; TUI launching "
                    "in idle state.");
            } else {
                simrv::log::error("device-tree file (-c) is required for Linux boot mode");
                return 1;
            }
        }
    } else {
        if (dtb_offset >= simrv::memory::kDramSize) {
            simrv::log::error("device-tree load offset is outside DRAM");
            return 1;
        }
        const auto dt_cap = static_cast<std::size_t>(simrv::memory::kDramSize - dtb_offset);
        load_image_into_ram(s_fn_dvtree, mmem + dtb_offset, dt_cap, "device-tree", s_tuimode);
    }

    if (s_use_disk) {
        disk->sector_storage_.resize(simrv::virtio::kDiskSize);
        disk->sector = disk->sector_storage_.data();
        load_image_into_ram(s_fn_dskimg, disk->sector,
                            static_cast<std::size_t>(simrv::virtio::kDiskSize), "disk", s_tuimode);
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

    return 0;
}

auto Machine::load_program_binary(const std::string& filepath) -> bool {
    if (filepath.empty()) {
        return false;
    }
    s_fn_memimg = filepath;
    load_image_into_ram(s_fn_memimg, mmem, static_cast<std::size_t>(simrv::memory::kDramSize),
                        "memory", s_tuimode);
    symbols.load_from_elf(s_spike_elf.empty() ? s_fn_memimg : s_spike_elf);

    CSRValue initial_misa = isa::misa_with_mxl(s_misa_override ? s_misa_profile
                                                                : isa::kMisaDefault);
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
                        if (header[0] == 0x7f && header[1] == 'E' && header[2] == 'L' && header[3] == 'F') {
                            if (header[4] == 1) { // ELFCLASS32
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
                if (last_dot != std::string::npos && (last_slash == std::string::npos || last_dot > last_slash)) {
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
    cpu.state().update_xlen();

    resolve_start_pc_and_dram_base(*this, symbols);

    cpu.soft_tlb_flush();
    cpu.TLB_flush();
    cpu.decode_cache.flush();
    cpu.pipeline_context = simrv::pipeline::PipelineContext{};
    cpu.undo_stack.clear();
    cpu.trace_history_.clear();
    cpu.state().load_res = 0;

    for (std::size_t r = 0; r < 32; ++r) {
        cpu.state().regs.write(static_cast<RegId>(r), 0);
    }
    cpu.state().pc = s_start_pc;
    if (cpu.state().regs.xlen == 32) {
        cpu.state().pc = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(cpu.state().pc)));
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
        if (s_fn_dvtree.empty()) {
            std::string dtb_candidate =
                std::format("linux-images/rv{}/devicetree.dtb", simrv::xlen::kXLenBits);
            if (std::filesystem::exists(dtb_candidate)) {
                s_fn_dvtree = dtb_candidate;
            } else {
                std::filesystem::path bin_p(s_fn_memimg);
                if (bin_p.has_parent_path()) {
                    auto dir_dtb = bin_p.parent_path() / "devicetree.dtb";
                    if (std::filesystem::exists(dir_dtb)) {
                        s_fn_dvtree = dir_dtb.string();
                    }
                }
            }
        }
        if (!s_fn_dvtree.empty()) {
            const auto dt_cap = static_cast<std::size_t>(simrv::memory::kDramSize - dtb_offset);
            load_image_into_ram(s_fn_dvtree, mmem + dtb_offset, dt_cap, "device-tree", s_tuimode);
        }
    }
    cpu.soft_tlb_flush();
    cpu.TLB_flush();
    return true;
}

auto Machine::load_disk_image(const std::string& filepath) -> bool {
    if (filepath.empty() || !disk) {
        return false;
    }
    if (disk->sector_storage_.empty()) {
        disk->sector_storage_.resize(simrv::virtio::kDiskSize);
        disk->sector = disk->sector_storage_.data();
    }
    s_fn_dskimg = filepath;
    s_use_disk = true;
    load_image_into_ram(s_fn_dskimg, disk->sector,
                        static_cast<std::size_t>(simrv::virtio::kDiskSize), "disk", s_tuimode);
    return true;
}

}  // namespace simrv::core