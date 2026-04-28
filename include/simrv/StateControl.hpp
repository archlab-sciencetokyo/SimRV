/**
 * @file StateControl.hpp
 * @brief SimRV state control helper interfaces.
 */
#pragma once

#include "Define.hpp"
#include "MmioDevice.hpp"

class CPU;
class Machine;

/**
 * @class TlbUnit
 * @brief Wrapper for TLB maintenance operations.
 */
class TlbUnit {
   public:
    explicit TlbUnit(CPU& cpu) : cpu_(cpu) {}

    void flush();

   private:
    CPU& cpu_;
};

/**
 * @class InterruptController
 * @brief Encapsulates interrupt pending/enable updates for CPU state.
 */
class InterruptController {
   public:
    explicit InterruptController(CPU& cpu) : cpu_(cpu) {}

    void updateMip();
    void setIrq(int irq_num, int state);

   private:
    CPU& cpu_;
};

/**
 * @class PlicMmio
 * @brief Platform-level interrupt controller MMIO front-end.
 */
class PlicMmio : public MmioDevice {
   public:
    explicit PlicMmio(CPU& cpu) : cpu_(cpu) {}

    static constexpr Address kBaseAddress = simrv::mmio::kPlicBaseAddress;
    static constexpr Address kSize = simrv::mmio::kPlicSize;

    [[nodiscard]] auto name() const -> const char* override { return "plic"; }
    [[nodiscard]] auto base_address() const -> Address override { return kBaseAddress; }
    [[nodiscard]] auto size() const -> Address override { return kSize; }
    auto read(Machine& machine, Address p_addr, Word& rdata) -> bool override;
    auto write(Machine& machine, Address p_addr, Word wdata) -> bool override;

    [[nodiscard]] constexpr auto contains(Address addr) const -> bool {
        return addr >= kBaseAddress && addr < (kBaseAddress + kSize);
    }
    [[nodiscard]] constexpr auto offset(Address addr) const -> Address {
        return addr - kBaseAddress;
    }

    auto mmio_read(Address offset) -> Word;
    void mmio_write(Address offset, Word wdata);

   private:
    CPU& cpu_;
};

/**
 * @class ClintMmio
 * @brief CLINT timer/software-interrupt MMIO front-end.
 */
class ClintMmio : public MmioDevice {
   public:
    explicit ClintMmio(CPU& cpu) : cpu_(cpu) {}

    static constexpr Address kBaseAddress = simrv::mmio::kClintBaseAddress;
    static constexpr Address kSize = simrv::mmio::kClintSize;

    [[nodiscard]] auto name() const -> const char* override { return "clint"; }
    [[nodiscard]] auto base_address() const -> Address override { return kBaseAddress; }
    [[nodiscard]] auto size() const -> Address override { return kSize; }
    auto read(Machine& machine, Address p_addr, Word& rdata) -> bool override;
    auto write(Machine& machine, Address p_addr, Word wdata) -> bool override;

    [[nodiscard]] constexpr auto contains(Address addr) const -> bool {
        return addr >= kBaseAddress && addr < (kBaseAddress + kSize);
    }
    [[nodiscard]] constexpr auto offset(Address addr) const -> Address {
        return addr - kBaseAddress;
    }

    [[nodiscard]] auto mmio_read(Address offset) const -> Word;
    void mmio_write(Address offset, Word wdata);

   private:
    CPU& cpu_;
};

/**
 * @class TrapController
 * @brief Handles trap entry/return sequencing and cause propagation.
 */
class TrapController {
   public:
    explicit TrapController(CPU& cpu) : cpu_(cpu) {}

    void mret();
    void sret();
    void raiseException(TrapCause cause, CSRValue tval);

   private:
    CPU& cpu_;
};
