/**
 * @file MachineInit.cpp
 * @brief Machine initialization and image loading routines.
 */
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
        std::println(stderr, "__ Error: invalid destination for {} image load", image_name);
        std::exit(EXIT_FAILURE);
    }

    std::ifstream in(file_path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        std::println(stderr, "__ Error: image_file {} cannot be found", file_path);
        std::exit(EXIT_FAILURE);
    }

    const auto file_size = static_cast<std::size_t>(in.tellg());
    if (file_size > capacity) {
        std::println(stderr, "__ Error: {} image {} is too large ({} bytes > {} bytes capacity)",
                     image_name, file_path, file_size, capacity);
        std::exit(EXIT_FAILURE);
    }

    in.seekg(0, std::ios::beg);
    if (!in.read(reinterpret_cast<char*>(ram), static_cast<std::streamsize>(file_size))) {
        std::println(stderr, "__ Error: failed to read {} image {}", image_name, file_path);
        std::exit(EXIT_FAILURE);
    }
}

void load_devicetree(Byte* ram) {
    constexpr std::array<Word, 344> tbuf = {
        0xedfe0dd0, 0x5a050000, 0x38000000, 0x84040000, 0x28000000, 0x11000000, 0x10000000,
        0x0,        0xd6000000, 0x4c040000, 0x0,        0x0,        0x0,        0x0,
        0x1000000,  0x0,        0x3000000,  0x4000000,  0x0,        0x2000000,  0x3000000,
        0x4000000,  0xf000000,  0x2000000,  0x3000000,  0xf000000,  0x1b000000, 0x6573696b,
        0x62616c2d, 0x6d69732c, 0x7672,     0x3000000,  0xf000000,  0x21000000, 0x6573696b,
        0x62616c5f, 0x6d69732c, 0x7672,     0x1000000,  0x73757063, 0x0,        0x3000000,
        0x4000000,  0x0,        0x1000000,  0x3000000,  0x4000000,  0xf000000,  0x0,
        0x3000000,  0x4000000,  0x2c000000, 0xe1f505,   0x1000000,  0x40757063, 0x30,
        0x3000000,  0x4000000,  0x3f000000, 0x757063,   0x3000000,  0x4000000,  0x4b000000,
        0x0,        0x3000000,  0x5000000,  0x4f000000, 0x79616b6f, 0x0,        0x3000000,
        0x6000000,  0x21000000, 0x63736972, 0x76,       0x3000000,  0x9000000,  0x56000000,
        0x32337672, 0x6d696361, 0x0,        0x3000000,  0xb000000,  0x60000000, 0x63736972,
        0x76732c76, 0x3233,     0x3000000,  0x4000000,  0x69000000, 0xe1f505,   0x1000000,
        0x65746e69, 0x70757272, 0x6f632d74, 0x6f72746e, 0x72656c6c, 0x0,        0x3000000,
        0x4000000,  0x79000000, 0x1000000,  0x3000000,  0x0,        0x8a000000, 0x3000000,
        0xf000000,  0x21000000, 0x63736972, 0x70632c76, 0x6e692d75, 0x6374,     0x3000000,
        0x4000000,  0x9f000000, 0x1000000,  0x2000000,  0x2000000,  0x2000000,  0x1000000,
        0x6f6d656d, 0x38407972, 0x30303030, 0x303030,   0x3000000,  0x7000000,  0x3f000000,
        0x6f6d656d, 0x7972,     0x3000000,  0x10000000, 0x4b000000, 0x0,        0x80,
        0x0,        0x4,        0x2000000,  0x1000000,  0x636f73,   0x3000000,  0x4000000,
        0x0,        0x2000000,  0x3000000,  0x4000000,  0xf000000,  0x2000000,  0x3000000,
        0xb000000,  0x21000000, 0x706d6973, 0x622d656c, 0x7375,     0x3000000,  0x0,
        0xa7000000, 0x1000000,  0x6e696c63, 0x30364074, 0x30303030, 0x3030,     0x3000000,
        0xd000000,  0x21000000, 0x63736972, 0x6c632c76, 0x30746e69, 0x0,        0x3000000,
        0x10000000, 0xae000000, 0x1000000,  0x3000000,  0x1000000,  0x7000000,  0x3000000,
        0x10000000, 0x4b000000, 0x0,        0x60,       0x0,        0x8,        0x2000000,
        0x1000000,  0x63696c70, 0x30303540, 0x30303030, 0x30,       0x3000000,  0x4000000,
        0x79000000, 0x1000000,  0x3000000,  0x0,        0x8a000000, 0x3000000,  0xc000000,
        0x21000000, 0x63736972, 0x6c702c76, 0x306369,   0x3000000,  0x4000000,  0xc2000000,
        0x1f000000, 0x3000000,  0x10000000, 0x4b000000, 0x0,        0x50,       0x0,
        0x8,        0x3000000,  0x10000000, 0xae000000, 0x1000000,  0x9000000,  0x1000000,
        0xb000000,  0x3000000,  0x4000000,  0x9f000000, 0x2000000,  0x2000000,  0x1000000,
        0x74726976, 0x34406f69, 0x30303030, 0x303030,   0x3000000,  0xc000000,  0x21000000,
        0x74726976, 0x6d2c6f69, 0x6f696d,   0x3000000,  0x10000000, 0x4b000000, 0x0,
        0x40,       0x0,        0x8,        0x3000000,  0x8000000,  0xae000000, 0x2000000,
        0x1000000,  0x2000000,  0x1000000,  0x74726976, 0x34406f69, 0x30303038, 0x303030,
        0x3000000,  0xc000000,  0x21000000, 0x74726976, 0x6d2c6f69, 0x6f696d,   0x3000000,
        0x10000000, 0x4b000000, 0x0,        0x48,       0x0,        0x8,        0x3000000,
        0x8000000,  0xae000000, 0x2000000,  0x2000000,  0x2000000,  0x2000000,  0x1000000,
        0x736f6863, 0x6e65,     0x3000000,  0x1e000000, 0xcd000000, 0x736e6f63, 0x3d656c6f,
        0x30637668, 0x6f6f7220, 0x642f3d74, 0x762f7665, 0x72206164, 0x77,       0x2000000,
        0x2000000,  0x9000000,  0x64646123, 0x73736572, 0x6c65632d, 0x2300736c, 0x657a6973,
        0x6c65632d, 0x6d00736c, 0x6c65646f, 0x6d6f6300, 0x69746170, 0x656c62,   0x656d6974,
        0x65736162, 0x6572662d, 0x6e657571, 0x64007963, 0x63697665, 0x79745f65, 0x72006570,
        0x73006765, 0x75746174, 0x69720073, 0x2c766373, 0x617369,   0x2d756d6d, 0x65707974,
        0x6f6c6300, 0x662d6b63, 0x75716572, 0x79636e65, 0x6e692300, 0x72726574, 0x2d747075,
        0x6c6c6563, 0x6e690073, 0x72726574, 0x2d747075, 0x746e6f63, 0x6c6c6f72, 0x70007265,
        0x646e6168, 0x7200656c, 0x65676e61, 0x6e690073, 0x72726574, 0x73747075, 0x7478652d,
        0x65646e65, 0x69720064, 0x2c766373, 0x7665646e, 0x6f6f6200, 0x67726174, 0xff0073,
        0x0};
    std::memcpy(ram, tbuf.data(), tbuf.size() * sizeof(Word));
}

}  // namespace

