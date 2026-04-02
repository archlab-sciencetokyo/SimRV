/**
 * @file StateControl.hpp
 * @brief SimRV state control helper interfaces.
 */
#pragma once

#include "Define.hpp"
#include "MmioDevice.hpp"

class CPU;
class Machine;

class TlbUnit {
   public:
    explicit TlbUnit(CPU& cpu) : cpu_(cpu) {}

    void flush();

   private:
    CPU& cpu_;
};

class InterruptController {
   public:
    explicit InterruptController(CPU& cpu) : cpu_(cpu) {}

    void updateMip();
    void setIrq(int irq_num, int state);

   private:
    CPU& cpu_;
};

class PlicMmio : public MmioDevice {
   public:
    explicit PlicMmio(CPU& cpu) : cpu_(cpu) {}

    static constexpr Address kBaseAddress = simrv::mmio::kPlicBaseAddress;
    static constexpr Address kSize = simrv::mmio::kPlicSize;

    const char* name() const override { return "plic"; }
    Address base_address() const override { return kBaseAddress; }
    Address size() const override { return kSize; }
    bool read(Machine& machine, Address p_addr, Word& rdata) override;
    bool write(Machine& machine, Address p_addr, Word wdata) override;

    constexpr bool contains(Address addr) const {
        return addr >= kBaseAddress && addr < (kBaseAddress + kSize);
    }
    constexpr Address offset(Address addr) const { return addr - kBaseAddress; }

    Word mmio_read(Address offset);
    void mmio_write(Address offset, Word wdata);

   private:
    CPU& cpu_;
};

class ClintMmio : public MmioDevice {
   public:
    explicit ClintMmio(CPU& cpu) : cpu_(cpu) {}

    static constexpr Address kBaseAddress = simrv::mmio::kClintBaseAddress;
    static constexpr Address kSize = simrv::mmio::kClintSize;

    const char* name() const override { return "clint"; }
    Address base_address() const override { return kBaseAddress; }
    Address size() const override { return kSize; }
    bool read(Machine& machine, Address p_addr, Word& rdata) override;
    bool write(Machine& machine, Address p_addr, Word wdata) override;

    constexpr bool contains(Address addr) const {
        return addr >= kBaseAddress && addr < (kBaseAddress + kSize);
    }
    constexpr Address offset(Address addr) const { return addr - kBaseAddress; }

    Word mmio_read(Address offset);
    void mmio_write(Address offset, Word wdata);

   private:
    CPU& cpu_;
};

class TrapController {
   public:
    explicit TrapController(CPU& cpu) : cpu_(cpu) {}

    void mret();
    void sret();
    void raiseException(TrapCause cause, CSRValue tval);

   private:
    CPU& cpu_;
};
