/**
 * @file StateControl.hpp
 * @brief SimRV state control helper interfaces.
 */
#pragma once

#include <array>
#include <atomic>
#include <utility>

#include "simrv/memory/Mmio.hpp"
#include "simrv/memory/TileLinkNode.hpp"

namespace simrv::core {

class CPU;
struct ArchState;

class PlicMmio;

/**
 * @class InterruptController
 * @brief Encapsulates interrupt pending/enable updates for CPU state.
 */
class InterruptController {
   public:
    static void updateMip(PlicMmio& plic, ArchState& state);
    static void setIrq(PlicMmio& plic, int irq_num, int state_val);
};

/**
 * @class PlicMmio
 * @brief Platform-level interrupt controller MMIO front-end.
 */
class PlicMmio : public memory::TileLinkNode {
   public:
    friend class InterruptController;
    explicit PlicMmio(CPU& cpu) : cpu_(cpu) {}

    static constexpr Address kBaseAddress = simrv::mmio::kPlicBaseAddress;
    static constexpr Address kSize = simrv::mmio::kPlicSize;

    [[nodiscard]] auto name() const -> const char* final { return "plic"; }
    [[nodiscard]] auto base_address() const -> Address final { return kBaseAddress; }
    [[nodiscard]] auto size() const -> Address final { return kSize; }
    auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool final;

    [[nodiscard]] constexpr auto contains(Address addr) const -> bool {
        return addr >= kBaseAddress && addr < (kBaseAddress + kSize);
    }
    [[nodiscard]] constexpr auto offset(Address addr) const -> Address {
        return addr - kBaseAddress;
    }

    auto mmio_read(Address offset) -> Word;
    void mmio_write(Address offset, Word wdata);

    // Pending interrupts (1 bit per IRQ). Index 0 holds IRQs 0-31, etc.
    std::array<Word, 32> plic_pending{};

    // Backing storage for PLIC registers to support OpenSBI drivers
    std::array<Word, 1024> plic_priorities{};

    // plic_enables[context][word_idx]. Support up to 2 contexts (M-mode, S-mode).
    std::array<std::array<Word, 32>, 2> plic_enables{};
    std::array<Word, 2> plic_threshold{};
    std::array<Word, 2> plic_claim{};  // The current claim value per context

   private:
    CPU& cpu_;
    auto get_context_for_offset(Address offset) const -> int;
};

/**
 * @class ClintMmio
 * @brief CLINT timer/software-interrupt MMIO front-end.
 */
class ClintMmio : public memory::TileLinkNode {
   public:
    explicit ClintMmio(CPU& cpu) : cpu_(cpu) {}

    static constexpr Address kBaseAddress = simrv::mmio::kClintBaseAddress;
    static constexpr Address kSize = simrv::mmio::kClintSize;

    [[nodiscard]] auto name() const -> const char* final { return "clint"; }
    [[nodiscard]] auto base_address() const -> Address final { return kBaseAddress; }
    [[nodiscard]] auto size() const -> Address final { return kSize; }
    auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool final;

    [[nodiscard]] constexpr auto contains(Address addr) const -> bool {
        return addr >= kBaseAddress && addr < (kBaseAddress + kSize);
    }
    [[nodiscard]] constexpr auto offset(Address addr) const -> Address {
        return addr - kBaseAddress;
    }

    [[nodiscard]] auto mmio_read(Address offset) const -> Word;
    void mmio_write(Address offset, Word wdata);

    std::atomic<Counter> mtime{1};
    std::atomic<Counter> mtimecmp{0};
    Counter mcycle{1};
    int rtc_divider{0};

   private:
    CPU& cpu_;
};

/**
 * @class TrapController
 * @brief Handles trap entry/return sequencing and cause propagation.
 */
class TrapController {
   public:
    /**
     * @brief Executes Machine-mode trap return (MRET).
     *
     * Restores privilege level from MPP, re-enables interrupt enabling bit MIE from MPIE,
     * resets MPIE to 1, sets MPP to U-mode, and updates program counter from MEPC.
     *
     * @param state Reference to architectural state.
     */
    static void mret(ArchState& state);

    /**
     * @brief Executes Supervisor-mode trap return (SRET).
     *
     * Restores privilege level from SPP, re-enables SIE from SPIE, resets SPIE to 1,
     * sets SPP to U-mode, clears MPRV, and updates program counter from SEPC.
     *
     * @param state Reference to architectural state.
     */
    static void sret(ArchState& state);

    /**
     * @brief Traps CPU execution to machine or supervisor handler for an exception or interrupt.
     *
     * Evaluates delegation vectors (medeleg/mideleg), populates cause (mcause/scause),
     * faulting address or payload (mtval/stval), saves return address (mepc/sepc), and updates
     * privilege level to handler target mode.
     *
     * @param cpu Reference to CPU instance.
     * @param cause Hardware trap cause code (interrupt or exception).
     * @param tval Auxiliary trap value (e.g. faulting virtual address or instruction encoding).
     */
    static void raiseException(CPU& cpu, TrapCause cause, CSRValue tval);

    /**
     * @brief Validates if the current privilege level is sufficient to execute a given privileged
     * instruction.
     *
     * Checks requirements for instruction variants like mret, sret, or sfence.vma based on current
     * privilege, MISA extensions, and TSR/TVM bits of mstatus.
     *
     * @param current_priv The CPU's current privilege level.
     * @param misa The current machine ISA configuration (MISA).
     * @param mstatus The current status register (mstatus).
     * @param funct12 The 12-bit privileged function field of the system instruction.
     * @param funct7 The 7-bit function field (used for SFENCE.VMA).
     * @return true if the instruction is allowed under current privilege, false otherwise.
     */
    static auto canExecutePrivilegedInstruction(PrivilegeLevel current_priv, CSRValue misa,
                                                CSRValue mstatus, Instruction funct12, Word funct7)
        -> bool;

    /**
     * @brief Validates if the current privilege level has access to a specific CSR.
     *
     * Checks if the CPU has the privilege to read/write the requested CSR address,
     * including read-only restrictions.
     *
     * @param current_priv The CPU's current privilege level.
     * @param csr_addr The address of the CSR.
     * @param is_write True if this is a write or read-write access to the CSR.
     * @return true if access is permitted, false if it triggers an illegal instruction exception.
     */
    static auto canAccessCsr(PrivilegeLevel current_priv, CSRAddress csr_addr, bool is_write)
        -> bool;
};

}  // namespace simrv::core
