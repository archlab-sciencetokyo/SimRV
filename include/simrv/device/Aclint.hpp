/**
 * @file Aclint.hpp
 * @brief RISC-V Advanced Core Local Interruptor (ACLINT) devices.
 */
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "simrv/memory/Mmio.hpp"
#include "simrv/memory/MmioDevice.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class Machine;
class CPU;
}  // namespace simrv::core

namespace simrv::device {

/**
 * @class AclintMtimer
 * @brief RISC-V Standard ACLINT MTIMER device providing mtime and mtimecmp.
 */
class AclintMtimer : public memory::MmioDevice {
   public:
    static constexpr Address kBaseAddress = mmio::kAclintMtimerBaseAddress;
    static constexpr Address kSize = mmio::kAclintMtimerSize;
    static constexpr size_t kMaxHarts = 16;

    explicit AclintMtimer(simrv::core::Machine* machine = nullptr);
    ~AclintMtimer() override = default;

    [[nodiscard]] auto name() const -> const char* override { return "aclint-mtimer"; }
    [[nodiscard]] auto base_address() const -> Address override { return kBaseAddress; }
    [[nodiscard]] auto size() const -> Address override { return kSize; }
    void reset() override;

    [[nodiscard]] auto read32(Address offset) -> uint32_t override;
    [[nodiscard]] auto read64(Address offset) -> uint64_t override;
    void write32(Address offset, uint32_t val) override;
    void write64(Address offset, uint64_t val) override;

    void tick();
    [[nodiscard]] auto mtime() const -> uint64_t { return mtime_; }
    void set_mtime(uint64_t val) { mtime_ = val; }

   private:
    uint64_t mtime_{0};
    std::array<std::atomic<uint64_t>, kMaxHarts> mtimecmp_{};
};

/**
 * @class AclintMswi
 * @brief RISC-V Standard ACLINT MSWI device providing Machine-level Software Interrupts.
 */
class AclintMswi : public memory::MmioDevice {
   public:
    static constexpr Address kBaseAddress = mmio::kAclintMswiBaseAddress;
    static constexpr Address kSize = mmio::kAclintMswiSize;
    static constexpr size_t kMaxHarts = 16;

    explicit AclintMswi(simrv::core::Machine* machine = nullptr);
    ~AclintMswi() override = default;

    [[nodiscard]] auto name() const -> const char* override { return "aclint-mswi"; }
    [[nodiscard]] auto base_address() const -> Address override { return kBaseAddress; }
    [[nodiscard]] auto size() const -> Address override { return kSize; }
    void reset() override;

    [[nodiscard]] auto read32(Address offset) -> uint32_t override;
    void write32(Address offset, uint32_t val) override;

   private:
    std::array<std::atomic<uint32_t>, kMaxHarts> msip_{};
};

}  // namespace simrv::device
