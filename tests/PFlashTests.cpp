/**
 * @file PFlashTests.cpp
 * @brief Unit tests for CFI Parallel NOR Flash controller.
 */
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "simrv/core/Machine.hpp"
#include "simrv/device/pflash/PFlash.hpp"

namespace {

auto failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_cfi_query() {
    simrv::core::MachineConfig cfg{};
    cfg.execution.appmode = true;
    simrv::core::Machine machine{cfg};

    constexpr uint32_t kSize = 32 * 1024 * 1024;
    constexpr uint32_t kSectorSize = 64 * 1024;
    simrv::device::PFlash pflash(machine, 0x20000000, kSize, kSectorSize, false, "pflash0");

    // Initially in Read Array mode -> should be erased (0xFF)
    expect(pflash.read8(0x0) == 0xFF, "PFlash initial content is 0xFF");

    // Enter CFI Query mode
    pflash.write8(0x0, 0x98);

    expect(pflash.read8(0x10) == 'Q', "CFI byte 0x10 is 'Q'");
    expect(pflash.read8(0x11) == 'R', "CFI byte 0x11 is 'R'");
    expect(pflash.read8(0x12) == 'Y', "CFI byte 0x12 is 'Y'");

    // Command Set 0x0001
    expect(pflash.read8(0x13) == 0x01, "CFI command set LSB is 0x01");
    expect(pflash.read8(0x14) == 0x00, "CFI command set MSB is 0x00");

    // Device size 2^25 = 32 MiB
    expect(pflash.read8(0x27) == 25, "CFI device size 2^25 (32 MiB)");

    // Erase block regions
    expect(pflash.read8(0x2C) == 0x01, "CFI 1 erase block region");
    const uint32_t num_sectors = kSize / kSectorSize;
    expect(pflash.read8(0x2D) == static_cast<uint8_t>((num_sectors - 1) & 0xFF),
           "CFI num_sectors LSB");
    expect(pflash.read8(0x2E) == static_cast<uint8_t>(((num_sectors - 1) >> 8) & 0xFF),
           "CFI num_sectors MSB");
    expect(pflash.read8(0x2F) == static_cast<uint8_t>((kSectorSize / 256) & 0xFF),
           "CFI sector_size / 256 LSB");
    expect(pflash.read8(0x30) == static_cast<uint8_t>(((kSectorSize / 256) >> 8) & 0xFF),
           "CFI sector_size / 256 MSB");

    // Return to Read Array mode
    pflash.write8(0x0, 0xFF);
    expect(pflash.read8(0x10) == 0xFF, "Read Array returns storage content after reset");
}

void test_read_status_and_id() {
    simrv::core::MachineConfig cfg{};
    cfg.execution.appmode = true;
    simrv::core::Machine machine{cfg};

    simrv::device::PFlash pflash(machine, 0x20000000, 1024 * 1024, 64 * 1024, false, "pflash0");

    // Read Status command (0x70)
    pflash.write8(0x0, 0x70);
    expect(pflash.read8(0x0) == 0x80, "Status register reports Ready (bit 7 set)");

    // Read Device ID command (0x90)
    pflash.write8(0x0, 0x90);
    expect(pflash.read8(0x0) == 0x89, "JEDEC Manufacturer ID is 0x89");
    expect(pflash.read8(0x1) == 0x18, "Device ID is 0x18");

    // Return to Read Array
    pflash.write8(0x0, 0xFF);
}

void test_program_and_erase() {
    simrv::core::MachineConfig cfg{};
    cfg.execution.appmode = true;
    simrv::core::Machine machine{cfg};

    constexpr uint32_t kSize = 128 * 1024;
    constexpr uint32_t kSectorSize = 64 * 1024;
    simrv::device::PFlash pflash(machine, 0x20000000, kSize, kSectorSize, false, "pflash0");

    // Program setup (0x40), then write data
    pflash.write8(0x100, 0x40);
    pflash.write8(0x100, 0xA5);

    // After write, should be in Read Status mode
    expect((pflash.read8(0x100) & 0x80) != 0, "Status is Ready after program");

    // Switch to Read Array
    pflash.write8(0x0, 0xFF);
    expect(pflash.read8(0x100) == 0xA5, "Programmed byte holds written value");

    // Erase setup (0x20), then confirm (0xD0)
    pflash.write8(0x100, 0x20);
    pflash.write8(0x100, 0xD0);

    // Switch back to Read Array
    pflash.write8(0x0, 0xFF);
    expect(pflash.read8(0x100) == 0xFF, "Erased block is restored to 0xFF");
}

void test_persistence() {
    simrv::core::MachineConfig cfg{};
    cfg.execution.appmode = true;
    simrv::core::Machine machine{cfg};

    const std::filesystem::path test_file = "/tmp/simrv_pflash_persist_test.bin";
    std::filesystem::remove(test_file);

    {
        simrv::device::PFlash pflash(machine, 0x22000000, 64 * 1024, 64 * 1024, false, "pflash1");
        pflash.set_backing_file(test_file);

        // Program byte
        pflash.write8(0x10, 0x40);
        pflash.write8(0x10, 0x5A);

        pflash.sync();
    }

    // Verify file on disk
    std::ifstream in(test_file, std::ios::binary);
    expect(in.is_open(), "Backing file created successfully");
    if (in.is_open()) {
        std::vector<uint8_t> data(64 * 1024);
        in.read(reinterpret_cast<char*>(data.data()), data.size());
        expect(data[0x10] == 0x5A, "Persisted byte matches programmed value in backing file");
    }

    std::filesystem::remove(test_file);
}

}  // namespace

auto main() -> int {
    test_cfi_query();
    test_read_status_and_id();
    test_program_and_erase();
    test_persistence();

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All PFlash unit tests passed.\n";
    return EXIT_SUCCESS;
}
