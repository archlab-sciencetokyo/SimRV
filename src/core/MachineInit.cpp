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

void set_options(simrv::core::Machine* m, int argc, char* const* argv);

namespace simrv::core {

namespace {

constexpr size_t D_SIZE_DRAM = (size_t{9} * 1024U * 1024U);   // 9MB of bbl + kernel
constexpr size_t D_SIZE_DEVT = (size_t{4} * 1024U);           // 4KB of device tree
constexpr size_t D_SIZE_DISK = (size_t{16} * 1024U * 1024U);  // 16MB of disk image
constexpr Address D_DEVT_OFFSET = static_cast<Address>(16U * 1024U * 1024U);

void load_image_into_ram(const std::string& file_path, Byte* ram, std::size_t capacity,
                         const char* image_name) {
    if (ram == nullptr || capacity == 0) {
        simrv::log::error("invalid destination for {} image load", image_name);
        std::exit(EXIT_FAILURE);
    }

    std::ifstream in(file_path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        simrv::log::error("image_file {} cannot be found", file_path);
        std::exit(EXIT_FAILURE);
    }

    const auto file_size = static_cast<std::size_t>(in.tellg());
    if (file_size > capacity) {
        simrv::log::error("{} image {} is too large ({} bytes > {} bytes capacity)",
                     image_name, file_path, file_size, capacity);
        std::exit(EXIT_FAILURE);
    }

    in.seekg(0, std::ios::beg);
    if (!in.read(reinterpret_cast<char*>(ram), static_cast<std::streamsize>(file_size))) {
        simrv::log::error("failed to read {} image {}", image_name, file_path);
        std::exit(EXIT_FAILURE);
    }

    if (file_size >= 5 && std::to_integer<uint8_t>(ram[0]) == 0x7f && 
        std::to_integer<char>(ram[1]) == 'E' && 
        std::to_integer<char>(ram[2]) == 'L' && 
        std::to_integer<char>(ram[3]) == 'F') {
        const uint8_t elf_class = std::to_integer<uint8_t>(ram[4]);
        constexpr uint8_t expected_class = simrv::xlen::kXLenBits == 32 ? 1 : 2;
        if (elf_class != expected_class) {
            simrv::log::warn("Loaded ELF image {} is {}-bit but SimRV is compiled for {}-bit!", 
                         file_path, elf_class == 1 ? 32 : 64, simrv::xlen::kXLenBits);
        }
    }
}



}  // namespace

void Machine::generate_binfile() const {
    std::ofstream out("inits.bin", std::ios::binary);
    if (!out.is_open()) {
        simrv::log::error("cannot create inits.bin");
        std::exit(EXIT_FAILURE);
    }
    out.write(reinterpret_cast<const char*>(mmem), D_SIZE_DRAM);
    out.write(reinterpret_cast<const char*>(mmem + D_DEVT_OFFSET), D_SIZE_DEVT);
    out.write(reinterpret_cast<const char*>(disk->sector), D_SIZE_DISK);
    out.close();
    simrv::log::info("File inits.bin was generated.");

    std::ifstream in("inits.bin", std::ios::binary);
    if (!in.is_open()) {
        simrv::log::error("cannot reopen inits.bin");
        std::exit(EXIT_FAILURE);
    }
    int i = 0;
    Word sum = 0;
    Word buf = 0;
    while (in.read(reinterpret_cast<char*>(&buf), sizeof(buf))) {
        sum += buf;
        i++;
    }
    simrv::log::info("{:8} byte file, checksum {:08x}\n", i * 4, sum);
    std::exit(EXIT_SUCCESS);
}

auto Machine::initialize(int argc, char* const* argv) -> int {
    set_options(this, argc, argv);

    disk = std::make_unique<simrv::device::Disk>(*this);
    console = std::make_unique<simrv::device::Console>(*this);
    rtc = std::make_unique<simrv::Rtc>(*this);
    uart = std::make_unique<simrv::device::Uart>(*this);
    power = std::make_unique<simrv::device::PowerMmio>(*this);
    mmem_owner_.reset(new Byte[simrv::memory::kDramSize]());
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
    const bool linux_boot = !s_appmode;
    const Address dtb_offset =
        linux_boot
            ? static_cast<Address>(simrv::memory::kDramSize - static_cast<Address>(0x00100000U))
            : simrv::boot::kInitDataAddress;

    CSRValue initial_misa = misa_with_mxl(s_misa_override ? s_misa_profile
                                                          : kMisaDefault);
    cpu.state().pc = s_start_pc;
    cpu.state().regs.write(static_cast<RegId>(10), 0);  // a0 = hartid
    cpu.state().regs.write(static_cast<RegId>(11), linux_boot ? (simrv::boot::kStartPc + dtb_offset) : 0);  // a1 = dtb
    cpu.state().misa = initial_misa;
    cpu.state().priv = kPrivMachine;
    cpu.TLB_flush();

    load_image_into_ram(s_fn_memimg, mmem, static_cast<std::size_t>(simrv::memory::kDramSize),
                        "memory");

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
        load_image_into_ram(s_fn_dvtree, mmem + dtb_offset, dt_cap, "device-tree");
    }

    if (s_use_disk) {
        load_image_into_ram(s_fn_dskimg, disk->sector,
                            static_cast<std::size_t>(simrv::virtio::kDiskSize), "disk");
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
        const std::string isa_str = simrv::debug::spike_isa_string(s_misa_profile);
        spike_lockstep = std::make_unique<simrv::debug::SpikeLockstep>(
            s_spike_bin, s_fn_memimg, s_fn_dskimg, s_fn_dvtree, isa_str);
        simrv::log::info("Spike lockstep co-simulation active (isa={})", isa_str);
        if (!spike_lockstep->start()) {
            simrv::log::error("Failed to launch Spike for lockstep verification");
            return 1;
        }
    }

    return 0;
}

}  // namespace simrv::core