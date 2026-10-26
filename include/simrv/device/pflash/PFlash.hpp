/**
 * @file PFlash.hpp
 * @brief Parallel NOR Flash memory controller with CFI query support.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "simrv/memory/MmioDevice.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::device {

/**
 * @enum PFlashMode
 * @brief Internal command states of the CFI flash interface.
 */
enum class PFlashMode : uint8_t {
    ReadArray = 0,
    CfiQuery = 1,
    ReadId = 2,
    ReadStatus = 3,
    BlockErasePending = 4,
    ProgramPending = 5,
};

/**
 * @class PFlash
 * @brief Parallel NOR Flash device with Common Flash Interface (CFI) support.
 *
 * Implements JEDEC CFI Standard (Command Set 0x0001) parallel NOR flash emulation
 * required by EDK2 VirtNorFlashDxe for firmware execution and NVRAM variable storage.
 */
class PFlash final : public memory::MmioDevice {
   public:
    PFlash(core::Machine& machine, simrv::xlen::Address base, simrv::xlen::Address size,
           uint32_t sector_size = 128 * 1024, bool read_only = false, std::string name = "pflash");
    ~PFlash() override;

    [[nodiscard]] auto name() const -> const char* override { return name_.c_str(); }
    [[nodiscard]] auto base_address() const -> simrv::xlen::Address override { return base_; }
    [[nodiscard]] auto size() const -> simrv::xlen::Address override { return size_; }

    [[nodiscard]] auto read8(simrv::memory::MmioOffset offset) -> simrv::memory::MmioData8 override;
    [[nodiscard]] auto read16(simrv::memory::MmioOffset offset)
        -> simrv::memory::MmioData16 override;
    [[nodiscard]] auto read32(simrv::memory::MmioOffset offset)
        -> simrv::memory::MmioData32 override;
    [[nodiscard]] auto read64(simrv::memory::MmioOffset offset)
        -> simrv::memory::MmioData64 override;

    void write8(simrv::memory::MmioOffset offset, simrv::memory::MmioData8 val) override;
    void write16(simrv::memory::MmioOffset offset, simrv::memory::MmioData16 val) override;
    void write32(simrv::memory::MmioOffset offset, simrv::memory::MmioData32 val) override;
    void write64(simrv::memory::MmioOffset offset, simrv::memory::MmioData64 val) override;

    /// Load flash content from binary file on host filesystem.
    auto load_image(const std::filesystem::path& path) -> bool;

    /// Set file path for non-volatile writeback persistence.
    void set_backing_file(const std::filesystem::path& path) { backing_path_ = path; }

    /// Flush in-memory flash content to backing file if modified.
    void sync();

    /// Direct host buffer access (for pre-loading or memory inspection).
    [[nodiscard]] auto data() noexcept -> uint8_t* { return storage_.data(); }
    [[nodiscard]] auto data() const noexcept -> const uint8_t* { return storage_.data(); }

   private:
    [[nodiscard]] auto read_cfi(simrv::xlen::Address offset) const -> uint8_t;
    void handle_command(simrv::xlen::Address offset, uint32_t value);

    [[maybe_unused]] core::Machine& machine_;
    simrv::xlen::Address base_;
    simrv::xlen::Address size_;
    uint32_t sector_size_;
    bool read_only_;
    std::string name_;

    std::vector<uint8_t> storage_;
    std::filesystem::path backing_path_{};
    bool dirty_{false};

    PFlashMode mode_{PFlashMode::ReadArray};
    uint8_t status_reg_{0x80};  // Ready bit set
};

}  // namespace simrv::device
