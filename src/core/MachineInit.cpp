/**
 * @file MachineInit.cpp
 * @brief Machine initialization and image loading routines.
 */
#include "simrv/core/Logger.hpp"
#include "simrv/debug/GdbStub.hpp"
#include "simrv/debug/SpikeLockstep.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
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
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/Types.hpp"
#include "simrv/core/CpuConfigParser.hpp"

void set_options(simrv::core::Machine* m, int argc, char* const* argv);

namespace simrv::core {

namespace {

constexpr size_t D_SIZE_DRAM = (size_t{9} * 1024U * 1024U);   // 9MB of bbl + kernel
constexpr size_t D_SIZE_DEVT = (size_t{4} * 1024U);           // 4KB of device tree
constexpr size_t D_SIZE_DISK = (size_t{16} * 1024U * 1024U);  // 16MB of disk image
constexpr Address D_DEVT_OFFSET = static_cast<Address>(16U * 1024U * 1024U);

void load_image_into_ram(std::string& file_path, Byte* ram, std::size_t capacity,
                         const char* image_name, bool tuimode) {
    if (ram == nullptr || capacity == 0) {
        simrv::log::error("invalid destination for {} image load", image_name);
        std::exit(EXIT_FAILURE);
    }

    std::ifstream in(file_path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        if (tuimode) {
            std::string temp_path = file_path;
            while (true) {
                std::println(stderr, "\033[1;33m[TUI File Prompt] {} image '{}' is missing or cannot be opened.\033[0m", image_name, temp_path);
                std::print(stderr, "Please enter path to a valid {} image file (or press Enter/Ctrl+D to exit): ", image_name);
                std::fflush(stderr);
                std::string input;
                std::getline(std::cin, input);
                if (input.empty() && std::cin.eof()) {
                    simrv::log::error("No valid {} image file provided. Exiting.", image_name);
                    std::exit(EXIT_FAILURE);
                }
                if (std::cin.eof()) {
                    std::cin.clear();
                }
                if (input.empty()) {
                    simrv::log::error("No valid {} image file provided. Exiting.", image_name);
                    std::exit(EXIT_FAILURE);
                }
                // Strip leading/trailing spaces and quotes
                if ((input.front() == '"' && input.back() == '"') ||
                    (input.front() == '\'' && input.back() == '\'')) {
                    input = input.substr(1, input.size() - 2);
                }
                std::ifstream test_in(input, std::ios::binary | std::ios::ate);
                if (test_in.is_open()) {
                    file_path = input;
                    in = std::move(test_in);
                    break;
                }
            }
        } else {
            simrv::log::error("image_file {} cannot be found", file_path);
            std::exit(EXIT_FAILURE);
        }
    }

    const auto file_size = static_cast<std::size_t>(in.tellg());
    if (file_size > capacity) {
        simrv::log::error("{} image {} is too large ({} bytes > {} bytes capacity)",
                     image_name, file_path, file_size, capacity);
        std::exit(EXIT_FAILURE);
    }

    in.seekg(0, std::ios::beg);
    if (!in.read(reinterpret_cast<char*>(ram), static_cast<std::streamsize>(file_size))) { // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        simrv::log::error("failed to read {} image {}", image_name, file_path);
        std::exit(EXIT_FAILURE);
    }

    if (file_size >= 5 && std::to_integer<uint8_t>(ram[0]) == 0x7f && 
        std::to_integer<char>(ram[1]) == 'E' && 
        std::to_integer<char>(ram[2]) == 'L' && 
        std::to_integer<char>(ram[3]) == 'F') {
        const auto elf_class = std::to_integer<uint8_t>(ram[4]);
        constexpr uint8_t expected_class = simrv::xlen::kXLenBits == 32 ? 1 : 2;
        if (elf_class != expected_class) {
            simrv::log::warn("Loaded ELF image {} is {}-bit but SimRV is compiled for {}-bit!", 
                         file_path, elf_class == 1 ? 32 : 64, simrv::xlen::kXLenBits);
        }
    }
}



}  // namespace

auto Machine::initialize(int argc, char* const* argv) -> int {
    set_options(this, argc, argv);

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
    mmem_owner_.reset(static_cast<Byte*>(std::calloc(simrv::memory::kDramSize, sizeof(Byte)))); // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    if (mmem_owner_ == nullptr) {
        std::println(std::cerr, "Error: failed to allocate main memory ({} bytes)",
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
    memory_.system_bus().add_node(&cpu.plic_mmio);
    memory_.system_bus().add_node(&cpu.clint_mmio);
    const bool linux_boot = !s_appmode && !s_isatest;
    const Address dtb_offset =
        linux_boot
            ? static_cast<Address>(simrv::memory::kDramSize - static_cast<Address>(0x00100000U))
            : simrv::boot::kInitDataAddress;

    CSRValue initial_misa = misa_with_mxl(s_misa_override ? s_misa_profile
                                                          : kMisaDefault);
    if constexpr (simrv::xlen::kIsXLen64) {
        bool is_32bit = false;
        if (s_misa_override && s_misa_xlen == 32) {
            is_32bit = true;
        } else if (!s_misa_override || s_misa_xlen == 0) {
            auto check_elf = [&](const std::string& path) {
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
    cpu.state().update_xlen();
    if (cpu.state().regs.xlen == 32) {
        cpu.state().pc = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(cpu.state().pc)));
    }
    cpu.TLB_flush();

    load_image_into_ram(s_fn_memimg, mmem, static_cast<std::size_t>(simrv::memory::kDramSize),
                        "memory", s_tuimode);
    symbols.load_from_elf(s_fn_memimg);

    if (s_fn_dvtree.empty()) {
        if (linux_boot) {
            simrv::log::error("device-tree file (-c) is required for Linux boot mode");
            return 1;
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
        load_image_into_ram(s_fn_dskimg, disk->sector,
                            static_cast<std::size_t>(simrv::virtio::kDiskSize), "disk", s_tuimode);
    } else {
        std::memset(disk->sector, 0, simrv::virtio::kDiskSize);
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

}  // namespace simrv::core