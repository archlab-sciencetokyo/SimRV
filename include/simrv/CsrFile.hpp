/**
 * @file CsrFile.hpp
 * @brief SimRV CSR register-file interface.
 */
#pragma once

#include "XLen.hpp"
class CPU;

/**
 * @class CsrFile
 * @brief Encapsulates CSR read/write semantics and side effects.
 */
class CsrFile {
   public:
    /**
     * @brief Construct CSR interface bound to CPU architectural state.
     * @param cpu CPU state owner.
     */
    explicit CsrFile(CPU& cpu) : cpu_(cpu) {}

    /// Read projected mstatus value under a visibility mask.
    [[nodiscard]] auto getMstatus(CSRValue mask) const -> CSRValue;
    /// Write mstatus while applying required architectural side effects.
    void setMstatus(CSRValue wdata);
    /// Read CSR value by CSR address.
    [[nodiscard]] auto read(CSRAddress addr) const -> CSRValue;
    /// Write CSR value by CSR address.
    void write(CSRAddress addr, CSRValue wdata);  // NOLINT(bugprone-easily-swappable-parameters)

   private:
    CPU& cpu_;
};
