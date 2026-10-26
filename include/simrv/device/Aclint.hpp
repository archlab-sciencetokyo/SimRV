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
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {
class Machine;
class CPU;
class IInterruptController;
using IInterruptSink = IInterruptController;
}  // namespace simrv::core

namespace simrv::device {

/**
 * @class AclintMtimer
 * @brief RISC-V Standard ACLINT MTIMER device providing mtime and mtimecmp.
 */
class AclintMtimer : public memory::MmioDevice {
   public:
    static constexpr simrv::xlen::Address kBaseAddress = mmio::kAclintMtimerBaseAddress;
    static constexpr simrv::xlen::Address kSize = mmio::kAclintMtimerSize;
    static constexpr size_t kMaxHarts = 16;

    explicit AclintMtimer(simrv::core::Machine* machine = nullptr);
    explicit AclintMtimer(simrv::core::IInterruptController* sink,
                          simrv::core::Machine* machine = nullptr);
    ~AclintMtimer() override = default;

    void set_sink(simrv::core::IInterruptController* sink) noexcept { sink_ = sink; }

    [[nodiscard]] auto name() const -> const char* override { return "aclint-mtimer"; }
    [[nodiscard]] auto base_address() const -> simrv::xlen::Address override {
        return kBaseAddress;
    }
    [[nodiscard]] auto size() const -> simrv::xlen::Address override { return kSize; }
    void reset() override;

    [[nodiscard]] auto read32(simrv::xlen::Address offset) -> uint32_t override;
    [[nodiscard]] auto read64(simrv::xlen::Address offset) -> uint64_t override;
    void write32(simrv::xlen::Address offset, uint32_t val) override;
    void write64(simrv::xlen::Address offset, uint64_t val) override;

    void tick();
    [[nodiscard]] auto mtime() const -> simrv::xlen::Counter { return mtime_; }
    void set_mtime(simrv::xlen::Counter val) { mtime_ = val; }

   private:
    simrv::core::IInterruptController* sink_{nullptr};
    simrv::xlen::Counter mtime_{0};
    std::array<std::atomic<simrv::xlen::Counter>, kMaxHarts> mtimecmp_{};
};

/**
 * @class AclintMswi
 * @brief RISC-V Standard ACLINT MSWI device providing Machine-level Software Interrupts.
 */
class AclintMswi : public memory::MmioDevice {
   public:
    static constexpr simrv::xlen::Address kBaseAddress = mmio::kAclintMswiBaseAddress;
    static constexpr simrv::xlen::Address kSize = mmio::kAclintMswiSize;
    static constexpr size_t kMaxHarts = 16;

    explicit AclintMswi(simrv::core::Machine* machine = nullptr);
    explicit AclintMswi(simrv::core::IInterruptController* sink,
                        simrv::core::Machine* machine = nullptr);
    ~AclintMswi() override = default;

    void set_sink(simrv::core::IInterruptController* sink) noexcept { sink_ = sink; }

    [[nodiscard]] auto name() const -> const char* override { return "aclint-mswi"; }
    [[nodiscard]] auto base_address() const -> simrv::xlen::Address override {
        return kBaseAddress;
    }
    [[nodiscard]] auto size() const -> simrv::xlen::Address override { return kSize; }
    void reset() override;

    [[nodiscard]] auto read32(simrv::xlen::Address offset) -> uint32_t override;
    void write32(simrv::xlen::Address offset, uint32_t val) override;

   private:
    simrv::core::IInterruptController* sink_{nullptr};
    std::array<std::atomic<uint32_t>, kMaxHarts> msip_{};
};

}  // namespace simrv::device