void Machine::generate_binfile() const {
    std::ofstream out("inits.bin", std::ios::binary);
    if (!out.is_open()) {
        std::println(stderr, "__ Error: cannot create inits.bin");
        std::exit(EXIT_FAILURE);
    }
    out.write(reinterpret_cast<const char*>(mmem), D_SIZE_DRAM);
    out.write(reinterpret_cast<const char*>(mmem + D_DEVT_OFFSET), D_SIZE_DEVT);
    out.write(reinterpret_cast<const char*>(disk->sector), D_SIZE_DISK);
    out.close();
    std::println("__ File inits.bin was generated.");

    std::ifstream in("inits.bin", std::ios::binary);
    if (!in.is_open()) {
        std::println(stderr, "__ Error: cannot reopen inits.bin");
        std::exit(EXIT_FAILURE);
    }
    int i = 0;
    Word sum = 0;
    Word buf = 0;
    while (in.read(reinterpret_cast<char*>(&buf), sizeof(buf))) {
        sum += buf;
        i++;
    }
    std::println("__ {:8} byte file, checksum {:08x}\n", i * 4, sum);
    std::exit(EXIT_SUCCESS);
}

auto Machine::initialize(int argc, char* const* argv) -> int {
    set_options(this, argc, argv);

    disk = std::make_unique<simrv::device::Disk>(*this);
    console = std::make_unique<simrv::device::Console>(*this);
    rtc = std::make_unique<simrv::Rtc>(*this);
    uart = std::make_unique<simrv::device::Uart>(*this);
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
    memory_.system_bus().add_node(&cpu.plic_mmio);
    memory_.system_bus().add_node(&cpu.clint_mmio);

    const bool linux_boot = !s_appmode && !s_rtosmode;
    const Address dtb_offset =
        linux_boot
            ? static_cast<Address>(simrv::memory::kDramSize - static_cast<Address>(0x00100000U))
            : simrv::boot::kInitDataAddress;

    CSRValue initial_misa = s_misa_override ? s_misa_profile
                            : s_rtosmode    ? misa_profile_bits(MisaProfile::I)
                                            : kMisaDefault;
    cpu.state().pc = s_start_pc;
    cpu.state().regs.write(static_cast<RegId>(10), 0);  // a0 = hartid
    cpu.state().regs.write(static_cast<RegId>(11), linux_boot ? (simrv::boot::kStartPc + dtb_offset) : 0);  // a1 = dtb
    cpu.state().misa = initial_misa;
    cpu.state().priv = kPrivMachine;
    cpu.TLB_flush();

    load_image_into_ram(s_fn_memimg, mmem, static_cast<std::size_t>(simrv::memory::kDramSize),
                        "memory");

    if (s_fn_dvtree.empty()) {
        load_devicetree(mmem + dtb_offset);
    } else {
        if (dtb_offset >= simrv::memory::kDramSize) {
            std::println(std::cerr, "__ Error: device-tree load offset is outside DRAM");
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

    return 0;
}

}  // namespace simrv::core