/**
 * @file AIA.hpp
 * @brief RISC-V Advanced Interrupt Architecture (AIA): APLIC and IMSIC.
 */
#pragma once

#include <array>
#include <cstdint>

#include "simrv/memory/MmioDevice.hpp"
#include "simrv/xlen/IdentifierTypes.hpp"

namespace simrv::core {
class Machine;
class CPU;
class IInterruptController;
using IInterruptSink = IInterruptController;
}  // namespace simrv::core

namespace simrv::device {

/**
 * @class Imsic
 * @brief RISC-V AIA Incoming Message-Signaled Interrupt Controller.
 */
class Imsic : public memory::MmioDevice {
   public:
    static constexpr size_t kMaxHarts = 16;
    static constexpr size_t kNumInterrupts = 64;  // 1..63

    enum class Privilege : uint8_t { Machine = 0, Supervisor = 1 };

    Imsic(simrv::core::Machine* machine, Privilege priv, simrv::xlen::Address base_addr,
          simrv::xlen::Address size);
    ~Imsic() override = default;

    void set_sink(simrv::core::IInterruptController* sink) noexcept { sink_ = sink; }

    [[nodiscard]] auto name() const -> const char* override {
        return priv_ == Privilege::Machine ? "imsic-m" : "imsic-s";
    }
    [[nodiscard]] auto base_address() const -> simrv::xlen::Address override { return base_addr_; }
    [[nodiscard]] auto size() const -> simrv::xlen::Address override { return size_; }
    void reset() override;

    [[nodiscard]] auto read32(simrv::xlen::Address offset) -> uint32_t override;
    void write32(simrv::xlen::Address offset, uint32_t val) override;

    // Indirect CSR register access via miselect/mireg or siselect/sireg
    [[nodiscard]] auto csr_read(simrv::xlen::HartId hart_idx, uint32_t reg_idx)
        -> simrv::xlen::Word;
    void csr_write(simrv::xlen::HartId hart_idx, uint32_t reg_idx, simrv::xlen::Word val);

    // Trigger an MSI interrupt message on a specific hart
    void trigger_msi(simrv::xlen::HartId hart_idx, simrv::xlen::InterruptSourceId interrupt_id);

    // Recalculate pending & enabled interrupts against threshold and update CPU mip
    void update_hart(simrv::xlen::HartId hart_idx);

   private:
    simrv::core::IInterruptController* sink_{nullptr};
    Privilege priv_{Privilege::Machine};
    simrv::xlen::Address base_addr_{0};
    simrv::xlen::Address size_{0};

    struct HartInterruptFile {
        uint32_t eidelivery{0};   // 0 = disabled, 1 = enabled
        uint32_t eithreshold{0};  // priority threshold (0 = all delivered)
        uint64_t eip{0};          // pending bits (1..63)
        uint64_t eie{0};          // enable bits (1..63)
    };

    std::array<HartInterruptFile, kMaxHarts> files_{};
};

/**
 * @class Aplic
 * @brief RISC-V AIA Advanced Platform-Level Interrupt Controller.
 */
class Aplic : public memory::MmioDevice {
   public:
    static constexpr size_t kMaxSources = 64;  // Sources 1..63
    enum class Privilege : uint8_t { Machine = 0, Supervisor = 1 };

    Aplic(simrv::core::Machine* machine, Privilege priv, simrv::xlen::Address base_addr,
          simrv::xlen::Address size, Imsic* imsic = nullptr);
    ~Aplic() override = default;

    void set_sink(simrv::core::IInterruptController* sink) noexcept { sink_ = sink; }
    void set_act_test_hooks(bool enabled) noexcept { act_test_hooks_ = enabled; }

    [[nodiscard]] auto name() const -> const char* override {
        return priv_ == Privilege::Machine ? "aplic-m" : "aplic-s";
    }
    [[nodiscard]] auto base_address() const -> simrv::xlen::Address override { return base_addr_; }
    [[nodiscard]] auto size() const -> simrv::xlen::Address override { return size_; }
    void reset() override;

    [[nodiscard]] auto read32(simrv::xlen::Address offset) -> uint32_t override;
    void write32(simrv::xlen::Address offset, uint32_t val) override;

    // External device wire IRQ trigger
    void set_irq(simrv::xlen::InterruptSourceId irq_source, bool active);

    // Update pending states and delivery to CPU/IMSIC
    void update();

   private:
    simrv::core::IInterruptController* sink_{nullptr};
    Privilege priv_{Privilege::Machine};
    bool act_test_hooks_{false};
    simrv::xlen::Address base_addr_{0};
    simrv::xlen::Address size_{0};
    Imsic* imsic_{nullptr};

    uint32_t domaincfg_{0};  // bit 2: DM (0=Direct, 1=MSI), bit 8: IE
    uint64_t mmsiaddrcfg_{0};
    uint64_t smsiaddrcfg_{0};

    std::array<uint32_t, kMaxSources> sourcecfg_{};
    std::array<uint32_t, kMaxSources> target_{};
    std::array<bool, kMaxSources> input_wires_{};

    uint64_t pending_{0};  // Bits 1..63
    uint64_t enabled_{0};  // Bits 1..63
};

}  // namespace simrv::device
