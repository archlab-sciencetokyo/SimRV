/**
 * @file PFlash.cpp
 * @brief Implementation of CFI Parallel NOR Flash controller.
 */
#include "simrv/device/pflash/PFlash.hpp"

#include <bit>
#include <cstring>
#include <fstream>

#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"

namespace simrv::device {

PFlash::PFlash(core::Machine& machine, simrv::xlen::Address base, simrv::xlen::Address size,
               uint32_t sector_size, bool read_only, std::string name)
    : memory::MmioDevice(&machine),
      machine_(machine),
      base_(base),
      size_(size),
      sector_size_(sector_size),
      read_only_(read_only),
      name_(std::move(name)),
      storage_(size, 0xFF) {}

PFlash::~PFlash() { sync(); }

auto PFlash::load_image(const std::filesystem::path& path) -> bool {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const auto file_size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    const size_t bytes_to_read = std::min(static_cast<size_t>(size_), file_size);
    file.read(reinterpret_cast<char*>(storage_.data()), bytes_to_read);
    return true;
}

void PFlash::sync() {
    if (!dirty_ || backing_path_.empty() || read_only_) {
        return;
    }
    std::ofstream out(backing_path_, std::ios::binary | std::ios::trunc);
    if (out) {
        out.write(reinterpret_cast<const char*>(storage_.data()), storage_.size());
        dirty_ = false;
    }
}

auto PFlash::read_cfi(simrv::xlen::Address offset) const -> uint8_t {
    const uint32_t num_sectors = sector_size_ > 0 ? (size_ / sector_size_) : 1;
    const uint32_t size_pow2 = std::bit_width(static_cast<uint64_t>(size_)) - 1;

    switch (offset) {
        case 0x10:
            return 'Q';
        case 0x11:
            return 'R';
        case 0x12:
            return 'Y';
        case 0x13:
            return 0x01;  // JEDEC CFI Command Set 0x0001 (Standard NOR Flash) LSB
        case 0x14:
            return 0x00;  // JEDEC CFI Command Set 0x0001 MSB
        case 0x1B:
            return 0x27;  // Vcc min = 2.7V
        case 0x1C:
            return 0x36;  // Vcc max = 3.6V
        case 0x27:
            return static_cast<uint8_t>(size_pow2);  // 2^N bytes total capacity
        case 0x28:
            return 0x02;  // x16 asynchronous interface LSB
        case 0x29:
            return 0x00;
        case 0x2A:
            return 0x08;  // Max 256 bytes buffer write
        case 0x2B:
            return 0x00;
        case 0x2C:
            return 0x01;  // 1 Erase Block Region
        case 0x2D:
            return static_cast<uint8_t>((num_sectors - 1) & 0xFF);
        case 0x2E:
            return static_cast<uint8_t>(((num_sectors - 1) >> 8) & 0xFF);
        case 0x2F:
            return static_cast<uint8_t>((sector_size_ / 256) & 0xFF);
        case 0x30:
            return static_cast<uint8_t>(((sector_size_ / 256) >> 8) & 0xFF);
        default:
            return 0x00;
    }
}

void PFlash::handle_command(simrv::xlen::Address offset, uint32_t value) {
    if (read_only_) {
        return;
    }

    const uint8_t cmd = static_cast<uint8_t>(value & 0xFF);

    if (mode_ == PFlashMode::ProgramPending) {
        if (offset < storage_.size()) {
            storage_[offset] = static_cast<uint8_t>(value);
            dirty_ = true;
        }
        mode_ = PFlashMode::ReadStatus;
        return;
    }

    if (mode_ == PFlashMode::BlockErasePending) {
        if (cmd == 0xD0) {
            // Confirm block erase
            const simrv::xlen::Address block_base = offset & ~(sector_size_ - 1u);
            if (block_base < storage_.size()) {
                const size_t erase_start = static_cast<size_t>(block_base);
                const size_t erase_len =
                    std::min(static_cast<size_t>(sector_size_), storage_.size() - erase_start);
                std::memset(storage_.data() + erase_start, 0xFF, erase_len);
                dirty_ = true;
            }
        }
        mode_ = PFlashMode::ReadStatus;
        return;
    }

    switch (cmd) {
        case 0xFF:  // Read Array
            mode_ = PFlashMode::ReadArray;
            break;
        case 0x98:  // Read CFI Query
            mode_ = PFlashMode::CfiQuery;
            break;
        case 0x90:  // Read JEDEC ID
            mode_ = PFlashMode::ReadId;
            break;
        case 0x70:  // Read Status
            mode_ = PFlashMode::ReadStatus;
            break;
        case 0x50:  // Clear Status
            status_reg_ = 0x80;
            break;
        case 0x20:  // Block Erase Setup
            mode_ = PFlashMode::BlockErasePending;
            break;
        case 0x40:  // Byte/Word Program Setup
        case 0x10:
            mode_ = PFlashMode::ProgramPending;
            break;
        default:
            break;
    }
}

auto PFlash::read8(simrv::memory::MmioOffset offset) -> simrv::memory::MmioData8 {
    if (offset >= storage_.size()) {
        return 0;
    }
    switch (mode_) {
        case PFlashMode::ReadArray:
            return storage_[offset];
        case PFlashMode::CfiQuery:
            return read_cfi(offset);
        case PFlashMode::ReadStatus:
            return status_reg_;
        case PFlashMode::ReadId:
            if (offset == 0) return 0x89;  // JEDEC JEP106 Manufacturer ID
            if (offset == 1) return 0x18;  // JEDEC Device ID
            return 0x00;
        default:
            return storage_[offset];
    }
}

auto PFlash::read16(simrv::memory::MmioOffset offset) -> simrv::memory::MmioData16 {
    if (offset + 1 >= storage_.size()) {
        return 0;
    }
    if (mode_ == PFlashMode::ReadArray) {
        return static_cast<uint16_t>(storage_[offset]) |
               (static_cast<uint16_t>(storage_[offset + 1]) << 8);
    }
    return static_cast<uint16_t>(read8(offset)) | (static_cast<uint16_t>(read8(offset + 1)) << 8);
}

auto PFlash::read32(simrv::memory::MmioOffset offset) -> simrv::memory::MmioData32 {
    if (offset + 3 >= storage_.size()) {
        return 0;
    }
    if (mode_ == PFlashMode::ReadArray) {
        uint32_t val = 0;
        std::memcpy(&val, storage_.data() + offset, sizeof(val));
        return val;
    }
    return static_cast<uint32_t>(read16(offset)) |
           (static_cast<uint32_t>(read16(offset + 2)) << 16);
}

auto PFlash::read64(simrv::memory::MmioOffset offset) -> simrv::memory::MmioData64 {
    if (offset + 7 >= storage_.size()) {
        return 0;
    }
    if (mode_ == PFlashMode::ReadArray) {
        uint64_t val = 0;
        std::memcpy(&val, storage_.data() + offset, sizeof(val));
        return val;
    }
    return static_cast<uint64_t>(read32(offset)) |
           (static_cast<uint64_t>(read32(offset + 4)) << 32);
}

void PFlash::write8(simrv::memory::MmioOffset offset, simrv::memory::MmioData8 val) {
    handle_command(offset, val);
}

void PFlash::write16(simrv::memory::MmioOffset offset, simrv::memory::MmioData16 val) {
    if (mode_ == PFlashMode::ProgramPending) {
        if (offset + 1 < storage_.size()) {
            storage_[offset] = static_cast<uint8_t>(val & 0xFF);
            storage_[offset + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
            dirty_ = true;
        }
        mode_ = PFlashMode::ReadStatus;
        return;
    }
    handle_command(offset, val & 0xFF);
}

void PFlash::write32(simrv::memory::MmioOffset offset, simrv::memory::MmioData32 val) {
    if (mode_ == PFlashMode::ProgramPending) {
        if (offset + 3 < storage_.size()) {
            std::memcpy(storage_.data() + offset, &val, sizeof(val));
            dirty_ = true;
        }
        mode_ = PFlashMode::ReadStatus;
        return;
    }
    handle_command(offset, val & 0xFF);
}

void PFlash::write64(simrv::memory::MmioOffset offset, simrv::memory::MmioData64 val) {
    if (mode_ == PFlashMode::ProgramPending) {
        if (offset + 7 < storage_.size()) {
            std::memcpy(storage_.data() + offset, &val, sizeof(val));
            dirty_ = true;
        }
        mode_ = PFlashMode::ReadStatus;
        return;
    }
    handle_command(offset, static_cast<uint32_t>(val & 0xFF));
}

}  // namespace simrv::device
